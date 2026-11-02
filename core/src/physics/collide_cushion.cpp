#include "ofs/physics/collide.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "ofs/physics/motion.hpp"

// Ball-cushion impacts: Mathavan, Jackson & Parkin (2010), "A theoretical analysis of billiard ball
// dynamics under cushion impacts", Proc. IMechE C 224(9), equations (12)-(17); the same equations as
// pooltool's physics/resolve/ball_cushion/mathavan_2010/model.py (Apache-2.0), which integrates them
// with a fixed-step Euler scheme over 1000-5000 steps. That is too slow for a break-off, so this
// integrates them with an adaptive Dormand-Prince 5(4) between the moments a contact's slip reaches
// zero, and decides at each such moment whether the contact sticks (Coulomb friction can hold it) or
// slips again, which is what the fine Euler scheme converges to. core/tests/physics/
// test_collide_cushion.cpp keeps the 5000-step scheme as the reference.
//
// Frame: x along the cushion, y into it (an approaching ball has vy > 0), z up. The independent
// variable is the normal impulse at the cushion contact I per unit mass, p = P_I / M, and spins are
// carried as u = R w, so mass and radius drop out. Contact I sits at angle theta above the ball's
// equator, sin(theta) = (h - R) / R; contact C is the cloth below the ball.

namespace ofs::phys {

namespace {

// M R^2 / I for a solid sphere.
constexpr double kQ = 2.5;
constexpr double kA = 1.0 + kQ;

// Integration tolerance relative to the impact's speed scale. 1e-4 keeps every case of the C7
// grid within 0.06% speed and 0.05 degrees of the 5000-step reference at about 30 evaluations.
constexpr double kRelTol = 1e-4;
// A slip closing on zero counts as zero once below this fraction of the speed scale; the error it
// leaves is of the same order.
constexpr double kSnap = 1e-3;
// Steps towards a predicted zero slip cover this fraction of the remaining distance, so they never
// land on the point where the slip's direction is undefined.
constexpr double kApproach = 0.8;
constexpr int kMaxSteps = 400;

enum class Mode : unsigned char { slip, stick };

struct Dir {
    double x = 0.0;
    double y = 0.0;
};

double length(Dir d) { return std::sqrt(d.x * d.x + d.y * d.y); }

Dir unit(Dir d) {
    const double n = length(d);
    return n > 0.0 ? Dir{d.x / n, d.y / n} : Dir{};
}

// Friction impulse per unit p at I (in the cushion's tangent plane: x and the in-plane direction
// up the contact) and at C.
struct Friction {
    double ix = 0.0;
    double iy = 0.0;
    double cx = 0.0;
    double cy = 0.0;
};

// vx, vy, ux, uy, uz and the work done at I along its normal (Mathavan's W_ZI).
using State = std::array<double, 6>;

class Impact {
public:
    Impact(double sin_theta, double mu_w, double mu_s, double e)
        : st_(sin_theta), ct_(std::sqrt(1.0 - sin_theta * sin_theta)), mu_w_(mu_w), mu_s_(mu_s), e_(e),
          b_(1.0 - kQ * sin_theta), c_(sin_theta * sin_theta + kQ), d_(kQ - sin_theta) {}

    // y0's work entry is ignored. False when the step limit ran out (the result is then unusable).
    bool solve(const State& y0, State& out);

private:
    struct Slips {
        Dir i;
        Dir c;
    };

    // Mathavan (12a), (12b), (13a), (13b).
    Slips raw_slips(const State& y) const {
        return {{y[0] + y[3] * st_ - y[4] * ct_, -y[1] * st_ + y[2]}, {y[0] - y[3], y[1] + y[2]}};
    }

    Slips slips(const State& y) const {
        const Slips s = raw_slips(y);
        return {{s.i.x - off_[0].x, s.i.y - off_[0].y}, {s.c.x - off_[1].x, s.c.y - off_[1].y}};
    }

