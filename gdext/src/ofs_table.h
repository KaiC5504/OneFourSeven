#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

// Table geometry for the game (ofs::table), in core coordinates and metres. The game maps
// core (x, y) to Godot's Vector3(-y, height, -x): Godot's -Z is the core's +x (towards the
// black) and Godot's +X is the core's -y, so a camera at the baulk end looking down -Z sees
// the table as a player standing at baulk does. game/scripts/table_space.gd owns that mapping.
class OfsTable : public godot::RefCounted {
    GDCLASS(OfsTable, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    // {playing_length, playing_width, ball_radius, baulk_line_x, d_radius,
    //  baulk_cushion_x, top_cushion_x} in metres, core coordinates.
    static godot::Dictionary dimensions();
    // opening_layout(gap) as an Array of {ball: String, index: int, x: float, y: float}.
    static godot::Array opening_layout(double gap);
    // {x, y} of a colour's spot; empty Dictionary for "cue", "red" or an unknown name.
    static godot::Dictionary spot(const godot::String &colour);
};
