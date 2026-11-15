// S3.17 (stalemate), one scenario per paragraph, written from the rulebook and
// docs/rules/README.md, never from the engine. The rule's whole input is how little each stroke
// changed, so the strokes are hand-made facts: a safety that finds the ball on and leaves it a few
// centimetres from where it lay.

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "ofs/rules/match.hpp"
#include "scenario.hpp"

using namespace ofs;
using namespace ofs::rules;
using scenario::ball;
using scenario::place_of;
using scenario::spotted;

namespace {

constexpr double kLine = 0.5;

const TableOracle& oracle() { return *scenario::rig().oracle; }
const RefParams& ref() { return scenario::rig().ref; }
const phys::PhysicsParams& params() { return scenario::rig().params; }

std::string name(Pending p) { return std::string(pending_name(p)); }
std::string name(OnKind k) { return std::string(on_kind_name(k)); }
std::string name(MatchEvent e) { return std::string(match_event_name(e)); }

std::string calls_text(const std::vector<Call>& calls) {
    std::string out;
    for (const Call& c : calls) {
        if (!out.empty()) out += " ";
        out += std::string(call_name(c.kind));
    }
    return out;
}

// What `player` is offered in answer to the pending question. Conceding and claiming, which other
// clauses govern, are left out.
std::string choices_text(const FrameState& s, int player) {
    std::string out;
    for (DecisionKind k : options(s, player)) {
        if (k == DecisionKind::claim_frame || k == DecisionKind::concede_frame || k == DecisionKind::concede_match) {
            continue;
        }
        if (!out.empty()) out += " ";
        out += std::string(decision_name(k));
    }
    return out;
}

bool lies_at(const Position& p, Ball b, Vec2 pos) {
    const int place = place_of(p, b);
    return place >= 0 && p.balls[std::size_t(place)].pos.x == pos.x && p.balls[std::size_t(place)].pos.y == pos.y;
}

// The ball the exchange is played off: a red while one is listed, else the colour on.
int ball_on(const FrameState& s) {
    const int red = place_of(s, Ball::red);
    return red >= 0 ? red : place_of(s, s.on.colour);
}

// Facts for a safety: the cue ball finds the ball on, which ends `dx` metres along the table from
// where it lay; nothing else moves and nothing is potted.
ShotFacts safety(const FrameState& s, double dx) {
    ShotFacts f = scenario::quiet_facts(s);
    const std::size_t on = std::size_t(ball_on(s));
    f.first_hit = {int(on)};
    f.first_hit_t = 1.0;
    f.closest_gap_m[on] = 0.0;
    f.rest[on].x += dx;
    f.object_travel_m = std::abs(dx);
    f.cue_travel_m = 1.0;
    return f;
}

// Whether a stroke can be played from `s`. When it cannot, a check fails and the scenario stops.
bool playable(const FrameState& s) {
    const bool ok = !s.over && !s.restart && s.pending == Pending::none && s.cue_ball >= 0 &&
                    std::size_t(s.cue_ball) < s.balls.size() && ball_on(s) >= 0;
    CHECK_MESSAGE(ok, "the frame is not waiting for a stroke");
    return ok;
}

// `n` quiet safeties, the players in turn, the ball on nudged 10 cm one way and then back. False
// when the frame stops taking strokes before the last of them. `last` receives the last judgement.
bool exchange(FrameState& s, int n, const RefParams& r, Judgement* last = nullptr) {
    for (int i = 0; i < n; ++i) {
        if (!playable(s)) return false;
        Judgement j = judge(s, {}, safety(s, i % 2 == 0 ? 0.1 : -0.1), oracle(), r);
        s = j.post;
        if (last != nullptr) *last = std::move(j);
    }
    return true;
}

// The decider's answer; `said` receives what the referee then announces. False, after failing a
// check, when the engine refuses it.
bool choose(FrameState& s, DecisionKind kind, std::vector<Call>* said = nullptr) {
    std::vector<Call> calls;
    std::string error;
    const bool ok = decide(s, {kind, s.decider}, oracle(), ref(), calls, error);
    CHECK_MESSAGE(ok, std::string(decision_name(kind)), " refused: ", error);
    if (said != nullptr) *said = calls;
    return ok;
}

// The same through the match, with the referee values `r`.
bool match_choose(MatchState& m, DecisionKind kind, int player, const RefParams& r, MatchEvent& event,
                  std::vector<Call>* said = nullptr) {
    std::vector<Call> calls;
    std::string error;
    const bool ok = match_decide(m, {kind, player}, oracle(), r, params(), calls, event, error);
    CHECK_MESSAGE(ok, std::string(decision_name(kind)), " refused: ", error);
    if (said != nullptr) *said = calls;
    return ok;
}

// A red and the black with the cue ball a metre from the red: nothing to stop the players
// trading safeties for ever.
FrameState safety_frame() {
    return scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::red, 0.5, kLine), spotted(Ball::black)});
}

