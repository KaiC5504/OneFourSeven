// WPBSA Section 4 Rule 2: conceding. The owner's ruling (docs/rules/README.md): a player given
// leave (FrameState::concede_any_time) may concede a frame at any time, the other only when a
// player requires penalty points. Nothing here turns on a stroke, so no shot is simulated.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ofs/rules/match.hpp"
#include "scenario.hpp"

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

std::string score(const FrameState& s) { return std::to_string(s.scores[0]) + "-" + std::to_string(s.scores[1]); }

std::string offered(const FrameState& s, int player) {
    std::string out;
    for (DecisionKind k : options(s, player)) {
        if (!out.empty()) out += ",";
        out += std::string(decision_name(k));
    }
    return out;
}

bool is_offered(const FrameState& s, int player, DecisionKind kind) {
    for (DecisionKind k : options(s, player)) {
        if (k == kind) return true;
    }
    return false;
}

std::string frame_over_call(const std::vector<Call>& calls) {
    for (const Call& c : calls) {
        if (c.kind == CallKind::frame_over) return std::to_string(c.value) + " " + c.detail;
    }
    return "";
}

bool decide_now(FrameState& s, DecisionKind kind, int player, std::vector<Call>* calls = nullptr,
                std::string* error = nullptr) {
    std::vector<Call> local_calls;
    std::string local_error;
    return decide(s, {kind, player}, *rig().oracle, rig().ref, calls != nullptr ? *calls : local_calls,
                  error != nullptr ? *error : local_error);
}

// Three reds and the colours: 51 points on the table, player 0 to play at a red.
FrameState three_reds(int first, int second) {
    FrameState s = frame({ball(Ball::cue, 0.0, 0.3), ball(Ball::red, 0.3, 0.3), ball(Ball::red, -0.3, 0.3, 1),
                          ball(Ball::red, 0.6, -0.4, 2), spotted(Ball::yellow), spotted(Ball::green), spotted(Ball::brown),
                          spotted(Ball::blue), spotted(Ball::pink), spotted(Ball::black)});
    s.scores = {first, second};
    return s;
}

MatchConfig config(int best_of, bool first_may, bool second_may) {
    MatchConfig c;
    c.best_of = best_of;
    c.first_breaker = 0;
    c.seed = 31;
    c.concede_any_time = {first_may, second_may};
    return c;
}

bool match_decide_now(MatchState& m, DecisionKind kind, int player, MatchEvent& event) {
    std::vector<Call> calls;
    std::string error;
    return match_decide(m, {kind, player}, *rig().oracle, rig().ref, rig().params, calls, event, error);
}

}  // namespace

