// Concessions (README "The score"): a level concedes, at its turn and when asked to choose, once it
// needs concede_snookers snookers or more; never with concede_snookers 0, never short of them, and
// the 147 never while it has a pot.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/rules/engine.hpp"

#include "opponent/choice.hpp"
#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::DecisionKind;
using rules::FrameState;

namespace {

bool offered(const FrameState& s, int player, DecisionKind kind) {
    const std::vector<DecisionKind> open = rules::options(s, player);
    return std::find(open.begin(), open.end(), kind) != open.end();
}

// The six colours on their spots, player 0 to play and `short_of` points beyond what is left.
FrameState colours_behind(int short_of) {
    FrameState s = rig::position("colours");
    s.scores = {0, rules::points_remaining(s) + short_of};
    return s;
}

Level with_concede(const Level& from, int snookers) {
    Level l = from;
    l.concede_snookers = snookers;
    return l;
}

Stroke turn(const FrameState& s, const Level& level) {
    Rng rng(8);
    Think t;
    t.max_sims = 40;
    return choose_stroke(s, level, rig::env(), rng, t);
}

}  // namespace

TEST_CASE("at its turn a level concedes once it needs its snookers, with why naming them") {
    const Level& world = rig::levels().world;
    REQUIRE(world.concede_snookers == 3);
    // 9 points short with the yellow on: three snookers of four.
    const FrameState s = colours_behind(9);
    REQUIRE(offered(s, 0, DecisionKind::concede_frame));
    REQUIRE(snookers_needed(s, 0) == 3);
    const Stroke st = turn(s, world);
    CHECK(st.decides);
    CHECK(st.decision == DecisionKind::concede_frame);
    CHECK(st.why.value("kind", std::string()) == "concede");
    CHECK(st.why.value("level", std::string()) == "world");
    CHECK(st.why.value("snookers", 0) == 3);

    // 8 short is two snookers: it plays on.
    const Stroke on = turn(colours_behind(8), world);
    CHECK_FALSE(on.decides);
}

TEST_CASE("a copy of Beginner that concedes at one snooker does so at once; its file, with 0, never does") {
    const Level& file = rig::levels().beginner;
    REQUIRE(file.concede_snookers == 0);
    CHECK_FALSE(turn(colours_behind(60), file).decides);
    const Level beginner = with_concede(file, 1);
    const Stroke st = turn(colours_behind(1), beginner);
    CHECK(st.decides);
    CHECK(st.decision == DecisionKind::concede_frame);
    CHECK(st.why.value("snookers", 0) == 1);

    // Not needing any it plays.
    CHECK_FALSE(turn(colours_behind(0), beginner).decides);

    const Level never = with_concede(beginner, 0);
    CHECK_FALSE(turn(colours_behind(60), never).decides);
}

TEST_CASE("the 147 does not concede while it has a pot, and does with none") {
    const Level level = with_concede(rig::levels().maximum, 1);
    // The black alone, an easy pot: the maximum is long gone and the frame lost, but it pots.
    FrameState black = rig::position("black_only");
    black.scores = {0, 40};
    REQUIRE(offered(black, 0, DecisionKind::concede_frame));
    CHECK_FALSE(turn(black, level).decides);

    // Snookered on the last red, with no pot: it concedes.
    FrameState hidden = rig::position("snookered");
    hidden.scores = {0, 147};
    REQUIRE(offered(hidden, 0, DecisionKind::concede_frame));
    REQUIRE(pots(hidden, rig::env(), level.max_cut_rad).empty());
    const Stroke st = turn(hidden, level);
    CHECK(st.decides);
    CHECK(st.decision == DecisionKind::concede_frame);
}

TEST_CASE("asked to choose, a level concedes when conceding is offered and it needs its snookers") {
    FrameState s = colours_behind(23);
    REQUIRE(snookers_needed(s, 0) == 6);
    // Nothing pending: the options are the claim and the concessions, conceding among them.
    const std::vector<DecisionKind> open = rules::options(s, 0);
    REQUIRE(std::find(open.begin(), open.end(), DecisionKind::concede_frame) != open.end());
    for (const Level* level : {&rig::levels().pro, &rig::levels().world}) {
        CAPTURE(level->id);
        Rng rng(3);
        nlohmann::json why;
        CHECK(choose_option(s, 0, open, *level, rig::env(), rng, &why) == DecisionKind::concede_frame);
        CHECK(why.value("kind", std::string()) == "concede");
        CHECK(why.value("snookers", 0) == 6);
        CHECK(why.value("level", std::string()) == level->id);
    }
    // Spec 10.4: only the top levels concede; at their turn the others play on.
    for (const Level* level : {&rig::levels().club, &rig::levels().amateur}) {
        CAPTURE(level->id);
        REQUIRE(level->concede_snookers == 0);
        CHECK_FALSE(turn(s, *level).decides);
    }

    // After the other player's foul, conceding is offered only to a player who may concede at any
    // time; then it concedes there too.
    FrameState after = s;
    after.pending = rules::Pending::after_foul;
    after.decider = 0;
    after.offender = 1;
    after.striker = 1;
    after.last_penalty = 4;
    after.concede_any_time = {true, false};
    const std::vector<DecisionKind> asked = rules::options(after, 0);
    REQUIRE(std::find(asked.begin(), asked.end(), DecisionKind::concede_frame) != asked.end());
    Rng rng(3);
    CHECK(choose_option(after, 0, asked, rig::levels().world, rig::env(), rng) == DecisionKind::concede_frame);

    // Short of its snookers, or with 0, it chooses as the stand-in does.
    const Level never = with_concede(rig::levels().world, 0);
    Rng a(3);
    Rng b(3);
    CHECK(choose_option(after, 0, asked, never, rig::env(), a) ==
          choose_decision(after, 0, asked, rig::levels().standin, b));
    FrameState close = after;
    close.scores = {0, rules::points_remaining(close) + 2};
    Rng c(3);
    Rng d(3);
    CHECK(choose_option(close, 0, rules::options(close, 0), rig::levels().world, rig::env(), c) ==
          choose_decision(close, 0, rules::options(close, 0), rig::levels().standin, d));
}