// The frame after enough quiet strokes for the offer. False when the engine stopped the exchange.
bool play_to_the_offer(FrameState& s, Judgement* last = nullptr) {
    return exchange(s, ref().stalemate_offer_strokes, ref(), last);
}

}  // namespace

TEST_CASE("S3.17: after a long run of strokes that change nothing, a re-rack is offered to the player about to play") {
    FrameState s = safety_frame();
    Judgement last;
    if (!play_to_the_offer(s, &last)) return;

    CHECK(calls_text(last.calls) == "rerack_offer");
    CHECK(name(s.pending) == "rerack");
    // Each stroke passed the turn, and player 0 played the first.
    CHECK(s.striker == ref().stalemate_offer_strokes % 2);
    CHECK(s.decider == s.striker);
    CHECK(s.rerack_accepted == -1);
    CHECK_FALSE(s.restart);
    CHECK_FALSE(s.over);
    CHECK(choices_text(s, s.decider) == "accept_rerack refuse_rerack");
}

TEST_CASE("S3.17: a stroke that changes the position starts the count again") {
    FrameState s = safety_frame();
    const int offer = ref().stalemate_offer_strokes;
    if (!exchange(s, offer - 1, ref()) || !playable(s)) return;
    CHECK(s.quiet_strokes == offer - 1);

    // The red is sent well up the table.
    const Judgement lively = judge(s, {}, safety(s, ref().stalemate_quiet_travel_m + 0.1), oracle(), ref());
    CHECK(calls_text(lively.calls) == "");
    CHECK(lively.post.quiet_strokes == 0);
    s = lively.post;

    if (!exchange(s, offer - 1, ref())) return;
    CHECK(name(s.pending) == "none");
    Judgement last;
    if (!exchange(s, 1, ref(), &last)) return;
    CHECK(calls_text(last.calls) == "rerack_offer");
    CHECK(name(s.pending) == "rerack");
}

TEST_CASE("S3.17: when both players accept, the frame is to be started again") {
    FrameState s = safety_frame();
    s.scores = {12, 30};
    if (!play_to_the_offer(s)) return;
    const int first = s.decider;

    std::vector<Call> said;
    if (!choose(s, DecisionKind::accept_rerack, &said)) return;
    CHECK(name(s.pending) == "rerack");
    CHECK(s.decider == 1 - first);
    CHECK(s.rerack_accepted == first);
    CHECK_FALSE(s.restart);
    CHECK_FALSE(scenario::has_call(said, CallKind::rerack));
    CHECK(choices_text(s, s.decider) == "accept_rerack refuse_rerack");

    if (!choose(s, DecisionKind::accept_rerack, &said)) return;
    CHECK(s.restart);
    CHECK(s.winner == -1);
    CHECK(scenario::has_call(said, CallKind::rerack));
}

