// Contract stub (M3): replaced by the rules engine's builder.

#include "ofs/rules/engine.hpp"

namespace ofs::rules {

FrameState new_frame(int breaker, const phys::TableState& rack, std::uint64_t seed) {
    FrameState s;
    s.seed = seed;
    s.striker = breaker;
    s.cue_in_hand = true;
    for (const phys::BallState& b : rack.balls) {
        s.balls.push_back({b.ball, b.index, {b.r.x, b.r.y}});
    }
    return s;
}

std::vector<int> places_on(const Position&) { return {}; }

std::vector<Ball> nominations(const FrameState&) { return {}; }

int points_remaining(const FrameState&) { return 0; }

int penalty_points_required(const FrameState&, int) { return 0; }

bool place_cue_ball(FrameState&, Vec2, const TableOracle&, std::string& error) {
    error = "not implemented";
    return false;
}

}  // namespace ofs::rules
