#pragma once

// The stand-in opponent (spec 21): simple and legal, so matches can be played end to end and the
// rules fuzzed with whole frames. Deterministic for one Rng state.
//
// core/data/standin_params.json holds its values the way table_params.json does.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/physics/params.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/physics/types.hpp"
#include "ofs/rng.hpp"
#include "ofs/rules/engine.hpp"
#include "ofs/rules/oracle.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::opponent {

struct StandinParams {
    double aim_error_rad = 0.004;     // standard deviation added to the chosen aim
    double speed_error = 0.06;        // standard deviation of the cue speed, as a fraction of it
    double max_cut_rad = 1.05;        // pots with a wider cut than this are not tried
    double pot_speed_m_s = 2.2;       // cue speed for a pot at one metre; scaled with distance
    double safety_speed_m_s = 1.6;
    int max_sims = 8;                 // simulations it may run to check candidates for one stroke
    // It concedes a frame when it needs at least this many penalty points and no red is left.
    int concede_required_points = 20;
};

const std::vector<std::string>& standin_param_names();
// As load_ref_params: problems are logged as params.invalid and appended; true when none.
bool load_standin_params(const nlohmann::json& file, StandinParams& out, std::vector<std::string>& problems);

struct StandinShot {
    // Set in place of a stroke when it would rather claim or concede the frame at its turn: one
    // of rules::options(s, striker), to apply with decide(). The other fields are then unused.
    bool decides = false;
    rules::DecisionKind decision = rules::DecisionKind::concede_frame;
    bool places_cue = false;  // the cue ball is in hand: place it at cue_pos first
    rules::Vec2 cue_pos;
    rules::ShotInput input;
    phys::CueStrike strike;
    // For opponent.decision. A stroke: {"kind": "pot" | "safety" | "escape" | "break", "target":
    // "red 3", "pocket": index or -1, "cut_rad", "candidates", "sims", "penalty" (what the stroke
    // chosen cost when simulated as aimed, -1 when nothing was), "aim_error_rad", "speed_error"},
    // and "misses" (FrameState::miss_streak) when the stroke follows a miss put back.
    // A decision: {"kind": "claim"} or {"kind": "concede", "required": penalty points it needs}.
    // When it could not work a stroke out: {"kind": "safety", "error"} and the plainest stroke.
    nlohmann::json why;
};

// The reds are still the pack: all fifteen on the table, each near their middle. With the cue ball
// in hand and red on, only then is the stroke a break-off; once the pack is split it is a stroke
// like any other, pots included.
bool pack_intact(const rules::Position& p);

// The stroke for the striker of `s` (not over, nothing pending). Always a stroke the engine
// accepts: the cue placement is legal, a nomination is given whenever one is open, and the strike
// is within the physics limits (speed in (0, max_cue_speed_m_s], tip offset inside miscue_limit).
StandinShot choose_shot(const rules::FrameState& s, const phys::TableShape& table, const phys::PhysicsParams& params,
                        const rules::TableOracle& oracle, const rules::RefParams& ref, const StandinParams& sp,
                        Rng& rng);

// One of `options` (not empty), which is rules::options(s, player). `why`, when given, gets what it
// weighed, for opponent.decision: {"rule": "replace" | "play_again" | "play" | "accept_rerack" |
// "refuse_rerack" | "play_first" | "first_option", "options", "scores", "points_remaining"}, and
// what the rule looked at: "clear_path_before" (whether the position before a miss had a straight
// stroke at a ball on), "pot" and "clear_path" (the position left), "score_gap" and
// "rerack_within"; "error" when it threw and took the first option.
rules::DecisionKind choose_decision(const rules::FrameState& s, int player,
                                    const std::vector<rules::DecisionKind>& options, const StandinParams& sp, Rng& rng,
                                    nlohmann::json* why = nullptr);

}  // namespace ofs::opponent