TEST_CASE("S3.17(a): when the player offered the re-rack objects, play goes on under a proviso") {
    FrameState s = safety_frame();
    if (!play_to_the_offer(s)) return;
    const int first = s.decider;

    if (!choose(s, DecisionKind::refuse_rerack)) return;
    CHECK(name(s.pending) == "none");
    CHECK(s.striker == first);
    CHECK_FALSE(s.restart);
    CHECK(s.rerack_accepted == -1);
    CHECK(s.rerack_deadline == ref().stalemate_grace_strokes);
}

TEST_CASE("S3.17(a): when the other player objects, play goes on under the same proviso") {
    FrameState s = safety_frame();
    if (!play_to_the_offer(s)) return;
    const int first = s.decider;

    if (!choose(s, DecisionKind::accept_rerack)) return;
    CHECK(s.decider == 1 - first);
    if (!choose(s, DecisionKind::refuse_rerack)) return;
    CHECK(name(s.pending) == "none");
    CHECK(s.striker == first);
    CHECK_FALSE(s.restart);
    CHECK(s.rerack_accepted == -1);
    CHECK(s.rerack_deadline == ref().stalemate_grace_strokes);
}

TEST_CASE("S3.17(a): the position changing within the stated period lifts the proviso") {
    FrameState s = safety_frame();
    const int grace = ref().stalemate_grace_strokes;
    if (!play_to_the_offer(s) || !choose(s, DecisionKind::refuse_rerack)) return;

    if (!exchange(s, grace - 1, ref()) || !playable(s)) return;
    CHECK(s.rerack_deadline == 1);
    const Judgement lively = judge(s, {}, safety(s, ref().stalemate_quiet_travel_m + 0.1), oracle(), ref());
    CHECK(calls_text(lively.calls) == "");
    CHECK_FALSE(lively.post.restart);
    CHECK(lively.post.rerack_deadline == -1);
    CHECK(lively.post.quiet_strokes == 0);

    // With the proviso gone, the strokes it had left bring nothing.
    s = lively.post;
    if (!exchange(s, grace, ref())) return;
    CHECK(name(s.pending) == "none");
    CHECK_FALSE(s.restart);
}

TEST_CASE("S3.17(b): unchanged at the end of the stated period, the frame is restarted whatever the players say") {
    FrameState s = safety_frame();
    const int grace = ref().stalemate_grace_strokes;
    if (!play_to_the_offer(s) || !choose(s, DecisionKind::refuse_rerack)) return;

    if (!exchange(s, grace - 1, ref())) return;
    CHECK(name(s.pending) == "none");
    CHECK_FALSE(s.restart);

    Judgement last;
    if (!exchange(s, 1, ref(), &last)) return;
    CHECK(calls_text(last.calls) == "rerack");
    CHECK(s.restart);
    CHECK(s.winner == -1);
}

TEST_CASE("S3.17(b), (c): the restarted frame is racked afresh with no score and the same player breaks") {
    MatchConfig config;
    config.best_of = 3;
    config.first_breaker = 1;
    config.seed = 7;
    MatchState m = start_match(config, params());
    CHECK(m.breaker == 1);

    // Some way into frame 1, with the first breaker's opponent at the table.
    FrameState f = safety_frame();
    f.seed = m.frame.seed;
    f.scores = {12, 30};
    f.strokes = 40;
    if (!play_to_the_offer(f) || !choose(f, DecisionKind::refuse_rerack)) return;
    if (!exchange(f, ref().stalemate_grace_strokes, ref())) return;
    CHECK(f.restart);
    if (!f.restart) return;

    m.frame = f;
    CHECK(name(settle(m, params())) == "reracked");
    CHECK_FALSE(m.over);
    CHECK(m.frame_number == 1);
    CHECK(m.breaker == 1);
    CHECK(m.reracks == 1);
    CHECK(m.frames[0] + m.frames[1] == 0);
    CHECK(m.history.empty());
    CHECK(m.frame.balls.size() == 22);
    CHECK(m.frame.scores[0] == 0);
    CHECK(m.frame.scores[1] == 0);
    CHECK(m.frame.strokes == 0);
    CHECK(m.frame.striker == 1);
    CHECK(m.frame.cue_in_hand);
    CHECK(name(m.frame.on.kind) == "red");
    CHECK(name(m.frame.pending) == "none");
    CHECK_FALSE(m.frame.restart);
    CHECK_FALSE(m.frame.over);
}

