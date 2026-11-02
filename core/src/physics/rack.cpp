#include "ofs/physics/rack.hpp"

#include <algorithm>
#include <cmath>

#include "ofs/rng.hpp"

namespace ofs::phys {

namespace {

// Its own stream, so a rack and a strike that share a seed don't draw the same numbers.
constexpr std::uint64_t kRackStream = 0x7261636b5f676170ULL;

}  // namespace

// The triangle is laid out with the middle gap, then each red moves by at most a quarter of the
// gap range. Two neighbours' distance changes by at most the sum of their moves (triangle
// inequality), so every neighbouring gap stays inside [min, max] without rejection sampling, and
// the apex's gap to the pink (which doesn't move) stays inside it with room to spare.
// Non-neighbours sit sqrt(3) spacings apart, far from touching.
TableState opening_rack(std::uint64_t seed, const PhysicsParams& p) {
    const double gap_min = std::min(p.rack_gap_min_m, p.rack_gap_max_m);
    const double gap_max = std::max(p.rack_gap_min_m, p.rack_gap_max_m);
    const double base = 0.5 * (gap_min + gap_max);
    // Slightly under a quarter, so rounding in the positions can't push a gap past the bounds.
    const double move = 0.249 * (gap_max - gap_min);
    const double half_side = move / std::sqrt(2.0);

    Rng rng(seed ^ kRackStream);
    TableState s;
    s.balls.reserve(22);
    for (const table::BallPlacement& placement : table::opening_layout(base)) {
        BallState b;
        b.ball = placement.ball;
        b.index = placement.index;
        b.r = {placement.position.x, placement.position.y, 0.0};
        if (b.ball == table::Ball::red) {
            b.r.x += rng.uniform(-half_side, half_side);
            b.r.y += rng.uniform(-half_side, half_side);
        }
        s.balls.push_back(b);
    }
    return s;
}

}  // namespace ofs::phys
