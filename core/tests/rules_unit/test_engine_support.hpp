#pragma once

// For the engine's own tests: facts made by hand and an oracle that answers what the test tells
// it, so each rule is judged apart from the simulator and the geometry.

#include <array>
#include <cmath>
#include <initializer_list>
#include <utility>
#include <vector>

#include "ofs/rules/engine.hpp"
#include "scenario.hpp"

namespace ofs::rules::engine_test {

using scenario::has_call;
using scenario::has_foul;

struct FakeOracle : TableOracle {
    bool is_snookered = false;
    std::vector<int> snookerers;
    bool has_clear_path = true;
    bool has_full_ball = true;
    bool reachable = true;

    // What the engine asked.
    mutable std::vector<std::vector<int>> snookered_on;
    mutable std::vector<bool> snookered_in_hand;
    mutable std::vector<std::vector<int>> path_on;
    mutable std::vector<std::vector<BallOnTable>> spot_tables;

    bool snookered(const std::vector<BallOnTable>&, int, const std::vector<int>& on, bool in_hand) const override {
        snookered_on.push_back(on);
        snookered_in_hand.push_back(in_hand);
        return is_snookered && !on.empty();
    }
    std::vector<int> snookering_balls(const std::vector<BallOnTable>&, int, const std::vector<int>&) const override {
        return snookerers;
    }
    bool clear_path(const std::vector<BallOnTable>&, int, const std::vector<int>& on) const override {
        path_on.push_back(on);
        return has_clear_path;
    }
    bool full_ball(const std::vector<BallOnTable>&, int, const std::vector<int>&) const override {
        return has_full_ball;
    }
    bool can_hit(const std::vector<BallOnTable>&, int, const std::vector<int>&) const override { return reachable; }

    // The one piece of real geometry: centres a ball's width apart are touching.
    std::vector<int> touching(const std::vector<BallOnTable>& balls, int cue_ball) const override {
        std::vector<int> out;
        const Vec2 cue = balls[std::size_t(cue_ball)].pos;
        for (std::size_t i = 0; i < balls.size(); ++i) {
            if (int(i) == cue_ball) continue;
            if (std::hypot(balls[i].pos.x - cue.x, balls[i].pos.y - cue.y) <= table::kBallDiameter + 5.0e-5) {
                out.push_back(int(i));
            }
        }
        return out;
    }

    std::vector<BallOnTable> spot(const std::vector<BallOnTable>& on_table,
                                  const std::vector<Ball>& colours) const override {
        spot_tables.push_back(on_table);
        std::vector<BallOnTable> out;
        for (Ball colour : colours) out.push_back({colour, 0, table::spot(colour)});
        return out;
    }

