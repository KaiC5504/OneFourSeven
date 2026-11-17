#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/standin.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::BallOnTable;
using rules::DecisionKind;
using rules::FrameState;
using rules::Pending;
using rules::scenario::ball;
using rules::scenario::spotted;

namespace {

std::vector<const Level*> both() { return {&rig::levels().beginner, &rig::levels().maximum}; }

bool offered(const std::vector<DecisionKind>& options, DecisionKind kind) {
    return std::find(options.begin(), options.end(), kind) != options.end();
}

DecisionKind choose(const FrameState& s, const Level& level, nlohmann::json* why = nullptr) {
    Rng rng(4);
    return choose_option(s, s.decider, rules::options(s, s.decider), level, rig::env(), rng, why);
}

DecisionKind standin(const FrameState& s) {
    Rng rng(4);
    return choose_decision(s, s.decider, rules::options(s, s.decider), rig::levels().standin, rng);
}

// Player 1 at the table with `reds` reds: one a short cut into the top-right corner from where
// the cue ball lies, the rest in a line across the baulk end.
FrameState table_with(int reds) {
    std::vector<BallOnTable> balls{ball(Ball::cue, 1.0, -0.3), ball(Ball::red, 1.4, -0.6, 0)};
    for (int i = 1; i < reds; ++i) balls.push_back(ball(Ball::red, -1.5, -0.7 + 0.1 * i, i));
    for (const Ball c : {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        balls.push_back(spotted(c));
    }
    FrameState s = rules::scenario::frame(balls);
    s.striker = 1;
    return s;
}

// Player 1 rolls the cue ball a hand's width towards the middle of the table and touches nothing.
FrameState after_a_miss(int reds) {
    const FrameState s = table_with(reds);
    const rules::Judgement j =
        rules::scenario::play(s, rules::scenario::strike(rules::scenario::aim_at(s, {0.0, 0.3}), 0.2)).judgement;
    REQUIRE(j.post.pending == Pending::after_foul);
    REQUIRE(j.post.decider == 0);
    return j.post;
}

// What player 0 would have to play after choosing to: the pots there, of `only` when given.
int pots_left(const FrameState& pending, const Level& level, const Ball* only = nullptr) {
    FrameState s = pending;
    std::vector<rules::Call> calls;
    std::string error;
    REQUIRE(rules::decide(s, {DecisionKind::play, pending.decider}, rig::env().oracle, rig::env().ref, calls, error));
    int count = 0;
    for (const Pot& pot : pots(s, rig::env(), level.max_cut_rad)) {
        if (only == nullptr || s.balls[std::size_t(pot.target)].ball == *only) ++count;
    }
    return count;
}

void check_one_of_the_options(const FrameState& s) {
    const std::vector<DecisionKind> open = rules::options(s, s.decider);
    REQUIRE_FALSE(open.empty());
    for (const Level* level : both()) {
        CAPTURE(level->id);
        nlohmann::json why;
        const DecisionKind kind = choose(s, *level, &why);
        CHECK(offered(open, kind));
        REQUIRE(why.is_object());
        CHECK(why.value("level", std::string()) == level->id);
        CHECK(why.contains("rule"));
        CHECK(why.contains("options"));
        CHECK(choose(s, *level) == kind);
    }
}

}  // namespace

TEST_CASE("after a foul, at a re-rack offer and at the toss each level takes one of the options") {
    check_one_of_the_options(after_a_miss(15));
    check_one_of_the_options(after_a_miss(9));

    FrameState offer = table_with(15);
    offer.pending = Pending::rerack;
    offer.decider = 1;
    REQUIRE(offered(rules::options(offer, 1), DecisionKind::accept_rerack));
    check_one_of_the_options(offer);

    FrameState tied = rules::scenario::frame({ball(Ball::cue, 0.0, 0.3), spotted(Ball::black)});
    tied.scores = {30, 30};
    tied.respotted_black = true;
    tied.pending = Pending::toss;
    tied.decider = 1;
    REQUIRE(offered(rules::options(tied, 1), DecisionKind::play_first));
    check_one_of_the_options(tied);
}

TEST_CASE("Beginner chooses as the stand-in does") {
    for (const int reds : {15, 9}) {
        const FrameState s = after_a_miss(reds);
        CHECK(choose(s, rig::levels().beginner) == standin(s));
    }
}

TEST_CASE("after the other player's foul the 147 plays when the table has the ball its maximum needs") {
    const Level& level = rig::levels().maximum;
    const FrameState s = after_a_miss(15);
    REQUIRE(offered(rules::options(s, 0), DecisionKind::replace));
    const Ball red = Ball::red;
    REQUIRE(pots_left(s, level, &red) > 0);
    nlohmann::json why;
    CHECK(choose(s, level, &why) == DecisionKind::play);
    CHECK(why.value("rule", std::string()) == "play");
    CHECK(why.value("pot", false));
}

TEST_CASE("with the maximum gone the 147 plays after a foul whenever any pot is there") {
    const Level& level = rig::levels().maximum;
    const FrameState s = after_a_miss(9);
    REQUIRE(pots_left(s, level) > 0);
    CHECK(choose(s, level) == DecisionKind::play);
}

TEST_CASE("with no pot left to it after a foul the 147 chooses as the stand-in does") {
    const Level& level = rig::levels().maximum;
    // Player 1 breaks off short of the pack: every red is still in it, and none goes anywhere.
    FrameState s = rig::racked();
    s.striker = 1;
    std::string error;
    REQUIRE(rules::place_cue_ball(s, s.balls[std::size_t(s.cue_ball)].pos, rig::env().oracle, error));
    const rules::Judgement j =
        rules::scenario::play(s, rules::scenario::strike(rules::scenario::aim_at(s, {0.0, 0.4}), 0.3)).judgement;
    REQUIRE(j.post.pending == Pending::after_foul);
    REQUIRE(pots_left(j.post, level) == 0);
    nlohmann::json why;
    CHECK(choose(j.post, level, &why) == standin(j.post));
    CHECK(why.value("level", std::string()) == "maximum");
}
