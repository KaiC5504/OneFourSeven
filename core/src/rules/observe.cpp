#include "ofs/rules/facts.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string>

#include "ofs/math/roots.hpp"
#include "ofs/physics/motion.hpp"
#include "ofs/rules/rules_log.hpp"

// Distances over a stroke are solved, not sampled: between events every centre moves on a
// quadratic (types.hpp), so the closest two balls come is at an end of such a stretch or at a root
// of a cubic, and a path's length has a closed form.

namespace ofs::rules {

namespace {

using phys::EventKind;
using phys::Motion;
using phys::Segment;
using phys::ShotEvent;
using phys::ShotRecord;

constexpr double kDiameter = table::kBallDiameter;
constexpr double kInf = std::numeric_limits<double>::infinity();
// facts.hpp: how far from its place in the position a ball may start in the record.
constexpr double kStartTolerance_m = 1e-9;
// facts.hpp: a ball was played at a pocket only if it got this much nearer to it than it began.
constexpr double kPotApproach_m = 0.010;

Vec2 xy(const Vec3& v) { return {v.x, v.y}; }
Vec2 minus(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
double length(Vec2 a) { return std::sqrt(dot(a, a)); }

std::string number(double v) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.6g", v);
    return buffer;
}

// A ball's centre from t0 to t1: p + v s + a s^2 with s = t - t0.
struct Piece {
    double t0 = 0.0;
    double t1 = 0.0;
    Vec2 p;
    Vec2 v;
    Vec2 a;

