#pragma once

// Shared helpers for the independent calibration suite (spec 7.11, C1-C14). Expected values are
// written from the published sources in each test, never from the physics code; the helpers here
// only load data, set up states and read the shot record.

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "ofs/physics/collide.hpp"
#include "ofs/physics/cue.hpp"
#include "ofs/physics/motion.hpp"
#include "ofs/physics/nap.hpp"
#include "ofs/physics/params.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/physics/serialize.hpp"
#include "ofs/physics/shot_record.hpp"
#include "ofs/physics/simulator.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/physics/types.hpp"
#include "ofs/rng.hpp"
#include "ofs/table_geometry.hpp"
#include "ofs/vec.hpp"

namespace calib {

using namespace ofs;
using namespace ofs::phys;

inline constexpr double R = table::kBallRadius;
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kDeg = kPi / 180.0;

inline nlohmann::json read_json(const std::string& path) {
    std::ifstream f(path);
    if (!f) return nlohmann::json();
    try {
        return nlohmann::json::parse(f);
    } catch (const std::exception&) {
        return nlohmann::json();
    }
}

inline std::string fixture_path(const std::string& rel) { return std::string(OFS_TEST_FIXTURES_DIR) + "/" + rel; }

struct LoadedParams {
    PhysicsParams p;
    std::vector<std::string> problems;
};

inline const LoadedParams& loaded_params() {
    static const LoadedParams lp = [] {
        LoadedParams out;
        const nlohmann::json file = read_json(std::string(OFS_CORE_DATA_DIR) + "/table_params.json");
        if (file.is_null()) {
            out.problems.push_back("core/data/table_params.json missing or not JSON");
        } else {
            load_physics_params(file, out.p, out.problems);
        }
        return out;
    }();
    return lp;
}

// The parameters every expectation is written with: a tuned value changes the expected number.
inline const PhysicsParams& params() { return loaded_params().p; }

inline std::string join(const std::vector<std::string>& v, std::size_t limit = 12) {
    std::string s;
    for (std::size_t i = 0; i < v.size() && i < limit; ++i) s += (i ? "\n  " : "  ") + v[i];
    if (v.size() > limit) s += "\n  ... and " + std::to_string(v.size() - limit) + " more";
    return s;
}

struct LoadedTable {
    TableShape t;
    std::vector<std::string> problems;
    bool ok = false;
};

inline const LoadedTable& loaded_table() {
    static const LoadedTable lt = [] {
        LoadedTable out;
        const nlohmann::json file = read_json(std::string(OFS_CORE_DATA_DIR) + "/table_shape.json");
        if (file.is_null()) {
            out.problems.push_back("core/data/table_shape.json missing or not JSON");
            return out;
        }
        out.ok = load_table_shape(file, out.t, out.problems);
        if (out.t.lines.size() < 6 || out.t.pockets.size() != 6) {
            out.ok = false;
            out.problems.push_back("outline has " + std::to_string(out.t.lines.size()) + " cushion lines and " +
                                   std::to_string(out.t.pockets.size()) + " pockets");
        }
        return out;
    }();
    return lt;
}

// No cushions or pockets: tests whose measurement ends long before any cushion use it, so they
// judge the motion and collision physics alone, independent of the outline.
inline const TableShape& open_table() {
    static const TableShape t;
    return t;
}

// The four cushion noses of the WPBSA playing area (3569 x 1778 mm) as straight lines, no pockets.
inline TableShape box_table() {
    const double hx = table::kPlayingLength / 2.0;
    const double hy = table::kPlayingWidth / 2.0;
    TableShape t;
    t.lines.push_back({{hx, -hy}, {hx, hy}, {-1.0, 0.0}});    // top
    t.lines.push_back({{-hx, -hy}, {-hx, hy}, {1.0, 0.0}});   // baulk
    t.lines.push_back({{-hx, hy}, {hx, hy}, {0.0, -1.0}});    // left
    t.lines.push_back({{-hx, -hy}, {hx, -hy}, {0.0, 1.0}});   // right
    return t;
}

inline BallState make_ball(table::Ball kind, int index, Vec3 r, Vec3 v = {}, Vec3 w = {}) {
    BallState b;
    b.ball = kind;
    b.index = index;
    b.r = r;
    b.v = v;
    b.w = w;
    b.motion = Motion::stationary;  // simulate() re-derives it
    return b;
}

inline SimInput moving(std::vector<BallState> balls) {
    SimInput in;
    in.start.balls = std::move(balls);
    in.cue_ball = 0;
    in.has_strike = false;
    in.seed = 1;
    in.params_hash = "calibration";
    return in;
}

// Rolling without slip along v (types.hpp: u = v + w x (0, 0, -R) = 0).
inline Vec3 rolling_spin(Vec3 v) { return {-v.y / R, v.x / R, 0.0}; }

inline double angle_of(Vec3 v) { return std::atan2(v.y, v.x); }

inline double wrap(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}

inline bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Kinetic energy written out here (solid sphere, I = 2/5 m R^2) rather than taken from motion.hpp.
inline double energy(const BallState& b, const PhysicsParams& p) {
    if (b.motion == Motion::pocketed) return 0.0;
    return 0.5 * p.ball_mass_kg * norm2(b.v) + 0.5 * 0.4 * p.ball_mass_kg * R * R * norm2(b.w);
}

inline double energy(const TableState& s, const PhysicsParams& p) {
    double e = 0.0;
    for (const BallState& b : s.balls) e += energy(b, p);
    return e;
}

inline const ShotEvent* first_event(const ShotRecord& r, EventKind kind, int a, int b = -1, double after = -1.0) {
    for (const ShotEvent& e : r.events) {
        if (e.kind != kind || e.t < after) continue;
        const bool match = b < 0 ? (e.a == a || e.b == a) : ((e.a == a && e.b == b) || (e.a == b && e.b == a));
        if (match) return &e;
    }
    return nullptr;
}

inline const ShotEvent* first_transition(const ShotRecord& r, int ball, Motion from, Motion to, double after = -1.0) {
    for (const ShotEvent& e : r.events)
        if (e.kind == EventKind::transition && e.a == ball && e.from == from && e.to == to && e.t >= after) return &e;
    return nullptr;
}

// The first segment of `ball` starting at or after t (the state just after an event at t).
inline const Segment* segment_from(const ShotRecord& r, int ball, double t, double slack = 1e-12) {
    if (ball < 0 || static_cast<std::size_t>(ball) >= r.segments.size()) return nullptr;
    for (const Segment& s : r.segments[static_cast<std::size_t>(ball)])
        if (s.t0 >= t - slack) return &s;
    return nullptr;
}

inline const Segment* first_segment_in(const ShotRecord& r, int ball, Motion m, double after) {
    if (ball < 0 || static_cast<std::size_t>(ball) >= r.segments.size()) return nullptr;
    for (const Segment& s : r.segments[static_cast<std::size_t>(ball)])
        if (s.t0 >= after - 1e-12 && s.start.motion == m) return &s;
    return nullptr;
}

inline std::string vec_str(Vec3 v) {
    std::ostringstream o;
    o.precision(10);
    o << "(" << v.x << ", " << v.y << ", " << v.z << ")";
    return o.str();
}

// The first events and every anomaly, for failure messages.
inline std::string record_summary(const ShotRecord& r, std::size_t max_events = 16) {
    std::ostringstream o;
    o.precision(10);
    o << r.events.size() << " events, t_end " << r.t_end << ", " << r.anomalies.size() << " anomalies";
    for (std::size_t i = 0; i < r.events.size() && i < max_events; ++i) {
        const ShotEvent& e = r.events[i];
        o << "\n    [" << i << "] t=" << e.t << " " << event_kind_name(e.kind) << " a=" << e.a << " b=" << e.b
          << " body=" << e.body;
        if (e.kind == EventKind::transition) o << " " << motion_name(e.from) << "->" << motion_name(e.to);
    }
    if (r.events.size() > max_events) o << "\n    ...";
    for (const Anomaly& a : r.anomalies) o << "\n    anomaly " << a.kind << " t=" << a.t << " " << a.detail;
    return o.str();
}

inline std::string input_json(const SimInput& in) {
    nlohmann::json j = {{"start", to_json(in.start)}, {"cue_ball", in.cue_ball}, {"has_strike", in.has_strike},
                        {"seed", std::to_string(in.seed)}};
    if (in.has_strike) j["strike"] = to_json(in.strike);
    return j.dump();
}

// Ball-ball collision as Alciatore's TP A.14 models it (normal impulse with restitution e; friction
// mu(v_rel) = a + b exp(-c v_rel) at the initial sliding speed, capped where sliding would stop),
// applied to both balls' v and w with the vertical translation held by the cloth (spec 7.9).
// Used to bracket how far friction can move the frictionless answers in C4-C6.
inline void tp_a14_collision(BallState& a, BallState& b, Vec3 n, const PhysicsParams& p) {
    const double m = p.ball_mass_kg;
    const double I = 0.4 * m * R * R;
    const double vn = dot(a.v - b.v, n);
    const double Jn = 0.5 * (1.0 + p.ball_restitution) * m * vn;
    Vec3 slip = (a.v + cross(a.w, n * R)) - (b.v + cross(b.w, n * -R));
    slip -= n * dot(slip, n);
    const double s = norm(slip);
    a.v -= n * (Jn / m);
    b.v += n * (Jn / m);
    if (s <= 0.0) return;
    const Vec3 sh = slip / s;
    const double horiz2 = sh.x * sh.x + sh.y * sh.y;
    const double rate = (7.0 * horiz2 + 5.0 * sh.z * sh.z) / m;
    const double mu = p.ball_friction_a + p.ball_friction_b * std::exp(-p.ball_friction_c * s);
    const double Jf = std::min(mu * Jn, s / rate);
    const Vec3 F = sh * -Jf;  // on a, at +R n
    a.v += Vec3{F.x, F.y, 0.0} / m;
    b.v -= Vec3{F.x, F.y, 0.0} / m;
    a.w += cross(n * R, F) / I;
    b.w += cross(n * -R, -F) / I;
}

// Velocity once sliding ends (angular momentum about the cloth contact is conserved, TP A.4).
inline Vec3 final_rolling_velocity(const BallState& b) {
    return Vec3{5.0 / 7.0 * b.v.x + 2.0 / 7.0 * R * b.w.y, 5.0 / 7.0 * b.v.y - 2.0 / 7.0 * R * b.w.x, 0.0};
}

// Two balls whose centres meet at contact with the line of centres at `cut` from the cue ball's
// line (+x): the cue ball 1e-7 m short of contact at the origin, the object ball on the +y side.
// Returns the line of centres at contact.
inline Vec3 cut_setup(double cut, Vec3 cue_v, Vec3 cue_w, std::vector<BallState>& balls) {
    const Vec3 n{std::cos(cut), std::sin(cut), 0.0};
    balls = {make_ball(table::Ball::cue, 0, {-1e-7, 0.0, 0.0}, cue_v, cue_w),
             make_ball(table::Ball::red, 0, n * (2.0 * R))};
    return n;
}

}  // namespace calib
