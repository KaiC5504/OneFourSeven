#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "ofs/physics/motion.hpp"
#include "ofs/physics/serialize.hpp"
#include "ofs/physics/shot_record.hpp"

using namespace ofs::phys;

namespace {

BallState make_ball(ofs::table::Ball kind, int index, ofs::Vec3 r, ofs::Vec3 v, Motion m) {
    BallState b;
    b.ball = kind;
    b.index = index;
    b.r = r;
    b.v = v;
    b.motion = m;
    return b;
}

// Ball 0 rolls, is deflected at t = 1 and stops at t = 2.5; ball 1 never moves.
ShotRecord sample_record() {
    ShotRecord r;
    const BallState s0 = make_ball(ofs::table::Ball::cue, 0, {-0.5, 0.0, 0.0}, {1.0, 0.0, 0.0}, Motion::rolling);
    const BallState s1 = make_ball(ofs::table::Ball::cue, 0, {0.2, 0.1, 0.0}, {0.3, -0.1, 0.0}, Motion::rolling);
    const BallState s2 = make_ball(ofs::table::Ball::cue, 0, {0.4, 0.05, 0.0}, {}, Motion::stationary);
    const BallState red = make_ball(ofs::table::Ball::red, 3, {0.9, -0.2, 0.0}, {}, Motion::stationary);
    r.segments = {{{0.0, s0, {}}, {1.0, s1, {}}, {2.5, s2, {}}}, {{0.0, red, {}}}};
    r.events = {{0.0, EventKind::strike, 0, -1, -1, Motion::stationary, Motion::rolling},
                {1.0, EventKind::ball_line, 0, -1, 2, Motion::rolling, Motion::rolling},
                {2.5, EventKind::transition, 0, -1, -1, Motion::rolling, Motion::stationary}};
    r.final_state.balls = {s2, red};
    r.t_end = 2.5;
    r.has_strike = true;
    r.strike.v = {1.0, 0.0, 0.0};
    r.anomalies = {{"overlap", 0.0, {0, 1}, "test"}};
    r.sim_us = 12.5;
    return r;
}

void check_same(const BallState& got, const BallState& want) {
    CHECK(got.ball == want.ball);
    CHECK(got.index == want.index);
    CHECK(got.r == want.r);
    CHECK(got.v == want.v);
    CHECK(got.w == want.w);
    CHECK(got.motion == want.motion);
    CHECK(got.pocket == want.pocket);
}

// What the contract says ball_at must give: the segment covering t, evolved from its start.
BallState expected_at(const ShotRecord& r, int ball, double t) {
    const std::vector<Segment>& segs = r.segments[static_cast<std::size_t>(ball)];
    std::size_t k = 0;
    while (k + 1 < segs.size() && segs[k + 1].t0 <= t) ++k;
    double dt = std::max(0.0, t - segs[k].t0);
    if (k + 1 < segs.size()) dt = std::min(dt, segs[k + 1].t0 - segs[k].t0);
    dt = std::min(dt, transition_time(segs[k].start, r.params, segs[k].nap));
    return evolve(segs[k].start, dt, r.params, segs[k].nap);
}

}  // namespace

TEST_CASE("record event kinds have their log names") {
    CHECK(event_kind_name(EventKind::strike) == "strike");
    CHECK(event_kind_name(EventKind::ball_ball) == "ball_ball");
    CHECK(event_kind_name(EventKind::ball_line) == "ball_line");
    CHECK(event_kind_name(EventKind::ball_arc) == "ball_arc");
    CHECK(event_kind_name(EventKind::pocket) == "pocket");
    CHECK(event_kind_name(EventKind::transition) == "transition");
    CHECK(event_kind_name(EventKind::nap) == "nap");
    CHECK(event_kind_name(EventKind::guard) == "guard");
}

