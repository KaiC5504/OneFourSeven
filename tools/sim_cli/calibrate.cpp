// sim_cli calibrate: the data series behind the calibration report (spec 16.1, 7.11). Each series
// is one JSON file of rows; tools/calibration_report.py turns them into charts.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>

#include "ofs/physics/cue.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/version.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

using Clock = std::chrono::steady_clock;
using ofs::table::Ball;

constexpr double kPi = 3.14159265358979323846;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double degrees(double rad) { return rad * 180.0 / kPi; }
double radians(double deg) { return deg * kPi / 180.0; }

nlohmann::json number(double v) { return std::isfinite(v) ? nlohmann::json(v) : nlohmann::json(nullptr); }

struct Series {
    std::string name;
    std::string title;
    std::string target;
    std::vector<std::string> columns;
    nlohmann::json rows = nlohmann::json::array();
    nlohmann::json summary = nlohmann::json::object();
    std::vector<std::string> notes;
};

phys::BallState ball(Ball kind, int index, Vec3 r) {
    phys::BallState b;
    b.ball = kind;
    b.index = index;
    b.r = r;
    return b;
}

// A cue ball moving at v: rolling (u = 0) or stunned (no spin), with side spin wz.
phys::BallState moving_cue_ball(Vec3 r, Vec3 v, double wz, bool rolling) {
    phys::BallState b = ball(Ball::cue, 0, r);
    b.v = v;
    b.w = rolling ? Vec3{-v.y / kR, v.x / kR, wz} : Vec3{0.0, 0.0, wz};
    b.motion = rolling ? phys::Motion::rolling : phys::Motion::sliding;
    return b;
}

phys::ShotRecord run(const Setup& s, std::vector<phys::BallState> balls, const phys::CueStrike* strike) {
    phys::SimInput in;
    in.start.balls = std::move(balls);
    in.cue_ball = 0;
    in.has_strike = strike != nullptr;
    if (strike != nullptr) in.strike = *strike;
    in.params_hash = s.params_hash;
    return phys::simulate(in, s.table, s.params);
}

bool is_contact(phys::EventKind k) {
    using phys::EventKind;
    return k == EventKind::ball_ball || k == EventKind::ball_line || k == EventKind::ball_arc ||
           k == EventKind::pocket;
}

const phys::ShotEvent* first_contact(const phys::ShotRecord& r, int ball_index, double after) {
    for (const phys::ShotEvent& e : r.events) {
        if (e.t > after && is_contact(e.kind) && (e.a == ball_index || e.b == ball_index)) return &e;
    }
    return nullptr;
}

const phys::ShotEvent* first_of(const phys::ShotRecord& r, phys::EventKind kind, int ball_index) {
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind == kind && (e.a == ball_index || e.b == ball_index)) return &e;
    }
    return nullptr;
}

// The ball's velocity when it first rolls at or after t, before its next contact.
bool rolling_velocity(const phys::ShotRecord& r, int ball_index, double t, Vec3& v) {
    const phys::ShotEvent* next = first_contact(r, ball_index, t);
    const double limit = next != nullptr ? next->t : std::numeric_limits<double>::infinity();
    if (static_cast<std::size_t>(ball_index) >= r.segments.size()) return false;
    for (const phys::Segment& s : r.segments[static_cast<std::size_t>(ball_index)]) {
        if (s.t0 >= t && s.t0 < limit && s.start.motion == phys::Motion::rolling) {
            v = s.start.v;
            return true;
        }
    }
    return false;
}

// From a to b, anticlockwise positive, in degrees.
double signed_angle(const Vec3& a, const Vec3& b) {
    return degrees(std::atan2(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y));
}

double unsigned_angle(const Vec3& a, const Vec3& b) {
    if (norm_xy(a) == 0.0 || norm_xy(b) == 0.0) return kNaN;
    return std::fabs(signed_angle(a, b));
}

struct Cushion {
    Vec3 nose;     // a point on the cushion nose
    Vec3 normal;   // into the playing area
    Vec3 tangent;  // along the nose, towards +x
};

