#include "ofs/physics/collide.hpp"

#include <algorithm>
#include <cmath>

#include "ofs/physics/motion.hpp"

// One instantaneous impulse with Coulomb friction at the contact: Alciatore TP A.14 (throw),
// extended to both balls' motion and full 3D spin as pooltool's FrictionalInelastic does
// (pooltool/physics/resolve/ball_ball/frictional_inelastic, Apache-2.0). pooltool's
// frictional_mathavan integrates the impact in ~1000 steps, too slow for spec 7.13.
//
// Snooker balls are equal, so every impulse below is per unit mass (j = J / m).

namespace ofs::phys {

void collide_balls(BallState& a, BallState& b, const PhysicsParams& p) {
    constexpr double R = table::kBallRadius;

    const double dx = b.r.x - a.r.x;
    const double dy = b.r.y - a.r.y;
    const double dist = std::sqrt(dx * dx + dy * dy);
    // Also false for NaN: a degenerate pair is left as it was rather than poisoned.
    if (!(dist > 0.0)) return;
    const Vec3 n{dx / dist, dy / dist, 0.0};

    const double approach = (a.v.x - b.v.x) * n.x + (a.v.y - b.v.y) * n.y;
    if (!(approach > 0.0)) return;
    const double jn = 0.5 * (1.0 + p.ball_restitution) * approach;

    // a's surface at the contact (R n from its centre) moves at v_a + w_a x R n, b's (at -R n) at
    // v_b - w_b x R n, so a slides over b at v_a - v_b + R (w_a + w_b) x n less its n part. That
    // leaves two components. Along t = z x n: the sideways relative velocity plus
    // R (w_a.z + w_b.z), which is cut- and spin-induced throw. Along z: R ((w_a + w_b) x n).z, the
    // top or bottom spin of either ball rubbing vertically, which is how follow and draw transfer
    // (as gears do) and why they reduce throw (TP A.14 eq. 3-4).
    const Vec3 vc = a.v - b.v + cross(a.w + b.w, n) * R;
    const Vec3 slip = vc - n * dot(vc, n);
    const double slide = norm(slip);

    // Coulomb friction against the slip, capped where it would stop it. An impulse j in the contact
    // plane changes a's contact-point velocity by j + (R n x j / I') x R n = 7/2 j, with I' = 2/5 R^2
    // and (n x j) x n = j, and b's by the opposite, so the slip changes by 7 j in any direction of
    // the plane and stops at j = slip / 7 (TP A.14 eq. 14). j's vertical part is then dropped from v:
    // the balls stay on the cloth (spec 7.9), whose reaction goes through the centre, adding no
    // torque and doing no work. A vertical slip that would have stopped therefore falls by only
    // 5/7, but no slip component ever reverses and the energy can only fall further. Along t this
    // is exactly TP A.14 eq. 15.
    Vec3 jt;
    if (slide > 0.0) {
        const double mu = p.ball_friction_a + p.ball_friction_b * std::exp(-p.ball_friction_c * slide);
        jt = slip * (-std::min(mu * jn, slide / 7.0) / slide);
    }

    const double dvx = jt.x - jn * n.x;
    const double dvy = jt.y - jn * n.y;
    a.v.x += dvx;
    a.v.y += dvy;
    b.v.x -= dvx;
    b.v.y -= dvy;

    // w += (r x J) / I with I = 2/5 m R^2; b's lever arm and impulse are both negated, so it
    // gets the same change.
    const Vec3 dw = cross(n, jt) * (2.5 / R);
    a.w += dw;
    b.w += dw;

    a.motion = classify(a, p);
    b.motion = classify(b, p);
}

}  // namespace ofs::phys
