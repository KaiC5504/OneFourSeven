#include <doctest/doctest.h>

#include <vector>

#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;
namespace table = ofs::table;

namespace {

Judgement judged(const FrameState& s, const ShotFacts& f, ShotInput input = {}) {
    FakeOracle oracle;
    return judge(s, input, f, oracle, RefParams{});
}

FrameState colour_of_choice(int reds = 14) {
    FrameState s = open_table(reds);
    s.on = {OnKind::any_colour, Ball::yellow};
    return s;
}

}  // namespace

TEST_CASE("no ball hit with red on costs four and hands the next player the choice (S3.11(a)(vi), S3.10)") {
    FrameState s = open_table(15);
    s.scores = {20, 9};
    s.break_points = 20;
    const Judgement j = judged(s, scenario::quiet_facts(s));
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::no_hit);
    CHECK(j.fouls[0].value == 4);
    CHECK(j.fouls[0].ball == -1);
    CHECK(j.penalty == 4);
    CHECK(j.points == 0);
    CHECK(j.post.scores[0] == 20);
    CHECK(j.post.scores[1] == 13);
    CHECK(j.post.break_points == 0);
    CHECK(j.post.pending == Pending::after_foul);
    CHECK(j.post.decider == 1);
    CHECK(j.post.striker == 1);
    CHECK(j.post.offender == 0);
    CHECK(j.post.last_penalty == 4);
    CHECK(j.post.on.kind == OnKind::red);
    CHECK(same(j.post.original, s));
    CHECK(j.post.stats[0].fouls == 1);
    CHECK(j.post.stats[0].shots == 1);
    CHECK(j.post.stats[1].points_from_fouls == 4);
    REQUIRE_FALSE(j.calls.empty());
    CHECK(j.calls[0].kind == CallKind::foul);
    CHECK(j.calls[0].value == 4);
    CHECK(j.calls[0].detail == "no_hit");
}

TEST_CASE("no ball hit costs the value of the colour on (S3.11(a)(vi))") {
    const FrameState s = colours_left({Ball::blue, Ball::pink, Ball::black});
    const Judgement j = judged(s, scenario::quiet_facts(s));
    CHECK(j.penalty == 5);
    CHECK(j.post.on.kind == OnKind::colour);
    CHECK(j.post.on.colour == Ball::blue);
}

TEST_CASE("on red, the blue hit first costs five (S3.11(b)(iv))") {
    const FrameState s = open_table(15);
    const Judgement j = judged(s, hit(s, Ball::blue));
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::first_hit_not_on);
    CHECK(j.fouls[0].value == 5);
    CHECK(j.fouls[0].ball == at(s, Ball::blue));
    CHECK(j.penalty == 5);
}

TEST_CASE("on a colour, a lower ball hit first costs the colour on") {
    const FrameState s = colours_left({Ball::pink, Ball::black});
    FrameState with_red = s;
    with_red.balls.push_back(scenario::ball(Ball::red, 0.3, 0.5, 0));
    const Judgement j = judged(with_red, hit(with_red, Ball::red, 0));
    CHECK(has_foul(j, FoulKind::first_hit_not_on));
    CHECK(j.penalty == 6);
}

TEST_CASE("declared blue, the pink hit first costs six (S3.11(b)(iv))") {
    const FrameState s = colour_of_choice();
    const Judgement j = judged(s, hit(s, Ball::pink), scenario::nominate(Ball::blue));
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::first_hit_not_on);
    CHECK(j.fouls[0].value == 6);
    CHECK(j.nominated.nominated == Ball::blue);
}

TEST_CASE("declared black, a red hit first costs seven; declared yellow, four") {
    const FrameState s = colour_of_choice();
    Judgement j = judged(s, hit(s, Ball::red, 0), scenario::nominate(Ball::black));
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::first_hit_not_on);
    CHECK(j.penalty == 7);
    j = judged(s, hit(s, Ball::red, 0), scenario::nominate(Ball::yellow));
    CHECK(j.penalty == 4);
    CHECK(j.post.on.kind == OnKind::red);
}

TEST_CASE("on red, a red and the pink potted costs six and scores nothing (S3.11(b)(iii), S3.10(e))") {
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::red, 2);
    pot(f, at(s, Ball::red, 2));
    pot(f, at(s, Ball::pink));
    const Judgement j = judged(s, f);
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::pot_not_on);
    CHECK(j.fouls[0].value == 6);
    CHECK(j.fouls[0].ball == at(s, Ball::pink));
    CHECK(j.points == 0);
    CHECK(j.post.scores[0] == 0);
    CHECK(j.post.scores[1] == 6);
    CHECK(j.post.stats[0].pots == 0);
}

