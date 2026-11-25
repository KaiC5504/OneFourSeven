// The strokes that are not pots (spec 10.2 step 1, tactics.hpp, docs/opponent/README.md
// "Tactics"): choose_tactic called on its own, each answer simulated as meant and judged by the
// referee. Written from the contract, never from tactics.cpp.

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ofs/opponent/budget.hpp"
#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/opponent/tactics.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using nlohmann::json;
using rules::Ball;
using rules::FrameState;

namespace {

constexpr int kSims = 400;

const Env& env() {
    static const PocketMouths mouths(rules::scenario::rig().table);
    static const Env e = [] {
        Env x = rig::env();
        x.mouths = &mouths;
        return x;
    }();
    return e;
}

std::vector<const Level*> tactical() {
    const rig::Levels& l = rig::levels();
    return {&l.club, &l.amateur, &l.pro, &l.world};
}

Think sims(int n) {
    Think t;
    t.max_sims = n;
    return t;
}

// Reds on the top half as a match racks them, the cue ball in baulk and not in hand: no pot.
FrameState pack() {
    FrameState s = rig::racked(1);
    s.cue_in_hand = false;
    return s;
}

FrameState touching() {
    FrameState s = rig::position("t3_touching");
    const std::vector<int> on = rules::places_on(s);
    for (int place : env().oracle.touching(s.balls, s.cue_ball)) {
        if (std::find(on.begin(), on.end(), place) != on.end()) s.touching.push_back(place);
    }
    return s;
}

FrameState named(const std::string& name) {
    if (name == "racked") return rig::racked(1);
    if (name == "pack") return pack();
    if (name == "t3_touching") return touching();
    return rig::position(name.c_str());
}

// A few strokes of the stand-in from the break-off, each position with nothing pending kept.
const std::vector<FrameState>& played() {
    static const std::vector<FrameState> out = [] {
        const rules::scenario::Rig& r = rules::scenario::rig();
        std::vector<FrameState> v;
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            FrameState s = rig::racked(seed);
            Rng rng(seed);
            for (int k = 0; k < 4; ++k) {
                const StandinShot shot = choose_shot(s, r.table, r.params, *r.oracle, r.ref, rig::levels().standin, rng);
                if (shot.decides) break;
                std::string error;
                if (shot.places_cue && !rules::place_cue_ball(s, shot.cue_pos, *r.oracle, error)) break;
                s = rules::scenario::play(s, shot.strike, shot.input, seed).judgement.post;
                if (s.pending == rules::Pending::after_foul) {
                    std::vector<rules::Call> calls;
                    if (!rules::decide(s, {rules::DecisionKind::play, s.decider}, *r.oracle, r.ref, calls, error)) break;
                }
                if (s.over || s.pending != rules::Pending::none) break;
                v.push_back(s);
            }
        }
        return v;
    }();
    return out;
}

struct Run {
    Tactic t;
    int steps = 0;
    int sims = 0;
    std::string stopped;
    bool halted = false;
};

Run run(const FrameState& s, const Level& level, Think think = sims(kSims), std::uint64_t seed = 1) {
    Budget budget(level, think);
    Rng judgement(seed);
    Run r;
    r.t = choose_tactic(s, level, env(), budget, judgement);
    r.steps = budget.steps;
    r.sims = budget.sims;
    r.stopped = budget.stopped;
    r.halted = budget.halted();
    return r;
}

const Run& cached(const std::string& name, const Level& level) {
    static std::map<std::pair<std::string, std::string>, Run> cache;
    const auto key = std::make_pair(name, level.id);
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, run(named(name), level)).first;
    return it->second;
}

// The tactic as meant: placed, simulated once with solve.hpp's trial, judged by the referee.
struct Outcome {
    bool placed = false;
    Trial trial;
    rules::Judgement judgement;
};