    Vec2 at(double s) const { return {p.x + (v.x + a.x * s) * s, p.y + (v.y + a.y * s) * s}; }
    Vec2 velocity(double s) const { return {v.x + 2.0 * a.x * s, v.y + 2.0 * a.y * s}; }
};

// The ball's path from t = 0 until the shot ends or the ball is potted.
void path_of(const ShotRecord& r, std::size_t ball, std::vector<Piece>& out) {
    out.clear();
    const std::vector<Segment>& segs = r.segments[ball];
    for (std::size_t k = 0; k < segs.size(); ++k) {
        const Segment& s = segs[k];
        if (s.start.motion == Motion::pocketed) break;
        const double until = std::max(s.t0, k + 1 < segs.size() ? segs[k + 1].t0 : r.t_end);
        if (s.start.motion != Motion::sliding && s.start.motion != Motion::rolling) {
            out.push_back({s.t0, until, xy(s.start.r), {}, {}});
            continue;
        }
        const phys::Trajectory tr = phys::trajectory(s.start, r.params, s.nap);
        const double stop = s.t0 + std::max(tr.duration, 0.0);
        if (!(stop < until)) {
            out.push_back({s.t0, until, xy(tr.c0), xy(tr.c1), xy(tr.c2)});
            continue;
        }
        // The simulator may start the next segment a hair after this one's transition; until then
        // the ball stays where the transition left it.
        out.push_back({s.t0, stop, xy(tr.c0), xy(tr.c1), xy(tr.c2)});
        out.push_back({stop, until, xy(tr.at(stop - s.t0)), {}, {}});
    }
}

// The centre and velocity a path has coming into time t, before any event at t.
bool entering(const std::vector<Piece>& path, double t, Vec2& at, Vec2& velocity) {
    if (path.empty()) return false;
    at = path.front().p;
    velocity = {};
    for (const Piece& piece : path) {
        if (!(piece.t0 < t)) break;
        const double s = std::min(t, piece.t1) - piece.t0;
        at = piece.at(s);
        velocity = piece.velocity(s);
    }
    return true;
}

// The length of p + v s + a s^2 from s = 0 to T: the integral of |v + 2 a s|.
double arc_length(Vec2 v, Vec2 a, double T) {
    if (!(T > 0.0)) return 0.0;
    const double a2 = dot(a, a);
    const double speed2 = dot(v, v);
    if (a2 == 0.0) return std::sqrt(speed2) * T;
    // |v + 2 a s|^2 = 4 a2 ((s + h)^2 + k2)
    const double h = dot(v, a) / (2.0 * a2);
    const double k2 = std::max(speed2 / (4.0 * a2) - h * h, 0.0);
    const auto primitive = [k2](double u) {
        const double spread = k2 > 0.0 ? k2 * std::asinh(u / std::sqrt(k2)) : 0.0;
        return 0.5 * (u * std::sqrt(u * u + k2) + spread);
    };
    return 2.0 * std::sqrt(a2) * (primitive(T + h) - primitive(h));
}

// The smallest squared distance between the two centres from time `from` to `to`, both within the
// pieces. An interval that cannot come under `best`, squared too, is not solved.
double closest2_within(const Piece& a, const Piece& b, double from, double to, double best) {
    const double sa = from - a.t0;
    const double sb = from - b.t0;
    const Vec2 p = minus(a.at(sa), b.at(sb));
    const Vec2 q = minus(a.velocity(sa), b.velocity(sb));
    const Vec2 c = minus(a.a, b.a);
    double least = dot(p, p);
    const double span = to - from;
    const double q2 = dot(q, q);
    const double c2 = dot(c, c);
    if (!(span > 0.0) || (q2 == 0.0 && c2 == 0.0)) return least;
    const double clear = std::sqrt(least) - (std::sqrt(q2) + std::sqrt(c2) * span) * span;
    if (clear > 0.0 && clear * clear >= best) return least;

    const auto consider = [&](double s) {
        const Vec2 d{p.x + (q.x + c.x * s) * s, p.y + (q.y + c.y * s) * s};
        least = std::min(least, dot(d, d));
    };
    consider(span);
    // The distance turns where d . d' = 0, d = p + q s + c s^2.
    double roots[3] = {};
    const int n = math::solve_cubic(2.0 * c2, 3.0 * dot(q, c), q2 + 2.0 * dot(p, c), dot(p, q), roots);
    for (int k = 0; k < n && k < 3; ++k) {
        if (roots[k] > 0.0 && roots[k] < span) consider(roots[k]);
    }
    return least;
}

// The smallest distance between two centres over the time both paths cover. It stops looking once
// that is `enough` or less.
double closest(const std::vector<Piece>& a, const std::vector<Piece>& b, double enough) {
    double best = kInf;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        const double from = std::max(a[i].t0, b[j].t0);
        const double to = std::min(a[i].t1, b[j].t1);
        if (from <= to) {
            best = std::min(best, closest2_within(a[i], b[j], from, to, best));
            if (best <= enough * enough) break;
        }
        if (a[i].t1 <= b[j].t1) {
            ++i;
        } else {
            ++j;
        }
    }
    return std::sqrt(best);
}

// The angle between the cue ball's line and the line of centres: 0 is a full-ball contact.
double cut_angle(Vec2 velocity, Vec2 centres) {
    const double scale = length(velocity) * length(centres);
    if (!(scale > 0.0)) return 0.0;
    return std::acos(std::clamp(dot(velocity, centres) / scale, -1.0, 1.0));
}

// How far along its path the ball has gone by time t.
double travelled(const std::vector<Piece>& path, double t) {
    double sum = 0.0;
    for (const Piece& piece : path) {
        if (!(piece.t0 < t)) break;
        sum += arc_length(piece.v, piece.a, std::min(t, piece.t1) - piece.t0);
    }
    return sum;
}

// The ball came out of the events at time t in motion. The record keeps one segment per ball per
// instant, the one after all its events there.
bool set_moving(const ShotRecord& r, std::size_t ball, double t) {
    const Segment* after = nullptr;
    for (const Segment& s : r.segments[ball]) {
        if (!(s.t0 <= t)) break;
        after = &s;
    }
    return after != nullptr && after->start.motion != Motion::stationary;
}

bool in_range(int index, std::size_t size) { return index >= 0 && static_cast<std::size_t>(index) < size; }

// Why the record cannot be a stroke played from `pre` on `table`; empty when it can.
std::string mismatch(const Position& pre, const ShotRecord& r, const phys::TableShape& table) {
    const std::size_t n = pre.balls.size();
    if (!in_range(pre.cue_ball, n)) {
        return "the position's cue ball is place " + std::to_string(pre.cue_ball) + " of " + std::to_string(n);
    }
    const BallOnTable& cue = pre.balls[static_cast<std::size_t>(pre.cue_ball)];
    if (cue.ball != Ball::cue) {
        return "place " + std::to_string(pre.cue_ball) + ", the position's cue ball, holds a " +
               std::string(table::ball_name(cue.ball));
    }
    if (r.segments.size() != n || r.final_state.balls.size() != n) {
        return "the position has " + std::to_string(n) + " balls, the record paths for " +
               std::to_string(r.segments.size()) + " and resting places for " +
               std::to_string(r.final_state.balls.size());
    }
    const auto ball = [](std::size_t i) { return "ball " + std::to_string(i); };
    for (std::size_t i = 0; i < n; ++i) {
        if (r.segments[i].empty()) return ball(i) + " has no path in the record";
        const phys::BallState& start = r.segments[i].front().start;
        if (start.ball != pre.balls[i].ball) {
            return ball(i) + " is a " + std::string(table::ball_name(pre.balls[i].ball)) + " in the position and a " +
                   std::string(table::ball_name(start.ball)) + " in the record";
        }
        if (start.motion == Motion::pocketed) return ball(i) + " starts the record in a pocket";
        const double off = length(minus(xy(start.r), pre.balls[i].pos));
        if (!(off <= kStartTolerance_m)) {
            return ball(i) + " starts the record " + number(off) + " m from its place in the position";
        }
    }
    const auto event = [&r](std::size_t k) {
        return "event " + std::to_string(k) + " (" + std::string(phys::event_kind_name(r.events[k].kind)) + ")";
    };
    for (std::size_t k = 0; k < r.events.size(); ++k) {
        const ShotEvent& e = r.events[k];
        if (e.kind == EventKind::guard) continue;
        if (!in_range(e.a, n)) return event(k) + " names ball " + std::to_string(e.a) + " of " + std::to_string(n);
        if (e.kind == EventKind::ball_ball && (!in_range(e.b, n) || e.b == e.a)) {
            return event(k) + " pairs ball " + std::to_string(e.a) + " with ball " + std::to_string(e.b) + " of " +
                   std::to_string(n);
        }
        if (e.kind == EventKind::pocket && !in_range(e.body, table.pockets.size())) {
            return event(k) + " names pocket " + std::to_string(e.body) + " of " + std::to_string(table.pockets.size());
        }
        if (e.kind == EventKind::strike && e.a != pre.cue_ball) {
            return "the record's strike is on ball " + std::to_string(e.a) + ", the position's cue ball is place " +
                   std::to_string(pre.cue_ball);
        }
    }
    return {};
}

}  // namespace

