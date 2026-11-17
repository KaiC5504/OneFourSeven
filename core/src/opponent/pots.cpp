#include "ofs/opponent/pots.hpp"

namespace ofs::opponent {

const char* band(const Pot& pot) {
    const double length = pot.cue_travel_m + pot.object_travel_m;
    if (length < kMediumFrom_m) return "short";
    return length < kLongFrom_m ? "medium" : "long";
}

std::vector<Pot> pots_from(const rules::Position&, rules::Vec2, const Env&, double) { return {}; }

std::vector<Pot> pots(const rules::FrameState&, const Env&, double) { return {}; }

std::vector<rules::Vec2> cue_placements(const rules::Position& p, const Env&, int) {
    return {p.balls.at(std::size_t(p.cue_ball)).pos};
}

std::vector<int> pockets_open_to(const rules::Position&, int, const Env&) { return {}; }

int open_reds(const rules::Position&, const Env&) { return 0; }

}  // namespace ofs::opponent
