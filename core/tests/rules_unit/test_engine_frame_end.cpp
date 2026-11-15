#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ofs/rng.hpp"
#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;
namespace table = ofs::table;

namespace {

FrameState black_only(int score0, int score1) {
    FrameState s = colours_left({Ball::black});
    s.scores = {score0, score1};
    s.seed = 2026;
    s.strokes = 60;
    return s;
}

Judgement black_potted(const FrameState& s) {
    FakeOracle oracle;
    ShotFacts f = hit(s, Ball::black);
    pot(f, at(s, Ball::black));
    return judge(s, {}, f, oracle, RefParams{});
}

Judgement black_missed(const FrameState& s) {
    FakeOracle oracle;
    return judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
}

bool chosen(FrameState& s, DecisionKind kind, int player, std::vector<Call>* calls = nullptr) {
    FakeOracle oracle;
    std::vector<Call> out;
    std::string error;
    const bool ok = decide(s, {kind, player}, oracle, RefParams{}, out, error);
    if (calls != nullptr) *calls = out;
    return ok;
}

// A re-spotted black with the toss decided: `opener` to play from in hand.
FrameState respotted(int opener) {
    FrameState s = black_potted(black_only(40, 47)).post;
    REQUIRE(s.pending == Pending::toss);
    REQUIRE(chosen(s, s.decider == opener ? DecisionKind::play_first : DecisionKind::play_second, s.decider));
    return s;
}

}  // namespace

TEST_CASE("the final black potted ends the frame for the higher score (S3.4(a))") {
    const Judgement j = black_potted(black_only(50, 40));
    CHECK(j.points == 7);
    CHECK(j.post.over);
    CHECK(j.post.winner == 0);
    CHECK(j.post.end_reason == EndReason::black);
    CHECK(j.post.scores[0] == 57);
    CHECK(j.post.pending == Pending::none);
    CHECK(j.post.balls.size() == 1);
    REQUIRE(j.calls.size() == 1);
    CHECK(j.calls[0].kind == CallKind::frame_over);
    CHECK(j.calls[0].value == 0);
    CHECK(j.calls[0].detail == "black");
    CHECK(options(j.post, 0).empty());
    CHECK(options(j.post, 1).empty());
}

TEST_CASE("the final black potted can win the frame from behind") {
    const Judgement j = black_potted(black_only(40, 45));
    CHECK(j.post.over);
    CHECK(j.post.winner == 0);
    CHECK(j.post.scores[0] == 47);
}

TEST_CASE("a foul on the final black costs seven and ends the frame (S3.4(a))") {
    const Judgement j = black_missed(black_only(50, 46));
    CHECK(j.penalty == 7);
    CHECK(j.post.over);
    CHECK(j.post.winner == 1);
    CHECK(j.post.end_reason == EndReason::black);
    CHECK(j.post.scores[1] == 53);
    CHECK(j.post.pending == Pending::none);
    CHECK_FALSE(j.miss);
    CHECK(kinds(j.calls) == std::vector<CallKind>{CallKind::foul, CallKind::frame_over});
    CHECK(j.calls[1].value == 1);
}

TEST_CASE("a legal miss on the final black passes the turn and the frame goes on") {
    FakeOracle oracle;
    const FrameState s = black_only(50, 46);
    const Judgement j = judge(s, {}, hit(s, Ball::black), oracle, RefParams{});
    CHECK_FALSE(j.post.over);
    CHECK(j.post.striker == 1);
    CHECK(j.post.on.colour == Ball::black);
}

TEST_CASE("the black potted to level the scores is re-spotted and lots are drawn (S3.4(b))") {
    const FrameState s = black_only(40, 47);
    const Judgement j = black_potted(s);
    CHECK_FALSE(j.post.over);
    CHECK(j.post.scores[0] == 47);
    CHECK(j.post.respotted_black);
    CHECK(j.post.pending == Pending::toss);
    CHECK(j.post.cue_in_hand);
    CHECK(j.post.on.kind == OnKind::colour);
    CHECK(j.post.on.colour == Ball::black);
    CHECK(j.post.break_points == 0);
    REQUIRE(j.post.balls.size() == 2);
    CHECK(j.post.cue_ball == 0);
    CHECK(j.post.balls[1].ball == Ball::black);
    CHECK(j.post.balls[1].pos.x == table::spot(Ball::black).x);
    CHECK(j.post.balls[1].pos.y == table::spot(Ball::black).y);
    CHECK(j.post.balls[0].pos.x == table::default_cue_ball_position().x);
    CHECK(points_remaining(j.post) == 7);

    ofs::Rng lots(j.post.seed + 7919 * std::uint64_t(j.post.strokes));
    const int winner = int(lots.next_u64() & 1);
    CHECK(j.post.decider == winner);
    CHECK(j.post.strokes == 61);
    REQUIRE(j.calls.size() == 1);
    CHECK(j.calls[0].kind == CallKind::respotted_black);
    CHECK(j.calls[0].value == winner);
    CHECK(options(j.post, winner) == std::vector<DecisionKind>{DecisionKind::play_first, DecisionKind::play_second});
    CHECK(options(j.post, 1 - winner).empty());
}

