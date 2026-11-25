// choose_tactic on a touching ball, the reds at the top, and an escape with no route to a ball on
// (README "Tactics").

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ofs/opponent/budget.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/opponent/tactics.hpp"
#include "ofs/rules/engine.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::FrameState;

namespace {

Tactic choose(const FrameState& s, const Level& level) {
    Think think;
    Budget budget(level, think);
    Rng judgement(7);
    return choose_tactic(s, level, rig::env(), budget, judgement);
}

struct Played {
    FrameState from;
    Trial trial;
    rules::Judgement j;
};

Played play(const FrameState& s, const Tactic& t) {
    const Env env = rig::env();
    Played out;
    out.from = s;
    std::string error;
    if (t.places_cue) REQUIRE(rules::place_cue_ball(out.from, t.cue_pos, env.oracle, error));
    phys::ShotRecord record;
    out.trial = try_strike(out.from, out.from.balls[std::size_t(s.cue_ball)].pos, t.meant, env, &record);
    out.j = rules::judge_shot(out.from, t.input, record, env.table, env.oracle, env.ref);
    return out;
}

FrameState with_touching(const char* name) {
    FrameState s = rig::position(name);
    const std::vector<int> on = rules::places_on(s);
    for (const int place : rig::env().oracle.touching(s.balls, s.cue_ball)) {
        if (std::find(on.begin(), on.end(), place) != on.end()) s.touching.push_back(place);
    }
    return s;
}

}  // namespace

TEST_CASE("tactics: with every red behind a touching red, a safety is played away from it") {
    const FrameState s = with_touching("ta_touching");
    REQUIRE(s.touching.size() == 1);
    const int touching = s.touching.front();
    const rig::Levels& l = rig::levels();
    for (const Level* level : {&l.club, &l.amateur}) {
        CAPTURE(level->id);
        const Tactic t = choose(s, *level);
        REQUIRE(t.found);
        CHECK(t.kind == "safety");
        const Played p = play(s, t);
        REQUIRE(p.trial.clean);
        CHECK(p.j.fouls.empty());
        CHECK(p.j.post.striker != s.striker);
        CHECK(p.trial.first_hit != touching);
        const rules::Vec2 was = s.balls[std::size_t(touching)].pos;
        const rules::Vec2 now = p.trial.rest[std::size_t(touching)];
        CHECK(now.x == doctest::Approx(was.x).epsilon(1e-9));
        CHECK(now.y == doctest::Approx(was.y).epsilon(1e-9));
        // Away from it, at nothing, a miss is not possible: no foul points are risked.
        if (p.trial.first_hit < 0) CHECK(t.why["risk"].get<double>() == 0.0);
    }
}

TEST_CASE("tactics: with every red in the top half a safety brings the cue ball back to baulk") {
    const rig::Levels& l = rig::levels();
    for (const char* name : {"ta_top_red", "ta_open", "long_red"}) {
        const FrameState s = rig::position(name);
        for (const Level* level : {&l.club, &l.amateur}) {
            CAPTURE(name);
            CAPTURE(level->id);
            const Tactic t = choose(s, *level);
            REQUIRE(t.found);
            REQUIRE(t.kind == "safety");
            const Played p = play(s, t);
            CHECK(p.j.fouls.empty());
            CHECK(p.trial.rest[std::size_t(s.cue_ball)].x < 0.0);
            CHECK(t.why["leave"]["cue"][0].get<double>() < 0.0);
        }
    }
}

TEST_CASE("tactics: an escape with no route to a ball on answers the stroke that comes nearest, why.hit false") {
    const FrameState s = rig::position("ta_ring");
    REQUIRE_FALSE(rig::env().oracle.can_hit(s.balls, s.cue_ball, rules::places_on(s)));
    const rig::Levels& l = rig::levels();
    Level two = l.amateur;
    two.escape_cushions = 2;
    for (const Level* level : {&l.amateur, static_cast<const Level*>(&two), &l.pro}) {
        CAPTURE(level->id);
        CAPTURE(level->escape_cushions);
        const Tactic t = choose(s, *level);
        REQUIRE(t.found);
        CHECK(t.kind == "escape");
        CHECK(t.why["hit"] == false);
        CHECK(t.candidates > 0);
        const Played p = play(s, t);
        CHECK(p.trial.clean);
        CHECK_FALSE(p.j.fouls.empty());
        CHECK(t.why["risk"].get<double>() > 0.0);
        // The same answer again: one pure function of the frame, the level and the stream.
        const Tactic again = choose(s, *level);
        CHECK(again.why == t.why);
        CHECK(again.meant.aim_rad == t.meant.aim_rad);
    }
    // With no cushions to go off and no swerve there is still nothing to answer.
    CHECK_FALSE(choose(s, l.club).found);
}
