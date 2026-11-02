#include "ofs/physics/detect.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ofs/math/roots.hpp"

// Each contact is a sign change of a gap polynomial f(t), positive while apart, as in pooltool's
// evolution/event_based/detect (ball_ball.py, ball_cushion.py, ball_pocket.py) and Leckie &
// Greenspan (2005). Unlike pooltool, which takes the smallest positive root, a contact here begins
// only where f is falling: the window is split at the critical points of f into monotone
// stretches and only falling ones are searched, so a touching pair that separates, a ball leaving
// a cushion, or a graze that only touches (a double root) gives no event.

namespace ofs::phys {

namespace {

constexpr double kEps = std::numeric_limits<double>::epsilon();
// f within kZeroTol eps of its terms' magnitude counts as zero: touching, or a tangent graze.
constexpr double kZeroTol = 64.0;
// Windows are capped here so the polynomials stay finite when t_max and the durations are all
// kNever; no segment lasts anywhere near this long.
constexpr double kHorizon = 1.0e6;
// How far past a cushion segment's ends (m) and an arc's angles (rad) a contact still counts, so
// a contact exactly at a joint between a segment and a jaw is never lost to rounding.
constexpr double kSegmentSlack = 1.0e-9;
constexpr double kAngleSlack = 1.0e-9;
constexpr double kTwoPi = 6.283185307179586476925286766559;

// f(t) = sum_k p[k] t^k, lowest degree first, zero above degree n. m[k] is the size of the terms
// p[k] was built from, before they cancelled, which sets how close to zero f can be told apart
// from zero.
struct GapPoly {
    double p[5] = {};
    double m[5] = {};
    int n = 4;

