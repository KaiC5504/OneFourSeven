#include "spotting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ofs::rules {

namespace {

constexpr std::array<Ball, 6> kSpotsByValue = {Ball::black, Ball::pink,  Ball::blue,
                                               Ball::brown, Ball::green, Ball::yellow};

bool is_colour(Ball b) { return b != Ball::cue && b != Ball::red; }

// S2.18: a ball at `p` would touch none of `taken`. `clear` is the centre distance to exceed.
bool available(const std::vector<Vec2>& taken, Vec2 p, double clear) {
    for (const Vec2& q : taken) {
        const double dx = p.x - q.x;
        const double dy = p.y - q.y;
        if (dx * dx + dy * dy <= clear * clear) return false;
    }
    return true;
}

// The free place nearest `from` on the line through it along the table, on the side `towards`
// (+1 the top cushion, -1 baulk) and no further than x = `limit`. `from` itself is taken, so the
// nearest free place is just clear of one of the balls in the way: those are the candidates.
bool nearest_on_line(const std::vector<Vec2>& taken, Vec2 from, double towards, double limit, double clear,
                     Vec2& out) {
    bool found = false;
    double best = 0.0;
    for (const Vec2& q : taken) {
        const double dy = from.y - q.y;
        if (!(std::abs(dy) <= clear)) continue;
        const double x = q.x + towards * (std::sqrt(clear * clear - dy * dy) + kJustClear);
        const double along = towards * (x - from.x);
        if (along <= 0.0 || towards * (limit - x) < 0.0) continue;
        if (found && along >= best) continue;
        if (!available(taken, {x, from.y}, clear)) continue;
        found = true;
        best = along;
        out = {x, from.y};
    }
    return found;
}

// Where a colour goes whose own spot is occupied.
Vec2 place_elsewhere(Ball colour, const std::vector<Vec2>& taken, double clear) {
    const Vec2 own = table::spot(colour);
    for (Ball other : kSpotsByValue) {
        const Vec2 spot = table::spot(other);
        if (other != colour && available(taken, spot, clear)) return spot;
    }
    // S3.7(g): from every spot the nearest part of the top cushion is straight up the table.
    Vec2 p;
    if (nearest_on_line(taken, own, 1.0, table::top_cushion_x() - table::kBallRadius, clear, p)) return p;
    // S3.7(h) puts pink and black on the centre line, which from their spots is this same line
    // going down towards baulk. The rulebook is silent on the other four with no room above
    // them; they go down their own line the same way.
    if (nearest_on_line(taken, own, -1.0, table::baulk_cushion_x() + table::kBallRadius, clear, p)) return p;
    // A whole line of the table full takes more balls than a frame has.
    return own;
}

}  // namespace

std::vector<BallOnTable> spot_colours(const std::vector<BallOnTable>& on_table, const std::vector<Ball>& colours,
                                      double touching_gap_m) {
    const double clear = table::kBallDiameter + touching_gap_m;
    std::vector<Vec2> taken;
    taken.reserve(on_table.size() + colours.size());
    for (const BallOnTable& b : on_table) taken.push_back(b.pos);

    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < colours.size(); ++i) {
        if (is_colour(colours[i])) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return table::ball_value(colours[a]) > table::ball_value(colours[b]);
    });
    // Every colour whose own spot is free goes on it before any other is looked at: S3.7(f)'s
    // precedence is only among those whose spots are occupied. Each colour placed is in the way
    // of the ones after it.
    std::vector<Vec2> placed(colours.size());
    std::vector<std::size_t> displaced;
    for (std::size_t i : order) {
        const Vec2 own = table::spot(colours[i]);
        if (!available(taken, own, clear)) {
            displaced.push_back(i);
            continue;
        }
        placed[i] = own;
        taken.push_back(own);
    }
    for (std::size_t i : displaced) {
        placed[i] = place_elsewhere(colours[i], taken, clear);
        taken.push_back(placed[i]);
    }

    std::vector<BallOnTable> out;
    out.reserve(order.size());
    for (std::size_t i = 0; i < colours.size(); ++i) {
        if (is_colour(colours[i])) out.push_back({colours[i], 0, placed[i]});
    }
    return out;
}

}  // namespace ofs::rules
