#pragma once

// Collision responses. Each takes bodies at the moment of contact (as detect.hpp found it) and
// replaces the balls' v and w with the post-impact values, then sets motion with classify(). None
// may add kinetic energy (C12) or leave a ball moving into the body it just hit.

#include "ofs/physics/params.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/physics/types.hpp"

namespace ofs::phys {

// Ball-ball (spec 7.4): normal impulse with ball_restitution, and contact friction from
// mu = ball_friction_a + ball_friction_b * exp(-ball_friction_c * v_rel) (Alciatore's fit,
// TP A-14), giving cut-induced throw, spin-induced throw and spin transfer: one instantaneous
// impulse (TP A.14, as pooltool's frictional_inelastic), not frictional_mathavan's numerical
// integration, which is too slow for the per-shot budget.
void collide_balls(BallState& a, BallState& b, const PhysicsParams& p);

// Ball-cushion (spec 7.5): Mathavan, Jackson & Parkin (2010), contact at
// cushion_contact_height_m with cushion_restitution, cushion_friction and cushion_table_friction.
// Reference: pooltool/physics/resolve/ball_cushion/mathavan_2010.
void collide_line(BallState& b, const LineCushion& c, const PhysicsParams& p);

// A jaw: the same model, normal along the line from the arc centre to the ball, with
// jaw_restitution and jaw_friction.
void collide_arc(BallState& b, const ArcCushion& c, const PhysicsParams& p);

}  // namespace ofs::phys