TEST_CASE("record ball_at evaluates the segment covering t from its start") {
    const ShotRecord r = sample_record();
    for (const double t : {0.0, 0.5, 0.999, 1.0, 1.7, 2.4999, 2.5, 10.0}) {
        CAPTURE(t);
        check_same(r.ball_at(0, t), expected_at(r, 0, t));
        check_same(r.ball_at(1, t), expected_at(r, 1, t));
    }
    // With every segment's own start: the binary search lands on the right one.
    check_same(r.ball_at(0, 1.0), evolve(r.segments[0][1].start, 0.0, r.params, {}));
    check_same(r.ball_at(0, 2.5), evolve(r.segments[0][2].start, 0.0, r.params, {}));
}

TEST_CASE("record ball_at before the first segment gives the first segment's start") {
    const ShotRecord r = sample_record();
    check_same(r.ball_at(0, -1.0), evolve(r.segments[0][0].start, 0.0, r.params, {}));
}

TEST_CASE("record ball_at takes the later of two segments that start together") {
    ShotRecord r = sample_record();
    BallState again = r.segments[0][1].start;
    again.r.x += 0.01;
    r.segments[0].insert(r.segments[0].begin() + 2, Segment{1.0, again, {}});
    check_same(r.ball_at(0, 1.0), evolve(again, 0.0, r.params, {}));
    check_same(r.ball_at(0, 1.5), expected_at(r, 0, 1.5));
}

TEST_CASE("record state_at gives every ball") {
    const ShotRecord r = sample_record();
    const TableState s = r.state_at(1.7);
    REQUIRE(s.balls.size() == 2);
    check_same(s.balls[0], r.ball_at(0, 1.7));
    check_same(s.balls[1], r.ball_at(1, 1.7));
}

TEST_CASE("record_json holds everything but sim_us") {
    const nlohmann::json j = record_json(sample_record());
    std::set<std::string> keys;
    for (const auto& [key, value] : j.items()) keys.insert(key);
    CHECK(keys == std::set<std::string>{"anomalies", "events", "final", "has_strike", "params", "segments",
                                        "strike", "t_end"});
    CHECK(j["params"] == physics_params_values(PhysicsParams{}));
    CHECK(j["segments"].size() == 2);
    CHECK(j["segments"][0].size() == 3);
    CHECK(j["segments"][0][1]["t0"] == 1.0);
    CHECK(j["segments"][0][1]["start"] == to_json(sample_record().segments[0][1].start));
    CHECK(j["events"][1] == nlohmann::json{{"t", 1.0}, {"kind", "ball_line"}, {"a", 0}, {"b", -1}, {"body", 2},
                                           {"from", "rolling"}, {"to", "rolling"}});
    CHECK(j["final"] == to_json(sample_record().final_state));
    CHECK(j["t_end"] == 2.5);
    CHECK(j["anomalies"][0] ==
          nlohmann::json{{"kind", "overlap"}, {"t", 0.0}, {"balls", {0, 1}}, {"detail", "test"}});
    for (const char* key : {"v", "w", "elevation_used_rad", "elevation_raised", "squirt_rad", "miscue"}) {
        CAPTURE(key);
        CHECK(j["strike"].contains(key));
    }
}

TEST_CASE("record_json is byte-stable and ignores sim_us") {
    ShotRecord a = sample_record();
    ShotRecord b = sample_record();
    b.sim_us = 999.0;
    CHECK(record_json(a).dump() == record_json(b).dump());
    CHECK(record_json(a).dump() == record_json(a).dump());
}

TEST_CASE("record_json keeps doubles at full precision") {
    ShotRecord r = sample_record();
    const std::string before = record_json(r).dump();
    const double x = std::nextafter(r.segments[0][1].start.r.x, 1.0);
    r.segments[0][1].start.r.x = x;
    const std::string after = record_json(r).dump();
    CHECK(before != after);
    const nlohmann::json back = nlohmann::json::parse(after);
    CHECK(back["segments"][0][1]["start"]["r"][0].get<double>() == x);
    r.events[1].t = std::nextafter(1.0, 2.0);
    CHECK(record_json(r).dump() != after);
}
