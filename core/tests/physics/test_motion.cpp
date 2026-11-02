#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "ofs/physics/motion.hpp"
#include "ofs/physics/nap.hpp"
#include "ofs/rng.hpp"

using namespace ofs::phys;
using ofs::Vec3;

namespace {

constexpr double kR = ofs::table::kBallRadius;

BallState make_ball(Vec3 v, Vec3 w, Motion motion) {
    BallState b;
    b.r = {0.1, -0.2, 0.0};
    b.v = v;
    b.w = w;
    b.motion = motion;
    return b;
}

BallState rolling_ball(Vec3 v, double wz) { return make_ball(v, {-v.y / kR, v.x / kR, wz}, Motion::rolling); }

bool close(double a, double b, double rel) {
    return std::abs(a - b) <= rel * std::max(std::abs(a), std::abs(b));
}

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool finite(const BallState& b) { return finite(b.r) && finite(b.v) && finite(b.w); }

// A random ball in `motion` with speeds and spins across what play produces.
BallState random_ball(ofs::Rng& rng, Motion motion) {
    const double heading = rng.uniform(-3.14159, 3.14159);
    const double speed = rng.uniform(0.02, 6.0);
    const Vec3 v{speed * std::cos(heading), speed * std::sin(heading), 0.0};
    const double wz = rng.uniform(-150.0, 150.0);
    switch (motion) {
        case Motion::sliding:
            return make_ball(v, {rng.uniform(-200.0, 200.0), rng.uniform(-200.0, 200.0), wz}, motion);
        case Motion::rolling:
            return rolling_ball(v, wz);
        case Motion::spinning:
            return make_ball({}, {0.0, 0.0, wz}, motion);
        default:
            return make_ball({}, {}, motion);
    }
}

// The nap a rolling ball feels and how long it holds, as the simulator will use them.
struct NapSegment {
    Vec3 nap;
    double duration = kNever;
};

NapSegment nap_segment(const BallState& b, const PhysicsParams& p) {
    const Vec3 nap = nap_acceleration(b, p);
    double duration = transition_time(b, p, nap);
    if (b.motion == Motion::rolling) {
        const Vec3 heading = b.v / norm_xy(b.v);
        const double along_decel = p.roll_friction * p.gravity_m_s2 - dot(nap, heading);
        duration = std::min(duration, nap_retick_time(b, p, along_decel));
    }
    return {nap, duration};
}

}  // namespace

// Alciatore TP 4.1: a ball struck at centre height with no spin slides with u0 = v0, grips after
// t = 2 v0 / (7 mu_s g), d = 12 v0^2 / (49 mu_s g) from the strike, at 5/7 v0 (C1).
TEST_CASE("motion: a centre-ball hit grips where and when TP 4.1 says") {
    const PhysicsParams p;
    const double mu_g = p.slide_friction * p.gravity_m_s2;
    for (double v0 : {0.5, 1.0, 2.0, 4.0}) {
        CAPTURE(v0);
        for (double aim : {0.0, 2.3}) {
            CAPTURE(aim);
            const Vec3 dir{std::cos(aim), std::sin(aim), 0.0};
            const BallState b = make_ball(dir * v0, {}, Motion::sliding);
            CHECK(classify(b, p) == Motion::sliding);
            const double t = transition_time(b, p, {});
            CHECK(close(t, 2.0 * v0 / (7.0 * mu_g), 1e-12));
            const BallState end = evolve(b, t, p, {});
            CHECK(close(norm(end.r - b.r), 12.0 * v0 * v0 / (49.0 * mu_g), 1e-12));
            CHECK(close(norm(end.v), 5.0 / 7.0 * v0, 1e-12));
            CHECK(norm(contact_slip(end)) < 1e-12 * v0);
            const BallState rolling = complete_transition(end, p);
            CHECK(rolling.motion == Motion::rolling);
            CHECK(contact_slip(rolling) == Vec3{});
            CHECK(close(norm(rolling.v), 5.0 / 7.0 * v0, 1e-12));
            CHECK(rolling.r == end.r);
        }
    }
}

