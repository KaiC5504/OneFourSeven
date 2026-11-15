#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "ofs/rng.hpp"
#include "rules/spotting.hpp"
#include "scenario.hpp"

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

constexpr double kD = table::kBallDiameter;
constexpr double kRadius = table::kDRadius;
// nearest_cue_placement is to be within this of the true nearest place.
constexpr double kNear = 1.0e-4;

const TableOracle& oracle() { return *rig().oracle; }

// The referee's touching gap, whatever referee_params.json makes it.
double gap() { return rig().ref.touching_gap_m; }

double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The point of the D nearest `p`.
Vec2 nearest_in_d(Vec2 p) {
    const Vec2 c = table::d_centre();
    if (table::in_d(p)) return p;
    if (p.x >= c.x) return {c.x, std::clamp(p.y, -kRadius, kRadius)};
    const double d = dist(p, c);
    return {c.x + (p.x - c.x) * kRadius / d, c.y + (p.y - c.y) * kRadius / d};
}

}  // namespace

TEST_CASE("S3.5: a place on or within the lines of the D is allowed, outside them refused") {
    const std::vector<BallOnTable> balls = {ball(Ball::cue, 0.5, 0.5)};
    const Vec2 c = table::d_centre();
    const double diag = kRadius / std::sqrt(2.0);
    for (Vec2 in : {c, Vec2{c.x, 0.1}, Vec2{c.x, kRadius}, Vec2{c.x, -kRadius}, Vec2{c.x - kRadius, 0.0},
                    Vec2{c.x - diag, diag}, Vec2{c.x - 0.2, -0.2}}) {
        CAPTURE(in.x);
        CAPTURE(in.y);
        CHECK(oracle().cue_placement_legal(balls, 0, in));
    }
    for (Vec2 out : {Vec2{c.x + 1.0e-6, 0.0}, Vec2{c.x - kRadius - 1.0e-6, 0.0}, Vec2{c.x, kRadius + 1.0e-6},
                     Vec2{c.x - 0.25, 0.25}, Vec2{0.0, 0.0}, Vec2{c.x - 1.0, 0.0}}) {
        CAPTURE(out.x);
        CAPTURE(out.y);
        CHECK_FALSE(oracle().cue_placement_legal(balls, 0, out));
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(oracle().cue_placement_legal(balls, 0, {nan, 0.0}));
}

TEST_CASE("S3.5: the cue ball may not be placed overlapping or touching a ball") {
    REQUIRE(gap() > 1.0e-9);
    const Vec2 c = table::d_centre();
    const std::vector<BallOnTable> balls = {ball(Ball::cue, c.x - 0.1, 0.1), spotted(Ball::brown)};
    CHECK_FALSE(oracle().cue_placement_legal(balls, 0, {c.x - 0.03, 0.0}));
    CHECK_FALSE(oracle().cue_placement_legal(balls, 0, {c.x - kD, 0.0}));
    CHECK_FALSE(oracle().cue_placement_legal(balls, 0, {c.x - kD - 0.5 * gap(), 0.0}));
    CHECK(oracle().cue_placement_legal(balls, 0, {c.x - kD - 2.0 * gap(), 0.0}));
    // Where the cue ball is listed is not in its own way.
    CHECK(oracle().cue_placement_legal(balls, 0, balls[0].pos));
    CHECK(oracle().cue_placement_legal(balls, 0, {balls[0].pos.x + 0.01, balls[0].pos.y}));
    // With no cue ball among them, every ball listed counts.
    CHECK_FALSE(oracle().cue_placement_legal(balls, -1, {balls[0].pos.x + 0.01, balls[0].pos.y}));
}

TEST_CASE("nearest_cue_placement: a legal place is returned as it is") {
    const std::vector<BallOnTable> balls = {ball(Ball::cue, 0.5, 0.5), spotted(Ball::yellow), spotted(Ball::green),
                                            spotted(Ball::brown)};
    const Vec2 c = table::d_centre();
    for (Vec2 wanted : {Vec2{c.x - 0.1, 0.05}, Vec2{c.x, 0.1}, Vec2{c.x - kRadius, 0.0},
                        table::default_cue_ball_position()}) {
        const Vec2 p = oracle().nearest_cue_placement(balls, 0, wanted);
        CHECK(p.x == wanted.x);
        CHECK(p.y == wanted.y);
    }
}

TEST_CASE("nearest_cue_placement: a place outside the D is brought to the nearest point of it") {
    const std::vector<BallOnTable> balls = {ball(Ball::cue, 0.5, 0.5)};
    const Vec2 c = table::d_centre();
    const double diag = kRadius / std::sqrt(2.0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    struct Case {
        Vec2 wanted;
        Vec2 expected;
    };
    for (const Case& k : {Case{{0.0, 0.0}, {c.x, 0.0}},              // straight back to the baulk line
                          Case{{0.0, 1.0}, {c.x, kRadius}},          // to the end of the line
                          Case{{c.x + 0.2, -1.0}, {c.x, -kRadius}},  // to its other end
                          Case{{-3.0, 0.0}, {c.x - kRadius, 0.0}},   // onto the arc, towards its centre
                          Case{{c.x - 1.0, 1.0}, {c.x - diag, diag}},
                          // 1 mm behind the line and 1 m off: 0.292 mm round the arc from its end.
                          Case{{c.x - 0.001, -1.0}, {c.x - 0.000292, -kRadius}},
                          Case{{nan, 0.0}, table::default_cue_ball_position()}}) {
        CAPTURE(k.wanted.x);
        CAPTURE(k.wanted.y);
        const Vec2 p = oracle().nearest_cue_placement(balls, 0, k.wanted);
        CHECK(oracle().cue_placement_legal(balls, 0, p));
        CHECK(dist(p, k.expected) < kNear);
    }
}

TEST_CASE("nearest_cue_placement: straight off a ball in the way") {
    const Vec2 c = table::d_centre();
    const std::vector<BallOnTable> balls = {ball(Ball::cue, 0.5, 0.5), spotted(Ball::brown)};
    // 20 mm from the brown at 30 degrees behind the line: out along the same line to just clear.
    const Vec2 dir{-std::cos(0.5236), std::sin(0.5236)};
    Vec2 p = oracle().nearest_cue_placement(balls, 0, {c.x + 0.02 * dir.x, 0.02 * dir.y});
    CHECK(oracle().cue_placement_legal(balls, 0, p));
    CHECK(dist(p, {c.x + (kD + gap()) * dir.x, (kD + gap()) * dir.y}) < kNear);
    // Dead on the brown, or in front of the line across it, any way out is as near.
    for (Vec2 wanted : {c, Vec2{c.x + 0.3, 0.0}}) {
        p = oracle().nearest_cue_placement(balls, 0, wanted);
        CHECK(oracle().cue_placement_legal(balls, 0, p));
        CHECK(dist(p, c) > kD + gap());
        CHECK(dist(p, c) < kD + gap() + kNear);
    }
}

TEST_CASE("nearest_cue_placement: between two balls, the nearer place where their clearances cross") {
    // The brown on its spot and a red 80 mm behind it. Between them, 1 mm above the line through
    // both, straight out of either ball is inside the other. The nearest place clear of both is
    // 40 mm from each along that line and sqrt(c^2 - 40^2), about 34 mm, above it (c being a
    // diameter and the touching gap).
    const Vec2 c = table::d_centre();
    const std::vector<BallOnTable> balls = {ball(Ball::cue, 0.5, 0.5), spotted(Ball::brown),
                                            ball(Ball::red, c.x - 0.08, 0.0)};
    const double up = std::sqrt((kD + gap()) * (kD + gap()) - 0.04 * 0.04);
    CHECK(up == doctest::Approx(0.0340).epsilon(1e-3));
    Vec2 p = oracle().nearest_cue_placement(balls, 0, {c.x - 0.04, 0.001});
    CHECK(oracle().cue_placement_legal(balls, 0, p));
    CHECK(dist(p, {c.x - 0.04, up}) < kNear);
    p = oracle().nearest_cue_placement(balls, 0, {c.x - 0.04, -0.001});
    CHECK(oracle().cue_placement_legal(balls, 0, p));
    CHECK(dist(p, {c.x - 0.04, -up}) < kNear);
}

TEST_CASE("nearest_cue_placement: a ball on the D's corner") {
    // The yellow on its spot, wanted beyond that corner. The nearest point of the D is the
    // yellow's centre, so every place in the D a ball and the touching gap from it is as near.
    const std::vector<BallOnTable> balls = {ball(Ball::cue, 0.5, 0.5), spotted(Ball::yellow)};
    const Vec2 yellow = table::spot(Ball::yellow);
    const Vec2 p = oracle().nearest_cue_placement(balls, 0, {yellow.x + 0.5, yellow.y - 0.2});
    CHECK(oracle().cue_placement_legal(balls, 0, p));
    CHECK(dist(p, yellow) > kD + gap());
    CHECK(dist(p, yellow) < kD + gap() + kNear);
}

TEST_CASE("nearest_cue_placement: left alone at the start of a frame the cue ball stays where it is") {
    std::vector<BallOnTable> balls;
    for (const table::BallPlacement& b : table::opening_layout(1e-4)) balls.push_back({b.ball, b.index, b.position});
    const Vec2 p = oracle().nearest_cue_placement(balls, 0, table::default_cue_ball_position());
    CHECK(p.x == table::default_cue_ball_position().x);
    CHECK(p.y == table::default_cue_ball_position().y);
}

TEST_CASE("nearest_cue_placement: always legal, and no legal place is noticeably nearer") {
    Rng rng(20261115);
    const Vec2 c = table::d_centre();
    int illegal = 0;
    int moved_legal = 0;
    int scanned = 0;
    int beaten = 0;
    for (int layout = 0; layout < 12; ++layout) {
        // The cue ball listed inside the D, where it must not count, and up to 16 balls scattered
        // over the D and a little round it, none overlapping.
        std::vector<BallOnTable> balls = {ball(Ball::cue, c.x - 0.1, 0.0)};
        const int wanted_balls = 3 + layout + layout / 4;
        for (int tries = 0; tries < 4000 && int(balls.size()) <= wanted_balls; ++tries) {
            const Vec2 p{rng.uniform(c.x - 0.33, c.x + 0.04), rng.uniform(-0.33, 0.33)};
            bool room = true;
            for (std::size_t i = 1; i < balls.size(); ++i) room = room && dist(p, balls[i].pos) > kD + 1.0e-4;
            if (room) balls.push_back({Ball::red, int(balls.size()) - 1, p});
        }
        int scanned_here = 0;
        for (int i = 0; i < 400; ++i) {
            const Vec2 wanted{rng.uniform(c.x - 0.45, c.x + 0.15), rng.uniform(-0.45, 0.45)};
            const Vec2 p = oracle().nearest_cue_placement(balls, 0, wanted);
            if (!oracle().cue_placement_legal(balls, 0, p)) ++illegal;
            const bool moved = p.x != wanted.x || p.y != wanted.y;
            if (moved && oracle().cue_placement_legal(balls, 0, wanted)) ++moved_legal;
            // For some of those pushed off a ball: every place on a 0.5 mm polar grid that is more
            // than 0.1 mm nearer must be illegal.
            const Vec2 start = nearest_in_d(wanted);
            const double reach = dist(p, start) - kNear;
            if (reach < 0.002 || scanned_here >= 8) continue;
            ++scanned_here;
            ++scanned;
            for (double r = 0.0; r < reach; r += 5.0e-4) {
                const int steps = std::max(1, int(6.283185307 * r / 5.0e-4));
                for (int k = 0; k < steps; ++k) {
                    const double a = 6.283185307 * k / steps;
                    const Vec2 nearer{start.x + r * std::cos(a), start.y + r * std::sin(a)};
                    if (oracle().cue_placement_legal(balls, 0, nearer)) ++beaten;
                }
            }
        }
    }
    CHECK(illegal == 0);
    CHECK(moved_legal == 0);
    CHECK(scanned > 60);
    CHECK(beaten == 0);
}
