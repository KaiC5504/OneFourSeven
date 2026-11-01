#include "ofs_table.h"

#include <string>

#include "ofs/table_geometry.hpp"

using namespace godot;

namespace {

String from_std(std::string_view text) {
    return String::utf8(text.data(), int64_t(text.size()));
}

}  // namespace

void OfsTable::_bind_methods() {
    ClassDB::bind_static_method("OfsTable", D_METHOD("dimensions"), &OfsTable::dimensions);
    ClassDB::bind_static_method("OfsTable", D_METHOD("opening_layout", "gap"), &OfsTable::opening_layout);
    ClassDB::bind_static_method("OfsTable", D_METHOD("spot", "colour"), &OfsTable::spot);
}

Dictionary OfsTable::dimensions() {
    Dictionary out;
    out["playing_length"] = ofs::table::kPlayingLength;
    out["playing_width"] = ofs::table::kPlayingWidth;
    out["ball_radius"] = ofs::table::kBallRadius;
    out["baulk_line_x"] = ofs::table::baulk_line_x();
    out["d_radius"] = ofs::table::kDRadius;
    out["baulk_cushion_x"] = ofs::table::baulk_cushion_x();
    out["top_cushion_x"] = ofs::table::top_cushion_x();
    return out;
}

Array OfsTable::opening_layout(double gap) {
    Array out;
    for (const ofs::table::BallPlacement &placement : ofs::table::opening_layout(gap)) {
        Dictionary entry;
        entry["ball"] = from_std(ofs::table::ball_name(placement.ball));
        entry["index"] = int64_t(placement.index);
        entry["x"] = placement.position.x;
        entry["y"] = placement.position.y;
        out.push_back(entry);
    }
    return out;
}

Dictionary OfsTable::spot(const String &colour) {
    Dictionary out;
    const CharString name = colour.utf8();
    ofs::table::Ball ball = ofs::table::Ball::cue;
    if (!ofs::table::parse_ball(std::string_view(name.get_data(), size_t(name.length())), ball) ||
        ball == ofs::table::Ball::cue || ball == ofs::table::Ball::red) {
        return out;
    }
    const ofs::table::Vec2 position = ofs::table::spot(ball);
    out["x"] = position.x;
    out["y"] = position.y;
    return out;
}
