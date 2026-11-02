#include "ofs/physics/nap.hpp"

#include <algorithm>
#include <cmath>

// The model (spec 7.8 asks for the behaviour; the form is ours and the owner's feel sets the
// magnitudes). A rolling ball at speed s with heading h = v / s, n = z x h (the ball's left) and
// side spin w.z feels
//
//     a = -g f (nap_spin_coeff sigma h.x + nap_lateral_coeff h.y) n
//     f = 2 nap_speed_ref / (s + nap_speed_ref)    1 at the reference speed, growing as it slows
//     sigma = R w.z / (s + R |w.z|)                 side relative to pace, within (-1, 1)
//
// The lateral term, -h.y n, is the part of +x across the heading: nothing for a ball rolling with
// or against the nap, most for one rolling across it, always towards the top. The spin term
// follows h.x: side (right-hand side gives w.z > 0) drifts the ball the way of the side when it
// runs with the nap, the other way against it, and not at all across it. Both are sideways, so the
// nap only bends the path; a sliver of drag along -h (kAlongGuard of the sideways part) keeps the
// along-velocity component negative after rounding too. Held through a segment, though, a strong
// enough push would turn the velocity far enough to raise the speed before the segment ends, so
// its size is capped (max_push).
//
// Re-evaluation thresholds are nap_speed_max * nap_tick_ratio^k down to nap_speed_min. The last
// stretch, below the lowest threshold, rolls out without nap: with a constant sideways push the
// speed would bottom out and rise again before the along-heading velocity reached zero (where
// motion.cpp stops the ball), a gain in energy, and at the default magnitudes the drift it would
// add there is under 0.3 mm.

namespace ofs::phys {

namespace {

constexpr double kR = table::kBallRadius;
constexpr double kAlongGuard = 0x1p-40;
// A segment's sideways velocity leaves the speed a little above the threshold its along-heading
// speed has just reached (about 1e-3 of it at the default magnitudes, most at the lowest one).
// Speeds within this fraction above a threshold count as at it, or the next re-evaluation would
// come a moment later at the same threshold.
constexpr double kThresholdSlack = 0.01;
// Above nap_speed_max there is no nap and so no sideways velocity: only rounding, for which this
// absolute margin (m/s) is plenty.
constexpr double kSpeedRounding = 1e-9;

// The threshold the ball re-evaluates at next, or 0 when none is left at or above nap_speed_min.
double next_threshold(double speed, const PhysicsParams& p) {
    // A ratio outside (0, 1) would never step down; the params loader rejects it anyway.
    if (!(p.nap_tick_ratio > 0.0 && p.nap_tick_ratio < 1.0)) return 0.0;
    double s = p.nap_speed_max_m_s;
    if (speed <= s + kSpeedRounding) {
        const double below = speed / (1.0 + kThresholdSlack);
        while (s >= below && s >= p.nap_speed_min_m_s) s *= p.nap_tick_ratio;
    }
    return s >= p.nap_speed_min_m_s ? s : 0.0;
}

// A sideways push k held while the along-heading speed falls from s to r s at a = roll_friction g
// (the guard only adds to a) gives speed^2 = (s - a t)^2 + k^2 t^2, least at t = a s / (a^2 + k^2).
// That comes before the segment ends, and the speed rises again inside it, when
// (k / a)^2 > r / (1 - r). A ball within kThresholdSlack above a threshold goes on to the next one
// down, so a segment can span r = nap_tick_ratio / (1 + kThresholdSlack). The default magnitudes
// stay under a twentieth of the bound; it binds only for nap coefficients far above them.
double max_push(const PhysicsParams& p) {
    const double r = p.nap_tick_ratio / (1.0 + kThresholdSlack);
    return p.roll_friction * p.gravity_m_s2 * std::sqrt(r / (1.0 - r));
}

}  // namespace

Vec3 nap_acceleration(const BallState& b, const PhysicsParams& p) {
    if (b.motion != Motion::rolling) return {};
    const double speed = norm_xy(b.v);
    if (!(speed >= p.nap_speed_min_m_s && speed <= p.nap_speed_max_m_s + kSpeedRounding)) return {};
    if (next_threshold(speed, p) == 0.0) return {};
    const Vec3 h{b.v.x / speed, b.v.y / speed, 0.0};
    const Vec3 n{-h.y, h.x, 0.0};
    const double f = 2.0 * p.nap_speed_ref_m_s / (speed + p.nap_speed_ref_m_s);
    const double side = kR * b.w.z;
    const double sigma = side / (speed + std::abs(side));
    const double push = -p.gravity_m_s2 * f * (p.nap_spin_coeff * sigma * h.x + p.nap_lateral_coeff * h.y);
    const double k = std::clamp(push, -max_push(p), max_push(p));
    return n * k - h * (std::abs(k) * kAlongGuard);
}

double nap_retick_time(const BallState& b, const PhysicsParams& p, double along_decel) {
    if (b.motion != Motion::rolling || !(along_decel > 0.0)) return kNever;
    const double speed = norm_xy(b.v);
    const double next = next_threshold(speed, p);
    if (!(next > 0.0 && speed > next)) return kNever;
    return (speed - next) / along_decel;
}

}  // namespace ofs::phys
