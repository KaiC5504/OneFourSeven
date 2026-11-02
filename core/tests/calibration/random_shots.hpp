#pragma once

// The suite's own random shot generator for C12-C14 (independent of sim_cli's fuzz): scattered
// balls, frozen clusters, balls touching cushions, break-offs and balls set moving with spin, all
// drawn from ofs::Rng so a seed always gives the same shots.

#include "calib.hpp"

namespace calib {

struct Shot {
    std::string kind;
    SimInput in;
};

inline double dist_to_segment(Vec3 r, table::Vec2 a, table::Vec2 b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    double u = len2 > 0.0 ? ((r.x - a.x) * dx + (r.y - a.y) * dy) / len2 : 0.0;
    u = std::clamp(u, 0.0, 1.0);
    return std::hypot(r.x - (a.x + u * dx), r.y - (a.y + u * dy));
}

inline bool angle_in_arc(double ang, const ArcCushion& c) {
    double span = c.angle_end - c.angle_start;
    while (span < 0.0) span += 2.0 * kPi;
    double off = ang - c.angle_start;
    while (off < 0.0) off += 2.0 * kPi;
    while (off >= 2.0 * kPi) off -= 2.0 * kPi;
    return off <= span;
}

// Where a resting ball may be placed: on the cloth, clear of every cushion line, jaw and pocket.
inline bool placeable(Vec3 r, const TableShape& t, double clearance) {
    const double hx = table::kPlayingLength / 2.0 - R - clearance;
    const double hy = table::kPlayingWidth / 2.0 - R - clearance;
    if (std::abs(r.x) > hx || std::abs(r.y) > hy) return false;
    for (const LineCushion& l : t.lines)
        if (dist_to_segment(r, l.p1, l.p2) < R + clearance) return false;
    for (const ArcCushion& a : t.arcs)
        if (std::hypot(r.x - a.centre.x, r.y - a.centre.y) < a.radius + R + clearance) return false;
    for (const Pocket& p : t.pockets)
        if (std::hypot(r.x - p.drop_centre.x, r.y - p.drop_centre.y) < p.drop_radius + R + clearance) return false;
    return true;
}

inline bool clear_of(Vec3 r, const std::vector<BallState>& balls, double min_dist) {
    for (const BallState& b : balls)
        if (norm_xy(r - b.r) < min_dist) return false;
    return true;
}

// Ball identities in table order: the cue ball, reds 0-14, then the colours.
inline void identity(int i, table::Ball& kind, int& index) {
    static const table::Ball colours[] = {table::Ball::yellow, table::Ball::green, table::Ball::brown,
                                          table::Ball::blue,   table::Ball::pink,  table::Ball::black};
    if (i == 0) {
        kind = table::Ball::cue;
        index = 0;
    } else if (i <= 15) {
        kind = table::Ball::red;
        index = i - 1;
    } else {
        kind = colours[(i - 16) % 6];
        index = 0;
    }
}

inline void add_ball(std::vector<BallState>& balls, Vec3 r) {
    table::Ball kind;
    int index;
    identity(static_cast<int>(balls.size()), kind, index);
    balls.push_back(make_ball(kind, index, r));
}

inline Vec3 random_spot(Rng& rng) {
    return {rng.uniform(-table::kPlayingLength / 2.0, table::kPlayingLength / 2.0),
            rng.uniform(-table::kPlayingWidth / 2.0, table::kPlayingWidth / 2.0), 0.0};
}

inline void scatter(Rng& rng, const TableShape& t, std::vector<BallState>& balls, int n) {
    for (int tries = 0; static_cast<int>(balls.size()) < n && tries < 20000; ++tries) {
        const Vec3 r = random_spot(rng);
        if (placeable(r, t, 1e-4) && clear_of(r, balls, 2.0 * R + 1e-4)) add_ball(balls, r);
    }
}

// A ball touching `base`: exactly 2R apart up to rounding, nudged out by ulps so it never starts
// overlapping; or a 1e-13 m gap, which makes near-zero-time event chains.
inline Vec3 touching(Vec3 base, double angle, bool exact) {
    const Vec3 dir{std::cos(angle), std::sin(angle), 0.0};
    double d = 2.0 * R + (exact ? 0.0 : 1e-13);
    Vec3 r = base + dir * d;
    while (norm_xy(r - base) < 2.0 * R) {
        d = std::nextafter(d, 1.0);
        r = base + dir * d;
    }
    return r;
}

inline void cluster(Rng& rng, const TableShape& t, std::vector<BallState>& balls, int n) {
    for (int tries = 0; tries < 200 && static_cast<int>(balls.size()) < n; ++tries) {
        const Vec3 seed = random_spot(rng);
        if (!placeable(seed, t, 0.05) || !clear_of(seed, balls, 2.0 * R + 0.02)) continue;
        add_ball(balls, seed);
        const std::size_t first = balls.size() - 1;
        const bool exact = rng.uniform() < 0.5;
        const bool line = rng.uniform() < 0.3;
        const double line_angle = rng.uniform(-kPi, kPi);
        for (int k = 0; k < 40 && static_cast<int>(balls.size()) < n; ++k) {
            const std::size_t from = line ? balls.size() - 1
                                          : first + static_cast<std::size_t>(rng.uniform() * (balls.size() - first));
            const Vec3 r = touching(balls[from].r, line ? line_angle : rng.uniform(-kPi, kPi), exact);
            if (!placeable(r, t, 1e-9)) continue;
            bool ok = true;
            for (std::size_t j = 0; j < balls.size(); ++j)
                if (j != from && norm_xy(r - balls[j].r) < 2.0 * R) ok = false;
            if (ok) add_ball(balls, r);
            if (line && !ok) break;
        }
        if (rng.uniform() < 0.5) break;
    }
}

// Balls resting against straight cushions, at R from the nose up to rounding.
inline void on_cushions(Rng& rng, const TableShape& t, std::vector<BallState>& balls, int n) {
    if (t.lines.empty()) return;
    for (int tries = 0; tries < 400 && static_cast<int>(balls.size()) < n; ++tries) {
        const LineCushion& l = t.lines[static_cast<std::size_t>(rng.uniform() * t.lines.size())];
        const double u = rng.uniform(0.05, 0.95);
        const Vec3 on{l.p1.x + u * (l.p2.x - l.p1.x), l.p1.y + u * (l.p2.y - l.p1.y), 0.0};
        const Vec3 nrm{l.normal.x, l.normal.y, 0.0};
        double d = R;
        Vec3 r = on + nrm * d;
        while (dist_to_segment(r, l.p1, l.p2) < R) {
            d = std::nextafter(d, 1.0);
            r = on + nrm * d;
        }
        bool ok = clear_of(r, balls, 2.0 * R);
        for (const LineCushion& o : t.lines)
            if (&o != &l && dist_to_segment(r, o.p1, o.p2) < R) ok = false;
        for (const ArcCushion& a : t.arcs)
            if (std::hypot(r.x - a.centre.x, r.y - a.centre.y) < a.radius + R) ok = false;
        for (const Pocket& p : t.pockets)
            if (std::hypot(r.x - p.drop_centre.x, r.y - p.drop_centre.y) < p.drop_radius + R) ok = false;
        if (ok) add_ball(balls, r);
    }
}

inline CueStrike random_strike(Rng& rng, const std::vector<BallState>& balls, const PhysicsParams& p) {
    CueStrike c;
    if (balls.size() > 1 && rng.uniform() < 0.6) {
        const BallState& target = balls[1 + static_cast<std::size_t>(rng.uniform() * (balls.size() - 1))];
        c.aim_rad = angle_of(target.r - balls[0].r) + rng.uniform(-0.05, 0.05);
    } else {
        c.aim_rad = rng.uniform(-kPi, kPi);
    }
    // Some beyond the miscue limit.
    const double radius = std::sqrt(rng.uniform()) * 0.62;
    const double a = rng.uniform(-kPi, kPi);
    c.tip_x = radius * std::cos(a);
    c.tip_y = radius * std::sin(a);
    c.elevation_rad = rng.uniform() < 0.7 ? rng.uniform(0.0, 0.35) : rng.uniform(0.0, p.max_elevation_rad);
    c.speed_m_s = rng.uniform() < 0.7 ? rng.uniform(0.1, 4.0) : rng.uniform(4.0, p.max_cue_speed_m_s);
    return c;
}

inline Shot break_off(Rng& rng, const PhysicsParams& p) {
    Shot s;
    s.kind = "break-off";
    s.in.start = opening_rack(rng.next_u64(), p);
    s.in.cue_ball = 0;
    s.in.has_strike = true;
    s.in.seed = rng.next_u64();
    s.in.params_hash = "calibration";
    const std::vector<BallState>& b = s.in.start.balls;
    if (b.size() < 2) return s;
    const Vec3 cue = b[0].r;
    // Reds are 1-15 (rack.hpp). Most break-offs clip an end red of the back row thinly from the
    // side the cue ball is on, as players do; the rest go anywhere into the pack.
    std::size_t target = 1;
    for (std::size_t i = 1; i < b.size() && i <= 15; ++i) {
        const bool deeper = b[i].r.x > b[target].r.x + 1e-3;
        const bool level = std::abs(b[i].r.x - b[target].r.x) <= 1e-3;
        const bool outer = (cue.y < 0.0) ? b[i].r.y < b[target].r.y : b[i].r.y > b[target].r.y;
        if (deeper || (level && outer)) target = i;
    }
    double aim;
    if (rng.uniform() < 0.7) {
        const Vec3 d = unit_or_zero(b[target].r - cue);
        Vec3 side{-d.y, d.x, 0.0};
        if (dot(side, Vec3{0.0, b[target].r.y, 0.0}) < 0.0) side = -side;
        const Vec3 aim_at = b[target].r + side * (2.0 * R * rng.uniform(0.6, 0.98));
        aim = angle_of(aim_at - cue);
    } else {
        const std::size_t any = 1 + static_cast<std::size_t>(rng.uniform() * 15.0);
        aim = angle_of(b[std::min(any, b.size() - 1)].r - cue) + rng.uniform(-0.03, 0.03);
    }
    s.in.strike = CueStrike{aim, rng.uniform(-0.45, 0.45), rng.uniform(-0.3, 0.3), rng.uniform(0.05, 0.25),
                            rng.uniform(3.0, 9.0)};
    return s;
}

inline Shot random_shot(Rng& rng, const TableShape& t, const PhysicsParams& p) {
    const double pick = rng.uniform();
    if (pick < 0.15) return break_off(rng, p);
    Shot s;
    s.in.cue_ball = 0;
    s.in.seed = rng.next_u64();
    s.in.params_hash = "calibration";
    std::vector<BallState> balls;
    const int n = 1 + static_cast<int>(rng.uniform() * 22.0);
    if (pick < 0.40) {
        s.kind = "cluster";
        scatter(rng, t, balls, 1);
        cluster(rng, t, balls, std::max(n, 3));
        scatter(rng, t, balls, std::max(n, static_cast<int>(balls.size())));
    } else if (pick < 0.60) {
        s.kind = "cushion";
        scatter(rng, t, balls, 1);
        on_cushions(rng, t, balls, std::max(2, n / 2));
        scatter(rng, t, balls, n);
    } else {
        s.kind = "scatter";
        scatter(rng, t, balls, n);
    }
    if (balls.empty()) scatter(rng, t, balls, 1);
    if (pick >= 0.85) {
        // Balls already moving, with every kind of spin: mid-shot states.
        s.kind += "+moving";
        s.in.has_strike = false;
        for (BallState& b : balls) {
            if (rng.uniform() < 0.4) continue;
            const double speed = rng.uniform(0.0, 5.0);
            const double dir = rng.uniform(-kPi, kPi);
            b.v = {speed * std::cos(dir), speed * std::sin(dir), 0.0};
            const double spin = (speed + 0.5) / R;
            b.w = {rng.uniform(-spin, spin), rng.uniform(-spin, spin), rng.uniform(-60.0, 60.0)};
        }
    } else {
        s.in.has_strike = true;
        s.in.strike = random_strike(rng, balls, p);
    }
    s.in.start.balls = std::move(balls);
    return s;
}

}  // namespace calib
