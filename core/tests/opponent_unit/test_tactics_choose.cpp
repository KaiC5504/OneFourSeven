// choose_tactic (docs/opponent/README.md "Tactics", "Worth", "The score"): the builder's own tests.
// Levels are copied with few tries where what is tested does not depend on how many.

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/budget.hpp"
#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/opponent/tactics.hpp"
#include "ofs/physics/cue.hpp"
#include "ofs/rules/engine.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::FrameState;

namespace {

Level with(const Level& base, int tries, double judgement_sigma = -1.0) {
    Level out = base;
    out.safety_tries = tries;
    if (judgement_sigma >= 0.0) out.judgement_sigma = judgement_sigma;
    return out;
}

struct Run {
    Tactic tactic;
    int steps = 0;
    int sims = 0;
    std::string stopped;
    std::uint64_t next = 0;  // the judgement stream's next number after the call
};

Run run(const FrameState& s, const Level& level, const Think& think = {}, std::uint64_t seed = 5) {
    Budget budget(level, think);
    Rng judgement(seed);
    Run out;
    out.tactic = choose_tactic(s, level, rig::env(), budget, judgement);
    out.steps = budget.steps;
    out.sims = budget.sims;
    out.stopped = budget.stopped;
    out.next = judgement.next_u64();
    return out;
}

struct Replayed {
    FrameState from;
    phys::ShotRecord record;
    Trial trial;
    rules::Judgement j;
};

Replayed replay(const FrameState& s, const Tactic& t) {
    const Env env = rig::env();
    Replayed out;
    out.from = s;
    std::string error;
    if (t.places_cue) REQUIRE(rules::place_cue_ball(out.from, t.cue_pos, env.oracle, error));
    const rules::Vec2 cue = t.places_cue ? t.cue_pos : s.balls[std::size_t(s.cue_ball)].pos;
    out.trial = try_strike(out.from, cue, t.meant, env, &out.record);
    out.j = rules::judge_shot(out.from, t.input, out.record, env.table, env.oracle, env.ref);
    return out;
}

int cushions_before_first_ball(const phys::ShotRecord& record, int cue) {
    int cushions = 0;
    for (const phys::ShotEvent& e : record.events) {
        if (e.kind == phys::EventKind::ball_ball && (e.a == cue || e.b == cue)) break;
        if ((e.kind == phys::EventKind::ball_line || e.kind == phys::EventKind::ball_arc) && e.a == cue) ++cushions;
    }
    return cushions;
}

bool planned_miss(const Tactic& t) { return t.kind == "escape" && !t.why.at("hit").get<bool>(); }

// The engine takes the stroke, the referee passes it (an escape's planned miss aside), and `why`
// says what the simulated stroke did.
void check_answer(const FrameState& s, const Tactic& t) {
    const Env env = rig::env();
    REQUIRE(t.found);
    CHECK(t.places_cue == s.cue_in_hand);
    if (s.on.kind == rules::OnKind::any_colour) {
        REQUIRE(t.input.has_nomination);
        const std::vector<rules::Ball> open = rules::nominations(s);
        CHECK(std::find(open.begin(), open.end(), t.input.nominated) != open.end());
    }
    CHECK(t.meant.speed_m_s >= kLeastSpeed_m_s);
    CHECK(t.meant.speed_m_s <= env.params.max_cue_speed_m_s);
    CHECK(std::hypot(t.meant.tip_x, t.meant.tip_y) <= kTipLimit * env.params.miscue_limit + 1e-12);
    CHECK(t.meant.elevation_rad <= env.params.max_elevation_rad);

    phys::TableState table;
    for (const rules::BallOnTable& b : s.balls) {
        phys::BallState st;
        st.ball = b.ball;
        st.index = b.index;
        st.r = {b.pos.x, b.pos.y, 0.0};
        table.balls.push_back(st);
    }
    if (t.places_cue) table.balls[std::size_t(s.cue_ball)].r = {t.cue_pos.x, t.cue_pos.y, 0.0};
    CHECK(t.meant.elevation_rad >= phys::min_cue_elevation(table, s.cue_ball, t.meant, env.table, env.params) - 1e-12);
    CHECK(t.meant.elevation_rad >=
          phys::min_cue_elevation(table, s.cue_ball, t.meant.aim_rad, env.table, env.params) - 1e-12);

    const nlohmann::json& why = t.why;
    CHECK(why.at("kind") == t.kind);
    CHECK(why.at("worth").get<double>() == t.worth);
    CHECK(why.at("gain").is_number());
    CHECK(why.at("risk").is_number());
    CHECK(why.at("spec").at("speed_m_s").get<double>() == t.meant.speed_m_s);
    CHECK(why.at("spec").at("tip_x").get<double>() == t.meant.tip_x);
    CHECK(why.at("spec").at("tip_y").get<double>() == t.meant.tip_y);
    CHECK(why.at("spec").at("elevation_rad").get<double>() == t.meant.elevation_rad);
    CHECK(why.at("others").is_array());
    CHECK(why.at("others").size() <= 3);

    const Replayed r = replay(s, t);
    REQUIRE(r.trial.clean);
    if (!planned_miss(t)) {
        CHECK(r.j.fouls.empty());
        CHECK(r.j.post.striker != s.striker);
    }
    const rules::Vec2 rest = r.trial.rest[std::size_t(s.cue_ball)];
    CHECK(why.at("leave").at("cue")[0].get<double>() == rest.x);
    CHECK(why.at("leave").at("cue")[1].get<double>() == rest.y);
    CHECK(why.at("cushions").get<int>() == cushions_before_first_ball(r.record, s.cue_ball));
    if (!r.j.post.over && r.j.post.pending == rules::Pending::none) {
        const FrameState& post = r.j.post;
        CHECK(why.at("leave").at("snookered").get<bool>() ==
              env.oracle.snookered(post.balls, post.cue_ball, rules::places_on(post), post.cue_in_hand));
    }
    if (t.kind == "safety" || t.kind == "snooker") {
        CHECK(why.at("thickness").get<double>() >= 0.0);
        CHECK(why.at("thickness").get<double>() <= 0.5);
    }
}

double formula(const nlohmann::json& why, const Level& level) {
    const double gain = why.at("gain").get<double>();
    const double leave = why.at("leave").at("worth").get<double>();
    const double risk = why.at("risk").get<double>();
    const double a = level.score_aware;
    const std::string score = why.value("score", std::string());
    if (score == "ahead") return (1.0 - a) * gain - leave - (1.0 + a) * risk;
    if (score == "snookers") return (1.0 + a) * gain - (1.0 - a) * leave - risk;
    return gain - leave - risk;
}

const char* const kPositions[] = {"ta_open", "long_red", "cut_red", "in_hand", "after_red", "colours", "straight_red",
                                  "snookered"};

}  // namespace