// Constant deceleration mu_r g (pooltool get_roll_time; Kiefl, pooltool theory 3): stops after
// v / (mu_r g), v^2 / (2 mu_r g) on.
TEST_CASE("motion: a rolling ball stops after v / (mu_r g) and v^2 / (2 mu_r g)") {
    const PhysicsParams p;
    const double mu_g = p.roll_friction * p.gravity_m_s2;
    for (double speed : {0.05, 0.5, 2.0, 5.0}) {
        CAPTURE(speed);
        const BallState b = rolling_ball(Vec3{0.6, -0.8, 0.0} * speed, 0.0);
        CHECK(classify(b, p) == Motion::rolling);
        const double t = transition_time(b, p, {});
        CHECK(close(t, speed / mu_g, 1e-12));
        const BallState end = evolve(b, t, p, {});
        CHECK(close(norm(end.r - b.r), speed * speed / (2.0 * mu_g), 1e-12));
        CHECK(norm(end.v) <= 1e-14 * speed);
        const BallState halfway = evolve(b, t / 2.0, p, {});
        CHECK(close(norm(halfway.v), speed / 2.0, 1e-12));
        CHECK(norm(contact_slip(halfway)) <= 1e-15 * speed);
        const BallState rest = complete_transition(end, p);
        CHECK(rest.motion == Motion::stationary);
        CHECK(rest.v == Vec3{});
        CHECK(rest.w == Vec3{});
        CHECK(rest.r == end.r);
    }
}

TEST_CASE("motion: a rolling ball with side left over ends spinning") {
    const PhysicsParams p;
    const BallState b = rolling_ball({0.02, 0.0, 0.0}, -40.0);
    const double t = transition_time(b, p, {});
    const BallState end = complete_transition(evolve(b, t, p, {}), p);
    CHECK(end.motion == Motion::spinning);
    CHECK(end.v == Vec3{});
    CHECK(end.w.x == 0.0);
    CHECK(end.w.y == 0.0);
    CHECK(close(end.w.z, -40.0 + 2.5 * p.spin_friction_per_r * p.gravity_m_s2 * t, 1e-12));
}

// pooltool get_spin_time: |w.z| 2R / (5 mu_sp g) with mu_sp = spin_friction_per_r R.
TEST_CASE("motion: a spinning ball stops after |w.z| / (5/2 spin_friction_per_r g)") {
    const PhysicsParams p;
    const double decel = 2.5 * p.spin_friction_per_r * p.gravity_m_s2;
    for (double wz : {-30.0, 0.5, 80.0}) {
        CAPTURE(wz);
        const BallState b = make_ball({}, {0.0, 0.0, wz}, Motion::spinning);
        CHECK(classify(b, p) == Motion::spinning);
        const double t = transition_time(b, p, {});
        CHECK(close(t, std::abs(wz) / decel, 1e-12));
        CHECK(close(evolve(b, t / 2.0, p, {}).w.z, wz / 2.0, 1e-12));
        const BallState end = evolve(b, t, p, {});
        CHECK(end.w.z == 0.0);
        CHECK(end.r == b.r);
        const BallState rest = complete_transition(end, p);
        CHECK(rest.motion == Motion::stationary);
        CHECK(rest.w == Vec3{});
    }
}

// pooltool _evolve_perpendicular_spin_component: "You can't decay past 0 angular velocity".
TEST_CASE("motion: side spin decays to zero and stays there while rolling and sliding") {
    const PhysicsParams p;
    const double decel = 2.5 * p.spin_friction_per_r * p.gravity_m_s2;
    for (double wz : {3.0, -3.0}) {
        CAPTURE(wz);
        const BallState rolling = rolling_ball({1.0, 0.5, 0.0}, wz);
        const double spin_time = std::abs(wz) / decel;
        REQUIRE(transition_time(rolling, p, {}) > 5.0 * spin_time);
        CHECK(close(evolve(rolling, spin_time / 2.0, p, {}).w.z, wz / 2.0, 1e-12));
        for (double t : {spin_time, 1.5 * spin_time, 4.0 * spin_time}) {
            CHECK(evolve(rolling, t, p, {}).w.z == 0.0);
        }

        const BallState sliding = make_ball({4.0, 0.0, 0.0}, {0.0, -100.0, wz}, Motion::sliding);
        REQUIRE(transition_time(sliding, p, {}) > 3.0 * spin_time);
        CHECK(close(evolve(sliding, spin_time / 2.0, p, {}).w.z, wz / 2.0, 1e-12));
        for (double t : {spin_time, 2.0 * spin_time, 3.0 * spin_time}) {
            CHECK(evolve(sliding, t, p, {}).w.z == 0.0);
        }
    }
}

