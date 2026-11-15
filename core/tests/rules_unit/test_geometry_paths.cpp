#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "scenario.hpp"

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

constexpr double kD = table::kBallDiameter;
constexpr double kPi = 3.14159265358979323846;

const TableOracle& oracle() { return *rig().oracle; }

std::unique_ptr<TableOracle> oracle_with_cushions(int cushions) {
    RefParams ref = rig().ref;
    ref.escape_max_cushions = cushions;
    return make_geometry_oracle(rig().table, ref);
}

// The cue ball at (-0.5, 0.3) looking up the table, a blue and a pink half a metre on, `aside`
// either side of its line, and a red a metre on with its centre 52 mm off the line.
std::vector<BallOnTable> gate(double aside) {
    return {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.5, 0.352), ball(Ball::blue, 0.0, 0.3 + aside),
            ball(Ball::pink, 0.0, 0.3 - aside)};
}

// The last red with the six colours in a ring round it, each a millimetre off touching it.
std::vector<BallOnTable> walled_red(Vec2 cue) {
    std::vector<BallOnTable> balls = {ball(Ball::cue, cue.x, cue.y), ball(Ball::red, 0.4, 0.2)};
    const Ball colours[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};
    for (int i = 0; i < 6; ++i) {
        const double a = i * kPi / 3.0;
        balls.push_back(ball(colours[i], 0.4 + (kD + 0.001) * std::cos(a), 0.2 + (kD + 0.001) * std::sin(a)));
    }
    return balls;
}

using Places = std::vector<int>;

}  // namespace

TEST_CASE("S3.14(c): a gap that shows only a thin edge of the ball on is a clear path") {
    // A ball stands clear of a straight path when its centre is a diameter from it, so the blue
    // and the pink leave a way between them exactly when each is more than 52.5 mm from the cue
    // ball's line. That line passes the red's centre at 52 mm: all that shows through the gap is
    // the last half millimetre of the red's edge. The rest of the red is behind the blue.
    CHECK(oracle().clear_path(gate(kD + 1e-6), 0, {1}));
    CHECK_FALSE(oracle().clear_path(gate(kD - 1e-6), 0, {1}));
    // Either way the red cannot be hit at both edges, nor full in the face.
    CHECK(oracle().snookered(gate(kD + 1e-6), 0, {1}, false));
    CHECK_FALSE(oracle().full_ball(gate(kD + 1e-6), 0, {1}));
    // With the red a millimetre further out the gap shows none of it.
    auto missed = gate(kD + 1e-6);
    missed[1].pos.y = 0.3 + kD + 0.0005;
    CHECK_FALSE(oracle().clear_path(missed, 0, {1}));
}

TEST_CASE("S3.14(c): a path to any one of the balls on is enough, and other balls on do not close it") {
    // Red 0 behind the blue, red 1 in the open, red 2 behind red 1.
    const std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.5, 0.3, 0),
                                            ball(Ball::blue, 0.0, 0.3), ball(Ball::red, -0.5, -0.2, 1),
                                            ball(Ball::red, -0.5, -0.5, 2)};
    CHECK_FALSE(oracle().clear_path(balls, 0, {1}));
    CHECK(oracle().clear_path(balls, 0, {1, 3}));
    CHECK(oracle().clear_path(balls, 0, {3}));
    // Red 2 on its own: red 1, not on now, hides it.
    CHECK_FALSE(oracle().clear_path(balls, 0, {4}));
    CHECK_FALSE(oracle().clear_path(balls, 0, {}));
    CHECK_FALSE(oracle().clear_path(balls, 5, {1}));
}

TEST_CASE("S3.14(c): a ball not on behind the ball on hides nothing") {
    const std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.0, 0.3),
                                            ball(Ball::blue, 0.06, 0.31), ball(Ball::pink, 0.5, 0.3)};
    CHECK(oracle().clear_path(balls, 0, {1}));
    CHECK(oracle().full_ball(balls, 0, {1}));
}