TEST_CASE("S3.17(d): on a re-spotted black only the black is set again, the same player to open") {
    // Three quiet strokes bring the offer here, so that the player about to play when it comes
    // is not the one who opened on the re-spotted black.
    RefParams three = ref();
    three.stalemate_offer_strokes = 3;
    MatchConfig config;
    config.best_of = 3;
    config.first_breaker = 0;
    config.seed = 11;
    MatchState m = start_match(config, params());

    // The last black, potted by the player seven behind: the frame is tied.
    FrameState f = scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::black, 0.5, kLine)});
    f.seed = m.frame.seed;
    f.striker = 1;
    f.scores = {40, 33};
    f.strokes = 60;
    const int black = place_of(f, Ball::black);
    ShotFacts pot = scenario::quiet_facts(f);
    pot.first_hit = {black};
    pot.first_hit_t = 0.5;
    pot.closest_gap_m[std::size_t(black)] = 0.0;
    pot.pots.push_back({black, 0, 2.0});
    pot.object_travel_m = 1.5;
    pot.cue_travel_m = 1.0;
    const Judgement tie = judge(f, {}, pot, oracle(), three);
    CHECK(tie.points == 7);
    CHECK(tie.post.scores[0] == 40);
    CHECK(tie.post.scores[1] == 40);
    CHECK(tie.post.respotted_black);
    CHECK(name(tie.post.pending) == "toss");
    if (tie.post.pending != Pending::toss) return;

    m.frame = tie.post;
    CHECK(name(settle(m, params())) == "none");
    const int opener = m.frame.decider;
    MatchEvent event = MatchEvent::none;
    if (!match_choose(m, DecisionKind::play_first, opener, three, event)) return;
    CHECK(m.frame.striker == opener);
    CHECK(m.frame.cue_in_hand);
    CHECK(lies_at(m.frame, Ball::black, table::spot(Ball::black)));

    Judgement last;
    if (!exchange(m.frame, 3, three, &last)) return;
    CHECK(calls_text(last.calls) == "rerack_offer");
    CHECK(name(m.frame.pending) == "rerack");
    CHECK(m.frame.decider == 1 - opener);
    if (m.frame.pending != Pending::rerack) return;

    if (!match_choose(m, DecisionKind::accept_rerack, m.frame.decider, three, event)) return;
    CHECK(name(event) == "none");
    std::vector<Call> said;
    if (!match_choose(m, DecisionKind::accept_rerack, m.frame.decider, three, event, &said)) return;
    // The engine sets the black again itself; the match racks nothing, so it has no event.
    CHECK(name(event) == "none");
    CHECK(scenario::has_call(said, CallKind::rerack));

    CHECK(m.frame_number == 1);
    CHECK(m.history.empty());
    CHECK_FALSE(m.over);
    CHECK(m.frame.balls.size() == 2);
    CHECK(place_of(m.frame, Ball::cue) >= 0);
    CHECK(lies_at(m.frame, Ball::black, table::spot(Ball::black)));
    CHECK(m.frame.cue_in_hand);
    CHECK(m.frame.respotted_black);
    CHECK(m.frame.striker == opener);
    CHECK(m.frame.scores[0] == m.frame.scores[1]);
    CHECK(name(m.frame.pending) == "none");
    CHECK_FALSE(m.frame.restart);
    CHECK_FALSE(m.frame.over);
}

