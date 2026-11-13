#include "ofs/physics/record_query.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include "ofs/physics/collide.hpp"
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

double dot_xy(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y; }

// Only a zero_time_chain anomaly runs longer; replaying all of one for each of its events would
// cost the square of the chain.
constexpr std::size_t kMaxReplay = 64;

bool restarted_at_zero(const ShotRecord& r, int ball) {
    for (const ShotEvent& e : r.events) {
        if (e.t != 0.0) break;
        if (e.kind != EventKind::strike && (e.a == ball || e.b == ball)) return true;
    }
    return false;
}

// The ball at time t before any event at t: on the latest segment with t0 < t. The simulator keeps
// one segment per ball per instant, so at t = 0 the first segment is whatever the last event there
// left; a ball some event touched at 0 is rebuilt, the cue ball as the strike left it, any other
// at rest as the shot began.
bool state_entering(const ShotRecord& r, int ball, double t, BallState& out) {
    const std::vector<Segment>* segs = segments_of(r, ball);
    if (segs == nullptr) return false;
    const auto after = std::lower_bound(segs->begin(), segs->end(), t,
                                        [](const Segment& s, double time) { return s.t0 < time; });
    if (after != segs->begin()) {
        const Segment& s = *std::prev(after);
        out = evolve_within(r, s, t - s.t0);
        return true;
    }
    out = segs->front().start;
    if (!restarted_at_zero(r, ball)) return true;
    out.v = {};
    out.w = {};
    out.motion = Motion::stationary;
    out.pocket = -1;
    if (r.has_strike && !r.events.empty() && r.events.front().kind == EventKind::strike &&
        r.events.front().a == ball) {
        out.v = r.strike.v;
        out.w = r.strike.w;
        out.motion = classify(out, r.params);
    }
    return true;
}

// Event i's balls just before it. A ball that took several events at one instant (a chain through
// a frozen pair) has no segment for the moments between them, so the events before i at its
// instant are replayed from the state entering it, with the simulator's responses.
class InstantReplay {
public:
    InstantReplay(const ShotRecord& r, std::size_t i, const TableShape& table) : r_(r), t_(r.events[i].t) {
        std::size_t first = i;
        while (first > 0 && i - first < kMaxReplay && r.events[first - 1].t == t_) --first;
        known_.reserve(2 * (i - first) + 2);
        for (std::size_t j = first; j < i; ++j) apply(r.events[j], table);
    }

    BallState* state(int ball) {
        for (auto& [k, s] : known_) {
            if (k == ball) return &s;
        }
        BallState s;
        if (!state_entering(r_, ball, t_, s)) return nullptr;
        known_.emplace_back(ball, s);
        return &known_.back().second;
    }

private:
    void apply(const ShotEvent& e, const TableShape& table) {
        const PhysicsParams& p = r_.params;
        const double rest = p.ball_rest_speed_m_s;
        switch (e.kind) {
            case EventKind::ball_ball: {
                BallState* a = state(e.a);
                BallState* b = state(e.b);
                if (a == nullptr || b == nullptr || a == b) return;
                const Vec3 n = unit_or_zero(Vec3{b->r.x - a->r.x, b->r.y - a->r.y, 0.0});
                // Closing slower than that, the simulator holds the pair as a contact instead.
                if (dot_xy(a->v - b->v, n) >= rest) collide_balls(*a, *b, p);
                return;
            }
            case EventKind::ball_line: {
                BallState* a = state(e.a);
                if (a == nullptr || e.body < 0 || static_cast<std::size_t>(e.body) >= table.lines.size()) return;
                const LineCushion& c = table.lines[static_cast<std::size_t>(e.body)];
                if (-(a->v.x * c.normal.x + a->v.y * c.normal.y) >= rest) collide_line(*a, c, p);
                return;
            }
            case EventKind::ball_arc: {
                BallState* a = state(e.a);
                if (a == nullptr || e.body < 0 || static_cast<std::size_t>(e.body) >= table.arcs.size()) return;
                const ArcCushion& c = table.arcs[static_cast<std::size_t>(e.body)];
                const Vec3 n = unit_or_zero(Vec3{a->r.x - c.centre.x, a->r.y - c.centre.y, 0.0});
                if (-dot_xy(a->v, n) >= rest) collide_arc(*a, c, p);
                return;
            }
            case EventKind::pocket: {
                BallState* a = state(e.a);
                if (a == nullptr) return;
                a->motion = Motion::pocketed;
                a->pocket = e.body;
                a->v = {};
                a->w = {};
                return;
            }
            default: return;
        }
    }

    const ShotRecord& r_;
    double t_;
    std::vector<std::pair<int, BallState>> known_;
};

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

    if (e.kind != EventKind::ball_ball && e.kind != EventKind::ball_line && e.kind != EventKind::ball_arc &&
        e.kind != EventKind::pocket) {
        return 0.0;
    }
    InstantReplay replay(r, i, t);
    const BallState* a_state = replay.state(e.a);
    if (a_state == nullptr) return 0.0;
    const BallState a = *a_state;

    switch (e.kind) {
        case EventKind::ball_ball: {
            const BallState* b_state = replay.state(e.b);
            if (b_state == nullptr) return 0.0;
            const BallState b = *b_state;
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
