#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;
namespace table = ofs::table;

namespace {

// A legal safety that moves the object balls `travel` metres in all.
Judgement safety(const FrameState& s, double travel = 0.1) {
    FakeOracle oracle;
    ShotFacts f = hit(s, places_on(s).front());
    f.object_travel_m = travel;
    return judge(s, {}, f, oracle, RefParams{});
}

// The frame after `strokes` quiet safeties in a row.
FrameState after_safeties(FrameState s, int strokes) {
    for (int i = 0; i < strokes; ++i) s = safety(s).post;
    return s;
}

bool chosen(FrameState& s, DecisionKind kind, int player, std::vector<Call>* calls = nullptr) {
    FakeOracle oracle;
    std::vector<Call> out;
    std::string error;
    const bool ok = decide(s, {kind, player}, oracle, RefParams{}, out, error);
    if (calls != nullptr) *calls = out;
    return ok;
}

}  // namespace

TEST_CASE("a re-rack is offered to the player about to play after twelve quiet strokes (S3.17)") {
    const FrameState s = after_safeties(open_table(6), 11);
    CHECK(s.pending == Pending::none);
    CHECK(s.quiet_strokes == 11);
    CHECK(s.striker == 1);

    const Judgement j = safety(s);
    CHECK(j.post.quiet_strokes == 12);
    CHECK(j.post.pending == Pending::rerack);
    CHECK(j.post.striker == 0);
    CHECK(j.post.decider == 0);
    CHECK(j.post.rerack_accepted == -1);
    REQUIRE(j.calls.size() == 1);
    CHECK(j.calls[0].kind == CallKind::rerack_offer);
    CHECK(j.calls[0].value == 0);
    CHECK(options(j.post, 0) == std::vector<DecisionKind>{DecisionKind::accept_rerack, DecisionKind::refuse_rerack});
    CHECK(options(j.post, 1).empty());
}

TEST_CASE("a stroke that pots a ball or moves the balls about is not quiet and clears the count") {
    FrameState s = after_safeties(open_table(6), 8);
    REQUIRE(s.quiet_strokes == 8);
    CHECK(safety(s, 0.6).post.quiet_strokes == 0);
    CHECK(safety(s, 0.59).post.quiet_strokes == 9);

    FakeOracle oracle;
    ShotFacts f = hit(s, Ball::red, 0);
    pot(f, at(s, Ball::red, 0));
    CHECK(judge(s, {}, f, oracle, RefParams{}).post.quiet_strokes == 0);

    // An in-off is a foul: it neither counts nor clears.
    ShotFacts g = hit(s, Ball::red, 0);
    in_off(g);
    CHECK(judge(s, {}, g, oracle, RefParams{}).post.quiet_strokes == 8);
}

TEST_CASE("a foul stroke neither counts nor clears, quiet or not, and brings no offer") {
    FakeOracle oracle;
    const FrameState s = after_safeties(open_table(6), 11);
    const Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    CHECK(j.post.quiet_strokes == 11);
    CHECK(j.post.pending == Pending::after_foul);
    CHECK_FALSE(has_call(j.calls, CallKind::rerack_offer));

    ShotFacts lively = hit(s, Ball::pink);
    lively.object_travel_m = 2.0;
    CHECK(judge(s, {}, lively, oracle, RefParams{}).post.quiet_strokes == 11);
}

TEST_CASE("both players accepting restarts the frame (S3.17)") {
    FrameState s = after_safeties(open_table(6), 12);
    REQUIRE(s.pending == Pending::rerack);
    s.scores = {14, 9};
    std::vector<Call> calls;
    REQUIRE(chosen(s, DecisionKind::accept_rerack, 0, &calls));
    CHECK(s.pending == Pending::rerack);
    CHECK(s.rerack_accepted == 0);
    CHECK(s.decider == 1);
    CHECK_FALSE(s.restart);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].kind == CallKind::rerack_offer);
    CHECK(calls[0].value == 1);
    CHECK(options(s, 0).empty());
    CHECK(options(s, 1) == std::vector<DecisionKind>{DecisionKind::accept_rerack, DecisionKind::refuse_rerack});

    REQUIRE(chosen(s, DecisionKind::accept_rerack, 1, &calls));
    CHECK(s.restart);
    CHECK_FALSE(s.over);
    CHECK(s.pending == Pending::none);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].kind == CallKind::rerack);
    CHECK(options(s, 0).empty());
    CHECK(options(s, 1).empty());
}

