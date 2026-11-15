#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "test_standin_support.hpp"

using namespace ofs;
using namespace ofs::opponent;
using namespace ofs::opponent::standin_test;
using rules::DecisionKind;
using rules::Pending;
using rules::scenario::ball;
using rules::scenario::spotted;

namespace {

// The only red hidden from the cue ball behind the blue: no pot and no straight way to it.
FrameState hidden_red() {
    std::vector<BallOnTable> balls{ball(Ball::cue, 0.3, 0.3), ball(Ball::red, 1.3, 0.3), ball(Ball::blue, 0.8, 0.3)};
    for (Ball colour : kColours) {
        if (colour != Ball::blue) balls.push_back(spotted(colour));
    }
    return rules::scenario::frame(std::move(balls));
}

// The same table with the blue on its spot: the red in plain view.
FrameState open_red() {
    FrameState s = hidden_red();
    s.balls[2].pos = table::spot(Ball::blue);
    return s;
}

// `left` as player 1's foul left it for player 0 to decide on, played from `original`.
FrameState after_foul(FrameState left, const FrameState& original, bool miss) {
    left.pending = Pending::after_foul;
    left.decider = 0;
    left.striker = 0;
    left.offender = 1;
    left.last_penalty = 4;
    left.last_miss = miss;
    left.original = original;
    left.scores = {4, 0};
    return left;
}

DecisionKind chosen(const FrameState& s, int player = 0) {
    Rng rng(1);
    const std::vector<DecisionKind> open = rules::options(s, player);
    REQUIRE_FALSE(open.empty());
    const DecisionKind kind = choose_decision(s, player, open, standin_params(), rng);
    bool offered = false;
    for (DecisionKind k : open) offered = offered || k == kind;
    CHECK(offered);
    return kind;
}

}  // namespace

TEST_CASE("after a miss from a snooker the balls go back") {
    const FrameState s = after_foul(open_red(), hidden_red(), true);
    CHECK(chosen(s) == DecisionKind::replace);
}

TEST_CASE("after a miss with the ball on in plain view it plays from where the balls lie") {
    const FrameState s = after_foul(open_red(), open_red(), true);
    CHECK(chosen(s) == DecisionKind::play);
}

TEST_CASE("a miss by a player who was in hand is not put back") {
    FrameState original = hidden_red();
    original.cue_in_hand = true;
    CHECK(chosen(after_foul(open_red(), original, true)) == DecisionKind::play);
}

TEST_CASE("a miss by a player who had a free ball is not put back") {
    FrameState original = hidden_red();
    original.free_ball = true;
    CHECK(chosen(after_foul(open_red(), original, true)) == DecisionKind::play);
}

TEST_CASE("left with neither a pot nor a way to a ball on, the offender plays again") {
    const FrameState s = after_foul(hidden_red(), open_red(), false);
    CHECK(chosen(s) == DecisionKind::play_again);
}

TEST_CASE("left with a way to a ball on, or a free ball, or the cue ball in hand, it plays") {
    CHECK(chosen(after_foul(open_red(), open_red(), false)) == DecisionKind::play);

    FrameState free_ball = after_foul(hidden_red(), open_red(), false);
    free_ball.free_ball = true;
    CHECK(chosen(free_ball) == DecisionKind::play);

    FrameState in_hand_now = after_foul(in_hand(hidden_red()), open_red(), false);
    CHECK(chosen(in_hand_now) == DecisionKind::play);
}

TEST_CASE("a foul and a miss made through the engine is sent back") {
    // Player 0, snookered, plays straight into the blue.
    const FrameState pre = hidden_red();
    const auto played = rules::scenario::play(pre, rules::scenario::strike(0.0, 1.0));
    REQUIRE(played.judgement.miss);
    const FrameState& post = played.judgement.post;
    REQUIRE(post.pending == Pending::after_foul);
    REQUIRE(post.decider == 1);
    CHECK(chosen(post, 1) == DecisionKind::replace);
}