    double at(double t) const { return (((p[4] * t + p[3]) * t + p[2]) * t + p[1]) * t + p[0]; }
    double tolerance(double t) const {
        const double a = std::abs(t);
        return kZeroTol * kEps * ((((m[4] * a + m[3]) * a + m[2]) * a + m[1]) * a + m[0]);
    }
};

double window(double t_max, double d1, double d2) {
    return std::min(std::min(t_max, kHorizon), std::min(d1, d2));
}

// |d0 + d1 t + d2 t^2|^2 - contact^2 over the planar components. `scale` is the size of the
// positions d0 was subtracted from, whose rounding d0 carries.
GapPoly distance_poly(double d0x, double d0y, double d1x, double d1y, double d2x, double d2y, double contact,
                      double scale) {
    GapPoly f;
    f.p[4] = d2x * d2x + d2y * d2y;
    f.p[3] = 2.0 * (d1x * d2x + d1y * d2y);
    f.p[2] = d1x * d1x + d1y * d1y + 2.0 * (d0x * d2x + d0y * d2y);
    f.p[1] = 2.0 * (d0x * d1x + d0y * d1y);
    const double d0_2 = d0x * d0x + d0y * d0y;
    f.p[0] = d0_2 - contact * contact;
    f.m[4] = f.p[4];
    f.m[3] = 2.0 * (std::abs(d1x * d2x) + std::abs(d1y * d2y));
    f.m[2] = d1x * d1x + d1y * d1y + 2.0 * (std::abs(d0x * d2x) + std::abs(d0y * d2y));
    f.m[1] = 2.0 * (std::abs(d0x * d1x) + std::abs(d0y * d1y));
    f.m[0] = d0_2 + contact * contact + 2.0 * std::sqrt(d0_2) * scale;
    return f;
}

// True when |d0| can't come down to reach_limit within t: the relative position moves at most
// |d1| t + |d2| t^2 (squared lengths given).
bool out_of_reach(double d0_2, double d1_2, double d2_2, double reach_limit, double t) {
    const double reach = reach_limit + std::sqrt(d1_2) * t + std::sqrt(d2_2) * t * t;
    return d0_2 > reach * reach * (1.0 + 1.0e-12);
}

// A start for the root in the falling stretch [x[i], x[i + 1]]: from whichever end is a critical
// point with the smaller |f|, f(c + h) ~ f(c) + f''(c) h^2 / 2 (exact for a head-on pair at constant
// speed), or the secant between the window's ends.
double start(const GapPoly& f, const double* x, const double* fv, int i, int m) {
    const double secant = x[i] - fv[i] * (x[i + 1] - x[i]) / (fv[i + 1] - fv[i]);
    const bool lower = i > 0;
    const bool upper = i + 2 < m;
    if (!lower && !upper) return secant;
    const int c = upper && (!lower || std::abs(fv[i + 1]) <= std::abs(fv[i])) ? i + 1 : i;
    const double curv = (12.0 * f.p[4] * x[c] + 6.0 * f.p[3]) * x[c] + 2.0 * f.p[2];
    if (!(fv[c] * curv < 0.0)) return secant;
    const double h = std::sqrt(-2.0 * fv[c] / curv);
    return c == i ? x[c] + h : x[c] - h;
}

// The earliest t in [0, hi] where a contact begins and accept(t) holds: f falls through zero,
// or f is falling from a start that is already touching or overlapping (but not below `floor`,
// which marks a ball behind the body rather than in it). A falling stretch that only reaches
// zero at its bottom is a graze and gives no event.
template <class Accept>
double first_contact(const GapPoly& poly, double hi, double floor, Accept accept) {
    // Rounding leaves a near-zero relative acceleration in place of zero, and with it top
    // coefficients that can't move f by a rounding's width over the window but spread the
    // derivative's roots so far apart that the cubic's estimates lose the one that matters.
    GapPoly f = poly;
    double size = 0.0;
    for (int k = f.n; k >= 0; --k) size = size * hi + f.m[k];
    while (f.n > 1 && std::abs(f.p[f.n]) * std::pow(hi, f.n) <= kEps * size) f.p[f.n--] = 0.0;

    double x[5] = {};
    int m = 0;
    x[m++] = 0.0;
    m += math::detail::critical_points(f.p, f.n, 0.0, hi, x + 1);
    x[m++] = hi;

    double fv[5] = {};
    double tol[5] = {};
    for (int i = 0; i < m; ++i) {
        fv[i] = f.at(x[i]);
        tol[i] = f.tolerance(x[i]);
    }

    for (int i = 0; i + 1 < m; ++i) {
        const double f0 = fv[i];
        const double f1 = fv[i + 1];
        if (!(f0 - f1 > tol[i] + tol[i + 1])) continue;  // rising, or flat to rounding
        if (f0 <= tol[i]) {
            if (f0 >= floor && accept(x[i])) return x[i];
            continue;
        }
        if (f1 > tol[i + 1]) continue;
        const bool window_end = i + 2 == m;
        if (f1 >= -tol[i + 1] && !window_end) continue;
        double t = x[i + 1];
        if (f1 < 0.0) t = math::detail::bracketed_root(f.p, x[i], f0, x[i + 1], start(f, x, fv, i, m));
        if (accept(t)) return t;
    }
    return kNever;
}

double wrap_two_pi(double a) {
    a = std::fmod(a, kTwoPi);
    return a < 0.0 ? a + kTwoPi : a;
}

bool within_arc(const ArcCushion& c, double dx, double dy) {
    const double raw_span = c.angle_end - c.angle_start;
    if (raw_span >= kTwoPi) return true;
    const double span = wrap_two_pi(raw_span);
    const double off = wrap_two_pi(std::atan2(dy, dx) - c.angle_start);
    return off <= span + kAngleSlack || off >= kTwoPi - kAngleSlack;
}

}  // namespace

double ball_ball_time(const Trajectory& a, const Trajectory& b, double R, double t_max) {
    const double hi = window(t_max, a.duration, b.duration);
    if (!(hi >= 0.0)) return kNever;
    const Vec3 d0 = a.c0 - b.c0;
    const Vec3 d1 = a.c1 - b.c1;
    const Vec3 d2 = a.c2 - b.c2;
    const double d1_2 = d1.x * d1.x + d1.y * d1.y;
    const double d2_2 = d2.x * d2.x + d2.y * d2.y;
    if (d1_2 == 0.0 && d2_2 == 0.0) return kNever;
    const double contact = 2.0 * R;
    if (out_of_reach(d0.x * d0.x + d0.y * d0.y, d1_2, d2_2, contact, hi)) return kNever;

    const double scale = std::abs(a.c0.x) + std::abs(a.c0.y) + std::abs(b.c0.x) + std::abs(b.c0.y);
    const GapPoly f = distance_poly(d0.x, d0.y, d1.x, d1.y, d2.x, d2.y, contact, scale);
    return first_contact(f, hi, -std::numeric_limits<double>::infinity(), [](double) { return true; });
}

double ball_line_time(const Trajectory& a, const LineCushion& c, double R, double t_max) {
    const double hi = window(t_max, a.duration, kNever);
    if (!(hi >= 0.0)) return kNever;
    const double nx = c.normal.x;
    const double ny = c.normal.y;
    GapPoly g;
    g.n = 2;
    g.p[2] = nx * a.c2.x + ny * a.c2.y;
    g.p[1] = nx * a.c1.x + ny * a.c1.y;
    g.p[0] = nx * (a.c0.x - c.p1.x) + ny * (a.c0.y - c.p1.y) - R;
    if (g.p[2] == 0.0 && g.p[1] == 0.0) return kNever;
    if (g.p[0] > std::abs(g.p[1]) * hi + std::abs(g.p[2]) * hi * hi) return kNever;
    g.m[2] = std::abs(nx * a.c2.x) + std::abs(ny * a.c2.y);
    g.m[1] = std::abs(nx * a.c1.x) + std::abs(ny * a.c1.y);
    g.m[0] = std::abs(nx) * (std::abs(a.c0.x) + std::abs(c.p1.x)) + std::abs(ny) * (std::abs(a.c0.y) + std::abs(c.p1.y)) + R;

    const double ux = c.p2.x - c.p1.x;
    const double uy = c.p2.y - c.p1.y;
    const double len = std::sqrt(ux * ux + uy * uy);
    auto on_segment = [&](double t) {
        const Vec3 r = a.at(t);
        const double s = ((r.x - R * nx - c.p1.x) * ux + (r.y - R * ny - c.p1.y) * uy) / len;
        return s >= -kSegmentSlack && s <= len + kSegmentSlack;
    };
    // Below -R the centre is behind the nose line: a ball beyond the cushion, not pressing into it.
    return first_contact(g, hi, -R, on_segment);
}

double ball_arc_time(const Trajectory& a, const ArcCushion& c, double R, double t_max) {
    const double hi = window(t_max, a.duration, kNever);
    if (!(hi >= 0.0)) return kNever;
    const double ex = a.c0.x - c.centre.x;
    const double ey = a.c0.y - c.centre.y;
    const double v_2 = a.c1.x * a.c1.x + a.c1.y * a.c1.y;
    const double acc_2 = a.c2.x * a.c2.x + a.c2.y * a.c2.y;
    if (v_2 == 0.0 && acc_2 == 0.0) return kNever;
    const double contact = c.radius + R;
    if (out_of_reach(ex * ex + ey * ey, v_2, acc_2, contact, hi)) return kNever;

    const double scale = std::abs(a.c0.x) + std::abs(a.c0.y) + std::abs(c.centre.x) + std::abs(c.centre.y);
    const GapPoly f = distance_poly(ex, ey, a.c1.x, a.c1.y, a.c2.x, a.c2.y, contact, scale);
    auto on_arc = [&](double t) {
        const Vec3 r = a.at(t);
        return within_arc(c, r.x - c.centre.x, r.y - c.centre.y);
    };
    // Closer to the centre than `radius` is inside the jaw itself, not touching its face.
    return first_contact(f, hi, c.radius * c.radius - contact * contact, on_arc);
}

double ball_pocket_time(const Trajectory& a, const Pocket& p, double t_max) {
    const double hi = window(t_max, a.duration, kNever);
    if (!(hi >= 0.0)) return kNever;
    const double ex = a.c0.x - p.drop_centre.x;
    const double ey = a.c0.y - p.drop_centre.y;
    const double e_2 = ex * ex + ey * ey;
    if (e_2 < p.drop_radius * p.drop_radius) return 0.0;
    const double v_2 = a.c1.x * a.c1.x + a.c1.y * a.c1.y;
    const double acc_2 = a.c2.x * a.c2.x + a.c2.y * a.c2.y;
    if (v_2 == 0.0 && acc_2 == 0.0) return kNever;
    if (out_of_reach(e_2, v_2, acc_2, p.drop_radius, hi)) return kNever;

    const double scale = std::abs(a.c0.x) + std::abs(a.c0.y) + std::abs(p.drop_centre.x) + std::abs(p.drop_centre.y);
    const GapPoly f = distance_poly(ex, ey, a.c1.x, a.c1.y, a.c2.x, a.c2.y, p.drop_radius, scale);
    return first_contact(f, hi, -std::numeric_limits<double>::infinity(), [](double) { return true; });
}

}  // namespace ofs::phys
