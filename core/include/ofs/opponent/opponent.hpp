#pragma once

// The computer opponent (spec 10): one stroke or one choice for a level. Deterministic for one
// Rng state and one budget of steps, so any decision logged can be made again (ofs_sim_cli
// opponent). Stage 1 of M4 has two levels, Beginner and the 147; safeties, escapes and break-offs
// are still the stand-in's, with the level's error.

#include <atomic>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/opponent/env.hpp"
#include "ofs/opponent/level.hpp"
#include "ofs/physics/types.hpp"
#include "ofs/rng.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::opponent {

// How long it may think. The search checks its budget between steps (one candidate's evaluation
// is a step), and when stopped answers with the best stroke found so far, so it never runs long
// and never comes back empty (spec 10.6).
struct Think {
    int max_sims = 0;      // simulations; 0 takes level.max_sims
    int max_steps = 0;     // steps; 0 no limit. A logged decision is re-run with the steps it took
    double max_ms = 0.0;   // wall clock; 0 no limit. The safety stop on a slow device
    const std::atomic<bool>* cancel = nullptr;  // set from another thread to stop at the next step
};

struct Stroke {
    // Set in place of a stroke when it would rather claim or concede the frame at its turn: one
    // of rules::options(s, striker), to apply with decide().
    bool decides = false;
    rules::DecisionKind decision = rules::DecisionKind::concede_frame;
    bool places_cue = false;  // the cue ball is in hand: place it at cue_pos first
    rules::Vec2 cue_pos;
    rules::ShotInput input;
    phys::CueStrike strike;  // as it is played, its error included
    phys::CueStrike meant;   // as it was planned
    // For opponent.decision (docs/opponent/README.md has the form in full): {"level", "kind":
    // "pot" | "safety" | "escape" | "break", "target": "red 3", "pocket", "band", "cut_rad",
    // "chance", "worth", "leave": {...}, "plan": [...], "others": [...], "drawn": {...},
    // "maximum": "on" | "off" | "gone", ...}; a decision: {"level", "kind": "claim" | "concede"}.
    nlohmann::json why;

    int candidates = 0;  // strokes it weighed
    int sims = 0;        // simulations it ran
    int steps = 0;       // steps it took
    int depth = 0;       // strokes ahead it planned at its deepest
    std::string stopped = "done";  // "done", "sims", "steps", "time" or "cancelled"
    // It could not work a stroke out and played the stand-in's plainest: why, for
    // opponent.fallback. Empty otherwise.
    std::string fallback;
};

// The stroke for the striker of `s` (not over, nothing pending). Always one the engine accepts:
// the cue placement is legal, a nomination is given whenever one is open, the strike is within
// the physics limits. Never throws.
Stroke choose_stroke(const rules::FrameState& s, const Level& level, const Env& env, Rng& rng,
                     const Think& think = {});

// One of `options` (not empty), which is rules::options(s, player). `why`, when given, gets what
// it weighed, in the stand-in's form (standin.hpp) plus "level".
rules::DecisionKind choose_option(const rules::FrameState& s, int player,
                                  const std::vector<rules::DecisionKind>& options, const Level& level,
                                  const Env& env, Rng& rng, nlohmann::json* why = nullptr);

}  // namespace ofs::opponent
