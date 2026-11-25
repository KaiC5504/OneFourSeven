// The pot against the tactic and the score (README "Worth", "The score"): the choice handed
// made-up tactics, the floor a lead puts under a pot, and a pot's points discounted while the
// level needs snookers.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "ofs/opponent/opponent.hpp"
#include "ofs/rules/engine.hpp"

#include "opponent/choice.hpp"
#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::FrameState;

namespace {

Tactic tactic_worth(double worth) {
    Tactic t;
    t.found = true;
    t.kind = "safety";
    t.worth = worth;
    return t;
}

PotOffer pot_of(double score, double chance) {
    PotOffer p;
    p.found = true;
    p.score = score;
    p.chance = chance;
    return p;
}

Level club() { return rig::levels().club; }

// straight_red with the scores set: player 0 to play.
FrameState scored(int mine, int theirs) {
    FrameState s = rig::position("straight_red");
    s.scores = {mine, theirs};
    return s;
}

}  // namespace

TEST_CASE("the pot is played when its score beats the tactic's worth, else the tactic") {
    const Level level = club();
    const FrameState s = scored(0, 0);
    CHECK(pick(pot_of(5.0, 0.6), tactic_worth(2.0), s, level) == Pick::pot);
    CHECK(pick(pot_of(1.5, 0.6), tactic_worth(2.0), s, level) == Pick::tactic);
    // Equal is not above.
    CHECK(pick(pot_of(2.0, 0.6), tactic_worth(2.0), s, level) == Pick::tactic);
    // A tactic worth less than nothing still loses to a pot worth less than nothing but more.
    CHECK(pick(pot_of(-1.0, 0.6), tactic_worth(-3.0), s, level) == Pick::pot);
    PotOffer none;
    CHECK(pick(none, tactic_worth(-50.0), s, level) == Pick::tactic);
}

TEST_CASE("a pot under safety_below is not played while a tactic was found, whatever its score") {
    const Level level = club();
    REQUIRE(level.safety_below > 0.05);
    const FrameState s = scored(0, 0);
    CHECK(pick(pot_of(40.0, level.safety_below - 0.01), tactic_worth(-10.0), s, level) == Pick::tactic);
    CHECK(pick(pot_of(40.0, level.safety_below), tactic_worth(-10.0), s, level) == Pick::pot);
}

TEST_CASE("without a tactic the pot is played on safety_below alone, as in stage 1") {
    const Level level = club();
    const FrameState s = scored(0, 0);
    const Tactic none;
    CHECK(pick(pot_of(-5.0, level.safety_below), none, s, level) == Pick::pot);
    CHECK(pick(pot_of(30.0, level.safety_below - 0.01), none, s, level) == Pick::standin);
    CHECK(pick(PotOffer{}, none, s, level) == Pick::standin);
}

TEST_CASE("ahead by more than the points left, a pot needs the raised floor while a tactic was found") {
    Level level = club();
    level.safety_below = 0.2;
    level.score_aware = 0.5;
    FrameState s = rig::position("black_only");
    const int left = rules::points_remaining(s);
    s.scores = {left + 1, 0};
    const double floor = 0.2 + 0.5 * (0.5 - 0.2);
    CHECK(pot_floor(s, level) == doctest::Approx(floor));
    CHECK(pick(pot_of(30.0, floor - 0.01), tactic_worth(0.0), s, level) == Pick::tactic);
    CHECK(pick(pot_of(30.0, floor + 0.01), tactic_worth(0.0), s, level) == Pick::pot);
    // Without a tactic the stage 1 rule stands.
    CHECK(pick(pot_of(30.0, floor - 0.01), Tactic{}, s, level) == Pick::pot);

    // Ahead by exactly what is left is not ahead by more.
    s.scores = {left, 0};
    CHECK(pot_floor(s, level) == doctest::Approx(0.2));
    // Behind, or for the other player, nothing changes.
    s.scores = {0, left + 1};
    CHECK(pot_floor(s, level) == doctest::Approx(0.2));

    // A floor already at a half or above is not lowered.
    level.safety_below = 0.6;
    s.scores = {left + 1, 0};
    CHECK(pot_floor(s, level) == doctest::Approx(0.6));
    // With no eye for the score the floor stays.
    level.safety_below = 0.2;
    level.score_aware = 0.0;
    CHECK(pot_floor(s, level) == doctest::Approx(0.2));
}

