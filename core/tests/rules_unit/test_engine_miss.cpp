#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;
namespace table = ofs::table;

namespace {

// Player 0 misses every ball with red on.
Judgement missed(const FrameState& s, const FakeOracle& oracle, const RefParams& ref = {}) {
    return judge(s, {}, scenario::quiet_facts(s), oracle, ref);
}

bool chosen(FrameState& s, DecisionKind kind, const FakeOracle& oracle, std::vector<Call>* calls = nullptr) {
    std::vector<Call> out;
    std::string error;
    const bool ok = decide(s, {kind, s.decider}, oracle, RefParams{}, out, error);
    if (calls != nullptr) *calls = out;
    return ok;
}

}  // namespace

TEST_CASE("a clear path to a ball on and no ball hit: FOUL AND A MISS (S3.14(c))") {
    FakeOracle oracle;
    const FrameState s = open_table(6);
    const Judgement j = missed(s, oracle);
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
    CHECK(j.post.last_miss);
    CHECK(j.post.stats[0].misses == 1);
    CHECK(j.post.stats[0].fouls == 1);
    REQUIRE(j.calls.size() == 2);
    CHECK(j.calls[0].kind == CallKind::foul);
    CHECK(j.calls[1].kind == CallKind::miss);
    CHECK(j.calls[1].detail == "S3.14(c)");
    REQUIRE_FALSE(oracle.path_on.empty());
    CHECK(oracle.path_on.back() == places_on(s));
}

TEST_CASE("a ball not on hit first with a clear path to a ball on is a miss too (S2.21(a))") {
    FakeOracle oracle;
    const FrameState s = open_table(6);
    const Judgement j = judge(s, {}, hit(s, Ball::blue), oracle, RefParams{});
    CHECK(j.penalty == 5);
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
}

TEST_CASE("a foul after a ball on was hit first is not a miss") {
    FakeOracle oracle;
    const FrameState s = open_table(6);
    ShotFacts f = hit(s, Ball::red, 0);
    in_off(f);
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.penalty == 4);
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason.empty());
    CHECK_FALSE(j.post.last_miss);
    CHECK(j.post.stats[0].misses == 0);
    CHECK_FALSE(listed(options(j.post, 1), DecisionKind::replace));
}

TEST_CASE("snookered with no clear path, a failed escape is still a miss: the television standard (S3.14(a))") {
    FakeOracle oracle;
    oracle.has_clear_path = false;
    oracle.has_full_ball = false;
    const FrameState s = open_table(6);
    const Judgement j = missed(s, oracle);
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(a)");
    CHECK(j.post.miss_run == 0);
}

TEST_CASE("a lenient gap spares a snookered player who came that close, and nobody with a clear path") {
    FakeOracle oracle;
    oracle.has_clear_path = false;
    oracle.has_full_ball = false;
    RefParams ref;
    ref.miss_lenient_gap_m = 0.01;
    const FrameState s = open_table(6);
    ShotFacts f = scenario::quiet_facts(s);
    f.closest_gap_m[std::size_t(at(s, Ball::red, 2))] = 0.004;
    Judgement j = judge(s, {}, f, oracle, ref);
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "lenient");
    CHECK(j.penalty == 4);

    f.closest_gap_m[std::size_t(at(s, Ball::red, 2))] = 0.02;
    f.closest_gap_m[std::size_t(at(s, Ball::blue))] = 0.001;
    j = judge(s, {}, f, oracle, ref);
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(a)");

    oracle.has_clear_path = true;
    f.closest_gap_m[std::size_t(at(s, Ball::red, 2))] = 0.004;
    j = judge(s, {}, f, oracle, ref);
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
}

TEST_CASE("no miss when the ball on cannot be hit at all (S3.14(a)(ii))") {
    FakeOracle oracle;
    oracle.reachable = false;
    const FrameState s = open_table(6);
    const Judgement j = missed(s, oracle);
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(ii)");
    CHECK(j.post.pending == Pending::after_foul);
    CHECK(options(j.post, 1) == std::vector<DecisionKind>{DecisionKind::play, DecisionKind::play_again});
}