Outcome outcome(const FrameState& s, const Tactic& t) {
    Outcome o;
    FrameState f = s;
    std::string error;
    if (t.places_cue && !rules::place_cue_ball(f, t.cue_pos, env().oracle, error)) return o;
    o.placed = true;
    phys::ShotRecord record;
    o.trial = try_strike(f, f.balls[std::size_t(f.cue_ball)].pos, t.meant, env(), &record);
    o.judgement = rules::judge_shot(f, t.input, record, env().table, env().oracle, env().ref);
    return o;
}

std::string text(const json& j, const char* key) {
    if (!j.is_object()) return "";
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

bool flag(const json& j, const char* key, bool otherwise) {
    if (!j.is_object()) return otherwise;
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : otherwise;
}

bool in_set(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

bool can_see_ball_on(const FrameState& s) {
    return s.cue_in_hand || env().oracle.clear_path(s.balls, s.cue_ball, rules::places_on(s));
}

bool snookered_after(const rules::Judgement& j) {
    const FrameState& p = j.post;
    return env().oracle.snookered(p.balls, p.cue_ball, rules::places_on(p), p.cue_in_hand);
}

int dearest_on_table(const FrameState& s) {
    int most = 4;
    for (const rules::BallOnTable& b : s.balls) most = std::max(most, table::ball_value(b.ball));
    return most;
}

// Everything README "Tactics" says any answer must be, from its own fields and the referee's view
// of it played as meant.
void check_answer(const FrameState& s, const Level& level, const Tactic& t) {
    const phys::PhysicsParams& params = rules::scenario::rig().params;
    CAPTURE(t.kind);
    CAPTURE(t.why.dump());
    CHECK((t.kind == "safety" || t.kind == "snooker" || t.kind == "escape" || t.kind == "break"));
    REQUIRE(t.why.is_object());
    CHECK(text(t.why, "kind") == t.kind);
    if (level.tactics < 3) CHECK(t.kind != "snooker");
    CHECK(t.places_cue == s.cue_in_hand);
    if (t.places_cue) {
        CHECK(table::in_d(t.cue_pos));
        CHECK(env().oracle.cue_placement_legal(s.balls, s.cue_ball, t.cue_pos));
    }
    if (s.on.kind == rules::OnKind::any_colour || s.free_ball) {
        CHECK(t.input.has_nomination);
        if (t.input.has_nomination) {
            const std::vector<Ball> open = rules::nominations(s);
            CHECK(std::find(open.begin(), open.end(), t.input.nominated) != open.end());
        }
    }
    CHECK(std::isfinite(t.meant.aim_rad));
    CHECK(t.meant.speed_m_s > 0.0);
    CHECK(t.meant.speed_m_s <= params.max_cue_speed_m_s);
    const double tip = std::hypot(t.meant.tip_x, t.meant.tip_y);
    CHECK(tip <= kTipLimit * params.miscue_limit + 1e-12);
    CHECK(tip <= level.max_tip + 1e-12);
    if (level.tactics == 1) {
        // Tactics 1 is thin to half ball at a few speeds: centre ball (README "Tactics").
        CHECK(t.meant.tip_x == 0.0);
        CHECK(t.meant.tip_y == 0.0);
    }
    REQUIRE(t.why.contains("spec"));
    const json& spec = t.why["spec"];
    REQUIRE(spec.contains("tip_x"));
    REQUIRE(spec.contains("speed_m_s"));
    CHECK(spec["tip_x"].get<double>() == t.meant.tip_x);
    CHECK(spec["speed_m_s"].get<double>() == t.meant.speed_m_s);
    REQUIRE(t.why.contains("worth"));
    CHECK(t.why["worth"].get<double>() == doctest::Approx(t.worth).epsilon(1e-12));

    const Outcome o = outcome(s, t);
    REQUIRE(o.placed);
    CHECK(o.trial.clean);
    const bool missed_escape = t.kind == "escape" && !flag(t.why, "hit", true);
    if (!missed_escape) {
        CAPTURE(o.judgement.fouls.size());
        CHECK(o.judgement.fouls.empty());
    }
    if (t.kind == "escape") {
        REQUIRE(t.why.contains("hit"));
        CHECK(t.why["hit"].is_boolean());
        REQUIRE(t.why.contains("cushions"));
        if (!missed_escape) CHECK(t.why["cushions"].get<int>() <= level.escape_cushions);
    }
    REQUIRE(t.why.contains("leave"));
    const json& leave = t.why["leave"];
    REQUIRE(leave.contains("snookered"));
    CHECK(leave["snookered"].is_boolean());
    if (leave.contains("cue") && leave["cue"].is_array() && leave["cue"].size() == 2 && !o.trial.rest.empty()) {
        const rules::Vec2 rest = o.trial.rest[std::size_t(s.cue_ball)];
        CHECK(leave["cue"][0].get<double>() == doctest::Approx(rest.x).epsilon(1e-9));
        CHECK(leave["cue"][1].get<double>() == doctest::Approx(rest.y).epsilon(1e-9));
    }
    if (t.kind == "snooker" || flag(leave, "snookered", false)) {
        CHECK(o.judgement.post.striker != s.striker);
        CHECK(snookered_after(o.judgement));
    }
    if (t.kind == "snooker") {
        REQUIRE(t.why.contains("gain"));
        const double gain = t.why["gain"].get<double>();
        CHECK(gain > 0.0);
        CHECK(gain <= double(dearest_on_table(s)) + 1e-12);
    }
}

const char* const kSweep[] = {"straight_red",      "cut_red",          "long_red",          "colours",
                              "black_only",        "after_red",        "in_hand",           "last_red",
                              "t3_cut_tempting",   "t3_snooker_pink",  "t3_cushion_tight",  "t3_touching",
                              "t3_cluster_middle", "pack"};

const char* const kSnookered[] = {"snookered", "t3_escape_one", "t3_escape_two", "t3_escape_ring"};

}  // namespace

TEST_CASE("README Tactics: with tactics 0 nothing is found and neither the budget nor the judgement stream moves") {
    Level no_tactics = rig::levels().club;
    no_tactics.tactics = 0;
    for (const Level* level : {&rig::levels().beginner, static_cast<const Level*>(&no_tactics)}) {
        for (const std::string name : {"pack", "long_red", "snookered", "racked", "after_red"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            const FrameState s = named(name);
            Think think;
            Budget budget(*level, think);
            Rng judgement(11);
            Rng copy = judgement;
            const Tactic t = choose_tactic(s, *level, env(), budget, judgement);
            CHECK_FALSE(t.found);
            CHECK(t.candidates == 0);
            CHECK(budget.steps == 0);
            CHECK(budget.sims == 0);
            CHECK_FALSE(budget.halted());
            CHECK(budget.stopped == "done");
            CHECK(judgement.next_u64() == copy.next_u64());
        }
    }
}

TEST_CASE("README Tactics: every level with tactics finds a stroke wherever a ball on can be hit, and it passes the referee") {
    for (const Level* level : tactical()) {
        for (const std::string name : kSweep) {
            CAPTURE(level->id);
            CAPTURE(name);
            const FrameState s = named(name);
            REQUIRE(can_see_ball_on(s));
            const Run& r = cached(name, *level);
            REQUIRE(r.t.found);
            check_answer(s, *level, r.t);
        }
    }
}

TEST_CASE("README Tactics: from positions a few strokes into a frame, whatever it answers passes the referee") {
    REQUIRE(played().size() >= 6);
    int found = 0;
    for (const Level* level : tactical()) {
        for (std::size_t i = 0; i < played().size(); ++i) {
            CAPTURE(level->id);
            CAPTURE(i);
            const FrameState& s = played()[i];
            // What passes the referee does not hang on the budget: a small one covers it.
            const Run r = run(s, *level, sims(150));
            if (can_see_ball_on(s) || level->escape_cushions > 0) {
                CHECK(r.t.found);
            }
            if (!r.t.found) continue;
            ++found;
            check_answer(s, *level, r.t);
        }
    }
    CHECK(found > 0);
}

TEST_CASE("README Tactics, safety: at tactics 1 it meets a ball on first, thin to half ball, and with the reds at the top the cue ball stops in the baulk half") {
    const Level& club = rig::levels().club;
    REQUIRE(club.tactics == 1);
    for (const std::string name : {"pack", "long_red"}) {
        CAPTURE(name);
        const FrameState s = named(name);
        REQUIRE(pots(s, env(), club.max_cut_rad).size() <= 1);
        const Run& r = cached(name, club);
        REQUIRE(r.t.found);
        CHECK(r.t.kind == "safety");
        const Outcome o = outcome(s, r.t);
        REQUIRE(o.placed);
        CHECK(in_set(rules::places_on(s), o.trial.first_hit));
        CHECK(o.judgement.fouls.empty());
        REQUIRE(r.t.why.contains("thickness"));
        const double thickness = r.t.why["thickness"].get<double>();
        CHECK(thickness >= 0.0);
        CHECK(thickness <= 0.5 + 1e-9);
        REQUIRE(o.trial.rest.size() == s.balls.size());
        CHECK(o.trial.rest[std::size_t(s.cue_ball)].x < 0.0);
        REQUIRE(r.t.why.contains("spec"));
        REQUIRE(r.t.why["spec"].contains("tip_x"));
        CHECK(r.t.why["spec"]["tip_x"].get<double>() == 0.0);
    }
}

TEST_CASE("README Tactics, snooker: only tactics 3 lays one, and on an easy one both levels that have it do") {
    const FrameState s = named("t3_snooker_pink");
    REQUIRE(s.on.kind == rules::OnKind::colour);
    REQUIRE(s.on.colour == Ball::pink);
    for (const Level* level : tactical()) {
        CAPTURE(level->id);
        const Run& r = cached("t3_snooker_pink", *level);
        REQUIRE(r.t.found);
        check_answer(s, *level, r.t);
        if (level->tactics >= 3) {
            CHECK(r.t.kind == "snooker");
        } else {
            CHECK(r.t.kind != "snooker");
        }
    }
}

TEST_CASE("README Tactics, escape: the escape fixtures are snookered as the rules see them, needing one, two and no cushions") {
    const rules::scenario::Rig& r = rules::scenario::rig();
    rules::RefParams one = r.ref;
    one.escape_max_cushions = 1;
    const auto one_cushion = rules::make_geometry_oracle(r.table, one);
    for (const std::string name : kSnookered) {
        CAPTURE(name);
        const FrameState s = named(name);
        const std::vector<int> on = rules::places_on(s);
        CHECK_FALSE(r.oracle->clear_path(s.balls, s.cue_ball, on));
        CHECK(r.oracle->snookered(s.balls, s.cue_ball, on, false));
    }
    const FrameState one_s = named("t3_escape_one");
    CHECK(one_cushion->can_hit(one_s.balls, one_s.cue_ball, rules::places_on(one_s)));
    const FrameState two = named("t3_escape_two");
    CHECK_FALSE(one_cushion->can_hit(two.balls, two.cue_ball, rules::places_on(two)));
    CHECK(r.oracle->can_hit(two.balls, two.cue_ball, rules::places_on(two)));
    const FrameState ring = named("t3_escape_ring");
    CHECK_FALSE(r.oracle->can_hit(ring.balls, ring.cue_ball, rules::places_on(ring)));
}

TEST_CASE("README Tactics, escape: with escape_cushions 0 and tactics under 3 nothing is found") {
    const Level& club = rig::levels().club;
    REQUIRE(club.escape_cushions == 0);
    REQUIRE(club.tactics < 3);
    Level two_tactics = rig::levels().amateur;
    two_tactics.escape_cushions = 0;
    for (const Level* level : {&club, static_cast<const Level*>(&two_tactics)}) {
        for (const std::string name : kSnookered) {
            CAPTURE(level->id);
            CAPTURE(name);
            const Run r = run(named(name), *level);
            CHECK_FALSE(r.t.found);
        }
    }
}

TEST_CASE("README Tactics, escape: off one cushion it hits the ball on, off no more cushions than allowed, and fouls nothing") {
    for (const Level* level : {&rig::levels().amateur, &rig::levels().pro, &rig::levels().world}) {
        REQUIRE(level->escape_cushions >= 1);
        for (const std::string name : {"snookered", "t3_escape_one"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            const FrameState s = named(name);
            const Run& r = cached(name, *level);
            REQUIRE(r.t.found);
            CHECK(r.t.kind == "escape");
            CHECK(flag(r.t.why, "hit", false));
            check_answer(s, *level, r.t);
            const Outcome o = outcome(s, r.t);
            CHECK(in_set(rules::places_on(s), o.trial.first_hit));
            CHECK_FALSE(rules::scenario::has_foul(o.judgement, rules::FoulKind::no_hit));
            CHECK_FALSE(rules::scenario::has_foul(o.judgement, rules::FoulKind::first_hit_not_on));
        }
    }
}

TEST_CASE("README Tactics, escape: where two cushions are needed, two allowed hit the ball on and one allowed hits it or says it did not") {
    const FrameState s = named("t3_escape_two");
    Level amateur_two = rig::levels().amateur;
    amateur_two.escape_cushions = 2;
    for (const Level* level : {&rig::levels().pro, static_cast<const Level*>(&amateur_two)}) {
        CAPTURE(level->id);
        const Run r = run(s, *level);
        REQUIRE(r.t.found);
        CHECK(r.t.kind == "escape");
        CHECK(flag(r.t.why, "hit", false));
        check_answer(s, *level, r.t);
    }
    const Level& amateur = rig::levels().amateur;
    REQUIRE(amateur.escape_cushions == 1);
    const Run& r = cached("t3_escape_two", amateur);
    REQUIRE(r.t.found);
    CHECK(r.t.kind == "escape");
    check_answer(s, amateur, r.t);
}

TEST_CASE("README Tactics, escape: when no stroke can hit the ball on it still answers, the nearest, with why.hit false") {
    const FrameState s = named("t3_escape_ring");
    for (const Level* level : {&rig::levels().amateur, &rig::levels().pro}) {
        CAPTURE(level->id);
        const Run& r = cached("t3_escape_ring", *level);
        REQUIRE(r.t.found);
        CHECK(r.t.kind == "escape");
        REQUIRE(r.t.why.contains("hit"));
        CHECK(r.t.why["hit"] == false);
        check_answer(s, *level, r.t);
    }
}

TEST_CASE("README Tactics, break-off: from the rack every level with tactics places in the D and meets a red first, no foul") {
    const FrameState s = rig::racked(1);
    REQUIRE(s.cue_in_hand);
    for (const Level* level : tactical()) {
        for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2)}) {
            CAPTURE(level->id);
            CAPTURE(seed);
            const Run r = run(s, *level, sims(kSims), seed);
            REQUIRE(r.t.found);
            CHECK(r.t.kind == "break");
            REQUIRE(r.t.places_cue);
            CHECK(table::in_d(r.t.cue_pos));
            const Outcome o = outcome(s, r.t);
            REQUIRE(o.placed);
            REQUIRE(o.trial.first_hit >= 0);
            CHECK(s.balls[std::size_t(o.trial.first_hit)].ball == Ball::red);
            CHECK(o.judgement.fouls.empty());
            CHECK(r.t.why.contains("thickness"));
            check_answer(s, *level, r.t);
        }
    }
}