// The middle of the longest left-hand side cushion section (normal -y), or the table edge.
Cushion side_cushion(const phys::TableShape& t) {
    Cushion c{{-0.8, ofs::table::kPlayingWidth / 2.0, 0.0}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}};
    double best = 0.0;
    for (const phys::LineCushion& line : t.lines) {
        const Vec3 d{line.p2.x - line.p1.x, line.p2.y - line.p1.y, 0.0};
        if (line.normal.y > -0.99 || norm_xy(d) <= best) continue;
        best = norm_xy(d);
        c.nose = Vec3{line.p1.x, line.p1.y, 0.0} + d * 0.5;
        c.normal = {line.normal.x, line.normal.y, 0.0};
        c.tangent = ofs::unit_or_zero(d.x < 0.0 ? -d : d);
    }
    return c;
}

struct Rebound {
    double incidence = kNaN;
    double rebound = kNaN;
    double speed_ratio = kNaN;
    double rolling = kNaN;
};

// A rolling ball sent into the side cushion at `incidence_deg` from its normal, 0.25 m out.
Rebound cushion_shot(const Setup& s, const Cushion& c, double speed, double incidence_deg, double side) {
    const double a = radians(incidence_deg);
    const Vec3 dir = -c.normal * std::cos(a) + c.tangent * std::sin(a);
    const Vec3 contact = c.nose + c.normal * kR;
    // Running side turns the contact point against the direction of travel along the cushion.
    const double running = (-c.normal.y * c.tangent.x + c.normal.x * c.tangent.y) >= 0.0 ? 1.0 : -1.0;
    const double wz = side * running * speed / kR;
    const phys::ShotRecord r = run(s, {moving_cue_ball(contact - dir * 0.25, dir * speed, wz, true)}, nullptr);
    Rebound out;
    const phys::ShotEvent* hit = first_of(r, phys::EventKind::ball_line, 0);
    if (hit == nullptr) return out;
    Vec3 n = c.normal;
    if (hit->body >= 0 && static_cast<std::size_t>(hit->body) < s.table.lines.size()) {
        const auto& line = s.table.lines[static_cast<std::size_t>(hit->body)];
        n = {line.normal.x, line.normal.y, 0.0};
    }
    const Vec3 tangent{-n.y, n.x, 0.0};
    const Vec3 before = state_before(r, 0, hit->t).v;
    const Vec3 after = state_after(r, 0, hit->t).v;
    const double along = dot(before, tangent) >= 0.0 ? 1.0 : -1.0;
    out.incidence = degrees(std::atan2(std::fabs(dot(before, tangent)), -dot(before, n)));
    out.rebound = degrees(std::atan2(along * dot(after, tangent), dot(after, n)));
    out.speed_ratio = norm_xy(before) > 0.0 ? norm_xy(after) / norm_xy(before) : kNaN;
    Vec3 roll;
    if (rolling_velocity(r, 0, hit->t, roll)) {
        out.rolling = degrees(std::atan2(along * dot(roll, tangent), dot(roll, n)));
    }
    return out;
}

Series cushion_rebound(const Setup& s) {
    Series out{"cushion_rebound", "Cushion rebound angle and speed against incidence", "C7",
               {"speed_m_s", "incidence_deg", "incidence_measured_deg", "rebound_deg", "speed_ratio",
                "rebound_rolling_deg"}};
    const Cushion c = side_cushion(s.table);
    for (double speed : {0.5, 1.0, 2.0, 4.0}) {
        for (int i = 0; i <= 16; ++i) {
            const double incidence = 5.0 * i;
            const Rebound r = cushion_shot(s, c, speed, incidence, 0.0);
            out.rows.push_back({speed, incidence, number(r.incidence), number(r.rebound), number(r.speed_ratio),
                                number(r.rolling)});
        }
    }
    out.notes.push_back("A rolling ball without side into the left side cushion. Angles from the cushion "
                        "normal; rebound_deg just after the impact, rebound_rolling_deg once the ball rolls "
                        "again.");
    return out;
}

Series cushion_side(const Setup& s) {
    Series out{"cushion_side", "Running and check side off a cushion", "C8",
               {"side", "rebound_deg", "speed_ratio", "rebound_rolling_deg"}};
    const Cushion c = side_cushion(s.table);
    for (int i = -4; i <= 4; ++i) {
        const double side = 0.25 * i;
        const Rebound r = cushion_shot(s, c, 1.5, 45.0, side);
        out.rows.push_back({side, number(r.rebound), number(r.speed_ratio), number(r.rolling)});
    }
    out.summary = {{"speed_m_s", 1.5}, {"incidence_deg", 45.0}};
    out.notes.push_back("side is w_z R / v at the start, positive for running side, negative for check side.");
    return out;
}

