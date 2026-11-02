#include "ofs/physics/simulator.hpp"

// Stub from the M1 contracts commit; the simulator agent replaces it.

namespace ofs::phys {

ShotRecord simulate(const SimInput& in, const TableShape&, const PhysicsParams& p) {
    ShotRecord r;
    r.params = p;
    r.has_strike = in.has_strike;
    r.final_state = in.start;
    for (const BallState& b : in.start.balls) r.segments.push_back({Segment{0.0, b, {}}});
    return r;
}

}  // namespace ofs::phys
