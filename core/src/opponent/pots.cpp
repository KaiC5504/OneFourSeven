// A port of the game's next-ball geometry (game/scripts/play/next_target.gd: _mouths, _pot_line,
// _room, _reaches_pocket) to the table in hand, so the opponent and the suggestion see the same
// pots. Only the geometry: the chance and the ranking are the error model's and the search's.
//
// One difference: a ball beside a cushion has no straight line into the corner at the cushion's
// end, and the suggestion offers none, but the opponent lists it along the cushion (rail_run()).

#include "ofs/opponent/pots.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "ofs/rules/engine.hpp"

namespace ofs::opponent {

namespace {

using rules::Ball;
using rules::BallOnTable;
using rules::OnKind;
using rules::Position;
using rules::Vec2;

constexpr double kR = table::kBallRadius;
constexpr double kTau = 6.28318530717958647692;
constexpr double kInf = std::numeric_limits<double>::infinity();
// next_target.gd's MAX_MIDDLE_APPROACH_DEG: off a middle pocket's axis by more than 50 degrees its
// mouth closes up from the side.
const double kCosMaxMiddle = std::cos(50.0 * kTau / 360.0);
constexpr double kMinTravel_m = 1.0e-9;
constexpr double kTouchEps = 1.0e-12;
constexpr double kAngleEps = 1.0e-9;
constexpr double kSegmentEps = 1.0e-9;
// A ball touching a cushion sits at exactly half the table less the radius, which rounding can
// push a hair past.
constexpr double kOutsideSlack_m = 1.0e-9;
// A cushion behind the cue ball hampers the stroke only when the cue points this much away from it.
constexpr double kAwayFromCushion = 0.3;
// A ball behind the cue ball hampers it when this close to the line of the cue, beyond a radius.
constexpr double kBesideCue_m = 0.015;
// Places in the D closer than this are the same place.
constexpr double kSamePlace_m = 0.01;

// A pot along a cushion, as the simulator plays it on this table (measured 2026-11-18: an object
// ball 0.12 to 3 m from the corner and from touching the cushion to where a straight line opens,
// the cue ball 0.15 to 1 m away at cuts to 70 degrees, cue speeds 1.5 to 6 m/s, top, screw and
// side; all four cushions alike). The ball is sent at the cushion's end: the point kRailEnd_m
// past the corner where a ball touching the cushion would be. Arriving within about 8 mm either
// side of that point at any pace (at a slow one from 30 mm on the cushion's side of it to 17 mm
// on the table's) it drops off the far jaw, and a ball that meets the cushion on the way runs
// along it there. Nothing goes into a middle pocket this way. The straight-line geometry above
// leaves these balls out: it lists a ball only from where the hole itself comes into view.
constexpr double kRailEnd_m = 0.03;
// What is claimed of that width, either side: about the tenth percentile of the windows measured
// (the middle one is twice it), less for a ball on the cushion, which can only be sent into the
// cushion or met off it. The full width from kRailFullWidthGap_m off the cushion.
constexpr double kRailHalfWidth_m = 0.0065;
constexpr double kRailHalfWidthTouching_m = 0.004;
constexpr double kRailFullWidthGap_m = 0.004;
// Nearer the corner than this the ball is in the jaws, and the measurements stop.
constexpr double kRailFrom_m = 0.12;
// The gap to the cushion as a share of the run to its end: beyond it the ball has a straight line.
constexpr double kRailSlope = 0.15;
// The widest cut measured.
const double kCosRailMaxCut = std::cos(70.0 * kTau / 360.0);

// One side of a pocket's mouth as a ball's centre meets it, looking into the pocket.
struct MouthSide {
    // Where the drop circle meets the jaw or facing a ball's radius out; none when it doesn't.
    bool met = false;
    double px = 0.0;
    double py = 0.0;
    // The jaw, its radius grown by a ball's (0 without one), and unit vectors from its centre to
    // the ends of the arc a ball can touch before the hole, anticlockwise.
    double jx = 0.0;
    double jy = 0.0;
    double rho = 0.0;
    double start_c = 0.0;
    double start_s = 0.0;
    double end_c = 0.0;
    double end_s = 0.0;
};

struct Mouth {
    double dcx = 0.0;
    double dcy = 0.0;
    double drop_r = 0.0;
    double ax = 0.0;  // outward axis
    double ay = 0.0;
    bool corner = true;
    MouthSide side[2];  // left looking into the pocket, then right
};

struct PotLine {
    double ux = 0.0;
    double uy = 0.0;
    double length = 0.0;    // to the drop circle
    double half_rad = 0.0;  // half the width of the directions that go in
};

// Godot's fposmod, so the arc tests match next_target.gd's to the last bit.
double fposmod(double x, double y) {
    double v = std::fmod(x, y);
    if (v < 0.0) v += y;
    return v + 0.0;
}

double signf(double x) { return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0); }

// The shape's lines carry no kind; the facings are the ones that run beyond the playing area,
// into a pocket's throat.
bool is_facing(const phys::LineCushion& line) {
    constexpr double kEps = 1.0e-6;
    const double hx = table::kPlayingLength / 2.0 + kEps;
    const double hy = table::kPlayingWidth / 2.0 + kEps;
    for (const table::Vec2& q : {line.p1, line.p2}) {
        if (std::abs(q.x) > hx || std::abs(q.y) > hy) return true;
    }
    return false;
}

bool meets(Mouth& m, double x, double y, double nearest[2]) {
    const double rx = x - m.dcx;
    const double ry = y - m.dcy;
    const double left = m.ax * ry - m.ay * rx;
    const int side = left > 0.0 ? 0 : 1;
    const double round_from_front = std::abs(std::atan2(left, -(m.ax * rx + m.ay * ry)));
    if (round_from_front >= nearest[side]) return false;
    nearest[side] = round_from_front;
    m.side[side].met = true;
    m.side[side].px = x;
    m.side[side].py = y;
    return true;
}

std::vector<Mouth> mouths(const phys::TableShape& t) {
    std::vector<Mouth> out(t.pockets.size());
    for (std::size_t p = 0; p < t.pockets.size(); ++p) {
        const phys::Pocket& pocket = t.pockets[p];
        Mouth& m = out[p];
        m.dcx = pocket.drop_centre.x;
        m.dcy = pocket.drop_centre.y;
        m.drop_r = pocket.drop_radius;
        m.corner = pocket.corner;
        m.ax = m.corner ? signf(m.dcx) * std::sqrt(0.5) : 0.0;
        m.ay = m.corner ? signf(m.dcy) * std::sqrt(0.5) : signf(m.dcy);
        double nearest[2] = {kInf, kInf};
        bool met_on_jaw[2] = {false, false};
        double start[2] = {0.0, 0.0};
        double sweep[2] = {0.0, 0.0};
        for (const phys::ArcCushion& arc : t.arcs) {
            if (arc.pocket != int(p)) continue;
            const double jx = arc.centre.x;
            const double jy = arc.centre.y;
            const double rho = arc.radius + kR;
            const int b = m.ax * (jy - m.dcy) - m.ay * (jx - m.dcx) > 0.0 ? 0 : 1;
            m.side[b].jx = jx;
            m.side[b].jy = jy;
            m.side[b].rho = rho;
            start[b] = arc.angle_start;
            sweep[b] = arc.angle_end - arc.angle_start;
            const double apart = std::sqrt((jx - m.dcx) * (jx - m.dcx) + (jy - m.dcy) * (jy - m.dcy));
            if (apart < kMinTravel_m || apart > rho + m.drop_r || apart < std::abs(rho - m.drop_r)) continue;
            const double along = (apart * apart + m.drop_r * m.drop_r - rho * rho) / (2.0 * apart);
            const double across = std::sqrt(std::max(m.drop_r * m.drop_r - along * along, 0.0));
            for (const double sign : {1.0, -1.0}) {
                const double x = m.dcx + (along * (jx - m.dcx) - sign * across * (jy - m.dcy)) / apart;
                const double y = m.dcy + (along * (jy - m.dcy) + sign * across * (jx - m.dcx)) / apart;
                const double past = fposmod(std::atan2(y - jy, x - jx) - arc.angle_start, kTau);
                const double arc_sweep = arc.angle_end - arc.angle_start;
                if ((past <= arc_sweep + kAngleEps || past >= kTau - kAngleEps) && meets(m, x, y, nearest)) {
                    met_on_jaw[m.ax * (y - m.dcy) - m.ay * (x - m.dcx) > 0.0 ? 0 : 1] = true;
                }
            }
        }
        for (const phys::LineCushion& line : t.lines) {
            if (!is_facing(line)) continue;
            const double x1 = line.p1.x + line.normal.x * kR;
            const double y1 = line.p1.y + line.normal.y * kR;
            const double ex = line.p2.x - line.p1.x;
            const double ey = line.p2.y - line.p1.y;
            const double e2 = ex * ex + ey * ey;
            const double half_b = (x1 - m.dcx) * ex + (y1 - m.dcy) * ey;
            const double disc =
                half_b * half_b - e2 * ((x1 - m.dcx) * (x1 - m.dcx) + (y1 - m.dcy) * (y1 - m.dcy) - m.drop_r * m.drop_r);
            if (e2 <= 0.0 || disc < 0.0) continue;
            for (const double sign : {1.0, -1.0}) {
                const double s = (-half_b + sign * std::sqrt(disc)) / e2;
                if (s < -kSegmentEps || s > 1.0 + kSegmentEps) continue;
                const double x = x1 + s * ex;
                const double y = y1 + s * ey;
                if (meets(m, x, y, nearest)) met_on_jaw[m.ax * (y - m.dcy) - m.ay * (x - m.dcx) > 0.0 ? 0 : 1] = false;
            }
        }
        // A jaw can only be touched before the hole: its arc stops where the drop circle crosses it.
        for (int b = 0; b < 2; ++b) {
            MouthSide& side = m.side[b];
            if (side.rho <= 0.0) continue;
            double from = start[b];
            double turn = sweep[b];
            if (met_on_jaw[b]) {
                const double at = std::clamp(
                    fposmod(std::atan2(side.py - side.jy, side.px - side.jx) - from + kAngleEps, kTau) - kAngleEps, 0.0,
                    turn);
                const double sx = side.jx + side.rho * std::cos(from) - m.dcx;
                const double sy = side.jy + side.rho * std::sin(from) - m.dcy;
                if (sx * sx + sy * sy > m.drop_r * m.drop_r) {
                    turn = at;
                } else {
                    from += at;
                    turn -= at;
                }
            }
            side.start_c = std::cos(from);
            side.start_s = std::sin(from);
            side.end_c = std::cos(from + turn);
            side.end_s = std::sin(from + turn);
        }
    }
    return out;
}

// The middle of the directions along which a ball at o reaches the reachable part of the drop
// circle without touching a jaw first, so it has the same room for error either side.
bool pot_line(const Mouth& m, double ox, double oy, PotLine& line) {
    const double wx = m.dcx - ox;
    const double wy = m.dcy - oy;
    const double dist2 = wx * wx + wy * wy;
    if (dist2 <= m.drop_r * m.drop_r) return false;
    const double dist = std::sqrt(dist2);
    const double ex = wx / dist;
    const double ey = wy / dist;
    // Directions are kept as the tangent of their angle from the line to the drop centre, positive
    // to the left: they all lie within a quarter turn of it.
    double hi = m.drop_r / std::sqrt(dist2 - m.drop_r * m.drop_r);
    double lo = -hi;
    for (int b = 0; b < 2; ++b) {
        const MouthSide& side = m.side[b];
        const double s = b == 0 ? 1.0 : -1.0;
        double bound = b == 0 ? hi : lo;
        if (side.met) {
            const double px = side.px;
            const double py = side.py;
            if ((px - m.dcx) * (ox - m.dcx) + (py - m.dcy) * (oy - m.dcy) >= m.drop_r * m.drop_r) {
                // The meeting point faces the ball: nothing of the hole shows beyond it.
                bound = (ex * (py - oy) - ey * (px - ox)) / (ex * (px - ox) + ey * (py - oy));
            } else if (s * (ex * (py - m.dcy) - ey * (px - m.dcx)) <= 0.0) {
                // It is round the back on the other side: the ball sees none of the reachable part.
                return false;
            }
        }
        if (side.rho > 0.0) {
            const double jx = side.jx - ox;
            const double jy = side.jy - oy;
            const double away = std::sqrt(jx * jx + jy * jy);
            const double sin_half = std::min(side.rho / away, 1.0);
            const double cos_half = std::sqrt(1.0 - sin_half * sin_half);
            // The line that just clears the jaw on the hole's side of it, and where it touches.
            const double tx = (jx * cos_half + s * jy * sin_half) / away;
            const double ty = (jy * cos_half - s * jx * sin_half) / away;
            const double qx = tx * away * cos_half - jx;
            const double qy = ty * away * cos_half - jy;
            if (side.start_c * qy - side.start_s * qx >= -kAngleEps * side.rho &&
                qx * side.end_s - qy * side.end_c >= -kAngleEps * side.rho) {
                const double ahead = ex * tx + ey * ty;
                const double left = ex * ty - ey * tx;
                if (ahead > 0.0) {
                    bound = b == 0 ? std::min(bound, left / ahead) : std::max(bound, left / ahead);
                } else if (s * left <= 0.0) {
                    // It clears the jaw only by turning away from the hole.
                    return false;
                }
            }
        }
        if (b == 0) {
            hi = bound;
        } else {
            lo = bound;
        }
    }
    if (hi - lo <= kAngleEps) return false;
    const double lo_scale = 1.0 / std::sqrt(1.0 + lo * lo);
    const double hi_scale = 1.0 / std::sqrt(1.0 + hi * hi);
    double mx = lo_scale + hi_scale;
    double my = lo * lo_scale + hi * hi_scale;
    const double mm = std::sqrt(mx * mx + my * my);
    mx /= mm;
    my /= mm;
    line.ux = ex * mx - ey * my;
    line.uy = ey * mx + ex * my;
    line.length = dist * mx - std::sqrt(std::max(m.drop_r * m.drop_r - dist2 * my * my, 0.0));
    line.half_rad = 0.5 * std::atan2((hi - lo) * lo_scale * hi_scale, (1.0 + lo * hi) * lo_scale * hi_scale);
    return true;
}

// How far, in radians at a, the path from a to b can turn before a ball other than skip_a and
// skip_b is in its way. Infinite with nothing near; -1 when a ball is already in the way.
double room(const std::vector<BallOnTable>& balls, int skip_a, int skip_b, double ax, double ay, double bx, double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length2 = dx * dx + dy * dy;
    const double length = std::sqrt(length2);
    double out = kInf;
    for (std::size_t j = 0; j < balls.size(); ++j) {
        if (int(j) == skip_a || int(j) == skip_b) continue;
        const double wx = balls[j].pos.x - ax;
        const double wy = balls[j].pos.y - ay;
        const double s = length2 > 0.0 ? std::clamp((wx * dx + wy * dy) / length2, 0.0, 1.0) : 0.0;
        const double ex = wx - s * dx;
        const double ey = wy - s * dy;
        const double gap2 = ex * ex + ey * ey;
        if (gap2 < 4.0 * kR * kR) return -1.0;
        // A ball level with a or behind it is never turned into.
        if (s > 0.0) out = std::min(out, (std::sqrt(gap2) - 2.0 * kR) / (s * length));
    }
    return out;
}

// Distance along the unit ray d until its origin, at w from a circle's centre, reaches `radius`.
double ray_circle(double wx, double wy, double dx, double dy, double radius) {
    const double b = wx * dx + wy * dy;
    const double c = wx * wx + wy * wy - radius * radius;
    if (c <= kTouchEps) return b < 0.0 ? 0.0 : kInf;
    const double disc = b * b - c;
    if (b >= 0.0 || disc < 0.0) return kInf;
    return -b - std::sqrt(disc);
}

// A ball sent from o along u reaches the drop circle before any cushion line or jaw.
bool reaches_pocket(const phys::TableShape& t, const Mouth& m, double ox, double oy, double ux, double uy) {
    const double to_pocket = ray_circle(ox - m.dcx, oy - m.dcy, ux, uy, m.drop_r);
    if (to_pocket == kInf) return false;
    for (const phys::LineCushion& line : t.lines) {
        const double heading = ux * line.normal.x + uy * line.normal.y;
        if (heading >= 0.0) continue;
        const double height = (ox - line.p1.x) * line.normal.x + (oy - line.p1.y) * line.normal.y;
        if (height < 0.0) continue;
        const double at = std::max((height - kR) / -heading, 0.0);
        if (at >= to_pocket) continue;
        const double ex = line.p2.x - line.p1.x;
        const double ey = line.p2.y - line.p1.y;
        const double s = ((ox + at * ux - line.p1.x) * ex + (oy + at * uy - line.p1.y) * ey) / (ex * ex + ey * ey);
        if (s >= -kSegmentEps && s <= 1.0 + kSegmentEps) return false;
    }
    for (const phys::ArcCushion& arc : t.arcs) {
        const double at = ray_circle(ox - arc.centre.x, oy - arc.centre.y, ux, uy, arc.radius + kR);
        if (at >= to_pocket) continue;
        const double past =
            fposmod(std::atan2(oy + at * uy - arc.centre.y, ox + at * ux - arc.centre.x) - arc.angle_start, kTau);
        if (past <= arc.angle_end - arc.angle_start + kAngleEps || past >= kTau - kAngleEps) return false;
    }
    return true;
}

bool ghost_on_cloth(double gx, double gy) {
    return std::abs(gx) <= table::kPlayingLength / 2.0 - kR + kOutsideSlack_m &&
           std::abs(gy) <= table::kPlayingWidth / 2.0 - kR + kOutsideSlack_m;
}

// The object ball's run into the mouth: a line in, room past every ball but `place` and the cue
// ball, and nothing touched before the hole. `straight` says whether it has such a line at all,
// other balls aside.
bool clear_run(const Position& p, int place, const phys::TableShape& t, const Mouth& m, PotLine& line,
               double& object_room, bool& straight) {
    const Vec2 o = p.balls[std::size_t(place)].pos;
    straight = false;
    if (!pot_line(m, o.x, o.y, line)) return false;
    straight = reaches_pocket(t, m, o.x, o.y, line.ux, line.uy);
    if (!straight) return false;
    object_room = room(p.balls, p.cue_ball, place, o.x, o.y, o.x + line.ux * line.length, o.y + line.uy * line.length);
    return object_room > 0.0;
}

// The run of a ball with no straight line into a corner pocket along a cushion that ends there:
// to the cushion's end, past every ball but `place` and the cue ball. `line` is filled as
// pot_line() fills it, its half width the measured one as the ball sees it.
bool rail_run(const Position& p, int place, const Mouth& m, PotLine& line, double& object_room) {
    if (!m.corner) return false;
    const Vec2 o = p.balls[std::size_t(place)].pos;
    const double sx = signf(m.dcx);
    const double sy = signf(m.dcy);
    const double cx = sx * table::kPlayingLength / 2.0;
    const double cy = sy * table::kPlayingWidth / 2.0;
    // The side cushion, then the top or baulk cushion: the way along it to the corner, and the way
    // off it into the table.
    const double cushions[2][4] = {{sx, 0.0, 0.0, -sy}, {0.0, sy, -sx, 0.0}};
    for (const auto& c : cushions) {
        const double run = (cx - o.x) * c[0] + (cy - o.y) * c[1];
        const double gap = (o.x - cx) * c[2] + (o.y - cy) * c[3] - kR;
        if (run < kRailFrom_m || gap < -kOutsideSlack_m || gap > kRailSlope * (run + kRailEnd_m)) continue;
        const double dx = cx + c[0] * kRailEnd_m + c[2] * kR - o.x;
        const double dy = cy + c[1] * kRailEnd_m + c[3] * kR - o.y;
        const double length = std::sqrt(dx * dx + dy * dy);
        object_room = room(p.balls, p.cue_ball, place, o.x, o.y, o.x + dx, o.y + dy);
        if (!(object_room > 0.0)) continue;
        const double share = std::clamp(gap / kRailFullWidthGap_m, 0.0, 1.0);
        line.ux = dx / length;
        line.uy = dy / length;
        line.length = length;
        line.half_rad = (kRailHalfWidthTouching_m + (kRailHalfWidth_m - kRailHalfWidthTouching_m) * share) / length;
        return true;
    }
    return false;
}

// A cushion nose within kAwkward_m behind the cue ball, or a ball close behind it on the cue's line.
bool awkward(const Position& p, Vec2 cue, double dx, double dy) {
    const double half_x = table::kPlayingLength / 2.0;
    const double half_y = table::kPlayingWidth / 2.0;
    const struct {
        double room;
        double nx;
        double ny;
    } cushions[] = {
        {half_x - cue.x, -1.0, 0.0},
        {cue.x + half_x, 1.0, 0.0},
        {half_y - cue.y, 0.0, -1.0},
        {cue.y + half_y, 0.0, 1.0},
    };
    for (const auto& c : cushions) {
        if (c.room < kAwkward_m && dx * c.nx + dy * c.ny > kAwayFromCushion) return true;
    }
    for (std::size_t j = 0; j < p.balls.size(); ++j) {
        if (int(j) == p.cue_ball) continue;
        const double wx = p.balls[j].pos.x - cue.x;
        const double wy = p.balls[j].pos.y - cue.y;
        const double behind = -(wx * dx + wy * dy);
        if (behind > 0.0 && behind <= kAwkward_m + kR && std::abs(wx * dy - wy * dx) <= kR + kBesideCue_m) return true;
    }
    return false;
}

struct Target {
    int place = -1;
    int value = 1;
    rules::ShotInput input;
};

std::vector<Target> targets(const Position& p) {
    std::vector<Target> out;
    const std::vector<int> on = rules::places_on(p);
    for (const int place : on) {
        const Ball ball = p.balls[std::size_t(place)].ball;
        Target t;
        t.place = place;
        t.value = table::ball_value(ball);
        if (p.on.kind == OnKind::any_colour) t.input = {true, ball};
        out.push_back(t);
    }
    // A colour of choice is already any colour: a free ball adds nothing to it (S3.12).
    if (p.free_ball && p.on.kind != OnKind::any_colour) {
        const int worth = p.on.kind == OnKind::red ? 1 : table::ball_value(p.on.colour);
        for (std::size_t i = 0; i < p.balls.size(); ++i) {
            if (int(i) == p.cue_ball || std::find(on.begin(), on.end(), int(i)) != on.end()) continue;
            Target t;
            t.place = int(i);
            t.value = worth;
            t.input = {true, p.balls[i].ball};
            out.push_back(t);
        }
        std::sort(out.begin(), out.end(), [](const Target& a, const Target& b) { return a.place < b.place; });
    }
    return out;
}

void add_pots(const Position& p, Vec2 cue, const Env& env, const std::vector<Mouth>& m, double max_cut_rad,
              std::vector<Pot>& out) {
    const double cos_max_cut = std::cos(max_cut_rad);
    for (const Target& target : targets(p)) {
        const Vec2 o = p.balls[std::size_t(target.place)].pos;
        for (std::size_t k = 0; k < m.size(); ++k) {
            PotLine line;
            double object_room = 0.0;
            bool straight = false;
            bool along = false;
            if (!clear_run(p, target.place, env.table, m[k], line, object_room, straight)) {
                if (straight || !rail_run(p, target.place, m[k], line, object_room)) continue;
                along = true;
            }
            if (!m[k].corner && line.ux * m[k].ax + line.uy * m[k].ay < kCosMaxMiddle) continue;
            const double gx = o.x - 2.0 * kR * line.ux;
            const double gy = o.y - 2.0 * kR * line.uy;
            if (!ghost_on_cloth(gx, gy)) continue;
            double vx = gx - cue.x;
            double vy = gy - cue.y;
            const double travel = std::sqrt(vx * vx + vy * vy);
            const double cos_cut = travel > kMinTravel_m ? (vx * line.ux + vy * line.uy) / travel : 1.0;
            if (cos_cut < cos_max_cut || (along && cos_cut < kCosRailMaxCut)) continue;
            const double cue_room = room(p.balls, p.cue_ball, target.place, cue.x, cue.y, gx, gy);
            if (cue_room < 0.0) continue;
            // The pocket's half width seen from the object ball, brought back through the contact
            // (2R cos(cut) of ghost-ball travel per radian) to the cue ball.
            const double gearing = 2.0 * kR * cos_cut / std::max(travel, 2.0 * kR);
            const double margin = std::min(std::min(line.half_rad, object_room) * gearing, cue_room);
            if (!(margin > 0.0)) continue;
            if (travel < kMinTravel_m) {
                vx = line.ux;
                vy = line.uy;
            }
            Pot pot;
            pot.target = target.place;
            pot.pocket = int(k);
            pot.cue = cue;
            pot.aim_point = {o.x + line.ux * line.length, o.y + line.uy * line.length};
            pot.ghost = {gx, gy};
            pot.aim_rad = std::atan2(vy, vx);
            pot.cut_rad = std::atan2(std::abs(vx * line.uy - vy * line.ux), vx * line.ux + vy * line.uy);
            pot.cue_travel_m = travel;
            pot.object_travel_m = line.length;
            pot.margin_rad = margin;
            const double norm = std::sqrt(vx * vx + vy * vy);
            pot.awkward = awkward(p, cue, vx / norm, vy / norm);
            pot.along_cushion = along;
            pot.value = target.value;
            pot.input = target.input;
            out.push_back(pot);
        }
    }
}

void order(std::vector<Pot>& pots) {
    std::stable_sort(pots.begin(), pots.end(), [](const Pot& a, const Pot& b) {
        if (a.margin_rad != b.margin_rad) return a.margin_rad > b.margin_rad;
        if (a.target != b.target) return a.target < b.target;
        return a.pocket < b.pocket;
    });
}

std::vector<int> open_pockets(const Position& p, int place, const Env& env, const std::vector<Mouth>& m) {
    std::vector<int> out;
    for (std::size_t k = 0; k < m.size(); ++k) {
        PotLine line;
        double object_room = 0.0;
        bool straight = false;
        if (clear_run(p, place, env.table, m[k], line, object_room, straight)) out.push_back(int(k));
    }
    return out;
}

}  // namespace