TEST_CASE("no miss when a player needed penalty points before the stroke (S3.14(a)(i))") {
    FakeOracle oracle;
    FrameState s = open_table(3);
    s.scores = {70, 10};
    REQUIRE(penalty_points_required(s, 1) == 9);
    Judgement j = missed(s, oracle);
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(i)");

    s.scores = {10, 70};
    j = missed(s, oracle);
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(i)");
}

TEST_CASE("no miss when the penalty itself leaves the offender needing penalty points (S3.14(a)(i))") {
    FakeOracle oracle;
    FrameState s = open_table(3);
    s.scores = {10, 59};
    REQUIRE(penalty_points_required(s, 0) == 0);
    const Judgement j = missed(s, oracle);
    CHECK(j.post.scores[1] == 63);
    CHECK(penalty_points_required(j.post, 0) == 2);
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(i)");
}

TEST_CASE("the miss tests run in order: penalty points, then impossible, then a clear path") {
    FakeOracle oracle;
    oracle.reachable = false;
    FrameState s = open_table(3);
    s.scores = {70, 10};
    CHECK(missed(s, oracle).miss_reason == "S3.14(a)(i)");
    s.scores = {0, 0};
    CHECK(missed(s, oracle).miss_reason == "S3.14(a)(ii)");
}

TEST_CASE("a miss with a full ball there to be hit starts the run (S3.14(d))") {
    FakeOracle oracle;
    const FrameState s = open_table(6);
    CHECK(missed(s, oracle).post.miss_run == 1);
    oracle.has_full_ball = false;
    CHECK(missed(s, oracle).post.miss_run == 0);
}

TEST_CASE("a miss with the cue ball touching a ball that could be on starts the run (S3.14(d))") {
    FakeOracle oracle;
    oracle.has_full_ball = false;
    FrameState s = open_table(6);
    s.on = {OnKind::any_colour, Ball::yellow};
    const Vec2 blue = table::spot(Ball::blue);
    s.balls[0].pos = {blue.x, blue.y - table::kBallDiameter};
    const Judgement j = judge(s, scenario::nominate(Ball::pink), scenario::quiet_facts(s), oracle, RefParams{});
    CHECK(j.miss);
    CHECK(j.post.miss_run == 1);
}

TEST_CASE("replace puts every ball back as it was and the offender plays; the penalty stands (S3.14(b))") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    s.cue_in_hand = true;
    s.balls[0].pos = {table::baulk_line_x() - 0.1, 0.05};
    s.scores = {5, 2};
    ShotFacts f = hit(s, Ball::blue);
    pot(f, at(s, Ball::red, 1));
    f.rest[0] = {0.4, 0.1};
    f.rest[std::size_t(at(s, Ball::blue))] = {0.6, -0.3};
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    REQUIRE(j.miss);
    REQUIRE(j.post.balls.size() == s.balls.size() - 1);
    CHECK(options(j.post, 1) ==
          std::vector<DecisionKind>{DecisionKind::play, DecisionKind::play_again, DecisionKind::replace});

    FrameState back = j.post;
    std::vector<Call> calls;
    REQUIRE(chosen(back, DecisionKind::replace, oracle, &calls));
    CHECK(same(static_cast<const Position&>(back), static_cast<const Position&>(s)));
    CHECK(back.cue_in_hand);
    CHECK(back.striker == 0);
    CHECK(back.pending == Pending::none);
    CHECK(back.scores[0] == 5);
    CHECK(back.scores[1] == 7);
    CHECK(back.miss_run == 1);
    CHECK_FALSE(back.miss_warned);
    CHECK(back.strokes == 1);
    CHECK(back.stats[0].misses == 1);
    CHECK(calls.empty());
}

