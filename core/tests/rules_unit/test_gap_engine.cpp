#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;
namespace table = ofs::table;

namespace {

bool chosen(FrameState& s, DecisionKind kind, int player, const FakeOracle& oracle = {},
            std::vector<Call>* calls = nullptr, const RefParams& ref = {}) {
    std::vector<Call> out;
    std::string error;
    const bool ok = decide(s, {kind, player}, oracle, ref, out, error);
    if (calls != nullptr) *calls = out;
    return ok;
}

FrameState black_only(int score0, int score1) {
    FrameState s = colours_left({Ball::black});
    s.scores = {score0, score1};
    s.seed = 2026;
    s.strokes = 60;
    return s;
}

// Red on, FREE BALL called, the cue ball against the blue.
FrameState free_ball_touching_blue() {
    FrameState s = open_table(6);
    const Vec2 cue = s.balls[std::size_t(s.cue_ball)].pos;
    s.balls[std::size_t(at(s, Ball::blue))].pos = {cue.x + table::kBallDiameter, cue.y};
    s.free_ball = true;
    s.touching = {at(s, Ball::blue)};
    return s;
}

Judgement safety(const FrameState& s) {
    FakeOracle oracle;
    ShotFacts f = hit(s, places_on(s).front());
    f.object_travel_m = 0.1;
    return judge(s, {}, f, oracle, RefParams{});
}

FrameState after_safeties(FrameState s, int strokes) {
    for (int i = 0; i < strokes; ++i) s = safety(s).post;
    return s;
}

}  // namespace

TEST_CASE("gap S3.4(a): a foul on the final black by a player more than seven ahead ends the frame, for them") {
    FakeOracle oracle;
    const FrameState s = black_only(60, 50);
    REQUIRE(listed(options(s, 0), DecisionKind::claim_frame));
    const Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    CHECK(j.penalty == 7);
    CHECK(j.post.scores == std::array<int, 2>{60, 57});
    CHECK(j.post.over);
    CHECK(j.post.winner == 0);
    CHECK(j.post.end_reason == EndReason::black);
    CHECK(j.post.pending == Pending::none);
    CHECK_FALSE(j.miss);
}

TEST_CASE("gap S3.4(a): a foul on the final black seven behind loses the frame by fourteen") {
    FakeOracle oracle;
    const FrameState s = black_only(40, 47);
    REQUIRE_FALSE(s.over);
    const Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    CHECK(j.post.scores == std::array<int, 2>{40, 54});
    CHECK(j.post.over);
    CHECK(j.post.winner == 1);
    CHECK_FALSE(j.post.respotted_black);
}

TEST_CASE("gap S3.11(a)(vii): an in-off while potting a red costs four, the red stays off and nothing scores") {
    FakeOracle oracle;
    const FrameState s = open_table(6);
    ShotFacts f = hit(s, Ball::red, 0);
    pot(f, at(s, Ball::red, 0));
    in_off(f);
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::in_off);
    CHECK(j.penalty == 4);
    CHECK(j.points == 0);
    CHECK(j.post.scores == std::array<int, 2>{0, 4});
    CHECK(balls_count(j.post, Ball::red) == 5);
    CHECK(j.post.cue_in_hand);
    CHECK(j.post.on.kind == OnKind::red);
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason.empty());
    CHECK(j.post.pending == Pending::after_foul);
    CHECK(options(j.post, 1) == std::vector<DecisionKind>{DecisionKind::play, DecisionKind::play_again});
    CHECK(j.spotted.empty());
}

TEST_CASE("gap S3.11(a)(vii): an in-off while potting the declared colour costs that colour, which is spotted") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    s.on = {OnKind::any_colour, Ball::yellow};
    ShotFacts f = hit(s, Ball::blue);
    pot(f, at(s, Ball::blue));
    in_off(f);
    const Judgement j = judge(s, scenario::nominate(Ball::blue), f, oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::in_off);
    CHECK(j.penalty == 5);
    CHECK(j.points == 0);
    CHECK(balls_count(j.post, Ball::blue) == 1);
    const int blue = at(j.post, Ball::blue);
    REQUIRE(blue >= 0);
    CHECK(j.post.balls[std::size_t(blue)].pos.x == table::spot(Ball::blue).x);
    CHECK(j.post.balls[std::size_t(blue)].pos.y == table::spot(Ball::blue).y);
    REQUIRE(j.spotted.size() == 1);
    CHECK(j.spotted[0].ball == Ball::blue);
    CHECK(j.post.on.kind == OnKind::red);
    CHECK(j.post.cue_in_hand);
}

