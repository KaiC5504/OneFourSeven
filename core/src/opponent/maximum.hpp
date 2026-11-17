#pragma once

// The 147's reading of a table it would be left to play (docs/opponent/README.md "The 147"):
// rules of thumb only, nothing simulated. The search (search.cpp) reads the last position of
// every plan with it.

#include <vector>

#include "ofs/opponent/env.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::opponent {

struct Outlook {
    // In points, on the scale of the points a plan pots: what the break can expect from here.
    // 0 with nothing to pot.
    double worth = 0.0;
    int next = -1;        // place of the ball it would play next; -1 none
    int pocket = -1;
    // How much room that pot leaves to play position, 0 to 1: the chance a steady player would
    // give it. The 147 misses nothing, so this is control of the cue ball, not doubt.
    double chance = 0.0;
};

// `s` is the striker's to play (not over, nothing pending) and `listed` is pots(s, ...). While
// maximum_on(s) only the ball the maximum needs counts; otherwise every pot, by its points.
Outlook read_table(const rules::FrameState& s, const std::vector<Pot>& listed, const Env& env);

}  // namespace ofs::opponent