TEST_CASE("tactics: a level with tactics 0 returns before the budget and the judgement stream") {
    const rig::Levels& l = rig::levels();
    // The 147's own file has tactics since stage 2: a copy without stands for any level that has none.
    Level without = l.maximum;
    without.tactics = 0;
    const Level& plain = without;
    for (const Level* level : {&l.beginner, &plain}) {
        for (const FrameState& s : {rig::position("ta_open"), rig::racked(2), rig::position("snookered")}) {
            const Run r = run(s, *level);
            CHECK_FALSE(r.tactic.found);
            CHECK(r.steps == 0);
            CHECK(r.sims == 0);
            CHECK(r.stopped == "done");
            CHECK(r.next == Rng(5).next_u64());
        }
    }
}

TEST_CASE("tactics: every stroke answered is taken by the engine and passed by the referee") {
    const rig::Levels& l = rig::levels();
    const Level levels[] = {with(l.club, 12), with(l.amateur, 16), with(l.pro, 20)};
    for (const Level& level : levels) {
        for (const char* name : kPositions) {
            CAPTURE(name);
            CAPTURE(level.id);
            const FrameState s = rig::position(name);
            const Run r = run(s, level);
            CHECK(r.steps <= level.safety_tries);
            CHECK(r.tactic.candidates == r.steps);
            if (!r.tactic.found) {
                // Only the club, with no cushions to escape off and no swerve, finds nothing.
                CHECK(level.id == "club");
                CHECK(std::string(name) == "snookered");
                continue;
            }
            check_answer(s, r.tactic);
        }
    }
}

