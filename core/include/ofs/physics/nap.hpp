#pragma once

// Nap (spec 7.8). The cloth's nap runs from baulk to the top (+x). A moving ball feels a small
// acceleration that depends on its heading relative to +x and grows as it slows; with side spin a
// further term makes side "hold" with the nap and act differently against it. The magnitudes are
// tunable (params nap_*), the owner's feel decides them.
//
// Only rolling balls feel it (sliding phases are short and end in a re-evaluation anyway). The
// acceleration is held constant until the ball's speed falls to the next threshold of the series
// nap_speed_max_m_s * nap_tick_ratio^k, down to nap_speed_min_m_s: a handful of re-evaluations per
// ball rather than a fixed tick, which keeps every segment quadratic and the event count low.
// It never adds energy: its component along the velocity is never positive, and its size is
// capped so that, held through a segment, it can't turn the velocity enough to raise the speed.

#include "ofs/physics/params.hpp"
#include "ofs/physics/types.hpp"

namespace ofs::phys {

// The nap acceleration until the next re-evaluation (z = 0). Zero unless the ball is rolling
// at a speed in [nap_speed_min_m_s, nap_speed_max_m_s], and zero below the lowest threshold of
// the series too: that last stretch rolls out without nap.
Vec3 nap_acceleration(const BallState& b, const PhysicsParams& p);

// Seconds until the rolling ball, decelerating at `along_decel` m/s^2 along its velocity
// (rolling resistance plus the nap's own along-velocity part, positive), reaches the next speed
// threshold below its current speed. kNever when it isn't rolling, when no threshold is left
// above nap_speed_min_m_s, or when along_decel <= 0.
double nap_retick_time(const BallState& b, const PhysicsParams& p, double along_decel);

}  // namespace ofs::phys
