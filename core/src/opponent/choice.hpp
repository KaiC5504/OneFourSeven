#pragma once

// The last of a decision (docs/opponent/README.md, "Worth" and "The score"): the best pot against
// the tactic, and when a level concedes. Apart from search.cpp so its tests can hand it any pot
// and any tactic.

#include <cstdint>

#include "ofs/opponent/level.hpp"
#include "ofs/opponent/tactics.hpp"
#include "ofs/rng.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::opponent {

// The best pot the search found, as the level read it.
struct PotOffer {
    bool found = false;
    double score = 0.0;   // its worth as misread
    double chance = 0.0;  // its chance as misread
};

enum class Pick { pot, tactic, standin };

// The least chance of a pot it plays while a tactic was found: safety_below, raised by the score
// when it is ahead by more than the points left.
double pot_floor(const rules::FrameState& s, const Level& level);

// With a tactic found, the pot when its score is above the tactic's worth and its chance is not
// under pot_floor, else the tactic. Without one, the pot when its chance is not under
// safety_below, else the stand-in's stroke.
Pick pick(const PotOffer& pot, const Tactic& tactic, const rules::FrameState& s, const Level& level);

// The seed of each stream a decision draws from besides its stroke's error, from a copy of its
// Rng so the stroke's draws stay four: `which` 0 is the judgement's (the misreadings, the tactics'
// first and then the pots'), 1 the noisy strokes'.
std::uint64_t stream_seed(const Rng& rng, int which);

// The snookers `player` needs: rules::penalty_points_required over the penalty a foul brings now
// (4, or the ball on's value when that is more), rounded up.
int snookers_needed(const rules::FrameState& s, int player);

// It would concede: level.concede_snookers above 0 and as many snookers needed or more. Whether
// conceding is offered, and the 147's pot, are the caller's to check.
bool wants_to_concede(const rules::FrameState& s, int player, const Level& level);

}  // namespace ofs::opponent
