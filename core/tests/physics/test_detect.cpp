#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "ofs/physics/detect.hpp"
#include "ofs/rng.hpp"

using namespace ofs;
using namespace ofs::phys;

namespace {

constexpr double R = 0.02625;
constexpr double kPi = 3.14159265358979323846;
constexpr double kTimeTol = 1e-9;

Trajectory traj(Vec3 c0, Vec3 c1 = {}, Vec3 c2 = {}, double duration = kNever) { return {c0, c1, c2, duration}; }

// Brute force, independent of the polynomial coefficients: the earliest t in [0, hi] where gap(t)
// falls from above zero to zero or below and accept(t) holds, from dense samples refined by
// bisection. kNever when there is none.
double brute_first_fall(const std::function<double(double)>& gap, const std::function<bool(double)>& accept,
                        double hi, int samples) {
    double t_prev = 0.0;
    double g_prev = gap(0.0);
    for (int k = 1; k <= samples; ++k) {
        const double t = hi * k / samples;
        const double g = gap(t);
        if (g_prev > 0.0 && g <= 0.0) {
            double lo = t_prev;
            double up = t;
            for (int it = 0; it < 80 && up - lo > 1e-15; ++it) {
                const double mid = 0.5 * (lo + up);
                (gap(mid) > 0.0 ? lo : up) = mid;
            }
            if (accept(up)) return up;
        }
        t_prev = t;
        g_prev = g;
    }
    return kNever;
}

double ball_gap(const Trajectory& a, const Trajectory& b, double t) {
    const Vec3 d = a.at(t) - b.at(t);
    return std::sqrt(d.x * d.x + d.y * d.y) - 2.0 * R;
}

bool brute_in_arc(const ArcCushion& c, double dx, double dy) {
    double off = std::atan2(dy, dx) - c.angle_start;
    while (off < 0.0) off += 2.0 * kPi;
    while (off >= 2.0 * kPi) off -= 2.0 * kPi;
    double span = c.angle_end - c.angle_start;
    while (span < 0.0) span += 2.0 * kPi;
    return off <= span + 1e-9 || off >= 2.0 * kPi - 1e-9;
}

struct Tally {
    int cases = 0;
    int contacts = 0;      // brute force found one and we agree within kTimeTol
    int none = 0;          // neither found one
    int between = 0;       // we found an earlier contact the samples stepped over, confirmed
    int disagreements = 0;
};

// Our answer against the brute force. An answer earlier than the brute force's must be a real
// contact the sampling missed: the gap is at zero there and goes negative just after.
void compare(Tally& tally, double ours, double brute, const std::function<double(double)>& gap, double hi,
             int samples) {
    ++tally.cases;
    if (ours == kNever && brute == kNever) {
        ++tally.none;
        return;
    }
    if (ours != kNever && brute != kNever && std::abs(ours - brute) <= kTimeTol) {
        ++tally.contacts;
        return;
    }
    if (ours != kNever && (brute == kNever || ours < brute)) {
        bool dips = false;
        const double step = hi / samples;
        for (int k = 1; k <= 1000 && !dips; ++k) dips = gap(std::min(hi, ours + step * k / 1000.0)) < 0.0;
        if (std::abs(gap(ours)) < 1e-9 && dips) {
            ++tally.between;
            return;
        }
    }
    ++tally.disagreements;
    CAPTURE(ours);
    CAPTURE(brute);
    CAPTURE(hi);
    FAIL_CHECK("detection disagrees with the brute force");
}

}  // namespace

