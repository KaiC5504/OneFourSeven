#include "ofs/opponent/solve.hpp"

namespace ofs::opponent {

Trial try_strike(const rules::Position&, rules::Vec2, const phys::CueStrike& strike, const Env&, phys::ShotRecord*) {
    Trial out;
    out.strike = strike;
    return out;
}

SolvedPot solve_pot(const rules::Position&, const Pot&, const Spec&, const Env&, bool, int) { return {}; }

double least_pot_speed(const Pot&, const Env&) { return kLeastSpeed_m_s; }

}  // namespace ofs::opponent
