// The fuzz's per-shot checks (spec 7.11 C12, C14), evaluated from the record's own segments at every
// event time and along every segment between them, independently of the simulator's guards.

#include <algorithm>
#include <cmath>

#include "ofs/math/roots.hpp"
#include "ofs/physics/motion.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

constexpr double kCushionTolerance = 1e-9;  // m: into a cushion by more than this is through it
// Rounding in a closed-form evaluation is ~1e-16 relative; anything near 1e-9 is a real gain.
constexpr double kEnergyRelative = 1e-9;
constexpr double kEnergyAbsolute = 1e-12;  // J

// The first occurrence of a kind of failure in full, and how many more there were.
struct Finding {
    std::string kind;
    std::string first;
    int count = 0;

    void note(const std::string& detail) {
        if (count++ == 0) first = detail;
    }
};

bool potted(const phys::BallState& b) { return b.motion == phys::Motion::pocketed; }

struct Box {
    double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0;

    bool meets(const Box& o) const { return x0 <= o.x1 && o.x0 <= x1 && y0 <= o.y1 && o.y0 <= y1; }
};

Box box_around(ofs::table::Vec2 c, double r) { return {c.x - r, c.x + r, c.y - r, c.y + r}; }

// One segment's centre path, r(t) = c0 + c1 t + c2 t^2 for t in [0, span].
struct Path {
    phys::Trajectory traj;
    double span = 0.0;

    // The range of c0 + c1 t + c2 t^2 over [0, span]: its ends and its turning point.
    void range(double c0, double c1, double c2, double& lo, double& hi) const {
        const double end = c0 + (c1 + c2 * span) * span;
        lo = std::min(c0, end);
        hi = std::max(c0, end);
        if (c2 == 0.0) return;
        const double t = -c1 / (2.0 * c2);
        if (t > 0.0 && t < span) {
            const double turn = c0 + (c1 + c2 * t) * t;
            lo = std::min(lo, turn);
            hi = std::max(hi, turn);
        }
    }

    Box box() const {
        Box b;
        range(traj.c0.x, traj.c1.x, traj.c2.x, b.x0, b.x1);
        range(traj.c0.y, traj.c1.y, traj.c2.y, b.y0, b.y1);
        return b;
    }

    void keep(const double* roots, int n, std::vector<double>& out) const {
        for (int k = 0; k < n; ++k) {
            if (roots[k] > 0.0 && roots[k] < span) out.push_back(roots[k]);
        }
    }

    // Where (nx, ny) . (r(t) - origin) = offset.
    void line_crossings(double nx, double ny, ofs::table::Vec2 origin, double offset, std::vector<double>& out) const {
        double roots[2];
        const int n = ofs::math::solve_quadratic(nx * traj.c2.x + ny * traj.c2.y, nx * traj.c1.x + ny * traj.c1.y,
                                                 nx * (traj.c0.x - origin.x) + ny * (traj.c0.y - origin.y) - offset,
                                                 roots);
        keep(roots, n, out);
    }

    // Where |r(t) - centre| = radius.
    void circle_crossings(ofs::table::Vec2 centre, double radius, std::vector<double>& out) const {
        const Vec3& c1 = traj.c1;
        const Vec3& c2 = traj.c2;
        const double ex = traj.c0.x - centre.x;
        const double ey = traj.c0.y - centre.y;
        double roots[4];
        const int n = ofs::math::solve_quartic(c2.x * c2.x + c2.y * c2.y, 2.0 * (c1.x * c2.x + c1.y * c2.y),
                                               c1.x * c1.x + c1.y * c1.y + 2.0 * (ex * c2.x + ey * c2.y),
                                               2.0 * (ex * c1.x + ey * c1.y), ex * ex + ey * ey - radius * radius,
                                               roots);
        keep(roots, n, out);
    }
};