TEST_CASE("gap S3.12(b)(i): a free ball nominated, the ball on hit first and the free ball potted") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    s.free_ball = true;
    ShotFacts f = hit(s, Ball::red, 0);
    pot(f, at(s, Ball::green));
    const Judgement j = judge(s, scenario::nominate(Ball::green), f, oracle, RefParams{});
    CHECK(j.free_ball_used);
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::first_hit_not_on);
    CHECK(j.penalty == 4);
    CHECK(j.points == 0);
    CHECK(balls_count(j.post, Ball::green) == 1);
    CHECK(j.post.balls[std::size_t(at(j.post, Ball::green))].pos.x == table::spot(Ball::green).x);
    // docs/rules/README.md: a free ball nominated and not hit first is considered for a miss.
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
}

TEST_CASE("gap S3.8 with FREE BALL: the touching ball nominated and played away from is no foul") {
    FakeOracle oracle;
    const FrameState s = free_ball_touching_blue();
    const Judgement j = judge(s, scenario::nominate(Ball::blue), scenario::quiet_facts(s), oracle, RefParams{});
    CHECK(j.free_ball_used);
    CHECK(j.fouls.empty());
    CHECK(j.points == 0);
    CHECK(j.post.striker == 1);
    CHECK(j.post.pending == Pending::none);
    CHECK_FALSE(j.post.free_ball);
}

TEST_CASE("gap S3.8 with FREE BALL: another ball nominated, played away from the touching ball and nothing hit") {
    FakeOracle oracle;
    oracle.has_full_ball = false;
    const FrameState s = free_ball_touching_blue();
    const Judgement j = judge(s, scenario::nominate(Ball::green), scenario::quiet_facts(s), oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::no_hit);
    CHECK(j.penalty == 4);
    CHECK(j.miss);
    // The cue ball touched a ball that could be on (any, with FREE BALL): the run starts (S3.14(d)).
    CHECK(j.post.miss_run == 1);
}

TEST_CASE("gap S3.14(b): replace after a miss from a touching free-ball position calls both again") {
    FakeOracle oracle;
    const FrameState s = free_ball_touching_blue();
    FrameState after =
        judge(s, scenario::nominate(Ball::green), scenario::quiet_facts(s), oracle, RefParams{}).post;
    REQUIRE(listed(options(after, 1), DecisionKind::replace));
    std::vector<Call> calls;
    REQUIRE(chosen(after, DecisionKind::replace, 1, oracle, &calls));
    CHECK(same(static_cast<const Position&>(after), static_cast<const Position&>(s)));
    CHECK(after.striker == 0);
    CHECK(after.free_ball);
    CHECK(after.touching == std::vector<int>{at(s, Ball::blue)});
    CHECK(kinds(calls) == std::vector<CallKind>{CallKind::free_ball, CallKind::touching_ball});
    CHECK(calls[1].detail == "blue");
    CHECK(after.scores == std::array<int, 2>{0, 4});
}

TEST_CASE("gap S3.14(b), S3.5: replace after a miss from hand puts the cue ball back in hand where it was placed") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    s.cue_in_hand = true;
    s.balls[std::size_t(s.cue_ball)].pos = {-1.2, 0.1};
    std::string error;
    REQUIRE(place_cue_ball(s, {-1.15, -0.05}, oracle, error));
    const Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    REQUIRE(j.miss);
    CHECK_FALSE(j.post.cue_in_hand);
    FrameState back = j.post;
    REQUIRE(chosen(back, DecisionKind::replace, 1, oracle));
    CHECK(back.cue_in_hand);
    CHECK(back.balls[std::size_t(back.cue_ball)].pos.x == -1.15);
    CHECK(back.balls[std::size_t(back.cue_ball)].pos.y == -0.05);
    CHECK(back.striker == 0);
    CHECK(place_cue_ball(back, {-1.25, 0.12}, oracle, error));
    CHECK(back.balls[std::size_t(back.cue_ball)].pos.x == -1.25);
}

