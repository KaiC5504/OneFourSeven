#pragma once

// Next-event times (spec 7.1): the earliest t in [0, t_max] at which a contact begins, for
// trajectories that share a time origin (Trajectory::shifted rebases one). Each returns kNever
// when there is no such contact. Contacts count only while the bodies approach: two balls
// touching at t = 0 and moving apart, or a ball leaving a cushion, give no event, while touching
// and approaching gives 0. That is what lets frozen clusters and balls resting on a cushion work.

#include "ofs/physics/table_shape.hpp"
#include "ofs/physics/types.hpp"

namespace ofs::phys {

// |ra(t) - rb(t)| = 2R (quartic).
double ball_ball_time(const Trajectory& a, const Trajectory& b, double R, double t_max);

// Distance from the centre to the line = R, contact point within [p1, p2] (quadratic).
double ball_line_time(const Trajectory& a, const LineCushion& c, double R, double t_max);

// |r(t) - centre| = radius + R, direction within the arc's angles (quartic).
double ball_arc_time(const Trajectory& a, const ArcCushion& c, double R, double t_max);

// The centre enters the drop circle (quartic).
double ball_pocket_time(const Trajectory& a, const Pocket& p, double t_max);

}  // namespace ofs::phys
