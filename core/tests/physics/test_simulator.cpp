#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/log_schema.hpp"
#include "ofs/physics/collide.hpp"
#include "ofs/physics/cue.hpp"
#include "ofs/physics/motion.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/physics/repro.hpp"
#include "ofs/physics/simulator.hpp"
#include "ofs/rng.hpp"

using namespace ofs::phys;

namespace {

constexpr double kR = ofs::table::kBallRadius;

nlohmann::json read_data(const char* name) {
    std::ifstream in(std::string(OFS_CORE_DATA_DIR) + "/" + name);
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

const TableShape& table() {
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

const nlohmann::json& schema() {
    static const nlohmann::json s = ofs::log::load_schema(std::string(OFS_CORE_DATA_DIR) + "/log_schema.json");
    return s;
}

struct Capture : ofs::log::Sink {
    std::vector<ofs::log::Event> events;
    void write(const ofs::log::Event& event, const std::string&) override { events.push_back(event); }
};

// Simulates with a capture sink attached; returns what was logged.
std::vector<ofs::log::Event> simulate_capturing(const SimInput& in, const PhysicsParams& p, ShotRecord& out) {
    auto capture = std::make_shared<Capture>();
    ofs::log::Logger::instance().clear_sinks();
    ofs::log::Logger::instance().add_sink(capture);
    out = simulate(in, table(), p);
    ofs::log::Logger::instance().clear_sinks();
    return capture->events;
}

BallState ball(ofs::table::Ball kind, int index, double x, double y) {
    BallState b;
    b.ball = kind;
    b.index = index;
    b.r = {x, y, 0.0};
    return b;
}

// The opening rack with the cue ball driven hard into the apex red.
SimInput break_off(std::uint64_t seed) {
    SimInput in;
    in.start = opening_rack(seed, file_params());
    const BallState& cue = in.start.balls[0];
    const BallState& apex = in.start.balls[1];
    in.cue_ball = 0;
    in.has_strike = true;
    in.strike.aim_rad = std::atan2(apex.r.y - cue.r.y, apex.r.x - cue.r.x);
    in.strike.speed_m_s = 6.0;
    in.seed = seed;
    in.params_hash = "test";
    return in;
}

bool at_rest(const BallState& b) {
    return b.motion == Motion::pocketed ||
           (b.motion == Motion::stationary && b.v == ofs::Vec3{} && b.w == ofs::Vec3{});
}

void check_anomaly_line(const ofs::log::Event& e, const SimInput& in, const PhysicsParams& p, const char* kind) {
    CHECK(e.event == "physics.anomaly");
    CHECK(e.subsystem == "physics");
    CHECK(e.level == ofs::log::Level::warn);
    const auto problem = ofs::log::check(e, schema());
    CHECK_MESSAGE(!problem.has_value(), problem.value_or(""));
    CHECK(e.data["kind"] == kind);
    SimInput back;
    PhysicsParams back_p;
    std::string error;
    REQUIRE_MESSAGE(parse_bundle(e.data["bundle"], back, back_p, error), error);
    CHECK(make_bundle(back, back_p) == make_bundle(in, p));
}

void check_no_overlaps(const TableState& s) {
    for (std::size_t i = 0; i < s.balls.size(); ++i) {
        for (std::size_t j = i + 1; j < s.balls.size(); ++j) {
            if (s.balls[i].motion == Motion::pocketed || s.balls[j].motion == Motion::pocketed) continue;
            const double d = std::hypot(s.balls[i].r.x - s.balls[j].r.x, s.balls[i].r.y - s.balls[j].r.y);
            if (d < 2.0 * kR - 1e-7) FAIL_CHECK("balls " << i << " and " << j << " overlap by " << 2.0 * kR - d);
        }
    }
}

// Every ball's segments are in time order, start at 0, and each later one starts at an event.
void check_record_shape(const ShotRecord& r, std::size_t n) {
    REQUIRE(r.segments.size() == n);
    for (std::size_t k = 1; k < r.events.size(); ++k) CHECK(r.events[k - 1].t <= r.events[k].t);
    for (std::size_t i = 0; i < n; ++i) {
        CAPTURE(i);
        REQUIRE_FALSE(r.segments[i].empty());
        CHECK(r.segments[i].front().t0 == 0.0);
        for (std::size_t k = 1; k < r.segments[i].size(); ++k) {
            CHECK(r.segments[i][k - 1].t0 < r.segments[i][k].t0);
            bool at_event = false;
            for (const ShotEvent& e : r.events) {
                at_event = at_event || (e.t == r.segments[i][k].t0 &&
                                        (e.a == static_cast<int>(i) || e.b == static_cast<int>(i) ||
                                         e.kind == EventKind::guard));
            }
            CHECK(at_event);
        }
    }
}

}  // namespace

TEST_CASE("simulate a struck rack: the strike at t = 0, a segment per ball from 0, all at rest") {
    const SimInput in = break_off(147);
    const PhysicsParams& p = file_params();
    ShotRecord r;
    const auto logged = simulate_capturing(in, p, r);
    CHECK(logged.empty());
    CHECK(r.has_strike);
    CHECK(physics_params_values(r.params) == physics_params_values(p));
    REQUIRE_FALSE(r.events.empty());
    CHECK(r.events[0].t == 0.0);
    CHECK(r.events[0].kind == EventKind::strike);
    CHECK(r.events[0].a == 0);
    check_record_shape(r, in.start.balls.size());
    REQUIRE(r.final_state.balls.size() == in.start.balls.size());
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
    CHECK(r.anomalies.empty());
    CHECK(r.t_end >= 0.0);
    CHECK(r.t_end <= p.time_cap_s);
    CHECK(r.sim_us > 0.0);
}

TEST_CASE("simulate keeps the strike outcome that cue.hpp's strike() gives with Rng(seed)") {
    const SimInput in = break_off(11);
    const PhysicsParams& p = file_params();
    const ShotRecord r = simulate(in, table(), p);
    ofs::Rng rng(in.seed);
    const StrikeOutcome want = strike(in.start, in.cue_ball, in.strike, table(), p, rng);
    CHECK(r.strike.v == want.v);
    CHECK(r.strike.w == want.w);
    CHECK(r.strike.elevation_used_rad == want.elevation_used_rad);
    CHECK(r.strike.elevation_raised == want.elevation_raised);
    CHECK(r.strike.squirt_rad == want.squirt_rad);
    CHECK(r.strike.miscue == want.miscue);
}

TEST_CASE("simulate without a strike leaves a resting table as it is") {
    SimInput in = break_off(3);
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    CHECK_FALSE(r.has_strike);
    CHECK(r.events.empty());
    CHECK(r.t_end == 0.0);
    CHECK(r.anomalies.empty());
    REQUIRE(r.final_state.balls.size() == in.start.balls.size());
    for (std::size_t i = 0; i < in.start.balls.size(); ++i) {
        CHECK(r.final_state.balls[i].r == in.start.balls[i].r);
        CHECK(r.segments[i].size() == 1);
    }
}

TEST_CASE("simulate is deterministic: the same input gives a byte-identical record_json") {
    const SimInput in = break_off(2026);
    const std::string a = record_json(simulate(in, table(), file_params())).dump();
    const std::string b = record_json(simulate(in, table(), file_params())).dump();
    CHECK(a == b);
}

TEST_CASE("simulate stops a ball still moving at time_cap_s, records the guard and logs it once") {
    SimInput in;
    BallState cue = ball(ofs::table::Ball::cue, 0, -0.5, 0.0);
    cue.v = {1.0, 0.0, 0.0};
    cue.w = {0.0, 1.0 / kR, 0.0};
    cue.motion = Motion::rolling;
    in.start.balls = {cue};
    in.has_strike = false;
    in.seed = 99;
    PhysicsParams p = file_params();
    p.time_cap_s = 2.0;
    ShotRecord r;
    const auto logged = simulate_capturing(in, p, r);

    REQUIRE(r.anomalies.size() == 1);
    CHECK(r.anomalies[0].kind == "time_cap");
    CHECK(r.anomalies[0].t == 2.0);
    CHECK(r.anomalies[0].balls == std::vector<int>{0});
    REQUIRE_FALSE(r.events.empty());
    CHECK(r.events.back().kind == EventKind::guard);
    CHECK(r.events.back().t == 2.0);
    CHECK(r.t_end == 2.0);
    CHECK(at_rest(r.final_state.balls[0]));
    CHECK(r.segments[0].back().t0 == 2.0);
    CHECK(r.segments[0].back().start.motion == Motion::stationary);

    REQUIRE(logged.size() == 1);
    check_anomaly_line(logged[0], in, p, "time_cap");
    CHECK(logged[0].data["t"] == 2.0);
}

TEST_CASE("simulate pushes balls overlapping in the start state apart to touching") {
    SimInput in;
    in.start.balls = {ball(ofs::table::Ball::red, 0, 0.0, 0.3), ball(ofs::table::Ball::red, 1, 2.0 * kR - 0.001, 0.3)};
    in.has_strike = false;
    ShotRecord r;
    const auto logged = simulate_capturing(in, file_params(), r);
    REQUIRE(r.anomalies.size() == 1);
    CHECK(r.anomalies[0].kind == "overlap");
    CHECK(r.anomalies[0].balls == std::vector<int>{0, 1});
    CHECK(r.final_state.balls[0].r == in.start.balls[0].r);
    CHECK(r.final_state.balls[1].r.x == 2.0 * kR);
    CHECK(r.final_state.balls[1].r.y == 0.3);
    CHECK(r.segments[1][0].start.r.x == 2.0 * kR);
    REQUIRE(logged.size() == 1);
    check_anomaly_line(logged[0], in, file_params(), "overlap");
}

TEST_CASE("simulate records every anomaly but logs each kind once, with the count") {
    SimInput in;
    in.start.balls = {ball(ofs::table::Ball::red, 0, 0.0, 0.0), ball(ofs::table::Ball::red, 1, 0.05, 0.0),
                      ball(ofs::table::Ball::red, 2, 0.0, 0.5), ball(ofs::table::Ball::red, 3, 0.0, 0.54)};
    in.has_strike = false;
    ShotRecord r;
    const auto logged = simulate_capturing(in, file_params(), r);
    REQUIRE(r.anomalies.size() == 2);
    CHECK(r.anomalies[0].balls == std::vector<int>{0, 1});
    CHECK(r.anomalies[1].balls == std::vector<int>{2, 3});
    REQUIRE(logged.size() == 1);
    check_anomaly_line(logged[0], in, file_params(), "overlap");
    CHECK(logged[0].data["detail"].get<std::string>().find("(2 in this shot)") != std::string::npos);
}

TEST_CASE("simulate does not treat exactly touching balls as overlapping") {
    // frozen_line_3.json: rounding leaves the last pair 3e-17 m inside 2R.
    SimInput in;
    in.start.balls = {ball(ofs::table::Ball::cue, 0, -0.5, 0.0), ball(ofs::table::Ball::red, 0, 0.3, 0.0),
                      ball(ofs::table::Ball::red, 1, 0.3525, 0.0),
                      ball(ofs::table::Ball::red, 2, 0.40499999999999997, 0.0)};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    CHECK(r.anomalies.empty());
    for (std::size_t i = 0; i < 4; ++i) CHECK(r.final_state.balls[i].r == in.start.balls[i].r);
}

TEST_CASE("simulate puts a ball with a non-finite start state at rest and reports it") {
    SimInput in;
    BallState b = ball(ofs::table::Ball::red, 0, 0.2, 0.1);
    b.v = {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0};
    BallState c = ball(ofs::table::Ball::red, 1, std::numeric_limits<double>::infinity(), -0.4);
    in.start.balls = {b, c};
    in.has_strike = false;
    ShotRecord r;
    const auto logged = simulate_capturing(in, file_params(), r);
    REQUIRE(r.anomalies.size() == 2);
    CHECK(r.anomalies[0].kind == "nan");
    CHECK(r.anomalies[0].balls == std::vector<int>{0});
    CHECK(r.anomalies[1].balls == std::vector<int>{1});
    CHECK(at_rest(r.final_state.balls[0]));
    CHECK(r.final_state.balls[0].r == in.start.balls[0].r);
    CHECK(r.final_state.balls[1].r == ofs::Vec3{0.0, -0.4, 0.0});
    REQUIRE(logged.size() == 1);
    CHECK(logged[0].data["kind"] == "nan");
    const auto problem = ofs::log::check(logged[0], schema());
    CHECK_MESSAGE(!problem.has_value(), problem.value_or(""));
}

TEST_CASE("simulate leaves pocketed balls pocketed") {
    SimInput in;
    BallState gone = ball(ofs::table::Ball::red, 4, 1.8, 0.9);
    gone.motion = Motion::pocketed;
    gone.pocket = 2;
    in.start.balls = {ball(ofs::table::Ball::cue, 0, 0.0, 0.0), gone};
    in.cue_ball = 0;
    in.strike.aim_rad = 3.0;
    in.strike.speed_m_s = 1.0;
    const ShotRecord r = simulate(in, table(), file_params());
    CHECK(r.final_state.balls[1].motion == Motion::pocketed);
    CHECK(r.final_state.balls[1].pocket == 2);
    CHECK(r.final_state.balls[1].r == gone.r);
    CHECK(r.segments[1].size() == 1);
    for (const ShotEvent& e : r.events) CHECK((e.a != 1 && e.b != 1));
}

TEST_CASE("simulate classifies the start state, pocketed balls excepted") {
    SimInput in;
    BallState moving = ball(ofs::table::Ball::red, 0, 0.0, 0.0);
    moving.v = {0.5, 0.0, 0.0};
    moving.motion = Motion::stationary;
    in.start.balls = {moving};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    CHECK(r.segments[0][0].start.motion == Motion::sliding);
}

TEST_CASE("simulate a straight pot: the red ends in the pocket") {
    // The red and cue ball on the 45 degree line into the top-left corner (+x, +y).
    const double cx = ofs::table::kPlayingLength / 2.0;
    const double cy = ofs::table::kPlayingWidth / 2.0;
    SimInput in;
    in.start.balls = {ball(ofs::table::Ball::cue, 0, cx - 0.8, cy - 0.8),
                      ball(ofs::table::Ball::red, 0, cx - 0.4, cy - 0.4)};
    in.cue_ball = 0;
    in.strike.aim_rad = std::atan2(1.0, 1.0);
    in.strike.speed_m_s = 2.0;
    const ShotRecord r = simulate(in, table(), file_params());
    int top_left = -1;
    for (std::size_t k = 0; k < table().pockets.size(); ++k) {
        if (table().pockets[k].name == "top_left") top_left = static_cast<int>(k);
    }
    REQUIRE(top_left >= 0);
    CHECK(r.final_state.balls[1].motion == Motion::pocketed);
    CHECK(r.final_state.balls[1].pocket == top_left);
    bool pot_event = false;
    for (const ShotEvent& e : r.events) {
        pot_event = pot_event || (e.kind == EventKind::pocket && e.a == 1 && e.body == top_left);
    }
    CHECK(pot_event);
    CHECK(r.anomalies.empty());
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
}

TEST_CASE("simulate a break-off: it ends with every ball at rest, nothing overlapping") {
    const SimInput in = break_off(147);
    const PhysicsParams& p = file_params();
    const ShotRecord r = simulate(in, table(), p);
    MESSAGE("break-off: " << r.events.size() << " events, t_end " << r.t_end << " s, " << r.sim_us << " us");
    CHECK(r.events.size() > 50);
    CHECK(r.anomalies.empty());
    for (const Anomaly& a : r.anomalies) MESSAGE(a.kind << " at " << a.t << ": " << a.detail);
    CHECK(r.t_end < p.time_cap_s);
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
    check_no_overlaps(r.final_state);
    check_record_shape(r, in.start.balls.size());
    for (const BallState& b : r.final_state.balls) {
        if (b.motion == Motion::pocketed) continue;
        CHECK(std::abs(b.r.x) <= ofs::table::kPlayingLength / 2.0);
        CHECK(std::abs(b.r.y) <= ofs::table::kPlayingWidth / 2.0);
    }
}

TEST_CASE("simulate C13: a break-off gives a bit-identical record on every run") {
    const SimInput in = break_off(31337);
    const ShotRecord a = simulate(in, table(), file_params());
    const ShotRecord b = simulate(in, table(), file_params());
    REQUIRE(a.events.size() > 50);
    CHECK(record_json(a).dump() == record_json(b).dump());
    CHECK(make_result(a)["event_digest"] == make_result(b)["event_digest"]);
    const ShotRecord other = simulate(break_off(31338), table(), file_params());
    CHECK(record_json(other).dump() != record_json(a).dump());
}

TEST_CASE("simulate a frozen line resolves as pairwise collisions at one instant, in index order") {
    SimInput in;
    in.start.balls = {ball(ofs::table::Ball::cue, 0, -0.5, 0.0), ball(ofs::table::Ball::red, 0, 0.3, 0.0),
                      ball(ofs::table::Ball::red, 1, 0.3525, 0.0),
                      ball(ofs::table::Ball::red, 2, 0.40499999999999997, 0.0)};
    in.cue_ball = 0;
    in.strike.speed_m_s = 3.0;
    const ShotRecord r = simulate(in, table(), file_params());
    CHECK(r.anomalies.empty());
    std::size_t k = 0;
    while (k < r.events.size() && r.events[k].kind != EventKind::ball_ball) ++k;
    REQUIRE(k + 2 < r.events.size());
    // The impulse runs down the line one pair at a time; whenever an earlier pair is back in
    // contact and approaching at the same instant, it goes first (lower indices).
    CHECK(r.events[k].a == 0);
    CHECK(r.events[k].b == 1);
    CHECK(r.events[k + 1].a == 1);
    CHECK(r.events[k + 1].b == 2);
    bool end_pair = false;
    for (std::size_t n = k; n < r.events.size() && r.events[n].t - r.events[k].t <= file_params().zero_time_s; ++n) {
        CHECK(r.events[n].kind == EventKind::ball_ball);
        end_pair = end_pair || (r.events[n].a == 2 && r.events[n].b == 3);
    }
    CHECK(end_pair);
}

namespace {

// The first ball_ball event of a two-ball shot, with both balls just before and just after it.
struct Contact {
    const ShotEvent* event = nullptr;
    BallState a_before, b_before, a_after, b_after;
};

Contact first_contact(const ShotRecord& r) {
    Contact c;
    for (const ShotEvent& e : r.events) {
        if (e.kind != EventKind::ball_ball) continue;
        c.event = &e;
        // The segments that end at the contact, evolved to it, and the ones it starts.
        for (int ball : {e.a, e.b}) {
            const std::vector<Segment>& segs = r.segments[static_cast<std::size_t>(ball)];
            std::size_t k = 0;
            while (k + 1 < segs.size() && segs[k + 1].t0 < e.t) ++k;
            BallState before =
                evolve(segs[k].start, std::min(e.t - segs[k].t0, transition_time(segs[k].start, r.params, segs[k].nap)),
                       r.params, segs[k].nap);
            BallState after = segs[k + 1 < segs.size() ? k + 1 : k].start;
            (ball == e.a ? c.a_before : c.b_before) = before;
            (ball == e.a ? c.a_after : c.b_after) = after;
        }
        break;
    }
    return c;
}

double pair_energy(const BallState& a, const BallState& b, const PhysicsParams& p) {
    return kinetic_energy(a, p) + kinetic_energy(b, p);
}

}  // namespace

TEST_CASE("simulate a slow contact as a push: both leave rolling at one speed along n") {
    // The cue ball rolls into a resting red at 0.5 mm/s, under ball_rest_speed_m_s.
    SimInput in;
    BallState cue = ball(ofs::table::Ball::cue, 0, 0.0, 0.0);
    cue.v = {5e-4, 0.0, 0.0};
    cue.w = {0.0, 5e-4 / kR, 0.0};
    in.start.balls = {cue, ball(ofs::table::Ball::red, 0, 2.0 * kR + 1e-6, 0.0)};
    in.has_strike = false;
    const PhysicsParams& p = file_params();
    REQUIRE(5e-4 < p.ball_rest_speed_m_s);
    const ShotRecord r = simulate(in, table(), p);
    CHECK(r.anomalies.empty());
    const Contact c = first_contact(r);
    REQUIRE(c.event != nullptr);
    const ofs::Vec3 n = ofs::unit_or_zero(c.b_before.r - c.a_before.r);
    CHECK(c.a_after.motion == Motion::rolling);
    CHECK(c.b_after.motion == Motion::rolling);
    CHECK(ofs::dot(c.a_after.v, n) == doctest::Approx(ofs::dot(c.b_after.v, n)).epsilon(1e-12));
    // A ball rolling at v into a resting one: both roll on at v / 2.
    CHECK(ofs::dot(c.a_after.v, n) == doctest::Approx(0.5 * ofs::dot(c.a_before.v, n)).epsilon(1e-9));
    CHECK(pair_energy(c.a_after, c.b_after, p) < pair_energy(c.a_before, c.b_before, p));
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
}

TEST_CASE("simulate a push between two moving balls keeps energy falling and ends the chase") {
    // A sliding cue ball with topspin catching a red that rolls slower along a slightly
    // different heading: the case that collapsed into endless re-collisions before pushes.
    SimInput in;
    BallState cue = ball(ofs::table::Ball::cue, 0, 0.0, 0.0);
    cue.v = {0.3, 0.0, 0.0};
    cue.w = {0.0, 0.6 / kR, 0.0};
    BallState red = ball(ofs::table::Ball::red, 0, 2.0 * kR + 1e-6, 0.002);
    red.v = {0.3, 0.02, 0.0};
    red.w = {-0.02 / kR, 0.3 / kR, 0.0};
    in.start.balls = {cue, red};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    CHECK(r.anomalies.empty());
    std::size_t contacts = 0;
    for (const ShotEvent& e : r.events) contacts += e.kind == EventKind::ball_ball ? 1 : 0;
    MESSAGE(contacts << " contacts, " << r.events.size() << " events");
    CHECK(contacts < 200);
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
}

TEST_CASE("simulate a contact above ball_rest_speed_m_s goes through collide_balls unchanged") {
    // straight_1m.json struck centre-ball: the stun of C2 when the cue ball still slides.
    SimInput in;
    in.start.balls = {ball(ofs::table::Ball::cue, 0, -0.5, 0.0), ball(ofs::table::Ball::red, 0, 0.5, 0.0)};
    in.cue_ball = 0;
    in.strike.speed_m_s = 4.0;
    const PhysicsParams& p = file_params();
    const ShotRecord r = simulate(in, table(), p);
    const Contact c = first_contact(r);
    REQUIRE(c.event != nullptr);
    BallState a = c.a_before;
    BallState b = c.b_before;
    collide_balls(a, b, p);
    CHECK(c.a_after.v == a.v);
    CHECK(c.a_after.w == a.w);
    CHECK(c.b_after.v == b.v);
    CHECK(c.b_after.w == b.w);
}

TEST_CASE("simulate 500 varied break-offs: no guard trips, no overlaps, energy never rises") {
    const PhysicsParams& p = file_params();
    std::size_t with_anomalies = 0;
    for (std::uint64_t seed = 0; seed < 500; ++seed) {
        CAPTURE(seed);
        SimInput in = break_off(seed);
        in.strike.speed_m_s = 3.0 + static_cast<double>(seed % 8);
        in.strike.aim_rad += 0.002 * static_cast<double>(static_cast<int>(seed % 11) - 5);
        const ShotRecord r = simulate(in, table(), p);
        with_anomalies += r.anomalies.empty() ? 0 : 1;
        for (const Anomaly& a : r.anomalies) MESSAGE("seed " << seed << ": " << a.kind << " " << a.detail);
        check_no_overlaps(r.final_state);
        for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
        double last = std::numeric_limits<double>::infinity();
        for (const ShotEvent& e : r.events) {
            if (e.kind == EventKind::strike) continue;
            double ke = 0.0;
            for (const BallState& b : r.state_at(e.t).balls) ke += kinetic_energy(b, p);
            CHECK(ke <= last * (1.0 + 1e-9) + 1e-12);
            last = ke;
        }
    }
    CHECK(with_anomalies == 0);
}

TEST_CASE("simulate max_events guard: the shot ends at the limit with every ball at rest") {
    const SimInput in = break_off(5);
    PhysicsParams p = file_params();
    p.max_events = 100;  // the lowest the parameter file allows, so the logged bundle replays
    ShotRecord r;
    const auto logged = simulate_capturing(in, p, r);
    REQUIRE(r.events.size() == 101);
    CHECK(r.events.back().kind == EventKind::guard);
    REQUIRE_FALSE(r.anomalies.empty());
    CHECK(r.anomalies.back().kind == "event_guard");
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
    REQUIRE(logged.size() == 1);
    check_anomaly_line(logged[0], in, p, "event_guard");
}

TEST_CASE("simulate zero-time chain guard: too many simultaneous events end the shot") {
    SimInput in;
    in.start.balls = {ball(ofs::table::Ball::cue, 0, -0.5, 0.0), ball(ofs::table::Ball::red, 0, 0.3, 0.0),
                      ball(ofs::table::Ball::red, 1, 0.3525, 0.0),
                      ball(ofs::table::Ball::red, 2, 0.40499999999999997, 0.0)};
    in.cue_ball = 0;
    in.strike.speed_m_s = 3.0;
    PhysicsParams p = file_params();
    p.max_zero_time_chain = 2;
    ShotRecord r;
    const auto logged = simulate_capturing(in, p, r);
    REQUIRE_FALSE(r.anomalies.empty());
    CHECK(r.anomalies.back().kind == "zero_time_chain");
    CHECK(r.events.back().kind == EventKind::guard);
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
    REQUIRE(logged.size() == 1);
    check_anomaly_line(logged[0], in, p, "zero_time_chain");
}

namespace {

// The deepest any two balls sink into each other, or a ball into a cushion line, at any event
// time: the fuzz's check (tools/sim_cli/shot_checks.cpp) fails anything past 1e-9 m.
double deepest_overlap(const ShotRecord& r) {
    double worst = 0.0;
    std::vector<double> times{0.0, r.t_end};
    for (const ShotEvent& e : r.events) times.push_back(e.t);
    for (const double t : times) {
        const TableState s = r.state_at(t);
        for (std::size_t i = 0; i < s.balls.size(); ++i) {
            const BallState& a = s.balls[i];
            if (a.motion == Motion::pocketed) continue;
            for (std::size_t j = i + 1; j < s.balls.size(); ++j) {
                const BallState& b = s.balls[j];
                if (b.motion == Motion::pocketed) continue;
                worst = std::max(worst, 2.0 * kR - std::hypot(a.r.x - b.r.x, a.r.y - b.r.y));
            }
            for (const LineCushion& c : table().lines) {
                const double gap = (a.r.x - c.p1.x) * c.normal.x + (a.r.y - c.p1.y) * c.normal.y - kR;
                const double len = std::hypot(c.p2.x - c.p1.x, c.p2.y - c.p1.y);
                const double along = ((a.r.x - c.p1.x) * (c.p2.x - c.p1.x) + (a.r.y - c.p1.y) * (c.p2.y - c.p1.y)) / len;
                if (gap > -kR && along >= 0.0 && along <= len) worst = std::max(worst, -gap);
            }
        }
    }
    return worst;
}

const LineCushion& baulk_cushion() {
    for (const LineCushion& c : table().lines) {
        if (c.normal.x == 1.0) return c;
    }
    FAIL("no baulk cushion");
    return table().lines.front();
}

BallState on_baulk_cushion(ofs::table::Ball kind, int index, double y) {
    return ball(kind, index, baulk_cushion().p1.x + kR, y);
}

void check_clean(const ShotRecord& r) {
    for (const Anomaly& a : r.anomalies) FAIL_CHECK(a.kind << ": " << a.detail);
    CHECK(deepest_overlap(r) <= 1e-9);
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
    MESSAGE(r.events.size() << " events");
}

}  // namespace

TEST_CASE("simulate a ball spun into the cushion it touches: it settles and rolls along it") {
    // fuzz seed 1 shots 19989, 19996: a ball left sliding along a cushion with its slip pointing out
    // of it, so cloth friction drives it back in with no normal speed to bounce on. Detection found
    // the contact at t = 0 each time, the cushion model had nothing to do, and the chain never
    // ended. Now it is a push: the ball settles to rolling along the cushion and is held there.
    SimInput in;
    BallState cue = on_baulk_cushion(ofs::table::Ball::cue, 0, 0.0);
    cue.v = {0.0, 0.4, 0.0};
    cue.w = {-0.4 / kR, -10.0, 0.0};  // slip u.x = +10 R: friction drives the ball into the cushion
    in.start.balls = {cue};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    check_clean(r);
    CHECK(r.events.size() < 20);
    const BallState& end = r.final_state.balls[0];
    // It leaves the cushion rolling along it; the nap may then carry it a few mm off, never in.
    REQUIRE(r.events.size() >= 2);
    CHECK(r.events[0].kind == EventKind::ball_line);
    CHECK(r.events[0].to == Motion::rolling);
    const BallState after = r.segments[0][0].start;  // the t = 0 push replaced the start segment
    CHECK(after.v.x == 0.0);
    CHECK(after.v.y > 0.3);
    CHECK(end.r.x >= baulk_cushion().p1.x + kR - 1e-12);
    CHECK(end.r.y > 0.5);
}

TEST_CASE("simulate a ball rolling into a cushion slower than ball_rest_speed_m_s: no bounce chain") {
    SimInput in;
    BallState cue = ball(ofs::table::Ball::cue, 0, baulk_cushion().p1.x + kR + 1e-7, 0.0);
    cue.v = {-5e-4, 2e-3, 0.0};
    cue.w = {-2e-3 / kR, -5e-4 / kR, 0.0};
    in.start.balls = {cue};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    check_clean(r);
    CHECK(r.events.size() < 20);
}

TEST_CASE("simulate a slow push into a frozen pair resolves the three balls in one event") {
    // fuzz seed 1 shots 19759, 19822 (break-offs): one pair's push re-closed the next pair's
    // contact, and pairwise pushes passed the speed back and forth for 100 simultaneous events.
    SimInput in;
    BallState cue = ball(ofs::table::Ball::cue, 0, 0.0, 0.0);
    cue.v = {5e-4, 0.0, 0.0};
    cue.w = {0.0, 5e-4 / kR, 0.0};
    in.start.balls = {cue, ball(ofs::table::Ball::red, 0, 2.0 * kR + 1e-6, 0.0),
                      ball(ofs::table::Ball::red, 1, 4.0 * kR + 1e-6, 0.0)};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    check_clean(r);
    const Contact c = first_contact(r);
    REQUIRE(c.event != nullptr);
    std::size_t contacts = 0;
    for (const ShotEvent& e : r.events) contacts += e.kind == EventKind::ball_ball ? 1 : 0;
    CHECK(contacts <= 2);
    // All three leave at a third of the speed, the third ball's contact resolved at the same instant.
    const double third = c.a_before.v.x / 3.0;
    CHECK(c.a_after.v.x == doctest::Approx(third).epsilon(1e-9));
    bool third_pushed = false;
    for (const Segment& s : r.segments[2]) {
        if (s.t0 != c.event->t) continue;
        third_pushed = true;
        CHECK(s.start.v.x == doctest::Approx(third).epsilon(1e-9));
    }
    CHECK(third_pushed);
}

TEST_CASE("simulate a pushing pair rolls on touching, never sinking into each other") {
    // fuzz seed 1, 1,140 break-off shots: after a push, rolling resistance along two headings drove
    // the pair together, and the hold-back that let them sink 5e-8 m before re-pushing showed as
    // overlaps (and thousands of re-pushes). A held contact keeps their normal accelerations equal.
    SimInput in;
    BallState a = ball(ofs::table::Ball::cue, 0, 0.0, 0.0);
    a.v = {0.3, 0.2, 0.0};
    a.w = {-0.2 / kR, 0.3 / kR, 0.0};
    BallState b = ball(ofs::table::Ball::red, 0, 2.0 * kR, 0.0);
    b.v = {0.3, 0.0, 0.0};
    b.w = {0.0, 0.3 / kR, 0.0};
    in.start.balls = {a, b};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    check_clean(r);
    CHECK(r.events.size() < 30);
}

TEST_CASE("simulate a ball rolling slowly into a ball frozen on a cushion: squeezed, then held") {
    SimInput in;
    BallState cue = ball(ofs::table::Ball::cue, 0, baulk_cushion().p1.x + 3.0 * kR + 1e-6, 0.01);
    cue.v = {-5e-4, 1e-4, 0.0};
    cue.w = {-1e-4 / kR, -5e-4 / kR, 0.0};
    in.start.balls = {cue, on_baulk_cushion(ofs::table::Ball::red, 0, 0.0)};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    check_clean(r);
    CHECK(r.events.size() < 30);
}

TEST_CASE("simulate a ball rolling between two resting balls almost in line with it") {
    // fuzz seed 7 shot 15001: the two contacts' normals differ by 0.003 rad, which Gauss-Seidel
    // alone closes on at ~1e-5 per sweep; the pushes alternated for 100 simultaneous events.
    const ofs::Vec3 u = ofs::unit_or_zero(ofs::Vec3{0.862, -0.507, 0.0});
    const ofs::Vec3 u2 = ofs::unit_or_zero(ofs::Vec3{0.861, -0.509, 0.0});
    const ofs::Vec3 mid{1.0805, -0.0262, 0.0};
    SimInput in;
    BallState rolling = ball(ofs::table::Ball::red, 1, mid.x, mid.y);
    rolling.v = {-1.23e-5, -2.08e-5, 0.0};
    rolling.w = {2.08e-5 / kR, -1.23e-5 / kR, 0.0};
    in.start.balls = {ball(ofs::table::Ball::red, 0, mid.x - 2.0 * kR * u.x, mid.y - 2.0 * kR * u.y), rolling,
                      ball(ofs::table::Ball::red, 2, mid.x + 2.0 * kR * u2.x, mid.y + 2.0 * kR * u2.y)};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    check_clean(r);
    CHECK(r.events.size() < 30);
}

namespace {

using ofs::table::Vec2;

double dot2(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

// A pocket's axis into it and the middle of its fall, the line through the points where its jaws
// meet their facings (table_shape.json "construction"). The facings are half_gap either side of
// the axis, so a ball whose centre stays within half_gap - R of it touches neither.
struct Mouth {
    int pocket = -1;
    Vec2 axis;
    Vec2 across;  // the axis turned anticlockwise
    Vec2 fall;
    double half_gap = 0.0;
};

Mouth mouth(const std::string& name) {
    Mouth m;
    for (std::size_t k = 0; k < table().pockets.size(); ++k) {
        if (table().pockets[k].name == name) m.pocket = static_cast<int>(k);
    }
    REQUIRE(m.pocket >= 0);
    const Pocket& pk = table().pockets[static_cast<std::size_t>(m.pocket)];
    const double sy = std::copysign(1.0, pk.drop_centre.y);
    m.axis = pk.corner ? Vec2{std::copysign(std::sqrt(0.5), pk.drop_centre.x), sy * std::sqrt(0.5)} : Vec2{0.0, sy};
    m.across = {-m.axis.y, m.axis.x};
    std::vector<Vec2> joints;
    for (const ArcCushion& a : table().arcs) {
        if (a.pocket != m.pocket) continue;
        const Vec2 s{a.centre.x + a.radius * std::cos(a.angle_start), a.centre.y + a.radius * std::sin(a.angle_start)};
        const Vec2 e{a.centre.x + a.radius * std::cos(a.angle_end), a.centre.y + a.radius * std::sin(a.angle_end)};
        // The end further into the pocket meets the facing; the other meets the cushion nose.
        joints.push_back(dot2(s, m.axis) > dot2(e, m.axis) ? s : e);
    }
    REQUIRE(joints.size() == 2);
    m.fall = {0.5 * (joints[0].x + joints[1].x), 0.5 * (joints[0].y + joints[1].y)};
    m.half_gap = 0.5 * std::abs(dot2({joints[1].x - joints[0].x, joints[1].y - joints[0].y}, m.across));
    return m;
}

// A ball that rattles out may go on round the table, so only this pocket's jaws are counted.
struct Entry {
    bool potted = false;  // in this pocket
    int jaw_hits = 0;     // on this pocket's jaws
    int line_hits = 0;
};

// A red rolling on a heading turned heading_rad from the axis, whose path crosses the fall
// `offset` across the axis. It starts 0.12 m back, fast enough to cross the fall at `speed` under
// rolling resistance had nothing been in its way.
Entry roll_into(const Mouth& m, double heading_rad, double offset, double speed) {
    const PhysicsParams& p = file_params();
    const double c = std::cos(heading_rad);
    const double s = std::sin(heading_rad);
    const Vec2 h{c * m.axis.x - s * m.axis.y, s * m.axis.x + c * m.axis.y};
    const double back = 0.12;
    const double v0 = std::sqrt(speed * speed + 2.0 * p.roll_friction * p.gravity_m_s2 * back);
    BallState red = ball(ofs::table::Ball::red, 0, m.fall.x + offset * m.across.x - back * h.x,
                         m.fall.y + offset * m.across.y - back * h.y);
    red.v = {v0 * h.x, v0 * h.y, 0.0};
    red.w = {-red.v.y / kR, red.v.x / kR, 0.0};
    red.motion = Motion::rolling;
    SimInput in;
    in.start.balls = {red};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), p);
    for (const Anomaly& a : r.anomalies) FAIL_CHECK(a.kind << ": " << a.detail);
    Entry out;
    out.potted = r.final_state.balls[0].motion == Motion::pocketed && r.final_state.balls[0].pocket == m.pocket;
    for (const ShotEvent& e : r.events) {
        if (e.kind == EventKind::ball_arc && table().arcs[static_cast<std::size_t>(e.body)].pocket == m.pocket) {
            ++out.jaw_hits;
        }
        out.line_hits += e.kind == EventKind::ball_line ? 1 : 0;
    }
    return out;
}

// The fastest ball a centre-ball strike gives (cue.cpp's impulse at max_cue_speed_m_s).
double fastest_ball(const PhysicsParams& p) {
    return (1.0 + p.cue_tip_restitution) * p.max_cue_speed_m_s / (1.0 + p.ball_mass_kg / p.cue_mass_kg);
}

}  // namespace

TEST_CASE("simulate pockets: a ball between the facings drops at every speed without touching anything") {
    // Spec 7.6: potted when the centre passes the drop boundary behind the jaws. A path parallel
    // to the axis within half_gap - R of it (15.75 mm corner, 17.75 mm middle) meets no cushion,
    // so the pot must not depend on pace: from 0.05 m/s at the fall (enough to roll the 2 mm set
    // back plus the drop circle's curve, under 5 mm) up to the fastest ball the cue can give
    // (13.9 m/s). The outer offsets keep 2 mm for the nap's sideways drift.
    const double top = fastest_ball(file_params());
    for (const char* name : {"top_left", "middle_left"}) {
        const Mouth m = mouth(name);
        const double limit = m.half_gap - kR;
        for (const double offset : {0.0, limit - 0.002, -(limit - 0.002)}) {
            for (const double speed : {0.05, 0.1, 0.3, 1.0, 3.0, 6.0, 10.0, top}) {
                CAPTURE(name);
                CAPTURE(offset);
                CAPTURE(speed);
                const Entry e = roll_into(m, 0.0, offset, speed);
                CHECK(e.potted);
                CHECK(e.jaw_hits == 0);
                CHECK(e.line_hits == 0);
            }
        }
    }
}

TEST_CASE("simulate pockets: a slow ball that clips a jaw still drops") {
    // Spec 6.3 owner check: a slow, accurate ball drops even when it touches a jaw; spec 7.6:
    // the outcome comes from the jaw collision itself. Each path overlaps a jaw's reach by
    // 1-9 mm, a sixth of the ball at most. Slower than about 0.1 m/s at the fall a clipped ball can
    // die in the jaws (hanging on the lip), so the sweep starts above that.
    for (const char* name : {"top_left", "middle_left"}) {
        const Mouth m = mouth(name);
        const double limit = m.half_gap - kR;
        for (const double past : {0.00125, 0.004, 0.00925}) {
            for (const double side : {1.0, -1.0}) {
                for (const double speed : {0.12, 0.2, 0.3, 0.5}) {
                    CAPTURE(name);
                    CAPTURE(side * (limit + past));
                    CAPTURE(speed);
                    const Entry e = roll_into(m, 0.0, side * (limit + past), speed);
                    CHECK(e.potted);
                    CHECK(e.jaw_hits >= 1);
                }
            }
        }
    }
    // A ball running along the side cushion, touching it or 2 mm off it, meets the far jaw nearly
    // square (35 degrees off its normal) while 22 mm short of the fall; paced to cross the fall at
    // 0.2 m/s or more it still drops. towards_top_left_jaw.json struck at 0.3 m/s reaches the jaw
    // at only 0.12 m/s, rolls 23.5 mm after it and stops 10 mm short: it hangs.
    const Mouth corner = mouth("top_left");
    for (const double gap : {0.0, 0.002}) {
        const double y = ofs::table::kPlayingWidth / 2.0 - kR - gap;
        const double offset = (y - corner.fall.y) / corner.across.y;
        for (const double speed : {0.2, 0.3, 0.5}) {
            CAPTURE(gap);
            CAPTURE(speed);
            const Entry e = roll_into(corner, -std::atan2(1.0, 1.0), offset, speed);
            CHECK(e.potted);
            CHECK(e.jaw_hits >= 1);
        }
    }
}

TEST_CASE("simulate pockets: a firm ball into a jaw face rattles and stays out") {
    // Spec 6.3 owner check: a ball hit firmly into the jaws rattles and stays out; spec 7.6:
    // rattles emerge from jaw collisions, not from scripted outcomes. These paths overlap a jaw's
    // reach by 24-32 mm, about half the ball.
    for (const char* name : {"top_left", "middle_left"}) {
        const Mouth m = mouth(name);
        const double limit = m.half_gap - kR;
        for (const double past : {0.024, 0.03225}) {
            for (const double side : {1.0, -1.0}) {
                for (const double speed : {2.0, 3.0, 4.0, 6.0, 8.0}) {
                    CAPTURE(name);
                    CAPTURE(side * (limit + past));
                    CAPTURE(speed);
                    const Entry e = roll_into(m, 0.0, side * (limit + past), speed);
                    CHECK_FALSE(e.potted);
                    CHECK(e.jaw_hits >= 1);
                }
            }
        }
    }
}

TEST_CASE("simulate a ball rolled square into a jaw: the jaw is its first event and it rebounds") {
    // Spec 7.5: jaws are curved cushions with the cushion impact model. Without this a jaw hit
    // turned into a push (no rebound) or a jaw dropped from detection passed every test but C14.
    const ArcCushion& jaw = table().arcs[0];
    const double mid = 0.5 * (jaw.angle_start + jaw.angle_end);
    const ofs::Vec3 out{std::cos(mid), std::sin(mid), 0.0};
    const double d = jaw.radius + kR + 0.02;
    BallState red = ball(ofs::table::Ball::red, 0, jaw.centre.x + out.x * d, jaw.centre.y + out.y * d);
    red.v = out * -1.0;
    red.w = {-red.v.y / kR, red.v.x / kR, 0.0};
    red.motion = Motion::rolling;
    SimInput in;
    in.start.balls = {red};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    CHECK(r.anomalies.empty());
    REQUIRE_FALSE(r.events.empty());
    REQUIRE(r.events[0].kind == EventKind::ball_arc);
    CHECK(r.events[0].body == 0);
    REQUIRE(r.segments[0].size() >= 2);
    REQUIRE(r.segments[0][1].t0 == r.events[0].t);
    CHECK(ofs::dot(r.segments[0][1].start.v, out) > 0.5);
}

TEST_CASE("simulate flags a collision that adds kinetic energy as energy_gain") {
    // Spec 7.11 C12 and 16.10: an energy gain is a physics anomaly. The loader refuses a
    // restitution above 1, so the parameters are set directly.
    PhysicsParams p = file_params();
    p.ball_restitution = 1.5;
    SimInput in;
    in.start.balls = {ball(ofs::table::Ball::cue, 0, -0.5, 0.0), ball(ofs::table::Ball::red, 0, 0.5, 0.0)};
    in.cue_ball = 0;
    in.strike.speed_m_s = 2.0;
    ShotRecord r;
    const auto logged = simulate_capturing(in, p, r);
    bool recorded = false;
    for (const Anomaly& a : r.anomalies) recorded = recorded || a.kind == "energy_gain";
    CHECK(recorded);
    bool logged_gain = false;
    for (const ofs::log::Event& e : logged) logged_gain = logged_gain || e.data.value("kind", "") == "energy_gain";
    CHECK(logged_gain);
}

TEST_CASE("simulate a push slower than the start speed leaves the touching ball where it is") {
    // Spec 7.7's guard against near-zero-time loops: a ball at rest joins a push only when it
    // closes at kStartSpeed (1e-6 m/s) or more. Below that it stands like a cushion; set rolling,
    // it would stop at once while its neighbour re-closed the contact.
    SimInput in;
    BallState cue = ball(ofs::table::Ball::cue, 0, 0.0, 0.0);
    cue.v = {5e-7, 0.0, 0.0};
    cue.w = {0.0, 5e-7 / kR, 0.0};
    cue.motion = Motion::rolling;
    const BallState red = ball(ofs::table::Ball::red, 0, 2.0 * kR, 0.0);
    in.start.balls = {cue, red};
    in.has_strike = false;
    const ShotRecord r = simulate(in, table(), file_params());
    CHECK(r.anomalies.empty());
    bool touched = false;
    for (const ShotEvent& e : r.events) touched = touched || e.kind == EventKind::ball_ball;
    REQUIRE(touched);
    CHECK(r.segments[1].size() == 1);
    CHECK(r.final_state.balls[1].r == red.r);
}