// The closed forms are exact, so stepping in two goes lands where one step does (spec 7.1).
// With a nap the rolling heading is the segment's start by contract (motion.hpp), so a restart
// mid-segment is a new segment; that case is left out.
TEST_CASE("motion: evolve(b, t1 + t2) matches evolve(evolve(b, t1), t2)") {
    const PhysicsParams p;
    ofs::Rng rng(147);
    for (Motion motion : {Motion::sliding, Motion::rolling, Motion::spinning}) {
        for (int i = 0; i < 200; ++i) {
            const BallState b = random_ball(rng, motion);
            const double t = transition_time(b, p, {});
            const double t1 = rng.uniform(0.0, 0.6) * t;
            const double t2 = rng.uniform(0.0, 0.4) * t;
            const BallState once = evolve(b, t1 + t2, p, {});
            const BallState twice = evolve(evolve(b, t1, p, {}), t2, p, {});
            const double moved = norm(once.r - b.r);
            CHECK(norm(once.r - twice.r) <= 1e-12 * moved);
            CHECK(norm(once.v - twice.v) <= 1e-12 * norm(b.v));
            CHECK(norm(once.w - twice.w) <= 1e-12 * norm(b.w));
            CHECK(twice.motion == motion);
        }
    }
}

TEST_CASE("motion: trajectory(b).at(t) is evolve(b, t).r exactly") {
    const PhysicsParams p;
    ofs::Rng rng(7);
    for (Motion motion : {Motion::sliding, Motion::rolling, Motion::spinning, Motion::stationary}) {
        for (int i = 0; i < 100; ++i) {
            const BallState b = random_ball(rng, motion);
            const Vec3 nap = motion == Motion::rolling ? Vec3{rng.uniform(-1e-3, 1e-3), 0.0, 0.0} : Vec3{};
            const Trajectory tr = trajectory(b, p, nap);
            CHECK(tr.duration == transition_time(b, p, nap));
            const double end = std::isfinite(tr.duration) ? tr.duration : 10.0;
            for (double f : {0.0, 0.1, 0.37, 0.5, 0.99, 1.0}) {
                const BallState e = evolve(b, f * end, p, nap);
                CHECK(tr.at(f * end) == e.r);
                if (motion == Motion::sliding || motion == Motion::rolling) {
                    CHECK(tr.velocity(f * end) == e.v);
                }
            }
        }
    }
}

TEST_CASE("motion: the rolling segment's acceleration is rolling resistance plus the nap") {
    const PhysicsParams p;
    const BallState b = rolling_ball({0.3, 0.4, 0.0}, 100.0);
    const Vec3 nap{1e-3, -2e-3, 0.0};
    const Trajectory tr = trajectory(b, p, nap);
    const Vec3 heading{0.6, 0.8, 0.0};
    const Vec3 a = heading * -(p.roll_friction * p.gravity_m_s2) + nap;
    CHECK(norm(tr.c2 * 2.0 - a) <= 1e-15);
    // The stop: the velocity along the starting heading reaches zero, not the speed.
    const double along_decel = p.roll_friction * p.gravity_m_s2 - dot(nap, heading);
    CHECK(close(tr.duration, 0.5 / along_decel, 1e-12));
    const BallState end = evolve(b, tr.duration, p, nap);
    CHECK(std::abs(dot(end.v, heading)) <= 1e-14);
    const BallState rest = complete_transition(end, p);
    CHECK(rest.motion == Motion::spinning);
    CHECK(rest.v == Vec3{});
}

// C12 within segments: sliding friction dissipates m mu_s g |u| (pooltool theory 3), rolling
// resistance and spin friction only remove energy, and the nap only bends the path.
TEST_CASE("motion: kinetic energy never rises within a segment, with and without nap") {
    const PhysicsParams p;
    ofs::Rng rng(2026);
    for (Motion motion : {Motion::sliding, Motion::rolling, Motion::spinning}) {
        for (bool with_nap : {false, true}) {
            for (int i = 0; i < 300; ++i) {
                BallState b = random_ball(rng, motion);
                // Within the nap's speeds, above the last stretch that rolls out without it.
                if (motion == Motion::rolling && with_nap) {
                    b = rolling_ball(b.v * (rng.uniform(0.025, 1.5) / norm_xy(b.v)), b.w.z);
                }
                const NapSegment s = with_nap ? nap_segment(b, p) : NapSegment{{}, transition_time(b, p, {})};
                if (motion == Motion::rolling && with_nap) CHECK(norm(s.nap) > 0.0);
                double previous = kinetic_energy(b, p);
                const double tolerance = 1e-14 * previous;
                for (int k = 1; k <= 400; ++k) {
                    const double e = kinetic_energy(evolve(b, s.duration * k / 400.0, p, s.nap), p);
                    REQUIRE(e <= previous + tolerance);
                    previous = e;
                }
            }
        }
    }
}