TEST_CASE("S3.14(c): two balls not on that overlap as the cue ball sees them close the path between") {
    // The pink 0.3 m from the cue ball and 45 mm above the line of centres, the blue 0.7 m from
    // it and 45 mm below. A ball is in the way of every path that passes within 52.5 mm of it:
    // the pink of those from 7.5 mm / 0.3 m = 0.025 rad below the line of centres upwards, the
    // blue of those from 7.5 mm / 0.7 m = 0.0107 rad above it downwards. Between them that is
    // every direction; either alone leaves the red's far edge (0.0525 rad from the line) in view.
    std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.5, 0.3),
                                      ball(Ball::blue, 0.2, 0.255), ball(Ball::pink, -0.2, 0.345)};
    CHECK_FALSE(oracle().clear_path(balls, 0, {1}));
    CHECK(oracle().clear_path({balls[0], balls[1], balls[2]}, 0, {1}));
    CHECK(oracle().clear_path({balls[0], balls[1], balls[3]}, 0, {1}));
    // 60 mm from the line instead, they leave the middle of the red open.
    balls[2].pos.y = 0.24;
    balls[3].pos.y = 0.36;
    CHECK(oracle().clear_path(balls, 0, {1}));
    CHECK(oracle().full_ball(balls, 0, {1}));
}

TEST_CASE("S3.14(d): full-ball contact needs the line of centres clear by a ball's width") {
    // A blue half way to the red is in the way of the line of centres while its centre is less
    // than a diameter, 52.5 mm, from it.
    std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.5, 0.3),
                                      ball(Ball::blue, 0.0, 0.3524)};
    CHECK_FALSE(oracle().full_ball(balls, 0, {1}));
    balls[2].pos.y = 0.3526;
    CHECK(oracle().full_ball(balls, 0, {1}));
    CHECK_FALSE(oracle().full_ball(balls, 0, {}));
}

TEST_CASE("S3.14(d): a red behind a red is open to full-ball contact, behind a colour it is not") {
    std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.3), ball(Ball::red, 0.5, 0.3, 0),
                                      ball(Ball::red, 0.0, 0.3, 1)};
    // Asked about the far red alone, the red in front of it still does not count.
    CHECK(oracle().full_ball(balls, 0, {1}));
    balls[2].ball = Ball::blue;
    CHECK_FALSE(oracle().full_ball(balls, 0, {1}));

    // A colour in front of both reds.
    balls[2].ball = Ball::red;
    balls.push_back(ball(Ball::blue, -0.25, 0.3));
    CHECK_FALSE(oracle().full_ball(balls, 0, {1, 2}));
}

TEST_CASE("S3.14(d): a colour on is obstructed by any ball, a red included") {
    std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.3), ball(Ball::pink, 0.5, 0.3),
                                      ball(Ball::red, 0.0, 0.3), ball(Ball::black, -0.5, -0.3)};
    CHECK_FALSE(oracle().full_ball(balls, 0, {1}));
    CHECK(oracle().full_ball(balls, 0, {1, 3}));
    // Two colours that could be on, one behind the other: the nearer is open; the further is
    // not, though the ball in front of it could be on too.
    balls[2].ball = Ball::blue;
    CHECK(oracle().full_ball(balls, 0, {1, 2}));
    CHECK_FALSE(oracle().full_ball(balls, 0, {1}));
}

TEST_CASE("S3.14(a)(ii): an escape off one cushion") {
    // Cue ball, blue and red in a line along the table's centre, 0.3 m apart. The centre of a
    // ball against the left cushion is at y = 0.889 - 0.02625 = 0.86275, so the red's image in
    // that cushion is at (0.9, 1.7255) and the straight line to it from (0.3, 0) meets the
    // cushion at x = 0.6: on the nose between the middle pocket (0.13) and the corner (1.68), and
    // nowhere near the blue.
    const std::vector<BallOnTable> balls = {ball(Ball::cue, 0.3, 0.0), ball(Ball::red, 0.9, 0.0),
                                            ball(Ball::blue, 0.6, 0.0)};
    CHECK_FALSE(oracle().clear_path(balls, 0, {1}));
    CHECK(oracle().can_hit(balls, 0, {1}));
    CHECK(oracle_with_cushions(1)->can_hit(balls, 0, {1}));
    CHECK_FALSE(oracle_with_cushions(0)->can_hit(balls, 0, {1}));
}