TEST_CASE("tactics: the leave is the level's own reading of the position left for the other player") {
    const rig::Levels& l = rig::levels();
    const Env env = rig::env();
    for (const Level& level : {with(l.club, 12, 0.0), with(l.pro, 20, 0.0)}) {
        for (const char* name : {"ta_open", "long_red", "colours"}) {
            CAPTURE(name);
            const FrameState s = rig::position(name);
            const Run r = run(s, level);
            REQUIRE(r.tactic.found);
            const Replayed p = replay(s, r.tactic);
            REQUIRE(p.j.post.striker != s.striker);
            CHECK(r.tactic.why["leave"]["worth"].get<double>() == read_position(p.j.post, level, env).worth);
            CHECK(r.tactic.worth == doctest::Approx(formula(r.tactic.why, level)).epsilon(1e-12));
        }
    }
}

TEST_CASE("tactics: a safety leaves no more than the median of plausible safeties would") {
    const rig::Levels& l = rig::levels();
    const Env env = rig::env();
    const rules::scenario::Rig& rr = rules::scenario::rig();
    for (const Level& level : {with(l.club, 24, 0.0), with(l.amateur, 30, 0.0)}) {
        for (const char* name : {"ta_open", "long_red", "cut_red"}) {
            CAPTURE(name);
            CAPTURE(level.id);
            const FrameState s = rig::position(name);
            const Run r = run(s, level);
            REQUIRE(r.tactic.found);
            REQUIRE(r.tactic.kind == "safety");

            // Thin to half ball either side of the two nearest balls on, at three paces.
            const rules::Vec2 cue = s.balls[std::size_t(s.cue_ball)].pos;
            std::vector<int> on = rules::places_on(s);
            std::stable_sort(on.begin(), on.end(), [&](int a, int b) {
                const auto d = [&](int k) {
                    return std::hypot(s.balls[std::size_t(k)].pos.x - cue.x, s.balls[std::size_t(k)].pos.y - cue.y);
                };
                return d(a) < d(b);
            });
            if (on.size() > 2) on.resize(2);
            std::vector<double> leaves;
            for (const int place : on) {
                const rules::Vec2 ball = s.balls[std::size_t(place)].pos;
                const double dx = ball.x - cue.x;
                const double dy = ball.y - cue.y;
                const double d = std::hypot(dx, dy);
                for (const double offset : {0.85, -0.85, 0.7, -0.7, 0.5, -0.5}) {
                    const double aim = std::atan2(dy, dx) + std::asin(std::clamp(offset * table::kBallDiameter / d, -1.0, 1.0));
                    for (const double speed : {0.8, 1.2, 1.8}) {
                        phys::CueStrike c;
                        c.aim_rad = aim;
                        c.speed_m_s = speed;
                        c.elevation_rad = phys::min_cue_elevation(rules::scenario::table_state(s), s.cue_ball, aim,
                                                                  rr.table, rr.params);
                        const rules::scenario::Played p = rules::scenario::play(s, c);
                        if (!p.judgement.fouls.empty() || p.judgement.post.striker == s.striker) continue;
                        leaves.push_back(read_position(p.judgement.post, level, env).worth);
                    }
                }
            }
            REQUIRE(leaves.size() >= 6);
            std::sort(leaves.begin(), leaves.end());
            const double median = leaves[leaves.size() / 2];
            CHECK(r.tactic.why["leave"]["worth"].get<double>() <= median + 1e-9);
        }
    }
}

TEST_CASE("tactics: tactics 1 plays centre ball, side and spin start at tactics 2") {
    const rig::Levels& l = rig::levels();
    for (const FrameState& s : {rig::position("ta_open"), rig::position("long_red"), rig::position("in_hand"),
                                rig::racked(1), rig::racked(3)}) {
        const Run r = run(s, l.club);
        REQUIRE(r.tactic.found);
        CHECK(r.tactic.meant.tip_x == 0.0);
        CHECK(r.tactic.meant.tip_y == 0.0);
    }
    // Somewhere among these an amateur finds a stroke with spin worth playing.
    bool spun = false;
    for (const char* name : {"ta_open", "long_red", "cut_red", "in_hand", "colours", "straight_red"}) {
        const Run r = run(rig::position(name), with(l.amateur, 40));
        REQUIRE(r.tactic.found);
        spun = spun || r.tactic.meant.tip_x != 0.0 || r.tactic.meant.tip_y != 0.0;
    }
    CHECK(spun);
}

