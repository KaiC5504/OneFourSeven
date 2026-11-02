#pragma once

// Collision responses. Each takes bodies at the moment of contact (as detect.hpp found it) and
// replaces the balls' v and w with the post-impact values, then sets motion with classify(). None
// may add kinetic energy (C12) or leave a ball moving into the body it just hit.

#include "ofs/physics/params.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/physics/types.hpp"

namespace ofs::phys {

// Ball-ball (spec 7.4): normal impulse with ball_restitution, and contact friction from
// mu = ball_friction_a + ball_friction_b * exp(-ball_friction_c * v_rel), Alciatore's TP A.14
// model refitted to published throw (a 0.058, b 0.092, c 2.73; table_params.json), giving
// cut-induced throw, spin-induced throw and spin transfer: one instantaneous impulse, as
// pooltool's frictional_inelastic, not frictional_mathavan's numerical integration, which is too
// slow for the per-shot budget.
void collide_balls(BallState& a, BallState& b, const PhysicsParams& p);

// Ball-cushion (spec 7.5): Mathavan, Jackson & Parkin (2010), contact at
// cushion_contact_height_m with cushion_restitution, cushion_friction and cushion_table_friction.
// Above cushion_rigid_limit_m_s of normal speed the restitution falls by
// cushion_restitution_slope_per_m_s per m/s beyond it, down to cushion_restitution_min.
// Reference: pooltool/physics/resolve/ball_cushion/mathavan_2010.
// Both cushion responses return false when the impact solver gave up and the ball took a plain
// restitution bounce instead; the caller records that as solver_fallback. It gives up when both
// contacts can hold the ball, which wedges it, for a contact at or above 2R / (1 + mu^2) with mu
// the smaller of the cushion (or jaw) friction and cushion_table_friction, and can run out of
// steps a little below that height.
bool collide_line(BallState& b, const LineCushion& c, const PhysicsParams& p);

// A jaw: the same model, normal along the line from the arc centre to the ball, with
// jaw_restitution (falling the same way) and jaw_friction.
bool collide_arc(BallState& b, const ArcCushion& c, const PhysicsParams& p);

}  // namespace ofs::phys