    bool cue_placement_legal(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 pos) const override {
        if (!table::in_d(pos)) return false;
        for (std::size_t i = 0; i < balls.size(); ++i) {
            if (int(i) == cue_ball) continue;
            if (std::hypot(balls[i].pos.x - pos.x, balls[i].pos.y - pos.y) <= table::kBallDiameter + 5.0e-5) {
                return false;
            }
        }
        return true;
    }
    Vec2 nearest_cue_placement(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 wanted) const override {
        for (int step = 0; step < 8; ++step) {
            const Vec2 pos{wanted.x - 0.03 * step, wanted.y};
            if (cue_placement_legal(balls, cue_ball, pos)) return pos;
        }
        return wanted;
    }
};

inline constexpr std::array<Ball, 6> kColours = {Ball::yellow, Ball::green, Ball::brown,
                                                 Ball::blue,   Ball::pink,  Ball::black};

// Player 0 to play with red on: the cue ball in open play, `reds` reds in a line clear of every
// spot, and the six colours on theirs. No two balls touch.
inline FrameState open_table(int reds = 15) {
    std::vector<BallOnTable> balls{scenario::ball(Ball::cue, -0.6, 0.35)};
    for (int i = 0; i < reds; ++i) balls.push_back(scenario::ball(Ball::red, 0.3 + 0.08 * i, 0.55, i));
    for (Ball colour : kColours) balls.push_back(scenario::spotted(colour));
    return scenario::frame(std::move(balls));
}

// The cue ball and these colours on their spots, the lowest of them on.
inline FrameState colours_left(std::initializer_list<Ball> colours) {
    std::vector<BallOnTable> balls{scenario::ball(Ball::cue, -0.6, 0.35)};
    for (Ball colour : colours) balls.push_back(scenario::spotted(colour));
    return scenario::frame(std::move(balls));
}

inline int at(const Position& p, Ball b, int index = 0) { return scenario::place_of(p, b, index); }

inline int balls_count(const Position& p, Ball b) {
    int count = 0;
    for (const BallOnTable& ball : p.balls) count += ball.ball == b ? 1 : 0;
    return count;
}

// The cue ball hit that ball first and nothing else happened.
inline ShotFacts hit(const Position& p, int place) {
    ShotFacts f = scenario::quiet_facts(p);
    f.first_hit = {place};
    f.first_hit_t = 0.4;
    f.closest_gap_m[std::size_t(place)] = 0.0;
    return f;
}

inline ShotFacts hit(const Position& p, Ball b, int index = 0) { return hit(p, at(p, b, index)); }

inline void pot(ShotFacts& f, int place) { f.pots.push_back({place, 0, 1.0 + 0.1 * double(f.pots.size())}); }

inline void in_off(ShotFacts& f) {
    f.cue_potted = true;
    f.cue_pocket = 2;
}

inline bool same(const BallOn& a, const BallOn& b) {
    return a.kind == b.kind && (a.kind != OnKind::colour || a.colour == b.colour);
}

inline bool same(const Position& a, const Position& b) {
    if (a.balls.size() != b.balls.size()) return false;
    for (std::size_t i = 0; i < a.balls.size(); ++i) {
        const BallOnTable& x = a.balls[i];
        const BallOnTable& y = b.balls[i];
        if (x.ball != y.ball || x.index != y.index || x.pos.x != y.pos.x || x.pos.y != y.pos.y) return false;
    }
    return a.cue_ball == b.cue_ball && a.cue_in_hand == b.cue_in_hand && same(a.on, b.on) &&
           a.free_ball == b.free_ball && a.touching == b.touching;
}

inline bool same(const PlayerStats& a, const PlayerStats& b) {
    return a.shots == b.shots && a.pots == b.pots && a.pot_attempts == b.pot_attempts && a.fouls == b.fouls &&
           a.misses == b.misses && a.points_from_fouls == b.points_from_fouls && a.highest_break == b.highest_break;
}

inline bool same(const FrameState& a, const FrameState& b) {
    return same(static_cast<const Position&>(a), static_cast<const Position&>(b)) && a.seed == b.seed &&
           a.striker == b.striker && a.scores == b.scores && a.break_points == b.break_points &&
           same(a.stats[0], b.stats[0]) && same(a.stats[1], b.stats[1]) && a.strokes == b.strokes &&
           a.respotted_black == b.respotted_black && a.concede_any_time == b.concede_any_time &&
           a.pending == b.pending && a.decider == b.decider && a.offender == b.offender &&
           a.last_penalty == b.last_penalty && a.last_miss == b.last_miss && same(a.original, b.original) &&
           a.miss_run == b.miss_run && a.miss_warned == b.miss_warned && a.quiet_strokes == b.quiet_strokes &&
           a.rerack_accepted == b.rerack_accepted && a.rerack_deadline == b.rerack_deadline && a.over == b.over &&
           a.winner == b.winner && a.end_reason == b.end_reason && a.restart == b.restart;
}

inline const Call* find_call(const std::vector<Call>& calls, CallKind kind) {
    for (const Call& c : calls) {
        if (c.kind == kind) return &c;
    }
    return nullptr;
}

inline std::vector<CallKind> kinds(const std::vector<Call>& calls) {
    std::vector<CallKind> out;
    for (const Call& c : calls) out.push_back(c.kind);
    return out;
}

inline bool listed(const std::vector<DecisionKind>& options, DecisionKind kind) {
    for (DecisionKind k : options) {
        if (k == kind) return true;
    }
    return false;
}

}  // namespace ofs::rules::engine_test
