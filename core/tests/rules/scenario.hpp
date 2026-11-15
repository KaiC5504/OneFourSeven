#pragma once

// Shared by the rules scenario tests (tests/rules) and the builders' unit tests
// (tests/rules_unit): the real table, physics and referee values, and one call that plays a
// stroke through the simulator, the referee's eyes and the rules.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/physics/params.hpp"
#include "ofs/physics/simulator.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/rules/engine.hpp"
#include "ofs/rules/facts.hpp"
#include "ofs/rules/oracle.hpp"
#include "ofs/rules/referee_params.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::rules::scenario {

struct Rig {
    phys::PhysicsParams params;
    phys::TableShape table;
    RefParams ref;
    std::unique_ptr<TableOracle> oracle;
};

inline nlohmann::json data_file(const char* name) {
    const std::string path = std::string(OFS_CORE_DATA_DIR) + "/" + name;
    std::ifstream in(path, std::ios::binary);
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (!in || j.is_discarded()) {
        std::fprintf(stderr, "scenario: cannot read %s\n", path.c_str());
        std::abort();
    }
    return j;
}

// core/data's table_params.json, table_shape.json and referee_params.json, loaded once. A file
// that is refused aborts the test run with its problems.
inline const Rig& rig() {
    static const Rig instance = [] {
        Rig r;
        std::vector<std::string> problems;
        load_physics_params(data_file("table_params.json"), r.params, problems);
        phys::load_table_shape(data_file("table_shape.json"), r.table, problems);
        load_ref_params(data_file("referee_params.json"), r.ref, problems);
        if (!problems.empty()) {
            for (const std::string& p : problems) std::fprintf(stderr, "scenario: %s\n", p.c_str());
            std::abort();
        }
        r.oracle = make_geometry_oracle(r.table, r.ref);
        return r;
    }();
    return instance;
}

inline BallOnTable ball(Ball b, double x, double y, int index = 0) { return {b, index, {x, y}}; }

// A colour on its own spot.
inline BallOnTable spotted(Ball colour) { return {colour, 0, table::spot(colour)}; }

// A frame in play with nothing pending and nobody in hand: `balls` with the cue ball at place
// `cue_ball`, player 0 to play, scores level. Red is on when a red is listed, else the
// lowest-value colour listed.
inline FrameState frame(std::vector<BallOnTable> balls, int cue_ball = 0) {
    FrameState s;
    s.balls = std::move(balls);
    s.cue_ball = cue_ball;
    bool reds = false;
    Ball lowest = Ball::black;
    for (const BallOnTable& b : s.balls) {
        if (b.ball == Ball::red) reds = true;
        if (b.ball != Ball::cue && b.ball != Ball::red && table::ball_value(b.ball) < table::ball_value(lowest)) {
            lowest = b.ball;
        }
    }
    s.on = reds ? BallOn{OnKind::red, Ball::yellow} : BallOn{OnKind::colour, lowest};
    return s;
}

// The place of the first ball of that kind (and index, for a red); -1 when it isn't listed.
inline int place_of(const Position& p, Ball b, int index = 0) {
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        if (p.balls[i].ball == b && p.balls[i].index == index) return int(i);
    }
    return -1;
}

inline double aim_at(const Position& p, Vec2 target) {
    const Vec2 cue = p.balls[std::size_t(p.cue_ball)].pos;
    return std::atan2(target.y - cue.y, target.x - cue.x);
}

inline double aim_at_ball(const Position& p, int place) { return aim_at(p, p.balls[std::size_t(place)].pos); }

// A centre-ball strike with a level cue.
inline phys::CueStrike strike(double aim_rad, double speed_m_s) {
    phys::CueStrike c;
    c.aim_rad = aim_rad;
    c.speed_m_s = speed_m_s;
    return c;
}

inline ShotInput nominate(Ball colour) { return {true, colour}; }

inline phys::TableState table_state(const Position& p) {
    phys::TableState t;
    for (const BallOnTable& b : p.balls) {
        phys::BallState s;
        s.ball = b.ball;
        s.index = b.index;
        s.r = {b.pos.x, b.pos.y, 0.0};
        t.balls.push_back(s);
    }
    return t;
}

inline phys::ShotRecord simulate(const Position& p, const phys::CueStrike& c, std::uint64_t seed = 1) {
    phys::SimInput in;
    in.start = table_state(p);
    in.cue_ball = p.cue_ball;
    in.strike = c;
    in.seed = seed;
    return phys::simulate(in, rig().table, rig().params);
}

struct Played {
    phys::ShotRecord record;
    ShotFacts facts;
    Judgement judgement;
};

// The stroke simulated from `s` and judged with the real oracle and referee values. `s` itself is
// not changed: carry on from the result's judgement.post.
inline Played play(const FrameState& s, const phys::CueStrike& c, ShotInput input = {}, std::uint64_t seed = 1) {
    Played out;
    out.record = simulate(s, c, seed);
    out.facts = observe(s, out.record, rig().table, rig().ref);
    out.judgement = judge(s, input, out.facts, *rig().oracle, rig().ref);
    return out;
}

// Facts for a stroke nobody simulated: every ball stays where it is, nothing hit or potted.
inline ShotFacts quiet_facts(const Position& p) {
    ShotFacts f;
    for (const BallOnTable& b : p.balls) {
        f.rest.push_back(b.pos);
        f.closest_gap_m.push_back(1.0);
    }
    f.closest_gap_m[std::size_t(p.cue_ball)] = 0.0;
    return f;
}

inline bool has_call(const std::vector<Call>& calls, CallKind kind) {
    for (const Call& c : calls) {
        if (c.kind == kind) return true;
    }
    return false;
}

inline bool has_foul(const Judgement& j, FoulKind kind) {
    for (const Foul& f : j.fouls) {
        if (f.kind == kind) return true;
    }
    return false;
}

}  // namespace ofs::rules::scenario
