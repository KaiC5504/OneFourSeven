#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "scenario.hpp"

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

constexpr double kD = table::kBallDiameter;

const TableOracle& oracle() { return *rig().oracle; }

// The cue ball at (-0.5, 0.3), a red a metre up the table from it and a blue half way between,
// `aside` off the line of centres.
std::vector<BallOnTable> one_blocker(double aside) {
    return {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.5, 0.3), ball(Ball::blue, 0.0, 0.3 + aside)};
}

using Places = std::vector<int>;

}  // namespace

TEST_CASE("S2.17: a ball not on between the cue ball and the ball on snookers") {
    const auto balls = one_blocker(0.0);
    CHECK(oracle().snookered(balls, 0, {1}, false));
    CHECK(oracle().snookering_balls(balls, 0, {1}) == Places{2});
}

TEST_CASE("S2.17: moved aside, the blocker frees one extreme edge, then both") {
    // The paths that end on the red's extreme edges leave the cue ball at a = asin(52.5 mm / 1 m)
    // either side of the line of centres, so half way they are 0.5 m x sin a = 26.25 mm from it.
    // A ball `aside` of the line there is 26.25 mm + aside cos a from the far path and
    // |26.25 mm - aside cos a| from the near one, and in the way of each while that is less than
    // a diameter: of the far path until aside = 26.25 mm / cos a = 26.286 mm, of the near one
    // until aside = 78.75 mm / cos a = 78.859 mm.
    const double cos_a = std::sqrt(1.0 - kD * kD);
    const double one_free = 0.5 * kD / cos_a;
    const double both_free = 1.5 * kD / cos_a;
    CHECK(one_free == doctest::Approx(0.026286).epsilon(1e-4));
    CHECK(both_free == doctest::Approx(0.078859).epsilon(1e-4));

    CHECK(oracle().snookered(one_blocker(one_free - 1e-6), 0, {1}, false));
    CHECK_FALSE(oracle().clear_path(one_blocker(one_free - 1e-6), 0, {1}));
    // One edge free is still a snooker, though there is now a clear path to that edge.
    CHECK(oracle().snookered(one_blocker(one_free + 1e-6), 0, {1}, false));
    CHECK(oracle().clear_path(one_blocker(one_free + 1e-6), 0, {1}));
    CHECK(oracle().snookering_balls(one_blocker(one_free + 1e-6), 0, {1}) == Places{2});

    CHECK(oracle().snookered(one_blocker(both_free - 1e-6), 0, {1}, false));
    CHECK_FALSE(oracle().snookered(one_blocker(both_free + 1e-6), 0, {1}, false));
    CHECK(oracle().snookering_balls(one_blocker(both_free + 1e-6), 0, {1}).empty());
    // The same on the other side of the line.
    CHECK(oracle().snookered(one_blocker(-both_free + 1e-6), 0, {1}, false));
    CHECK_FALSE(oracle().snookered(one_blocker(-both_free - 1e-6), 0, {1}, false));
}

TEST_CASE("S2.17: a path that exactly grazes a ball is not obstructed") {
    const double both_free = 1.5 * kD / std::sqrt(1.0 - kD * kD);
    CHECK_FALSE(oracle().snookered(one_blocker(both_free), 0, {1}, false));
}

TEST_CASE("S2.17: a ball on the cue ball already touches can be hit") {
    // The blue is against the cue ball on the far side from the red and in nobody's way.
    const std::vector<BallOnTable> balls = {ball(Ball::cue, 0.0, 0.3), ball(Ball::red, kD, 0.3),
                                            ball(Ball::blue, -kD - 1e-5, 0.3)};
    CHECK_FALSE(oracle().snookered(balls, 0, {1}, false));
    // Resting a hair inside touching a ball not on, the cue ball still leaves it freely.
    const std::vector<BallOnTable> leaving = {ball(Ball::cue, 0.0, 0.3), ball(Ball::red, 0.6, 0.3),
                                              ball(Ball::blue, -kD + 1e-9, 0.3)};
    CHECK_FALSE(oracle().snookered(leaving, 0, {1}, false));
}

