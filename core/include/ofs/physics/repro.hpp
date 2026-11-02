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

// Version 2 added table_hash, params_values_hash and the "nan"/"inf"/"-inf" strings for non-finite
// doubles (serialize.hpp). A version-1 reader would skip both checks without a word and refuse
// those strings as malformed, so the number moved. Version 1 is still read: it has neither field,
// so it replays with no table identity and no values check.
inline constexpr int kBundleVersion = 2;
// A result lists at most this many events (plus a digest of all of them), so a break-off's log
// line stays a few kilobytes.
inline constexpr int kResultEventCap = 400;

// {"v", "seed", "params_hash", "params": {name: value}, "params_values_hash", "table_hash",
//  "cue_ball", "has_strike", "strike", "start"}. The seed is a decimal string: JSON numbers can't
// carry every uint64. params_hash is the parameter file's hash, which the values alone can't
// reproduce; params_values_hash is ofs::params::content_hash of "params" as physics_params_values
// writes it (integer fields as integers, the rest as doubles; FNV-1a 64 over the canonical text of
// docs/devlink-protocol.md). table_hash is SimInput::table_hash, empty when the caller didn't know it.
nlohmann::json make_bundle(const SimInput& in, const PhysicsParams& p);

// {"events", "t_end", "potted": [{"ball", "pocket", "t"}], "final", "miscue",
//  "elevation_used_rad", "elevation_raised", "anomalies", "sim_us", "event_list": [[t, kind, a,
//  b, body], ...] (first kResultEventCap), "event_digest"}. event_digest is ofs::params::content_hash
// of [[kind, a, b, body], ...] over every event in order, without the times, which differ across
// platforms by libm rounding.
nlohmann::json make_result(const ShotRecord& r);

// False with a reason when the bundle is malformed, from a newer version, its params invalid, or
// its params not the values params_values_hash was taken over. The hash is recomputed from the
// parsed values, so key order, whitespace or 120 for 120.0 don't matter and a changed value does.
// Both hashes are optional in either version: without table_hash, in.table_hash is left empty;
// without params_values_hash, the values aren't checked.
bool parse_bundle(const nlohmann::json& bundle, SimInput& in, PhysicsParams& p, std::string& error);

struct Divergence {
    bool diverged = false;
    // Index of the first listed event that differs; -1 if the listed events agree, including when
    // only event_digest shows a difference past kResultEventCap.
    int first_event = -1;
    double max_position_error_m = 0.0;  // over the final positions
    std::string summary;    // one line a person can read
};

// Logged result against a replayed one: event list (kinds and bodies exactly, times within
// 1e-9 s), potted balls exactly, final positions within tolerance_m. Events past kResultEventCap
// are checked only by event_digest, so there a different sequence of events diverges and a
// different time doesn't.
Divergence compare_results(const nlohmann::json& logged, const nlohmann::json& replayed, double tolerance_m);

// Emits shot.simulated (info) with {bundle, result}.
void log_shot(const SimInput& in, const PhysicsParams& p, const ShotRecord& r);

}  // namespace ofs::phys
