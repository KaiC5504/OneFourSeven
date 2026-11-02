#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "ofs/physics/motion.hpp"
#include "ofs/physics/nap.hpp"

// The behaviours of spec 7.8, each its own case. The magnitudes are the owner's to tune, so these
// check directions, shapes and limits; magnitudes only where nap.cpp's model fixes them.

using namespace ofs::phys;
using ofs::Vec3;

namespace {

constexpr double kR = ofs::table::kBallRadius;
constexpr double kPi = 3.14159265358979323846;

// cos and sin, without the rounding residue at the axes, so 0, pi/2 and pi head exactly along them.
Vec3 direction(double heading) {
    Vec3 d{std::cos(heading), std::sin(heading), 0.0};
    if (std::abs(d.x) < 1e-15) d.x = 0.0;
    if (std::abs(d.y) < 1e-15) d.y = 0.0;
    return d;
}

BallState rolling(double heading, double speed, double wz) {
    BallState b;
    b.v = direction(heading) * speed;
    b.w = {-b.v.y / kR, b.v.x / kR, wz};
    b.motion = Motion::rolling;
    return b;
}

// The ball's right, seen from behind it.
Vec3 right_of(const BallState& b) {
    const Vec3 h = b.v / norm_xy(b.v);
    return {h.y, -h.x, 0.0};
}

double along_decel(const BallState& b, const PhysicsParams& p, Vec3 nap) {
    return p.roll_friction * p.gravity_m_s2 - dot(nap, b.v / norm_xy(b.v));
}

// Rolls a lone ball to rest the way the simulator steps it: a segment per nap value, the nap
// re-evaluated at each threshold, transitions completed. Returns each segment's start and nap.
struct Step {
    BallState start;
    Vec3 nap;
    double duration = 0.0;
};

std::vector<Step> roll_out(BallState b, const PhysicsParams& p) {
    std::vector<Step> steps;
    for (int i = 0; i < 1000 && b.motion != Motion::stationary; ++i) {
        const Vec3 nap = nap_acceleration(b, p);
        const double end = transition_time(b, p, nap);
        const double retick =
            b.motion == Motion::rolling ? nap_retick_time(b, p, along_decel(b, p, nap)) : kNever;
        steps.push_back({b, nap, std::min(end, retick)});
        b = retick < end ? evolve(b, retick, p, nap) : complete_transition(evolve(b, end, p, nap), p);
    }
    REQUIRE(b.motion == Motion::stationary);
    steps.push_back({b, {}, 0.0});
    return steps;
}

}  // namespace

TEST_CASE("nap: a plain ball rolling with or against the nap feels nothing sideways") {
    const PhysicsParams p;
    for (double heading : {0.0, kPi}) {
        for (double speed : {0.05, 0.3, 1.0, 1.5}) {
            const BallState b = rolling(heading, speed, 0.0);
            const Vec3 a = nap_acceleration(b, p);
            CHECK(a.x == 0.0);
            CHECK(a.y == 0.0);
            CHECK(a.z == 0.0);
        }
    }
}

TEST_CASE("nap: a plain ball rolling across the nap drifts towards the top") {
    const PhysicsParams p;
    for (double heading : {kPi / 2.0, -kPi / 2.0}) {
        for (double speed : {0.05, 0.3, 1.0}) {
            const BallState b = rolling(heading, speed, 0.0);
            const Vec3 a = nap_acceleration(b, p);
            CHECK(a.x > 0.0);
            CHECK(std::abs(a.y) <= 1e-12 * a.x);
            // nap.cpp: nap_lateral_coeff g at nap_speed_ref, scaled by 2 ref / (speed + ref).
            const double expected = p.nap_lateral_coeff * p.gravity_m_s2 * 2.0 * p.nap_speed_ref_m_s
                                    / (speed + p.nap_speed_ref_m_s);
            CHECK(a.x == doctest::Approx(expected).epsilon(1e-12));
        }
    }
    const Vec3 at_ref = nap_acceleration(rolling(kPi / 2.0, p.nap_speed_ref_m_s, 0.0), p);
    CHECK(at_ref.x == doctest::Approx(p.nap_lateral_coeff * p.gravity_m_s2).epsilon(1e-12));
}

TEST_CASE("nap: up the table, side drifts the ball the way of the side") {
    const PhysicsParams p;
    for (double speed : {0.05, 0.3, 1.0}) {
        // Right-hand side spins anticlockwise from above (w.z > 0), as the cue strike gives it.
        const BallState right_side = rolling(0.0, speed, 20.0);
        CHECK(dot(nap_acceleration(right_side, p), right_of(right_side)) > 0.0);
        const BallState left_side = rolling(0.0, speed, -20.0);
        CHECK(dot(nap_acceleration(left_side, p), right_of(left_side)) < 0.0);
    }
}