TEST_CASE("tactics: snookers are laid from tactics 3 only, and agree with the oracle") {
    const rig::Levels& l = rig::levels();
    const Env env = rig::env();
    const FrameState s = rig::position("colours");

    const Run pro = run(s, with(l.pro, 40));
    REQUIRE(pro.tactic.found);
    CHECK(pro.tactic.kind == "snooker");
    CHECK(pro.tactic.why["leave"]["snookered"].get<bool>());
    const double gain = pro.tactic.why["gain"].get<double>();
    CHECK(gain > 0.0);
    CHECK(gain <= 7.0);  // the black's penalty, the dearest on the table
    const Replayed p = replay(s, pro.tactic);
    const FrameState& post = p.j.post;
    CHECK(env.oracle.snookered(post.balls, post.cue_ball, rules::places_on(post), post.cue_in_hand));

    for (const Level& below : {with(l.club, 24), with(l.amateur, 40)}) {
        const Run r = run(s, below);
        REQUIRE(r.tactic.found);
        CHECK(r.tactic.kind != "snooker");
        CHECK(r.tactic.why["gain"].get<double>() == 0.0);
        for (const nlohmann::json& other : r.tactic.why["others"]) CHECK(other["kind"] != "snooker");
    }
}

TEST_CASE("tactics: an escape hits a ball on off no more cushions than the level goes off") {
    const rig::Levels& l = rig::levels();
    const FrameState s = rig::position("snookered");
    CHECK_FALSE(run(s, l.club).tactic.found);
    for (const Level& level : {with(l.amateur, 16), with(l.pro, 30)}) {
        CAPTURE(level.id);
        const Run r = run(s, level);
        REQUIRE(r.tactic.found);
        CHECK(r.tactic.kind == "escape");
        CHECK(r.tactic.why["hit"].get<bool>());
        const Replayed p = replay(s, r.tactic);
        CHECK(p.j.fouls.empty());
        CHECK(p.trial.first_hit >= 0);
        CHECK(s.balls[std::size_t(p.trial.first_hit)].ball == rules::Ball::red);
        const int cushions = cushions_before_first_ball(p.record, s.cue_ball);
        CHECK(cushions >= 1);
        if (!r.tactic.why.value("swerve", false)) CHECK(cushions <= level.escape_cushions);
        CHECK(r.tactic.why["risk"].get<double>() > 0.0);
    }
    const Run amateur = run(s, with(l.amateur, 16));
    CHECK_FALSE(amateur.tactic.why.contains("swerve"));
}

TEST_CASE("tactics: the break-off is legal, meets a red first and brings the cue ball back to baulk") {
    const rig::Levels& l = rig::levels();
    const Env env = rig::env();
    for (const std::uint64_t seed : {1u, 2u}) {
        const FrameState s = rig::racked(seed);
        for (const Level& level : {with(l.club, 24), with(l.amateur, 24)}) {
            CAPTURE(seed);
            CAPTURE(level.id);
            const Run r = run(s, level);
            REQUIRE(r.tactic.found);
            CHECK(r.tactic.kind == "break");
            CHECK(r.tactic.places_cue);
            CHECK(env.oracle.cue_placement_legal(s.balls, s.cue_ball, r.tactic.cue_pos));
            const Replayed p = replay(s, r.tactic);
            CHECK(p.j.fouls.empty());
            REQUIRE(p.trial.first_hit >= 0);
            CHECK(s.balls[std::size_t(p.trial.first_hit)].ball == rules::Ball::red);
            CHECK(p.trial.rest[std::size_t(s.cue_ball)].x < table::baulk_line_x());
        }
    }
}

