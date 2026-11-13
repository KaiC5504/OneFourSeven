#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include "ofs/physics/record_query.hpp"
#include "ofs/physics/simulator.hpp"

// record_query.hpp checked against the spec and first principles, not its implementation. The
// expected speeds come from spec 7.2: a rolling ball decelerates at mu_r g along its heading, so
// after s metres v^2 = v0^2 - 2 mu_r g s.

using namespace ofs::phys;
using ofs::Vec3;

namespace {

constexpr double kR = ofs::table::kBallRadius;
constexpr double kPi = 3.14159265358979323846;
constexpr double kTopNoseX = ofs::table::kPlayingLength / 2.0;

nlohmann::json read_data(const char* name) {
    std::ifstream in(std::string(OFS_CORE_DATA_DIR) + "/" + name);
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

const TableShape& shape() {
    static const TableShape t = [] {
        TableShape s;
        std::vector<std::string> problems;
        REQUIRE(load_table_shape(read_data("table_shape.json"), s, problems));
        return s;
    }();
    return t;
}

const PhysicsParams& file_params() {
    static const PhysicsParams params = [] {
        PhysicsParams p;
        std::vector<std::string> problems;
        REQUIRE(load_physics_params(read_data("table_params.json"), p, problems));
        return p;
    }();
    return params;
}

BallState still(ofs::table::Ball kind, int index, double x, double y) {
    BallState b;
    b.ball = kind;
    b.index = index;
    b.r = {x, y, 0.0};
    return b;
}

// Rolling without slip (types.hpp): u = (v.x - R w.y, v.y + R w.x) = 0.
BallState rolling(double x, double y, Vec3 v) {
    BallState b = still(ofs::table::Ball::cue, 0, x, y);
    b.v = v;
    b.w = {-v.y / kR, v.x / kR, 0.0};
    b.motion = Motion::rolling;
    return b;
}

ShotRecord run(std::vector<BallState> balls, const PhysicsParams& p = file_params()) {
    SimInput in;
    in.start.balls = std::move(balls);
    in.has_strike = false;
    return simulate(in, shape(), p);
}

double rolled_speed(double v0, double s) {
    const PhysicsParams& p = file_params();
    return std::sqrt(v0 * v0 - 2.0 * p.roll_friction * p.gravity_m_s2 * s);
}

// The first event of `kind` involving ball `a`; r.events.size() when there is none.
std::size_t first_of(const ShotRecord& r, EventKind kind, int a) {
    for (std::size_t i = 0; i < r.events.size(); ++i) {
        if (r.events[i].kind == kind && (r.events[i].a == a || r.events[i].b == a)) return i;
    }
    return r.events.size();
}

// The cue ball rolling straight into a still red; heading +x it feels no nap (spec 7.8).
ShotRecord collision(Vec3 v, double gap) {
    const BallState cue = rolling(-1.2, 0.2, v);
    const Vec3 dir = v / norm_xy(v);
    const double centres = 2.0 * kR + gap;
    return run({cue, still(ofs::table::Ball::red, 0, cue.r.x + dir.x * centres, cue.r.y + dir.y * centres)});
}

// A slow cue ball rolling across the nap into a red: both balls move, drift and bounce off a
// cushion, so their paths have many segments.
ShotRecord busy_shot() {
    const BallState cue = rolling(0.3, -0.6, {0.0, 1.0, 0.0});
    return run({cue, still(ofs::table::Ball::red, 0, 0.3, -0.6 + 2.0 * kR + 0.5)});
}

double max_speed(const ShotRecord& r, int ball) {
    double fastest = 0.0;
    for (const Segment& s : r.segments[ball]) fastest = std::max(fastest, norm_xy(s.start.v));
    for (int k = 0; k <= 4000; ++k) fastest = std::max(fastest, norm_xy(r.ball_at(ball, r.t_end * k / 4000.0).v));
    return fastest;
}

bool contains(const std::vector<ofs::table::Vec2>& path, Vec3 p) {
    return std::any_of(path.begin(), path.end(), [&](const ofs::table::Vec2& q) {
        return std::hypot(q.x - p.x, q.y - p.y) <= 1e-12;
    });
}

}  // namespace

TEST_CASE("record query (spec 7.2): a rolling ball meets a still ball at sqrt(v0^2 - 2 mu_r g d)") {
    struct Case {
        double v0;
        double gap;
    };
    for (const Case c : {Case{1.0, 0.6}, Case{2.0, 0.9}, Case{0.6, 0.3}}) {
        CAPTURE(c.v0);
        CAPTURE(c.gap);
        const ShotRecord r = collision({c.v0, 0.0, 0.0}, c.gap);
        const std::size_t i = first_of(r, EventKind::ball_ball, 0);
        REQUIRE(i < r.events.size());
        CHECK(std::abs(event_impact_speed(r, i, shape()) - rolled_speed(c.v0, c.gap)) <= 1e-9);
    }
}

TEST_CASE("record query (spec 7.2, 7.8): across the nap the drift is sideways only, within 2e-4 m/s") {
    const ShotRecord r = busy_shot();
    const std::size_t i = first_of(r, EventKind::ball_ball, 0);
    REQUIRE(i < r.events.size());
    CHECK(std::abs(event_impact_speed(r, i, shape()) - rolled_speed(1.0, 0.5)) <= 2e-4);
}

TEST_CASE("record query (spec 7.2): a rolling ball meets a straight cushion head-on at sqrt(v0^2 - 2 mu_r g d)") {
    const double x0 = 1.0;
    const double v0 = 1.0;
    const ShotRecord r = run({rolling(x0, 0.1, {v0, 0.0, 0.0})});
    const std::size_t i = first_of(r, EventKind::ball_line, 0);
    REQUIRE(i < r.events.size());
    const double expected = rolled_speed(v0, kTopNoseX - kR - x0);
    CHECK(std::abs(event_impact_speed(r, i, shape()) - expected) <= 1e-9);
}

TEST_CASE("record query (spec 7.2): at 45 degrees the cushion impact speed is the normal component") {
    const double x0 = 1.2;
    const double v0 = 2.0;
    const double c45 = std::cos(kPi / 4.0);
    const ShotRecord r = run({rolling(x0, 0.0, {v0 * c45, v0 * c45, 0.0})});
    const std::size_t i = first_of(r, EventKind::ball_line, 0);
    REQUIRE(i < r.events.size());
    const double travel = (kTopNoseX - kR - x0) / c45;
    const double speed = rolled_speed(v0, travel);
    // Above nap_speed_max the whole way, so no nap turns it (spec 7.8).
    REQUIRE(speed > file_params().nap_speed_max_m_s);
    CHECK(std::abs(event_impact_speed(r, i, shape()) - speed * c45) <= 1e-9);
}

TEST_CASE("record query (spec 7.6): a pocket event reads the speed as the centre enters the drop circle") {
    // Straight up the middle of the middle_left pocket, clear of its facings and jaws.
    const Pocket& middle = shape().pockets[2];
    REQUIRE(middle.name == "middle_left");
    const double y0 = 0.4;
    const ShotRecord r = run({rolling(middle.drop_centre.x, y0, {0.0, 2.0, 0.0})});
    const std::size_t i = first_of(r, EventKind::pocket, 0);
    REQUIRE(i < r.events.size());
    const double speed = event_impact_speed(r, i, shape());
    CHECK(speed > 0.0);
    const double expected = rolled_speed(2.0, middle.drop_centre.y - middle.drop_radius - y0);
    CHECK(std::abs(speed - expected) <= 1e-6);
}

TEST_CASE("record query: transition, nap and guard events read 0, and so does an index past the end") {
    // A stun across the nap: slides, rolls, re-evaluates its nap, stops.
    BallState stun = still(ofs::table::Ball::cue, 0, -1.0, -0.3);
    stun.v = {0.4 * std::cos(kPi / 6.0), 0.4 * std::sin(kPi / 6.0), 0.0};
    const ShotRecord r = run({stun});
    int transitions = 0;
    int naps = 0;
    for (std::size_t i = 0; i < r.events.size(); ++i) {
        const EventKind k = r.events[i].kind;
        if (k != EventKind::transition && k != EventKind::nap) continue;
        transitions += k == EventKind::transition;
        naps += k == EventKind::nap;
        CAPTURE(i);
        CHECK(event_impact_speed(r, i, shape()) == 0.0);
    }
    REQUIRE(transitions >= 2);
    REQUIRE(naps >= 1);
    CHECK(event_impact_speed(r, r.events.size(), shape()) == 0.0);
    CHECK(event_impact_speed(r, r.events.size() + 100, shape()) == 0.0);

    PhysicsParams capped = file_params();
    capped.time_cap_s = 0.5;
    const ShotRecord stopped = run({rolling(-1.0, 0.0, {1.0, 0.0, 0.0})}, capped);
    REQUIRE_FALSE(stopped.events.empty());
    REQUIRE(stopped.events.back().kind == EventKind::guard);
    CHECK(event_impact_speed(stopped, stopped.events.size() - 1, shape()) == 0.0);
}

TEST_CASE("record query (spec 7.3): the strike event reads the cue ball's speed just after the strike") {
    SimInput in;
    in.start.balls = {still(ofs::table::Ball::cue, 0, -1.0, 0.0)};
    in.cue_ball = 0;
    in.has_strike = true;
    in.strike.speed_m_s = 2.0;
    in.seed = 7;
    const ShotRecord r = simulate(in, shape(), file_params());
    REQUIRE_FALSE(r.events.empty());
    REQUIRE(r.events[0].kind == EventKind::strike);
    const double expected = norm_xy(r.strike.v);
    REQUIRE(expected > 0.0);
    CHECK(event_impact_speed(r, 0, shape()) == doctest::Approx(expected).epsilon(1e-12));
}

TEST_CASE("record query (spec 8.4): sample_path starts at the start, ends at rest and keeps every segment start") {
    const ShotRecord r = busy_shot();
    for (int ball : {0, 1}) {
        CAPTURE(ball);
        const std::vector<ofs::table::Vec2> path = sample_path(r, ball, 0.01, 100000);
        REQUIRE_FALSE(path.empty());
        const Vec3 start = r.segments[ball].front().start.r;
        CHECK(std::hypot(path.front().x - start.x, path.front().y - start.y) <= 1e-12);
        const Vec3 end = r.final_state.balls[ball].r;
        CHECK(std::hypot(path.back().x - end.x, path.back().y - end.y) <= 1e-12);
        REQUIRE(r.segments[ball].size() >= 3);
        for (std::size_t k = 0; k < r.segments[ball].size(); ++k) {
            CAPTURE(k);
            CHECK(contains(path, r.segments[ball][k].start.r));
        }
    }
}

TEST_CASE("record query (spec 8.4): sample_path points are never more than max_step_s of travel apart") {
    const ShotRecord r = busy_shot();
    for (int ball : {0, 1}) {
        for (double step : {0.01, 0.05}) {
            CAPTURE(ball);
            CAPTURE(step);
            const std::vector<ofs::table::Vec2> path = sample_path(r, ball, step, 100000);
            REQUIRE(path.size() >= 2);
            const double limit = step * max_speed(r, ball) + 1e-9;
            double widest = 0.0;
            for (std::size_t k = 1; k < path.size(); ++k) {
                widest = std::max(widest, std::hypot(path[k].x - path[k - 1].x, path[k].y - path[k - 1].y));
            }
            CHECK(widest <= limit);
        }
    }
}

TEST_CASE("record query (spec 8.4): sample_path keeps to max_points, segment starts first") {
    const ShotRecord r = busy_shot();
    for (int ball : {0, 1}) {
        CAPTURE(ball);
        const std::size_t segments = r.segments[ball].size();
        const std::size_t budget = segments + 5;
        const std::vector<ofs::table::Vec2> path = sample_path(r, ball, 1e-4, budget);
        REQUIRE_FALSE(path.empty());
        CHECK(path.size() <= budget);
        CHECK(path.size() > segments / 2);
        for (std::size_t k = 0; k < segments; ++k) {
            CAPTURE(k);
            CHECK(contains(path, r.segments[ball][k].start.r));
        }
        const Vec3 end = r.final_state.balls[ball].r;
        CHECK(std::hypot(path.back().x - end.x, path.back().y - end.y) <= 1e-12);
    }
}

TEST_CASE("record query: sample_path of a ball that isn't in the record is empty") {
    const ShotRecord r = busy_shot();
    CHECK(sample_path(r, -1, 0.01, 100).empty());
    CHECK(sample_path(r, static_cast<int>(r.segments.size()), 0.01, 100).empty());
    CHECK(sample_path(r, 99, 0.01, 100).empty());
}
