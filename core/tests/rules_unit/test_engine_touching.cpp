#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;
namespace table = ofs::table;

namespace {

// The position with the cue ball moved up against that ball, beside it and clear of the others.
FrameState touching(FrameState s, int place) {
    const Vec2 pos = s.balls[std::size_t(place)].pos;
    s.balls[std::size_t(s.cue_ball)].pos = {pos.x, pos.y - table::kBallDiameter};
    return s;
}

// Facts that leave the cue ball at rest against that ball.
ShotFacts rests_against(const FrameState& s, ShotFacts f, int place) {
    const Vec2 pos = f.rest[std::size_t(place)];
    f.rest[std::size_t(s.cue_ball)] = {pos.x, pos.y - table::kBallDiameter};
    return f;
}

FrameState colour_of_choice() {
    FrameState s = open_table(14);
    s.on = {OnKind::any_colour, Ball::yellow};
    return s;
}

}  // namespace

TEST_CASE("TOUCHING BALL is called when the cue ball comes to rest against a ball on (S3.8(a))") {
    FakeOracle oracle;
    const FrameState s = open_table(15);
    const int red = at(s, Ball::red, 5);
    const Judgement j = judge(s, {}, rests_against(s, hit(s, red), red), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.post.touching == std::vector<int>{red});
    const Call* call = find_call(j.calls, CallKind::touching_ball);
    REQUIRE(call != nullptr);
    CHECK(call->detail == "red");
}

TEST_CASE("TOUCHING BALL is not called for a ball that is not on (S3.8(d))") {
    FakeOracle oracle;
    const FrameState s = open_table(15);
    const Judgement j =
        judge(s, {}, rests_against(s, hit(s, Ball::red, 0), at(s, Ball::blue)), oracle, RefParams{});
    CHECK(j.post.touching.empty());
    CHECK_FALSE(has_call(j.calls, CallKind::touching_ball));
}

TEST_CASE("after a red, TOUCHING BALL is called for any colour, named by value") {
    FakeOracle oracle;
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::red, 0);
    pot(f, at(s, Ball::red, 0));
    f.rest[std::size_t(s.cue_ball)] = {0.0, -0.5};
    f.rest[std::size_t(at(s, Ball::pink))] = {table::kBallDiameter, -0.5};
    f.rest[std::size_t(at(s, Ball::green))] = {-table::kBallDiameter, -0.5};
    f.rest[std::size_t(at(s, Ball::red, 9))] = {0.0, -0.5 - table::kBallDiameter};
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    REQUIRE(j.post.on.kind == OnKind::any_colour);
    CHECK(j.post.touching == std::vector<int>{at(j.post, Ball::green), at(j.post, Ball::pink)});
    const Call* call = find_call(j.calls, CallKind::touching_ball);
    REQUIRE(call != nullptr);
    CHECK(call->detail == "green,pink");
}

TEST_CASE("several reds touching are named once") {
    FakeOracle oracle;
    const FrameState s = open_table(15);
    ShotFacts f = hit(s, Ball::red, 0);
    f.rest[std::size_t(s.cue_ball)] = {0.0, -0.5};
    f.rest[std::size_t(at(s, Ball::red, 3))] = {table::kBallDiameter, -0.5};
    f.rest[std::size_t(at(s, Ball::red, 4))] = {-table::kBallDiameter, -0.5};
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.post.touching.size() == 2);
    const Call* call = find_call(j.calls, CallKind::touching_ball);
    REQUIRE(call != nullptr);
    CHECK(call->detail == "red");
}

TEST_CASE("with FREE BALL called any touching ball could be on") {
    FakeOracle oracle;
    oracle.is_snookered = true;
    const FrameState s = open_table(15);
    const int blue = at(s, Ball::blue);
    const Judgement j = judge(s, {}, rests_against(s, hit(s, blue), blue), oracle, RefParams{});
    REQUIRE(j.post.free_ball);
    CHECK(j.post.touching == std::vector<int>{blue});
    CHECK(kinds(j.calls) == std::vector<CallKind>{CallKind::foul, CallKind::miss, CallKind::free_ball,
                                                  CallKind::touching_ball});

    FrameState again = j.post;
    std::vector<Call> calls;
    std::string error;
    REQUIRE(decide(again, {DecisionKind::play_again, 1}, oracle, RefParams{}, calls, error));
    CHECK(again.touching.empty());
}

