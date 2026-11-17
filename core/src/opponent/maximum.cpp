// The 147's own reading of a table (spec 10.1, docs/opponent/README.md "The 147"): how alive a
// maximum is in a position, for the search to choose by.

#include "maximum.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/level.hpp"
#include "ofs/opponent/solve.hpp"

namespace ofs::opponent {

namespace {

using rules::Ball;
using rules::BallOnTable;
using rules::FrameState;
using rules::OnKind;
using rules::Vec2;

// A position at its best: an easy pot with everything after it in place.
constexpr double kWorth = 12.0;
// How much of a pot's worth is the pot itself; the rest is what follows it.
constexpr double kNow = 0.45;
// A red this near the black leaves the cue ball beside it; further off, getting back is a stroke
// across the table, worth less by this much a metre down to kFarRed.
constexpr double kNearBlack_m = 0.35;
constexpr double kPerMetreFromBlack = 0.45;
constexpr double kFarRed = 0.45;
// The black after a red: open to both top corners, to one, to neither.
constexpr double kBlackOpen[] = {0.1, 0.7, 1.0};
// With the maximum gone another colour does for the black.
constexpr double kAnotherColour = 0.6;
// A red to go on with after a colour, by how many are open: one is a break hanging on a single
// ball, three or more leave a spare.
constexpr double kRedsAfter[] = {0.03, 0.55, 0.8, 0.92, 1.0};
// The colours after the last red start from the yellow, a table's length from the black.
constexpr double kYellowAfterBlack = 0.6;
constexpr double kNextColourShut = 0.3;
// A red still shut in must be opened by a stroke that also pots: each counts this much of an open one.
constexpr double kShutRed = 0.4;
constexpr double kHealth = 0.35;

// The steady player a pot's room is measured by.
Level yardstick() {
    Level l;
    l.aim_sigma_rad = 0.0016;
    l.aim_sigma_per_cut = 1.0;
    l.aim_sigma_per_m = 0.5;
    l.awkward_factor = 1.6;
    return l;
}

int place_of(const FrameState& s, Ball ball) {
    for (std::size_t i = 0; i < s.balls.size(); ++i) {
        if (s.balls[i].ball == ball) return int(i);
    }
    return -1;
}

double distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

double black_open(const FrameState& s, int black, const Env& env) {
    if (black < 0) return 0.0;
    int corners = 0;
    for (const int k : pockets_open_to(s, black, env)) {
        const phys::Pocket& pocket = env.table.pockets[std::size_t(k)];
        if (pocket.corner && pocket.drop_centre.x > 0.0) ++corners;
    }
    return kBlackOpen[std::min(corners, 2)];
}

}  // namespace

Outlook read_table(const rules::FrameState& s, const std::vector<Pot>& listed, const Env& env) {
    Outlook out;
    if (s.over || listed.empty()) return out;
    const Level yard = yardstick();
    const bool on = maximum_on(s);
    const Ball needed = maximum_next(s);

    int reds = 0;
    for (const BallOnTable& b : s.balls) reds += b.ball == Ball::red ? 1 : 0;
    const int open = reds > 0 ? open_reds(s, env) : 0;
    const int black = place_of(s, Ball::black);

    double colour_follows = 0.0;
    if (s.on.kind == OnKind::red) {
        colour_follows = black_open(s, black, env);
        if (!on) colour_follows = std::max(colour_follows, kAnotherColour);
    }
    double red_follows = kYellowAfterBlack;
    if (reds > 0) red_follows = kRedsAfter[std::min(open, 4)];

    double best = 0.0;
    for (const Pot& pot : listed) {
        const BallOnTable& ball = s.balls[std::size_t(pot.target)];
        if (on && ball.ball != needed) continue;
        const double room = pot_chance(yard, pot, Spec{}, pot.margin_rad);
        double weight = 1.0;
        double after = 1.0;
        switch (s.on.kind) {
            case OnKind::red: {
                after = colour_follows;
                if (black >= 0) {
                    const double away = distance(ball.pos, s.balls[std::size_t(black)].pos) - kNearBlack_m;
                    after *= std::clamp(1.0 - kPerMetreFromBlack * away, kFarRed, 1.0);
                }
                break;
            }
            case OnKind::any_colour:
                // Gone, the colours count by their points: a blue is five sevenths of a black.
                if (!on) weight = pot.value / 7.0;
                after = red_follows;
                break;
            case OnKind::colour: {
                if (ball.ball == Ball::black || s.free_ball) break;
                const int next = place_of(s, Ball(int(s.on.colour) + 1));
                if (next >= 0 && pockets_open_to(s, next, env).empty()) after = kNextColourShut;
                break;
            }
        }
        const double score = room * weight * (kNow + (1.0 - kNow) * after);
        if (out.next < 0 || score > best) {
            best = score;
            out.next = pot.target;
            out.pocket = pot.pocket;
            out.chance = room;
        }
    }
    if (out.next < 0) return out;

    double health = 1.0;
    if (reds > 0) health = (open + kShutRed * (reds - open)) / reds;
    out.worth = kWorth * best * (1.0 - kHealth + kHealth * health);
    return out;
}

}  // namespace ofs::opponent
