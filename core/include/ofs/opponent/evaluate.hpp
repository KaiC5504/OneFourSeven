#pragma once

// What a position is worth to the player about to play it (spec 10.2 step 4), read from its pots
// with nothing simulated, and the facts a maximum break turns on (spec 10.1).

#include <vector>

#include "ofs/opponent/env.hpp"
#include "ofs/opponent/level.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::opponent {

// Points a pot is worth beyond its ball for keeping the break going: the value the game's
// next-ball pick uses (next_target.gd BREAK_POINTS).
inline constexpr double kBreakOn = 16.0;

struct Read {
    // The best of chance * (value + kBreakOn) over the pots: what the player can expect from the
    // stroke. 0 with no pot.
    double worth = 0.0;
    int pots = 0;        // how many pots there are
    int best = -1;       // index into the pots read of the one `worth` is from; -1 none
    double chance = 0.0; // that pot's chance
};

// Reads pots already listed, each with pot_chance(level, pot, a centre-ball stroke with a level
// cue, pot.margin_rad). Of two pots worth the same the earlier listed is the best.
Read read_pots(const std::vector<Pot>& pots, const Level& level, const Env& env);
// pots(s, env, level.max_cut_rad), then read_pots.
Read read_position(const rules::FrameState& s, const Level& level, const Env& env);

// A maximum break (147) is still possible for the striker of `s` in the break it is making or
// about to start: every red is on the table or was potted in this break, each followed by the
// black. From the state alone, with r reds left:
//   red on:              break_points == 8 * (15 - r)
//   a colour of choice:  break_points == 8 * (15 - r) - 7
//   the colours:         r == 0 and break_points == 120 + the values of the colours already gone
// False with a free ball called, a re-spotted black, or a frame that is over or pending.
bool maximum_on(const rules::FrameState& s);

// The ball the maximum needs potted next: a red (Ball::red) while red is on, the black after a
// red, then the colour that is on. Meaningful only while maximum_on(s).
rules::Ball maximum_next(const rules::FrameState& s);

}  // namespace ofs::opponent
