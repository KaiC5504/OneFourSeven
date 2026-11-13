#include "ofs/physics/record_query.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "ofs/physics/motion.hpp"

namespace ofs::phys {

namespace {

const std::vector<Segment>* segments_of(const ShotRecord& r, int ball) {
    if (ball < 0 || static_cast<std::size_t>(ball) >= r.segments.size()) return nullptr;
    const std::vector<Segment>& segs = r.segments[static_cast<std::size_t>(ball)];
    return segs.empty() ? nullptr : &segs;
}

// evolve() is defined only up to the segment's transition, and the simulator may start the next
// segment a hair after it (simultaneous events resolve in turn).
BallState evolve_within(const ShotRecord& r, const Segment& s, double dt) {
    dt = std::min(std::max(dt, 0.0), transition_time(s.start, r.params, s.nap));
    return evolve(s.start, dt, r.params, s.nap);
}

// The ball on the segment it was on before time t: the latest with t0 < t, or the first when none
// starts before t (the strike at t = 0).
bool state_before(const ShotRecord& r, int ball, double t, BallState& out) {
    const std::vector<Segment>* segs = segments_of(r, ball);
    if (segs == nullptr) return false;
    const auto after = std::lower_bound(segs->begin(), segs->end(), t,
                                        [](const Segment& s, double time) { return s.t0 < time; });
    const Segment& s = after == segs->begin() ? segs->front() : *std::prev(after);
    out = evolve_within(r, s, t - s.t0);
    return true;
}

double dot_xy(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y; }

bool translates(Motion m) { return m == Motion::sliding || m == Motion::rolling; }

table::Vec2 xy(const Vec3& v) { return {v.x, v.y}; }

// Interior samples a segment of duration d gets at a step: enough that none is more than step apart.
std::size_t interior_count(double d, double step) {
    if (!(d > 0.0)) return 0;
    const double pieces = std::ceil(d / step);
    if (!(pieces > 1.0)) return 0;
    // A step near zero during the search asks for absurd counts; capped so the cast stays defined.
    return pieces > 1e15 ? static_cast<std::size_t>(1e15) : static_cast<std::size_t>(pieces) - 1;
}

}  // namespace

double event_impact_speed(const ShotRecord& r, std::size_t i, const TableShape& t) {
    if (i >= r.events.size()) return 0.0;
    const ShotEvent& e = r.events[i];
    if (e.kind == EventKind::strike) return norm_xy(r.strike.v);

    BallState a;
    if (e.kind != EventKind::ball_ball && e.kind != EventKind::ball_line && e.kind != EventKind::ball_arc &&
        e.kind != EventKind::pocket) {
        return 0.0;
    }
    if (!state_before(r, e.a, e.t, a)) return 0.0;

    switch (e.kind) {
        case EventKind::ball_ball: {
            BallState b;
            if (!state_before(r, e.b, e.t, b)) return 0.0;
            const Vec3 n = unit_or_zero(Vec3{b.r.x - a.r.x, b.r.y - a.r.y, 0.0});
            return std::max(0.0, dot_xy(a.v - b.v, n));
        }
        case EventKind::ball_line: {
            if (e.body < 0 || static_cast<std::size_t>(e.body) >= t.lines.size()) return 0.0;
            const table::Vec2 n = t.lines[static_cast<std::size_t>(e.body)].normal;
            return std::max(0.0, -(a.v.x * n.x + a.v.y * n.y));
        }
        case EventKind::ball_arc: {
            if (e.body < 0 || static_cast<std::size_t>(e.body) >= t.arcs.size()) return 0.0;
            const table::Vec2 c = t.arcs[static_cast<std::size_t>(e.body)].centre;
            const Vec3 n = unit_or_zero(Vec3{a.r.x - c.x, a.r.y - c.y, 0.0});
            return std::max(0.0, -dot_xy(a.v, n));
        }
        case EventKind::pocket: return norm_xy(a.v);
        default: return 0.0;
    }
}

std::vector<table::Vec2> sample_path(const ShotRecord& r, int ball, double max_step_s, std::size_t max_points) {
    std::vector<table::Vec2> out;
    const std::vector<Segment>* segs = segments_of(r, ball);
    if (segs == nullptr || max_points == 0) return out;
    const std::size_t n = segs->size();
    const table::Vec2 final_position = static_cast<std::size_t>(ball) < r.final_state.balls.size()
                                           ? xy(r.final_state.balls[static_cast<std::size_t>(ball)].r)
                                           : xy(segs->back().start.r);

    // How long each segment moves the ball: up to the next segment, and for the last one up to its
    // own transition or the shot's end, whichever is first.
    std::vector<double> moving(n, 0.0);
    double total_moving = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        const Segment& s = (*segs)[k];
        if (!translates(s.start.motion)) continue;
        double end = 0.0;
        if (k + 1 < n) {
            end = (*segs)[k + 1].t0;
        } else {
            end = std::min(s.t0 + transition_time(s.start, r.params, s.nap), r.t_end);
        }
        moving[k] = std::isfinite(end) ? std::max(0.0, end - s.t0) : 0.0;
        total_moving += moving[k];
    }

    if (n + 1 > max_points) {
        out.reserve(max_points);
        for (std::size_t k = 0; k + 1 < max_points; ++k) out.push_back(xy((*segs)[k].start.r));
        out.push_back(max_points == 1 ? xy(segs->front().start.r) : final_position);
        return out;
    }

    const std::size_t budget = max_points - n - 1;
    const auto interior_total = [&](double step) {
        std::size_t sum = 0;
        for (std::size_t k = 0; k < n && sum <= budget; ++k) sum += interior_count(moving[k], step);
        return sum;
    };
    double step = 0.0;
    if (budget > 0 && total_moving > 0.0) {
        // total / budget always fits: each segment takes fewer than moving / step samples.
        const double widest = total_moving / static_cast<double>(budget);
        if (max_step_s > 0.0 && max_step_s >= widest) {
            step = max_step_s;
        } else if (max_step_s > 0.0 && interior_total(max_step_s) <= budget) {
            step = max_step_s;
        } else {
            double lo = max_step_s > 0.0 ? max_step_s : 0.0;
            double hi = widest;
            for (int iter = 0; iter < 64 && hi - lo > hi * 1e-12; ++iter) {
                const double mid = lo + 0.5 * (hi - lo);
                if (mid > 0.0 && interior_total(mid) <= budget) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            step = hi;
        }
    }

    out.reserve(n + 1 + (step > 0.0 ? interior_total(step) : 0));
    for (std::size_t k = 0; k < n; ++k) {
        const Segment& s = (*segs)[k];
        out.push_back(xy(s.start.r));
        const std::size_t inside = step > 0.0 ? interior_count(moving[k], step) : 0;
        for (std::size_t j = 1; j <= inside; ++j) {
            const double dt = moving[k] * static_cast<double>(j) / static_cast<double>(inside + 1);
            out.push_back(xy(evolve_within(r, s, dt).r));
        }
    }
    out.push_back(final_position);
    return out;
}

}  // namespace ofs::phys