TEST_CASE("in a foul stroke a red stays off and a colour is spotted (S3.3(i), S3.7)") {
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::red, 2);
    pot(f, at(s, Ball::red, 2));
    pot(f, at(s, Ball::pink));
    const Judgement j = judged(s, f);
    CHECK(at(j.post, Ball::red, 2) == -1);
    const int pink = at(j.post, Ball::pink);
    REQUIRE(pink >= 0);
    CHECK(j.post.balls[std::size_t(pink)].pos.x == table::spot(Ball::pink).x);
    REQUIRE(j.spotted.size() == 1);
    CHECK(j.spotted[0].ball == Ball::pink);
}

TEST_CASE("a colour pocketed in a foul is spotted once the reds are gone too (S3.7)") {
    const FrameState s = colours_left({Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black});
    ShotFacts f = hit(s, Ball::yellow);
    pot(f, at(s, Ball::yellow));
    in_off(f);
    const Judgement j = judged(s, f);
    CHECK(j.penalty == 4);
    CHECK(at(j.post, Ball::yellow) == at(s, Ball::yellow));
    CHECK(j.post.on.kind == OnKind::colour);
    CHECK(j.post.on.colour == Ball::yellow);
    CHECK(j.post.scores[0] == 0);
}

TEST_CASE("in-off on red costs four and leaves the cue ball in hand (S3.11(a)(vii), S3.10(f))") {
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::red, 0);
    in_off(f);
    const Judgement j = judged(s, f);
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::in_off);
    CHECK(j.fouls[0].value == 4);
    CHECK(j.post.cue_in_hand);
    const Vec2 cue = j.post.balls[std::size_t(j.post.cue_ball)].pos;
    CHECK(cue.x == table::default_cue_ball_position().x);
    CHECK(cue.y == table::default_cue_ball_position().y);
    CHECK(j.post.touching.empty());
}

TEST_CASE("in-off costs the value of the ball on: seven on the declared black") {
    const FrameState s = colour_of_choice();
    ShotFacts f = hit(s, Ball::black);
    in_off(f);
    const Judgement j = judged(s, f, scenario::nominate(Ball::black));
    CHECK(j.penalty == 7);
    CHECK(j.calls[0].detail == "in_off");
}

TEST_CASE("a red and the black hit at once on red costs seven (S3.6, S3.11(c))") {
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::red, 0);
    f.first_hit.push_back(at(s, Ball::black));
    const Judgement j = judged(s, f);
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::simultaneous_hit);
    CHECK(j.fouls[0].value == 7);
    CHECK(j.fouls[0].ball == at(s, Ball::black));
}

TEST_CASE("two reds hit at once on red is no foul (S3.6)") {
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::red, 0);
    f.first_hit.push_back(at(s, Ball::red, 1));
    pot(f, at(s, Ball::red, 1));
    const Judgement j = judged(s, f);
    CHECK(j.fouls.empty());
    CHECK(j.points == 1);
}

TEST_CASE("two colours hit at once in the final sequence costs the higher of them") {
    const FrameState s = colours_left({Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black});
    ShotFacts f = hit(s, Ball::green);
    f.first_hit.push_back(at(s, Ball::pink));
    const Judgement j = judged(s, f);
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::simultaneous_hit);
    CHECK(j.fouls[0].value == 6);
}

TEST_CASE("a push on a red costs four (S2.19, S3.11(b)(v))") {
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::red, 0);
    f.push = true;
    pot(f, at(s, Ball::red, 0));
    const Judgement j = judged(s, f);
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::push_stroke);
    CHECK(j.fouls[0].value == 4);
    CHECK(j.fouls[0].ball == at(s, Ball::red, 0));
    CHECK(j.points == 0);
}

TEST_CASE("a push on a ball not on costs that ball when it is higher") {
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::pink);
    f.push = true;
    const Judgement j = judged(s, f);
    CHECK(j.penalty == 6);
    CHECK(has_foul(j, FoulKind::push_stroke));
    CHECK(has_foul(j, FoulKind::first_hit_not_on));
    CHECK(j.fouls[0].kind == FoulKind::first_hit_not_on);
}

