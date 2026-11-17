// Decisions besides strokes (spec 10.4, docs/opponent/README.md "Choices"): after a foul, after a
// foul and a miss, at a re-rack offer and at the toss for a re-spotted black, each level answers
// with one of the options open; the 147 plays on itself whenever a pot it wants is there.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using nlohmann::json;
using rules::Ball;
using rules::BallOnTable;
using rules::DecisionKind;
using rules::FrameState;
using rules::Pending;
using rules::scenario::ball;
using rules::scenario::spotted;

namespace {

std::vector<const Level*> both() { return {&rig::levels().beginner, &rig::levels().maximum}; }

std::string text(const json& j, const char* key) {
    if (!j.is_object()) return "";
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

bool offered(const std::vector<DecisionKind>& options, DecisionKind k) {
    return std::find(options.begin(), options.end(), k) != options.end();
}

DecisionKind choose(const FrameState& s, const Level& level, std::uint64_t seed, json* why = nullptr) {
    Rng rng(seed);
    return choose_option(s, s.decider, rules::options(s, s.decider), level, rig::env(), rng, why);
}

constexpr rules::Vec2 kCue{0.8084, 0.206};
constexpr rules::Vec2 kRed{1.3, 0.55};

// `reds` reds, one dead straight into the top-left corner from the cue ball and the rest out of
// the way along y = -0.6; the colours on their spots. Player 1 is at the table.
FrameState open_table(int reds) {
    std::vector<BallOnTable> balls{ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, kRed.x, kRed.y, 0)};
    for (int i = 1; i < reds; ++i) balls.push_back(ball(Ball::red, -0.65 + 0.1 * (i - 1), -0.6, i));
    for (Ball c : {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        balls.push_back(spotted(c));
    }
    FrameState s = rules::scenario::frame(balls);
    s.striker = 1;
    return s;
}

// Player 1 nudges the cue ball straight back from the red and hits nothing: a foul and a miss,
// the straight pot still there for player 0.
rules::Judgement no_hit(const FrameState& s) {
    const double away = std::atan2(kCue.y - kRed.y, kCue.x - kRed.x);
    return rules::scenario::play(s, rules::scenario::strike(away, 0.15)).judgement;
}

// The cue ball goes in off a red into the left middle pocket, player 1 to blame (the rules
// tests' in-off position): a foul with no miss.
rules::Judgement in_off() {
    FrameState s = rules::scenario::frame({ball(Ball::cue, -0.0869, 0.5170), ball(Ball::red, 0.10, 0.80),
                                           ball(Ball::red, 0.6, -0.4, 1), spotted(Ball::black)});
    s.striker = 1;
    return rules::scenario::play(s, rules::scenario::strike(rules::scenario::aim_at(s, {0.0500, 0.7839}), 2.0))
        .judgement;
}

// Player 0 pots the last black to tie the frame: the black is re-spotted and the lots drawn.
FrameState toss() {
    FrameState s = rules::scenario::frame({ball(Ball::cue, 0.0, 0.3), ball(Ball::black, 0.0, 0.6)});
    s.scores = {28, 35};
    s.seed = 12;
    s.strokes = 60;
    rules::ShotFacts f = rules::scenario::quiet_facts(s);
    f.first_hit = {1};
    f.first_hit_t = 0.2;
    f.closest_gap_m[1] = 0.0;
    f.pots = {{1, 2, 0.5}};
    f.cue_travel_m = 0.4;
    f.object_travel_m = 0.4;
    f.pot_attempt = true;
    return rules::judge(s, {}, f, rig::env().oracle, rig::env().ref).post;
}

FrameState rerack_offer() {
    FrameState s = open_table(15);
    s.striker = 0;
    s.pending = Pending::rerack;
    s.decider = 1;
    return s;
}

void check_answers(const FrameState& s) {
    const std::vector<DecisionKind> open = rules::options(s, s.decider);
    REQUIRE_FALSE(open.empty());
    for (const Level* level : both()) {
        for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2), std::uint64_t(3)}) {
            CAPTURE(level->id);
            CAPTURE(seed);
            json why;
            const DecisionKind k = choose(s, *level, seed, &why);
            CHECK(offered(open, k));
            REQUIRE(why.is_object());
            CHECK(text(why, "level") == level->id);
            CHECK(choose(s, *level, seed) == k);
        }
    }
}