TEST_CASE("a refusal lets play go on with six strokes for the position to change (S3.17(a))") {
    FrameState s = after_safeties(open_table(6), 12);
    std::vector<Call> calls;
    REQUIRE(chosen(s, DecisionKind::refuse_rerack, 0, &calls));
    CHECK(calls.empty());
    CHECK(s.pending == Pending::none);
    CHECK(s.striker == 0);
    CHECK(s.rerack_deadline == 6);
    CHECK(s.rerack_accepted == -1);
    CHECK_FALSE(s.restart);
}

TEST_CASE("the second player refusing counts the same as the first") {
    FrameState s = after_safeties(open_table(6), 12);
    REQUIRE(chosen(s, DecisionKind::accept_rerack, 0));
    REQUIRE(chosen(s, DecisionKind::refuse_rerack, 1));
    CHECK(s.pending == Pending::none);
    CHECK(s.striker == 0);
    CHECK(s.rerack_deadline == 6);
    CHECK(s.rerack_accepted == -1);
}

TEST_CASE("six more quiet strokes after a refusal restart the frame anyway (S3.17(b))") {
    FrameState s = after_safeties(open_table(6), 12);
    REQUIRE(chosen(s, DecisionKind::refuse_rerack, 0));
    for (int i = 0; i < 5; ++i) {
        const Judgement j = safety(s);
        CHECK(j.calls.empty());
        CHECK(j.post.pending == Pending::none);
        CHECK_FALSE(j.post.restart);
        s = j.post;
    }
    CHECK(s.rerack_deadline == 1);
    const Judgement j = safety(s);
    CHECK(j.post.restart);
    CHECK_FALSE(j.post.over);
    CHECK(j.post.pending == Pending::none);
    REQUIRE(j.calls.size() == 1);
    CHECK(j.calls[0].kind == CallKind::rerack);
}

TEST_CASE("a stroke that changes the position lifts the proviso and the count (S3.17(a))") {
    FrameState s = after_safeties(open_table(6), 12);
    REQUIRE(chosen(s, DecisionKind::refuse_rerack, 0));
    s = after_safeties(s, 3);
    REQUIRE(s.rerack_deadline == 3);
    s = safety(s, 1.5).post;
    CHECK(s.rerack_deadline == -1);
    CHECK(s.quiet_strokes == 0);
    s = after_safeties(s, 11);
    CHECK(s.pending == Pending::none);
    CHECK(safety(s).post.pending == Pending::rerack);
}

TEST_CASE("a stalemate in a re-spotted black sets only the black again, the same player to open (S3.17(d))") {
    FakeOracle oracle;
    FrameState s = colours_left({Ball::black});
    s.scores = {40, 47};
    ShotFacts f = hit(s, Ball::black);
    pot(f, at(s, Ball::black));
    s = judge(s, {}, f, oracle, RefParams{}).post;
    REQUIRE(s.pending == Pending::toss);
    const int opener = 1 - s.decider;
    REQUIRE(chosen(s, DecisionKind::play_second, s.decider));
    REQUIRE(s.striker == opener);

    for (int i = 0; i < 12; ++i) {
        ShotFacts g = hit(s, Ball::black);
        g.rest[std::size_t(s.cue_ball)] = {0.3, 0.2 + 0.01 * i};
        g.rest[std::size_t(at(s, Ball::black))] = {1.2, -0.3};
        g.object_travel_m = 0.02;
        s = judge(s, {}, g, oracle, RefParams{}).post;
    }
    REQUIRE(s.pending == Pending::rerack);
    REQUIRE(s.striker == opener);
    const std::array<int, 2> scores = s.scores;
    const int strokes = s.strokes;

    REQUIRE(chosen(s, DecisionKind::accept_rerack, opener));
    std::vector<Call> calls;
    REQUIRE(chosen(s, DecisionKind::accept_rerack, 1 - opener, &calls));
    CHECK(kinds(calls) == std::vector<CallKind>{CallKind::rerack});
    CHECK_FALSE(s.restart);
    CHECK_FALSE(s.over);
    CHECK(s.respotted_black);
    CHECK(s.pending == Pending::none);
    CHECK(s.striker == opener);
    CHECK(s.cue_in_hand);
    CHECK(s.scores == scores);
    CHECK(s.strokes == strokes);
    CHECK(s.quiet_strokes == 0);
    CHECK(s.rerack_deadline == -1);
    const int black = at(s, Ball::black);
    REQUIRE(black >= 0);
    CHECK(s.balls[std::size_t(black)].pos.x == table::spot(Ball::black).x);
    CHECK(s.balls[std::size_t(black)].pos.y == table::spot(Ball::black).y);
    CHECK(s.balls[std::size_t(s.cue_ball)].pos.x == table::default_cue_ball_position().x);
}
