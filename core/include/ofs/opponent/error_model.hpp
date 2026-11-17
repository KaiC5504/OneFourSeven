#pragma once

// A level's error (spec 10.3): how far its stroke lands from the one it meant, and so the chance
// it gives a pot. Aim error grows with the cut, the length of the shot and awkward cueing.

#include "ofs/opponent/env.hpp"
#include "ofs/opponent/level.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/physics/types.hpp"
#include "ofs/rng.hpp"

namespace ofs::opponent {

// The standard deviation of the aim for this pot:
//   level.aim_sigma_rad * (1 + aim_sigma_per_cut * cut_rad) * (1 + aim_sigma_per_m * cue_travel_m)
//   * (awkward_factor when pot.awkward, or when the cue is raised past kRaised_rad)
inline constexpr double kRaised_rad = 0.17;
double aim_sigma(const Level& level, const Pot& pot, const Spec& spec);

// The chance the pot goes in: the aim error, normal with aim_sigma, landing within `window_rad`
// either way: erf(window_rad / (sigma * sqrt(2))). 1 when sigma is 0 and the window is open, 0
// when the window is not. `window_rad` is solve_pot's measured window, or pot.margin_rad before
// anything is simulated.
double pot_chance(const Level& level, const Pot& pot, const Spec& spec, double window_rad);

// What was added to a stroke.
struct Drawn {
    double aim_rad = 0.0;
    double speed = 0.0;  // as a fraction of the speed meant
    double tip_x = 0.0;
    double tip_y = 0.0;
};

// `meant` with the level's error added. Always draws four normals from `rng`, in the order aim,
// speed, tip_x, tip_y, so a decision replays from its seed. `pot` is the pot being played, or
// null for any other stroke (the aim's sigma is then level.aim_sigma_rad alone). The result is a
// stroke the game accepts: speed in [kLeastSpeed_m_s, max_cue_speed_m_s], the tip offset within
// kTipLimit of the miscue limit (its direction kept), the elevation as meant.
inline constexpr double kTipLimit = 0.98;
phys::CueStrike with_error(const phys::CueStrike& meant, const Level& level, const Pot* pot, const Env& env,
                           Rng& rng, Drawn* drawn = nullptr);

}  // namespace ofs::opponent