TEST_CASE("S2.17: balls on never obstruct each other") {
    // Red 1 is straight behind red 2 as the cue ball sees them.
    std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.5, 0.3, 0),
                                      ball(Ball::red, 0.0, 0.3, 1)};
    CHECK_FALSE(oracle().snookered(balls, 0, {1, 2}, false));
    // The same ball in the same place, when it is not on, snookers.
    CHECK(oracle().snookered(balls, 0, {1}, false));
    CHECK(oracle().snookering_balls(balls, 0, {1}) == Places{2});

    // A blue 0.25 m from the cue ball and 70 mm aside. By the sums of the test above it is in
    // the way of the near edge of a ball until aside = (52.5 mm + 0.25 m x sin a) / cos a: 65.72
    // mm for the red at 1 m (sin a = 0.0525), 79.19 mm for the red at 0.5 m (sin a = 0.105). So
    // it takes an edge of the nearer red and neither of the further one, which only the nearer
    // red stands in front of.
    balls.push_back(ball(Ball::blue, -0.25, 0.37));
    CHECK(oracle().snookered(balls, 0, {2}, false));
    CHECK(oracle().snookering_balls(balls, 0, {2}) == Places{3});
    CHECK_FALSE(oracle().snookered(balls, 0, {1, 2}, false));
    CHECK(oracle().snookering_balls(balls, 0, {1, 2}).empty());
}

TEST_CASE("S2.17(b): the obstructing ball nearest the cue ball is the effective one") {
    // Blue 0.3 m and pink 0.6 m from the cue ball, both on the line to the red.
    const std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.5, 0.3),
                                            ball(Ball::pink, 0.1, 0.3), ball(Ball::blue, -0.2, 0.3)};
    CHECK(oracle().snookered(balls, 0, {1}, false));
    CHECK(oracle().snookering_balls(balls, 0, {1}) == Places{3});
}

TEST_CASE("S2.17(b)(ii): obstructing balls the same distance from the cue ball are all effective") {
    // Green and yellow 0.4 m up the line and 30 mm either side of it: each is hypot(0.4, 0.03)
    // from the cue ball and within a diameter of both edge paths (21 mm from the line there).
    std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.5, 0.3),
                                      ball(Ball::green, -0.1, 0.27), ball(Ball::yellow, -0.1, 0.33)};
    CHECK(oracle().snookering_balls(balls, 0, {1}) == Places{2, 3});
    // A millimetre further off, the green is no longer the nearest.
    balls[2].pos.x += 0.001;
    CHECK(oracle().snookered(balls, 0, {1}, false));
    CHECK(oracle().snookering_balls(balls, 0, {1}) == Places{3});
}

TEST_CASE("S2.17(c): different reds obstructed by different balls leave no effective snookering ball") {
    // Red 0 up the table behind the blue, red 1 across the table behind the pink.
    const std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.5, 0.3, 0),
                                            ball(Ball::red, -0.5, -0.5, 1), ball(Ball::blue, 0.0, 0.3),
                                            ball(Ball::pink, -0.5, -0.1)};
    CHECK(oracle().snookered(balls, 0, {1, 2}, false));
    CHECK(oracle().snookering_balls(balls, 0, {1, 2}).empty());
    CHECK(oracle().snookering_balls(balls, 0, {1}) == Places{3});
    CHECK(oracle().snookering_balls(balls, 0, {2}) == Places{4});
}

TEST_CASE("S2.17(c): one ball obstructing every red is the effective snookering ball") {
    const std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.5, 0.3, 0),
                                            ball(Ball::red, 0.6, 0.3, 1), ball(Ball::blue, 0.0, 0.3)};
    CHECK(oracle().snookering_balls(balls, 0, {1, 2}) == Places{3});
}

