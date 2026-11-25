// Levels 2 to 5 through choose_stroke (spec 10.1, 10.2; docs/opponent/README.md "Stage 2", "The
// order", "Budget"): every stroke is one the engine accepts, a decision is ordered claim, tactic,
// pots, the weak pot gives way to a tactic, and a logged decision is made again from its steps.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "ofs/opponent/budget.hpp"
#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
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

std::vector<const Level*> tactical() {
    const rig::Levels& l = rig::levels();
    return {&l.club, &l.amateur, &l.pro, &l.world};
}

Think sims(int n) {
    Think t;
    t.max_sims = n;
    return t;
}

Level sharp(const Level& level) {
    Level l = level;
    l.judgement_sigma = 0.0;
    return l;
}

FrameState pack() {
    FrameState s = rig::racked(1);
    s.cue_in_hand = false;
    return s;
}

FrameState touching() {
    FrameState s = rig::position("t3_touching");
    const std::vector<int> on = rules::places_on(s);
    for (int place : rig::env().oracle.touching(s.balls, s.cue_ball)) {
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

const char* const kNamed[] = {
    "straight_red",     "cut_red",          "long_red",          "colours",          "black_only",
    "last_red",         "after_red",        "in_hand",           "snookered",        "racked",
    "pack",             "t3_cut_tempting",  "t3_escape_one",     "t3_escape_two",    "t3_escape_ring",
    "t3_snooker_pink",  "t3_cushion_tight", "t3_touching",       "t3_cluster_middle", "t3_long_ahead",
    "t3_colours_ahead", "t3_colours_close"};

// Positions a few stand-in strokes into frames from the rack, nothing pending.
const std::vector<FrameState>& played() {
    static const std::vector<FrameState> out = [] {
        const rules::scenario::Rig& r = rules::scenario::rig();
        std::vector<FrameState> v;
        for (std::uint64_t seed = 11; seed <= 14; ++seed) {
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

std::vector<FrameState> every_position() {
    std::vector<FrameState> v;
    for (const std::string name : kNamed) v.push_back(named(name));
    for (const FrameState& s : played()) v.push_back(s);
    return v;
}

Stroke choose(const FrameState& s, const Level& level, std::uint64_t seed = 1, const Think& think = sims(kSims)) {
    Rng rng(seed);
    return choose_stroke(s, level, rig::env(), rng, think);
}

std::string text(const json& j, const char* key) {
    if (!j.is_object()) return "";
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

bool same_strike(const phys::CueStrike& a, const phys::CueStrike& b) {
    return a.aim_rad == b.aim_rad && a.speed_m_s == b.speed_m_s && a.tip_x == b.tip_x && a.tip_y == b.tip_y &&
           a.elevation_rad == b.elevation_rad;
}

bool finite(const phys::CueStrike& c) {
    return std::isfinite(c.aim_rad) && std::isfinite(c.speed_m_s) && std::isfinite(c.tip_x) &&
           std::isfinite(c.tip_y) && std::isfinite(c.elevation_rad);
}

bool offered(const std::vector<rules::DecisionKind>& options, rules::DecisionKind k) {
    return std::find(options.begin(), options.end(), k) != options.end();
}

bool tactic_kind(const std::string& kind) { return kind == "safety" || kind == "snooker" || kind == "escape"; }

void check_accepted(const FrameState& s, const Level& level, const Stroke& st) {
    const phys::PhysicsParams& params = rules::scenario::rig().params;
    REQUIRE(st.why.is_object());
    CAPTURE(st.why.dump());
    CHECK(text(st.why, "level") == level.id);
    const std::string kind = text(st.why, "kind");
    CHECK((st.stopped == "done" || st.stopped == "sims" || st.stopped == "steps" || st.stopped == "time" ||
           st.stopped == "cancelled"));
    CHECK(st.fallback.empty());
    if (st.decides) {
        CHECK(offered(rules::options(s, s.striker), st.decision));
        CHECK((kind == "claim" || kind == "concede"));
        return;
    }
    CHECK((kind == "pot" || tactic_kind(kind) || kind == "break"));
    if (level.tactics < 3) CHECK(kind != "snooker");
    CHECK(st.places_cue == s.cue_in_hand);
    if (st.places_cue) {
        CHECK(table::in_d(st.cue_pos));
        CHECK(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, st.cue_pos));
    }
    if (s.on.kind == rules::OnKind::any_colour || s.free_ball) {
        CHECK(st.input.has_nomination);
        if (st.input.has_nomination) {
            const std::vector<Ball> open = rules::nominations(s);
            CHECK(std::find(open.begin(), open.end(), st.input.nominated) != open.end());
        }
    }
    CHECK(finite(st.strike));
    CHECK(finite(st.meant));
    CHECK(st.strike.speed_m_s > 0.0);
    CHECK(st.strike.speed_m_s <= params.max_cue_speed_m_s);
    CHECK(std::hypot(st.strike.tip_x, st.strike.tip_y) <= kTipLimit * params.miscue_limit + 1e-12);

    FrameState f = s;
    std::string error;
    if (st.places_cue) REQUIRE(rules::place_cue_ball(f, st.cue_pos, rig::env().oracle, error));
    const rules::Judgement j = rules::scenario::play(f, st.strike, st.input).judgement;
    CHECK(j.post.strokes == s.strokes + 1);
}

// The reds, all fifteen, are at the top and no ball can be potted from where the cue ball is.
void require_no_pot(const FrameState& s) {
    for (const Level* level : tactical()) REQUIRE(pots(s, rig::env(), level->max_cut_rad).empty());
}

bool tactic_found(const FrameState& s, const Level& level) {
    Think think = sims(kSims);
    Budget budget(level, think);
    Rng judgement(1);
    return choose_tactic(s, level, rig::env(), budget, judgement).found;
}

}  // namespace

TEST_CASE("spec 10.2 step 6, opponent.hpp: every stroke of club, amateur, pro and world over thirty positions and more is one the engine accepts") {
    const std::vector<FrameState> positions = every_position();
    REQUIRE(positions.size() >= 30);
    for (const Level* level : tactical()) {
        int strokes = 0;
        for (std::size_t i = 0; i < positions.size(); ++i) {
            CAPTURE(level->id);
            CAPTURE(i);
            Stroke st;
            CHECK_NOTHROW(st = choose(positions[i], *level, 1 + i));
            check_accepted(positions[i], *level, st);
            ++strokes;
        }
        CHECK(strokes >= 30);
    }
}

TEST_CASE("README The order: the break-off of a level with tactics is the tactic's, kind break with its thickness") {
    const FrameState s = rig::racked(1);
    for (const Level* level : tactical()) {
        for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2)}) {
            CAPTURE(level->id);
            CAPTURE(seed);
            const Stroke st = choose(s, *level, seed);
            check_accepted(s, *level, st);
            CHECK(text(st.why, "kind") == "break");
            CHECK(st.why.contains("thickness"));
            CHECK(st.why.contains("leave"));
        }
    }
}

TEST_CASE("README The order: with no pot on the table a level with tactics plays the tactic, its why the tactic's with the level's fields") {
    const FrameState s = pack();
    require_no_pot(s);
    for (const Level* level : tactical()) {
        CAPTURE(level->id);
        const Stroke st = choose(s, *level);
        check_accepted(s, *level, st);
        CHECK(tactic_kind(text(st.why, "kind")));
        for (const std::string key : {"thickness", "leave", "level", "drawn", "maximum", "listed", "pots"}) {
            CAPTURE(key);
            CHECK(st.why.contains(key));
        }
        CHECK(text(st.why, "maximum") == "off");
    }
}

TEST_CASE("README The order, Worth: a long red under safety_below as the only pot gives way to the tactic") {
    const FrameState s = named("long_red");
    REQUIRE(pots(s, rig::env(), rig::levels().world.max_cut_rad).size() == 1);
    // The levels' own floors were tuned to under this red's chance: each takes one above it.
    std::vector<Level> held = {rig::levels().club, rig::levels().amateur, sharp(rig::levels().pro)};
    for (Level& floor : held) floor.safety_below = 0.3;
    for (const Level& held_level : held) {
        const Level* level = &held_level;
        const std::vector<Pot> listed = pots(s, rig::env(), level->max_cut_rad);
        REQUIRE(listed.size() == 1);
        CHECK(pot_chance(*level, listed[0], Spec{}, listed[0].margin_rad) < level->safety_below);
        REQUIRE(tactic_found(s, *level));
        for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2), std::uint64_t(3)}) {
            CAPTURE(level->id);
            CAPTURE(seed);
            const Stroke st = choose(s, *level, seed);
            check_accepted(s, *level, st);
            CHECK(text(st.why, "kind") != "pot");
            CHECK(tactic_kind(text(st.why, "kind")));
            CHECK(st.why.contains("thickness"));
        }
    }
}

