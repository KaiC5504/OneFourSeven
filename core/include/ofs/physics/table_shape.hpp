#pragma once

// The cushion and pocket outline (spec 6.3, 7.5, 7.6), from core/data/table_shape.json, which
// records every dimension with its source. Straight cushion sections are line segments; pocket
// jaws are convex circular arcs (rounded noses); a ball is potted when its centre enters a
// pocket's drop circle, which sits behind the jaws.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/table_geometry.hpp"

namespace ofs::phys {

// A straight cushion nose. The ball touches it when its centre is R from the segment, on the
// side `normal` points to (into the playing area), with the contact point between p1 and p2.
struct LineCushion {
    table::Vec2 p1;
    table::Vec2 p2;
    table::Vec2 normal;  // unit
};

// A rounded jaw: the ball touches it when its centre is radius + R from `centre`, in the
// direction from the centre whose angle (atan2, from +x towards +y) lies in
// [angle_start, angle_end] going anticlockwise.
struct ArcCushion {
    table::Vec2 centre;
    double radius = 0.0;
    double angle_start = 0.0;
    double angle_end = 0.0;
    int pocket = -1;  // the pocket whose jaw this is
};

struct Pocket {
    std::string name;  // "top_left", "top_right", "middle_left", "middle_right", "bottom_left", "bottom_right"
    bool corner = true;
    table::Vec2 drop_centre;
    double drop_radius = 0.0;
};

struct TableShape {
    std::vector<LineCushion> lines;
    std::vector<ArcCushion> arcs;  // jaws use the jaw_* params, lines the cushion_* params
    std::vector<Pocket> pockets;
    // For the cue obstruction (spec 7.3): the rail top's height above the cloth and its outer
    // edge's distance behind the cushion nose.
    double rail_height_m = 0.0;
    double rail_width_m = 0.0;
};

// Parses table_shape.json. False with every problem appended when the file is malformed or the
// outline is inconsistent (a non-unit normal, an arc with no pocket, a pocket without jaws).
bool load_table_shape(const nlohmann::json& file, TableShape& out, std::vector<std::string>& problems);

}  // namespace ofs::phys
