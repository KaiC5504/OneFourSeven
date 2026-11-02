#pragma once

// Repro bundles (spec 16.10): everything needed to re-run a shot on the PC, and the result to
// compare the re-run against. shot.simulated logs {bundle, result}; physics.anomaly logs the
// bundle. tools/sim_cli replay re-runs a bundle and reports the first divergence.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/physics/params.hpp"
#include "ofs/physics/shot_record.hpp"
#include "ofs/physics/simulator.hpp"

namespace ofs::phys {

inline constexpr int kBundleVersion = 1;
// A result lists at most this many events (plus a digest of all of them), so a break-off's log
// line stays a few kilobytes.
inline constexpr int kResultEventCap = 400;

// {"v", "seed", "params_hash", "params": {name: value}, "cue_ball", "has_strike", "strike",
//  "start"}. The seed is a decimal string: JSON numbers can't carry every uint64.
nlohmann::json make_bundle(const SimInput& in, const PhysicsParams& p);

// {"events", "t_end", "potted": [{"ball", "pocket", "t"}], "final", "miscue",
//  "elevation_used_rad", "elevation_raised", "anomalies", "sim_us", "event_list": [[t, kind, a,
//  b, body], ...] (first kResultEventCap), "event_digest"}.
nlohmann::json make_result(const ShotRecord& r);

// False with a reason when the bundle is malformed, from a newer version, or its params invalid.
bool parse_bundle(const nlohmann::json& bundle, SimInput& in, PhysicsParams& p, std::string& error);

struct Divergence {
    bool diverged = false;
    int first_event = -1;   // index of the first event that differs, -1 if the lists agree
    double max_position_error_m = 0.0;  // over the final positions
    std::string summary;    // one line a person can read
};

// Logged result against a replayed one: event list (kinds and bodies exactly, times within
// 1e-9 s), potted balls exactly, final positions within tolerance_m.
Divergence compare_results(const nlohmann::json& logged, const nlohmann::json& replayed, double tolerance_m);

// Emits shot.simulated (info) with {bundle, result}.
void log_shot(const SimInput& in, const PhysicsParams& p, const ShotRecord& r);

}  // namespace ofs::phys
