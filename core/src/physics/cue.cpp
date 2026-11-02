#include "ofs/physics/cue.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ofs::phys {

namespace {

constexpr double kR = table::kBallRadius;
constexpr double kTwoPi = 6.283185307179586476925286766559;

// The cue radius that matters depends on the elevation (through the taper and where along the cue
// an obstacle is nearest), so each obstacle re-solves with the radius at the previous answer. It
// changes slowly with the elevation, so a few passes converge.
constexpr int kMaxPasses = 8;
constexpr double kConvergedRad = 1e-12;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

// The cue's axis seen from above runs from `origin` along `back`. Square to the axis, it passes
// tip_x R right of the cue ball centre (folded into `origin`) and `lift` = tip_y R above it.
// `contact` is how far along the axis, from where it passes the centre, the tip meets the ball.
struct CueLine {
    Vec2 origin;
    Vec2 back;
    double lift = 0.0;
    double contact = 0.0;
};

double cue_radius(double from_tip, const PhysicsParams& p) {
    const double f = std::clamp(from_tip / p.cue_length_m, 0.0, 1.0);
    return 0.5 * (p.cue_tip_diameter_m + (p.cue_butt_diameter_m - p.cue_tip_diameter_m) * f);
}

// How fast the cue's radius grows along it.
double taper(const PhysicsParams& p) {
    return 0.5 * (p.cue_butt_diameter_m - p.cue_tip_diameter_m) / p.cue_length_m;
}

// The smallest theta in [0, max_theta] with a sin(theta) - b cos(theta) >= k, for a > 0; max_theta
// when there is none. The left side is hypot(a, b) sin(theta - atan2(b, a)), which for b >= 0 keeps
// rising up to 90 degrees, so every larger elevation clears too.
double min_angle(double a, double b, double k, double max_theta) {
    if (-b >= k) return 0.0;
    const double rho = std::hypot(a, b);
    if (k >= rho) return max_theta;
    return std::min(std::atan2(b, a) + std::asin(k / rho), max_theta);
}

// Both obstacles below use the distance from the cue's axis line and the cue's radius where the
// axis is nearest. A tapered cue closes on the obstacle slightly further back, by a factor
// sqrt(1 - taper^2 / sigma^2), sigma being the sine of the angle between the axis and the
// obstacle's edge (1 for a ball). When the nearest point lies ahead of the tip, the tip end is
// further away than the line, and the answer errs high.

// A horizontal rail edge at `height`, crossing the cue's line `station` behind the cue ball centre;
// `outward` is the cosine between that line and the edge's outward normal. The axis passes the edge
// line at (station sin + lift - (height - R) cos) outward / sigma, nearest at `foot` along the axis.
double edge_elevation(const CueLine& cue, double station, double height, double outward,
                      const PhysicsParams& p) {
    if (station - cue.contact > p.cue_length_m) return 0.0;
    const double oblique2 = 1.0 - outward * outward;
    const double taper2 = taper(p) * taper(p);
    double theta = 0.0;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        const double s = std::sin(theta);
        const double c = std::cos(theta);
        const double sigma2 = 1.0 - c * c * oblique2;
        const double foot =
            (station * c * outward * outward - cue.lift * s * c * oblique2 + (height - kR) * s) / sigma2;
        // sigma >= outward, which stays well above the taper for any edge within the cue's reach;
        // the floor only guards odd cue dimensions.
        const double grip = std::sqrt(std::max(sigma2 - taper2, 0.25 * sigma2));
        const double reach =
            (cue_radius(foot - cue.contact, p) + p.cue_clearance_m) * sigma2 / (outward * grip);
        const double next = min_angle(station, height - kR, reach - cue.lift, p.max_elevation_rad);
        if (std::abs(next - theta) <= kConvergedRad) return next;
        theta = next;
    }
    return theta;
}

// Another ball `behind` the cue ball along the cue's line and `side` to its right. The axis passes
// its centre at hypot(behind sin + lift, side), nearest at behind cos along the axis. Over it is the
// only way past: a ball on the cloth leaves no room under it.
double ball_elevation(const CueLine& cue, double behind, double side, const PhysicsParams& p) {
    const double grip = std::sqrt(1.0 - taper(p) * taper(p));
    double theta = 0.0;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        const double from_tip = behind * std::cos(theta) - cue.contact;
        const double reach = (kR + cue_radius(from_tip, p) + p.cue_clearance_m) / grip;
        const double over = reach * reach - side * side;
        const double next =
            over <= 0.0 ? 0.0 : min_angle(behind, 0.0, std::sqrt(over) - cue.lift, p.max_elevation_rad);
        if (std::abs(next - theta) <= kConvergedRad) return next;
        theta = next;
    }
    return theta;
}