TEST_CASE("the re-spotted black keeps its place in the ball list, wherever the cue ball is listed") {
    FakeOracle oracle;
    FrameState s = scenario::frame({scenario::spotted(Ball::black), scenario::ball(Ball::cue, -0.6, 0.35)}, 1);
    s.scores = {40, 47};
    ShotFacts f = hit(s, 0);
    pot(f, 0);
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    REQUIRE(j.post.pending == Pending::toss);
    REQUIRE(j.post.balls.size() == 2);
    CHECK(j.post.balls[0].ball == Ball::black);
    CHECK(j.post.cue_ball == 1);
    CHECK(j.post.balls[1].ball == Ball::cue);
    REQUIRE(j.spotted.size() == 1);
    CHECK(j.spotted[0].ball == Ball::black);
}

TEST_CASE("a foul on the final black that levels the scores brings the re-spotted black too") {
    FakeOracle oracle;
    const FrameState s = black_only(47, 40);
    ShotFacts f = scenario::quiet_facts(s);
    f.rest[1] = {0.7, 0.3};
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.penalty == 7);
    CHECK_FALSE(j.post.over);
    CHECK(j.post.respotted_black);
    CHECK(j.post.pending == Pending::toss);
    CHECK(j.post.cue_in_hand);
    CHECK(j.post.balls[1].pos.x == table::spot(Ball::black).x);
    CHECK(kinds(j.calls) == std::vector<CallKind>{CallKind::foul, CallKind::respotted_black});
}

TEST_CASE("the black pocketed in a levelling foul is listed as spotted once") {
    FakeOracle oracle;
    const FrameState s = black_only(47, 40);
    ShotFacts f = hit(s, Ball::black);
    pot(f, at(s, Ball::black));
    in_off(f);
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.penalty == 7);
    CHECK(j.points == 0);
    REQUIRE(j.post.pending == Pending::toss);
    REQUIRE(j.post.balls.size() == 2);
    REQUIRE(j.spotted.size() == 1);
    CHECK(j.spotted[0].ball == Ball::black);
    CHECK(j.spotted[0].pos.x == table::spot(Ball::black).x);
}

TEST_CASE("the toss follows the frame's seed and stroke count, and both players win it for some seed") {
    bool won[2] = {false, false};
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        FrameState s = black_only(40, 47);
        s.seed = seed;
        const Judgement a = black_potted(s);
        const Judgement b = black_potted(s);
        CHECK(a.post.decider == b.post.decider);
        won[a.post.decider] = true;
    }
    CHECK(won[0]);
    CHECK(won[1]);
}

TEST_CASE("the winner of the lots chooses who plays the re-spotted black first (S3.4(b)(ii), (iii))") {
    const FrameState tossed = black_potted(black_only(40, 47)).post;
    const int winner = tossed.decider;

    FrameState first = tossed;
    REQUIRE(chosen(first, DecisionKind::play_first, winner));
    CHECK(first.striker == winner);
    CHECK(first.pending == Pending::none);
    CHECK(first.cue_in_hand);

    FrameState second = tossed;
    REQUIRE(chosen(second, DecisionKind::play_second, winner));
    CHECK(second.striker == 1 - winner);
    CHECK(second.pending == Pending::none);

    FrameState wrong = tossed;
    CHECK_FALSE(chosen(wrong, DecisionKind::play_first, 1 - winner));
    CHECK(same(wrong, tossed));
}

TEST_CASE("the first pot on the re-spotted black wins the frame (S3.4(b)(iv))") {
    const FrameState s = respotted(1);
    const Judgement j = black_potted(s);
    CHECK(j.post.over);
    CHECK(j.post.winner == 1);
    CHECK(j.post.end_reason == EndReason::respotted_black);
    CHECK(j.post.scores[1] == 54);
    CHECK(j.calls.back().detail == "respotted_black");
}

TEST_CASE("the first foul on the re-spotted black loses the frame (S3.4(b)(iv))") {
    const FrameState s = respotted(1);
    const Judgement j = black_missed(s);
    CHECK(j.penalty == 7);
    CHECK(j.post.over);
    CHECK(j.post.winner == 0);
    CHECK(j.post.end_reason == EndReason::respotted_black);
}

TEST_CASE("a legal miss on the re-spotted black passes the turn") {
    FakeOracle oracle;
    const FrameState s = respotted(0);
    const Judgement j = judge(s, {}, hit(s, Ball::black), oracle, RefParams{});
    CHECK_FALSE(j.post.over);
    CHECK(j.post.striker == 1);
    CHECK(j.post.respotted_black);
    CHECK(j.post.pending == Pending::none);
}

