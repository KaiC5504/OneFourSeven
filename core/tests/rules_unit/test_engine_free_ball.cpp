#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;
namespace table = ofs::table;

namespace {

// Three reds left and FREE BALL called for player 0.
FrameState free_ball_on_red() {
    FrameState s = open_table(3);
    s.free_ball = true;
    return s;
}

// Yellow on with the colours all there and FREE BALL called.
FrameState free_ball_on_yellow() {
    FrameState s = colours_left({Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black});
    s.free_ball = true;
    return s;
}

}  // namespace

TEST_CASE("FREE BALL is called when a foul leaves the next player snookered (S3.12)") {
    FakeOracle oracle;
    oracle.is_snookered = true;
    const FrameState s = open_table(3);
    const Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    CHECK(j.post.free_ball);
    CHECK(has_call(j.calls, CallKind::free_ball));
    CHECK(nominations(j.post).size() == 6);
    REQUIRE_FALSE(oracle.snookered_on.empty());
    CHECK(oracle.snookered_on.back() == places_on(j.post));
    CHECK_FALSE(oracle.snookered_in_hand.back());
}

TEST_CASE("no FREE BALL when the next player can see a ball on, or after a legal stroke") {
    FakeOracle oracle;
    const FrameState s = open_table(3);
    Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    CHECK_FALSE(j.post.free_ball);
    CHECK_FALSE(has_call(j.calls, CallKind::free_ball));

    oracle.is_snookered = true;
    j = judge(s, {}, hit(s, Ball::red, 0), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK_FALSE(j.post.free_ball);
}

TEST_CASE("after an in-off the snooker is judged from the whole D (S2.17(a))") {
    FakeOracle oracle;
    oracle.is_snookered = true;
    const FrameState s = open_table(3);
    ShotFacts f = hit(s, Ball::red, 0);
    in_off(f);
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.post.free_ball);
    REQUIRE_FALSE(oracle.snookered_in_hand.empty());
    CHECK(oracle.snookered_in_hand.back());
}

TEST_CASE("a free ball potted scores as the ball on and is spotted (S3.12(c))") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_red();
    ShotFacts f = hit(s, Ball::blue);
    pot(f, at(s, Ball::blue));
    const Judgement j = judge(s, scenario::nominate(Ball::blue), f, oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.free_ball_used);
    CHECK(j.points == 1);
    CHECK(j.post.scores[0] == 1);
    CHECK(j.post.striker == 0);
    CHECK(j.post.on.kind == OnKind::any_colour);
    CHECK_FALSE(j.post.free_ball);
    const int blue = at(j.post, Ball::blue);
    REQUIRE(blue >= 0);
    CHECK(j.post.balls[std::size_t(blue)].pos.x == table::spot(Ball::blue).x);
    CHECK(balls_count(j.post, Ball::red) == 3);
    REQUIRE(j.spotted.size() == 1);
    CHECK(j.spotted[0].ball == Ball::blue);
}

TEST_CASE("with red on, a free ball and a red both potted score two (S3.12(e))") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_red();
    ShotFacts f = hit(s, Ball::blue);
    pot(f, at(s, Ball::blue));
    pot(f, at(s, Ball::red, 1));
    const Judgement j = judge(s, scenario::nominate(Ball::blue), f, oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.points == 2);
    CHECK(balls_count(j.post, Ball::red) == 2);
    CHECK(at(j.post, Ball::blue) >= 0);
}

TEST_CASE("a red potted after the free ball was hit first scores (S3.12(d))") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_red();
    ShotFacts f = hit(s, Ball::blue);
    pot(f, at(s, Ball::red, 0));
    const Judgement j = judge(s, scenario::nominate(Ball::blue), f, oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.points == 1);
    CHECK(j.spotted.empty());
}

TEST_CASE("a free ball for a colour scores that colour and leaves it on (S3.12(c))") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_yellow();
    ShotFacts f = hit(s, Ball::pink);
    pot(f, at(s, Ball::pink));
    const Judgement j = judge(s, scenario::nominate(Ball::pink), f, oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.points == 2);
    CHECK(at(j.post, Ball::pink) >= 0);
    CHECK(j.post.on.kind == OnKind::colour);
    CHECK(j.post.on.colour == Ball::yellow);
    CHECK(j.post.striker == 0);
}

TEST_CASE("a free ball and the colour on both potted: only the colour scores and stays off (S3.12(e))") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_yellow();
    ShotFacts f = hit(s, Ball::pink);
    pot(f, at(s, Ball::pink));
    pot(f, at(s, Ball::yellow));
    const Judgement j = judge(s, scenario::nominate(Ball::pink), f, oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.points == 2);
    CHECK(at(j.post, Ball::yellow) == -1);
    CHECK(at(j.post, Ball::pink) >= 0);
    CHECK(j.post.on.colour == Ball::green);
}

TEST_CASE("the free ball not hit first is a foul, even for a ball on (S3.12(b)(i))") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_red();
    Judgement j = judge(s, scenario::nominate(Ball::blue), hit(s, Ball::red, 0), oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::first_hit_not_on);
    CHECK(j.fouls[0].value == 4);

    j = judge(s, scenario::nominate(Ball::blue), hit(s, Ball::black), oracle, RefParams{});
    CHECK(j.fouls[0].kind == FoulKind::first_hit_not_on);
    CHECK(j.penalty == 7);
}