TEST_CASE("gap S3.10(h), S3.12(f): play_again after every kind of foul gives the offender the table as left") {
    struct Case {
        const char* name;
        ShotFacts facts;
    };
    const FrameState s = open_table(6);
    std::vector<Case> cases;
    cases.push_back({"no_hit", scenario::quiet_facts(s)});
    {
        ShotFacts f = hit(s, Ball::red, 0);
        in_off(f);
        cases.push_back({"in_off", f});
    }
    {
        ShotFacts f = hit(s, Ball::red, 0);
        pot(f, at(s, Ball::blue));
        cases.push_back({"pot_not_on", f});
    }
    {
        ShotFacts f = hit(s, Ball::red, 0);
        f.push = true;
        cases.push_back({"push_stroke", f});
    }
    cases.push_back({"first_hit_not_on", hit(s, Ball::pink)});
    {
        ShotFacts f = hit(s, Ball::red, 0);
        f.first_hit = {at(s, Ball::red, 0), at(s, Ball::black)};
        cases.push_back({"simultaneous_hit", f});
    }
    FakeOracle snookering;
    snookering.is_snookered = true;
    for (const Case& c : cases) {
        CAPTURE(c.name);
        const Judgement j = judge(s, {}, c.facts, snookering, RefParams{});
        REQUIRE(j.post.pending == Pending::after_foul);
        CHECK(foul_name(j.fouls.front().kind) == c.name);
        REQUIRE(j.post.free_ball);

        FrameState play = j.post;
        REQUIRE(chosen(play, DecisionKind::play, 1, snookering));
        CHECK(play.striker == 1);
        CHECK(play.free_ball);

        FrameState again = j.post;
        REQUIRE(chosen(again, DecisionKind::play_again, 1, snookering));
        CHECK(again.striker == 0);
        CHECK(again.pending == Pending::none);
        CHECK_FALSE(again.free_ball);
        CHECK(again.cue_in_hand == j.post.cue_in_hand);
        CHECK(again.balls.size() == j.post.balls.size());
        bool lie_as_left = again.balls.size() == j.post.balls.size();
        for (std::size_t i = 0; lie_as_left && i < again.balls.size(); ++i) {
            lie_as_left = again.balls[i].ball == j.post.balls[i].ball &&
                          again.balls[i].pos.x == j.post.balls[i].pos.x &&
                          again.balls[i].pos.y == j.post.balls[i].pos.y;
        }
        CHECK(lie_as_left);
        CHECK(again.scores == j.post.scores);
        CHECK(again.miss_run == 0);
        CHECK_FALSE(again.miss_warned);
        CHECK(again.last_penalty == 0);
        CHECK_FALSE(again.last_miss);
        CHECK(again.original.balls.empty());
        CHECK(options(again, 0) == std::vector<DecisionKind>{DecisionKind::concede_match});
    }
}

TEST_CASE("gap S3.12(f), S3.8(a): play_again drops a touching ball only the free ball made one that could be on") {
    FakeOracle oracle;
    FrameState s = free_ball_touching_blue();
    s.pending = Pending::after_foul;
    s.striker = 1;
    s.decider = 1;
    s.offender = 0;
    s.last_penalty = 4;

    FrameState play = s;
    REQUIRE(chosen(play, DecisionKind::play, 1, oracle));
    CHECK(play.free_ball);
    CHECK(play.touching == std::vector<int>{at(s, Ball::blue)});

    FrameState again = s;
    REQUIRE(chosen(again, DecisionKind::play_again, 1, oracle));
    CHECK_FALSE(again.free_ball);
    CHECK(again.touching.empty());
}

