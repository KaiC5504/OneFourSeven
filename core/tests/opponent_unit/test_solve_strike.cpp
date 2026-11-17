#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>

#include "ofs/opponent/solve.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;

namespace {

rules::Position two_balls(rules::Vec2 cue, rules::Vec2 red) {
    rules::Position p;
    p.balls = {{table::Ball::cue, 0, cue}, {table::Ball::red, 0, red}};
    p.cue_ball = 0;
    return p;
}

phys::CueStrike stroke(double aim, double speed) {
    phys::CueStrike c;
    c.aim_rad = aim;
    c.speed_m_s = speed;
    return c;
}

}  // namespace

TEST_CASE("try_strike: a stroke into empty cloth hits nothing") {
    const rules::Position p = two_balls({-0.5, 0.0}, {0.5, 0.5});
    const Trial t = try_strike(p, p.balls[0].pos, stroke(0.0, 0.8), rig::env());
    CHECK(t.clean);
    CHECK(t.first_hit == -1);
    CHECK(t.potted.empty());
    CHECK_FALSE(t.cue_potted);
    CHECK_FALSE(t.cannon);
    CHECK(t.cue_cushions == 0);
    CHECK(t.t_end > 0.0);
    CHECK(t.strike.speed_m_s == 0.8);
}

TEST_CASE("try_strike: the cue ball sent at a corner goes in-off") {
    const rules::Position p = two_balls({0.0, 0.0}, {-1.0, -0.5});
    const rules::Vec2 cue{1.5, 0.6};
    const rules::Vec2 pocket = rig::env().table.pockets[0].drop_centre;
    REQUIRE(rig::env().table.pockets[0].name == "top_left");
    const Trial t = try_strike(p, cue, stroke(std::atan2(pocket.y - cue.y, pocket.x - cue.x), 1.0), rig::env());
    CHECK(t.clean);
    CHECK(t.cue_potted);
    CHECK(t.potted.empty());
    CHECK(t.first_hit == -1);
}

TEST_CASE("try_strike: rest has every ball, the record is filled when asked, cushions after the first ball count") {
    // A half-ball hit: the red goes left, the cue ball off to the right-hand side cushion.
    const rules::Position p = two_balls({-0.5, -0.3}, {0.0, -0.3 + table::kBallRadius});
    phys::ShotRecord record;
    const Trial t = try_strike(p, p.balls[0].pos, stroke(0.0, 2.0), rig::env(), &record);
    CHECK(t.clean);
    CHECK(t.first_hit == 1);
    CHECK(t.cue_cushions >= 1);
    CHECK_FALSE(t.cannon);
    REQUIRE(t.rest.size() == p.balls.size());
    REQUIRE(record.final_state.balls.size() == p.balls.size());
    CHECK_FALSE(record.events.empty());
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        CHECK(t.rest[i].x == record.final_state.balls[i].r.x);
        CHECK(t.rest[i].y == record.final_state.balls[i].r.y);
    }
    CHECK(t.t_end == record.t_end);

    // A ball the stroke never reaches stays where it was.
    rules::Position three = p;
    three.balls.push_back({table::Ball::black, 0, {1.4605, 0.0}});
    const Trial far = try_strike(three, three.balls[0].pos, stroke(3.14159265358979323846, 0.4), rig::env());
    REQUIRE(far.rest.size() == 3);
    CHECK(far.rest[2].x == 1.4605);
    CHECK(far.rest[2].y == 0.0);
}

TEST_CASE("try_strike: the cue ball's listed place is replaced by the one given") {
    const rules::Position p = two_balls({-0.5, 0.0}, {0.5, 0.0});
    // From where it is listed the stroke hits the red; from the given place it passes under it.
    const Trial t = try_strike(p, {-0.5, -0.4}, stroke(0.0, 0.8), rig::env());
    CHECK(t.first_hit == -1);
    const Trial listed = try_strike(p, p.balls[0].pos, stroke(0.0, 0.8), rig::env());
    CHECK(listed.first_hit == 1);
}

TEST_CASE("try_strike: a position without its cue ball is refused, not simulated") {
    rules::Position p = two_balls({-0.5, 0.0}, {0.5, 0.0});
    p.cue_ball = 5;
    const Trial t = try_strike(p, {-0.5, 0.0}, stroke(0.0, 0.8), rig::env());
    CHECK_FALSE(t.clean);
    CHECK(t.rest.empty());
}