TEST_CASE("ball_ball_time: head-on, glancing and one stationary") {
    const Trajectory moving = traj({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    CHECK(ball_ball_time(moving, traj({1.0, 0.0, 0.0}), R, 10.0) == doctest::Approx(1.0 - 2.0 * R).epsilon(1e-14));
    CHECK(ball_ball_time(traj({1.0, 0.0, 0.0}), moving, R, 10.0) == doctest::Approx(1.0 - 2.0 * R).epsilon(1e-14));

    const double y = 0.05;
    const double glance = 1.0 - std::sqrt(4.0 * R * R - y * y);
    CHECK(ball_ball_time(moving, traj({1.0, y, 0.0}), R, 10.0) == doctest::Approx(glance).epsilon(1e-13));
    CHECK(ball_ball_time(moving, traj({1.0, 2.0 * R + 1e-6, 0.0}), R, 10.0) == kNever);

    // Both moving, head-on at 1 m/s each.
    CHECK(ball_ball_time(moving, traj({1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}), R, 10.0) ==
          doctest::Approx(0.5 - R).epsilon(1e-14));
}

TEST_CASE("ball_ball_time: both sliding on parabolas") {
    const Trajectory a = traj({-0.3, 0.1, 0.0}, {2.0, 0.3, 0.0}, {-0.5, 0.1, 0.0});
    const Trajectory b = traj({1.15, 0.5, 0.0}, {-1.0, -0.2, 0.0}, {0.2, -0.3, 0.0});
    const double brute = brute_first_fall([&](double t) { return ball_gap(a, b, t); }, [](double) { return true; },
                                          2.0, 200000);
    REQUIRE(brute != kNever);
    CHECK(std::abs(ball_ball_time(a, b, R, 2.0) - brute) < kTimeTol);
}

TEST_CASE("ball_ball_time: touching at t = 0") {
    const Trajectory still = traj({2.0 * R, 0.0, 0.0});
    CHECK(ball_ball_time(traj({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), still, R, 1.0) == 0.0);
    CHECK(ball_ball_time(traj({0.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}), still, R, 1.0) == kNever);
    CHECK(ball_ball_time(traj({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}), still, R, 1.0) == kNever);
    CHECK(ball_ball_time(traj({0.0, 0.0, 0.0}), still, R, 1.0) == kNever);
    // Overlapping by rounding.
    CHECK(ball_ball_time(traj({1e-12, 0.0, 0.0}, {1.0, 0.0, 0.0}), still, R, 1.0) == 0.0);
    CHECK(ball_ball_time(traj({1e-12, 0.0, 0.0}, {-1.0, 0.0, 0.0}), still, R, 1.0) == kNever);
    // Rotated, so the touching distance carries rounding.
    const double c = std::cos(0.7);
    const double s = std::sin(0.7);
    const Trajectory b = traj({0.3 + 2.0 * R * c, -0.2 + 2.0 * R * s, 0.0});
    CHECK(ball_ball_time(traj({0.3, -0.2, 0.0}, {c, s, 0.0}), b, R, 1.0) <= 1e-15);
    CHECK(ball_ball_time(traj({0.3, -0.2, 0.0}, {-c, -s, 0.0}), b, R, 1.0) == kNever);
}

TEST_CASE("ball_ball_time: touching with no normal speed but accelerating together") {
    // A sliding ball at rest pressing into a touching ball: the gap never crosses zero from above.
    const Trajectory still = traj({2.0 * R, 0.0, 0.0});
    CHECK(ball_ball_time(traj({0.0, 0.0, 0.0}, {}, {1.0, 0.0, 0.0}), still, R, 1.0) == 0.0);
    // The same with rounding-level separating speed and overlap.
    CHECK(ball_ball_time(traj({-7e-18, 0.0, 0.0}, {-1e-16, 0.0, 0.0}, {1.0, 0.0, 0.0}), still, R, 1.0) < 1e-9);
    // Accelerating apart: no event.
    CHECK(ball_ball_time(traj({0.0, 0.0, 0.0}, {}, {-1.0, 0.0, 0.0}), still, R, 1.0) == kNever);
}

TEST_CASE("ball_ball_time: a grazing double root is no event") {
    // Passes exactly at contact distance: |d|^2 - 4R^2 = (t - 1)^2.
    CHECK(ball_ball_time(traj({0.0, 2.0 * R, 0.0}, {1.0, 0.0, 0.0}), traj({1.0, 0.0, 0.0}), R, 5.0) == kNever);
    // Tangent while decelerating along a parabola, both balls moving.
    const Trajectory a = traj({-1.5, 2.0 * R, 0.0}, {2.0, 0.0, 0.0}, {-0.5, 0.0, 0.0});
    const Trajectory b = traj({-0.5, 0.0, 0.0}, {1.0, 0.0, 0.0}, {-0.25, 0.0, 0.0});
    // a - b = (-(1 - t/2)^2, 2R): closest at t = 2, where the x gap is exactly 0.
    CHECK(ball_ball_time(a, b, R, 5.0) == kNever);
    // 1e-9 m inside contact is a real (shallow) contact.
    CHECK(ball_ball_time(traj({0.0, 2.0 * R - 1e-9, 0.0}, {1.0, 0.0, 0.0}), traj({1.0, 0.0, 0.0}), R, 5.0) <
          1.0);
}

TEST_CASE("ball_ball_time: never beyond t_max or either duration") {
    const Trajectory a = traj({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    const Trajectory b = traj({1.0, 0.0, 0.0});
    const double t = 1.0 - 2.0 * R;
    CHECK(ball_ball_time(a, b, R, t - 1e-6) == kNever);
    CHECK(ball_ball_time(a, b, R, t + 1e-6) == doctest::Approx(t));
    CHECK(ball_ball_time(traj(a.c0, a.c1, {}, t - 1e-6), b, R, 10.0) == kNever);
    CHECK(ball_ball_time(a, traj(b.c0, {}, {}, t - 1e-6), R, 10.0) == kNever);
    CHECK(ball_ball_time(a, b, R, -1.0) == kNever);
    CHECK(ball_ball_time(a, b, R, kNever) == doctest::Approx(t));
    // Shifted trajectories share the later origin.
    CHECK(ball_ball_time(a.shifted(0.5), b, R, 10.0) == doctest::Approx(t - 0.5));
}

TEST_CASE("ball_line_time: straight on, segment ends and from behind") {
    // Cushion along x = 0 between y = -0.5 and 0.5, playing area at x < 0.
    const LineCushion c{{0.0, -0.5}, {0.0, 0.5}, {-1.0, 0.0}};
    CHECK(ball_line_time(traj({-0.5, 0.0, 0.0}, {1.0, 0.0, 0.0}), c, R, 5.0) == doctest::Approx(0.5 - R).epsilon(1e-14));
    CHECK(ball_line_time(traj({-0.5, 0.5 - 1e-6, 0.0}, {1.0, 0.0, 0.0}), c, R, 5.0) == doctest::Approx(0.5 - R));
    CHECK(ball_line_time(traj({-0.5, 0.5 + 1e-6, 0.0}, {1.0, 0.0, 0.0}), c, R, 5.0) == kNever);
    CHECK(ball_line_time(traj({-0.5, -0.5 + 1e-6, 0.0}, {1.0, 0.0, 0.0}), c, R, 5.0) == doctest::Approx(0.5 - R));
    CHECK(ball_line_time(traj({-0.5, -0.5 - 1e-6, 0.0}, {1.0, 0.0, 0.0}), c, R, 5.0) == kNever);
    // Moving away, parallel, and from behind the cushion either way.
    CHECK(ball_line_time(traj({-0.5, 0.0, 0.0}, {-1.0, 0.0, 0.0}), c, R, 5.0) == kNever);
    CHECK(ball_line_time(traj({-0.5, 0.0, 0.0}, {0.0, 1.0, 0.0}), c, R, 5.0) == kNever);
    CHECK(ball_line_time(traj({0.5, 0.0, 0.0}, {-1.0, 0.0, 0.0}), c, R, 5.0) == kNever);
    CHECK(ball_line_time(traj({0.5, 0.0, 0.0}, {1.0, 0.0, 0.0}), c, R, 5.0) == kNever);
    // Beyond t_max and the duration.
    CHECK(ball_line_time(traj({-0.5, 0.0, 0.0}, {1.0, 0.0, 0.0}), c, R, 0.4) == kNever);
    CHECK(ball_line_time(traj({-0.5, 0.0, 0.0}, {1.0, 0.0, 0.0}, {}, 0.4), c, R, 5.0) == kNever);
}

TEST_CASE("ball_line_time: resting on the cushion, curving into it, and the far end of the segment") {
    const LineCushion c{{0.0, -0.5}, {0.0, 0.5}, {-1.0, 0.0}};
    CHECK(ball_line_time(traj({-R, 0.0, 0.0}, {1.0, 0.0, 0.0}), c, R, 5.0) == 0.0);
    CHECK(ball_line_time(traj({-R, 0.0, 0.0}, {-1.0, 0.0, 0.0}), c, R, 5.0) == kNever);
    CHECK(ball_line_time(traj({-R, 0.0, 0.0}, {}, {0.5, 0.0, 0.0}), c, R, 5.0) == 0.0);
    // Decelerating towards the cushion and turning back short of it: -0.2 + t - 2.6 t^2 peaks at -0.104.
    CHECK(ball_line_time(traj({-0.2, 0.0, 0.0}, {1.0, 0.0, 0.0}, {-2.6, 0.0, 0.0}), c, R, 5.0) == kNever);
    // Swerving in: the contact time solves -0.3 + 0.2 t + 0.4 t^2 = -R.
    const double t = (-0.2 + std::sqrt(0.04 + 1.6 * (0.3 - R))) / 0.8;
    CHECK(ball_line_time(traj({-0.3, 0.0, 0.0}, {0.2, 0.1, 0.0}, {0.4, 0.0, 0.0}), c, R, 5.0) ==
          doctest::Approx(t).epsilon(1e-13));
    // Crossing the line's extension past the end, then passing behind the segment: the only
    // crossing is off the segment.
    CHECK(ball_line_time(traj({-0.5, 0.7, 0.0}, {1.0, -0.1, 0.0}), c, R, 5.0) == kNever);
}

TEST_CASE("ball_arc_time: range, wrap-around past pi and the far side") {
    // Jaw at the origin facing -x: angles from 3.0 anticlockwise through pi to -3.0.
    const ArcCushion jaw{{0.0, 0.0}, 0.01, 3.0, -3.0, 0};
    const double reach = 0.01 + R;
    CHECK(ball_arc_time(traj({-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), jaw, R, 5.0) ==
          doctest::Approx(1.0 - reach).epsilon(1e-13));
    // From +x the contact is on the side the arc doesn't cover; it then leaves through the arc side.
    CHECK(ball_arc_time(traj({1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}), jaw, R, 5.0) == kNever);
    // Contact directions just inside and just outside the range: angle pi - 0.14 and pi - 0.15.
    for (double ang : {kPi - 0.14, kPi - 0.15}) {
        const double cx = reach * std::cos(ang);
        const double cy = reach * std::sin(ang);
        const Trajectory a = traj({cx + 0.5 * std::cos(ang), cy + 0.5 * std::sin(ang), 0.0},
                                  {-std::cos(ang), -std::sin(ang), 0.0});
        // Aimed through the contact point at the centre: contact at t = 0.5.
        const double got = ball_arc_time(a, jaw, R, 5.0);
        if (ang > kPi - 0.1416) {
            CHECK(got == doctest::Approx(0.5).epsilon(1e-12));
        } else {
            CHECK(got == kNever);
        }
    }
    // A full circle from -pi to pi, as table_shape.json may give it, and the same half range written
    // past pi either way.
    const ArcCushion full{{0.0, 0.0}, 0.01, -kPi, kPi, 2};
    CHECK(ball_arc_time(traj({1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}), full, R, 5.0) == doctest::Approx(1.0 - reach));
    CHECK(ball_arc_time(traj({-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), full, R, 5.0) == doctest::Approx(1.0 - reach));
    for (const ArcCushion& upper : {ArcCushion{{0.0, 0.0}, 0.01, 0.0, kPi, 3}, ArcCushion{{0.0, 0.0}, 0.01, 2.0 * kPi, 3.0 * kPi, 3},
                                    ArcCushion{{0.0, 0.0}, 0.01, -2.0 * kPi, -kPi, 3}}) {
        CHECK(ball_arc_time(traj({0.0, 1.0, 0.0}, {0.0, -1.0, 0.0}), upper, R, 5.0) == doctest::Approx(1.0 - reach));
        CHECK(ball_arc_time(traj({0.0, -1.0, 0.0}, {0.0, 1.0, 0.0}), upper, R, 5.0) == kNever);
    }
    // A range that doesn't wrap, approached head-on, and a ball resting on the jaw pressing in.
    const ArcCushion right{{0.0, 0.0}, 0.01, -0.5, 0.5, 1};
    CHECK(ball_arc_time(traj({1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}), right, R, 5.0) == doctest::Approx(1.0 - reach));
    CHECK(ball_arc_time(traj({reach, 0.0, 0.0}, {-1.0, 0.0, 0.0}), right, R, 5.0) == 0.0);
    CHECK(ball_arc_time(traj({reach, 0.0, 0.0}, {1.0, 0.0, 0.0}), right, R, 5.0) == kNever);
    CHECK(ball_arc_time(traj({1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}), right, R, 0.9) == kNever);
}

TEST_CASE("ball_pocket_time: entering, already inside, passing by") {
    const Pocket p{"top_left", true, {1.0, 0.0}, 0.05};
    CHECK(ball_pocket_time(traj({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), p, 5.0) == doctest::Approx(0.95).epsilon(1e-14));
    CHECK(ball_pocket_time(traj({0.99, 0.01, 0.0}, {-1.0, 0.0, 0.0}), p, 5.0) == 0.0);
    CHECK(ball_pocket_time(traj({0.99, 0.01, 0.0}), p, 5.0) == 0.0);
    CHECK(ball_pocket_time(traj({0.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}), p, 5.0) == kNever);
    CHECK(ball_pocket_time(traj({0.0, 0.05, 0.0}, {1.0, 0.0, 0.0}), p, 5.0) == kNever);
    CHECK(ball_pocket_time(traj({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), p, 0.9) == kNever);
    // Rolling to a stop: 0.5 t - 0.25 t^2 reaches 0.25 at t = 1, so it stops short of 0.95 - 0.5.
    CHECK(ball_pocket_time(traj({0.5, 0.0, 0.0}, {0.5, 0.0, 0.0}, {-0.25, 0.0, 0.0}, 1.0), p, 5.0) == kNever);
}

TEST_CASE("detection agrees with a brute-force search on random cases") {
    Rng rng(147147);
    constexpr int kSamples = 2000;
    auto rand_vec = [&](double scale) { return Vec3{scale * rng.uniform(-1.0, 1.0), scale * rng.uniform(-1.0, 1.0), 0.0}; };
    // An offset at least `min_len` long, so no case starts in contact (those have their own tests).
    auto rand_offset = [&](double scale, double min_len) {
        Vec3 off = rand_vec(scale);
        while (std::sqrt(off.x * off.x + off.y * off.y) < min_len + 1e-6) off = rand_vec(scale);
        return off;
    };
    // Half the cases are aimed roughly along -off, so plenty of them make contact.
    auto rand_velocity = [&](int i, Vec3 off, double scale) {
        if (i % 2 == 0) return rand_vec(scale);
        const double len = std::sqrt(off.x * off.x + off.y * off.y);
        return off * (-rng.uniform(0.3, 1.0) * scale / len) + rand_vec(0.2 * scale);
    };
    auto accept_all = [](double) { return true; };

    Tally pairs;
    for (int i = 0; i < 20000; ++i) {
        const Vec3 off = rand_offset(0.4, 2.0 * R);
        const Trajectory a = traj(rand_vec(1.0), rand_vec(1.0), rand_vec(i % 4 == 0 ? 0.0 : 1.0));
        const Vec3 b1 = i % 3 == 0 ? Vec3{} : rand_vec(1.0);
        const Trajectory b = traj(a.c0 + off, a.c1 + rand_velocity(i, off, 3.0) + b1, rand_vec(i % 5 == 0 ? 0.0 : 1.0));
        const double hi = rng.uniform(0.05, 2.0);
        auto gap = [&](double t) { return ball_gap(a, b, t); };
        compare(pairs, ball_ball_time(a, b, R, hi), brute_first_fall(gap, accept_all, hi, kSamples), gap, hi, kSamples);
    }
    // Near-tangent passes: a stationary ball whose centre lies within 0.1% of 2R of a's path, with
    // a little curvature, so many contacts are shallower than the samples can see.
    Tally grazes;
    for (int i = 0; i < 5000; ++i) {
        const double speed = rng.uniform(0.2, 3.0);
        const double heading = rng.uniform(-kPi, kPi);
        const Vec3 dir{std::cos(heading), std::sin(heading), 0.0};
        const Vec3 normal{-dir.y, dir.x, 0.0};
        const Vec3 c0 = rand_vec(1.0);
        const double along = rng.uniform(0.1, 1.0);
        const double miss = 2.0 * R * (1.0 + rng.uniform(-1e-3, 1e-3));
        const Trajectory a = traj(c0, dir * speed, normal * rng.uniform(-1e-3, 1e-3) + dir * rng.uniform(-0.2, 0.0));
        const Trajectory b = traj(c0 + dir * along + normal * miss);
        const double hi = 2.0 * along / speed + 0.1;
        auto gap = [&](double t) { return ball_gap(a, b, t); };
        // Coarse samples on purpose: most contacts here last less than a step.
        constexpr int kCoarse = 100;
        const double ours = ball_ball_time(a, b, R, hi);
        compare(grazes, ours, brute_first_fall(gap, accept_all, hi, kCoarse), gap, hi, kCoarse);
        // A miss reported as none must survive a dense search as well.
        if (ours == kNever) CHECK(brute_first_fall(gap, accept_all, hi, 10000) == kNever);
    }

    Tally lines;
    for (int i = 0; i < 10000; ++i) {
        const Vec3 p1 = rand_vec(1.0);
        const Vec3 along = rand_vec(1.0);
        const double len = std::sqrt(along.x * along.x + along.y * along.y);
        const double side = rng.uniform() < 0.5 ? -1.0 : 1.0;
        const LineCushion c{{p1.x, p1.y}, {p1.x + along.x, p1.y + along.y}, {-side * along.y / len, side * along.x / len}};
        const double s = rng.uniform(-0.2, 1.2) * len;
        const double h = rng.uniform(-0.05, 0.5);
        const Vec3 start{p1.x + along.x / len * s + c.normal.x * (R + h + 1e-6), p1.y + along.y / len * s + c.normal.y * (R + h + 1e-6), 0.0};
        const Trajectory a = traj(start, rand_vec(3.0), rand_vec(i % 4 == 0 ? 0.0 : 1.0), rng.uniform(0.2, 3.0));
        const double hi = std::min(a.duration, rng.uniform(0.05, 2.0));
        auto gap = [&](double t) {
            const Vec3 r = a.at(t);
            return c.normal.x * (r.x - c.p1.x) + c.normal.y * (r.y - c.p1.y) - R;
        };
        auto on_segment = [&](double t) {
            const Vec3 r = a.at(t);
            const double u = ((r.x - c.p1.x) * along.x + (r.y - c.p1.y) * along.y) / len;
            return u >= -1e-9 && u <= len + 1e-9;
        };
        // Starts behind the nose line count only if the centre is still on the playing side.
        if (gap(0.0) <= 0.0) continue;
        compare(lines, ball_line_time(a, c, R, hi), brute_first_fall(gap, on_segment, hi, kSamples), gap, hi, kSamples);
    }

    Tally arcs;
    for (int i = 0; i < 10000; ++i) {
        const double start = rng.uniform(-kPi, kPi);
        const ArcCushion c{{rng.uniform(-1.0, 1.0), rng.uniform(-1.0, 1.0)}, rng.uniform(0.005, 0.05), start,
                           start + rng.uniform(0.2, 5.5), 0};
        const Vec3 centre{c.centre.x, c.centre.y, 0.0};
        const Vec3 off = rand_offset(0.3, c.radius + R);
        const Trajectory a = traj(centre + off, rand_velocity(i, off, 3.0), rand_vec(i % 4 == 0 ? 0.0 : 1.0));
        const double hi = rng.uniform(0.05, 2.0);
        auto gap = [&](double t) {
            const Vec3 d = a.at(t) - centre;
            return std::sqrt(d.x * d.x + d.y * d.y) - (c.radius + R);
        };
        auto on_arc = [&](double t) {
            const Vec3 d = a.at(t) - centre;
            return brute_in_arc(c, d.x, d.y);
        };
        compare(arcs, ball_arc_time(a, c, R, hi), brute_first_fall(gap, on_arc, hi, kSamples), gap, hi, kSamples);
    }

    Tally pockets;
    for (int i = 0; i < 10000; ++i) {
        const Pocket p{"middle_left", false, {rng.uniform(-1.0, 1.0), rng.uniform(-1.0, 1.0)}, rng.uniform(0.04, 0.08)};
        const Vec3 centre{p.drop_centre.x, p.drop_centre.y, 0.0};
        const Vec3 off = rand_offset(0.4, p.drop_radius);
        const Trajectory a = traj(centre + off, rand_velocity(i, off, 3.0), rand_vec(i % 4 == 0 ? 0.0 : 1.0));
        const double hi = rng.uniform(0.05, 2.0);
        auto gap = [&](double t) {
            const Vec3 d = a.at(t) - centre;
            return std::sqrt(d.x * d.x + d.y * d.y) - p.drop_radius;
        };
        compare(pockets, ball_pocket_time(a, p, hi), brute_first_fall(gap, accept_all, hi, kSamples), gap, hi,
                kSamples);
    }

    const struct {
        std::string name;
        const Tally* tally;
    } kinds[] = {{"ball-ball", &pairs}, {"ball-ball grazing", &grazes}, {"ball-line", &lines},
                 {"ball-arc", &arcs}, {"ball-pocket", &pockets}};
    for (const auto& k : kinds) {
        const Tally& t = *k.tally;
        MESSAGE(k.name << ": " << t.cases << " cases, " << t.contacts << " contacts agree, " << t.none
                       << " with none, " << t.between << " earlier contacts between samples, " << t.disagreements
                       << " disagreements");
        CHECK(t.disagreements == 0);
    }
    CHECK(grazes.between > 100);
    CHECK(pairs.cases >= 20000);
    CHECK(pairs.contacts > 2000);
    CHECK(lines.contacts > 1000);
    CHECK(arcs.contacts > 1000);
    CHECK(pockets.contacts > 1000);
}

TEST_CASE("ball_ball_time: rounding noise in the relative acceleration doesn't hide a contact") {
    // fuzz seed 1 shot 6505: two balls leaving the baulk cushion with one cloth deceleration, so
    // the relative path is a straight line, except for c2.y = -8.8e-17 left by rounding. That gave
    // a quartic whose top coefficients are ~1e-16 and ~1e-32, and the cubic estimate of its
    // derivative's roots lost the closest approach at 0.287 s: the balls passed through each other.
    const Trajectory a = traj({-1.758250000, 0.203470298}, {1.9844, -0.797041}, {-0.981, -8.7648e-17}, 0.36191);
    const Trajectory b = traj({-1.734537100, -0.134754844}, {1.73902, 0.34733}, {-0.981, 0.0}, 0.310352);
    const double t = ball_ball_time(a, b, R, 10.0);
    REQUIRE(t < kNever);
    const Vec3 d = a.at(t) - b.at(t);
    CHECK(std::hypot(d.x, d.y) == doctest::Approx(2.0 * R).epsilon(1e-12));
    Trajectory clean = a;
    clean.c2.y = 0.0;
    CHECK(t == doctest::Approx(ball_ball_time(clean, b, R, 10.0)).epsilon(1e-12));
}

TEST_CASE("detection micro-benchmark" * doctest::skip()) {
    Rng rng(99);
    constexpr int kSets = 1024;
    std::vector<Trajectory> far_a, far_b, near_a, near_b;
    for (int i = 0; i < kSets; ++i) {
        const Vec3 c0{rng.uniform(-1.5, 1.5), rng.uniform(-0.8, 0.8), 0.0};
        // Far: 0.3-0.8 m apart, slow relative motion over a 0.1 s window.
        far_a.push_back(traj(c0, {rng.uniform(-1.0, 1.0), rng.uniform(-1.0, 1.0), 0.0}, {0.1, -0.2, 0.0}));
        far_b.push_back(traj(c0 + Vec3{0.3 + 0.5 * rng.uniform(), 0.0, 0.0}, {}, {}));
        // Near: aimed at each other, both sliding, so the quartic is solved.
        near_a.push_back(traj(c0, {2.0, rng.uniform(-0.05, 0.05), 0.0}, {-0.8, 0.3, 0.0}));
        near_b.push_back(traj(c0 + Vec3{0.3, rng.uniform(-0.05, 0.05), 0.0}, {-0.5, 0.1, 0.0}, {0.2, 0.1, 0.0}));
    }
    // The best of several trials: the machine is shared, and the minimum is the cost without
    // interference.
    auto best_ns = [](auto&& body) {
        double best = 1e300;
        for (int trial = 0; trial < 7; ++trial) {
            const auto t0 = std::chrono::steady_clock::now();
            body();
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count());
        }
        return best;
    };
    constexpr int kReps = 500;
    const double calls = double(kReps) * kSets;
    double sink = 0.0;
    int hits = 0;
    auto pairs = [&](const std::vector<Trajectory>& a, const std::vector<Trajectory>& b, double t_max) {
        hits = 0;
        for (int rep = 0; rep < kReps; ++rep) {
            for (int i = 0; i < kSets; ++i) {
                const double t = ball_ball_time(a[i], b[i], R, t_max);
                if (t != kNever) {
                    sink += t;
                    ++hits;
                }
            }
        }
    };
    double ns = best_ns([&] { pairs(far_a, far_b, 0.1); }) / calls;
    std::printf("ball_ball_time far apart: %.1f ns/call (%.0f%% with an event)\n", ns, 100.0 * hits / calls);
    ns = best_ns([&] { pairs(near_a, near_b, 2.0); }) / calls;
    std::printf("ball_ball_time quartic solved: %.1f ns/call (%.0f%% with an event)\n", ns, 100.0 * hits / calls);

    const LineCushion c{{1.7845, -0.8}, {1.7845, 0.8}, {-1.0, 0.0}};
    ns = best_ns([&] {
        for (int rep = 0; rep < kReps; ++rep) {
            for (int i = 0; i < kSets; ++i) {
                const double t = ball_line_time(near_a[i], c, R, 2.0);
                if (t != kNever) sink += t;
            }
        }
    }) / calls;
    std::printf("ball_line_time: %.1f ns/call [sink %g]\n", ns, sink);
}