TEST_CASE("S2.17: nothing on, or places that are not balls, is not a snooker") {
    const auto balls = one_blocker(0.0);
    CHECK_FALSE(oracle().snookered(balls, 0, {}, false));
    CHECK_FALSE(oracle().snookered(balls, 0, {}, true));
    CHECK_FALSE(oracle().snookered(balls, 0, {-1, 7}, false));
    CHECK_FALSE(oracle().snookered(balls, 9, {1}, false));
    CHECK(oracle().snookering_balls(balls, 0, {}).empty());
    CHECK(oracle().snookering_balls(balls, -1, {1}).empty());
    // The cue ball's own place among the balls on counts for nothing.
    CHECK(oracle().snookered(balls, 0, {0, 1}, false));
}

TEST_CASE("S2.17(a): in hand, snookered from every position in the D") {
    // The blue on its spot with the pink 60 mm in front of it on the centre line. From the D the
    // blue is d = 1.05 to 1.34 m away and the pink is at most 60 mm x sin 15.6 deg = 16.1 mm off
    // the line of centres (the D's corners are 15.6 degrees off the centre line as the blue sees
    // them). The path to the blue's edge on the pink's side is 52.5 mm x (d - 60 mm) / d, at
    // least 49.5 mm, from that line where the pink stands, so it passes the pink's centre at 33.6
    // (from a corner) to 49.5 mm (from straight on): always less than a diameter.
    const std::vector<BallOnTable> balls = {ball(Ball::cue, 0.5, 0.0), spotted(Ball::blue),
                                            ball(Ball::pink, -0.06, 0.0)};
    CHECK(oracle().snookered(balls, 0, {1}, true));
    // The cue ball's listed position, beyond the blue with the pink straight behind it, is
    // ignored.
    CHECK_FALSE(oracle().snookered(balls, 0, {1}, false));
    // In hand with no cue ball listed at all.
    const std::vector<BallOnTable> no_cue = {spotted(Ball::blue), ball(Ball::pink, -0.06, 0.0)};
    CHECK(oracle().snookered(no_cue, -1, {0}, true));
}

TEST_CASE("S2.17(a): in hand, one position in the D with both edges free is no snooker") {
    // The pink moved 91 mm aside. Worked as above, from the D's corner on the other side (the
    // yellow spot) the path to the blue's nearer edge passes the pink's centre at 52.76 mm, a
    // quarter of a millimetre clear; from 32 mm along the baulk line from that corner it passes
    // at 51.92 mm, and from the middle of the baulk line at 41.39 mm.
    const Vec2 corner = table::spot(Ball::yellow);
    std::vector<BallOnTable> balls = {ball(Ball::cue, corner.x, corner.y), spotted(Ball::blue),
                                      ball(Ball::pink, -0.06, 0.091)};
    CHECK_FALSE(oracle().snookered(balls, 0, {1}, false));
    CHECK_FALSE(oracle().snookered(balls, 0, {1}, true));
    balls[0].pos = {corner.x, corner.y + 0.032};
    CHECK(oracle().snookered(balls, 0, {1}, false));
    balls[0].pos = table::d_centre();
    CHECK(oracle().snookered(balls, 0, {1}, false));
    // Still no snooker in hand, wherever the cue ball is listed.
    CHECK_FALSE(oracle().snookered(balls, 0, {1}, true));

    // The free positions are all within 27 mm of that corner. With the yellow on its spot there,
    // none of them is a place the cue ball can go, and the snooker holds from all that are.
    balls.push_back(spotted(Ball::yellow));
    CHECK(oracle().snookered(balls, 0, {1}, true));
}

TEST_CASE("S2.17(a): in hand at the start of a frame the reds are in view") {
    std::vector<BallOnTable> balls;
    std::vector<int> reds;
    for (const table::BallPlacement& p : table::opening_layout(1e-4)) {
        if (p.ball == Ball::red) reds.push_back(int(balls.size()));
        balls.push_back({p.ball, p.index, p.position});
    }
    CHECK_FALSE(oracle().snookered(balls, 0, reds, true));
    CHECK_FALSE(oracle().snookered(balls, 0, reds, false));
}
