#include "ofs/opponent/error_model.hpp"

namespace ofs::opponent {

double aim_sigma(const Level& level, const Pot&, const Spec&) { return level.aim_sigma_rad; }

double pot_chance(const Level&, const Pot&, const Spec&, double) { return 0.0; }

phys::CueStrike with_error(const phys::CueStrike& meant, const Level&, const Pot*, const Env&, Rng& rng,
                           Drawn* drawn) {
    for (int i = 0; i < 4; ++i) rng.normal();
    if (drawn != nullptr) *drawn = {};
    return meant;
}

}  // namespace ofs::opponent
