#pragma once

// For the stand-in's tests: seeded positions of every kind a frame reaches, and the checks a
// stroke must pass for the engine to take it.

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/physics/cue.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/rng.hpp"
#include "scenario.hpp"

namespace ofs::opponent::standin_test {

using rules::Ball;
using rules::BallOnTable;
using rules::FrameState;
using rules::OnKind;
using rules::Vec2;
using rules::scenario::rig;

inline constexpr double kD = table::kBallDiameter;
inline constexpr Ball kColours[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};

// core/data/standin_params.json, loaded once.
inline const StandinParams& standin_params() {
    static const StandinParams instance = [] {
        StandinParams p;
        std::vector<std::string> problems;
        load_standin_params(rules::scenario::data_file("standin_params.json"), p, problems);
        return p;
    }();
    return instance;
}

// The same player with no error: it plays exactly where it aims.
inline StandinParams exact() {
    StandinParams p = standin_params();
    p.aim_error_rad = 0.0;
    p.speed_error = 0.0;
    return p;
}

inline StandinShot choose(const FrameState& s, const StandinParams& sp, Rng& rng) {
    return choose_shot(s, rig().table, rig().params, *rig().oracle, rig().ref, sp, rng);
}

struct Capture : log::Sink {
    std::vector<log::Event> events;
    void write(const log::Event& event, const std::string&) override { events.push_back(event); }
    int count(const std::string& name) const {
        int n = 0;
        for (const log::Event& e : events) n += e.event == name ? 1 : 0;
        return n;
    }
};

inline std::shared_ptr<Capture> capture_log() {
    auto sink = std::make_shared<Capture>();
    log::Logger::instance().clear_sinks();
    log::Logger::instance().add_sink(sink);
    return sink;
}

inline bool room(const std::vector<BallOnTable>& balls, Vec2 p, double gap = 0.002) {
    for (const BallOnTable& b : balls) {
        if (std::hypot(p.x - b.pos.x, p.y - b.pos.y) <= kD + gap) return false;
    }
    return true;
}

// Somewhere on the cloth clear of the cushions, the pockets and every ball already there.
inline Vec2 free_place(const std::vector<BallOnTable>& balls, Rng& rng) {
    for (;;) {
        const Vec2 p{rng.uniform(-1.7, 1.7), rng.uniform(-0.8, 0.8)};
        if (room(balls, p)) return p;
    }
}

inline void put(std::vector<BallOnTable>& balls, Ball b, int index, Rng& rng) {
    const Vec2 p = free_place(balls, rng);
    balls.push_back({b, index, p});
}

// A colour on its spot when there is room, else anywhere.
inline void put_colour(std::vector<BallOnTable>& balls, Ball colour, Rng& rng) {
    const Vec2 spot = table::spot(colour);
    if (room(balls, spot)) {
        balls.push_back({colour, 0, spot});
    } else {
        put(balls, colour, 0, rng);
    }
}

// Mid-frame: the cue ball and `reds` reds anywhere, the six colours on their spots.
inline FrameState scattered(Rng& rng, int reds) {
    std::vector<BallOnTable> balls;
    put(balls, Ball::cue, 0, rng);
    for (int i = 0; i < reds; ++i) put(balls, Ball::red, i, rng);
    for (Ball colour : kColours) put_colour(balls, colour, rng);
    FrameState s = rules::scenario::frame(std::move(balls));
    s.strokes = 20;
    return s;
}

// The colours from `lowest` up, anywhere, with the lowest on.
inline FrameState colours_only(Rng& rng, Ball lowest) {
    std::vector<BallOnTable> balls;
    put(balls, Ball::cue, 0, rng);
    for (Ball colour : kColours) {
        if (table::ball_value(colour) >= table::ball_value(lowest)) put(balls, colour, 0, rng);
    }
    FrameState s = rules::scenario::frame(std::move(balls));
    s.strokes = 60;
    return s;
}

inline FrameState in_hand(FrameState s) {
    s.cue_in_hand = true;
    s.balls[std::size_t(s.cue_ball)].pos =
        rig().oracle->nearest_cue_placement(s.balls, s.cue_ball, table::default_cue_ball_position());
    return s;
}

// One red hidden from the cue ball behind the blue, the other colours on their spots.
inline FrameState snookered(Rng& rng) {
    for (;;) {
        std::vector<BallOnTable> balls;
        put(balls, Ball::cue, 0, rng);
        put(balls, Ball::red, 0, rng);
        const Vec2 cue = balls[0].pos;
        const Vec2 red = balls[1].pos;
        const double share = rng.uniform(0.3, 0.7);
        const Vec2 between{cue.x + (red.x - cue.x) * share, cue.y + (red.y - cue.y) * share};
        if (!room(balls, between, 0.01)) continue;
        for (Ball colour : kColours) {
            if (colour == Ball::blue) {
                balls.push_back({colour, 0, between});
            } else {
                put_colour(balls, colour, rng);
            }
        }
        FrameState s = rules::scenario::frame(std::move(balls));
        s.strokes = 40;
        if (rig().oracle->snookered(s.balls, s.cue_ball, rules::places_on(s), false)) return s;
    }
}

inline phys::TableState table_state(const rules::Position& p) { return rules::scenario::table_state(p); }

// Why the engine would not take the stroke as given; "" when it would. `placed` is the frame
// with the cue ball where the stroke plays from.
inline std::string refusal(const FrameState& s, const StandinShot& shot, FrameState& placed) {
    const rules::scenario::Rig& r = rig();
    placed = s;
    if (shot.decides) {
        const std::vector<rules::DecisionKind> open = rules::options(s, s.striker);
        for (rules::DecisionKind k : open) {
            if (k == shot.decision) return "";
        }
        return std::string(rules::decision_name(shot.decision)) + " is not open to the striker";
    }
    if (shot.places_cue && !s.cue_in_hand) return "places a cue ball that is not in hand";
    if (!shot.places_cue && s.cue_in_hand) return "leaves the cue ball in hand unplaced";
    std::string error;
    if (shot.places_cue && !rules::place_cue_ball(placed, shot.cue_pos, *r.oracle, error)) return "cue place: " + error;
    const std::vector<Ball> open = rules::nominations(placed);
    if (placed.on.kind == OnKind::any_colour && !shot.input.has_nomination) return "no colour declared";
    if (shot.input.has_nomination) {
        bool listed = false;
        for (Ball b : open) listed = listed || b == shot.input.nominated;
        if (!listed) return std::string(table::ball_name(shot.input.nominated)) + " cannot be declared";
    }
    const phys::CueStrike& c = shot.strike;
    if (!std::isfinite(c.aim_rad)) return "aim is not a number";
    if (!(c.speed_m_s > 0.0 && c.speed_m_s <= r.params.max_cue_speed_m_s)) {
        return "speed " + std::to_string(c.speed_m_s);
    }
    if (std::hypot(c.tip_x, c.tip_y) > r.params.miscue_limit) return "tip offset past the miscue limit";
    if (c.tip_x != 0.0 || c.tip_y != 0.0) return "not a centre-ball strike";
    const double least = phys::min_cue_elevation(table_state(placed), placed.cue_ball, c.aim_rad, r.table, r.params);
    if (c.elevation_rad < least - 1.0e-12 || c.elevation_rad > r.params.max_elevation_rad) {
        return "elevation " + std::to_string(c.elevation_rad) + ", the least is " + std::to_string(least);
    }
    return "";
}

// The stroke played through the simulator and the referee from `placed`.
inline rules::scenario::Played play(const FrameState& placed, const StandinShot& shot, std::uint64_t seed = 1) {
    return rules::scenario::play(placed, shot.strike, shot.input, seed);
}

}  // namespace ofs::opponent::standin_test