TEST_CASE("motion: kinetic energy is translational plus 2/5 m R^2 rotational") {
    PhysicsParams p;
    p.ball_mass_kg = 0.15;
    const BallState b = make_ball({1.0, -2.0, 0.0}, {3.0, 0.0, -4.0}, Motion::sliding);
    const double expected = 0.5 * 0.15 * 5.0 + 0.5 * (0.4 * 0.15 * kR * kR) * 25.0;
    CHECK(close(kinetic_energy(b, p), expected, 1e-15));
}

// Swerve (spec 7.1, 7.3; C10 sense). A tip impulse J along the cue d = (cos e, 0, -sin e) (aim
// +x, butt raised e), `side` R right of centre at centre height, meets the ball at
// Q = (-c cos e, -side R, c sin e) with c = R sqrt(1 - side^2). The ball gets m v = J d, the
// cloth taking the vertical part (spec 7.9), and I w = Q x J d = J side R (sin e, 0, cos e).
// With I = 2/5 m R^2 and V the planar speed: w = 5 side V / (2R) (tan e, 0, 1), the same sense as
// pooltool's cue_strike (right-hand side spins anticlockwise from above, w.z > 0). The forward
// tilt of the axis makes the slip u = (V, R w.x) point left, so friction pushes the ball right
// along a parabola until it grips, then it rolls on straight.
TEST_CASE("motion: an elevated cue with right-hand side swerves right") {
    const PhysicsParams p;
    for (double aim : {0.0, 2.0}) {
        for (double side : {0.5, -0.5, 0.0}) {
            CAPTURE(aim);
            CAPTURE(side);
            const double speed = 2.0;
            const double elevation = 0.5;
            const double c = std::cos(aim);
            const double s = std::sin(aim);
            const double w_along = 2.5 * side * speed * std::tan(elevation) / kR;
            const Vec3 heading{c, s, 0.0};
            const Vec3 right{s, -c, 0.0};
            const Vec3 w{w_along * c, w_along * s, 2.5 * side * speed / kR};
            const BallState b = make_ball(heading * speed, w, Motion::sliding);
            REQUIRE(classify(b, p) == Motion::sliding);
            const double t = transition_time(b, p, {});
            const BallState quarter = evolve(b, t / 4.0, p, {});
            const BallState half = evolve(b, t / 2.0, p, {});
            const BallState end = evolve(b, t, p, {});
            const double off_quarter = dot(quarter.r - b.r, right);
            const double off_half = dot(half.r - b.r, right);
            const double off_end = dot(end.r - b.r, right);
            if (side == 0.0) {
                CHECK(std::abs(off_end) <= 1e-15);
                continue;
            }
            const double sense = side > 0.0 ? 1.0 : -1.0;
            CHECK(sense * off_end > 0.01);
            // A parabola from a standing start sideways: four times as far at twice the time.
            CHECK(close(off_half, 4.0 * off_quarter, 1e-9));
            CHECK(close(off_end, 4.0 * off_half, 1e-9));
            CHECK(sense * dot(end.v, right) > 0.0);
            const BallState rolling = complete_transition(end, p);
            REQUIRE(rolling.motion == Motion::rolling);
            const Trajectory after = trajectory(rolling, p, {});
            CHECK(norm(cross(after.c1, after.c2)) <= 1e-14 * norm(after.c1) * norm(after.c2));
        }
    }
}

TEST_CASE("motion: classify") {
    const PhysicsParams p;
    CHECK(classify(make_ball({}, {}, Motion::rolling), p) == Motion::stationary);
    CHECK(classify(make_ball({}, {0.0, 0.0, 2.0}, Motion::stationary), p) == Motion::spinning);
    CHECK(classify(make_ball({}, {0.0, 0.0, 1e-9}, Motion::spinning), p) == Motion::stationary);
    CHECK(classify(make_ball({}, {0.0, 3.0, 0.0}, Motion::stationary), p) == Motion::sliding);
    CHECK(classify(rolling_ball({0.7, -0.1, 0.0}, 4.0), p) == Motion::rolling);
    CHECK(classify(make_ball({1e-10, 0.0, 0.0}, {0.0, 1e-10 / kR, 0.0}, Motion::sliding), p)
          == Motion::stationary);
    CHECK(classify(make_ball({1.0, 0.0, 0.0}, {0.0, (1.0 - 1e-12) / kR, 0.0}, Motion::sliding), p)
          == Motion::rolling);
    CHECK(classify(make_ball({1.0, 0.0, 0.0}, {0.0, (1.0 - 1e-6) / kR, 0.0}, Motion::rolling), p)
          == Motion::sliding);
    CHECK(classify(make_ball({1.0, 0.0, 0.0}, {}, Motion::rolling), p) == Motion::sliding);
    BallState potted = make_ball({1.0, 0.0, 0.0}, {}, Motion::pocketed);
    CHECK(classify(potted, p) == Motion::pocketed);
}