// Every time in (0, span) at which one of PlayArea::violation's cushion, jaw and drop circle tests
// can change its answer along the path: the distance from a cushion line, or the foot of the
// perpendicular to it, or the distance from a jaw's or a drop circle's centre, or the bearing from
// a jaw's centre reaching the limit the test compares it with. Between two neighbouring times each
// test keeps one answer, so testing one point of each gap covers the whole path at any speed.
// Bodies whose box the path's box, grown by R, misses can't be touched and are skipped.
std::vector<double> boundary_times(const Path& path, const phys::TableShape& t) {
    std::vector<double> times{0.0, path.span};
    Box reach = path.box();
    reach.x0 -= kR;
    reach.x1 += kR;
    reach.y0 -= kR;
    reach.y1 += kR;
    for (const phys::LineCushion& line : t.lines) {
        const Box near{std::min(line.p1.x, line.p2.x), std::max(line.p1.x, line.p2.x), std::min(line.p1.y, line.p2.y),
                       std::max(line.p1.y, line.p2.y)};
        if (!reach.meets(near)) continue;
        const double ux = line.p2.x - line.p1.x;
        const double uy = line.p2.y - line.p1.y;
        const double length = std::sqrt(ux * ux + uy * uy);
        if (!(length > 0.0)) continue;
        path.line_crossings(line.normal.x, line.normal.y, line.p1, kR - kCushionTolerance, times);
        path.line_crossings(ux / length, uy / length, line.p1, 0.0, times);
        path.line_crossings(ux / length, uy / length, line.p1, length, times);
    }
    for (const phys::ArcCushion& arc : t.arcs) {
        if (!reach.meets(box_around(arc.centre, arc.radius))) continue;
        path.circle_crossings(arc.centre, arc.radius - kCushionTolerance, times);
        path.circle_crossings(arc.centre, arc.radius + kR - kCushionTolerance, times);
        for (double angle : {arc.angle_start, arc.angle_end}) {
            path.line_crossings(-std::sin(angle), std::cos(angle), arc.centre, 0.0, times);
        }
    }
    for (const phys::Pocket& pocket : t.pockets) {
        if (!reach.meets(box_around(pocket.drop_centre, std::max(0.0, pocket.drop_radius - kR)))) continue;
        path.circle_crossings(pocket.drop_centre, pocket.drop_radius - kCushionTolerance, times);
    }
    std::sort(times.begin(), times.end());
    return times;
}

}  // namespace