ShotFacts observe(const Position& pre, const ShotRecord& record, const phys::TableShape& table,
                  const RefParams& ref) {
    const std::size_t n = pre.balls.size();
    ShotFacts f;
    f.rest.reserve(n);
    for (const BallOnTable& b : pre.balls) f.rest.push_back(b.pos);
    f.closest_gap_m.assign(n, 0.0);

    const std::string differs = mismatch(pre, record, table);
    if (!differs.empty()) {
        log_invalid("observe", differs);
        f.anomaly = true;
        // Nothing can be read from it: the balls as the position has them, nothing hit.
        if (in_range(pre.cue_ball, n)) {
            const Vec2 cue_pos = pre.balls[static_cast<std::size_t>(pre.cue_ball)].pos;
            for (std::size_t i = 0; i < n; ++i) {
                f.closest_gap_m[i] = std::max(length(minus(pre.balls[i].pos, cue_pos)) - kDiameter, 0.0);
            }
            f.closest_gap_m[static_cast<std::size_t>(pre.cue_ball)] = 0.0;
        }
        return f;
    }
    const std::size_t cue = static_cast<std::size_t>(pre.cue_ball);
    const Vec2 cue_start = pre.balls[cue].pos;

    for (const phys::Anomaly& a : record.anomalies) {
        if (a.kind == "event_guard" || a.kind == "zero_time_chain" || a.kind == "time_cap") f.anomaly = true;
    }

    std::vector<double> met(n, kInf);  // when the cue ball first touched each ball
    std::vector<int> pocket_of(n, -1);
    bool hit = false;
    for (const ShotEvent& e : record.events) {
        if (e.kind == EventKind::ball_ball && (e.a == pre.cue_ball || e.b == pre.cue_ball)) {
            const int other = e.a == pre.cue_ball ? e.b : e.a;
            met[static_cast<std::size_t>(other)] = std::min(met[static_cast<std::size_t>(other)], e.t);
            if (!hit) {
                hit = true;
                f.first_hit_t = e.t;
                f.first_hit.push_back(other);
            } else if (e.t - f.first_hit_t <= ref.simultaneous_hit_s) {
                f.first_hit.push_back(other);
            }
        } else if (e.kind == EventKind::pocket && pocket_of[static_cast<std::size_t>(e.a)] < 0) {
            pocket_of[static_cast<std::size_t>(e.a)] = e.body;
            if (e.a == pre.cue_ball) {
                f.cue_potted = true;
                f.cue_pocket = e.body;
            } else {
                f.pots.push_back({e.a, e.body, e.t});
            }
        }
    }
    std::sort(f.first_hit.begin(), f.first_hit.end());
    f.first_hit.erase(std::unique(f.first_hit.begin(), f.first_hit.end()), f.first_hit.end());
    std::stable_sort(f.pots.begin(), f.pots.end(),
                     [](const Pot& a, const Pot& b) { return a.t != b.t ? a.t < b.t : a.ball < b.ball; });

    std::vector<Piece> cue_path;
    std::vector<Piece> path;
    path_of(record, cue, cue_path);
    f.cue_travel_m = travelled(cue_path, kInf);

    for (std::size_t i = 0; i < n; ++i) {
        if (i == cue) continue;
        const bool potted = pocket_of[i] >= 0;
        if (!potted) f.rest[i] = xy(record.final_state.balls[i].r);
        const Vec2 end = potted ? table.pockets[static_cast<std::size_t>(pocket_of[i])].drop_centre : f.rest[i];
        f.object_travel_m += length(minus(end, pre.balls[i].pos));

        if (!(met[i] < kInf)) {
            path_of(record, i, path);
            f.closest_gap_m[i] = std::max(closest(cue_path, path, kDiameter) - kDiameter, 0.0);
            continue;
        }
        // S3.8(b) is about the cue ball's way out: a touching ball it ran into as it left, not one
        // that it or another ball came back to later. Across a gap that still counts as touching
        // the contact comes a moment after the strike, but before the cue ball has gone further
        // than a straight line can take it to a ball that near, sqrt(gap (4R + gap)).
        const double gap = std::max(length(minus(pre.balls[i].pos, cue_start)) - kDiameter, 0.0);
        if (!(gap < ref.touching_gap_m)) continue;
        const bool leaving = met[i] <= ref.simultaneous_hit_s ||
                             travelled(cue_path, met[i]) <= std::sqrt(gap * (2.0 * kDiameter + gap));
        if (leaving && set_moving(record, i, met[i])) f.touching_moved.push_back(static_cast<int>(i));
    }
    if (!f.cue_potted) f.rest[cue] = xy(record.final_state.balls[cue].r);

    // S2.19: the tip is taken to be on the cue ball until push_contact_time_s after the strike.
    bool tip_on = record.has_strike && hit && f.first_hit_t < ref.push_contact_time_s;
    Vec2 cue_at;
    Vec2 cue_velocity;
    tip_on = tip_on && entering(cue_path, f.first_hit_t, cue_at, cue_velocity);
    // A contact at the instant of the strike: the record's first segment is already the rebound.
    if (tip_on && !(f.first_hit_t > 0.0)) cue_velocity = xy(record.strike.v);

    std::vector<Piece> mouth(1);
    mouth[0].t1 = kInf;
    for (const int ball : f.first_hit) {
        const std::size_t i = static_cast<std::size_t>(ball);
        path_of(record, i, path);
        Vec2 at;
        Vec2 velocity;
        if (tip_on && entering(path, f.first_hit_t, at, velocity) &&
            cut_angle(cue_velocity, minus(at, cue_at)) < ref.push_fine_edge_rad) {
            f.push = true;
        }
        if (pocket_of[i] >= 0) f.pot_attempt = true;
        for (std::size_t k = 0; k < table.pockets.size() && !f.pot_attempt; ++k) {
            // Near the pocket and nearer to it than it began, both at one moment: together, one
            // bound on its distance from the middle of the pocket.
            mouth[0].p = table.pockets[k].drop_centre;
            const double began = length(minus(pre.balls[i].pos, mouth[0].p));
            const double reach =
                std::min(table.pockets[k].drop_radius + ref.pot_attempt_radius_m, began - kPotApproach_m);
            f.pot_attempt = reach > 0.0 && closest(path, mouth, reach) <= reach;
        }
    }
    return f;
}

}  // namespace ofs::rules
