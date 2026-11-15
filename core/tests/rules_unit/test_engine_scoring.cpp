#include <doctest/doctest.h>

#include <vector>

#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;
namespace table = ofs::table;

namespace {

// Hits and pots that ball with nothing declared.
Judgement pot_ball(const FrameState& s, Ball b, int index = 0) {
    FakeOracle oracle;
    ShotFacts f = hit(s, b, index);
    pot(f, at(s, b, index));
    return judge(s, {}, f, oracle, RefParams{});
}

}  // namespace

TEST_CASE("a red potted scores one and leaves a colour of the striker's choice on (S3.3(g), (h)(i))") {
    const FrameState s = open_table(15);
    const Judgement j = pot_ball(s, Ball::red, 4);
    CHECK(j.fouls.empty());
    CHECK(j.calls.empty());
    CHECK(j.points == 1);
    CHECK(j.post.scores[0] == 1);
    CHECK(j.post.scores[1] == 0);
    CHECK(j.post.striker == 0);
    CHECK(j.post.on.kind == OnKind::any_colour);
    CHECK(j.post.break_points == 1);
    CHECK(j.post.pending == Pending::none);
    CHECK(j.post.balls.size() == s.balls.size() - 1);
    CHECK(at(j.post, Ball::red, 4) == -1);
    CHECK(j.post.strokes == 1);
}

TEST_CASE("every red potted in one stroke scores (S3.3(g))") {
    FakeOracle oracle;
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::red, 0);
    pot(f, at(s, Ball::red, 0));
    pot(f, at(s, Ball::red, 7));
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.points == 2);
    CHECK(j.post.scores[0] == 2);
    CHECK(j.post.balls.size() == s.balls.size() - 2);
}

TEST_CASE("the colour of choice scores its value and goes back on its spot (S3.3(h)(i), S3.7)") {
    FakeOracle oracle;
    FrameState s = open_table(14);
    s.on = {OnKind::any_colour, Ball::yellow};
    s.break_points = 1;
    s.scores = {1, 0};
    ShotFacts f = hit(s, Ball::blue);
    pot(f, at(s, Ball::blue));
    const Judgement j = judge(s, scenario::nominate(Ball::blue), f, oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.points == 5);
    CHECK(j.nominated.has_nomination);
    CHECK(j.nominated.nominated == Ball::blue);
    CHECK_FALSE(j.free_ball_used);
    CHECK(j.post.scores[0] == 6);
    CHECK(j.post.break_points == 6);
    CHECK(j.post.striker == 0);
    CHECK(j.post.on.kind == OnKind::red);
    CHECK(j.post.balls.size() == s.balls.size());
    const int blue = at(j.post, Ball::blue);
    REQUIRE(blue == at(s, Ball::blue));
    CHECK(j.post.balls[std::size_t(blue)].pos.x == table::spot(Ball::blue).x);
    REQUIRE(j.spotted.size() == 1);
    CHECK(j.spotted[0].ball == Ball::blue);
}

TEST_CASE("with nothing declared the first colour hit is the colour of choice (S2.12)") {
    FrameState s = open_table(14);
    s.on = {OnKind::any_colour, Ball::yellow};
    const Judgement j = pot_ball(s, Ball::pink);
    CHECK(j.fouls.empty());
    CHECK(j.points == 6);
    CHECK(j.nominated.has_nomination);
    CHECK(j.nominated.nominated == Ball::pink);
}

TEST_CASE("red, colour, red: the scores, the break and the ball on") {
    FrameState s = open_table(15);
    s = pot_ball(s, Ball::red, 0).post;
    CHECK(s.on.kind == OnKind::any_colour);
    s = pot_ball(s, Ball::black).post;
    CHECK(s.on.kind == OnKind::red);
    CHECK(s.scores[0] == 8);
    s = pot_ball(s, Ball::red, 1).post;
    CHECK(s.on.kind == OnKind::any_colour);
    CHECK(s.scores[0] == 9);
    CHECK(s.break_points == 9);
    CHECK(s.striker == 0);
    CHECK(s.stats[0].shots == 3);
    CHECK(s.stats[0].pots == 3);
    CHECK(s.stats[0].highest_break == 9);
    CHECK(s.strokes == 3);
    CHECK(balls_count(s, Ball::red) == 13);
    CHECK(at(s, Ball::black) >= 0);
}