// After player 0 chooses to play: a pot of `b` is there for them.
bool pot_left_for_player_0(const FrameState& pending, Ball b) {
    FrameState s = pending;
    std::vector<rules::Call> calls;
    std::string error;
    if (!rules::decide(s, {DecisionKind::play, 0}, rig::env().oracle, rig::env().ref, calls, error)) return false;
    if (s.striker != 0 || s.pending != Pending::none) return false;
    for (const Pot& p : pots(s, rig::env(), rig::levels().maximum.max_cut_rad)) {
        if (s.balls[std::size_t(p.target)].ball == b) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("spec 10.4: after a foul each level chooses one of the options open, and says its level") {
    const rules::Judgement j = in_off();
    REQUIRE(j.post.pending == Pending::after_foul);
    REQUIRE(rules::scenario::has_foul(j, rules::FoulKind::in_off));
    REQUIRE_FALSE(j.post.last_miss);
    REQUIRE(j.post.decider == 0);
    check_answers(j.post);
}

TEST_CASE("spec 10.4: after a foul and a miss each level chooses one of the options open, replace among them") {
    const rules::Judgement j = no_hit(open_table(15));
    REQUIRE(j.post.pending == Pending::after_foul);
    REQUIRE(rules::scenario::has_foul(j, rules::FoulKind::no_hit));
    REQUIRE(j.post.last_miss);
    REQUIRE(j.post.decider == 0);
    REQUIRE(offered(rules::options(j.post, 0), DecisionKind::replace));
    check_answers(j.post);
}

TEST_CASE("spec 10.4: at a re-rack offer each level accepts or refuses") {
    const FrameState s = rerack_offer();
    const std::vector<DecisionKind> open = rules::options(s, s.decider);
    REQUIRE(offered(open, DecisionKind::accept_rerack));
    REQUIRE(offered(open, DecisionKind::refuse_rerack));
    check_answers(s);
}

TEST_CASE("spec 10.4: having won the lots for a re-spotted black, each level chooses who plays first") {
    const FrameState s = toss();
    REQUIRE(s.pending == Pending::toss);
    REQUIRE(s.respotted_black);
    const std::vector<DecisionKind> open = rules::options(s, s.decider);
    REQUIRE(offered(open, DecisionKind::play_first));
    REQUIRE(offered(open, DecisionKind::play_second));
    check_answers(s);
}

TEST_CASE("README, choices: each level's why is the stand-in's form with its level added") {
    const rules::Judgement j = no_hit(open_table(15));
    REQUIRE(j.post.pending == Pending::after_foul);
    for (const Level* level : both()) {
        CAPTURE(level->id);
        json why;
        choose(j.post, *level, 1, &why);
        REQUIRE(why.is_object());
        CHECK(text(why, "level") == level->id);
        CHECK(why.contains("rule"));
        CHECK(why.contains("options"));
    }
}

TEST_CASE("README, choices: the other player's foul leaves the 147 a red with all fifteen on the table, so it plays") {
    const rules::Judgement j = no_hit(open_table(15));
    REQUIRE(j.post.pending == Pending::after_foul);
    REQUIRE(j.post.decider == 0);
    REQUIRE(j.post.offender == 1);
    REQUIRE(pot_left_for_player_0(j.post, Ball::red));
    for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2), std::uint64_t(3)}) {
        CAPTURE(seed);
        CHECK(choose(j.post, rig::levels().maximum, seed) == DecisionKind::play);
    }
}

TEST_CASE("README, choices: after an in-off with the maximum gone the 147 plays on from hand when it has a pot") {
    const rules::Judgement j = in_off();
    REQUIRE(j.post.pending == Pending::after_foul);
    REQUIRE(j.post.decider == 0);
    REQUIRE(pot_left_for_player_0(j.post, Ball::red));
    for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2), std::uint64_t(3)}) {
        CAPTURE(seed);
        CHECK(choose(j.post, rig::levels().maximum, seed) == DecisionKind::play);
    }
}

TEST_CASE("README, choices: the maximum gone, after the other player's foul the 147 plays on when any pot is there") {
    const rules::Judgement j = no_hit(open_table(14));
    REQUIRE(j.post.pending == Pending::after_foul);
    REQUIRE(j.post.decider == 0);
    REQUIRE(pot_left_for_player_0(j.post, Ball::red));
    FrameState played = j.post;
    std::vector<rules::Call> calls;
    std::string error;
    REQUIRE(rules::decide(played, {DecisionKind::play, 0}, rig::env().oracle, rig::env().ref, calls, error));
    REQUIRE_FALSE(maximum_on(played));
    for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2), std::uint64_t(3)}) {
        CAPTURE(seed);
        CHECK(choose(j.post, rig::levels().maximum, seed) == DecisionKind::play);
    }
}
