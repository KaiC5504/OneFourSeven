#include "ofs/rules/engine.hpp"

#include <array>
#include <cmath>

namespace ofs::rules {

namespace {

constexpr std::array<Ball, 7> kObjectBallsByValue = {Ball::red,  Ball::yellow, Ball::green, Ball::brown,
                                                     Ball::blue, Ball::pink,   Ball::black};

bool is_colour(Ball b) { return b != Ball::cue && b != Ball::red; }

bool is_on(const Position& p, Ball b) {
    switch (p.on.kind) {
        case OnKind::red: return b == Ball::red;
        case OnKind::any_colour: return is_colour(b);
        case OnKind::colour: return b == p.on.colour;
    }
    return false;
}

bool on_table(const Position& p, Ball b) {
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        if (int(i) != p.cue_ball && p.balls[i].ball == b) return true;
    }
    return false;
}

}  // namespace

FrameState new_frame(int breaker, const phys::TableState& rack, std::uint64_t seed) {
    FrameState s;
    s.seed = seed;
    s.striker = breaker == 1 ? 1 : 0;
    s.cue_in_hand = true;
    s.on = {OnKind::red, Ball::yellow};
    for (const phys::BallState& b : rack.balls) {
        if (b.ball == Ball::cue) s.cue_ball = int(s.balls.size());
        s.balls.push_back({b.ball, b.index, {b.r.x, b.r.y}});
    }
    return s;
}

std::vector<int> places_on(const Position& p) {
    std::vector<int> out;
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        if (int(i) != p.cue_ball && is_on(p, p.balls[i].ball)) out.push_back(int(i));
    }
    return out;
}

std::vector<Ball> nominations(const FrameState& s) {
    std::vector<Ball> out;
    if (s.over) return out;
    const bool choice = s.on.kind == OnKind::any_colour;
    if (!choice && !s.free_ball) return out;
    for (Ball b : kObjectBallsByValue) {
        if (!on_table(s, b)) continue;
        if (choice ? is_colour(b) : !is_on(s, b)) out.push_back(b);
    }
    return out;
}

int points_remaining(const FrameState& s) {
    if (s.respotted_black) return 7;
    int reds = 0;
    int colours = 0;
    for (std::size_t i = 0; i < s.balls.size(); ++i) {
        if (int(i) == s.cue_ball) continue;
        const Ball b = s.balls[i].ball;
        if (b == Ball::red) {
            ++reds;
        } else if (is_colour(b)) {
            colours += table::ball_value(b);
        }
    }
    // Each red is worth itself and a black; the colours are all back on their spots while one remains.
    int total = reds > 0 ? 8 * reds + 27 : colours;
    if (s.on.kind == OnKind::any_colour) total += 7;
    return total;
}

int penalty_points_required(const FrameState& s, int player) {
    if (player != 0 && player != 1) return 0;
    const int deficit = s.scores[std::size_t(1 - player)] - s.scores[std::size_t(player)];
    const int needed = deficit - points_remaining(s);
    return needed > 0 ? needed : 0;
}

bool place_cue_ball(FrameState& s, Vec2 pos, const TableOracle& oracle, std::string& error) {
    if (s.over || s.restart) {
        error = "the frame is over";
        return false;
    }
    if (s.pending != Pending::none) {
        error = "a decision is pending";
        return false;
    }
    if (!s.cue_in_hand) {
        error = "the cue ball is not in hand";
        return false;
    }
    if (s.cue_ball < 0 || s.cue_ball >= int(s.balls.size())) {
        error = "the frame has no cue ball";
        return false;
    }
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y)) {
        error = "the place is not a point on the table";
        return false;
    }
    if (!oracle.cue_placement_legal(s.balls, s.cue_ball, pos)) {
        error = "the cue ball must be in the D and clear of every ball";
        return false;
    }
    s.balls[std::size_t(s.cue_ball)].pos = pos;
    error.clear();
    return true;
}

}  // namespace ofs::rules
