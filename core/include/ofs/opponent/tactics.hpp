#pragma once

// The strokes that are not pots (spec 10.2 step 1): a safety, a snooker, an escape when
// snookered, and the break-off. Found on the simulator as a pot is, and weighed by what they
// leave. docs/opponent/README.md, "Tactics", is the contract.

#include <string>

#include <nlohmann/json.hpp>

#include "ofs/opponent/budget.hpp"
#include "ofs/opponent/env.hpp"
#include "ofs/opponent/level.hpp"
#include "ofs/physics/types.hpp"
#include "ofs/rng.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::opponent {

struct Tactic {
    // False when it worked nothing out (level.tactics is 0, the budget stopped it before its
    // first candidate, no candidate was legal): the caller plays the stand-in's stroke.
    bool found = false;
    std::string kind;  // "safety", "snooker", "escape" or "break"
    bool places_cue = false;
    rules::Vec2 cue_pos;
    rules::ShotInput input;
    phys::CueStrike meant;  // as planned; the caller adds the level's error
    // What the stroke is worth to the striker in points, on the scale a pot is scored on
    // (README, "Worth"): the foul points it expects to be given less what the position left is
    // worth to the other player, as the level reads it. A pot is preferred when its score is higher.
    double worth = 0.0;
    // For opponent.decision: {"kind", "target", "leave": {...}, "others": [...], ...}.
    nlohmann::json why;
    int candidates = 0;  // strokes it weighed
};

// The best stroke of those kinds for the striker of `s` (not over, nothing pending), as far as
// level.tactics, level.safety_tries and level.escape_cushions reach. One candidate is one step of
// `budget`, which it shares with the search for pots; stopped, it answers with the best so far.
// `judgement` is the stream a level's misreadings are drawn from, never the stroke's error.
// Deterministic for the frame, the level, the stream and the steps taken. Never throws.
Tactic choose_tactic(const rules::FrameState& s, const Level& level, const Env& env, Budget& budget,
                     Rng& judgement);

}  // namespace ofs::opponent
