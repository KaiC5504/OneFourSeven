#include "ofs/physics/cue.hpp"

// Stub from the M1 contracts commit; the cue agent replaces it.

namespace ofs::phys {

double min_cue_elevation(const TableState&, int, double, const TableShape&, const PhysicsParams&) { return 0.0; }

StrikeOutcome strike(const TableState&, int, const CueStrike& c, const TableShape&, const PhysicsParams&, Rng&) {
    StrikeOutcome out;
    out.elevation_used_rad = c.elevation_rad;
    return out;
}

}  // namespace ofs::phys