TEST_CASE("S4.2(b): a frame may not be conceded while nobody requires penalty points") {
    FrameState s = three_reds(0, 51);
    CHECK(points_remaining(s) == 51);
    CHECK(penalty_points_required(s, 0) == 0);
    CHECK(offered(s, 0) == "concede_match");
    CHECK(offered(s, 1) == "concede_match");

    std::string error;
    CHECK_FALSE(decide_now(s, DecisionKind::concede_frame, 0, nullptr, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(s.over);
    CHECK(s.winner == -1);
    CHECK_FALSE(decide_now(s, DecisionKind::concede_frame, 1));
    CHECK_FALSE(s.over);
}

TEST_CASE("S4.2(b): once a player requires penalty points the frame may be conceded") {
    FrameState s = three_reds(0, 52);
    CHECK(points_remaining(s) == 51);
    CHECK(penalty_points_required(s, 0) == 1);
    CHECK(offered(s, 0) == "concede_frame,concede_match");
    // The rule turns on any player needing them, so the non-striker is free to concede as well.
    CHECK(offered(s, 1) == "concede_frame,concede_match");

    std::vector<Call> calls;
    CHECK(decide_now(s, DecisionKind::concede_frame, 0, &calls));
    CHECK(s.over);
    CHECK(s.winner == 1);
    CHECK(s.end_reason == EndReason::conceded);
    CHECK(score(s) == "0-52");
    CHECK(frame_over_call(calls) == "1 conceded");
}

TEST_CASE("S4.2(b), the owner's ruling: a player given leave may concede at any time, the other only then") {
    FrameState s = three_reds(0, 0);
    s.concede_any_time = {true, false};
    CHECK(offered(s, 0) == "concede_frame,concede_match");
    CHECK(offered(s, 1) == "concede_match");
    CHECK_FALSE(decide_now(s, DecisionKind::concede_frame, 1));
    CHECK_FALSE(s.over);

    // Ahead, and not at the table: still theirs to give away.
    s.scores = {60, 10};
    s.striker = 1;
    std::vector<Call> calls;
    CHECK(decide_now(s, DecisionKind::concede_frame, 0, &calls));
    CHECK(s.over);
    CHECK(s.winner == 1);
    CHECK(s.end_reason == EndReason::conceded);
    CHECK(score(s) == "60-10");
    CHECK(frame_over_call(calls) == "1 conceded");
}

TEST_CASE("S4.2(a), (e): a concession stands at once and cannot be taken back") {
    FrameState s = three_reds(0, 60);
    CHECK(decide_now(s, DecisionKind::concede_frame, 0));
    CHECK(s.over);
    CHECK(s.winner == 1);

    // Nothing is left for either player to choose, the conceder least of all.
    CHECK(offered(s, 0) == "");
    CHECK(offered(s, 1) == "");
    CHECK_FALSE(decide_now(s, DecisionKind::play, 0));
    CHECK_FALSE(decide_now(s, DecisionKind::concede_frame, 1));
    CHECK(s.over);
    CHECK(s.winner == 1);
    CHECK(s.end_reason == EndReason::conceded);
}

TEST_CASE("S4.2: a frame is not conceded while the referee waits on a choice after a foul") {
    FrameState s = three_reds(0, 60);
    s.concede_any_time = {true, true};
    // Player 0 misses everything.
    const Judgement j = judge(s, {}, quiet_facts(s), *rig().oracle, rig().ref);
    REQUIRE(j.post.pending == Pending::after_foul);
    CHECK_FALSE(is_offered(j.post, 0, DecisionKind::concede_frame));
    CHECK_FALSE(is_offered(j.post, 1, DecisionKind::concede_frame));
    CHECK(is_offered(j.post, 1, DecisionKind::play));

    FrameState next = j.post;
    CHECK(decide_now(next, DecisionKind::play, 1));
    CHECK(is_offered(next, 0, DecisionKind::concede_frame));
    CHECK(is_offered(next, 1, DecisionKind::concede_frame));
}

TEST_CASE("S4.2, S3.1(f)(ii): a conceded frame counts for the opponent in the match") {
    MatchState m = start_match(config(3, true, true), rig().params);
    REQUIRE(m.frame.balls.size() == 22);
    CHECK(m.frame.concede_any_time[0]);
    CHECK(m.frame.concede_any_time[1]);

    MatchEvent event = MatchEvent::none;
    CHECK(match_decide_now(m, DecisionKind::concede_frame, 0, event));
    CHECK(event == MatchEvent::frame_ended);
    CHECK(m.frames[0] == 0);
    CHECK(m.frames[1] == 1);
    CHECK_FALSE(m.over);
    REQUIRE(m.history.size() == 1);
    CHECK(m.history[0].winner == 1);
    CHECK(m.history[0].reason == EndReason::conceded);
}

TEST_CASE("S4.2(b): in a match the frame's leave to concede is each player's own") {
    MatchState m = start_match(config(3, true, false), rig().params);
    REQUIRE(m.frame.balls.size() == 22);
    CHECK(is_offered(m.frame, 0, DecisionKind::concede_frame));
    CHECK_FALSE(is_offered(m.frame, 1, DecisionKind::concede_frame));

    MatchEvent event = MatchEvent::none;
    CHECK_FALSE(match_decide_now(m, DecisionKind::concede_frame, 1, event));
    CHECK(event == MatchEvent::none);
    CHECK(m.frames[0] == 0);
    CHECK(m.frames[1] == 0);
    CHECK_FALSE(m.frame.over);
}

TEST_CASE("S4.2: the match may be conceded at any time, whatever the frames stand at") {
    MatchState m = start_match(config(5, false, false), rig().params);
    REQUIRE(m.frame.balls.size() == 22);
    CHECK(is_offered(m.frame, 0, DecisionKind::concede_match));
    CHECK(is_offered(m.frame, 1, DecisionKind::concede_match));

    MatchEvent event = MatchEvent::none;
    CHECK(match_decide_now(m, DecisionKind::concede_match, 1, event));
    CHECK(event == MatchEvent::match_ended);
    CHECK(m.over);
    CHECK(m.winner == 0);
    CHECK(m.frame.over);
    CHECK(m.frame.winner == 0);
    CHECK(m.frame.end_reason == EndReason::match_conceded);
    CHECK_FALSE(next_frame(m, rig().params));
}

TEST_CASE("S4.2: a player a frame up who concedes the match still loses it") {
    MatchState m = start_match(config(5, true, true), rig().params);
    REQUIRE(m.frame.balls.size() == 22);
    MatchEvent event = MatchEvent::none;
    CHECK(match_decide_now(m, DecisionKind::concede_frame, 1, event));
    CHECK(m.frames[0] == 1);
    CHECK(next_frame(m, rig().params));

    CHECK(match_decide_now(m, DecisionKind::concede_match, 0, event));
    CHECK(event == MatchEvent::match_ended);
    CHECK(m.over);
    CHECK(m.winner == 1);
}