TEST_CASE("README Budget: for each new level the steps it reported, given back as max_steps, give the same strike, kind and target") {
    for (const Level* level : tactical()) {
        for (const std::string name : {"straight_red", "pack", "racked", "colours"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            const FrameState s = named(name);
            const Stroke whole = choose(s, *level, 4);
            REQUIRE(whole.steps > 0);
            Think t = sims(kSims);
            t.max_steps = whole.steps;
            const Stroke again = choose(s, *level, 4, t);
            CHECK(again.steps == whole.steps);
            CHECK(again.decides == whole.decides);
            CHECK(same_strike(again.strike, whole.strike));
            CHECK(same_strike(again.meant, whole.meant));
            CHECK(again.places_cue == whole.places_cue);
            CHECK(again.cue_pos.x == whole.cue_pos.x);
            CHECK(again.cue_pos.y == whole.cue_pos.y);
            CHECK(text(again.why, "kind") == text(whole.why, "kind"));
            CHECK(text(again.why, "target") == text(whole.why, "target"));
        }
    }
}

TEST_CASE("README Stage 2: Beginner, tactics 0, still plays the stand-in's safety where there is no pot") {
    const Level& beginner = rig::levels().beginner;
    REQUIRE(beginner.tactics == 0);
    const FrameState s = pack();
    require_no_pot(s);
    for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2), std::uint64_t(3)}) {
        CAPTURE(seed);
        Rng rng(seed);
        const Stroke st = choose_stroke(s, beginner, rig::env(), rng);
        REQUIRE(st.why.is_object());
        CHECK(text(st.why, "kind") == "safety");
        CHECK_FALSE(st.why.contains("thickness"));
        CHECK(text(st.why, "level") == "beginner");
    }
}
