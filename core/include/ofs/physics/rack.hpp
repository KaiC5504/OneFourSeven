#pragma once

#include <cstdint>

#include "ofs/physics/params.hpp"
#include "ofs/physics/types.hpp"

namespace ofs::phys {

// The opening layout (table::opening_layout, cue ball first, reds 0-14, then the colours) with
// seeded microscopic gaps of [rack_gap_min_m, rack_gap_max_m] between neighbouring reds, so
// collisions in the pack arrive in a defined order (spec 7.7). No two balls touch, and the apex
// red stays clear of the pink (rules 3.2(a)(i), 2.18). The same seed gives the same rack.
TableState opening_rack(std::uint64_t seed, const PhysicsParams& p);

}  // namespace ofs::phys