TEST_CASE("S3.14(a)(ii): a bounce in a pocket opening is no escape") {
    // The same three balls a metre apart about the blue spot. Off either side cushion the
    // bounce would be at x = 0, in the middle pocket; off the top cushion the first leg runs
    // through the blue; off the baulk cushion the cue ball comes back through it.
    const std::vector<BallOnTable> balls = {ball(Ball::cue, -0.5, 0.0), ball(Ball::red, 0.5, 0.0),
                                            ball(Ball::blue, 0.0, 0.0)};
    CHECK_FALSE(oracle_with_cushions(1)->can_hit(balls, 0, {1}));
    // Two cushions: the red's image in the top cushion (x = 1.75825) and then the left one is at
    // (3.0165, 1.7255). The line to it from the cue ball meets the left cushion at x = 1.258 and
    // the top one at y = 0.617, both on the nose, and passes the blue 0.22 m away.
    CHECK(oracle_with_cushions(2)->can_hit(balls, 0, {1}));
    CHECK(oracle().can_hit(balls, 0, {1}));
}

TEST_CASE("S3.14(a)(ii): a ball on walled in by balls not on cannot be hit") {
    // Neighbours in the ring are 53.5 mm apart, a 1 mm gap. To touch the red between two of them
    // the cue ball's centre would be sqrt(52.5^2 + 53.5^2 - 2 x 52.5 x 53.5 x cos 30) = 27.4 mm
    // from each, well inside both.
    for (Vec2 cue : {Vec2{-0.8, -0.3}, Vec2{0.4, -0.6}, Vec2{1.5, 0.7}, Vec2{0.4, 0.2 + 2.0 * kD + 0.002}}) {
        CAPTURE(cue.x);
        CAPTURE(cue.y);
        const auto balls = walled_red(cue);
        CHECK_FALSE(oracle().clear_path(balls, 0, {1}));
        CHECK_FALSE(oracle().can_hit(balls, 0, {1}));
        CHECK(oracle().snookered(balls, 0, {1}, false));
    }
    // With the ring's ball on the baulk side taken away the red shows from there.
    auto balls = walled_red({-0.8, 0.2});
    CHECK_FALSE(oracle().can_hit(balls, 0, {1}));
    balls.erase(balls.begin() + 5);
    CHECK(oracle().clear_path(balls, 0, {1}));
    CHECK(oracle().can_hit(balls, 0, {1}));
}

TEST_CASE("S3.14(a)(ii): nothing on cannot be hit") {
    const std::vector<BallOnTable> balls = {ball(Ball::cue, 0.3, 0.0), ball(Ball::red, 0.9, 0.0)};
    CHECK_FALSE(oracle().can_hit(balls, 0, {}));
    CHECK_FALSE(oracle().can_hit(balls, 2, {1}));
    CHECK(oracle().can_hit(balls, 0, {1}));
}

TEST_CASE("S3.8: touching is a gap of no more than touching_gap_m") {
    const double gap = rig().ref.touching_gap_m;
    REQUIRE(gap > 1.0e-9);
    // A red twice the gap from the cue ball, the pink half the gap, another red with none.
    const std::vector<BallOnTable> balls = {ball(Ball::red, 0.2, 0.1 + kD + 2.0 * gap, 0),
                                            ball(Ball::pink, 0.2 - kD - 0.5 * gap, 0.1), ball(Ball::cue, 0.2, 0.1),
                                            ball(Ball::red, 0.2 + kD, 0.1, 1), ball(Ball::black, 1.0, 0.5)};
    CHECK(oracle().touching(balls, 2) == Places{1, 3});
    CHECK(oracle().touching(balls, 5).empty());
    CHECK(oracle().touching(balls, -1).empty());

    // The same balls for a referee whose gap is four times as wide, and one with none at all.
    RefParams ref = rig().ref;
    ref.touching_gap_m = 4.0 * gap;
    CHECK(make_geometry_oracle(rig().table, ref)->touching(balls, 2) == Places{0, 1, 3});
    ref.touching_gap_m = 0.0;
    CHECK(make_geometry_oracle(rig().table, ref)->touching(balls, 2) == Places{3});
}