TEST_CASE("a stroke that scores nothing passes the turn and ends the break (S2.5(a), S2.8)") {
    FakeOracle oracle;
    FrameState s = open_table(15);
    s.scores = {12, 3};
    s.break_points = 12;
    s.stats[0].highest_break = 12;
    const Judgement j = judge(s, {}, hit(s, Ball::red, 3), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.points == 0);
    CHECK(j.post.striker == 1);
    CHECK(j.post.break_points == 0);
    CHECK(j.post.on.kind == OnKind::red);
    CHECK(j.post.scores[0] == 12);
    CHECK(j.post.stats[0].shots == 1);
    CHECK(j.post.stats[0].pots == 0);
    CHECK(j.post.stats[0].highest_break == 12);
    CHECK(j.post.stats[1].shots == 0);
}

TEST_CASE("a colour of choice missed leaves red on for the other player") {
    FakeOracle oracle;
    FrameState s = open_table(14);
    s.on = {OnKind::any_colour, Ball::yellow};
    const Judgement j = judge(s, scenario::nominate(Ball::green), hit(s, Ball::green), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.post.striker == 1);
    CHECK(j.post.on.kind == OnKind::red);
}

TEST_CASE("the last red, its colour, then yellow to black in order (S3.3(h)(ii), (iii))") {
    FrameState s = open_table(1);
    s = pot_ball(s, Ball::red, 0).post;
    CHECK(s.on.kind == OnKind::any_colour);
    CHECK(points_remaining(s) == 34);

    Judgement j = pot_ball(s, Ball::black);
    CHECK(j.points == 7);
    CHECK(at(j.post, Ball::black) >= 0);
    CHECK(j.post.on.kind == OnKind::colour);
    CHECK(j.post.on.colour == Ball::yellow);
    s = j.post;

    int score = 8;
    const Ball order[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink};
    const Ball next[] = {Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};
    for (int i = 0; i < 5; ++i) {
        j = pot_ball(s, order[i]);
        score += table::ball_value(order[i]);
        CHECK(j.fouls.empty());
        CHECK(j.points == table::ball_value(order[i]));
        CHECK(j.spotted.empty());
        CHECK(at(j.post, order[i]) == -1);
        CHECK(j.post.on.kind == OnKind::colour);
        CHECK(j.post.on.colour == next[i]);
        CHECK(j.post.scores[0] == score);
        CHECK(j.post.striker == 0);
        CHECK_FALSE(j.post.over);
        s = j.post;
    }
    CHECK(s.balls.size() == 2);
    CHECK(s.break_points == 28);
}

TEST_CASE("the colour of choice after the last red, missed, leaves yellow on") {
    FakeOracle oracle;
    FrameState s = open_table(0);
    s.on = {OnKind::any_colour, Ball::yellow};
    const Judgement j = judge(s, scenario::nominate(Ball::black), hit(s, Ball::black), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.post.striker == 1);
    CHECK(j.post.on.kind == OnKind::colour);
    CHECK(j.post.on.colour == Ball::yellow);
}

TEST_CASE("the lowest colour left is on in the final sequence") {
    FakeOracle oracle;
    const FrameState s = colours_left({Ball::brown, Ball::blue, Ball::pink, Ball::black});
    CHECK(s.on.colour == Ball::brown);
    const Judgement j = judge(s, {}, hit(s, Ball::brown), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.post.striker == 1);
    CHECK(j.post.on.kind == OnKind::colour);
    CHECK(j.post.on.colour == Ball::brown);
}

TEST_CASE("the balls left keep their order, at rest where the facts say, and the cue ball its place") {
    FakeOracle oracle;
    const FrameState s =
        scenario::frame({scenario::ball(Ball::red, 0.3, 0.5, 0), scenario::ball(Ball::red, 0.5, 0.5, 1),
                         scenario::ball(Ball::cue, -0.6, 0.3), scenario::ball(Ball::red, 0.7, 0.5, 2),
                         scenario::spotted(Ball::black)},
                        2);
    ShotFacts f = hit(s, 0);
    pot(f, 0);
    f.rest[2] = {0.1, -0.2};
    f.rest[3] = {0.9, 0.4};
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    REQUIRE(j.post.balls.size() == 4);
    CHECK(j.post.cue_ball == 1);
    CHECK(j.post.balls[0].index == 1);
    CHECK(j.post.balls[1].ball == Ball::cue);
    CHECK(j.post.balls[1].pos.x == 0.1);
    CHECK(j.post.balls[1].pos.y == -0.2);
    CHECK(j.post.balls[2].index == 2);
    CHECK(j.post.balls[2].pos.x == 0.9);
    CHECK(j.post.balls[3].ball == Ball::black);
    CHECK_FALSE(j.post.cue_in_hand);
}