TEST_CASE("a re-rack is accepted with the scores within twenty and refused otherwise") {
    FrameState s = open_red();
    s.pending = Pending::rerack;
    s.decider = 0;
    struct Case {
        int a;
        int b;
        bool accepts;
    };
    for (const Case& c : {Case{30, 30, true}, Case{30, 50, true}, Case{50, 30, true}, Case{30, 51, false},
                          Case{72, 30, false}}) {
        CAPTURE(c.a);
        CAPTURE(c.b);
        s.scores = {c.a, c.b};
        CHECK(chosen(s) == (c.accepts ? DecisionKind::accept_rerack : DecisionKind::refuse_rerack));
    }
}

TEST_CASE("the toss winner plays the re-spotted black first") {
    FrameState s = rules::scenario::frame({ball(Ball::cue, -1.2, 0.1), spotted(Ball::black)});
    s.respotted_black = true;
    s.cue_in_hand = true;
    s.pending = Pending::toss;
    s.decider = 1;
    s.striker = 1;
    s.scores = {50, 50};
    CHECK(chosen(s, 1) == DecisionKind::play_first);
}

TEST_CASE("anything else offered gets the first option") {
    const FrameState s = open_red();
    Rng rng(1);
    const StandinParams sp = standin_params();
    CHECK(choose_decision(s, 0, {DecisionKind::claim_frame, DecisionKind::concede_match}, sp, rng) ==
          DecisionKind::claim_frame);
    CHECK(choose_decision(s, 0, {DecisionKind::play_second}, sp, rng) == DecisionKind::play_second);
    // Only what is offered is ever chosen.
    CHECK(choose_decision(s, 0, {DecisionKind::play_again, DecisionKind::replace}, sp, rng) ==
          DecisionKind::play_again);
    CHECK(choose_decision(s, 0, {DecisionKind::refuse_rerack}, sp, rng) == DecisionKind::refuse_rerack);
}

TEST_CASE("why says what each choice weighed") {
    struct Case {
        FrameState s;
        DecisionKind kind;
        const char* rule;
        std::vector<const char*> looked_at;
    };
    FrameState rerack = open_red();
    rerack.pending = Pending::rerack;
    rerack.decider = 0;
    rerack.scores = {30, 72};
    const Case cases[] = {
        {after_foul(open_red(), hidden_red(), true), DecisionKind::replace, "replace", {"clear_path_before"}},
        {after_foul(hidden_red(), open_red(), false), DecisionKind::play_again, "play_again", {"pot", "clear_path"}},
        {after_foul(open_red(), open_red(), true), DecisionKind::play, "play", {"clear_path_before", "pot"}},
        {rerack, DecisionKind::refuse_rerack, "refuse_rerack", {"score_gap", "rerack_within"}},
    };
    for (const Case& c : cases) {
        CAPTURE(c.rule);
        Rng rng(1);
        nlohmann::json why;
        const std::vector<DecisionKind> open = rules::options(c.s, 0);
        CHECK(choose_decision(c.s, 0, open, standin_params(), rng, &why) == c.kind);
        CHECK(why.value("rule", "") == c.rule);
        CHECK(why["options"].size() == open.size());
        CHECK(why["scores"] == nlohmann::json::array({c.s.scores[0], c.s.scores[1]}));
        CHECK(why["points_remaining"] == rules::points_remaining(c.s));
        for (const char* key : c.looked_at) CHECK_MESSAGE(why.contains(key), key);
    }

    Rng rng(1);
    nlohmann::json why;
    choose_decision(rerack, 0, rules::options(rerack, 0), standin_params(), rng, &why);
    CHECK(why.value("score_gap", 0) == 42);

    CHECK(choose_decision(open_red(), 0, {DecisionKind::play_second}, standin_params(), rng, &why) ==
          DecisionKind::play_second);
    CHECK(why.value("rule", "") == "first_option");
}

TEST_CASE("the same choice whatever the Rng holds") {
    const FrameState s = after_foul(open_red(), hidden_red(), true);
    const std::vector<DecisionKind> open = rules::options(s, 0);
    Rng a(1);
    Rng b(99);
    CHECK(choose_decision(s, 0, open, standin_params(), a) == choose_decision(s, 0, open, standin_params(), b));
}