TEST_CASE("needing snookers a pot's ball and break count for 1 - score_aware, its leave as before") {
    // No miss cost, no misreading and no position, so the worth is chance * (value + kBreakOn)
    // times the factor; one way of striking, so the same stroke is planned either way.
    Level level = club();
    level.tactics = 0;
    level.miss_weight = 0.0;
    level.judgement_sigma = 0.0;
    level.position_weight = 0.0;
    level.concede_snookers = 0;
    level.speeds = 1;
    level.spins = 1;
    const FrameState even = scored(0, 0);
    FrameState behind = scored(0, 0);
    behind.scores = {0, rules::points_remaining(behind) + 10};
    REQUIRE(rules::penalty_points_required(behind, 0) > 0);

    for (const double a : {0.0, 0.4, 1.0}) {
        CAPTURE(a);
        level.score_aware = a;
        Rng r1(5);
        Rng r2(5);
        const Stroke st_even = choose_stroke(even, level, rig::env(), r1);
        const Stroke st_behind = choose_stroke(behind, level, rig::env(), r2);
        REQUIRE(st_even.why.value("kind", std::string()) == "pot");
        REQUIRE(st_behind.why.value("kind", std::string()) == "pot");
        CHECK(st_behind.meant.aim_rad == st_even.meant.aim_rad);
        const double worth = st_even.why["worth"].get<double>();
        CHECK(st_behind.why["worth"].get<double>() == doctest::Approx(worth * (1.0 - a)));
    }

    // With a position weight the leave keeps its whole weight.
    level.position_weight = 1.0;
    level.score_aware = 1.0;
    Rng r1(5);
    Rng r2(5);
    const Stroke st_even = choose_stroke(even, level, rig::env(), r1);
    const Stroke st_behind = choose_stroke(behind, level, rig::env(), r2);
    REQUIRE(st_even.why.value("kind", std::string()) == "pot");
    REQUIRE(st_behind.meant.aim_rad == st_even.meant.aim_rad);
    // w = 1: value and break are weighted 1 and 0, the leave 1; needing snookers only the value goes.
    const double chance = st_even.why["chance"].get<double>();
    CHECK(st_behind.why["worth"].get<double>() ==
          doctest::Approx(st_even.why["worth"].get<double>() - chance * 1.0).epsilon(1e-9));
}

TEST_CASE("snookers needed: the points required by the rules over the penalty a foul brings now") {
    FrameState s = rig::position("colours");
    const int left = rules::points_remaining(s);
    for (const int short_of : {1, 4, 5, 9, 23}) {
        CAPTURE(short_of);
        s.scores = {0, left + short_of};
        REQUIRE(rules::penalty_points_required(s, 0) == short_of);
        CHECK(snookers_needed(s, 0) == (short_of + 3) / 4);
    }
    s.scores = {0, left};
    CHECK(snookers_needed(s, 0) == 0);

    // The pink and black alone: a snooker on the pink brings 6.
    FrameState two = rig::position("black_only");
    two.balls.push_back(rules::scenario::spotted(rules::Ball::pink));
    two.on = {rules::OnKind::colour, rules::Ball::pink};
    two.scores = {0, rules::points_remaining(two) + 10};
    CHECK(snookers_needed(two, 0) == 2);
    // The black alone: 7 a foul.
    FrameState last = rig::position("black_only");
    last.scores = {0, 7 + 8};
    CHECK(snookers_needed(last, 0) == 2);
    last.scores = {0, 7 + 7};
    CHECK(snookers_needed(last, 0) == 1);

    // Reds left: every snooker counts four.
    FrameState reds = rig::position("straight_red");
    reds.scores = {0, rules::points_remaining(reds) + 9};
    CHECK(snookers_needed(reds, 0) == 3);
}
