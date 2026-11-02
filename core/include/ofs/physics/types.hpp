#pragma once

// Shared physics types (spec 7). Frame and units: table_geometry.hpp and vec.hpp.
//
// Spin convention: w is the angular velocity in the table frame. The cloth contact point sits at
// (0, 0, -R) from the centre, so its slip velocity is u = v + w x (0, 0, -R)
// = (v.x - R w.y, v.y + R w.x, 0). A ball rolls when u = 0: rolling towards +x has w.y = v.x / R
// (topspin), and w.z is side spin, positive anticlockwise seen from above.

#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include "ofs/table_geometry.hpp"
#include "ofs/vec.hpp"

namespace ofs::phys {

inline constexpr double kNever = std::numeric_limits<double>::infinity();

enum class Motion : std::uint8_t { stationary, spinning, sliding, rolling, pocketed };

std::string_view motion_name(Motion m);
bool parse_motion(std::string_view name, Motion& out);

struct BallState {
    table::Ball ball = table::Ball::cue;
    int index = 0;  // 0-14 for reds, as in table::BallPlacement; 0 otherwise
    Vec3 r;         // centre, z = 0
    Vec3 v;         // z = 0
    Vec3 w;
    Motion motion = Motion::stationary;
    int pocket = -1;  // TableShape::pockets index once potted
};

// Balls are addressed by their index in `balls` everywhere: events, records, logs.
struct TableState {
    std::vector<BallState> balls;
};

struct CueStrike {
    double aim_rad = 0.0;        // direction of the cue in the table plane, from +x towards +y
    double tip_x = 0.0;          // tip offset right of centre as the player sees it, fraction of R
    double tip_y = 0.0;          // tip offset above centre, fraction of R (positive: topspin)
    double elevation_rad = 0.0;  // cue butt raised above horizontal
    double speed_m_s = 0.0;      // cue speed at impact
};

// A ball centre's path within one motion segment: r(t) = c0 + c1 t + c2 t^2 for t in
// [0, duration], t measured from the segment's start. Every state's motion is at most
// quadratic (spec 7.1), nap included because its acceleration is held constant per tick.
struct Trajectory {
    Vec3 c0;
    Vec3 c1;
    Vec3 c2;
    double duration = kNever;

    Vec3 at(double t) const { return c0 + c1 * t + c2 * (t * t); }
    Vec3 velocity(double t) const { return c1 + c2 * (2.0 * t); }
    // The same path with its origin moved dt later.
    Trajectory shifted(double dt) const {
        return {at(dt), velocity(dt), c2, duration - dt};
    }
};

}  // namespace ofs::phys
