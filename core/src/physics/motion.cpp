#include "ofs/physics/motion.hpp"

// Stub from the M1 contracts commit; the motion agent replaces it.

namespace ofs::phys {

Vec3 contact_slip(const BallState&) { return {}; }
Motion classify(const BallState& b, const PhysicsParams&) { return b.motion; }
double transition_time(const BallState&, const PhysicsParams&, Vec3) { return kNever; }
BallState evolve(const BallState& b, double, const PhysicsParams&, Vec3) { return b; }
BallState complete_transition(const BallState& b, const PhysicsParams&) { return b; }
Trajectory trajectory(const BallState& b, const PhysicsParams&, Vec3) { return {b.r, {}, {}, kNever}; }
double kinetic_energy(const BallState&, const PhysicsParams&) { return 0.0; }

}  // namespace ofs::phys
