#include "ofs/opponent/error_model.hpp"

#include <algorithm>
#include <cmath>

namespace ofs::opponent {

double aim_sigma(const Level& level, const Pot& pot, const Spec& spec) {
    double sigma = level.aim_sigma_rad * (1.0 + level.aim_sigma_per_cut * pot.cut_rad) *
                   (1.0 + level.aim_sigma_per_m * pot.cue_travel_m);
    // A raised cue over a hampered bridge is still one awkward stroke, not two.
    if (pot.awkward || spec.elevation_rad > kRaised_rad) sigma *= level.awkward_factor;
    return sigma;
}

double pot_chance(const Level& level, const Pot& pot, const Spec& spec, double window_rad) {
    if (!(window_rad > 0.0)) return 0.0;
    const double sigma = aim_sigma(level, pot, spec);
    if (sigma <= 0.0) return 1.0;
    return std::erf(window_rad / (sigma * std::sqrt(2.0)));
}

phys::CueStrike with_error(const phys::CueStrike& meant, const Level& level, const Pot* pot, const Env& env,
                           Rng& rng, Drawn* drawn) {
    const double n_aim = rng.normal();
    const double n_speed = rng.normal();
    const double n_tip_x = rng.normal();
    const double n_tip_y = rng.normal();

    const Spec spec{meant.speed_m_s, meant.tip_x, meant.tip_y, meant.elevation_rad};
    const double sigma = pot != nullptr ? aim_sigma(level, *pot, spec) : level.aim_sigma_rad;
    Drawn added;
    added.aim_rad = n_aim * sigma;
    added.speed = n_speed * level.speed_sigma;
    added.tip_x = n_tip_x * level.tip_sigma;
    added.tip_y = n_tip_y * level.tip_sigma;
    if (drawn != nullptr) *drawn = added;

    phys::CueStrike out = meant;
    out.aim_rad += added.aim_rad;
    out.speed_m_s = std::clamp(meant.speed_m_s * (1.0 + added.speed), kLeastSpeed_m_s, env.params.max_cue_speed_m_s);
    out.tip_x += added.tip_x;
    out.tip_y += added.tip_y;
    const double tip_limit = kTipLimit * env.params.miscue_limit;
    const double tip = std::hypot(out.tip_x, out.tip_y);
    if (tip > tip_limit) {
        out.tip_x *= tip_limit / tip;
        out.tip_y *= tip_limit / tip;
    }
    return out;
}

}  // namespace ofs::opponent