TEST_CASE("motion: complete_transition snaps exactly") {
    const PhysicsParams p;
    ofs::Rng rng(99);
    for (int i = 0; i < 2000; ++i) {
        const BallState b = random_ball(rng, Motion::sliding);
        const BallState end = complete_transition(evolve(b, transition_time(b, p, {}), p, {}), p);
        REQUIRE(end.motion == Motion::rolling);
        CHECK(contact_slip(end) == Vec3{});
        CHECK(classify(end, p) == Motion::rolling);
    }

    // Enough backspin that the ball stops dead as it grips: v_end = v0 - 2/7 u0 = 0 when
    // u0 = 7/2 v0, i.e. R w.y = -5/2 v0.
    const BallState stun = make_ball({1.0, 0.0, 0.0}, {0.0, -2.5 / kR, 0.0}, Motion::sliding);
    const BallState stopped = complete_transition(evolve(stun, transition_time(stun, p, {}), p, {}), p);
    CHECK(stopped.motion == Motion::stationary);
    CHECK(stopped.v == Vec3{});
    CHECK(stopped.w == Vec3{});
    BallState stun_side = stun;
    stun_side.w.z = 30.0;
    const double stun_side_time = transition_time(stun_side, p, {});
    const BallState spinning = complete_transition(evolve(stun_side, stun_side_time, p, {}), p);
    CHECK(spinning.motion == Motion::spinning);
    CHECK(spinning.v == Vec3{});
    CHECK(spinning.w.x == 0.0);
    CHECK(spinning.w.y == 0.0);
    CHECK(spinning.w.z > 0.0);

    const BallState potted = make_ball({1.0, 0.0, 0.0}, {0.0, 0.0, 3.0}, Motion::pocketed);
    const BallState still_potted = complete_transition(potted, p);
    CHECK(still_potted.motion == Motion::pocketed);
    CHECK(still_potted.v == potted.v);
}

TEST_CASE("motion: stationary and pocketed balls never move or change") {
    const PhysicsParams p;
    for (Motion motion : {Motion::stationary, Motion::pocketed}) {
        BallState b = make_ball({}, {}, motion);
        if (motion == Motion::pocketed) b.pocket = 3;
        CHECK(transition_time(b, p, {}) == kNever);
        CHECK(transition_time(b, p, {1e-3, 0.0, 0.0}) == kNever);
        const BallState e = evolve(b, 12.5, p, {1e-3, 0.0, 0.0});
        CHECK(e.r == b.r);
        CHECK(e.v == b.v);
        CHECK(e.w == b.w);
        CHECK(e.motion == motion);
        const Trajectory tr = trajectory(b, p, {});
        CHECK(tr.duration == kNever);
        CHECK(tr.c0 == b.r);
        CHECK(tr.c1 == Vec3{});
        CHECK(tr.c2 == Vec3{});
    }
}

TEST_CASE("motion: zero vectors give no NaN") {
    PhysicsParams p;
    const BallState cases[] = {
        make_ball({}, {}, Motion::stationary),
        make_ball({}, {0.0, 0.0, 5.0}, Motion::spinning),
        make_ball({}, {}, Motion::spinning),
        make_ball({}, {}, Motion::sliding),
        make_ball({}, {0.0, 0.0, -5.0}, Motion::sliding),
        make_ball({}, {}, Motion::rolling),
        make_ball({}, {0.0, 0.0, 5.0}, Motion::rolling),
    };
    for (double spin_friction : {p.spin_friction_per_r, 0.0}) {
        p.spin_friction_per_r = spin_friction;
        for (const BallState& b : cases) {
            CAPTURE(static_cast<int>(b.motion));
            const double t = transition_time(b, p, {});
            CHECK(!std::isnan(t));
            CHECK(t >= 0.0);
            const double dt = std::isfinite(t) ? t : 1.0;
            const BallState e = evolve(b, dt, p, {});
            CHECK(finite(e));
            CHECK(finite(complete_transition(e, p)));
            const Trajectory tr = trajectory(b, p, {});
            CHECK(finite(tr.at(dt)));
            CHECK(std::isfinite(kinetic_energy(b, p)));
            CHECK(finite(nap_acceleration(b, p)));
        }
    }
}
