#pragma once

// The stand-in opponent (spec 21, M3): simple and legal, so matches can be played end to end and
// the rules fuzzed with whole frames. M4's opponent replaces it. Deterministic for one Rng state.
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
    bool places_cue = false;  // the cue ball is in hand: place it at cue_pos first
    rules::Vec2 cue_pos;
    rules::ShotInput input;
    phys::CueStrike strike;
    // For opponent.decision: {"kind": "pot" | "safety" | "escape" | "break", "target": "red 3",
    // "pocket": index or -1, "cut_rad", "candidates", "sims", "aim_error_rad", "speed_error"}.
    nlohmann::json why;
};

// The stroke for the striker of `s` (not over, nothing pending). Always a stroke the engine
// accepts: the cue placement is legal, a nomination is given whenever one is open, and the strike
// is within the physics limits (speed in (0, max_cue_speed_m_s], tip offset inside miscue_limit).
StandinShot choose_shot(const rules::FrameState& s, const phys::TableShape& table, const phys::PhysicsParams& params,
                        const rules::TableOracle& oracle, const rules::RefParams& ref, const StandinParams& sp,
                        Rng& rng);

// One of `options` (not empty), which is rules::options(s, player).
rules::DecisionKind choose_decision(const rules::FrameState& s, int player,
                                    const std::vector<rules::DecisionKind>& options, const StandinParams& sp, Rng& rng);

}  // namespace ofs::opponent