struct Cut {
    phys::ShotRecord record;
    const phys::ShotEvent* hit = nullptr;
};

// The cue ball 1 mm short of contact with a red at a cut of `cut_deg`, travelling along +x.
Cut cut_shot(const Setup& s, double cut_deg, double speed, bool rolling) {
    const Vec3 object{0.3, 0.0, 0.0};
    const double a = radians(cut_deg);
    const Vec3 centres{std::cos(a), std::sin(a), 0.0};
    const Vec3 dir{1.0, 0.0, 0.0};
    const Vec3 start = object - centres * (2.0 * kR) - dir * 1e-3;
    Cut out;
    out.record =
        run(s, {moving_cue_ball(start, dir * speed, 0.0, rolling), ball(Ball::red, 0, object)}, nullptr);
    out.hit = first_of(out.record, phys::EventKind::ball_ball, 0);
    return out;
}

Series throw_series(const Setup& s) {
    Series out{"throw", "Cut-induced throw against cut", "C6",
               {"speed_m_s", "cut_deg", "fraction", "throw_deg"}};
    for (double speed : {0.5, 1.5, 4.0}) {
        for (int i = 0; i <= 16; ++i) {
            const double cut = 5.0 * i;
            const Cut c = cut_shot(s, cut, speed, false);
            double thrown = kNaN;
            if (c.hit != nullptr) {
                const Vec3 cue = state_before(c.record, 0, c.hit->t).r;
                const Vec3 object = state_after(c.record, 1, c.hit->t).v;
                const Vec3 centres = ofs::unit_or_zero(state_before(c.record, 1, c.hit->t).r - cue);
                // Positive when the object ball is dragged towards the cue ball's side of the line of
                // centres (-y here), which is what throw does.
                if (norm_xy(object) > 0.0) thrown = -signed_angle(centres, object);
            }
            out.rows.push_back({speed, cut, 1.0 - std::sin(radians(cut)), number(thrown)});
        }
    }
    out.notes.push_back("Stun shots (no spin at contact): throw is the angle between the object ball's "
                        "direction and the line of centres at impact.");
    return out;
}

Series deflection_series(const Setup& s) {
    Series out{"deflection", "Cue ball deflection against cut, rolling and stunned", "C4, C5",
               {"mode", "cut_deg", "fraction", "separation_deg", "cue_deflection_deg"}};
    for (bool rolling : {true, false}) {
        for (int i = 0; i <= 17; ++i) {
            const double cut = 5.0 * i;
            const Cut c = cut_shot(s, cut, 1.0, rolling);
            double separation = kNaN;
            double deflection = kNaN;
            if (c.hit != nullptr) {
                const Vec3 cue_after = state_after(c.record, 0, c.hit->t).v;
                const Vec3 object_after = state_after(c.record, 1, c.hit->t).v;
                separation = unsigned_angle(cue_after, object_after);
                Vec3 roll;
                if (rolling_velocity(c.record, 0, c.hit->t, roll)) {
                    deflection = unsigned_angle({1.0, 0.0, 0.0}, roll);
                }
            }
            const double fraction = 1.0 - std::sin(radians(cut));
            out.rows.push_back(
                {rolling ? "rolling" : "stun", cut, fraction, number(separation), number(deflection)});
        }
    }
    out.notes.push_back("At 1 m/s. separation_deg is between the two balls' directions just after impact; "
                        "cue_deflection_deg is the cue ball's direction from the aim line once it rolls "
                        "again.");
    return out;
}

Series screw_follow(const Setup& s) {
    Series out{"screw_follow", "Screw-back and follow distance against tip offset", "7.12",
               {"speed_m_s", "tip_y", "distance_m"}};
    for (double speed : {1.0, 2.0, 3.0}) {
        for (int i = -5; i <= 5; ++i) {
            phys::CueStrike c;
            c.speed_m_s = speed;
            c.tip_y = 0.1 * i;
            const phys::ShotRecord r =
                run(s, {ball(Ball::cue, 0, {-0.5, 0.0, 0.0}), ball(Ball::red, 0, {0.5, 0.0, 0.0})}, &c);
            double distance = kNaN;
            if (const phys::ShotEvent* hit = first_of(r, phys::EventKind::ball_ball, 0)) {
                distance = r.final_state.balls[0].r.x - state_after(r, 0, hit->t).r.x;
            }
            out.rows.push_back({speed, c.tip_y, number(distance)});
        }
    }
    out.notes.push_back("Full-ball hit on a red 1 m away (straight_1m). Distance from the impact to where the "
                        "cue ball stops, along the aim: negative is screw back.");
    return out;
}

