#include "ofs/physics/simulator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/physics/collide.hpp"
#include "ofs/physics/cue.hpp"
#include "ofs/physics/detect.hpp"
#include "ofs/physics/motion.hpp"
#include "ofs/physics/nap.hpp"
#include "ofs/physics/repro.hpp"
#include "ofs/rng.hpp"

namespace ofs::phys {

namespace {

constexpr double R = table::kBallRadius;
// Contacts come out of the root solver exact to rounding, and simultaneous events resolved in
// turn leave at most speed * zero_time_s (about 1e-8 m) of overlap. Beyond this it is real.
constexpr double kOverlapTolerance = 1e-7;
// Slack on the bounding boxes that cull detection; far above the rounding of the box arithmetic.
constexpr double kBoxMargin = 1e-6;
// A shot that trips a check at every event would otherwise grow its record without bound.
constexpr std::size_t kMaxRecordedAnomalies = 256;
constexpr std::size_t kReserveEvents = 512;
constexpr std::size_t kReserveSegments = 32;

enum class AnomalyKind : std::uint8_t {
    event_guard,
    zero_time_chain,
    time_cap,
    solver_fallback,
    energy_gain,
    overlap,
    nan
};
constexpr std::array<const char*, 7> kAnomalyNames = {"event_guard", "zero_time_chain", "time_cap", "solver_fallback",
                                                      "energy_gain", "overlap",         "nan"};

std::string fmt(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(const BallState& b) { return finite(b.r) && finite(b.v) && finite(b.w); }

void put_at_rest(BallState& b) {
    b.v = {};
    b.w = {};
    if (b.motion != Motion::pocketed) b.motion = Motion::stationary;
}

// Where the ball's centre can be during a segment: detection is skipped for bodies outside it.
struct Box {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
};

constexpr Box kEverywhere{-kNever, -kNever, kNever, kNever};

bool boxes_meet(const Box& a, const Box& b, double pad) {
    return a.x0 - pad <= b.x1 && b.x0 - pad <= a.x1 && a.y0 - pad <= b.y1 && b.y0 - pad <= a.y1;
}

void axis_range(double c0, double c1, double c2, double T, double& lo, double& hi) {
    const double end = c0 + c1 * T + c2 * (T * T);
    lo = std::min(c0, end);
    hi = std::max(c0, end);
    if (c2 != 0.0) {
        const double tv = -c1 / (2.0 * c2);
        if (tv > 0.0 && tv < T) {
            const double xv = c0 + c1 * tv + c2 * (tv * tv);
            lo = std::min(lo, xv);
            hi = std::max(hi, xv);
        }
    }
}

Box path_box(const Trajectory& path, double T) {
    if (path.c1.x == 0.0 && path.c1.y == 0.0 && path.c2.x == 0.0 && path.c2.y == 0.0) {
        return {path.c0.x, path.c0.y, path.c0.x, path.c0.y};
    }
    if (!(T < kNever)) return kEverywhere;
    Box b;
    axis_range(path.c0.x, path.c1.x, path.c2.x, T, b.x0, b.x1);
    axis_range(path.c0.y, path.c1.y, path.c2.y, T, b.y0, b.y1);
    // NaN coefficients would make every comparison false and cull real events.
    if (!(b.x0 <= b.x1 && b.y0 <= b.y1)) return kEverywhere;
    return b;
}

Box circle_box(table::Vec2 c, double radius) { return {c.x - radius, c.y - radius, c.x + radius, c.y + radius}; }

struct Live {
    double t0 = 0.0;
    BallState start;
    Vec3 nap;
    Trajectory path;
    double transition_dt = kNever;  // from t0
    double own_dt = kNever;         // from t0: the earlier of the transition and the nap re-evaluation
    EventKind own_kind = EventKind::transition;
    double end = kNever;  // t0 + own_dt: the segment is valid until then
    Box box;
    bool active = false;  // on the table
    bool moves = false;   // the centre moves during the segment
};

struct Next {
    double t = kNever;
    EventKind kind = EventKind::guard;
    int a = -1;
    int b = -1;
    int body = -1;
};

class Simulation {
public:
    Simulation(const SimInput& in, const TableShape& table, const PhysicsParams& p, ShotRecord& r)
        : in_(in), table_(table), p_(p), r_(r), n_(static_cast<int>(in.start.balls.size())) {}

    void run();
    void log_anomalies() const;

private:
    void setup();
    void start_segment(int i, double t, const BallState& state);
    BallState state_at(int i, double t, bool own_event) const;
    Vec3 centre_at(int i, double t) const;
    Trajectory rebased(int i) const;
    double checked(double t_rel, double window, EventKind kind, int a, int b, int body);
    double checked_dt(double dt, int i, const char* what);
    double pair_time(int i, int j);
    void refresh_pair(int i, int j) { pair_t_[pair_index(i, j)] = pair_time(std::min(i, j), std::max(i, j)); }
    void refresh_cushion(int i);
    std::size_t pair_index(int i, int j) const {
        return i < j ? static_cast<std::size_t>(row_[i] + j) : static_cast<std::size_t>(row_[j] + i);
    }
    bool next_event(Next& out) const;
    void resolve(const Next& e, double t);
    void keep_finite(int i, BallState& b, const BallState& fallback, double t, const char* when);
    void separate(int i, BallState& b, double t, int other, const BallState* other_state);
    void stop_all(double t, AnomalyKind kind, std::vector<int> balls, std::string detail);
    bool anyone_moving() const;
    void anomaly(AnomalyKind kind, double t, std::vector<int> balls, std::string detail);

    const SimInput& in_;
    const TableShape& table_;
    const PhysicsParams& p_;
    ShotRecord& r_;
    const int n_;
    double now_ = 0.0;

    std::vector<Live> live_;
    std::vector<int> row_;           // pair (i, j), i < j, sits at pair_t_[row_[i] + j]
    std::vector<double> pair_t_;     // next contact of each pair, absolute
    std::vector<double> own_t_;      // next transition or nap re-evaluation of each ball, absolute
    std::vector<double> cushion_t_;  // next cushion, jaw or pocket of each ball, absolute
    std::vector<EventKind> cushion_kind_;
    std::vector<int> cushion_body_;
    std::vector<Box> line_box_, arc_box_, pocket_box_;
    std::array<std::size_t, kAnomalyNames.size()> anomaly_count_{};
};

void Simulation::anomaly(AnomalyKind kind, double t, std::vector<int> balls, std::string detail) {
    const auto k = static_cast<std::size_t>(kind);
    if (anomaly_count_[k]++ == 0 || r_.anomalies.size() < kMaxRecordedAnomalies) {
        r_.anomalies.push_back({kAnomalyNames[k], t, std::move(balls), std::move(detail)});
    }
}

// Logged after the shot so the log's cost stays out of sim_us. One line per kind with the repro
// bundle is enough to find the rest by replaying it.
void Simulation::log_anomalies() const {
    if (r_.anomalies.empty()) return;
    const nlohmann::json bundle = make_bundle(in_, p_);
    std::array<bool, kAnomalyNames.size()> logged{};
    for (const Anomaly& a : r_.anomalies) {
        std::size_t k = 0;
        while (k < kAnomalyNames.size() && a.kind != kAnomalyNames[k]) ++k;
        if (k == kAnomalyNames.size() || logged[k]) continue;
        logged[k] = true;
        std::string detail = a.detail;
        if (anomaly_count_[k] > 1) detail += " (" + std::to_string(anomaly_count_[k]) + " in this shot)";
        log::warn("physics", "physics.anomaly",
                  {{"kind", a.kind}, {"t", a.t}, {"balls", a.balls}, {"detail", detail}, {"bundle", bundle}});
    }
}

double Simulation::checked_dt(double dt, int i, const char* what) {
    if (std::isnan(dt) || dt < -p_.zero_time_s) {
        anomaly(AnomalyKind::solver_fallback, now_, {i}, std::string(what) + " returned " + fmt(dt) + "; ignored");
        return kNever;
    }
    return std::max(dt, 0.0);
}

// A detection result relative to now_, made absolute; kNever when outside the window.
double Simulation::checked(double t_rel, double window, EventKind kind, int a, int b, int body) {
    if (std::isnan(t_rel) || t_rel < -p_.zero_time_s) {
        std::vector<int> balls{a};
        if (b >= 0) balls.push_back(b);
        anomaly(AnomalyKind::solver_fallback, now_, std::move(balls),
                std::string(event_kind_name(kind)) + " detection returned " + fmt(t_rel) + " (body " +
                    std::to_string(body) + "); ignored");
        return kNever;
    }
    if (t_rel > window) return kNever;
    return now_ + std::max(t_rel, 0.0);
}

void Simulation::start_segment(int i, double t, const BallState& state) {
    // classify() calls a ball at rest below its speed tolerance without zeroing the residue a
    // collision left; start such segments clean, as complete_transition() does.
    BallState b = state;
    if (b.motion == Motion::stationary) {
        b.v = {};
        b.w = {};
    } else if (b.motion == Motion::spinning) {
        b.v = {};
        b.w.x = 0.0;
        b.w.y = 0.0;
    }
    Live& L = live_[static_cast<std::size_t>(i)];
    L.t0 = t;
    L.start = b;
    L.active = b.motion != Motion::pocketed;
    L.nap = {};
    L.transition_dt = kNever;
    L.own_dt = kNever;
    L.own_kind = EventKind::transition;
    if (L.active) {
        L.nap = nap_acceleration(b, p_);
        if (!finite(L.nap)) {
            anomaly(AnomalyKind::nan, t, {i}, "nap acceleration not finite; set to zero");
            L.nap = {};
        }
        L.transition_dt = checked_dt(transition_time(b, p_, L.nap), i, "transition_time");
        L.own_dt = L.transition_dt;
        if (b.motion == Motion::rolling) {
            const Vec3 heading = unit_or_zero(Vec3{b.v.x, b.v.y, 0.0});
            const double along_decel = p_.roll_friction * p_.gravity_m_s2 - dot(L.nap, heading);
            const double nap_dt = checked_dt(nap_retick_time(b, p_, along_decel), i, "nap_retick_time");
            if (nap_dt < L.own_dt) {
                L.own_dt = nap_dt;
                L.own_kind = EventKind::nap;
            }
        }
        L.path = trajectory(b, p_, L.nap);
    } else {
        L.path = {b.r, {}, {}, kNever};
    }
    L.end = t + L.own_dt;
    L.moves = L.active && (L.path.c1.x != 0.0 || L.path.c1.y != 0.0 || L.path.c2.x != 0.0 || L.path.c2.y != 0.0);
    L.box = path_box(L.path, std::min(L.end, p_.time_cap_s) - t);
    own_t_[static_cast<std::size_t>(i)] = L.active ? L.end : kNever;

    // A segment that lasted no time at all (simultaneous events) is replaced rather than kept.
    std::vector<Segment>& segs = r_.segments[static_cast<std::size_t>(i)];
    if (!segs.empty() && segs.back().t0 == t) {
        segs.back() = {t, b, L.nap};
    } else {
        segs.push_back({t, b, L.nap});
    }
}

BallState Simulation::state_at(int i, double t, bool own_event) const {
    const Live& L = live_[static_cast<std::size_t>(i)];
    if (!L.active) return L.start;
    // A ball's own event evolves by exactly its own time, not by (t0 + dt) - t0 with rounding.
    const double dt = own_event ? L.own_dt : std::min(std::max(t - L.t0, 0.0), L.transition_dt);
    return evolve(L.start, dt, p_, L.nap);
}

Vec3 Simulation::centre_at(int i, double t) const {
    const Live& L = live_[static_cast<std::size_t>(i)];
    if (!L.moves) return L.start.r;
    return L.path.at(std::min(std::max(t - L.t0, 0.0), L.transition_dt));
}

Trajectory Simulation::rebased(int i) const {
    const Live& L = live_[static_cast<std::size_t>(i)];
    return now_ == L.t0 ? L.path : L.path.shifted(now_ - L.t0);
}

double Simulation::pair_time(int i, int j) {
    const Live& a = live_[static_cast<std::size_t>(i)];
    const Live& b = live_[static_cast<std::size_t>(j)];
    if (!a.active || !b.active || (!a.moves && !b.moves)) return kNever;
    if (!boxes_meet(a.box, b.box, 2.0 * R + kBoxMargin)) return kNever;
    const double window = std::max(0.0, std::min({a.end, b.end, p_.time_cap_s}) - now_);
    return checked(ball_ball_time(rebased(i), rebased(j), R, window), window, EventKind::ball_ball, i, j, -1);
}

void Simulation::refresh_cushion(int i) {
    const auto u = static_cast<std::size_t>(i);
    cushion_t_[u] = kNever;
    cushion_kind_[u] = EventKind::ball_line;
    cushion_body_[u] = -1;
    const Live& L = live_[u];
    if (!L.active || !L.moves) return;
    const double window = std::max(0.0, std::min(L.end, p_.time_cap_s) - now_);
    const Trajectory path = rebased(i);
    // Strictly earlier wins, so exact ties keep line, arc, pocket order and the lower body index.
    auto consider = [&](double t_rel, EventKind kind, int body) {
        const double t = checked(t_rel, window, kind, i, -1, body);
        if (t < cushion_t_[u]) {
            cushion_t_[u] = t;
            cushion_kind_[u] = kind;
            cushion_body_[u] = body;
        }
    };
    for (std::size_t k = 0; k < table_.lines.size(); ++k) {
        if (!boxes_meet(L.box, line_box_[k], kBoxMargin)) continue;
        consider(ball_line_time(path, table_.lines[k], R, window), EventKind::ball_line, static_cast<int>(k));
    }
    for (std::size_t k = 0; k < table_.arcs.size(); ++k) {
        if (!boxes_meet(L.box, arc_box_[k], kBoxMargin)) continue;
        consider(ball_arc_time(path, table_.arcs[k], R, window), EventKind::ball_arc, static_cast<int>(k));
    }
    for (std::size_t k = 0; k < table_.pockets.size(); ++k) {
        if (!boxes_meet(L.box, pocket_box_[k], kBoxMargin)) continue;
        consider(ball_pocket_time(path, table_.pockets[k], window), EventKind::pocket, static_cast<int>(k));
    }
}

// The earliest time over every cache, then, among everything within zero_time_s of it, the lowest
// (kind, a, b, body). Pairs come first (ball_ball is the lowest kind) in (a, b) order, which is
// the packed order of pair_t_.
bool Simulation::next_event(Next& out) const {
    double t_min = kNever;
    for (const double t : pair_t_) t_min = std::min(t_min, t);
    for (const double t : cushion_t_) t_min = std::min(t_min, t);
    for (const double t : own_t_) t_min = std::min(t_min, t);
    if (!(t_min < kNever)) return false;
    const double limit = t_min + p_.zero_time_s;

    for (int i = 0; i < n_; ++i) {
        for (int j = i + 1; j < n_; ++j) {
            const double t = pair_t_[static_cast<std::size_t>(row_[static_cast<std::size_t>(i)] + j)];
            if (t <= limit) {
                out = {t, EventKind::ball_ball, i, j, -1};
                return true;
            }
        }
    }
    for (const EventKind kind : {EventKind::ball_line, EventKind::ball_arc, EventKind::pocket}) {
        for (int i = 0; i < n_; ++i) {
            const auto u = static_cast<std::size_t>(i);
            if (cushion_kind_[u] == kind && cushion_t_[u] <= limit) {
                out = {cushion_t_[u], kind, i, -1, cushion_body_[u]};
                return true;
            }
        }
    }
    for (const EventKind kind : {EventKind::transition, EventKind::nap}) {
        for (int i = 0; i < n_; ++i) {
            const auto u = static_cast<std::size_t>(i);
            if (live_[u].own_kind == kind && own_t_[u] <= limit) {
                out = {own_t_[u], kind, i, -1, -1};
                return true;
            }
        }
    }
    return false;
}

void Simulation::keep_finite(int i, BallState& b, const BallState& fallback, double t, const char* when) {
    if (finite(b)) return;
    anomaly(AnomalyKind::nan, t, {i},
            std::string("ball state not finite ") + when + "; reverted to its last finite state at rest");
    b = fallback;
    put_at_rest(b);
}

// Pushes ball i, which an event just touched, out of any ball it overlaps.
void Simulation::separate(int i, BallState& b, double t, int other, const BallState* other_state) {
    if (b.motion == Motion::pocketed) return;
    for (int j = 0; j < n_; ++j) {
        if (j == i) continue;
        Vec3 rj;
        if (j == other) {
            if (other_state->motion == Motion::pocketed) continue;
            rj = other_state->r;
        } else {
            if (!live_[static_cast<std::size_t>(j)].active) continue;
            rj = centre_at(j, t);
        }
        const Vec3 d{b.r.x - rj.x, b.r.y - rj.y, 0.0};
        const double dist = norm_xy(d);
        if (dist >= 2.0 * R - kOverlapTolerance) continue;
        const Vec3 dir = dist > 0.0 ? d / dist : Vec3{1.0, 0.0, 0.0};
        b.r = {rj.x + dir.x * (2.0 * R), rj.y + dir.y * (2.0 * R), 0.0};
        anomaly(AnomalyKind::overlap, t, {i, j},
                "balls " + std::to_string(i) + " and " + std::to_string(j) + " overlapped by " + fmt(2.0 * R - dist) +
                    " m; ball " + std::to_string(i) + " pushed apart to touching");
    }
}

bool Simulation::anyone_moving() const {
    for (const Live& L : live_) {
        if (L.active && L.start.motion != Motion::stationary) return true;
    }
    return false;
}

void Simulation::stop_all(double t, AnomalyKind kind, std::vector<int> balls, std::string detail) {
    now_ = t;
    for (int i = 0; i < n_; ++i) {
        const Live& L = live_[static_cast<std::size_t>(i)];
        if (!L.active || (L.start.motion == Motion::stationary && !L.moves)) continue;
        BallState b = state_at(i, t, false);
        keep_finite(i, b, L.start, t, "when stopped");
        put_at_rest(b);
        start_segment(i, t, b);
    }
    r_.events.push_back({t, EventKind::guard, -1, -1, -1, Motion::stationary, Motion::stationary});
    anomaly(kind, t, std::move(balls), std::move(detail));
}

void Simulation::resolve(const Next& e, double t) {
    const bool own = e.kind == EventKind::transition || e.kind == EventKind::nap;
    const bool pair = e.b >= 0;
    BallState a = state_at(e.a, t, own);
    BallState b = pair ? state_at(e.b, t, false) : BallState{};
    keep_finite(e.a, a, live_[static_cast<std::size_t>(e.a)].start, t, "after evolve");
    if (pair) keep_finite(e.b, b, live_[static_cast<std::size_t>(e.b)].start, t, "after evolve");
    const BallState a_before = a;
    const BallState b_before = b;
    const double ke_before = kinetic_energy(a, p_) + (pair ? kinetic_energy(b, p_) : 0.0);

    switch (e.kind) {
        case EventKind::ball_ball: collide_balls(a, b, p_); break;
        case EventKind::ball_line: collide_line(a, table_.lines[static_cast<std::size_t>(e.body)], p_); break;
        case EventKind::ball_arc: collide_arc(a, table_.arcs[static_cast<std::size_t>(e.body)], p_); break;
        case EventKind::pocket:
            a.motion = Motion::pocketed;
            a.pocket = e.body;
            a.v = {};
            a.w = {};
            break;
        case EventKind::transition: a = complete_transition(a, p_); break;
        case EventKind::nap:  // the new segment re-evaluates the nap acceleration
        case EventKind::strike:
        case EventKind::guard: break;
    }
    keep_finite(e.a, a, a_before, t, std::string("after ").append(event_kind_name(e.kind)).c_str());
    if (pair) keep_finite(e.b, b, b_before, t, "after ball_ball");

    const double ke_after = kinetic_energy(a, p_) + (pair ? kinetic_energy(b, p_) : 0.0);
    if (ke_after - ke_before > 1e-9 * ke_before + 1e-12) {
        std::vector<int> balls{e.a};
        if (pair) balls.push_back(e.b);
        anomaly(AnomalyKind::energy_gain, t, std::move(balls),
                "kinetic energy rose from " + fmt(ke_before) + " J to " + fmt(ke_after) + " J at a " +
                    std::string(event_kind_name(e.kind)) + " event (body " + std::to_string(e.body) + ")");
    }

    r_.events.push_back({t, e.kind, e.a, e.b, e.body, a_before.motion, a.motion});

    separate(e.a, a, t, e.b, pair ? &b : nullptr);
    if (pair) separate(e.b, b, t, e.a, &a);

    start_segment(e.a, t, a);
    if (pair) start_segment(e.b, t, b);
    refresh_cushion(e.a);
    if (pair) refresh_cushion(e.b);
    for (int j = 0; j < n_; ++j) {
        if (j != e.a) refresh_pair(e.a, j);
    }
    if (pair) {
        for (int j = 0; j < n_; ++j) {
            if (j != e.a && j != e.b) refresh_pair(e.b, j);
        }
    }
}

void Simulation::setup() {
    const auto n = static_cast<std::size_t>(n_);
    r_.params = p_;
    r_.has_strike = in_.has_strike;
    r_.segments.assign(n, {});
    for (std::vector<Segment>& segs : r_.segments) segs.reserve(kReserveSegments);
    r_.events.reserve(kReserveEvents);

    live_.assign(n, Live{});
    own_t_.assign(n, kNever);
    cushion_t_.assign(n, kNever);
    cushion_kind_.assign(n, EventKind::ball_line);
    cushion_body_.assign(n, -1);
    row_.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        // Packed upper triangle: row i starts after the n-1 + n-2 + ... + n-i entries of rows < i.
        row_[i] = static_cast<int>(i * (2 * n - i - 1) / 2) - static_cast<int>(i) - 1;
    }
    pair_t_.assign(n * (n > 0 ? n - 1 : 0) / 2, kNever);

    for (const LineCushion& c : table_.lines) {
        line_box_.push_back({std::min(c.p1.x, c.p2.x) - R, std::min(c.p1.y, c.p2.y) - R, std::max(c.p1.x, c.p2.x) + R,
                             std::max(c.p1.y, c.p2.y) + R});
    }
    for (const ArcCushion& c : table_.arcs) arc_box_.push_back(circle_box(c.centre, c.radius + R));
    for (const Pocket& pk : table_.pockets) pocket_box_.push_back(circle_box(pk.drop_centre, pk.drop_radius));

    TableState s = in_.start;
    for (int i = 0; i < n_; ++i) {
        BallState& b = s.balls[static_cast<std::size_t>(i)];
        if (!finite(b)) {
            for (double* c : {&b.r.x, &b.r.y, &b.r.z}) {
                if (!std::isfinite(*c)) *c = 0.0;
            }
            put_at_rest(b);
            anomaly(AnomalyKind::nan, 0.0, {i}, "start state not finite; non-finite position set to 0, ball at rest");
        }
        if (b.motion != Motion::pocketed) b.motion = classify(b, p_);
    }
    for (int i = 0; i < n_; ++i) {
        const BallState& a = s.balls[static_cast<std::size_t>(i)];
        if (a.motion == Motion::pocketed) continue;
        for (int j = i + 1; j < n_; ++j) {
            BallState& b = s.balls[static_cast<std::size_t>(j)];
            if (b.motion == Motion::pocketed) continue;
            const Vec3 d{b.r.x - a.r.x, b.r.y - a.r.y, 0.0};
            const double dist = norm_xy(d);
            if (dist >= 2.0 * R - kOverlapTolerance) continue;
            const Vec3 dir = dist > 0.0 ? d / dist : Vec3{1.0, 0.0, 0.0};
            b.r = {a.r.x + dir.x * (2.0 * R), a.r.y + dir.y * (2.0 * R), 0.0};
            anomaly(AnomalyKind::overlap, 0.0, {i, j},
                    "balls " + std::to_string(i) + " and " + std::to_string(j) + " overlap by " + fmt(2.0 * R - dist) +
                        " m in the start state; ball " + std::to_string(j) + " pushed apart to touching");
        }
    }

    if (in_.has_strike) {
        const bool valid = in_.cue_ball >= 0 && in_.cue_ball < n_ &&
                           s.balls[static_cast<std::size_t>(in_.cue_ball)].motion != Motion::pocketed;
        OFS_ASSERT(valid && "simulate: cue_ball must index a ball on the table");
        if (valid) {
            Rng rng(in_.seed);
            r_.strike = strike(s, in_.cue_ball, in_.strike, table_, p_, rng);
            BallState& c = s.balls[static_cast<std::size_t>(in_.cue_ball)];
            const BallState before = c;
            c.v = r_.strike.v;
            c.w = r_.strike.w;
            keep_finite(in_.cue_ball, c, before, 0.0, "after the strike");
            c.motion = classify(c, p_);
            r_.events.push_back({0.0, EventKind::strike, in_.cue_ball, -1, -1, before.motion, c.motion});
        }
    }

    for (int i = 0; i < n_; ++i) start_segment(i, 0.0, s.balls[static_cast<std::size_t>(i)]);
    for (int i = 0; i < n_; ++i) refresh_cushion(i);
    for (int i = 0; i < n_; ++i) {
        for (int j = i + 1; j < n_; ++j) refresh_pair(i, j);
    }
}

void Simulation::run() {
    setup();
    bool chained = !r_.events.empty();
    int chain = chained ? 1 : 0;
    double last_t = 0.0;
    for (;;) {
        Next e;
        const bool any = next_event(e);
        if (!any || e.t > p_.time_cap_s) {
            if (!anyone_moving()) break;
            std::vector<int> moving;
            for (int i = 0; i < n_; ++i) {
                const Live& L = live_[static_cast<std::size_t>(i)];
                if (L.active && L.start.motion != Motion::stationary) moving.push_back(i);
            }
            const std::size_t count = moving.size();
            stop_all(std::max(now_, p_.time_cap_s), AnomalyKind::time_cap, std::move(moving),
                     std::to_string(count) + " balls still moving at time_cap_s " + fmt(p_.time_cap_s) + " (next event " +
                         (any ? std::string(event_kind_name(e.kind)) + " at t=" + fmt(e.t) : std::string("never")) +
                         "); stopped where they were");
            break;
        }
        std::vector<int> pending;
        if (r_.events.size() >= static_cast<std::size_t>(std::max(p_.max_events, 0))) {
            pending = {e.a};
            if (e.b >= 0) pending.push_back(e.b);
            stop_all(now_, AnomalyKind::event_guard, std::move(pending),
                     std::to_string(r_.events.size()) + " events reached (max_events " + std::to_string(p_.max_events) +
                         ") at t=" + fmt(now_) + "; next: " + std::string(event_kind_name(e.kind)) + " a=" +
                         std::to_string(e.a) + " b=" + std::to_string(e.b) + " body=" + std::to_string(e.body));
            break;
        }
        const double t = std::max(now_, e.t);
        chain = (chained && t - last_t <= p_.zero_time_s) ? chain + 1 : 1;
        if (chain > p_.max_zero_time_chain) {
            pending = {e.a};
            if (e.b >= 0) pending.push_back(e.b);
            stop_all(now_, AnomalyKind::zero_time_chain, std::move(pending),
                     std::to_string(chain) + " simultaneous events at t=" + fmt(t) + "; next: " +
                         std::string(event_kind_name(e.kind)) + " a=" + std::to_string(e.a) + " b=" +
                         std::to_string(e.b) + " body=" + std::to_string(e.body));
            break;
        }
        now_ = t;
        resolve(e, t);
        last_t = t;
        chained = true;
    }
    r_.t_end = now_;
    r_.final_state.balls.resize(static_cast<std::size_t>(n_));
    for (int i = 0; i < n_; ++i) r_.final_state.balls[static_cast<std::size_t>(i)] = state_at(i, now_, false);
}

}  // namespace

ShotRecord simulate(const SimInput& in, const TableShape& table, const PhysicsParams& p) {
    const auto started = std::chrono::steady_clock::now();
    ShotRecord r;
    Simulation sim(in, table, p, r);
    sim.run();
    r.sim_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    sim.log_anomalies();
    return r;
}

}  // namespace ofs::phys