const char* band(const Pot& pot) {
    const double length = pot.cue_travel_m + pot.object_travel_m;
    if (length < kMediumFrom_m) return "short";
    return length < kLongFrom_m ? "medium" : "long";
}

std::vector<Pot> pots_from(const rules::Position& p, rules::Vec2 cue, const Env& env, double max_cut_rad) {
    std::vector<Pot> out;
    add_pots(p, cue, env, mouths(env.table), max_cut_rad, out);
    order(out);
    return out;
}

std::vector<Pot> pots(const rules::FrameState& s, const Env& env, double max_cut_rad) {
    const std::vector<Mouth> m = mouths(env.table);
    std::vector<Pot> out;
    if (s.cue_in_hand) {
        for (const Vec2 place : cue_placements(s, env, kPlacements)) add_pots(s, place, env, m, max_cut_rad, out);
    } else {
        add_pots(s, s.balls.at(std::size_t(s.cue_ball)).pos, env, m, max_cut_rad, out);
    }
    order(out);
    return out;
}

std::vector<rules::Vec2> cue_placements(const rules::Position& p, const Env& env, int count) {
    const Vec2 listed = p.balls.at(std::size_t(p.cue_ball)).pos;
    const double b = table::baulk_line_x();
    const Vec2 candidates[] = {
        listed,          {b - 0.03, 0.0},  {b - 0.03, 0.10},  {b - 0.03, -0.10}, {b - 0.03, 0.20},
        {b - 0.03, -0.20}, {b - 0.03, 0.27}, {b - 0.03, -0.27}, {b - 0.15, 0.12},  {b - 0.15, -0.12},
        {b - 0.25, 0.0},
    };
    std::vector<Vec2> out;
    const std::size_t wanted = std::size_t(std::max(count, 1));
    for (const Vec2 c : candidates) {
        if (out.size() >= wanted) break;
        if (!env.oracle.cue_placement_legal(p.balls, p.cue_ball, c)) continue;
        const bool seen = std::any_of(out.begin(), out.end(), [&](Vec2 q) {
            return (q.x - c.x) * (q.x - c.x) + (q.y - c.y) * (q.y - c.y) < kSamePlace_m * kSamePlace_m;
        });
        if (!seen) out.push_back(c);
    }
    if (out.empty()) out.push_back(env.oracle.nearest_cue_placement(p.balls, p.cue_ball, listed));
    return out;
}

std::vector<int> pockets_open_to(const rules::Position& p, int place, const Env& env) {
    return open_pockets(p, place, env, mouths(env.table));
}

int open_reds(const rules::Position& p, const Env& env) {
    const std::vector<Mouth> m = mouths(env.table);
    int count = 0;
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        if (int(i) == p.cue_ball || p.balls[i].ball != Ball::red) continue;
        if (!open_pockets(p, int(i), env, m).empty()) ++count;
    }
    return count;
}

}  // namespace ofs::opponent