TEST_CASE("the player who comes to the black more than seven behind has lost (S2.1(c))") {
    FakeOracle oracle;
    const FrameState s = black_only(60, 52);
    const Judgement j = judge(s, {}, hit(s, Ball::black), oracle, RefParams{});
    CHECK(j.fouls.empty());
    CHECK(j.post.over);
    CHECK(j.post.winner == 0);
    CHECK(j.post.end_reason == EndReason::awarded);
    REQUIRE(j.calls.size() == 1);
    CHECK(j.calls[0].kind == CallKind::frame_over);
    CHECK(j.calls[0].detail == "awarded");
}

TEST_CASE("seven behind on the black still plays on") {
    FakeOracle oracle;
    const FrameState s = black_only(60, 53);
    const Judgement j = judge(s, {}, hit(s, Ball::black), oracle, RefParams{});
    CHECK_FALSE(j.post.over);
    CHECK(j.post.striker == 1);
}

TEST_CASE("potting the pink and still more than seven behind loses the frame at once (S2.1(c))") {
    FakeOracle oracle;
    FrameState s = colours_left({Ball::pink, Ball::black});
    s.scores = {30, 45};
    ShotFacts f = hit(s, Ball::pink);
    pot(f, at(s, Ball::pink));
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK(j.points == 6);
    CHECK(j.post.over);
    CHECK(j.post.winner == 1);
    CHECK(j.post.end_reason == EndReason::awarded);
}

TEST_CASE("more than seven ahead on the black the striker may claim the frame or play on (S2.1(b))") {
    FrameState s = black_only(60, 52);
    // The other player needs a penalty point, so either may concede as well (S4.2(b)).
    CHECK(options(s, 0) == std::vector<DecisionKind>{DecisionKind::claim_frame, DecisionKind::concede_frame,
                                                     DecisionKind::concede_match});
    CHECK(options(s, 1) == std::vector<DecisionKind>{DecisionKind::concede_frame, DecisionKind::concede_match});

    std::vector<Call> calls;
    REQUIRE(chosen(s, DecisionKind::claim_frame, 0, &calls));
    CHECK(s.over);
    CHECK(s.winner == 0);
    CHECK(s.end_reason == EndReason::claimed);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].kind == CallKind::frame_over);
    CHECK(calls[0].value == 0);
    CHECK(calls[0].detail == "claimed");
}

TEST_CASE("no claim seven ahead, with another ball left, or on a re-spotted black") {
    CHECK_FALSE(listed(options(black_only(60, 53), 0), DecisionKind::claim_frame));

    FrameState pink = colours_left({Ball::pink, Ball::black});
    pink.scores = {80, 20};
    CHECK_FALSE(listed(options(pink, 0), DecisionKind::claim_frame));

    FrameState again = black_only(60, 40);
    again.respotted_black = true;
    CHECK_FALSE(listed(options(again, 0), DecisionKind::claim_frame));
    FrameState copy = again;
    CHECK_FALSE(chosen(copy, DecisionKind::claim_frame, 0));
    CHECK(same(copy, again));
}

TEST_CASE("a player allowed to concede at any time may; the other only when penalty points are needed (S4.2(b))") {
    FrameState s = open_table(10);
    s.concede_any_time = {true, false};
    CHECK(options(s, 0) == std::vector<DecisionKind>{DecisionKind::concede_frame, DecisionKind::concede_match});
    CHECK(options(s, 1) == std::vector<DecisionKind>{DecisionKind::concede_match});

    s.scores = {120, 0};
    REQUIRE(penalty_points_required(s, 1) > 0);
    CHECK(listed(options(s, 1), DecisionKind::concede_frame));
    CHECK(listed(options(s, 0), DecisionKind::concede_frame));
}

TEST_CASE("conceding the frame gives it to the other player (S4.2)") {
    FrameState s = open_table(10);
    s.concede_any_time = {true, false};
    s.scores = {30, 12};
    std::vector<Call> calls;
    REQUIRE(chosen(s, DecisionKind::concede_frame, 0, &calls));
    CHECK(s.over);
    CHECK(s.winner == 1);
    CHECK(s.end_reason == EndReason::conceded);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].kind == CallKind::frame_over);
    CHECK(calls[0].value == 1);
    CHECK(calls[0].detail == "conceded");
}

TEST_CASE("a concession that is not open is refused") {
    FrameState s = open_table(10);
    const FrameState before = s;
    CHECK_FALSE(chosen(s, DecisionKind::concede_frame, 1));
    CHECK(same(s, before));
    CHECK_FALSE(chosen(s, DecisionKind::concede_frame, 2));
    CHECK(same(s, before));
}

TEST_CASE("conceding the match ends the frame for the other player, and is open to either") {
    FrameState s = open_table(10);
    std::vector<Call> calls;
    REQUIRE(chosen(s, DecisionKind::concede_match, 1, &calls));
    CHECK(s.over);
    CHECK(s.winner == 0);
    CHECK(s.end_reason == EndReason::match_conceded);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].detail == "match_conceded");
    CHECK_FALSE(chosen(s, DecisionKind::concede_match, 0));
}
