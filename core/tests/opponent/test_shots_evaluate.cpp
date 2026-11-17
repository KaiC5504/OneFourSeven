// What a position is worth and when a maximum is still on (spec 10.1, 10.2 step 4, evaluate.hpp,
// docs/opponent/README.md "The 147").

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/pots.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::BallOn;
using rules::OnKind;
using rules::scenario::ball;
using rules::scenario::place_of;
using rules::scenario::spotted;

namespace {

Pot hand_pot(int value, double margin, double cut, double cue_m) {
    Pot p;
    p.target = 1;
    p.pocket = 0;
    p.value = value;
    p.margin_rad = margin;
    p.cut_rad = cut;
    p.cue_travel_m = cue_m;
    p.object_travel_m = 0.5;
    return p;
}

// README "Error", for a centre-ball stroke with a level cue on a pot that is not awkward.
double chance_by_hand(const Level& l, const Pot& p) {
    const double sigma = l.aim_sigma_rad * (1.0 + l.aim_sigma_per_cut * p.cut_rad) *
                         (1.0 + l.aim_sigma_per_m * p.cue_travel_m);
    return std::erf(p.margin_rad / (sigma * std::sqrt(2.0)));
}

// Fourteen reds, red on, the break given.
rules::FrameState fourteen_reds(int break_points) {
    rules::FrameState s = rig::racked();
    const int red = place_of(s, Ball::red, 0);
    REQUIRE(red >= 0);
    s.balls.erase(s.balls.begin() + red);
    if (red < s.cue_ball) --s.cue_ball;
    s.cue_in_hand = false;
    s.touching.clear();
    s.on = BallOn{OnKind::red, Ball::yellow};
    s.break_points = break_points;
    s.scores = {break_points, 0};
    return s;
}

// The reds gone and `from` the colour on, every colour from it up on its spot.
rules::FrameState colours_from(Ball from, int break_points) {
    std::vector<rules::BallOnTable> balls{ball(Ball::cue, -0.6, 0.3)};
    for (const Ball c : {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        if (table::ball_value(c) >= table::ball_value(from)) balls.push_back(spotted(c));
    }
    rules::FrameState s = rules::scenario::frame(balls);
    s.on = BallOn{OnKind::colour, from};
    s.break_points = break_points;
    s.scores = {break_points, 0};
    return s;
}

}  // namespace

TEST_CASE("evaluate.hpp: no pots read as worth 0 with no best") {
    const Read r = read_pots({}, rig::levels().beginner, rig::env());
    CHECK(r.worth == 0.0);
    CHECK(r.pots == 0);
    CHECK(r.best == -1);
    CHECK(r.chance == 0.0);
}

TEST_CASE("evaluate.hpp: the best pot maximises chance * (value + 16), not the chance or the value alone") {
    const Level& b = rig::levels().beginner;
    const std::vector<Pot> list{hand_pot(1, 0.003, 0.2, 0.5), hand_pot(7, 0.0005, 0.9, 2.0),
                                hand_pot(5, 0.004, 0.3, 0.6), hand_pot(1, 0.002, 0.1, 0.4)};
    int best = -1;
    double worth = 0.0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const double w = chance_by_hand(b, list[i]) * (list[i].value + kBreakOn);
        if (w > worth) {
            worth = w;
            best = int(i);
        }
    }
    REQUIRE(best == 2);
    const Read r = read_pots(list, b, rig::env());
    CHECK(r.pots == 4);
    CHECK(r.best == best);
    CHECK(r.worth == doctest::Approx(worth).epsilon(1e-9));
    CHECK(r.chance == doctest::Approx(chance_by_hand(b, list[2])).epsilon(1e-9));
}

TEST_CASE("evaluate.hpp: of two pots worth the same the earlier listed is the best") {
    const Level& b = rig::levels().beginner;
    const Pot p = hand_pot(3, 0.004, 0.3, 0.7);
    const Read r = read_pots({hand_pot(1, 0.0001, 0.2, 0.5), p, p}, b, rig::env());
    CHECK(r.best == 1);
}

TEST_CASE("README, The 147: with no error every open pot is certain, so the best is the highest value") {
    const Level& m = rig::levels().maximum;
    const std::vector<Pot> list{hand_pot(1, 0.02, 0.1, 0.4), hand_pot(7, 0.0003, 1.1, 2.5),
                                hand_pot(5, 0.01, 0.4, 0.8)};
    const Read r = read_pots(list, m, rig::env());
    CHECK(r.best == 1);
    CHECK(r.chance == 1.0);
    CHECK(r.worth == doctest::Approx(7.0 + kBreakOn).epsilon(1e-12));
}