bool within_arc(Vec2 from_centre, const ArcCushion& a) {
    const double span = a.angle_end - a.angle_start;
    if (span >= kTwoPi) return true;
    double rel = std::fmod(std::atan2(from_centre.y, from_centre.x) - a.angle_start, kTwoPi);
    if (rel < 0.0) rel += kTwoPi;
    double end = std::fmod(span, kTwoPi);
    if (end < 0.0) end += kTwoPi;
    return rel <= end;
}

struct RailCrossing {
    double station = 0.0;  // along the cue's line to the cushion nose
    double depth = 0.0;    // along the cue's line across the rail top
    double outward = 1.0;
};

void consider(RailCrossing& best, bool& found, double station, double outward, const TableShape& t) {
    if (found && station >= best.station) return;
    found = true;
    best.station = station;
    best.depth = t.rail_width_m / outward;
    best.outward = outward;
}

// Where the cue's line, going back from the cue ball, first passes a cushion nose or jaw on its
// way off the playing area. `outward` is the cosine between the line and the nose's outward normal.
bool first_rail_crossing(const CueLine& cue, const TableShape& t, RailCrossing& out) {
    bool found = false;
    for (const LineCushion& l : t.lines) {
        const Vec2 n{l.normal.x, l.normal.y};
        const double outward = -dot(cue.back, n);
        if (outward <= 0.0) continue;
        const Vec2 from_p1{cue.origin.x - l.p1.x, cue.origin.y - l.p1.y};
        const double station = dot(from_p1, n) / outward;
        if (station < 0.0) continue;
        const Vec2 along{l.p2.x - l.p1.x, l.p2.y - l.p1.y};
        const Vec2 hit{from_p1.x + station * cue.back.x, from_p1.y + station * cue.back.y};
        const double f = dot(hit, along);
        if (f < 0.0 || f > dot(along, along)) continue;
        consider(out, found, station, outward, t);
    }
    for (const ArcCushion& a : t.arcs) {
        const Vec2 w{cue.origin.x - a.centre.x, cue.origin.y - a.centre.y};
        const double proj = dot(w, cue.back);
        const double outside = dot(w, w) - a.radius * a.radius;
        const double disc = proj * proj - outside;
        if (outside <= 0.0 || disc < 0.0 || a.radius <= 0.0) continue;
        const double station = -proj - std::sqrt(disc);
        if (station < 0.0) continue;
        const Vec2 hit{w.x + station * cue.back.x, w.y + station * cue.back.y};
        if (!within_arc(hit, a)) continue;
        const double outward = -dot(cue.back, hit) / a.radius;
        if (outward <= 0.0) continue;
        consider(out, found, station, outward, t);
    }
    return found;
}

// min_cue_elevation for any tip offset; the header's version is the centre-ball one.
double min_elevation(const TableState& s, int cue_ball, double cos_aim, double sin_aim, double tip_x,
                     double tip_y, const TableShape& t, const PhysicsParams& p) {
    const BallState& cb = s.balls[static_cast<std::size_t>(cue_ball)];
    const Vec2 right{sin_aim, -cos_aim};
    CueLine cue;
    cue.origin = {cb.r.x + right.x * tip_x * kR, cb.r.y + right.y * tip_x * kR};
    cue.back = {-cos_aim, -sin_aim};
    cue.lift = tip_y * kR;
    cue.contact = kR * std::sqrt(std::max(0.0, 1.0 - tip_x * tip_x - tip_y * tip_y));

    double theta = 0.0;
    RailCrossing rail;
    if (first_rail_crossing(cue, t, rail)) {
        // The cue's underside changes height linearly across the rail top, so its two edges bound it.
        const double far = rail.station + rail.depth;
        theta = std::max(theta, edge_elevation(cue, rail.station, t.rail_height_m, rail.outward, p));
        theta = std::max(theta, edge_elevation(cue, far, t.rail_height_m, rail.outward, p));
    }

    const double widest =
        (kR + 0.5 * p.cue_butt_diameter_m + p.cue_clearance_m) / std::sqrt(1.0 - taper(p) * taper(p));
    for (std::size_t i = 0; i < s.balls.size(); ++i) {
        const BallState& b = s.balls[i];
        if (static_cast<int>(i) == cue_ball || b.motion == Motion::pocketed) continue;
        const Vec2 d{b.r.x - cb.r.x, b.r.y - cb.r.y};
        const double behind = dot(d, cue.back);
        if (behind <= 0.0 || behind - kR - cue.contact > p.cue_length_m) continue;
        const double side = dot(d, right) - tip_x * kR;
        if (std::abs(side) >= widest) continue;
        theta = std::max(theta, ball_elevation(cue, behind, side, p));
    }
    return std::min(theta, p.max_elevation_rad);
}

}  // namespace

