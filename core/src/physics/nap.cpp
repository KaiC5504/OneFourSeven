#include "ofs/physics/nap.hpp"

// Stub from the M1 contracts commit; the motion agent replaces it.

namespace ofs::phys {

Vec3 nap_acceleration(const BallState&, const PhysicsParams&) { return {}; }
double nap_retick_time(const BallState&, const PhysicsParams&, double) { return kNever; }

}  // namespace ofs::phys