Series squirt_series(const Setup& s) {
    Series out{"squirt", "Squirt against side", "C9", {"tip_x", "squirt_deg", "ball_speed_m_s", "miscue"}};
    phys::TableState lone{{ball(Ball::cue, 0, {0.0, 0.0, 0.0})}};
    for (int i = 0; i <= 14; ++i) {
        phys::CueStrike c;
        c.speed_m_s = 3.0;
        c.tip_x = 0.05 * i;
        ofs::Rng rng(147);
        const phys::StrikeOutcome o = phys::strike(lone, 0, c, s.table, s.params, rng);
        out.rows.push_back({c.tip_x, degrees(o.squirt_rad), norm_xy(o.v), o.miscue});
    }
    out.summary = {{"miscue_limit", s.params.miscue_limit}};
    out.notes.push_back("A level cue at 3 m/s. squirt_deg is signed from the aim line.");
    return out;
}

Series swerve_series(const Setup& s) {
    Series out{"swerve", "Swerve paths with an elevated cue and side", "C10",
               {"variant", "elevation_deg", "tip_x", "t_s", "x_m", "y_m"}};
    const double variants[][2] = {{0.0, 0.4}, {15.0, 0.4}, {30.0, 0.4}, {30.0, -0.4}, {45.0, 0.4}};
    for (const auto& variant : variants) {
        phys::CueStrike c;
        c.speed_m_s = 1.5;
        c.elevation_rad = radians(variant[0]);
        c.tip_x = variant[1];
        const phys::ShotRecord r = run(s, {ball(Ball::cue, 0, {-1.3, 0.0, 0.0})}, &c);
        const phys::ShotEvent* stop = first_contact(r, 0, -1.0);
        const double end = std::fmin(stop != nullptr ? stop->t : r.t_end, 5.0);
        const std::string label = format("%.0f deg, tip %+.2f", variant[0], variant[1]);
        for (int k = 0;; ++k) {
            const double t = std::fmin(0.01 * k, end);
            const Vec3 p = state_after(r, 0, t).r;
            out.rows.push_back({label, variant[0], variant[1], t, number(p.x), number(p.y)});
            if (t >= end) break;
        }
    }
    out.notes.push_back("Aiming along +x at 1.5 m/s; positive tip_x is right of centre as the player sees it, "
                        "which is towards -y. Each path stops at the cue ball's first contact.");
    return out;
}

Series lag_series(const Setup& s) {
    Series out{"lag", "The lag: cue speed to reach the top cushion and back to baulk", "C11",
               {"cue_speed_m_s", "ball_speed_m_s", "cushion_contacts", "final_x_m"}};
    const Vec3 start{ofs::table::baulk_line_x(), 0.0, 0.0};
    const auto lag = [&s, &start](double speed, double& ball_speed, int& contacts, double& final_x) {
        phys::CueStrike c;
        c.speed_m_s = speed;
        const phys::ShotRecord r = run(s, {ball(Ball::cue, 0, start)}, &c);
        ball_speed = norm_xy(r.strike.v);
        contacts = 0;
        for (const phys::ShotEvent& e : r.events) {
            contacts += e.kind == phys::EventKind::ball_line && e.a == 0 ? 1 : 0;
        }
        final_x = r.final_state.balls.empty() ? kNaN : r.final_state.balls[0].r.x;
        return contacts >= 2;
    };
    double reached_at = kNaN;
    double short_of = kNaN;
    for (int i = 0; i <= 36; ++i) {
        const double speed = 0.4 + 0.1 * i;
        double ball_speed = kNaN;
        double final_x = kNaN;
        int contacts = 0;
        const bool reached = lag(speed, ball_speed, contacts, final_x);
        out.rows.push_back({speed, number(ball_speed), contacts, number(final_x)});
        if (reached && std::isnan(reached_at)) reached_at = speed;
        if (!reached && std::isnan(reached_at)) short_of = speed;
    }
    nlohmann::json lag_cue = nullptr;
    nlohmann::json lag_ball = nullptr;
    if (!std::isnan(reached_at)) {
        double lo = std::isnan(short_of) ? 0.0 : short_of;
        double hi = reached_at;
        double ball_speed = kNaN;
        double final_x = kNaN;
        int contacts = 0;
        for (int i = 0; i < 25; ++i) {
            const double mid = (lo + hi) / 2.0;
            (lag(mid, ball_speed, contacts, final_x) ? hi : lo) = mid;
        }
        lag(hi, ball_speed, contacts, final_x);
        lag_cue = hi;
        lag_ball = number(ball_speed);
    }
    out.summary = {{"lag_cue_speed_m_s", lag_cue}, {"lag_ball_speed_m_s", lag_ball}, {"start_x_m", start.x}};
    out.notes.push_back("Centre ball from the middle of the baulk line straight up the table. The lag is the "
                        "slowest cue speed whose cue ball touches the top cushion and then the baulk cushion.");
    return out;
}