TEST_CASE("tactics: the score moves the worth by the README's formulas") {
    const rig::Levels& l = rig::levels();
    const Level level = with(l.pro, 16, 0.0);
    struct Case {
        const char* name;
        const char* score;
    };
    for (const Case& c : {Case{"ta_ahead", "ahead"}, Case{"ta_behind", "snookers"}, Case{"ta_open", ""}}) {
        CAPTURE(c.name);
        const Run r = run(rig::position(c.name), level);
        REQUIRE(r.tactic.found);
        CHECK(r.tactic.why.value("score", std::string()) == c.score);
        CHECK(r.tactic.worth == doctest::Approx(formula(r.tactic.why, level)).epsilon(1e-12));
    }
    // The same table weighed both ways: ahead counts a foul given away more, and the leave fully.
    const Run ahead = run(rig::position("ta_ahead"), level);
    const Run behind = run(rig::position("ta_behind"), level);
    const double a = level.score_aware;
    const double risk = ahead.tactic.why["risk"].get<double>();
    const double leave = ahead.tactic.why["leave"]["worth"].get<double>();
    const double gain = ahead.tactic.why["gain"].get<double>();
    CHECK(ahead.tactic.worth == doctest::Approx((1.0 - a) * gain - leave - (1.0 + a) * risk).epsilon(1e-12));
    CHECK(behind.tactic.why["score"] == "snookers");
}

TEST_CASE("tactics: the answer is a pure function of the frame, the level, the stream and the steps") {
    const rig::Levels& l = rig::levels();
    for (const char* name : {"ta_open", "snookered", "in_hand"}) {
        CAPTURE(name);
        const FrameState s = rig::position(name);
        const Level level = with(l.pro, 20);
        const Run a = run(s, level);
        const Run b = run(s, level);
        REQUIRE(a.tactic.found);
        CHECK(a.tactic.why == b.tactic.why);
        CHECK(a.tactic.meant.aim_rad == b.tactic.meant.aim_rad);
        CHECK(a.tactic.meant.speed_m_s == b.tactic.meant.speed_m_s);
        CHECK(a.steps == b.steps);
        CHECK(a.sims == b.sims);
        CHECK(a.next == b.next);

        Think again;
        again.max_steps = a.steps;
        const Run c = run(s, level, again);
        CHECK(c.tactic.why == a.tactic.why);
        CHECK(c.tactic.meant.aim_rad == a.tactic.meant.aim_rad);
        CHECK(c.tactic.meant.elevation_rad == a.tactic.meant.elevation_rad);
        CHECK(c.next == a.next);
    }
}

TEST_CASE("tactics: a stopped budget answers with the best so far") {
    const rig::Levels& l = rig::levels();
    const FrameState s = rig::position("ta_open");
    const Level level = with(l.pro, 20, 0.0);

    Think three;
    three.max_steps = 3;
    const Run early = run(s, level, three);
    CHECK(early.steps == 3);
    CHECK(early.stopped == "steps");
    CHECK(early.tactic.candidates == 3);
    REQUIRE(early.tactic.found);
    check_answer(s, early.tactic);
    const Run whole = run(s, level);
    // The reds are at the top: a safety that came back to baulk is played before any that did not.
    const auto away = [](const Tactic& t) {
        return t.kind != "safety" || t.why["leave"]["cue"][0].get<double>() < 0.0;
    };
    if (away(whole.tactic) == away(early.tactic)) {
        CHECK(whole.tactic.worth >= early.tactic.worth);
    } else {
        CHECK(away(whole.tactic));
    }

    // The budget is shared: steps the search took before count against the limit.
    {
        Think eight;
        eight.max_steps = 8;
        Budget budget(level, eight);
        budget.steps = 5;
        Rng judgement(5);
        const Tactic t = choose_tactic(s, level, rig::env(), budget, judgement);
        CHECK(t.found);
        CHECK(t.candidates == 3);
        CHECK(budget.steps == 8);
    }

    std::atomic<bool> cancel{true};
    Think cancelled;
    cancelled.cancel = &cancel;
    const Run none = run(s, level, cancelled);
    CHECK_FALSE(none.tactic.found);
    CHECK(none.steps == 0);
    CHECK(none.stopped == "cancelled");
}

TEST_CASE("tactics: a frame that is pending is refused with why.error") {
    FrameState s = rig::position("ta_open");
    s.pending = rules::Pending::after_foul;
    const Run r = run(s, rig::levels().club);
    CHECK_FALSE(r.tactic.found);
    CHECK(r.tactic.why.contains("error"));
    CHECK(r.steps == 0);
}
