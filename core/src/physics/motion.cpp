#include "ofs/physics/motion.hpp"

#include <algorithm>
#include <cmath>

// The equations of each state are those of Kiefl, "pooltool theory" section 3 (ball with arbitrary
// spin), ported from pooltool (Apache-2.0) pooltool/physics/evolve: _evolve_slide_state,
// _evolve_roll_state and _evolve_perpendicular_spin_component, written in the table frame rather
// than pooltool's ball frame. The transition times are pooltool/physics/utils get_slide_time,
// get_roll_time and get_spin_time; the slide time is also Alciatore TP 4.1.

namespace ofs::phys {

namespace {

constexpr double kR = table::kBallRadius;

// Slip, speed and spin (as the equator's surface speed R |w.z|) at or below this count as zero.
// Rounding leaves residues near 1e-16 of a ball's speed, so about 1e-15 m/s at 10 m/s; 1e-9 m/s
// is far above that and far below anything that shows: it moves a ball 0.12 um in a 120 s shot,
// and a slip this small would grip within 2e-9 / (7 mu_s g), about 1e-10 s, under zero_time_s.
constexpr double kSpeedTolerance = 1e-9;

// Side spin's deceleration in rad/s^2: 5 mu_sp g / (2R) with mu_sp = spin_friction_per_r * R.
double spin_decel(const PhysicsParams& p) { return 2.5 * p.spin_friction_per_r * p.gravity_m_s2; }

// w.z moved dt towards zero; it stops there rather than changing sign. The stop is decided by time,
// the way transition_time computes it, so evolving to that time leaves exactly zero.
double decay_spin(double wz, double decel, double dt) {
    if (wz == 0.0 || !(dt < std::abs(wz) / decel)) return 0.0;
    return wz > 0.0 ? std::max(wz - decel * dt, 0.0) : std::min(wz + decel * dt, 0.0);
}

// What stays constant through the ball's current segment.
struct Segment {
    Vec3 a;      // linear acceleration
    Vec3 alpha;  // angular acceleration of w.x and w.y; w.z decays separately
    double duration = kNever;
};

Segment segment(const BallState& b, const PhysicsParams& p, Vec3 nap) {
    Segment s;
    switch (b.motion) {
        case Motion::sliding: {
            // Friction mu_s m g opposes the slip u at the contact. Its torque turns w.x, w.y by
            // (5 mu_s g / 2R) z x u_hat, so u shrinks at 7/2 mu_s g along a fixed u_hat: the
            // acceleration is constant and the path a parabola, which is where swerve comes from.
            const Vec3 u = contact_slip(b);
            const double slip = norm(u);
            const double friction = p.slide_friction * p.gravity_m_s2;
            if (slip > 0.0) {
                const Vec3 u_hat = u / slip;
                s.a = u_hat * -friction;
                s.alpha = Vec3{-u_hat.y, u_hat.x, 0.0} * (2.5 * friction / kR);
                s.duration = 2.0 * slip / (7.0 * friction);
            } else {
                s.duration = 0.0;
            }
            break;
        }
        case Motion::rolling: {
            // Rolling resistance acts against the heading at the segment start; the nap is held
            // constant with it, so the stop comes when the velocity along that heading is zero.
            // Only rolling feels the nap (nap.hpp): the other states ignore the argument.
            const double speed = norm_xy(b.v);
            const double resistance = p.roll_friction * p.gravity_m_s2;
            if (speed > 0.0) {
                const Vec3 heading{b.v.x / speed, b.v.y / speed, 0.0};
                s.a = heading * -resistance + nap;
                const double along_decel = resistance - dot(nap, heading);
                s.duration = along_decel > 0.0 ? speed / along_decel : kNever;
            } else {
                s.a = nap;
                s.duration = 0.0;
            }
            break;
        }
        case Motion::spinning: {
            const double decel = spin_decel(p);
            if (b.w.z == 0.0) {
                s.duration = 0.0;
            } else if (decel > 0.0) {
                s.duration = std::abs(b.w.z) / decel;
            }
            break;
        }
        case Motion::stationary:
        case Motion::pocketed:
            break;
    }
    return s;
}

Trajectory path(const BallState& b, const Segment& s) {
    if (b.motion != Motion::sliding && b.motion != Motion::rolling) return {b.r, {}, {}, s.duration};
    return {b.r, b.v, s.a * 0.5, s.duration};
}

}  // namespace

Vec3 contact_slip(const BallState& b) { return {b.v.x - kR * b.w.y, b.v.y + kR * b.w.x, 0.0}; }

Motion classify(const BallState& b, const PhysicsParams&) {
    if (b.motion == Motion::pocketed) return Motion::pocketed;
    if (norm(contact_slip(b)) > kSpeedTolerance) return Motion::sliding;
    if (norm_xy(b.v) > kSpeedTolerance) return Motion::rolling;
    if (kR * std::abs(b.w.z) > kSpeedTolerance) return Motion::spinning;
    return Motion::stationary;
}

double transition_time(const BallState& b, const PhysicsParams& p, Vec3 nap) {
    return segment(b, p, nap).duration;
}

BallState evolve(const BallState& b, double dt, const PhysicsParams& p, Vec3 nap) {
    BallState out = b;
    if (b.motion == Motion::stationary || b.motion == Motion::pocketed) return out;
    if (b.motion == Motion::spinning) {
        out.w.z = decay_spin(b.w.z, spin_decel(p), dt);
        return out;
    }
    const Segment s = segment(b, p, nap);
    // Held at the segment's end, so a time that rounding put just past it can't run the slip or
    // the speed backwards.
    const double t = std::min(dt, s.duration);
    const Trajectory tr = path(b, s);
    out.r = tr.at(t);
    out.v = tr.velocity(t);
    if (b.motion == Motion::sliding) {
        out.w.x = b.w.x + s.alpha.x * t;
        out.w.y = b.w.y + s.alpha.y * t;
    } else {
        out.w.x = -out.v.y / kR;
        out.w.y = out.v.x / kR;
    }
    out.w.z = decay_spin(b.w.z, spin_decel(p), t);
    return out;
}

BallState complete_transition(const BallState& b_at_end, const PhysicsParams&) {
    BallState b = b_at_end;
    switch (b.motion) {
        case Motion::sliding:
            // w from v, then v back from w, so contact_slip's two products cancel bit for bit.
            b.w.x = -b.v.y / kR;
            b.w.y = b.v.x / kR;
            b.v = {kR * b.w.y, -(kR * b.w.x), 0.0};
            if (norm_xy(b.v) > kSpeedTolerance) {
                b.motion = Motion::rolling;
                return b;
            }
            [[fallthrough]];
        case Motion::rolling:
            b.v = {};
            b.w.x = 0.0;
            b.w.y = 0.0;
            if (kR * std::abs(b.w.z) > kSpeedTolerance) {
                b.motion = Motion::spinning;
                return b;
            }
            [[fallthrough]];
        case Motion::spinning:
            b.v = {};
            b.w = {};
            b.motion = Motion::stationary;
            return b;
        case Motion::stationary:
        case Motion::pocketed:
            break;
    }
    return b;
}

Trajectory trajectory(const BallState& b, const PhysicsParams& p, Vec3 nap) {
    return path(b, segment(b, p, nap));
}

double kinetic_energy(const BallState& b, const PhysicsParams& p) {
    return 0.5 * p.ball_mass_kg * (norm2(b.v) + 0.4 * kR * kR * norm2(b.w));
}

}  // namespace ofs::phys
