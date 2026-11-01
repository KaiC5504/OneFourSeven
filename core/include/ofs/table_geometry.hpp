#pragma once

// Table geometry from the WPBSA Official Rules (2022 rulebook), section 1 (spec section 6).
//
// Coordinates are metres on the cloth plane. The origin is the centre of the playing
// area. +x runs along the long axis towards the top (black) cushion; +y runs across the
// table towards the left-hand side cushion as seen from the baulk end. With z up through
// the cloth this is right-handed. A ball position is its centre projected onto the cloth.

#include <cstdint>
#include <string_view>
#include <vector>

namespace ofs::table {

inline constexpr double kPlayingLength = 3.569;  // between cushion noses, long axis
inline constexpr double kPlayingWidth = 1.778;
inline constexpr double kBallDiameter = 0.0525;
inline constexpr double kBallRadius = kBallDiameter / 2.0;
inline constexpr double kBaulkLineFromBaulkCushion = 0.737;
inline constexpr double kDRadius = 0.292;
inline constexpr double kBlackSpotFromTopCushion = 0.324;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

enum class Ball : std::uint8_t { cue, red, yellow, green, brown, blue, pink, black };

// 1 for red, 2 (yellow) through 7 (black) for colours, 0 for the cue ball.
int ball_value(Ball ball);
// "cue", "red", "yellow", ... as used in logs and the Godot binding.
std::string_view ball_name(Ball ball);
bool parse_ball(std::string_view name, Ball& out);

double baulk_cushion_x();  // the baulk cushion face, -kPlayingLength / 2
double top_cushion_x();    // the top cushion face, +kPlayingLength / 2
double baulk_line_x();
Vec2 d_centre();           // the middle of the baulk line
// Inside or on the D: on or behind the baulk line and within kDRadius of its centre.
bool in_d(Vec2 p);

// The colour's own spot. Precondition: ball is a colour (yellow through black).
Vec2 spot(Ball colour);

struct BallPlacement {
    Ball ball = Ball::cue;
    int index = 0;  // 0-14 for reds, numbered row by row from the apex; 0 for every other ball
    Vec2 position;
};

// Where the cue ball sits before the break-off unless the player moves it: in the D,
// halfway between the brown and yellow spots.
Vec2 default_cue_ball_position();

// The standard opening layout: 15 reds in a triangle, the six colours on their spots and
// the cue ball at default_cue_ball_position(); 22 placements, cue ball first, then reds
// 0-14, then yellow, green, brown, blue, pink, black.
//
// The apex red sits on the centre line behind the pink (towards the top cushion), its
// surface `gap` metres from the pink's; with s = 2R + gap, each further row sits s * sqrt(3)/2
// further back (an equilateral triangle), with the base row parallel to the top cushion. `gap` is also the clearance between
// neighbouring reds. gap = 0 gives touching balls. The seeded microscopic gaps of spec
// 7.7 are built on top of this in M1, and must keep the apex clear of the pink: the rules
// (3.2(a)(i), 2.18) don't allow the apex to touch it.
std::vector<BallPlacement> opening_layout(double gap);

}  // namespace ofs::table