TEST_CASE("a stroke played from in hand leaves the cue ball in play (S2.9(b))") {
    FakeOracle oracle;
    FrameState s = open_table(15);
    s.cue_in_hand = true;
    const Judgement j = judge(s, {}, hit(s, Ball::red, 0), oracle, RefParams{});
    CHECK_FALSE(j.post.cue_in_hand);
}

TEST_CASE("pot attempts are counted from the facts, pots from the score") {
    FakeOracle oracle;
    FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::red, 0);
    f.pot_attempt = true;
    Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.post.stats[0].pot_attempts == 1);
    CHECK(j.post.stats[0].pots == 0);
    pot(f, at(s, Ball::red, 0));
    j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.post.stats[0].pot_attempts == 1);
    CHECK(j.post.stats[0].pots == 1);
}

TEST_CASE("the highest break is the largest the player reached, not the last") {
    FrameState s = open_table(15);
    s.stats[0].highest_break = 40;
    const Judgement j = pot_ball(s, Ball::red, 0);
    CHECK(j.post.stats[0].highest_break == 40);
}

TEST_CASE("the same frame, declaration and facts give the same judgement") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    s.seed = 31;
    ShotFacts f = hit(s, Ball::blue);
    pot(f, at(s, Ball::black));
    in_off(f);
    const Judgement a = judge(s, {}, f, oracle, RefParams{});
    const Judgement b = judge(s, {}, f, oracle, RefParams{});
    CHECK(same(a.post, b.post));
    CHECK(a.penalty == b.penalty);
    CHECK(a.fouls.size() == b.fouls.size());
    CHECK(kinds(a.calls) == kinds(b.calls));
}

TEST_CASE("facts that do not fit the frame are refused: the frame comes back unchanged with no calls") {
    FakeOracle oracle;
    const FrameState s = open_table(15);
    const auto refused = [&](const ShotFacts& f) {
        const Judgement j = judge(s, {}, f, oracle, RefParams{});
        return same(j.post, s) && j.calls.empty() && j.fouls.empty() && j.penalty == 0 && j.points == 0;
    };
    ShotFacts f = hit(s, Ball::red, 0);
    f.rest.pop_back();
    CHECK(refused(f));
    f = hit(s, Ball::red, 0);
    f.closest_gap_m.push_back(1.0);
    CHECK(refused(f));
    f = hit(s, Ball::red, 0);
    f.first_hit = {int(s.balls.size())};
    CHECK(refused(f));
    f = hit(s, Ball::red, 0);
    f.first_hit = {-1};
    CHECK(refused(f));
    f = hit(s, Ball::red, 0);
    f.first_hit = {s.cue_ball};
    CHECK(refused(f));
    f = hit(s, Ball::red, 0);
    f.pots.push_back({99, 0, 1.0});
    CHECK(refused(f));
    f = hit(s, Ball::red, 0);
    f.touching_moved = {-3};
    CHECK(refused(f));
    f = hit(s, Ball::red, 0);
    f.rest[4].x = std::nan("");
    CHECK(refused(f));
}

TEST_CASE("a frame that is over, pending or has no cue ball is not judged") {
    FakeOracle oracle;
    const auto refused = [&](const FrameState& s) {
        const Judgement j = judge(s, {}, scenario::quiet_facts(open_table(15)), oracle, RefParams{});
        return same(j.post, s) && j.calls.empty() && j.fouls.empty();
    };
    FrameState s = open_table(15);
    s.over = true;
    CHECK(refused(s));
    s = open_table(15);
    s.pending = Pending::after_foul;
    CHECK(refused(s));
    s = open_table(15);
    s.pending = Pending::toss;
    CHECK(refused(s));
    s = open_table(15);
    s.restart = true;
    CHECK(refused(s));
    s = open_table(15);
    s.cue_ball = 40;
    CHECK(refused(s));
    s = open_table(15);
    s.cue_ball = 3;
    CHECK(refused(s));
    s = open_table(15);
    s.striker = 2;
    CHECK(refused(s));
}

TEST_CASE("a simulator anomaly alone does not stop the judgement") {
    FakeOracle oracle;
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::red, 0);
    pot(f, at(s, Ball::red, 0));
    f.anomaly = true;
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.points == 1);
    CHECK(j.post.strokes == 1);
}