namespace {

// Nothing hit at all, with the red in plain view: FOUL AND A MISS.
Judgement miss(const FrameState& s, const RefParams& r) { return judge(s, {}, scenario::quiet_facts(s), oracle(), r); }

// The black hit first and sent 70 cm down the table: a foul that changes the position.
Judgement lively_foul(const FrameState& s) {
    ShotFacts f = scenario::quiet_facts(s);
    const std::size_t black = std::size_t(place_of(s, Ball::black));
    f.first_hit = {int(black)};
    f.first_hit_t = 1.0;
    f.closest_gap_m[black] = 0.0;
    f.rest[black].x -= 0.7;
    f.object_travel_m = 0.7;
    f.cue_travel_m = 1.0;
    return judge(s, {}, f, oracle(), ref());
}

}  // namespace

TEST_CASE("S3.17: foul strokes, a miss put back among them, neither count towards the offer nor start the count again") {
    FrameState s = safety_frame();
    const int offer = ref().stalemate_offer_strokes;
    if (!exchange(s, offer - 1, ref()) || !playable(s)) return;

    const Judgement missed = miss(s, ref());
    CHECK(missed.miss);
    CHECK(calls_text(missed.calls) == "foul miss");
    s = missed.post;
    if (!choose(s, DecisionKind::replace) || !playable(s)) return;

    const Judgement moved = lively_foul(s);
    CHECK_FALSE(moved.fouls.empty());
    CHECK_FALSE(scenario::has_call(moved.calls, CallKind::rerack_offer));
    s = moved.post;
    if (!choose(s, DecisionKind::play)) return;
    CHECK(name(s.pending) == "none");

    // One more quiet stroke is the last of the count the fouls interrupted.
    Judgement last;
    if (!exchange(s, 1, ref(), &last)) return;
    CHECK(calls_text(last.calls) == "rerack_offer");
    CHECK(name(s.pending) == "rerack");
}

TEST_CASE("S3.17(a), (b): the stated period counts only fair strokes; a miss put back in it changes nothing") {
    FrameState s = safety_frame();
    const int grace = ref().stalemate_grace_strokes;
    if (!play_to_the_offer(s) || !choose(s, DecisionKind::refuse_rerack)) return;
    if (!exchange(s, grace / 2, ref()) || !playable(s)) return;

    const Judgement missed = miss(s, ref());
    CHECK(missed.miss);
    CHECK_FALSE(missed.post.restart);
    s = missed.post;
    if (!choose(s, DecisionKind::replace) || !playable(s)) return;
    const Judgement moved = lively_foul(s);
    CHECK_FALSE(moved.post.restart);
    s = moved.post;
    if (!choose(s, DecisionKind::play_again)) return;

    if (!exchange(s, grace - grace / 2 - 1, ref())) return;
    CHECK(name(s.pending) == "none");
    CHECK_FALSE(s.restart);
    Judgement last;
    if (!exchange(s, 1, ref(), &last)) return;
    CHECK(calls_text(last.calls) == "rerack");
    CHECK(s.restart);
}

TEST_CASE("S3.17(b): the period does not run out on a foul stroke, whose choice stays to be made") {
    FrameState s = safety_frame();
    const int grace = ref().stalemate_grace_strokes;
    if (!play_to_the_offer(s) || !choose(s, DecisionKind::refuse_rerack)) return;
    if (!exchange(s, grace - 1, ref()) || !playable(s)) return;

    // The stroke that would have ended the period is a miss.
    const Judgement missed = miss(s, ref());
    CHECK(calls_text(missed.calls) == "foul miss");
    CHECK_FALSE(missed.post.restart);
    CHECK(name(missed.post.pending) == "after_foul");
    CHECK(choices_text(missed.post, missed.post.decider) == "play play_again replace");
    s = missed.post;
    if (!choose(s, DecisionKind::play)) return;
    CHECK_FALSE(s.restart);

    // The next fair stroke that changes nothing ends it.
    Judgement last;
    if (!exchange(s, 1, ref(), &last)) return;
    CHECK(calls_text(last.calls) == "rerack");
    CHECK(s.restart);
}