TEST_CASE("played away from a touching ball that is on, hitting nothing, is no foul (S3.8(c)(i))") {
    FakeOracle oracle;
    const FrameState s = touching(open_table(15), 3);
    const Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.points == 0);
    CHECK(j.post.striker == 1);
    CHECK(j.post.pending == Pending::none);
}

TEST_CASE("played away from a touching ball on, whatever is hit next is no foul for the first impact") {
    FakeOracle oracle;
    const FrameState s = touching(open_table(15), 3);
    const Judgement j = judge(s, {}, hit(s, Ball::blue), oracle, RefParams{});
    CHECK(j.fouls.empty());
}

TEST_CASE("a touching ball that moves is a push stroke (S3.8(b))") {
    FakeOracle oracle;
    const FrameState s = touching(open_table(15), 3);
    ShotFacts f = hit(s, 3);
    f.touching_moved = {3};
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::push_stroke);
    CHECK(j.fouls[0].value == 4);
    CHECK(j.fouls[0].ball == 3);
    CHECK_FALSE(j.miss);
}

TEST_CASE("a touching ball not on that moves costs its value") {
    FakeOracle oracle;
    FrameState s = open_table(15);
    const int black = at(s, Ball::black);
    s = touching(s, black);
    ShotFacts f = hit(s, black);
    f.touching_moved = {black};
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.penalty == 7);
    CHECK(has_foul(j, FoulKind::push_stroke));
    CHECK(has_foul(j, FoulKind::first_hit_not_on));
}

TEST_CASE("played away from a touching ball that is not on, hitting nothing, is a foul") {
    FakeOracle oracle;
    FrameState s = open_table(15);
    s = touching(s, at(s, Ball::blue));
    const Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::no_hit);
    CHECK(j.penalty == 4);
}

TEST_CASE("touching a colour after a red, none declared, played away: it is taken as declared (S3.8(c)(ii))") {
    FakeOracle oracle;
    FrameState s = colour_of_choice();
    s = touching(s, at(s, Ball::blue));
    const Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.nominated.has_nomination);
    CHECK(j.nominated.nominated == Ball::blue);
    CHECK(j.post.striker == 1);
    CHECK(j.post.on.kind == OnKind::red);
}

TEST_CASE("touching one colour, another declared and hit first, is no foul (S3.8(c)(iii))") {
    FakeOracle oracle;
    FrameState s = colour_of_choice();
    s = touching(s, at(s, Ball::blue));
    ShotFacts f = hit(s, Ball::pink);
    pot(f, at(s, Ball::pink));
    const Judgement j = judge(s, scenario::nominate(Ball::pink), f, oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.points == 6);
}

TEST_CASE("touching one colour, another declared and missed, is a foul for the declared colour") {
    FakeOracle oracle;
    FrameState s = colour_of_choice();
    s = touching(s, at(s, Ball::blue));
    const Judgement j = judge(s, scenario::nominate(Ball::pink), scenario::quiet_facts(s), oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::no_hit);
    CHECK(j.penalty == 6);
}

TEST_CASE("touching a colour after a red, nothing declared, a red hit next: reds in succession") {
    FakeOracle oracle;
    FrameState s = colour_of_choice();
    s = touching(s, at(s, Ball::blue));
    const Judgement j = judge(s, {}, hit(s, Ball::red, 0), oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::reds_in_succession);
    CHECK(j.penalty == 7);
}

TEST_CASE("with FREE BALL called, the one ball left touching and unmoved is taken as the free ball") {
    FakeOracle oracle;
    FrameState s = open_table(3);
    s.free_ball = true;
    s = touching(s, at(s, Ball::brown));
    const Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.free_ball_used);
    CHECK(j.nominated.nominated == Ball::brown);
}
