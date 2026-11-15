#pragma once

// What the referee saw (spec 9.2): the physical facts of one stroke, read from its shot record
// with no rule applied. judge() (engine.hpp) turns them into a decision, so tests can hand it
// facts no simulation is needed for.

#include <vector>

#include "ofs/physics/shot_record.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/rules/referee_params.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::rules {

struct Pot {
    int ball = -1;    // place in the pre-shot ball list
    int pocket = -1;  // TableShape::pockets index
    double t = 0.0;
};

struct ShotFacts {
    // Places of the object balls the cue ball's first impact hit: none, one, or several hit within
    // RefParams::simultaneous_hit_s of the first (S3.6), lowest place first.
    std::vector<int> first_hit;
    double first_hit_t = 0.0;  // 0 when no ball was hit
    // Object balls that entered a pocket, by time, then place.
    std::vector<Pot> pots;
    bool cue_potted = false;
    int cue_pocket = -1;
    // S2.19: the first contact came while the tip was still on the cue ball, by RefParams.
    bool push = false;
    // Balls that were within RefParams::touching_gap_m of the cue ball before the stroke and that
    // it set moving as it left (S3.8(b)), lowest place first: the cue ball met them within
    // RefParams::simultaneous_hit_s of the strike or, across a gap that still counts as touching,
    // before it had gone further than a straight line takes it to a ball that near. A touching
    // ball it was played away from, moved later in the stroke by another ball or by the cue ball
    // coming back, is not listed.
    std::vector<int> touching_moved;
    // Per pre-shot place: where the ball came to rest (a potted ball's entry is where it started),
    // and the smallest gap between its surface and the cue ball's during the stroke (0 on contact;
    // the cue ball's own entry is 0).
    std::vector<Vec2> rest;
    std::vector<double> closest_gap_m;
    double cue_travel_m = 0.0;     // the cue ball's path length
    double object_travel_m = 0.0;  // summed over object balls: distance from start to rest (potted balls: to the pocket)
    // The first ball hit was potted, or passed within RefParams::pot_attempt_radius_m of a
    // pocket's drop circle at a moment when it was at least 10 mm nearer that pocket than where it
    // started. A ball that begins near a pocket and is knocked away is not an attempt.
    bool pot_attempt = false;
    // The simulator ended the shot with a guard (ShotRecord::anomalies has an event_guard,
    // zero_time_chain or time_cap). The facts still describe what was simulated.
    bool anomaly = false;
};

// `pre` must be the position the record was simulated from: the same balls in the same order, the
// cue ball at pre.cue_ball. A record that doesn't match (a different ball count, a ball's kind or
// start position differing by more than 1e-9 m) gives facts with anomaly set and logs
// rules.invalid; nothing throws.
ShotFacts observe(const Position& pre, const phys::ShotRecord& record, const phys::TableShape& table,
                  const RefParams& ref);

}  // namespace ofs::rules