TEST_CASE("gap S3.4(b)(ii): play_second gives the re-spotted black to the other player, who places in the D") {
    FakeOracle oracle;
    FrameState s = black_only(40, 47);
    ShotFacts f = hit(s, Ball::black);
    pot(f, at(s, Ball::black));
    FrameState toss = judge(s, {}, f, oracle, RefParams{}).post;
    REQUIRE(toss.pending == Pending::toss);
    const int winner = toss.decider;
    std::string error;
    CHECK_FALSE(place_cue_ball(toss, {-1.2, 0.1}, oracle, error));

    REQUIRE(chosen(toss, DecisionKind::play_second, winner, oracle));
    CHECK(toss.striker == 1 - winner);
    CHECK(toss.offender == 1 - winner);
    CHECK(toss.pending == Pending::none);
    CHECK(toss.cue_in_hand);
    CHECK(toss.respotted_black);
    CHECK_FALSE(listed(options(toss, toss.striker), DecisionKind::claim_frame));
    CHECK(place_cue_ball(toss, {-1.2, 0.1}, oracle, error));
    CHECK(error.empty());
}

TEST_CASE("gap S3.17(b): the last grace stroke restarts the frame even when it is a foul") {
    FakeOracle oracle;
    FrameState s = after_safeties(open_table(6), 12);
    REQUIRE(s.pending == Pending::rerack);
    REQUIRE(chosen(s, DecisionKind::refuse_rerack, s.decider));
    s = after_safeties(s, 5);
    REQUIRE(s.rerack_deadline == 1);
    const Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    CHECK_FALSE(j.fouls.empty());
    CHECK(j.post.restart);
    CHECK_FALSE(j.post.over);
    CHECK(j.post.pending == Pending::none);
    CHECK(has_call(j.calls, CallKind::foul));
    CHECK(has_call(j.calls, CallKind::rerack));
    CHECK_FALSE(has_call(j.calls, CallKind::free_ball));
    CHECK(options(j.post, 0).empty());
    CHECK(options(j.post, 1).empty());
}

TEST_CASE("gap S3.17(a): a foul in the grace strokes and its choice do not stop the count") {
    FakeOracle oracle;
    FrameState s = after_safeties(open_table(6), 12);
    REQUIRE(chosen(s, DecisionKind::refuse_rerack, s.decider));
    s = after_safeties(s, 2);
    REQUIRE(s.rerack_deadline == 4);
    s = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{}).post;
    REQUIRE(s.pending == Pending::after_foul);
    CHECK(s.rerack_deadline == 3);
    REQUIRE(chosen(s, DecisionKind::play, s.decider));
    CHECK(s.rerack_deadline == 3);
    s = after_safeties(s, 2);
    CHECK_FALSE(s.restart);
    CHECK(s.rerack_deadline == 1);
    CHECK(safety(s).post.restart);
}

TEST_CASE("gap S4.2, docs/rules/README.md: nothing is conceded while a decision is pending") {
    FakeOracle oracle;
    std::vector<FrameState> pending;
    pending.push_back(judge(open_table(6), {}, scenario::quiet_facts(open_table(6)), oracle, RefParams{}).post);
    pending.push_back(after_safeties(open_table(6), 12));
    {
        const FrameState s = black_only(40, 47);
        ShotFacts f = hit(s, Ball::black);
        pot(f, at(s, Ball::black));
        pending.push_back(judge(s, {}, f, oracle, RefParams{}).post);
    }
    for (FrameState s : pending) {
        CAPTURE(pending_name(s.pending));
        REQUIRE(s.pending != Pending::none);
        s.concede_any_time = {true, true};
        s.scores = {0, 140};
        for (int player = 0; player < 2; ++player) {
            CAPTURE(player);
            CHECK_FALSE(listed(options(s, player), DecisionKind::concede_match));
            CHECK_FALSE(listed(options(s, player), DecisionKind::concede_frame));
            for (DecisionKind kind : {DecisionKind::concede_match, DecisionKind::concede_frame}) {
                FrameState tried = s;
                std::vector<Call> calls{{CallKind::foul, 4, "left over"}};
                std::string error;
                CHECK_FALSE(decide(tried, {kind, player}, oracle, RefParams{}, calls, error));
                CHECK_FALSE(error.empty());
                CHECK(calls.empty());
                CHECK(same(tried, s));
            }
        }
    }
}

