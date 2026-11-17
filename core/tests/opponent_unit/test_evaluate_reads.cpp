#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/evaluate.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::OnKind;

namespace {

Pot pot_of(double margin_rad, double cut_rad, double cue_travel_m, int value) {
    Pot p;
    p.margin_rad = margin_rad;
    p.cut_rad = cut_rad;
    p.cue_travel_m = cue_travel_m;
    p.value = value;
    return p;
}

void drop_red(rules::FrameState& s) {
    for (std::size_t i = s.balls.size(); i-- > 0;) {
        if (s.balls[i].ball != Ball::red) continue;
        s.balls.erase(s.balls.begin() + std::ptrdiff_t(i));
        if (int(i) < s.cue_ball) --s.cue_ball;
        return;
    }
}

int reds(const rules::FrameState& s) {
    int n = 0;
    for (const rules::BallOnTable& b : s.balls) n += b.ball == Ball::red ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("read_pots with no pot reads nothing") {
    const Read r = read_pots({}, rig::levels().beginner, rig::env());
    CHECK(r.worth == 0.0);
    CHECK(r.pots == 0);
    CHECK(r.best == -1);
    CHECK(r.chance == 0.0);
}

TEST_CASE("read_pots of one pot is its chance times its value and the break") {
    const Level& b = rig::levels().beginner;
    const Pot pot = pot_of(0.012, 0.3, 1.0, 5);
    const Read r = read_pots({pot}, b, rig::env());
    const double chance = pot_chance(b, pot, Spec{}, 0.012);
    CHECK(r.pots == 1);
    CHECK(r.best == 0);
    CHECK(r.chance == chance);
    CHECK(r.worth == doctest::Approx(chance * (5 + kBreakOn)).epsilon(1e-12));
}

TEST_CASE("the best pot is the one worth most, not the likeliest") {
    const Level& b = rig::levels().beginner;
    const Pot red = pot_of(0.02, 0.0, 0.5, 1);
    const Pot black = pot_of(0.015, 0.3, 1.0, 7);
    const double red_chance = pot_chance(b, red, Spec{}, red.margin_rad);
    const double black_chance = pot_chance(b, black, Spec{}, black.margin_rad);
    REQUIRE(black_chance < red_chance);
    REQUIRE(black_chance * (7 + kBreakOn) > red_chance * (1 + kBreakOn));

    const Read r = read_pots({red, black}, b, rig::env());
    CHECK(r.pots == 2);
    CHECK(r.best == 1);
    CHECK(r.chance == black_chance);
    CHECK(r.worth == doctest::Approx(black_chance * (7 + kBreakOn)).epsilon(1e-12));
}

TEST_CASE("of pots worth the same the earlier is the best") {
    const Level& b = rig::levels().beginner;
    const Pot hard = pot_of(0.004, 0.8, 2.5, 1);
    const Pot easy = pot_of(0.02, 0.1, 0.6, 4);
    const Read r = read_pots({hard, easy, easy}, b, rig::env());
    CHECK(r.pots == 3);
    CHECK(r.best == 1);

    const Read sure = read_pots({easy, easy}, rig::levels().maximum, rig::env());
    CHECK(sure.best == 0);
    CHECK(sure.chance == 1.0);
    CHECK(sure.worth == 4 + kBreakOn);
}

TEST_CASE("read_position is read_pots of the position's pots") {
    const Env env = rig::env();
    for (const char* name : {"straight_red", "cut_red", "long_red", "colours", "in_hand", "after_red"}) {
        CAPTURE(name);
        const rules::FrameState s = rig::position(name);
        for (const Level* level : {&rig::levels().beginner, &rig::levels().maximum}) {
            const Read whole = read_position(s, *level, env);
            const Read parts = read_pots(pots(s, env, level->max_cut_rad), *level, env);
            CHECK(whole.worth == parts.worth);
            CHECK(whole.pots == parts.pots);
            CHECK(whole.best == parts.best);
            CHECK(whole.chance == parts.chance);
        }
    }
}

TEST_CASE("the maximum is on in the named positions that keep it") {
    CHECK(maximum_on(rig::position("last_red")));
    CHECK(maximum_on(rig::position("after_red")));
    // Fourteen reds have gone and the break is nothing.
    CHECK_FALSE(maximum_on(rig::position("straight_red")));
    CHECK(maximum_on(rig::racked()));
}

TEST_CASE("the maximum follows the break through the reds") {
    rules::FrameState s = rig::racked();
    REQUIRE(reds(s) == 15);
    drop_red(s);
    REQUIRE(reds(s) == 14);

    s.on = {OnKind::any_colour, Ball::yellow};
    s.break_points = 1;
    CHECK(maximum_on(s));
    s.break_points = 0;
    CHECK_FALSE(maximum_on(s));

    s.on = {OnKind::red, Ball::yellow};
    s.break_points = 8;
    CHECK(maximum_on(s));
    // A red and a colour other than the black.
    s.break_points = 6;
    CHECK_FALSE(maximum_on(s));
}

TEST_CASE("the maximum through the colours") {
    rules::FrameState s = rig::position("colours");
    REQUIRE(reds(s) == 0);
    REQUIRE(s.on.kind == OnKind::colour);
    REQUIRE(s.on.colour == Ball::yellow);
    s.break_points = 120;
    CHECK(maximum_on(s));
    s.break_points = 119;
    CHECK_FALSE(maximum_on(s));

    s.on = {OnKind::colour, Ball::green};
    s.break_points = 122;
    CHECK(maximum_on(s));
    s.break_points = 121;
    CHECK_FALSE(maximum_on(s));

    const int on_each[] = {120, 122, 125, 129, 134, 140};
    for (int c = 0; c < 6; ++c) {
        s.on = {OnKind::colour, Ball(int(Ball::yellow) + c)};
        s.break_points = on_each[c];
        CHECK(maximum_on(s));
    }

    rules::FrameState black = rig::position("black_only");
    black.break_points = 140;
    CHECK(maximum_on(black));
}

TEST_CASE("the maximum is off with anything pending, the frame over, a free ball or a re-spotted black") {
    const rules::FrameState base = rig::position("last_red");
    REQUIRE(maximum_on(base));

    rules::FrameState pending = base;
    pending.pending = rules::Pending::after_foul;
    CHECK_FALSE(maximum_on(pending));

    rules::FrameState over = base;
    over.over = true;
    CHECK_FALSE(maximum_on(over));

    rules::FrameState free_ball = base;
    free_ball.free_ball = true;
    CHECK_FALSE(maximum_on(free_ball));

    rules::FrameState respotted = rig::position("black_only");
    respotted.break_points = 140;
    REQUIRE(maximum_on(respotted));
    respotted.respotted_black = true;
    CHECK_FALSE(maximum_on(respotted));
}

TEST_CASE("maximum_next is the red, then the black, then the colour on") {
    rules::FrameState s = rig::racked();
    CHECK(maximum_next(s) == Ball::red);
    CHECK(maximum_next(rig::position("after_red")) == Ball::black);
    CHECK(maximum_next(rig::position("colours")) == Ball::yellow);
    s.on = {OnKind::colour, Ball::pink};
    CHECK(maximum_next(s) == Ball::pink);
    CHECK(maximum_next(rig::position("black_only")) == Ball::black);
}
