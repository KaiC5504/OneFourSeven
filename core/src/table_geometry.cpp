#include "ofs/table_geometry.hpp"

// Contract stub: agent B replaces every body in this file.

namespace ofs::table {

int ball_value(Ball) { return 0; }
std::string_view ball_name(Ball) { return "cue"; }
bool parse_ball(std::string_view, Ball&) { return false; }
double baulk_cushion_x() { return 0.0; }
double top_cushion_x() { return 0.0; }
double baulk_line_x() { return 0.0; }
Vec2 d_centre() { return {}; }
bool in_d(Vec2) { return false; }
Vec2 spot(Ball) { return {}; }
Vec2 default_cue_ball_position() { return {}; }
std::vector<BallPlacement> opening_layout(double) { return {}; }

}  // namespace ofs::table