    Slips slip_rates(const Friction& f) const {
        return {{kA * f.ix + b_ * f.cx, st_ * ct_ + c_ * f.iy + d_ * f.cy},
                {b_ * f.ix + kA * f.cx, -ct_ + d_ * f.iy + kA * f.cy}};
    }

    // Mathavan (14a)-(14f) with Mathavan's (8) for the cloth's normal impulse, clamped at zero
    // (it only goes negative for a cushion friction above tan(theta)).
    double cloth_normal(double fiy) const { return std::max(0.0, st_ - fiy * ct_); }

    Friction friction(Mode mi, Mode mc, Dir di, Dir dc) const;
    bool feasible(Mode mi, Mode mc, const Friction& f) const;
    Dir slip_dir(Dir s, int contact) const;
    State rhs(const State& y) const;
    double step(const State& y, double h, const State& k1, State& yn, State& k7) const;
    double time_to_zero_slip(const State& y, const State& k, int& which) const;
    double time_to_end(const State& y, const State& k, bool restitution, double target) const;
    double stick_margin(const State& y) const;
    void resolve(const State& y, bool no_stick_i, bool no_stick_c);

    double st_, ct_, mu_w_, mu_s_, e_;
    double b_, c_, d_;  // cross and self terms of the contacts' mobility
    double zero_ = 0.0;
    double snap_ = 0.0;
    Mode mode_[2] = {Mode::slip, Mode::slip};
    // A contact that restarts from zero slip measures its slip from the residual left at that
    // moment and starts in the direction its slip will grow.
    Dir off_[2];
    Dir restart_[2];
};

Friction Impact::friction(Mode mi, Mode mc, Dir di, Dir dc) const {
    Friction f;
    if (mi == Mode::slip) {
        f.ix = -mu_w_ * di.x;
        f.iy = -mu_w_ * di.y;
        const double nc = cloth_normal(f.iy);
        if (mc == Mode::slip) {
            f.cx = -mu_s_ * nc * dc.x;
            f.cy = -mu_s_ * nc * dc.y;
        } else {
            f.cx = -b_ * f.ix / kA;
            f.cy = (ct_ - d_ * f.iy) / kA;
        }
        return f;
    }
    if (mc == Mode::slip) {
        // I holds: solve its slip rate = 0 with C's friction depending on I's through the cloth's
        // normal impulse.
        const double den = c_ + d_ * mu_s_ * ct_ * dc.y;
        if (den <= 1e-9) {
            // No finite friction holds I (needs cloth friction of 1 or more): report one outside
            // the cone so feasible() and stick_margin() let it slip.
            f.iy = 2.0 * mu_w_ + 1.0;
            f.cx = -mu_s_ * st_ * dc.x;
            f.cy = -mu_s_ * st_ * dc.y;
            return f;
        }
        f.iy = (-st_ * ct_ + d_ * mu_s_ * st_ * dc.y) / den;
        double nc = st_ - f.iy * ct_;
        if (nc < 0.0) {
            nc = 0.0;
            f.iy = -st_ * ct_ / c_;
        }
        f.cx = -mu_s_ * nc * dc.x;
        f.cy = -mu_s_ * nc * dc.y;
        f.ix = -b_ * f.cx / kA;
        return f;
    }
    // Both hold; the x parts cancel because kA != |b_|.
    const double det = c_ * kA - d_ * d_;
    f.iy = (-st_ * ct_ * kA - d_ * ct_) / det;
    f.cy = (c_ * ct_ + d_ * st_ * ct_) / det;
    return f;
}

bool Impact::feasible(Mode mi, Mode mc, const Friction& f) const {
    constexpr double kSlack = 1.0 + 1e-9;
    const double nc = st_ - f.iy * ct_;
    if (mi == Mode::stick && std::hypot(f.ix, f.iy) > mu_w_ * kSlack) return false;
    if (mc == Mode::stick && (nc <= 0.0 || std::hypot(f.cx, f.cy) > mu_s_ * nc * kSlack)) return false;
    return true;
}

Dir Impact::slip_dir(Dir s, int contact) const {
    const double n = length(s);
    return n > zero_ ? Dir{s.x / n, s.y / n} : restart_[contact];
}

State Impact::rhs(const State& y) const {
    const Slips s = slips(y);
    const Friction f = friction(mode_[0], mode_[1], slip_dir(s.i, 0), slip_dir(s.c, 1));
    return {f.ix + f.cx,
            -ct_ - f.iy * st_ + f.cy,
            kQ * (f.iy + f.cy),
            kQ * (f.ix * st_ - f.cx),
            -kQ * f.ix * ct_,
            ct_ * std::abs(y[1])};
}

// One Dormand-Prince 5(4) step; returns the error estimate over the velocities.
double Impact::step(const State& y, double h, const State& k1, State& yn, State& k7) const {
    constexpr double a21 = 1.0 / 5.0;
    constexpr double a31 = 3.0 / 40.0, a32 = 9.0 / 40.0;
    constexpr double a41 = 44.0 / 45.0, a42 = -56.0 / 15.0, a43 = 32.0 / 9.0;
    constexpr double a51 = 19372.0 / 6561.0, a52 = -25360.0 / 2187.0, a53 = 64448.0 / 6561.0,
                     a54 = -212.0 / 729.0;
    constexpr double a61 = 9017.0 / 3168.0, a62 = -355.0 / 33.0, a63 = 46732.0 / 5247.0, a64 = 49.0 / 176.0,
                     a65 = -5103.0 / 18656.0;
    constexpr double b1 = 35.0 / 384.0, b3 = 500.0 / 1113.0, b4 = 125.0 / 192.0, b5 = -2187.0 / 6784.0,
                     b6 = 11.0 / 84.0;
    constexpr double e1 = 71.0 / 57600.0, e3 = -71.0 / 16695.0, e4 = 71.0 / 1920.0, e5 = -17253.0 / 339200.0,
                     e6 = 22.0 / 525.0, e7 = -1.0 / 40.0;
    constexpr std::size_t n = 6;
    State t;
    for (std::size_t i = 0; i < n; ++i) t[i] = y[i] + h * a21 * k1[i];
    const State k2 = rhs(t);
    for (std::size_t i = 0; i < n; ++i) t[i] = y[i] + h * (a31 * k1[i] + a32 * k2[i]);
    const State k3 = rhs(t);
    for (std::size_t i = 0; i < n; ++i) t[i] = y[i] + h * (a41 * k1[i] + a42 * k2[i] + a43 * k3[i]);
    const State k4 = rhs(t);
    for (std::size_t i = 0; i < n; ++i) {
        t[i] = y[i] + h * (a51 * k1[i] + a52 * k2[i] + a53 * k3[i] + a54 * k4[i]);
    }
    const State k5 = rhs(t);
    for (std::size_t i = 0; i < n; ++i) {
        t[i] = y[i] + h * (a61 * k1[i] + a62 * k2[i] + a63 * k3[i] + a64 * k4[i] + a65 * k5[i]);
    }
    const State k6 = rhs(t);
    for (std::size_t i = 0; i < n; ++i) {
        yn[i] = y[i] + h * (b1 * k1[i] + b3 * k3[i] + b4 * k4[i] + b5 * k5[i] + b6 * k6[i]);
    }
    k7 = rhs(yn);
    double err = 0.0;
    for (std::size_t i = 0; i < 5; ++i) {
        const double ei = h * (e1 * k1[i] + e3 * k3[i] + e4 * k4[i] + e5 * k5[i] + e6 * k6[i] + e7 * k7[i]);
        err = std::max(err, std::abs(ei));
    }
    return err;
}

// The p until a slipping contact's slip, extrapolated linearly, is closest to zero, for the one
// that gets there first; infinity when neither is closing.
double Impact::time_to_zero_slip(const State& y, const State& k, int& which) const {
    const Slips s = slips(y);
    const Slips ds{{k[0] + k[3] * st_ - k[4] * ct_, -k[1] * st_ + k[2]}, {k[0] - k[3], k[1] + k[2]}};
    const Dir* sv[2] = {&s.i, &s.c};
    const Dir* dv[2] = {&ds.i, &ds.c};
    double best = std::numeric_limits<double>::infinity();
    which = -1;
    for (int i = 0; i < 2; ++i) {
        if (mode_[i] != Mode::slip) continue;
        const double n2 = sv[i]->x * sv[i]->x + sv[i]->y * sv[i]->y;
        const double closing = sv[i]->x * dv[i]->x + sv[i]->y * dv[i]->y;
        if (closing < 0.0 && n2 > 0.0 && -n2 / closing < best) {
            best = -n2 / closing;
            which = i;
        }
    }
    return best;
}

// The p until compression ends (vy = 0) or restitution has returned `target` work, with the
// rates at y held.
double Impact::time_to_end(const State& y, const State& k, bool restitution, double target) const {
    constexpr double inf = std::numeric_limits<double>::infinity();
    if (!restitution) return k[1] < 0.0 ? std::max(0.0, y[1] / -k[1]) : inf;
    const double rem = std::max(0.0, target - y[5]);
    const double qa = -ct_ * k[1] / 2.0;
    const double qb = -ct_ * y[1];
    if (qa <= 0.0) return qb > 0.0 ? rem / qb : inf;
    return 2.0 * rem / (qb + std::sqrt(qb * qb + 4.0 * qa * rem));
}

// How far the sticking contacts' friction is inside its cone; negative once one must slip.
double Impact::stick_margin(const State& y) const {
    if (mode_[0] == Mode::slip && mode_[1] == Mode::slip) return std::numeric_limits<double>::infinity();
    const Slips s = slips(y);
    const Friction f = friction(mode_[0], mode_[1], slip_dir(s.i, 0), slip_dir(s.c, 1));
    double g = std::numeric_limits<double>::infinity();
    if (mode_[0] == Mode::stick) g = std::min(g, mu_w_ - std::hypot(f.ix, f.iy));
    if (mode_[1] == Mode::stick) g = std::min(g, mu_s_ * (st_ - f.iy * ct_) - std::hypot(f.cx, f.cy));
    return g;
}

// At a zero slip (or a broken stick) choose each contact's mode: stick when friction can hold it,
// otherwise slip in the direction that is a fixed point of "slip grows along its own rate", the
// direction a fine Euler scheme settles into after the crossing.
void Impact::resolve(const State& y, bool no_stick_i, bool no_stick_c) {
    const Slips s = slips(y);
    const Dir sv[2] = {s.i, s.c};
    const bool no_stick[2] = {no_stick_i, no_stick_c};
    bool zero[2];
    for (int i = 0; i < 2; ++i) zero[i] = mode_[i] == Mode::stick || length(sv[i]) <= snap_;

    Dir guess[2];
    if (mode_[0] == Mode::stick || mode_[1] == Mode::stick) {
        const Friction f = friction(mode_[0], mode_[1], slip_dir(s.i, 0), slip_dir(s.c, 1));
        if (mode_[0] == Mode::stick) guess[0] = unit({-f.ix, -f.iy});
        if (mode_[1] == Mode::stick) guess[1] = unit({-f.cx, -f.cy});
    }

    const Mode order[4][2] = {
        {Mode::stick, Mode::stick}, {Mode::stick, Mode::slip}, {Mode::slip, Mode::stick},
        {Mode::slip, Mode::slip}};
    Mode chosen[2] = {Mode::slip, Mode::slip};
    Dir dirs[2] = {unit(sv[0]), unit(sv[1])};
    Dir fallback[2] = {dirs[0], dirs[1]};
    bool found = false;
    for (const auto& modes : order) {
        bool allowed = true;
        for (int i = 0; i < 2; ++i) {
            if (modes[i] == Mode::stick && (!zero[i] || no_stick[i])) allowed = false;
        }
        if (!allowed) continue;
        bool restart[2];
        Dir d[2];
        for (int i = 0; i < 2; ++i) {
            restart[i] = zero[i] && modes[i] == Mode::slip;
            d[i] = restart[i] ? guess[i] : unit(sv[i]);
        }
        Slips rates{};
        for (int it = 0; it < 60; ++it) {
            rates = slip_rates(friction(modes[0], modes[1], d[0], d[1]));
            const Dir r[2] = {unit(rates.i), unit(rates.c)};
            double change = 0.0;
            for (int i = 0; i < 2; ++i) {
                if (!restart[i]) continue;
                change += std::abs(r[i].x - d[i].x) + std::abs(r[i].y - d[i].y);
                d[i] = r[i];
            }
            if (change < 1e-14) break;
        }
        const Friction f = friction(modes[0], modes[1], d[0], d[1]);
        rates = slip_rates(f);
        const Dir r[2] = {rates.i, rates.c};
        bool ok = feasible(modes[0], modes[1], f);
        for (int i = 0; i < 2; ++i) {
            if (restart[i] && r[i].x * d[i].x + r[i].y * d[i].y < -1e-9) ok = false;
        }
        if (modes[0] == Mode::slip && modes[1] == Mode::slip) {
            fallback[0] = d[0];
            fallback[1] = d[1];
        }
        if (ok) {
            chosen[0] = modes[0];
            chosen[1] = modes[1];
            dirs[0] = d[0];
            dirs[1] = d[1];
            found = true;
            break;
        }
    }
    if (!found) {
        // No assignment passes within rounding (seen only right at a stick breaking, where the
        // slip rate is zero): slip on in the directions found, which is what the next steps refine.
        dirs[0] = fallback[0];
        dirs[1] = fallback[1];
    }
    const Slips raw = raw_slips(y);
    const Dir rawv[2] = {raw.i, raw.c};
    for (int i = 0; i < 2; ++i) {
        mode_[i] = chosen[i];
        if (zero[i]) {
            off_[i] = rawv[i];
            restart_[i] = dirs[i];
        }
    }
}

bool Impact::solve(const State& y0, State& out) {
    constexpr double inf = std::numeric_limits<double>::infinity();
    const double scale = std::hypot(y0[0], y0[1]) + std::sqrt(y0[2] * y0[2] + y0[3] * y0[3] + y0[4] * y0[4]);
    zero_ = 1e-13 * scale;
    snap_ = kSnap * scale;
    const double tol = kRelTol * scale;
    const double p_total = (1.0 + e_) * y0[1];

    State y = y0;
    y[5] = 0.0;
    {
        const Slips s = slips(y);
        if (length(s.i) <= snap_ || length(s.c) <= snap_) resolve(y, false, false);
    }
    double h = p_total / 3.0;
    bool restitution = false;
    double target = 0.0;
    State k = rhs(y);
    State yn;
    State kn;
    for (int steps = 0; steps < kMaxSteps; ++steps) {
        const double te = time_to_end(y, k, restitution, target);
        int which = -1;
        const double ts = time_to_zero_slip(y, k, which);
        if (which >= 0) {
            const Slips s = slips(y);
            if (length(which == 0 ? s.i : s.c) <= snap_) {
                resolve(y, false, false);
                k = rhs(y);
                continue;
            }
        }
        const double hh = std::min({h, te, kApproach * ts});
        const bool to_end = hh == te;
        const double err = step(y, hh, k, yn, kn);
        if (err > tol && hh > 1e-9 * p_total) {
            h = hh * std::max(0.1, 0.9 * std::pow(tol / err, 0.2));
            continue;
        }
        if (stick_margin(yn) < 0.0) {
            // Find where the stick breaks (regula falsi on the margin), then let it slip.
            double lo = 0.0;
            double hi = hh;
            double glo = stick_margin(y);
            double ghi = stick_margin(yn);
            State yhi = yn;
            State yt;
            State kt;
            for (int it = 0; it < 50 && hi - lo > 1e-12 * p_total; ++it) {
                double t = lo + (hi - lo) * glo / (glo - ghi);
                t = std::clamp(t, lo + 1e-3 * (hi - lo), hi - 1e-3 * (hi - lo));
                step(y, t, k, yt, kt);
                const double gt = stick_margin(yt);
                if (gt < 0.0) {
                    hi = t;
                    ghi = gt;
                    yhi = yt;
                    if (gt >= -1e-10) break;
                } else {
                    lo = t;
                    glo = gt;
                }
            }
            y = yhi;
            resolve(y, mode_[0] == Mode::stick, mode_[1] == Mode::stick);
            k = rhs(y);
            continue;
        }
        y = yn;
        k = kn;
        if (!to_end) {
            h = hh * (err > 0.0 ? std::min(5.0, 0.9 * std::pow(tol / err, 0.2)) : 5.0);
            if (restitution || y[1] > 0.0) continue;
        }
        // Close the small gap the step left to the phase's end with the rates held.
        const double rest = time_to_end(y, k, restitution, target);
        if (rest < inf) {
            for (std::size_t i = 0; i < 6; ++i) y[i] += k[i] * rest;
        }
        if (restitution) {
            out = y;
            return true;
        }
        target = e_ * e_ * y[5];
        y[1] = 0.0;
        y[5] = 0.0;
        restitution = true;
        if (target <= 0.0) {
            out = y;
            return true;
        }
        k = rhs(y);
    }
    return false;
}

struct Surface {
    Vec3 normal;  // horizontal, unit, from the cushion towards the ball
    double restitution;
    double friction;
};

void collide(BallState& b, const Surface& s, const PhysicsParams& p) {
    const double R = table::kBallRadius;
    const double nx = s.normal.x;
    const double ny = s.normal.y;
    // Cushion frame: y into the cushion, x = y cross z.
    const double vx = -b.v.x * ny + b.v.y * nx;
    const double vy = -(b.v.x * nx + b.v.y * ny);
    if (vy > 0.0) {
        const double over = std::max(0.0, vy - p.cushion_rigid_limit_m_s);
        const double e =
            std::min(s.restitution,
                     std::max(p.cushion_restitution_min, s.restitution - p.cushion_restitution_slope_per_m_s * over));
        const double sin_theta = std::clamp((p.cushion_contact_height_m - R) / R, 0.0, 1.0);
        const State y0{vx, vy, R * (-b.w.x * ny + b.w.y * nx), -R * (b.w.x * nx + b.w.y * ny), R * b.w.z, 0.0};
        State y;
        Impact impact(sin_theta, s.friction, p.cushion_table_friction, e);
        if (!impact.solve(y0, y)) {
            // Unreached on every tested impact; a plain restitution bounce keeps the ball valid.
            y = y0;
            y[1] = -e * vy;
        }
        b.v = {-y[0] * ny - y[1] * nx, y[0] * nx - y[1] * ny, 0.0};
        b.w = {(-y[2] * ny - y[3] * nx) / R, (y[2] * nx - y[3] * ny) / R, y[4] / R};
    }
    b.motion = classify(b, p);
}

}  // namespace

void collide_line(BallState& b, const LineCushion& c, const PhysicsParams& p) {
    collide(b, {{c.normal.x, c.normal.y, 0.0}, p.cushion_restitution, p.cushion_friction}, p);
}

void collide_arc(BallState& b, const ArcCushion& c, const PhysicsParams& p) {
    const Vec3 n = unit_or_zero(Vec3{b.r.x - c.centre.x, b.r.y - c.centre.y, 0.0});
    collide(b, {n, p.jaw_restitution, p.jaw_friction}, p);
}

}  // namespace ofs::phys