TEST_CASE("replace gives back a colour of choice and a free ball the offender had (S3.14(b)(iii))") {
    FakeOracle oracle;
    FrameState choice = open_table(6);
    choice.on = {OnKind::any_colour, Ball::yellow};
    const ShotInput blue = scenario::nominate(Ball::blue);
    FrameState after = judge(choice, blue, scenario::quiet_facts(choice), oracle, RefParams{}).post;
    REQUIRE(after.on.kind == OnKind::red);
    REQUIRE(chosen(after, DecisionKind::replace, oracle));
    CHECK(after.on.kind == OnKind::any_colour);

    FrameState free = open_table(6);
    free.free_ball = true;
    after = judge(free, blue, scenario::quiet_facts(free), oracle, RefParams{}).post;
    REQUIRE_FALSE(after.free_ball);
    std::vector<Call> calls;
    REQUIRE(chosen(after, DecisionKind::replace, oracle, &calls));
    CHECK(after.free_ball);
    CHECK(kinds(calls) == std::vector<CallKind>{CallKind::free_ball});
}

TEST_CASE("with a free ball available every object ball counts as one that could be on") {
    FakeOracle oracle;
    FrameState s = open_table(2);
    s.free_ball = true;
    const Judgement j = judge(s, scenario::nominate(Ball::blue), scenario::quiet_facts(s), oracle, RefParams{});
    CHECK(j.miss);
    REQUIRE_FALSE(oracle.path_on.empty());
    CHECK(oracle.path_on.back() == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8});
}

TEST_CASE("a second miss from the replaced position is a miss whatever the scores (S3.14(d)(i))") {
    FakeOracle oracle;
    FrameState after = missed(open_table(3), oracle).post;
    REQUIRE(after.miss_run == 1);
    REQUIRE(chosen(after, DecisionKind::replace, oracle));
    after.scores = {10, 70};
    REQUIRE(penalty_points_required(after, 0) > 0);

    oracle.reachable = false;
    oracle.has_clear_path = false;
    oracle.has_full_ball = false;
    const Judgement j = missed(after, oracle);
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(d)");
    CHECK(j.post.miss_run == 2);
    CHECK(listed(options(j.post, 1), DecisionKind::replace));
}

TEST_CASE("asked to play a third time from the original position, the offender is warned (S3.14(d)(ii))") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    std::vector<Call> calls;
    s = missed(s, oracle).post;
    REQUIRE(chosen(s, DecisionKind::replace, oracle, &calls));
    CHECK_FALSE(has_call(calls, CallKind::warning));
    CHECK_FALSE(s.miss_warned);

    s = missed(s, oracle).post;
    REQUIRE(s.miss_run == 2);
    REQUIRE_FALSE(s.over);
    REQUIRE(chosen(s, DecisionKind::replace, oracle, &calls));
    CHECK(kinds(calls) == std::vector<CallKind>{CallKind::warning});
    CHECK(s.miss_warned);
    CHECK(s.miss_run == 2);
}

TEST_CASE("a third miss after the warning loses the frame (S3.14(d)(ii))") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    s.scores = {50, 0};
    for (int i = 0; i < 2; ++i) {
        s = missed(s, oracle).post;
        REQUIRE(chosen(s, DecisionKind::replace, oracle));
    }
    REQUIRE(s.miss_warned);
    const Judgement j = missed(s, oracle);
    CHECK(j.miss);
    CHECK(j.post.over);
    CHECK(j.post.winner == 1);
    CHECK(j.post.end_reason == EndReason::three_misses);
    CHECK(j.post.pending == Pending::none);
    CHECK(j.post.scores[1] == 12);
    CHECK(kinds(j.calls) == std::vector<CallKind>{CallKind::foul, CallKind::miss, CallKind::frame_over});
    CHECK(j.calls[2].value == 1);
    CHECK(j.calls[2].detail == "three_misses");
    CHECK(options(j.post, 0).empty());
    CHECK(options(j.post, 1).empty());
}