Series break_times(const Setup& s, long long breaks) {
#ifdef NDEBUG
    const char* build = "release";
#else
    const char* build = "debug";
#endif
    Series out{"break_sim_time", "Simulation time of break-offs", "7.13",
               {"rack_seed", "cue_speed_m_s", "events", "potted", "sim_us"}};
    std::vector<double> times;
    for (long long i = 1; i <= breaks; ++i) {
        const auto seed = static_cast<std::uint64_t>(i);
        ofs::Rng rng(seed);
        const phys::TableState rack = phys::opening_rack(seed, s.params);
        const phys::CueStrike c = break_off_strike(rack, rng.uniform(5.0, 10.0));
        const auto start = Clock::now();
        const phys::ShotRecord r = run(s, rack.balls, &c);
        const double us = microseconds_since(start);
        times.push_back(us);
        int potted = 0;
        for (const phys::ShotEvent& e : r.events) potted += e.kind == phys::EventKind::pocket ? 1 : 0;
        out.rows.push_back({i, c.speed_m_s, r.events.size(), potted, us});
    }
    const Stats st = stats_of(times);
    out.summary = {{"build", build},   {"p50_us", st.p50},       {"p99_us", st.p99},
                   {"max_us", st.max}, {"budget_us", 2000.0}};
    out.notes.push_back(std::string("Wall-clock time of simulate() per break-off, measured in a ") + build +
                        " build; only release timings count against the budget.");
    return out;
}

}  // namespace

int cmd_calibrate(const std::vector<std::string>& argv) {
    const ArgSpec spec{{"out", "breaks", "params", "table"}, {}, 0, 0};
    Args args;
    std::string error;
    if (!parse_args(argv, spec, args, error)) return usage_error("calibrate", error);
    if (args.value("out") == nullptr) return usage_error("calibrate", "--out DIR is required");
    long long breaks = 200;
    if (!option_int(args, "breaks", 1, 1000000, breaks, error)) return usage_error("calibrate", error);
    Setup setup;
    if (!load_setup(args, setup)) return kExitUsage;
#ifdef NDEBUG
    const char* build = "release";
#else
    const char* build = "debug";
#endif

    const std::vector<Series> all = {cushion_rebound(setup), cushion_side(setup), throw_series(setup),
                                     deflection_series(setup), screw_follow(setup), squirt_series(setup),
                                     swerve_series(setup), lag_series(setup), break_times(setup, breaks)};
    int code = kExitOk;
    for (const Series& series : all) {
        const nlohmann::json file = {{"series", series.name},
                                     {"title", series.title},
                                     {"target", series.target},
                                     {"params_hash", setup.params_hash},
                                     {"table_hash", setup.table_hash},
                                     {"core_version", std::string(ofs::core_version())},
                                     {"build", build},
                                     {"columns", series.columns},
                                     {"rows", series.rows},
                                     {"summary", series.summary},
                                     {"notes", series.notes}};
        const auto folder = std::filesystem::u8path(*args.value("out"));
        const std::string path = (folder / std::filesystem::u8path(series.name + ".json")).u8string();
        if (!write_text_file(path, file.dump(1) + "\n", error)) {
            std::fprintf(stderr, "calibrate: %s\n", error.c_str());
            code = kExitUsage;
            continue;
        }
        std::printf("%-16s %4zu rows  %s\n", series.name.c_str(), series.rows.size(), path.c_str());
    }
    std::printf("params %s, table %s, %s build\n", setup.params_hash.c_str(), setup.table_hash.c_str(), build);
    return code;
}

}  // namespace sim_cli