TEST_CASE("gap S3.10(g): a push on a red and a colour hit at once lists both, the push first at the same value") {
    FakeOracle oracle;
    const FrameState s = open_table(6);
    ShotFacts f = hit(s, Ball::red, 0);
    f.first_hit = {at(s, Ball::red, 0), at(s, Ball::pink)};
    f.push = true;
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    REQUIRE(j.fouls.size() == 2);
    CHECK(j.fouls[0].kind == FoulKind::push_stroke);
    CHECK(j.fouls[0].value == 6);
    CHECK(j.fouls[0].ball == at(s, Ball::pink));
    CHECK(j.fouls[1].kind == FoulKind::simultaneous_hit);
    CHECK(j.fouls[1].value == 6);
    CHECK(j.penalty == 6);
    CHECK(j.calls.front().detail == "push_stroke");
}

TEST_CASE("gap S3.6, S3.11(c): two reds and a colour hit at once on red cost the colour") {
    FakeOracle oracle;
    const FrameState s = open_table(6);
    ShotFacts f = hit(s, Ball::red, 0);
    f.first_hit = {at(s, Ball::red, 0), at(s, Ball::red, 1), at(s, Ball::blue)};
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    REQUIRE(j.fouls.size() == 1);
    CHECK(j.fouls[0].kind == FoulKind::simultaneous_hit);
    CHECK(j.fouls[0].value == 5);
    CHECK(j.fouls[0].ball == at(s, Ball::blue));
    // A ball on was among those hit first: no miss is considered.
    CHECK_FALSE(j.miss);
}

TEST_CASE("gap S3.11(d)(iii), (vi): a colour of choice on, nothing declared, a red hit and potted") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    s.on = {OnKind::any_colour, Ball::yellow};
    ShotFacts f = hit(s, Ball::red, 0);
    pot(f, at(s, Ball::red, 0));
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK_FALSE(j.nominated.has_nomination);
    REQUIRE(j.fouls.size() == 3);
    CHECK(j.fouls[0].kind == FoulKind::reds_in_succession);
    CHECK(j.fouls[1].kind == FoulKind::before_nominating);
    CHECK(j.fouls[2].kind == FoulKind::pot_not_on);
    CHECK(j.fouls[2].value == 4);
    CHECK(j.penalty == 7);
    CHECK(j.points == 0);
    CHECK(balls_count(j.post, Ball::red) == 5);
    CHECK(j.post.on.kind == OnKind::red);
}

TEST_CASE("gap S3.11(d)(vi): a colour of choice on, nothing declared, two colours hit at once") {
    FakeOracle oracle;
    FrameState s = open_table(6);
    s.on = {OnKind::any_colour, Ball::yellow};
    ShotFacts f = hit(s, Ball::green);
    f.first_hit = {at(s, Ball::green), at(s, Ball::brown)};
    const Judgement j = judge(s, {}, f, oracle, RefParams{});
    CHECK_FALSE(j.nominated.has_nomination);
    REQUIRE(j.fouls.size() == 2);
    CHECK(j.fouls[0].kind == FoulKind::before_nominating);
    CHECK(j.fouls[0].value == 7);
    CHECK(j.fouls[1].kind == FoulKind::simultaneous_hit);
    CHECK(j.fouls[1].value == 4);
    CHECK(j.penalty == 7);
}

TEST_CASE("gap S3.14(d), S3.17: a miss at the twelfth quiet stroke keeps its run going, with no offer between") {
    FakeOracle oracle;
    // Twelve quiet strokes, then a miss: it is the miss's choice that is pending, not the offer.
    FrameState s = after_safeties(open_table(6), 11);
    REQUIRE(s.quiet_strokes == 11);
    const Judgement j = judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
    CHECK(j.miss);
    CHECK(j.post.pending == Pending::after_foul);
    CHECK(j.post.quiet_strokes == 12);
    FrameState replaced = j.post;
    REQUIRE(chosen(replaced, DecisionKind::replace, replaced.decider, oracle));
    CHECK(replaced.pending == Pending::none);
    CHECK(replaced.miss_run == 1);
    // The next quiet stroke, from the replaced position, is a further miss before any offer is made.
    const Judgement again = judge(replaced, {}, scenario::quiet_facts(replaced), oracle, RefParams{});
    CHECK(again.miss);
    CHECK(again.miss_reason == "S3.14(d)");
    CHECK(again.post.pending == Pending::after_foul);
    CHECK(again.post.miss_run == 2);
    CHECK_FALSE(has_call(again.calls, CallKind::rerack_offer));
}
