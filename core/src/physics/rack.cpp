#include "ofs/physics/rack.hpp"

// Stub from the M1 contracts commit; the simulator agent replaces it.

namespace ofs::phys {

TableState opening_rack(std::uint64_t, const PhysicsParams&) {
    TableState s;
    for (const table::BallPlacement& placement : table::opening_layout(0.0)) {
        BallState b;
        b.ball = placement.ball;
        b.index = placement.index;
        b.r = {placement.position.x, placement.position.y, 0.0};
        s.balls.push_back(b);
    }
    return s;
}

}  // namespace ofs::phys