TEST_CASE("nap: against the nap the side's drift reverses") {
    const PhysicsParams p;
    for (double speed : {0.05, 0.3, 1.0}) {
        const BallState right_side = rolling(kPi, speed, 20.0);
        CHECK(dot(nap_acceleration(right_side, p), right_of(right_side)) < 0.0);
        const BallState left_side = rolling(kPi, speed, -20.0);
        CHECK(dot(nap_acceleration(left_side, p), right_of(left_side)) > 0.0);
        // Mirror images: the same size of drift up and down the table.
        CHECK(norm(nap_acceleration(right_side, p)) ==
              doctest::Approx(norm(nap_acceleration(rolling(0.0, speed, 20.0), p))).epsilon(1e-12));
    }
}

TEST_CASE("nap: the drift blends smoothly with heading") {
    const PhysicsParams p;
    for (double wz : {0.0, 15.0, -15.0}) {
        const double step = 2.0 * kPi / 3600.0;
        // |da/dheading| <= 2 g f (nap_lateral_coeff + nap_spin_coeff), with f = 1 at this speed.
        const double bound = 2.0 * step * (p.nap_lateral_coeff + p.nap_spin_coeff) * p.gravity_m_s2 * 1.01;
        Vec3 previous = nap_acceleration(rolling(0.0, 0.3, wz), p);
        double largest = 0.0;
        for (int i = 1; i <= 3600; ++i) {
            const Vec3 a = nap_acceleration(rolling(i * step, 0.3, wz), p);
            largest = std::max(largest, norm(a));
            CHECK(norm(a - previous) <= bound);
            previous = a;
        }
        CHECK(largest > 0.0);
    }
    // Half-way between with the nap and across it, the lateral term is sin 45 of its full size.
    const Vec3 across = nap_acceleration(rolling(kPi / 2.0, 0.3, 0.0), p);
    const Vec3 diagonal = nap_acceleration(rolling(kPi / 4.0, 0.3, 0.0), p);
    CHECK(norm(diagonal) == doctest::Approx(norm(across) * std::sqrt(0.5)).epsilon(1e-12));
}

TEST_CASE("nap: it grows as the ball slows and is gone above nap_speed_max") {
    const PhysicsParams p;
    for (double wz : {0.0, 10.0}) {
        double previous = 0.0;
        for (double speed = p.nap_speed_max_m_s; speed > 0.025; speed *= 0.97) {
            const double a = norm(nap_acceleration(rolling(1.0, speed, wz), p));
            CHECK(a > previous);
            previous = a;
        }
    }
    CHECK(norm(nap_acceleration(rolling(1.0, p.nap_speed_max_m_s, 0.0), p)) > 0.0);
    for (double speed : {p.nap_speed_max_m_s + 1e-6, 2.0, 5.0, 10.0}) {
        CHECK(nap_acceleration(rolling(1.0, speed, 10.0), p) == Vec3{});
    }
    PhysicsParams none = p;
    none.nap_speed_max_m_s = 0.0;
    CHECK(nap_acceleration(rolling(1.0, 0.3, 10.0), none) == Vec3{});
}

TEST_CASE("nap: only rolling balls feel it") {
    const PhysicsParams p;
    for (Motion motion : {Motion::sliding, Motion::spinning, Motion::stationary, Motion::pocketed}) {
        BallState b = rolling(1.0, 0.3, 10.0);
        b.motion = motion;
        CHECK(nap_acceleration(b, p) == Vec3{});
        CHECK(nap_retick_time(b, p, 0.08) == kNever);
    }
}

TEST_CASE("nap: it never adds energy") {
    PhysicsParams strong;
    strong.nap_lateral_coeff = 0.01;
    strong.nap_spin_coeff = 0.01;
    for (const PhysicsParams& p : {PhysicsParams{}, strong}) {
        int felt = 0;
        for (int i = 0; i < 720; ++i) {
            const double heading = i * kPi / 360.0 + 1e-3;
            for (double speed : {0.021, 0.05, 0.1234, 0.3, 0.77, 1.2, 1.5}) {
                for (double wz : {-150.0, -20.0, -1.0, 0.0, 0.3, 7.0, 90.0}) {
                    const BallState b = rolling(heading, speed, wz);
                    const Vec3 a = nap_acceleration(b, p);
                    REQUIRE(dot(a, b.v) <= 0.0);
                    // Sideways but for the rounding guard: the along part is a sliver.
                    REQUIRE(-dot(a, b.v) <= 1e-11 * norm(a) * speed);
                    if (norm(a) > 0.0) ++felt;
                }
            }
        }
        CHECK(felt > 25000);
    }
}