TEST_CASE("evaluate.hpp: snookered reads as no pots; the straight red as a pot worth something to Beginner") {
    const Level& b = rig::levels().beginner;
    const Read none = read_position(rig::position("snookered"), b, rig::env());
    CHECK(none.pots == 0);
    CHECK(none.worth == 0.0);
    CHECK(none.best == -1);

    const Read some = read_position(rig::position("straight_red"), b, rig::env());
    CHECK(some.pots >= 1);
    CHECK(some.worth > 0.0);
    CHECK(some.best >= 0);
    CHECK(some.best < some.pots);
    CHECK(some.chance > 0.0);
    CHECK(some.chance <= 1.0);
    CHECK(some.worth <= some.chance * (1.0 + kBreakOn) + 1e-12);
}

TEST_CASE("evaluate.hpp: read_position is read_pots of pots at the level's max_cut_rad") {
    for (const Level* level : {&rig::levels().beginner, &rig::levels().maximum}) {
        for (const char* name : {"straight_red", "after_red", "in_hand", "colours"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            const rules::FrameState s = rig::position(name);
            const Read a = read_position(s, *level, rig::env());
            const Read b = read_pots(pots(s, rig::env(), level->max_cut_rad), *level, rig::env());
            CHECK(a.pots == b.pots);
            CHECK(a.best == b.best);
            CHECK(a.worth == b.worth);
            CHECK(a.chance == b.chance);
        }
    }
}

TEST_CASE("README, The 147: the maximum is on from the break-off rack, with the last red, and after a red") {
    CHECK(maximum_on(rig::racked()));
    CHECK(maximum_on(rig::position("last_red")));
    CHECK(maximum_on(rig::position("after_red")));
}

TEST_CASE("README, The 147: the maximum is gone with reds missing and nothing in the break") {
    CHECK_FALSE(maximum_on(rig::position("straight_red")));
    CHECK_FALSE(maximum_on(rig::position("long_red")));
}

TEST_CASE("evaluate.hpp: fourteen reds and red on: on at a break of 8, gone at 6") {
    CHECK(maximum_on(fourteen_reds(8)));
    CHECK_FALSE(maximum_on(fourteen_reds(6)));
    CHECK_FALSE(maximum_on(fourteen_reds(0)));
    CHECK_FALSE(maximum_on(fourteen_reds(16)));
}

TEST_CASE("evaluate.hpp: fourteen reds and a colour of choice: on at a break of 1, gone at 8") {
    rules::FrameState s = fourteen_reds(1);
    s.on = BallOn{OnKind::any_colour, Ball::yellow};
    CHECK(maximum_on(s));
    s.break_points = 8;
    CHECK_FALSE(maximum_on(s));
}

TEST_CASE("evaluate.hpp: the colours: on at 120 with the yellow on and 134 with the pink, gone at 133") {
    CHECK(maximum_on(colours_from(Ball::yellow, 120)));
    CHECK_FALSE(maximum_on(colours_from(Ball::yellow, 113)));
    CHECK(maximum_on(colours_from(Ball::pink, 134)));
    CHECK_FALSE(maximum_on(colours_from(Ball::pink, 133)));
    CHECK(maximum_on(colours_from(Ball::black, 140)));
}

TEST_CASE("evaluate.hpp: the maximum is off with the frame over, a decision pending, a free ball or a re-spotted black") {
    for (const char* name : {"last_red", "after_red"}) {
        CAPTURE(name);
        const rules::FrameState on = rig::position(name);
        REQUIRE(maximum_on(on));

        rules::FrameState over = on;
        over.over = true;
        over.winner = 0;
        over.end_reason = rules::EndReason::conceded;
        CHECK_FALSE(maximum_on(over));

        rules::FrameState pending = on;
        pending.pending = rules::Pending::after_foul;
        pending.decider = 0;
        pending.offender = 1;
        CHECK_FALSE(maximum_on(pending));

        rules::FrameState free_ball = on;
        free_ball.free_ball = true;
        CHECK_FALSE(maximum_on(free_ball));
    }
    rules::FrameState black = colours_from(Ball::black, 140);
    REQUIRE(maximum_on(black));
    black.respotted_black = true;
    CHECK_FALSE(maximum_on(black));
}

TEST_CASE("README, The 147: the ball the maximum needs: a red while red is on, the black after a red, else the colour on") {
    CHECK(maximum_next(rig::racked()) == Ball::red);
    CHECK(maximum_next(rig::position("last_red")) == Ball::red);
    CHECK(maximum_next(fourteen_reds(8)) == Ball::red);
    CHECK(maximum_next(rig::position("after_red")) == Ball::black);
    CHECK(maximum_next(colours_from(Ball::yellow, 120)) == Ball::yellow);
    CHECK(maximum_next(colours_from(Ball::blue, 129)) == Ball::blue);
    CHECK(maximum_next(colours_from(Ball::pink, 134)) == Ball::pink);
}