std::vector<ShotFailure> check_record(const phys::SimInput& in, const phys::ShotRecord& r,
                                      const PlayArea& area) {
    std::vector<ShotFailure> out;
    for (const phys::Anomaly& a : r.anomalies) {
        if (a.kind != "solver_fallback") out.push_back({"anomaly:" + a.kind, anomaly_text(a)});
    }
    const std::size_t n = in.start.balls.size();
    if (r.segments.size() != n || r.final_state.balls.size() != n) {
        out.push_back({"record", format("%zu balls in, %zu segment lists and %zu final balls out", n,
                                        r.segments.size(), r.final_state.balls.size())});
        return out;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (r.segments[i].empty() || r.segments[i].front().t0 != 0.0) {
            out.push_back({"record", format("ball %zu's segments don't start at t = 0", i)});
            return out;
        }
    }

    Finding nan{"nan"};
    Finding overlap{"overlap"};
    Finding tunnel{"tunnel"};
    Finding energy{"energy_gain"};
    Finding order{"record"};
    Finding rest{"not_at_rest"};

    std::vector<double> times{0.0};
    double previous_t = 0.0;
    for (std::size_t k = 0; k < r.events.size(); ++k) {
        const double t = r.events[k].t;
        if (!std::isfinite(t)) {
            nan.note(format("event %zu has t = %g", k, t));
            continue;
        }
        if (t < previous_t) {
            order.note(format("event %zu at t=%.9g s comes after one at %.9g s", k, t, previous_t));
        }
        previous_t = std::max(previous_t, t);
        times.push_back(t);
    }
    if (std::isfinite(r.t_end)) {
        times.push_back(r.t_end);
    } else {
        nan.note(format("t_end = %g", r.t_end));
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    std::vector<phys::BallState> states(n);
    double previous_energy = -1.0;
    double previous_energy_t = 0.0;
    const auto check_state = [&](const std::vector<phys::BallState>& balls, const char* when) {
        for (std::size_t i = 0; i < n; ++i) {
            if (!finite(balls[i])) {
                nan.note(format("%s (%zu) is not finite %s", ball_label(balls[i]).c_str(), i, when));
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (potted(balls[i]) || !finite(balls[i])) continue;
            for (std::size_t j = i + 1; j < n; ++j) {
                if (potted(balls[j]) || !finite(balls[j])) continue;
                const double d = planar_distance(balls[i].r, balls[j].r);
                if (d < 2.0 * kR - phys::kOverlapTolerance_m) {
                    overlap.note(
                        format("balls %zu and %zu are %.12g m apart (2R = %.12g) %s", i, j, d, 2.0 * kR, when));
                }
            }
            const std::string where = area.violation(balls[i].r, kCushionTolerance);
            if (!where.empty()) {
                tunnel.note(format("%s (%zu) %s %s", ball_label(balls[i]).c_str(), i, where.c_str(), when));
            }
        }
    };

    for (double t : times) {
        for (std::size_t i = 0; i < n; ++i) states[i] = state_after(r, static_cast<int>(i), t);
        const std::string when = format("at t=%.9g s", t);
        check_state(states, when.c_str());
        double total = 0.0;
        for (const phys::BallState& b : states) {
            if (!potted(b)) total += phys::kinetic_energy(b, r.params);
        }
        if (previous_energy >= 0.0 && total > previous_energy * (1.0 + kEnergyRelative) + kEnergyAbsolute) {
            energy.note(format("total kinetic energy rose from %.12g J at t=%.9g s to %.12g J at t=%.9g s",
                               previous_energy, previous_energy_t, total, t));
        }
        if (std::isfinite(total)) {
            previous_energy = total;
            previous_energy_t = t;
        }
    }

    // A ball that passes through a jaw or a cushion and then drops looks legal at every event time.
    for (std::size_t i = 0; i < n; ++i) {
        const std::vector<phys::Segment>& segments = r.segments[i];
        for (std::size_t k = 0; k < segments.size(); ++k) {
            const phys::Segment& s = segments[k];
            const phys::Motion m = s.start.motion;
            if (m == phys::Motion::stationary || m == phys::Motion::spinning || m == phys::Motion::pocketed ||
                !finite(s.start)) {
                continue;
            }
            const double t1 = k + 1 < segments.size() ? segments[k + 1].t0 : r.t_end;
            Path path{phys::trajectory(s.start, r.params, s.nap), 0.0};
            path.span = std::min(t1 - s.t0, path.traj.duration);
            if (!(path.span > 0.0)) continue;
            const std::vector<double> crossings = boundary_times(path, area.table());
            for (std::size_t j = 0; j + 1 < crossings.size(); ++j) {
                if (!(crossings[j + 1] > crossings[j])) continue;
                const double dt = 0.5 * (crossings[j] + crossings[j + 1]);
                const phys::BallState b = phys::evolve(s.start, dt, r.params, s.nap);
                const std::string where = area.violation(b.r, kCushionTolerance);
                if (where.empty()) continue;
                tunnel.note(format("%s (%zu) %s at t=%.9g s, between its events at %.9g and %.9g s",
                                   ball_label(b).c_str(), i, where.c_str(), s.t0 + dt, s.t0, t1));
                break;
            }
        }
    }

    check_state(r.final_state.balls, "in the final state");
    for (std::size_t i = 0; i < n; ++i) {
        const phys::BallState& b = r.final_state.balls[i];
        const bool at_rest = b.motion == phys::Motion::pocketed ||
                             (b.motion == phys::Motion::stationary && norm2(b.v) == 0.0 && norm2(b.w) == 0.0);
        if (!at_rest) {
            rest.note(format("%s (%zu) ends %s with |v| %.3g m/s, |w| %.3g rad/s", ball_label(b).c_str(), i,
                             std::string(phys::motion_name(b.motion)).c_str(), norm(b.v), norm(b.w)));
        }
    }

    for (const Finding* f : {&nan, &overlap, &tunnel, &energy, &order, &rest}) {
        if (f->count == 0) continue;
        out.push_back({f->kind, f->count > 1 ? f->first + format(" (and %d more)", f->count - 1) : f->first});
    }
    return out;
}

}  // namespace sim_cli
