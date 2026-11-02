#pragma once

// The event loop (spec 7.1, 7.7): find the earliest event over every ball, move every ball to it,
// resolve it, repeat until all balls are at rest or potted. Events from balls whose segment didn't
// change are cached, so each event re-solves only what it touched (spec 7.13: a break-off in
// <= 2 ms on one desktop core).
//
// Simultaneous events (within zero_time_s) resolve one at a time in a fixed order: earliest time,
// then kind, then ball indices, then body. That order, the seeded rack gaps and Rng make a shot
// bit-identical for the same input on one platform (C13).
//
// Guards (each logged as physics.anomaly with the shot's repro bundle, and kept in
// ShotRecord::anomalies): max_events, max_zero_time_chain in a row and time_cap_s end the shot
// with every ball brought to rest where it is; a non-finite value reverts that ball to its last
// finite state at rest; two balls found overlapping are pushed apart along their centre line to
// touching; a rise in total kinetic energy across an event is logged. The shot always ends.

#include <cstdint>
#include <string>

#include "ofs/physics/params.hpp"
#include "ofs/physics/shot_record.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/physics/types.hpp"

namespace ofs::phys {

// Two balls closer than 2R by more than this overlap. The simulator logs an overlap anomaly past it
// and the fuzz fails a shot on it, so both use this one value.
inline constexpr double kOverlapTolerance_m = 1e-9;

struct SimInput {
    TableState start;  // Motion is re-derived with classify(), except for pocketed balls
    int cue_ball = 0;
    bool has_strike = true;  // false: simulate `start` as it moves (tests, mid-shot states)
    CueStrike strike;
    std::uint64_t seed = 0;
    std::string params_hash;  // logged with anomalies; the hash of the parameter file in use
    std::string table_hash;   // the same for the table shape file; empty when unknown
};

ShotRecord simulate(const SimInput& in, const TableShape& table, const PhysicsParams& p);

}  // namespace ofs::phys
