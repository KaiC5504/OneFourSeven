#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;
namespace table = ofs::table;

namespace {

ofs::phys::TableState rack() {
    ofs::phys::TableState t;
    for (const table::BallPlacement& b : table::opening_layout(0.0005)) {
        ofs::phys::BallState s;
        s.ball = b.ball;
        s.index = b.index;
        s.r = {b.position.x, b.position.y, 0.0};
        t.balls.push_back(s);
    }
    return t;
}

}  // namespace

TEST_CASE("a new frame has the breaker in hand with red on (S3.3(c), (g))") {
    const FrameState s = new_frame(1, rack(), 99);
    CHECK(s.striker == 1);
    CHECK(s.seed == 99);
    CHECK(s.cue_in_hand);
    CHECK(s.on.kind == OnKind::red);
    CHECK_FALSE(s.free_ball);
    CHECK(s.pending == Pending::none);
    CHECK_FALSE(s.over);
    CHECK(s.scores[0] + s.scores[1] == 0);
    REQUIRE(s.balls.size() == 22);
    CHECK(s.cue_ball == 0);
    CHECK(s.balls[0].ball == Ball::cue);
    CHECK(s.balls[0].pos.x == table::default_cue_ball_position().x);
    CHECK(s.balls[0].pos.y == table::default_cue_ball_position().y);
    CHECK(s.balls[16].ball == Ball::yellow);
    CHECK(s.balls[21].ball == Ball::black);
    CHECK(places_on(s).size() == 15);
}

TEST_CASE("the balls on are every red, every colour of choice, or the one colour") {
    FrameState s = open_table(2);
    CHECK(places_on(s) == std::vector<int>{1, 2});
    s.on = {OnKind::any_colour, Ball::yellow};
    CHECK(places_on(s) == std::vector<int>{3, 4, 5, 6, 7, 8});
    s.on = {OnKind::colour, Ball::blue};
    CHECK(places_on(s) == std::vector<int>{at(s, Ball::blue)});
}

TEST_CASE("the cue ball is never among the balls on, wherever it is listed") {
    FrameState s = scenario::frame({scenario::ball(Ball::red, 0.3, 0.5, 0), scenario::ball(Ball::cue, -0.6, 0.3),
                                    scenario::ball(Ball::red, 0.5, 0.5, 1)},
                                   1);
    CHECK(places_on(s) == std::vector<int>{0, 2});
}

TEST_CASE("nothing is declared with red on, any colour after a red") {
    FrameState s = open_table(3);
    CHECK(nominations(s).empty());
    s.on = {OnKind::any_colour, Ball::yellow};
    CHECK(nominations(s) ==
          std::vector<Ball>{Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black});
}

TEST_CASE("a free ball may be any object ball that is not on (S3.12(a)(i))") {
    FrameState s = open_table(3);
    s.free_ball = true;
    CHECK(nominations(s) ==
          std::vector<Ball>{Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black});

    FrameState late = colours_left({Ball::brown, Ball::blue, Ball::pink, Ball::black});
    CHECK(nominations(late).empty());
    late.free_ball = true;
    CHECK(nominations(late) == std::vector<Ball>{Ball::blue, Ball::pink, Ball::black});
}

TEST_CASE("nothing is declared once the frame is over") {
    FrameState s = open_table(3);
    s.on = {OnKind::any_colour, Ball::yellow};
    s.over = true;
    CHECK(nominations(s).empty());
}

TEST_CASE("points remaining: 147 at the start, 8 a red and 27 for the colours") {
    CHECK(points_remaining(open_table(15)) == 147);
    CHECK(points_remaining(open_table(3)) == 51);
}

TEST_CASE("points remaining: the colours left once the reds are gone") {
    CHECK(points_remaining(colours_left({Ball::pink, Ball::black})) == 13);
    CHECK(points_remaining(colours_left({Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink,
                                         Ball::black})) == 27);
    CHECK(points_remaining(colours_left({Ball::black})) == 7);
}

TEST_CASE("points remaining: a colour of choice is worth a black more") {
    FrameState s = open_table(14);
    s.on = {OnKind::any_colour, Ball::yellow};
    CHECK(points_remaining(s) == 146);
    FrameState last = open_table(0);
    last.on = {OnKind::any_colour, Ball::yellow};
    CHECK(points_remaining(last) == 34);
}