TEST_CASE("after the warning, a ball on hit first ends the run") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    for (int i = 0; i < 2; ++i) {
        s = missed(s, oracle).post;
        REQUIRE(chosen(s, DecisionKind::replace, oracle));
    }
    const Judgement j = judge(s, {}, hit(s, Ball::red, 0), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.post.miss_run == 0);
    CHECK_FALSE(j.post.miss_warned);
    CHECK_FALSE(j.post.over);
}

TEST_CASE("play: the non-offender plays from the position left and the run ends (S3.14(d)(iii))") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    ShotFacts f = scenario::quiet_facts(s);
    f.rest[0] = {0.2, -0.4};
    s = judge(s, {}, f, oracle, RefParams{}).post;
    REQUIRE(s.miss_run == 1);
    const FrameState left = s;
    std::vector<Call> calls;
    REQUIRE(chosen(s, DecisionKind::play, oracle, &calls));
    CHECK(calls.empty());
    CHECK(s.striker == 1);
    CHECK(s.pending == Pending::none);
    CHECK(s.miss_run == 0);
    CHECK(s.balls[0].pos.x == 0.2);
    CHECK(s.scores == left.scores);
    CHECK(s.on.kind == OnKind::red);
    CHECK_FALSE(s.last_miss);
}

TEST_CASE("play again: the offender plays from the position left and the run ends (S3.10(h), S3.14(d)(iii))") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    ShotFacts f = scenario::quiet_facts(s);
    f.rest[0] = {0.2, -0.4};
    s = judge(s, {}, f, oracle, RefParams{}).post;
    REQUIRE(chosen(s, DecisionKind::play_again, oracle));
    CHECK(s.striker == 0);
    CHECK(s.pending == Pending::none);
    CHECK(s.miss_run == 0);
    CHECK_FALSE(s.miss_warned);
    CHECK(s.balls[0].pos.x == 0.2);
    CHECK(s.scores[1] == 4);
}

TEST_CASE("a decision's calls are its own: what the list held before is gone") {
    FakeOracle oracle;
    FrameState s = missed(open_table(6), oracle).post;
    std::vector<Call> calls{{CallKind::foul, 4, "no_hit"}};
    std::string error = "old";
    REQUIRE(decide(s, {DecisionKind::play, 1}, oracle, RefParams{}, calls, error));
    CHECK(calls.empty());
    CHECK(error.empty());
}

TEST_CASE("replace with no position kept is refused") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    s.pending = Pending::after_foul;
    s.decider = 1;
    s.striker = 1;
    s.offender = 0;
    s.last_miss = true;
    const FrameState before = s;
    std::vector<Call> calls;
    std::string error;
    CHECK_FALSE(decide(s, {DecisionKind::replace, 1}, oracle, RefParams{}, calls, error));
    CHECK_FALSE(error.empty());
    CHECK(same(s, before));
}

TEST_CASE("a choice that is not offered is refused and nothing changes") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    oracle.reachable = false;
    s = missed(s, oracle).post;
    REQUIRE_FALSE(s.last_miss);
    const FrameState before = s;
    std::vector<Call> calls;
    std::string error;
    CHECK_FALSE(decide(s, {DecisionKind::replace, 1}, oracle, RefParams{}, calls, error));
    CHECK_FALSE(error.empty());
    CHECK(same(s, before));
    CHECK_FALSE(decide(s, {DecisionKind::play, 0}, oracle, RefParams{}, calls, error));
    CHECK(same(s, before));
    CHECK_FALSE(decide(s, {DecisionKind::accept_rerack, 1}, oracle, RefParams{}, calls, error));
    CHECK(same(s, before));
    CHECK_FALSE(decide(s, {DecisionKind::concede_match, 1}, oracle, RefParams{}, calls, error));
    CHECK(same(s, before));
}