TEST_CASE("a free ball hit at once with a ball on is legal; with a ball not on it is not (S3.6)") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_red();
    ShotFacts f = hit(s, Ball::red, 0);
    f.first_hit.push_back(at(s, Ball::blue));
    Judgement j = judge(s, scenario::nominate(Ball::blue), f, oracle, RefParams{});
    CHECK(j.fouls.empty());

    f = hit(s, Ball::blue);
    f.first_hit.push_back(at(s, Ball::pink));
    j = judge(s, scenario::nominate(Ball::blue), f, oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::simultaneous_hit);
    CHECK(j.fouls[0].value == 6);
}

TEST_CASE("with FREE BALL called and nothing declared, the ball hit first is the free ball (S2.12)") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_red();
    ShotFacts f = hit(s, Ball::green);
    pot(f, at(s, Ball::green));
    Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.free_ball_used);
    CHECK(j.nominated.has_nomination);
    CHECK(j.nominated.nominated == Ball::green);
    CHECK(j.points == 1);

    j = judge(s, {}, hit(s, Ball::red, 0), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK_FALSE(j.free_ball_used);
    CHECK_FALSE(j.nominated.has_nomination);
}

TEST_CASE("a free ball has the value of the ball on in a foul (S3.12(a)(ii))") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_red();
    ShotFacts f = hit(s, Ball::black);
    in_off(f);
    const Judgement j = judge(s, scenario::nominate(Ball::black), f, oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::in_off);
    CHECK(j.penalty == 4);
}

TEST_CASE("leaving the next player snookered behind the free ball is a foul (S3.12(b)(ii))") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_red();
    oracle.is_snookered = true;
    oracle.snookerers = {at(s, Ball::blue)};
    const Judgement j = judge(s, scenario::nominate(Ball::blue), hit(s, Ball::blue), oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::free_ball_snooker);
    CHECK(j.fouls[0].value == 4);
    CHECK(j.fouls[0].ball == at(s, Ball::blue));
    CHECK(j.post.scores[1] == 4);
    CHECK(j.post.pending == Pending::after_foul);
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason.empty());
    REQUIRE_FALSE(oracle.snookered_on.empty());
    CHECK(oracle.snookered_on.front() == std::vector<int>{1, 2, 3});
}

TEST_CASE("a snooker behind the free ball costs the colour on") {
    FakeOracle oracle;
    FrameState s = colours_left({Ball::blue, Ball::pink, Ball::black});
    s.free_ball = true;
    oracle.is_snookered = true;
    oracle.snookerers = {at(s, Ball::black)};
    const Judgement j = judge(s, scenario::nominate(Ball::black), hit(s, Ball::black), oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::free_ball_snooker);
    CHECK(j.penalty == 5);
}

TEST_CASE("no free-ball snooker when another ball snookers, or when the stroke scored") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_red();
    oracle.is_snookered = true;
    oracle.snookerers = {at(s, Ball::black)};
    Judgement j = judge(s, scenario::nominate(Ball::blue), hit(s, Ball::blue), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.post.striker == 1);

    oracle.snookerers = {at(s, Ball::blue) - 1};
    ShotFacts f = hit(s, Ball::blue);
    pot(f, at(s, Ball::red, 0));
    j = judge(s, scenario::nominate(Ball::blue), f, oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.points == 1);
}

TEST_CASE("with only two object balls left a snooker behind the free ball is allowed (S3.12(b)(ii))") {
    FakeOracle oracle;
    FrameState s = colours_left({Ball::pink, Ball::black});
    s.free_ball = true;
    oracle.is_snookered = true;
    oracle.snookerers = {at(s, Ball::black)};
    const Judgement j = judge(s, scenario::nominate(Ball::black), hit(s, Ball::black), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.free_ball_used);
    CHECK(j.post.striker == 1);
}

TEST_CASE("the free ball lapses after the stroke") {
    FakeOracle oracle;
    const FrameState s = free_ball_on_red();
    const Judgement j = judge(s, scenario::nominate(Ball::blue), hit(s, Ball::blue), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK_FALSE(j.post.free_ball);
    CHECK(nominations(j.post).empty());
}

TEST_CASE("the free ball is void once the offender is asked to play again (S3.12(f))") {
    FakeOracle oracle;
    oracle.is_snookered = true;
    const FrameState s = open_table(3);
    FrameState after = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{}).post;
    REQUIRE(after.free_ball);

    FrameState played = after;
    std::vector<Call> calls;
    std::string error;
    REQUIRE(decide(played, {DecisionKind::play, 1}, oracle, RefParams{}, calls, error));
    CHECK(played.free_ball);
    CHECK(played.striker == 1);
    CHECK(nominations(played).size() == 6);

    REQUIRE(decide(after, {DecisionKind::play_again, 1}, oracle, RefParams{}, calls, error));
    CHECK_FALSE(after.free_ball);
    CHECK(after.striker == 0);
    CHECK(nominations(after).empty());
}

TEST_CASE("a declaration that is not open is ignored (docs/rules/README.md)") {
    FakeOracle oracle;
    const FrameState s = open_table(3);
    const Judgement j = judge(s, scenario::nominate(Ball::blue), hit(s, Ball::blue), oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::first_hit_not_on);
    CHECK(j.penalty == 5);
    CHECK_FALSE(j.nominated.has_nomination);
    CHECK_FALSE(j.free_ball_used);

    const FrameState free = free_ball_on_red();
    const Judgement k = judge(free, scenario::nominate(Ball::red), hit(free, Ball::red, 0), oracle, RefParams{});
    CHECK(k.fouls.empty());
    CHECK_FALSE(k.free_ball_used);
}