TEST_CASE("points remaining: seven with a re-spotted black") {
    FrameState s = colours_left({Ball::black});
    s.respotted_black = true;
    s.scores = {40, 40};
    CHECK(points_remaining(s) == 7);
}

TEST_CASE("penalty points required: the deficit beyond the balls left") {
    FrameState s = open_table(3);
    s.scores = {10, 70};
    CHECK(penalty_points_required(s, 0) == 9);
    CHECK(penalty_points_required(s, 1) == 0);
    s.scores = {19, 70};
    CHECK(penalty_points_required(s, 0) == 0);
    s.scores = {18, 70};
    CHECK(penalty_points_required(s, 0) == 1);
    CHECK(penalty_points_required(s, 2) == 0);
}

TEST_CASE("penalty points required: the colour of choice counts only for the striker (S3.14(a)(i), S4.2(b))") {
    // The last red gone, a colour of choice on: 34 for the striker, the 27 of the colours for the other.
    FrameState s = open_table(0);
    s.on = {OnKind::any_colour, Ball::yellow};
    s.striker = 0;
    s.scores = {60, 30};
    REQUIRE(points_remaining(s) == 34);
    CHECK(penalty_points_required(s, 1) == 3);
    CHECK(penalty_points_required(s, 0) == 0);
    s.scores = {30, 63};
    CHECK(penalty_points_required(s, 0) == 0);
    s.scores = {30, 65};
    CHECK(penalty_points_required(s, 0) == 1);

    s.striker = 1;
    s.scores = {60, 30};
    CHECK(penalty_points_required(s, 1) == 0);
    s.scores = {30, 60};
    CHECK(penalty_points_required(s, 0) == 3);

    // Red on, nothing extra for anyone.
    FrameState red = open_table(1);
    red.scores = {70, 34};
    CHECK(penalty_points_required(red, 1) == 1);
}

TEST_CASE("the cue ball in hand goes where the striker puts it in the D (S3.5)") {
    FakeOracle oracle;
    FrameState s = open_table(15);
    s.cue_in_hand = true;
    std::string error = "old";
    const Vec2 pos{table::baulk_line_x() - 0.05, 0.1};
    REQUIRE(place_cue_ball(s, pos, oracle, error));
    CHECK(error.empty());
    CHECK(s.balls[0].pos.x == pos.x);
    CHECK(s.balls[0].pos.y == pos.y);
    CHECK(s.cue_in_hand);
}

TEST_CASE("a place outside the D or against a ball is refused and nothing moves") {
    FakeOracle oracle;
    FrameState s = open_table(15);
    s.cue_in_hand = true;
    const FrameState before = s;
    std::string error;
    CHECK_FALSE(place_cue_ball(s, {0.2, 0.0}, oracle, error));
    CHECK_FALSE(error.empty());
    CHECK(same(s, before));
    const Vec2 brown = table::spot(Ball::brown);
    CHECK_FALSE(place_cue_ball(s, {brown.x - table::kBallDiameter, brown.y}, oracle, error));
    CHECK(same(s, before));
    CHECK_FALSE(place_cue_ball(s, {std::nan(""), 0.0}, oracle, error));
    CHECK(same(s, before));
}

TEST_CASE("the cue ball is placed only in hand, with nothing pending, in a live frame") {
    FakeOracle oracle;
    const Vec2 pos{table::baulk_line_x() - 0.05, 0.1};
    std::string error;

    FrameState in_play = open_table(15);
    CHECK_FALSE(place_cue_ball(in_play, pos, oracle, error));
    CHECK(error == "the cue ball is not in hand");

    FrameState pending = open_table(15);
    pending.cue_in_hand = true;
    pending.pending = Pending::after_foul;
    CHECK_FALSE(place_cue_ball(pending, pos, oracle, error));

    FrameState over = open_table(15);
    over.cue_in_hand = true;
    over.over = true;
    const FrameState before = over;
    CHECK_FALSE(place_cue_ball(over, pos, oracle, error));
    CHECK(same(over, before));
}