double min_cue_elevation(const TableState& s, int cue_ball, double aim_rad, const TableShape& t,
                         const PhysicsParams& p) {
    return min_elevation(s, cue_ball, std::cos(aim_rad), std::sin(aim_rad), 0.0, 0.0, t, p);
}

StrikeOutcome strike(const TableState& s, int cue_ball, const CueStrike& c, const TableShape& t,
                     const PhysicsParams& p, Rng& rng) {
    // Both draws happen on every strike, so the stream afterwards never depends on whether it miscued.
    const double miscue_pace = rng.uniform();
    const double miscue_angle = rng.uniform(-p.miscue_max_angle_rad, p.miscue_max_angle_rad);

    const double cos_aim = std::cos(c.aim_rad);
    const double sin_aim = std::sin(c.aim_rad);

    StrikeOutcome out;
    const double requested = std::clamp(c.elevation_rad, 0.0, p.max_elevation_rad);
    const double minimum = min_elevation(s, cue_ball, cos_aim, sin_aim, c.tip_x, c.tip_y, t, p);
    out.elevation_raised = minimum > requested;
    out.elevation_used_rad = std::max(requested, minimum);
    const double ce = std::cos(out.elevation_used_rad);
    const double se = std::sin(out.elevation_used_rad);

    // Marlow's instantaneous impulse along the cue's axis through the contact point Q (ported from
    // pooltool, Apache-2.0, physics/resolve/stick_ball/instantaneous_point cue_strike; derivation in
    // Alciatore TP A-30). Linear and angular momentum, with restitution e on the contact point's speed
    // along the cue, give J (1/M + 1/m + |Q x d|^2 / I) = (1 + e) V with |Q x d| = R hypot(tip_x,
    // tip_y), whatever the elevation. pooltool's numerator 2 V0 is the elastic case e = 1.
    const double cue_speed = std::clamp(c.speed_m_s, 0.0, p.max_cue_speed_m_s);
    const double push = (1.0 + p.cue_tip_restitution) * cue_speed;
    const double mass_ratio = p.ball_mass_kg / p.cue_mass_kg;

    if (std::hypot(c.tip_x, c.tip_y) > p.miscue_limit) {
        // The tip skids off: what impulse there is acts along the contact normal, through the
        // centre, so it leaves next to no spin.
        out.miscue = true;
        const double full_hit = push / (1.0 + mass_ratio) * ce;
        const double speed = full_hit * p.miscue_speed_factor * miscue_pace;
        const double heading = c.aim_rad + miscue_angle;
        out.v = {speed * std::cos(heading), speed * std::sin(heading), 0.0};
        out.squirt_rad = miscue_angle;
        return out;
    }

    const double offset2 = c.tip_x * c.tip_x + c.tip_y * c.tip_y;
    const double ball_speed = push / (1.0 + mass_ratio + 2.5 * offset2);

    // w = J (Q x d) / I. With d the cue's direction (down into the ball when elevated), Q x d is
    // R (tip_x up' - tip_y right), up' = aim sin(e) + z cos(e) being "up" square to the cue: the
    // elevation tilts the side-spin axis towards the aim, which is what swerves the ball.
    const Vec3 up_square{cos_aim * se, sin_aim * se, ce};
    const Vec3 right{sin_aim, -cos_aim, 0.0};
    out.w = (up_square * c.tip_x - right * c.tip_y) * (2.5 * ball_speed / kR);

    // Squirt from pooltool (Apache-2.0) physics/resolve/stick_ball/squirt.py get_squirt_angle
    // (Alciatore TP A-31), whose a is our -tip_x: the tip's end mass is knocked aside and the ball
    // goes the other way, left for right-hand side.
    const double a = c.tip_x;
    const double rest = 1.0 - a * a;
    const double squirt_sin = 2.5 * a * std::sqrt(rest);
    const double squirt_cos = 1.0 + p.ball_mass_kg / p.cue_end_mass_kg + 2.5 * rest;
    out.squirt_rad = std::atan2(squirt_sin, squirt_cos);

    // The impulse's vertical part goes into the cloth (spec 7.9).
    const double scale = ball_speed * ce / std::hypot(squirt_sin, squirt_cos);
    out.v = {scale * (cos_aim * squirt_cos - sin_aim * squirt_sin),
             scale * (sin_aim * squirt_cos + cos_aim * squirt_sin), 0.0};
    return out;
}

}  // namespace ofs::phys
