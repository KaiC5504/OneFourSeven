#include "ofs/table_geometry.hpp"

#include <array>
#include <cassert>
#include <cmath>

namespace ofs::table {

namespace {

constexpr std::array<Ball, 8> kAllBalls = {Ball::cue,   Ball::red,  Ball::yellow, Ball::green,
                                           Ball::brown, Ball::blue, Ball::pink,   Ball::black};

// Absorbs rounding when a point is computed to lie exactly on the D's arc or baulk line.
constexpr double kInDTolerance = 1e-12;

}  // namespace

int ball_value(Ball ball) {
    switch (ball) {
        case Ball::cue: return 0;
        case Ball::red: return 1;
        case Ball::yellow: return 2;
        case Ball::green: return 3;
        case Ball::brown: return 4;
        case Ball::blue: return 5;
        case Ball::pink: return 6;
        case Ball::black: return 7;
    }
    return 0;
}

std::string_view ball_name(Ball ball) {
    switch (ball) {
        case Ball::cue: return "cue";
        case Ball::red: return "red";
        case Ball::yellow: return "yellow";
        case Ball::green: return "green";
        case Ball::brown: return "brown";
        case Ball::blue: return "blue";
        case Ball::pink: return "pink";
        case Ball::black: return "black";
    }
    return "cue";
}

bool parse_ball(std::string_view name, Ball& out) {
    for (Ball ball : kAllBalls) {
        if (ball_name(ball) == name) {
            out = ball;
            return true;
        }
    }
    return false;
}

double baulk_cushion_x() { return -kPlayingLength / 2.0; }

double top_cushion_x() { return kPlayingLength / 2.0; }

double baulk_line_x() { return baulk_cushion_x() + kBaulkLineFromBaulkCushion; }

Vec2 d_centre() { return {baulk_line_x(), 0.0}; }

bool in_d(Vec2 p) {
    if (p.x > baulk_line_x() + kInDTolerance) {
        return false;
    }
    const Vec2 c = d_centre();
    return std::hypot(p.x - c.x, p.y - c.y) <= kDRadius + kInDTolerance;
}

Vec2 spot(Ball colour) {
    switch (colour) {
        case Ball::yellow: return {baulk_line_x(), -kDRadius};
        case Ball::green: return {baulk_line_x(), kDRadius};
        case Ball::brown: return d_centre();
        case Ball::blue: return {0.0, 0.0};
        case Ball::pink: return {top_cushion_x() / 2.0, 0.0};
        case Ball::black: return {top_cushion_x() - kBlackSpotFromTopCushion, 0.0};
        case Ball::cue:
        case Ball::red: break;
    }
    assert(false && "spot() needs a colour");
    return {0.0, 0.0};
}

Vec2 default_cue_ball_position() {
    const Vec2 brown = spot(Ball::brown);
    const Vec2 yellow = spot(Ball::yellow);
    return {(brown.x + yellow.x) / 2.0, (brown.y + yellow.y) / 2.0};
}

std::vector<BallPlacement> opening_layout(double gap) {
    std::vector<BallPlacement> layout;
    layout.reserve(22);
    layout.push_back({Ball::cue, 0, default_cue_ball_position()});

    const double spacing = kBallDiameter + gap;
    const double row_depth = spacing * std::sqrt(3.0) / 2.0;
    const double apex_x = spot(Ball::pink).x + spacing;
    int index = 0;
    for (int row = 0; row < 5; ++row) {
        const double x = apex_x + row * row_depth;
        for (int i = 0; i <= row; ++i) {
            const double y = (row / 2.0 - i) * spacing;
            layout.push_back({Ball::red, index++, {x, y}});
        }
    }

    for (Ball colour : {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        layout.push_back({colour, 0, spot(colour)});
    }
    return layout;
}

}  // namespace ofs::table
