#include "ofs/opponent/evaluate.hpp"

#include <cstddef>

#include "ofs/opponent/error_model.hpp"

namespace ofs::opponent {

namespace {

using rules::Ball;
using rules::OnKind;

int reds_left(const rules::FrameState& s) {
    int reds = 0;
    for (const rules::BallOnTable& b : s.balls) {
        if (b.ball == Ball::red) ++reds;
    }
    return reds;
}

}  // namespace

Read read_pots(const std::vector<Pot>& pots, const Level& level, const Env&) {
    Read read;
    read.pots = int(pots.size());
    for (std::size_t i = 0; i < pots.size(); ++i) {
        const Pot& pot = pots[i];
        const double chance = pot_chance(level, pot, Spec{}, pot.margin_rad);
        const double worth = chance * (pot.value + kBreakOn);
        if (read.best < 0 || worth > read.worth) {
            read.worth = worth;
            read.best = int(i);
            read.chance = chance;
        }
    }
    return read;
}

Read read_position(const rules::FrameState& s, const Level& level, const Env& env) {
    return read_pots(pots(s, env, level.max_cut_rad), level, env);
}

bool maximum_on(const rules::FrameState& s) {
    if (s.over || s.pending != rules::Pending::none || s.free_ball || s.respotted_black) return false;
    const int reds = reds_left(s);
    switch (s.on.kind) {
        case OnKind::red:
            return s.break_points == 8 * (15 - reds);
        case OnKind::any_colour:
            return s.break_points == 8 * (15 - reds) - 7;
        case OnKind::colour: {
            if (reds != 0) return false;
            int gone = 0;
            for (Ball c = Ball::yellow; c != s.on.colour && c != Ball::black; c = Ball(int(c) + 1)) {
                gone += table::ball_value(c);
            }
            return s.break_points == 120 + gone;
        }
    }
    return false;
}

Ball maximum_next(const rules::FrameState& s) {
    switch (s.on.kind) {
        case OnKind::red:
            return Ball::red;
        case OnKind::any_colour:
            return Ball::black;
        case OnKind::colour:
            return s.on.colour;
    }
    return Ball::red;
}

}  // namespace ofs::opponent
