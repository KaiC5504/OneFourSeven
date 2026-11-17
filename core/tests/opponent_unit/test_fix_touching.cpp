// With the cue ball touching a ball (centres 2R apart to rounding) the paths played away from it
// stay open, and the touching ball itself is never listed: it may only be played away from (S3.8).

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::FrameState;
using rules::scenario::ball;

namespace {

constexpr double R = table::kBallRadius;

// The cue ball at (0.5, 0) touching red 0 on its baulk side; red 1 0.7 m straight up the table,
// played directly away from red 0. `gap` is between the two balls' surfaces.
FrameState touching(double gap) {
    FrameState s = rules::scenario::frame(
        {ball(Ball::cue, 0.5, 0.0), ball(Ball::red, 0.5 - 2.0 * R - gap, 0.0, 0), ball(Ball::red, 1.2, 0.0, 1)});
    s.touching = rig::env().oracle.touching(s.balls, s.cue_ball);
    return s;
}

int pots_of(const FrameState& s, int place) {
    const std::vector<Pot> list = pots(s, rig::env(), 1.35);
    return int(std::count_if(list.begin(), list.end(), [&](const Pot& p) { return p.target == place; }));
}

}  // namespace

TEST_CASE("a ball touching the cue ball behind it does not take away the pots played away from it") {
    // README "A pot's geometry": a pot is listed when both paths are clear. Red 0 lies behind the
    // cue ball's path to red 1, never in it.
    const FrameState apart = touching(0.0005);
    REQUIRE(apart.touching.empty());
    REQUIRE(pots_of(apart, 2) > 0);

    const FrameState touched = touching(0.0);
    REQUIRE(touched.touching == std::vector<int>{1});
    CHECK(pots_of(touched, 2) == pots_of(apart, 2));

    Rng rng(1);
    Think t;
    t.max_sims = 1500;
    const Stroke st = choose_stroke(touched, rig::levels().maximum, rig::env(), rng, t);
    CHECK(st.why.value("kind", std::string()) == "pot");
}

TEST_CASE("the ball the cue ball touches is not listed as a pot, however it lies") {
    // Red 0 straight in front of the cue ball on the line to the left middle pocket: 2 mm away it is
    // a pot, touching it is not.
    const auto beside = [](double gap) {
        return rules::scenario::frame({ball(Ball::cue, 0.0, 0.3), ball(Ball::red, 0.0, 0.3 + 2.0 * R + gap, 0)});
    };
    REQUIRE(pots_of(beside(0.002), 1) > 0);
    CHECK(pots_of(beside(0.0), 1) == 0);
    // A hair under touching by rounding counts the same.
    CHECK(pots_of(beside(-1.0e-12), 1) == 0);
    CHECK(pots_of(touching(0.0), 1) == 0);
}
