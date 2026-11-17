// Choices after a foul that leaves a free ball (README "Choices", spec 10.4): each level answers with
// an option the rules offered, the same for the same seed, and the 147, its maximum gone with a free
// ball called, plays whenever any pot is there, the free ball's own included.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/standin.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::DecisionKind;
using rules::FrameState;
using rules::Pending;
using rules::scenario::place_of;

namespace {

std::vector<const Level*> both() { return {&rig::levels().beginner, &rig::levels().maximum}; }

bool offered(const std::vector<DecisionKind>& options, DecisionKind kind) {
    return std::find(options.begin(), options.end(), kind) != options.end();
}

// Player 1 has fouled and left player 0 unable to see the red: the red tight behind the black, the
// pink a straight pot into the top left. A free ball is called.
FrameState free_ball_after_foul(bool miss) {
    FrameState s = rig::position("straight_red");
    s.balls[std::size_t(place_of(s, Ball::red))].pos = {1.511, -0.0161};
    s.balls[std::size_t(place_of(s, Ball::pink))].pos = {1.3, 0.55};
    REQUIRE(rig::env().oracle.snookered(s.balls, s.cue_ball, rules::places_on(s), false));
    s.free_ball = true;
    s.pending = Pending::after_foul;
    s.decider = 0;
    s.offender = 1;
    s.striker = 1;
    s.last_penalty = 4;
    s.last_miss = miss;
    if (miss) s.original = static_cast<const rules::Position&>(s);
    return s;
}

}  // namespace

TEST_CASE("after a foul leaving a free ball each level picks an option offered, the same each time for a seed") {
    for (const bool miss : {false, true}) {
        const FrameState s = free_ball_after_foul(miss);
        const std::vector<DecisionKind> open = rules::options(s, 0);
        REQUIRE(offered(open, DecisionKind::play));
        if (miss) REQUIRE(offered(open, DecisionKind::replace));
        for (const Level* level : both()) {
            for (std::uint64_t seed = 1; seed <= 5; ++seed) {
                CAPTURE(miss);
                CAPTURE(level->id);
                CAPTURE(seed);
                Rng a(seed);
                Rng b(seed);
                nlohmann::json why;
                const DecisionKind first = choose_option(s, 0, open, *level, rig::env(), a, &why);
                CHECK(offered(open, first));
                CHECK(first == choose_option(s, 0, open, *level, rig::env(), b));
                CHECK(why.value("level", std::string()) == level->id);
            }
        }
    }
}

TEST_CASE("a free ball whose only pot is the nominated ball: the 147 plays it") {
    const FrameState s = free_ball_after_foul(false);
    FrameState left = s;
    std::vector<rules::Call> calls;
    std::string error;
    REQUIRE(rules::decide(left, {DecisionKind::play, 0}, rig::env().oracle, rig::env().ref, calls, error));
    REQUIRE(left.free_ball);
    REQUIRE_FALSE(maximum_on(left));
    const std::vector<Pot> listed = pots(left, rig::env(), rig::levels().maximum.max_cut_rad);
    REQUIRE_FALSE(listed.empty());
    for (const Pot& pot : listed) CHECK(left.balls[std::size_t(pot.target)].ball != Ball::red);

    Rng rng(3);
    nlohmann::json why;
    CHECK(choose_option(s, 0, rules::options(s, 0), rig::levels().maximum, rig::env(), rng, &why) ==
          DecisionKind::play);
    CHECK(why.value("rule", std::string()) == "play");
}