TEST_CASE("README Tactics, break-off: a thin contact on an end red of the last row") {
    const FrameState s = rig::racked(1);
    REQUIRE(s.cue_in_hand);
    double back = -1e9;
    for (const rules::BallOnTable& b : s.balls) {
        if (b.ball == Ball::red) back = std::max(back, b.pos.x);
    }
    // The rack's rows are a ball's width apart less a little: half a radius parts them.
    int left = -1;
    int right = -1;
    for (int place = 0; place < int(s.balls.size()); ++place) {
        const rules::BallOnTable& b = s.balls[std::size_t(place)];
        if (b.ball != Ball::red || b.pos.x < back - 0.5 * table::kBallRadius) continue;
        if (left < 0 || b.pos.y > s.balls[std::size_t(left)].pos.y) left = place;
        if (right < 0 || b.pos.y < s.balls[std::size_t(right)].pos.y) right = place;
    }
    REQUIRE(left >= 0);
    REQUIRE(right != left);
    for (const Level* level : tactical()) {
        for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2)}) {
            CAPTURE(level->id);
            CAPTURE(seed);
            const Run r = run(s, *level, sims(kSims), seed);
            REQUIRE(r.t.found);
            REQUIRE(r.t.kind == "break");
            const Outcome o = outcome(s, r.t);
            REQUIRE(o.placed);
            CAPTURE(o.trial.first_hit);
            CHECK((o.trial.first_hit == left || o.trial.first_hit == right));
            REQUIRE(r.t.why.contains("thickness"));
            REQUIRE(r.t.why["thickness"].is_number());
            CHECK(r.t.why["thickness"].get<double>() <= 0.5);
        }
    }
}