TEST_CASE("a red played at with a colour of choice on and none declared costs seven (S3.11(d)(iii))") {
    const FrameState s = colour_of_choice();
    const Judgement j = judged(s, hit(s, Ball::red, 0));
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::reds_in_succession);
    CHECK(j.fouls[0].value == 7);
    CHECK_FALSE(j.nominated.has_nomination);
    CHECK(j.calls[0].value == 7);
    CHECK(j.calls[0].detail == "reds_in_succession");
}

TEST_CASE("a colour of choice on, nothing declared, nothing hit costs seven (S3.11(d)(vi))") {
    const FrameState s = colour_of_choice();
    const Judgement j = judged(s, scenario::quiet_facts(s));
    REQUIRE(j.fouls.size() == 2);
    CHECK(j.fouls[0].kind == FoulKind::before_nominating);
    CHECK(j.fouls[0].value == 7);
    CHECK(j.fouls[1].kind == FoulKind::no_hit);
    CHECK(j.fouls[1].value == 4);
    CHECK(j.penalty == 7);
    CHECK(j.post.scores[1] == 7);
    CHECK(j.calls[0].detail == "before_nominating");
}

TEST_CASE("a declared colour missed altogether costs that colour, not seven") {
    const FrameState s = colour_of_choice();
    const Judgement j = judged(s, scenario::quiet_facts(s), scenario::nominate(Ball::blue));
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::no_hit);
    CHECK(j.penalty == 5);
}

TEST_CASE("the highest of several fouls is the penalty and the list runs highest first (S3.10(g))") {
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::blue);
    pot(f, at(s, Ball::black));
    in_off(f);
    const Judgement j = judged(s, f);
    REQUIRE(j.fouls.size() == 3);
    CHECK(j.fouls[0].kind == FoulKind::pot_not_on);
    CHECK(j.fouls[0].value == 7);
    CHECK(j.fouls[1].kind == FoulKind::first_hit_not_on);
    CHECK(j.fouls[1].value == 5);
    CHECK(j.fouls[2].kind == FoulKind::in_off);
    CHECK(j.fouls[2].value == 4);
    CHECK(j.penalty == 7);
    CHECK(j.post.scores[1] == 7);
    CHECK(j.post.stats[0].fouls == 1);
    CHECK(j.calls[0].detail == "pot_not_on");
}

TEST_CASE("fouls of one value are listed in FoulKind order") {
    const FrameState s = open_table(15);
    ShotFacts f = scenario::quiet_facts(s);
    in_off(f);
    const Judgement j = judged(s, f);
    REQUIRE(j.fouls.size() == 2);
    CHECK(j.fouls[0].kind == FoulKind::no_hit);
    CHECK(j.fouls[1].kind == FoulKind::in_off);
    CHECK(j.penalty == 4);
}

TEST_CASE("colours to spot go to the oracle in one call, without the cue ball when it is in hand (S3.7)") {
    FakeOracle oracle;
    const FrameState s = open_table(2);
    ShotFacts f = hit(s, Ball::red, 0);
    pot(f, at(s, Ball::blue));
    pot(f, at(s, Ball::black));
    in_off(f);
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    REQUIRE(oracle.spot_tables.size() == 1);
    CHECK(oracle.spot_tables[0].size() == 6);
    for (const BallOnTable& b : oracle.spot_tables[0]) {
        CHECK(b.ball != Ball::cue);
        CHECK(b.ball != Ball::blue);
        CHECK(b.ball != Ball::black);
    }
    CHECK(j.spotted.size() == 2);
    CHECK(j.post.balls.size() == s.balls.size());

    FakeOracle on_table;
    ShotFacts g = hit(s, Ball::red, 0);
    pot(g, at(s, Ball::blue));
    judge(s, {}, g, on_table, RefParams{});
    REQUIRE(on_table.spot_tables.size() == 1);
    CHECK(on_table.spot_tables[0].size() == 8);
}

TEST_CASE("after a foul red is on while a red remains, else the lowest colour (S3.3(g))") {
    FrameState s = colour_of_choice(0);
    Judgement j = judged(s, scenario::quiet_facts(s), scenario::nominate(Ball::pink));
    CHECK(j.penalty == 6);
    CHECK(j.post.on.kind == OnKind::colour);
    CHECK(j.post.on.colour == Ball::yellow);

    s = colour_of_choice(3);
    j = judged(s, scenario::quiet_facts(s), scenario::nominate(Ball::pink));
    CHECK(j.post.on.kind == OnKind::red);
}
