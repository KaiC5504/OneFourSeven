#pragma once

// The shot record (spec 5): every ball's motion as analytic segments plus the event list. The
// game animates, replays and plays sounds from it; nothing is simulated twice.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/physics/cue.hpp"
#include "ofs/physics/params.hpp"
#include "ofs/physics/types.hpp"

namespace ofs::phys {

enum class EventKind : std::uint8_t {
    strike,      // t = 0, a = the cue ball
    ball_ball,   // a, b
    ball_line,   // a, body = TableShape::lines index
    ball_arc,    // a, body = TableShape::arcs index
    pocket,      // a, body = TableShape::pockets index
    transition,  // a changes Motion by itself: from -> to
    nap,         // a's nap acceleration re-evaluated
    guard,       // the simulator stopped the shot (see the anomaly)
};

std::string_view event_kind_name(EventKind k);

struct ShotEvent {
    double t = 0.0;
    EventKind kind = EventKind::strike;
    int a = -1;
    int b = -1;
    int body = -1;
    Motion from = Motion::stationary;
    Motion to = Motion::stationary;
};

// One ball's motion from t0 until the next segment's t0 (the last lasts for ever): evaluate
// with motion.hpp's evolve(start, t - t0, params, nap).
struct Segment {
    double t0 = 0.0;
    BallState start;
    Vec3 nap;  // the nap acceleration plus, for a rolling ball held against another ball or a
               // cushion, its contact acceleration (simulator.cpp)
};

// kind: "event_guard" (max_events), "zero_time_chain", "time_cap", "solver_fallback",
// "energy_gain", "overlap", "nan" (spec 16.10).
struct Anomaly {
    std::string kind;
    double t = 0.0;
    std::vector<int> balls;
    std::string detail;
};

struct ShotRecord {
    PhysicsParams params;
    bool has_strike = false;
    StrikeOutcome strike;
    std::vector<std::vector<Segment>> segments;  // per ball, by start time; the first at t0 = 0
    std::vector<ShotEvent> events;               // by time
    TableState final_state;
    double t_end = 0.0;  // when the last ball came to rest or was potted
    std::vector<Anomaly> anomalies;
    double sim_us = 0.0;  // wall-clock simulation time; the only field that isn't deterministic

    BallState ball_at(int ball, double t) const;
    TableState state_at(double t) const;
};

// Everything but sim_us, doubles at full precision: C13 compares two of these byte for byte.
nlohmann::json record_json(const ShotRecord& r);

}  // namespace ofs::phys
