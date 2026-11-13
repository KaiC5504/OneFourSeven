#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include "ofs/physics/collide.hpp"
#include "ofs/physics/motion.hpp"
#include "ofs/physics/record_query.hpp"
#include "ofs/physics/simulator.hpp"

using namespace ofs::phys;

namespace {

constexpr double kR = ofs::table::kBallRadius;
constexpr double kPi = 3.14159265358979323846;

nlohmann::json read_data(const char* name) {
    std::ifstream in(std::string(OFS_CORE_DATA_DIR) + "/" + name);
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

const TableShape& file_table() {
    static const TableShape shape = [] {
        TableShape t;
        std::vector<std::string> problems;
        REQUIRE(load_table_shape(read_data("table_shape.json"), t, problems));
        return t;
    }();
    return shape;
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

BallState rolling(double x, double y, double vx, double vy) {
    BallState b;
    b.r = {x, y, 0.0};
    b.v = {vx, vy, 0.0};
    b.w = {-vy / kR, vx / kR, 0.0};
    b.motion = Motion::rolling;
    return b;
}

BallState at_rest(double x, double y) {
    BallState b;
    b.ball = ofs::table::Ball::red;
    b.r = {x, y, 0.0};
    return b;
}

// Ball 0 rolls from the origin along +x and changes segment at t = 0.5 (the event's own time);
// ball 1 waits on the x axis.
ShotRecord two_ball_record() {
    ShotRecord r;
    const BallState a0 = rolling(0.0, 0.0, 1.0, 0.0);
    const BallState a_at = evolve(a0, 0.5, r.params, {});
    BallState a1 = rolling(a_at.r.x, a_at.r.y, -0.2, 0.05);
    r.segments = {{{0.0, a0, {}}, {0.5, a1, {}}}, {{0.0, at_rest(a_at.r.x + 2.0 * kR, 0.0), {}}}};
    r.final_state.balls = {a1, at_rest(a_at.r.x + 2.0 * kR, 0.0)};
    r.t_end = 1.0;
    return r;
}

SimInput straight_pot() {
    SimInput in;
    BallState cue;
    cue.r = {0.0, 0.2, 0.0};
    in.start.balls = {cue, at_rest(0.0, 0.55)};
    in.cue_ball = 0;
    in.strike.aim_rad = kPi / 2.0;
    in.strike.speed_m_s = 1.5;
    return in;
}

int find_event(const ShotRecord& r, EventKind kind, int a) {
    for (std::size_t i = 0; i < r.events.size(); ++i) {
        if (r.events[i].kind == kind && r.events[i].a == a) return static_cast<int>(i);
    }
    return -1;
}

bool same_point(const ofs::table::Vec2& p, const ofs::Vec3& q) { return p.x == q.x && p.y == q.y; }

}  // namespace

TEST_CASE("record query impl: the strike reads the cue ball's speed just after it") {
    const ShotRecord r = simulate(straight_pot(), file_table(), file_params());
    REQUIRE(!r.events.empty());
    REQUIRE(r.events[0].kind == EventKind::strike);
    CHECK(event_impact_speed(r, 0, file_table()) == std::hypot(r.strike.v.x, r.strike.v.y));
    // The cue outweighs the ball, so a centre hit sends it off faster than the cue's 1.5 m/s.
    CHECK(event_impact_speed(r, 0, file_table()) > 1.5);
    CHECK(event_impact_speed(r, 0, file_table()) < 2.5);
}

TEST_CASE("record query impl: ball_ball uses the incoming segment at an event that starts a new one") {
    ShotRecord r = two_ball_record();
    r.events = {{0.5, EventKind::ball_ball, 0, 1, -1, Motion::rolling, Motion::rolling}};
    const BallState before = evolve(r.segments[0][0].start, 0.5, r.params, {});
    CHECK(event_impact_speed(r, 0, TableShape{}) == doctest::Approx(before.v.x).epsilon(1e-15));
    // The outgoing segment heads away; reading it would give 0.
    CHECK(event_impact_speed(r, 0, TableShape{}) > 0.9);

    // b closing on a counts too; separating balls read 0.
    // Started further out so that it is still ahead of a at t = 0.5.
    r.segments[1][0].start = rolling(r.segments[1][0].start.r.x + 0.2, 0.0, -0.3, 0.0);
    const BallState b_before = evolve(r.segments[1][0].start, 0.5, r.params, {});
    CHECK(event_impact_speed(r, 0, TableShape{}) == doctest::Approx(before.v.x - b_before.v.x).epsilon(1e-15));
    r.events[0].a = 1;
    r.events[0].b = 0;
    CHECK(event_impact_speed(r, 0, TableShape{}) == doctest::Approx(before.v.x - b_before.v.x).epsilon(1e-15));
    r.segments[1][0].start = rolling(r.segments[1][0].start.r.x, 0.0, 2.0, 0.0);
    r.events[0].a = 0;
    r.events[0].b = 1;
    CHECK(event_impact_speed(r, 0, TableShape{}) == 0.0);
}

TEST_CASE("record query impl: a chain through a frozen pair reads each impact at the same instant") {
    // The second red touches the first, so the first passes the cue ball's hit on at once.
    SimInput in = straight_pot();
    in.start.balls.push_back(at_rest(0.0, 0.55 + 2.0 * kR));
    const ShotRecord r = simulate(in, file_table(), file_params());
    const int first = find_event(r, EventKind::ball_ball, 0);
    REQUIRE(first > 0);
    const std::size_t hit = static_cast<std::size_t>(first);
    REQUIRE(hit + 1 < r.events.size());
    const ShotEvent& chained = r.events[hit + 1];
    REQUIRE(chained.kind == EventKind::ball_ball);
    REQUIRE(chained.t == r.events[hit].t);
    REQUIRE(((chained.a == 1 && chained.b == 2) || (chained.a == 2 && chained.b == 1)));

    const double cue_speed = event_impact_speed(r, hit, file_table());
    CHECK(cue_speed == doctest::Approx(r.ball_at(0, r.events[hit].t - 1e-9).v.y).epsilon(1e-6));
    // The red takes nearly all of the cue ball's speed into the second red; the segment the record
    // kept for it at that instant is after both impacts, nearly still.
    BallState cue = r.ball_at(0, r.events[hit].t - 1e-9);
    BallState red = at_rest(0.0, 0.55);
    collide_balls(cue, red, r.params);
    const double passed_on = event_impact_speed(r, hit + 1, file_table());
    CHECK(passed_on == doctest::Approx(red.v.y).epsilon(1e-3));
    CHECK(passed_on > 0.8 * cue_speed);
    CHECK(norm_xy(r.ball_at(1, chained.t).v) < 0.2 * cue_speed);
}

TEST_CASE("record query impl: a hand-built chain replays the earlier events at its instant") {
    ShotRecord r = two_ball_record();
    const BallState a_at = evolve(r.segments[0][0].start, 0.5, r.params, {});
    r.segments.push_back({{0.0, at_rest(a_at.r.x + 4.0 * kR, 0.0), {}}});
    r.final_state.balls.push_back(r.segments[2][0].start);
    r.events = {{0.5, EventKind::ball_ball, 0, 1, -1, Motion::rolling, Motion::rolling},
                {0.5, EventKind::ball_ball, 1, 2, -1, Motion::stationary, Motion::sliding},
                {0.5, EventKind::ball_ball, 2, 1, -1, Motion::stationary, Motion::sliding}};
    BallState a = a_at;
    BallState b = r.segments[1][0].start;
    collide_balls(a, b, r.params);
    REQUIRE(b.v.x > 0.5);
    CHECK(event_impact_speed(r, 0, TableShape{}) == doctest::Approx(a_at.v.x).epsilon(1e-15));
    CHECK(event_impact_speed(r, 1, TableShape{}) == doctest::Approx(b.v.x).epsilon(1e-15));
    // By then ball 1 has passed its speed on to ball 2, which leaves it behind.
    CHECK(event_impact_speed(r, 2, TableShape{}) == 0.0);

    // A pot earlier at the instant leaves the ball still.
    r.events = {{0.5, EventKind::pocket, 0, -1, 1, Motion::rolling, Motion::pocketed},
                {0.5, EventKind::ball_ball, 0, 1, -1, Motion::pocketed, Motion::pocketed}};
    CHECK(event_impact_speed(r, 1, TableShape{}) == 0.0);
}

TEST_CASE("record query impl: at t = 0 a ball an event touched enters as the strike left it") {
    // The cue ball frozen to a red: the record's first segment for each is after their contact.
    SimInput in = straight_pot();
    in.start.balls[1] = at_rest(0.0, 0.2 + 2.0 * kR);
    const ShotRecord r = simulate(in, file_table(), file_params());
    const int hit = find_event(r, EventKind::ball_ball, 0);
    REQUIRE(hit > 0);
    REQUIRE(r.events[static_cast<std::size_t>(hit)].t == 0.0);
    CHECK(event_impact_speed(r, static_cast<std::size_t>(hit), file_table()) ==
          doctest::Approx(r.strike.v.y).epsilon(1e-12));
}

TEST_CASE("record query impl: ball_line reads the velocity into the cushion along its normal") {
    ShotRecord r = two_ball_record();
    TableShape t;
    t.lines.push_back({{1.0, -1.0}, {1.0, 1.0}, {-1.0, 0.0}});
    t.lines.push_back({{-1.0, 1.0}, {1.0, 1.0}, {0.0, -1.0}});
    r.events = {{0.5, EventKind::ball_line, 0, -1, 0, Motion::rolling, Motion::rolling},
                {0.5, EventKind::ball_line, 0, -1, 1, Motion::rolling, Motion::rolling},
                {0.5, EventKind::ball_line, 0, -1, 7, Motion::rolling, Motion::rolling}};
    const BallState before = evolve(r.segments[0][0].start, 0.5, r.params, {});
    CHECK(event_impact_speed(r, 0, t) == doctest::Approx(before.v.x).epsilon(1e-15));
    CHECK(event_impact_speed(r, 1, t) == 0.0);  // moving along that line, not into it
    CHECK(event_impact_speed(r, 2, t) == 0.0);  // no such line
}

TEST_CASE("record query impl: ball_arc reads the velocity along the radius from the jaw's centre") {
    ShotRecord r = two_ball_record();
    const BallState before = evolve(r.segments[0][0].start, 0.5, r.params, {});
    TableShape t;
    ArcCushion arc;
    // The jaw's centre lies 45 degrees off the ball's heading, so the normal takes 1/sqrt(2) of it.
    arc.centre = {before.r.x + 0.1 / std::sqrt(2.0), before.r.y + 0.1 / std::sqrt(2.0)};
    arc.radius = 0.1 - kR;
    t.arcs.push_back(arc);
    r.events = {{0.5, EventKind::ball_arc, 0, -1, 0, Motion::rolling, Motion::rolling},
                {0.5, EventKind::ball_arc, 0, -1, 1, Motion::rolling, Motion::rolling}};
    CHECK(event_impact_speed(r, 0, t) == doctest::Approx(before.v.x / std::sqrt(2.0)).epsilon(1e-12));
    CHECK(event_impact_speed(r, 1, t) == 0.0);
}

TEST_CASE("record query impl: pocket reads the speed; other kinds and indices past the list read 0") {
    ShotRecord r = two_ball_record();
    r.events = {{0.5, EventKind::pocket, 0, -1, 2, Motion::rolling, Motion::pocketed},
                {0.25, EventKind::transition, 0, -1, -1, Motion::sliding, Motion::rolling},
                {0.25, EventKind::nap, 0, -1, -1, Motion::rolling, Motion::rolling},
                {0.5, EventKind::guard, 0, -1, -1, Motion::rolling, Motion::stationary},
                {0.5, EventKind::pocket, 9, -1, 2, Motion::rolling, Motion::pocketed}};
    const BallState before = evolve(r.segments[0][0].start, 0.5, r.params, {});
    CHECK(event_impact_speed(r, 0, TableShape{}) == doctest::Approx(std::hypot(before.v.x, before.v.y)));
    for (std::size_t i = 1; i < r.events.size(); ++i) CHECK(event_impact_speed(r, i, TableShape{}) == 0.0);
    CHECK(event_impact_speed(r, r.events.size(), TableShape{}) == 0.0);
    CHECK(event_impact_speed(r, 1000000, TableShape{}) == 0.0);
}

TEST_CASE("record query impl: a simulated straight pot reads plausible speeds") {
    const ShotRecord r = simulate(straight_pot(), file_table(), file_params());
    const int hit = find_event(r, EventKind::ball_ball, 0);
    const int pot = find_event(r, EventKind::pocket, 1);
    REQUIRE(hit > 0);
    REQUIRE(pot > hit);
    const double t = r.events[static_cast<std::size_t>(hit)].t;
    const BallState cue_before = r.ball_at(0, t - 1e-9);
    CHECK(event_impact_speed(r, static_cast<std::size_t>(hit), file_table()) ==
          doctest::Approx(cue_before.v.y).epsilon(1e-6));
    CHECK(event_impact_speed(r, static_cast<std::size_t>(pot), file_table()) > 0.1);
    for (std::size_t i = 0; i < r.events.size(); ++i) {
        const double s = event_impact_speed(r, i, file_table());
        CHECK(std::isfinite(s));
        CHECK(s >= 0.0);
    }
}

TEST_CASE("record query impl: a path holds every segment start, the final position and short steps") {
    const ShotRecord r = simulate(straight_pot(), file_table(), file_params());
    const double step = 0.01;
    for (int ball = 0; ball < 2; ++ball) {
        CAPTURE(ball);
        const auto& segs = r.segments[static_cast<std::size_t>(ball)];
        const std::vector<ofs::table::Vec2> path = sample_path(r, ball, step, 4096);
        REQUIRE(path.size() >= segs.size() + 1);
        CHECK(path.size() <= 4096);
        CHECK(same_point(path.front(), r.segments[static_cast<std::size_t>(ball)][0].start.r));
        CHECK(same_point(path.back(), r.final_state.balls[static_cast<std::size_t>(ball)].r));
        std::size_t next = 0;
        for (const ofs::table::Vec2& p : path) {
            if (next < segs.size() && same_point(p, segs[next].start.r)) ++next;
        }
        CHECK(next == segs.size());
        // Nothing here moves faster than 2.5 m/s, so 0.01 s apart is at most 2.5 cm.
        for (std::size_t i = 1; i < path.size(); ++i) {
            CHECK(std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y) <= 0.025);
        }
    }
    const std::vector<ofs::table::Vec2> cue = sample_path(r, 0, step, 4096);
    CHECK(cue.size() > r.segments[0].size() + 10);
}

TEST_CASE("record query impl: a path widens its step to fit max_points and keeps segment starts first") {
    const ShotRecord r = simulate(straight_pot(), file_table(), file_params());
    const auto& segs = r.segments[0];
    const std::size_t n = segs.size();
    REQUIRE(n >= 2);

    const std::size_t limit = n + 1 + 5;
    const std::vector<ofs::table::Vec2> fitted = sample_path(r, 0, 1e-4, limit);
    CHECK(fitted.size() <= limit);
    CHECK(fitted.size() > n + 1);  // the widened step still samples inside segments
    std::size_t next = 0;
    for (const ofs::table::Vec2& p : fitted) {
        if (next < n && same_point(p, segs[next].start.r)) ++next;
    }
    CHECK(next == n);
    CHECK(same_point(fitted.back(), r.final_state.balls[0].r));

    // A step of 0 asks for infinitely many points: the budget sets the step.
    const std::vector<ofs::table::Vec2> zero = sample_path(r, 0, 0.0, 64);
    CHECK(zero.size() <= 64);
    CHECK(zero.size() > n + 1);

    // Fewer points than segment starts: the first starts, then the final position.
    const std::vector<ofs::table::Vec2> few = sample_path(r, 0, 0.01, n);
    REQUIRE(few.size() == n);
    for (std::size_t k = 0; k + 1 < n; ++k) CHECK(same_point(few[k], segs[k].start.r));
    CHECK(same_point(few.back(), r.final_state.balls[0].r));
    const std::vector<ofs::table::Vec2> one = sample_path(r, 0, 0.01, 1);
    REQUIRE(one.size() == 1);
    CHECK(same_point(one[0], segs[0].start.r));
}

TEST_CASE("record query impl: path limits") {
    const ShotRecord r = simulate(straight_pot(), file_table(), file_params());
    CHECK(sample_path(r, -1, 0.01, 100).empty());
    CHECK(sample_path(r, 2, 0.01, 100).empty());
    CHECK(sample_path(r, 0, 0.01, 0).empty());
    CHECK(sample_path(ShotRecord{}, 0, 0.01, 100).empty());

    // A ball that never moves: its start, then its final position.
    SimInput in = straight_pot();
    in.start.balls.push_back(at_rest(-1.0, -0.5));
    const ShotRecord still = simulate(in, file_table(), file_params());
    const std::vector<ofs::table::Vec2> path = sample_path(still, 2, 0.01, 100);
    REQUIRE(path.size() == 2);
    CHECK(path[0].x == -1.0);
    CHECK(path[0].y == -0.5);
    CHECK(path[1].x == -1.0);
    CHECK(path[1].y == -0.5);
}
