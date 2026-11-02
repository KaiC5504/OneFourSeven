#pragma once

// Closed-form ball motion between events (spec 7.1, 7.2): Leckie & Greenspan (2005), the
// equations pooltool implements (pooltool/physics/evolve), Alciatore TP 4.1 for slide to roll.
//
// A segment is the stretch of one ball's motion in one Motion state with one nap acceleration.
// Within it every acceleration is constant, so position is quadratic and v, w are linear in time
// (side spin w.z decays linearly and stops at zero without ending the segment).

#include "ofs/physics/params.hpp"
#include "ofs/physics/types.hpp"

namespace ofs::phys {

// u = v + w x (0, 0, -R), the cloth contact point's velocity (z = 0).
Vec3 contact_slip(const BallState& b);

// The state v and w put the ball in, for a ball on the cloth: sliding when |u| exceeds a small
// tolerance, otherwise rolling when |v| does, otherwise spinning when |w.z| does, otherwise
// stationary. Pocketed stays pocketed. Used after strikes and collisions.
Motion classify(const BallState& b, const PhysicsParams& p);

// Seconds until the ball's state changes by itself: sliding to rolling at 2|u0| / (7 mu_s g),
// rolling to spinning or stationary when v reaches zero, spinning to stationary when w.z does.
// kNever for stationary and pocketed. `nap` is the constant nap acceleration of this segment
// (nap.hpp); a rolling ball stops when its velocity along its heading at the segment start
// reaches zero, since the nap's sideways part never brings the speed exactly to zero.
double transition_time(const BallState& b, const PhysicsParams& p, Vec3 nap);

// The ball dt seconds into its current segment, 0 <= dt <= transition_time(b, p, nap). The
// state's Motion is unchanged; positions, velocities and spins follow the closed form exactly.
BallState evolve(const BallState& b, double dt, const PhysicsParams& p, Vec3 nap);

// The ball at the end of its segment with its new Motion: residual slip, speed or spin that
// rounding left is snapped to zero so the next segment starts clean (rolling has u = 0 exactly,
// stationary has v = w = 0 exactly).
BallState complete_transition(const BallState& b_at_end, const PhysicsParams& p);

// The segment's centre path, for event detection: duration = transition_time.
Trajectory trajectory(const BallState& b, const PhysicsParams& p, Vec3 nap);

// Kinetic energy, translational plus rotational (solid sphere, I = 2/5 m R^2); C12 checks it.
double kinetic_energy(const BallState& b, const PhysicsParams& p);

}  // namespace ofs::phys