// Skipped until the break-off is kept to the racked table; take the skip off once it is.
TEST_CASE("README Tactics, break-off: with the cue ball in hand and fifteen reds that are no longer a pack, the stroke is chosen as any other" *
          doctest::skip()) {
    FrameState s = rig::racked(1);
    REQUIRE(s.cue_in_hand);
    // The pack spread over the top half clear of the spots, as after an in-off a few strokes in.
    const double xs[] = {0.35, 0.62, 1.15};
    const double ys[] = {-0.6, -0.3, 0.0, 0.3, 0.6};
    int k = 0;
    for (rules::BallOnTable& b : s.balls) {
        if (b.ball != Ball::red) continue;
        b.pos = {xs[k / 5], ys[k % 5]};
        ++k;
    }
    REQUIRE(k == 15);
    s.strokes = 4;
    s.scores = {0, 4};
    REQUIRE(s.on.kind == rules::OnKind::red);
    for (const Level* level : tactical()) {
        for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2)}) {
            CAPTURE(level->id);
            CAPTURE(seed);
            const Run r = run(s, *level, sims(kSims), seed);
            REQUIRE(r.t.found);
            CHECK(r.t.kind != "break");
            CHECK(text(r.t.why, "kind") != "break");
            check_answer(s, *level, r.t);
        }
    }
}

TEST_CASE("tactics.hpp: it never throws on odd tables") {
    FrameState black_in_hand = rig::position("black_only");
    black_in_hand.cue_in_hand = true;
    black_in_hand.balls[std::size_t(black_in_hand.cue_ball)].pos = table::default_cue_ball_position();
    std::vector<FrameState> odd{rig::position("black_only"), black_in_hand,     touching(),
                                rig::position("t3_cushion_tight"), rig::position("t3_cluster_middle"), pack(),
                                rig::position("t3_escape_ring")};
    for (const Level* level : tactical()) {
        for (std::size_t i = 0; i < odd.size(); ++i) {
            CAPTURE(level->id);
            CAPTURE(i);
            Think think = sims(kSims);
            Budget budget(*level, think);
            Rng judgement(3);
            Tactic t;
            CHECK_NOTHROW(t = choose_tactic(odd[i], *level, env(), budget, judgement));
            if (t.found) check_answer(odd[i], *level, t);
        }
    }
}