TEST_CASE("nap: at the loader's extreme magnitudes kinetic energy never rises within a segment") {
    // Held through a segment, a sideways push k raises the speed before the next re-evaluation
    // when (k / a)^2 > r / (1 - r), a = roll_friction g, r the ratio the segment spans (nap.cpp).
    // Without the cap, nap_lateral_coeff 0.01 with nap_speed_ref_m_s 5 takes a ball rolling across
    // the nap from its lowest point in a segment back up by 20-55% in energy by the segment's end.
    PhysicsParams p;
    p.nap_lateral_coeff = 0.01;
    p.nap_spin_coeff = 0.01;
    p.nap_speed_ref_m_s = 5.0;
    p.nap_speed_max_m_s = 20.0;
    p.nap_speed_min_m_s = 1e-4;
    int capped = 0;
    for (double ratio : {0.05, 0.5, 0.95}) {
        p.nap_tick_ratio = ratio;
        for (double roll : {1e-3, 0.01, 0.1}) {
            p.roll_friction = roll;
            // nap.cpp's slack lets a segment span ratio / 1.01.
            const double r = ratio / 1.01;
            const double bound = roll * p.gravity_m_s2 * std::sqrt(r / (1.0 - r));
            // Speeds just inside and just outside the slack above a threshold span the most.
            std::vector<double> speeds{0.3, 2.0, 15.0};
            for (int k : {1, 2, 5, 12, 30, 80}) {
                const double threshold = p.nap_speed_max_m_s * std::pow(ratio, k);
                if (threshold < 1e-3) break;
                speeds.push_back(threshold * 1.0099);
                speeds.push_back(threshold * 1.0101);
            }
            for (double heading : {0.3, kPi / 2.0, 2.0, -1.0}) {
                for (double wz : {-80.0, 0.0, 40.0}) {
                    for (double speed : speeds) {
                        CAPTURE(ratio);
                        CAPTURE(roll);
                        CAPTURE(heading);
                        CAPTURE(wz);
                        CAPTURE(speed);
                        const std::vector<Step> steps = roll_out(rolling(heading, speed, wz), p);
                        for (const Step& s : steps) {
                            const double k = norm(s.nap);
                            REQUIRE(k <= bound * (1.0 + 1e-12));
                            if (k > 0.999 * bound) ++capped;
                            double previous = kinetic_energy(s.start, p);
                            for (int i = 1; i <= 50; ++i) {
                                const double e = kinetic_energy(evolve(s.start, s.duration * i / 50.0, p, s.nap), p);
                                REQUIRE(e <= previous * (1.0 + 1e-12));
                                previous = e;
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK(capped > 1000);
}

TEST_CASE("nap: re-evaluation thresholds run nap_speed_max * nap_tick_ratio^k down to nap_speed_min") {
    const PhysicsParams p;  // 1.5, 0.75, 0.375, 0.1875, 0.09375, 0.046875, 0.0234375
    const double d = 0.08;
    CHECK(nap_retick_time(rolling(0.3, 3.0, 0.0), p, d) == doctest::Approx(1.5 / d).epsilon(1e-12));
    CHECK(nap_retick_time(rolling(0.3, 1.5, 0.0), p, d) == doctest::Approx(0.75 / d).epsilon(1e-12));
    CHECK(nap_retick_time(rolling(0.3, 1.0, 0.0), p, d) == doctest::Approx(0.25 / d).epsilon(1e-12));
    CHECK(nap_retick_time(rolling(0.3, 0.75, 0.0), p, d) == doctest::Approx(0.375 / d).epsilon(1e-12));
    CHECK(nap_retick_time(rolling(0.3, 0.03, 0.0), p, d) ==
          doctest::Approx((0.03 - 0.0234375) / d).epsilon(1e-12));
    // Just re-evaluated at 0.375, with the sideways speed it picked up: the next is 0.1875, not a
    // second stop at 0.375 a moment later.
    const double just_past = 0.375 * (1.0 + 1e-3);
    CHECK(nap_retick_time(rolling(0.3, just_past, 0.0), p, d) ==
          doctest::Approx((just_past - 0.1875) / d).epsilon(1e-12));
    // Rounding at the top: the first threshold counts as reached.
    CHECK(nap_retick_time(rolling(0.3, 1.5 + 1e-13, 0.0), p, d) ==
          doctest::Approx((1.5 + 1e-13 - 0.75) / d).epsilon(1e-12));

    // None left above nap_speed_min: the lowest threshold, and below it.
    for (double speed : {0.0234375, 0.022, 0.02, 0.01, 0.0}) {
        CHECK(nap_retick_time(rolling(0.3, speed, 0.0), p, d) == kNever);
        CHECK(nap_acceleration(rolling(0.3, speed, 10.0), p) == Vec3{});
    }
    CHECK(nap_retick_time(rolling(0.3, 1.0, 0.0), p, 0.0) == kNever);
    CHECK(nap_retick_time(rolling(0.3, 1.0, 0.0), p, -0.1) == kNever);

    // Nap felt exactly where another re-evaluation is due.
    for (double speed = 0.015; speed < 3.0; speed *= 1.013) {
        const BallState b = rolling(1.0, speed, 0.0);
        const bool felt = norm(nap_acceleration(b, p)) > 0.0;
        const bool due = nap_retick_time(b, p, d) != kNever;
        CHECK(felt == (due && speed <= p.nap_speed_max_m_s));
    }
}

TEST_CASE("nap: a ratio outside (0, 1) leaves no thresholds rather than hanging") {
    PhysicsParams p;
    for (double ratio : {1.0, 1.5, 0.0, -0.5}) {
        p.nap_tick_ratio = ratio;
        CHECK(nap_retick_time(rolling(0.3, 1.0, 0.0), p, 0.08) == kNever);
        CHECK(nap_acceleration(rolling(0.3, 1.0, 10.0), p) == Vec3{});
    }
}

// The four behaviours of spec 7.8 end to end, with the motion of motion.cpp.
TEST_CASE("nap: slow balls drift as spec 7.8 describes, firm ones barely") {
    const PhysicsParams p;
    struct Case {
        double heading;
        double wz;
        double expected_right;  // sign of the final offset right of the starting line
    };
    for (const Case& c : {Case{0.0, 25.0, 1.0}, Case{0.0, -25.0, -1.0}, Case{kPi, 25.0, -1.0},
                          Case{kPi, -25.0, 1.0}, Case{0.0, 0.0, 0.0}, Case{kPi, 0.0, 0.0}}) {
        CAPTURE(c.heading);
        CAPTURE(c.wz);
        const BallState start = rolling(c.heading, 0.6, c.wz);
        const std::vector<Step> steps = roll_out(start, p);
        const BallState& end = steps.back().start;
        const double travelled = norm(end.r - start.r);
        const double offset = dot(end.r - start.r, right_of(start));
        if (c.expected_right == 0.0) {
            CHECK(offset == 0.0);
        } else {
            CHECK(c.expected_right * offset > 1e-3);
            CHECK(std::abs(offset) < 0.05 * travelled);
        }
    }
    for (double heading : {kPi / 2.0, -kPi / 2.0}) {
        const BallState start = rolling(heading, 0.4, 0.0);
        const BallState end = roll_out(start, p).back().start;
        CHECK(end.r.x > 1e-3);
        CHECK(end.r.x < 0.05 * norm(end.r - start.r));
    }
    // A firm ball across the nap: nothing until it has slowed to nap_speed_max.
    const std::vector<Step> firm = roll_out(rolling(kPi / 2.0, 3.0, 0.0), p);
    REQUIRE(firm.size() > 2);
    CHECK(firm[0].nap == Vec3{});
    CHECK(firm[1].start.r.x == 0.0);
    CHECK(firm[1].start.v.x == 0.0);
    CHECK(norm_xy(firm[1].start.v) == doctest::Approx(p.nap_speed_max_m_s).epsilon(1e-12));
    CHECK(norm(firm[1].nap) > 0.0);
}

TEST_CASE("nap: kinetic energy never rises through a whole roll-out") {
    const PhysicsParams p;
    for (double heading : {0.0, 0.7, kPi / 2.0, 2.5, kPi, -1.2}) {
        for (double wz : {-60.0, 0.0, 35.0}) {
            for (double speed : {0.1, 0.6, 2.0}) {
                const std::vector<Step> steps = roll_out(rolling(heading, speed, wz), p);
                double previous = kinetic_energy(steps.front().start, p);
                const double tolerance = 1e-14 * previous;
                for (const Step& s : steps) {
                    for (int k = 0; k <= 50; ++k) {
                        const double e = kinetic_energy(evolve(s.start, s.duration * k / 50.0, p, s.nap), p);
                        REQUIRE(e <= previous + tolerance);
                        previous = e;
                    }
                }
                CHECK(steps.size() < 20);
            }
        }
    }
}
