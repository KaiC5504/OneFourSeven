// WPBSA Section 2: frame, striker and turn, stroke, pot, break, in-hand, ball in play and penalty
// points, each shown by play. A REQUIRE is a premise read from the simulator's own record (never
// from observe()), or a check that the frame carried on from it; a CHECK is the rule's outcome.

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "ofs/physics/rack.hpp"
#include "scenario.hpp"

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kToTop = 0.0;         // +x
constexpr double kToLeft = kPi / 2.0;  // +y: along the line between the middle pockets
constexpr double kToBaulk = kPi;

// A straight pot into the left middle pocket: the cue ball here, the object ball there, played
// at kToLeft.
constexpr Vec2 kCue{0.0, 0.3};
constexpr Vec2 kObject{0.0, 0.6};

// An in-off: the cue ball cuts the object ball and runs on into the left middle pocket, the
// object ball staying up.
constexpr Vec2 kInOffCue{-0.0869, 0.5170};
constexpr Vec2 kInOffObject{0.10, 0.80};
constexpr Vec2 kInOffAim{0.0500, 0.7839};

int first_hit(const phys::ShotRecord& r, int cue) {
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind != phys::EventKind::ball_ball) continue;
        if (e.a == cue) return e.b;
        if (e.b == cue) return e.a;
    }
    return -1;
}

bool dropped(const phys::ShotRecord& r, int place) {
    return r.final_state.balls[std::size_t(place)].motion == phys::Motion::pocketed;
}

int drops(const phys::ShotRecord& r) {
    int n = 0;
    for (const phys::BallState& b : r.final_state.balls) n += b.motion == phys::Motion::pocketed ? 1 : 0;
    return n;
}

Vec2 rest_of(const phys::ShotRecord& r, int place) {
    const phys::BallState& b = r.final_state.balls[std::size_t(place)];
    return {b.r.x, b.r.y};
}

bool same(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y) < 1e-9; }

bool lies_at(const Position& p, Ball b, Vec2 where, int index = 0) {
    const int place = place_of(p, b, index);
    return place >= 0 && same(p.balls[std::size_t(place)].pos, where);
}

Vec2 cue_pos(const Position& p) { return p.balls[std::size_t(p.cue_ball)].pos; }

int count(const Position& p, Ball b) {
    int n = 0;
    for (const BallOnTable& x : p.balls) n += x.ball == b ? 1 : 0;
    return n;
}

// The frame went on from where the record left the cue ball, which a later stroke relies on.
bool follows(const Played& p, const FrameState& pre) {
    const FrameState& post = p.judgement.post;
    return !post.over && post.pending == Pending::none && same(cue_pos(post), rest_of(p.record, pre.cue_ball));
}

std::string score(const FrameState& s) { return std::to_string(s.scores[0]) + "-" + std::to_string(s.scores[1]); }

std::string on_text(const Position& p) {
    std::string out(on_kind_name(p.on.kind));
    if (p.on.kind == OnKind::colour) out += " " + std::string(table::ball_name(p.on.colour));
    return out;
}

std::string fouls_text(const Judgement& j) {
    std::string out;
    for (const Foul& f : j.fouls) {
        if (!out.empty()) out += ", ";
        out += std::string(foul_name(f.kind)) + " " + std::to_string(f.value);
    }
    return out;
}

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

// "1 conceded": the winner and the reason the referee gave; "" when the frame was not called over.
std::string frame_over_call(const std::vector<Call>& calls) {
    for (const Call& c : calls) {
        if (c.kind == CallKind::frame_over) return std::to_string(c.value) + " " + c.detail;
    }
    return "";
}

bool decide_now(FrameState& s, DecisionKind kind, int player, std::vector<Call>* calls = nullptr) {
    std::vector<Call> local;
    std::string error;
    return decide(s, {kind, player}, *rig().oracle, rig().ref, calls != nullptr ? *calls : local, error);
}

phys::CueStrike follow(double aim, double speed) {
    phys::CueStrike c = strike(aim, speed);
    c.tip_y = 0.3;
    return c;
}

// Red on, the cue ball played into the black first: seven away.
FrameState black_first() {
    return frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, 0.3, 0.3), ball(Ball::red, -0.6, 0.3)});
}

// The cue ball goes in off a red, which stays on the table with another.
FrameState in_off_position() {
    return frame({ball(Ball::cue, kInOffCue.x, kInOffCue.y), ball(Ball::red, kInOffObject.x, kInOffObject.y),
                  ball(Ball::red, 0.6, -0.4, 1), spotted(Ball::black)});
}

Played play_in_off(const FrameState& s) {
    Played p = play(s, strike(aim_at(s, kInOffAim), 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 0));
    REQUIRE(drops(p.record) == 1);
    return p;
}

}  // namespace

TEST_CASE("S2.1(a): a concession ends the frame in the other player's favour") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.3, 0.3), spotted(Ball::black)});
    s.scores = {10, 20};
    s.concede_any_time = {true, true};
    std::vector<Call> calls;
    CHECK(decide_now(s, DecisionKind::concede_frame, 0, &calls));
    CHECK(s.over);
    CHECK(s.winner == 1);
    CHECK(s.end_reason == EndReason::conceded);
    CHECK(score(s) == "10-20");
    CHECK(frame_over_call(calls) == "1 conceded");
}

TEST_CASE("S2.1(b): the striker, more than seven ahead with only the black left, may claim the frame") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), spotted(Ball::black)});
    s.scores = {40, 32};
    CHECK(is_offered(s, 0, DecisionKind::claim_frame));
    CHECK_FALSE(is_offered(s, 1, DecisionKind::claim_frame));

    std::vector<Call> calls;
    CHECK(decide_now(s, DecisionKind::claim_frame, 0, &calls));
    CHECK(s.over);
    CHECK(s.winner == 0);
    CHECK(s.end_reason == EndReason::claimed);
    CHECK(score(s) == "40-32");
    CHECK(frame_over_call(calls) == "0 claimed");
}

TEST_CASE("S2.1(b): no claim at seven ahead, with another colour still up, or by the non-striker") {
    FrameState seven = frame({ball(Ball::cue, kCue.x, kCue.y), spotted(Ball::black)});
    seven.scores = {39, 32};
    CHECK_FALSE(is_offered(seven, 0, DecisionKind::claim_frame));
    CHECK_FALSE(decide_now(seven, DecisionKind::claim_frame, 0));
    CHECK_FALSE(seven.over);

    FrameState pink_up = frame({ball(Ball::cue, kCue.x, kCue.y), spotted(Ball::pink), spotted(Ball::black)});
    pink_up.scores = {60, 20};
    CHECK_FALSE(is_offered(pink_up, 0, DecisionKind::claim_frame));
    CHECK_FALSE(decide_now(pink_up, DecisionKind::claim_frame, 0));
    CHECK_FALSE(pink_up.over);

    FrameState other = frame({ball(Ball::cue, kCue.x, kCue.y), spotted(Ball::black)});
    other.scores = {40, 32};
    CHECK_FALSE(decide_now(other, DecisionKind::claim_frame, 1));
    CHECK_FALSE(other.over);
}

TEST_CASE("S2.1(b): potting the pink to go more than seven ahead leaves the striker to claim or play on") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::pink, kObject.x, kObject.y), spotted(Ball::black)});
    s.scores = {50, 30};
    const Played p = play(s, strike(kToLeft, 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(drops(p.record) == 1);

    const FrameState& post = p.judgement.post;
    CHECK(p.judgement.points == 6);
    CHECK(score(post) == "56-30");
    CHECK_FALSE(post.over);
    CHECK(post.pending == Pending::none);
    CHECK(post.striker == 0);
    CHECK(on_text(post) == "colour black");
    CHECK(is_offered(post, 0, DecisionKind::claim_frame));
}

TEST_CASE("S2.1(c): the striker left on the black alone and more than seven behind loses the frame") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::pink, kObject.x, kObject.y), spotted(Ball::black)});
    s.scores = {30, 50};
    const Played p = play(s, strike(kToLeft, 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(drops(p.record) == 1);

    const FrameState& post = p.judgement.post;
    CHECK(score(post) == "36-50");
    CHECK(post.over);
    CHECK(post.winner == 1);
    CHECK(post.end_reason == EndReason::awarded);
    CHECK(frame_over_call(p.judgement.calls) == "1 awarded");
}

TEST_CASE("S2.1(c): a leader's stroke at the black that scores nothing gives up the turn and wins the frame") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, 0.3, 0.3)});
    s.scores = {50, 30};
    const Played p = play(s, strike(kToTop, 0.6));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(drops(p.record) == 0);

    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "");
    CHECK(score(post) == "50-30");
    CHECK(post.over);
    CHECK(post.winner == 0);
    CHECK(post.end_reason == EndReason::awarded);
    CHECK(frame_over_call(p.judgement.calls) == "0 awarded");
}

TEST_CASE("S2.1(c): seven behind on the black alone, the frame goes on") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, 0.3, 0.3)});
    s.scores = {30, 37};
    s.striker = 1;
    const Played p = play(s, strike(kToTop, 0.6));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(drops(p.record) == 0);

    const FrameState& post = p.judgement.post;
    CHECK_FALSE(post.over);
    CHECK(post.winner == -1);
    CHECK(post.striker == 0);
    CHECK(post.pending == Pending::none);
    CHECK(on_text(post) == "colour black");
    CHECK_FALSE(is_offered(post, 1, DecisionKind::claim_frame));
}

TEST_CASE("S2.1(c): reached through a free ball: the pink scores, the black comes back, and the striker is too far behind") {
    // The pink on and FREE BALL: the black nominated, hit first, and both go down. Only the pink
    // scores (S3.12(e)), the black is spotted (S3.12(c)), and with the black alone left the striker
    // is fourteen behind.
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::pink, 0.6, -0.4), ball(Ball::black, 0.3, 0.3)});
    s.free_ball = true;
    s.scores = {0, 20};
    ShotFacts f = quiet_facts(s);
    f.first_hit = {2};
    f.first_hit_t = 0.3;
    f.closest_gap_m[1] = 0.0;
    f.closest_gap_m[2] = 0.0;
    f.pots = {{2, 1, 1.0}, {1, 4, 2.0}};
    f.cue_travel_m = 1.0;
    f.object_travel_m = 1.5;
    const Judgement j = judge(s, nominate(Ball::black), f, *rig().oracle, rig().ref);

    CHECK(fouls_text(j) == "");
    CHECK(j.points == 6);
    CHECK(place_of(j.post, Ball::pink) == -1);
    CHECK(lies_at(j.post, Ball::black, table::spot(Ball::black)));
    CHECK(score(j.post) == "6-20");
    CHECK(j.post.over);
    CHECK(j.post.winner == 1);
    CHECK(j.post.end_reason == EndReason::awarded);
    CHECK(frame_over_call(j.calls) == "1 awarded");
}

TEST_CASE("S2.5: a stroke that scores keeps the striker at the table") {
    for (int who : {0, 1}) {
        CAPTURE(who);
        FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, kObject.x, kObject.y),
                              ball(Ball::red, 0.6, -0.4, 1), spotted(Ball::black)});
        s.striker = who;
        const Played p = play(s, strike(kToLeft, 2.0));
        REQUIRE(first_hit(p.record, 0) == 1);
        REQUIRE(dropped(p.record, 1));
        REQUIRE(drops(p.record) == 1);

        CHECK(p.judgement.points == 1);
        CHECK(p.judgement.post.striker == who);
        CHECK(p.judgement.post.pending == Pending::none);
    }
}

TEST_CASE("S2.5(a): a stroke that scores nothing ends the turn") {
    for (int who : {0, 1}) {
        CAPTURE(who);
        FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.3, 0.3), spotted(Ball::black)});
        s.striker = who;
        const Played p = play(s, strike(kToTop, 0.6));
        REQUIRE(first_hit(p.record, 0) == 1);
        REQUIRE(drops(p.record) == 0);

        const FrameState& post = p.judgement.post;
        CHECK(fouls_text(p.judgement) == "");
        CHECK(p.judgement.points == 0);
        CHECK(post.striker == 1 - who);
        CHECK(post.pending == Pending::none);
        CHECK(on_text(post) == "red");
    }
}

TEST_CASE("S2.5(b): a foul ends the offender's turn") {
    const FrameState s = black_first();
    const Played p = play(s, strike(kToTop, 0.6));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(drops(p.record) == 0);

    const FrameState& post = p.judgement.post;
    CHECK(has_foul(p.judgement, FoulKind::first_hit_not_on));
    CHECK(post.pending == Pending::after_foul);
    CHECK(post.offender == 0);
    CHECK(post.decider == 1);
    CHECK(post.striker == 1);
    CHECK(post.break_points == 0);
    CHECK(offered(post, 0) == "");
}

TEST_CASE("S2.5(c): asked to play again after a foul, the offender is the striker once more") {
    const FrameState s = black_first();
    const Played p = play(s, strike(kToTop, 0.6));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(drops(p.record) == 0);

    FrameState put_back = p.judgement.post;
    CHECK_FALSE(decide_now(put_back, DecisionKind::play_again, 0));
    CHECK(decide_now(put_back, DecisionKind::play_again, 1));
    CHECK(put_back.striker == 0);
    CHECK(put_back.pending == Pending::none);
    CHECK(same(cue_pos(put_back), rest_of(p.record, 0)));

    FrameState plays_on = p.judgement.post;
    CHECK(decide_now(plays_on, DecisionKind::play, 1));
    CHECK(plays_on.striker == 1);
    CHECK(plays_on.pending == Pending::none);
}

TEST_CASE("S2.6(a): each strike of the cue ball is one stroke") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.3, 0.3), spotted(Ball::black)});
    s.striker = 1;
    s.strokes = 5;
    s.stats[1].shots = 2;
    const Played p = play(s, strike(kToTop, 0.6));
    REQUIRE(p.record.has_strike);
    REQUIRE(first_hit(p.record, 0) == 1);

    CHECK(p.judgement.post.strokes == 6);
    CHECK(p.judgement.post.stats[1].shots == 3);
    CHECK(p.judgement.post.stats[0].shots == 0);
}

TEST_CASE("S2.6(c), S3.3(e): a stroke with no infringement is a legal one") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, kObject.x, kObject.y),
                                ball(Ball::red, 0.6, -0.4, 1), spotted(Ball::black)});
    const Played p = play(s, strike(kToLeft, 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(drops(p.record) == 1);

    CHECK(fouls_text(p.judgement) == "");
    CHECK(p.judgement.penalty == 0);
    CHECK_FALSE(p.judgement.miss);
    CHECK_FALSE(has_call(p.judgement.calls, CallKind::foul));
    CHECK(p.judgement.post.stats[0].fouls == 0);
}

TEST_CASE("S2.6(d): a stroke is complete with every ball at rest and the potted colour back on its spot") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, kObject.x, kObject.y),
                          ball(Ball::red, 0.6, -0.4)});
    s.on = {OnKind::any_colour, Ball::yellow};
    const Played p = play(s, strike(kToLeft, 2.0), nominate(Ball::black));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(drops(p.record) == 1);
    REQUIRE(same(rest_of(p.record, 2), s.balls[2].pos));

    const FrameState& post = p.judgement.post;
    CHECK(post.balls.size() == 3);
    CHECK(same(cue_pos(post), rest_of(p.record, 0)));
    CHECK(lies_at(post, Ball::red, s.balls[2].pos));
    CHECK(lies_at(post, Ball::black, table::spot(Ball::black)));
    CHECK(p.judgement.spotted.size() == 1);
    if (p.judgement.spotted.size() == 1) {
        CHECK(p.judgement.spotted[0].ball == Ball::black);
        CHECK(same(p.judgement.spotted[0].pos, table::spot(Ball::black)));
    }
}

TEST_CASE("S2.6(e): a stroke made off a cushion is as legal as a direct one") {
    const double rail = table::kPlayingWidth / 2.0 - table::kBallRadius;
    const FrameState s = frame({ball(Ball::cue, 0.4, 0.6), ball(Ball::red, 0.8, 0.75), spotted(Ball::black)});
    // Aimed at the red's mirror image in the left side cushion.
    const Played p = play(s, strike(aim_at(s, {0.8, 2.0 * rail - 0.75}), 1.5));
    bool cushion_first = false;
    for (const phys::ShotEvent& e : p.record.events) {
        if (e.kind == phys::EventKind::ball_line && e.a == 0) cushion_first = true;
        if (e.kind == phys::EventKind::ball_ball) break;
    }
    REQUIRE(cushion_first);
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(drops(p.record) == 0);

    CHECK(fouls_text(p.judgement) == "");
    CHECK_FALSE(p.judgement.miss);
    CHECK(p.judgement.post.striker == 1);
    CHECK(p.judgement.post.pending == Pending::none);
}

TEST_CASE("S2.7: a ball on sent into a pocket by a legal stroke is potted and scores") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, kObject.x, kObject.y),
                                ball(Ball::red, 0.6, -0.4, 1), spotted(Ball::black)});
    const Played p = play(s, strike(kToLeft, 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(p.record.final_state.balls[1].pocket >= 0);
    REQUIRE(rig().table.pockets[std::size_t(p.record.final_state.balls[1].pocket)].name == "middle_left");
    REQUIRE(drops(p.record) == 1);

    const FrameState& post = p.judgement.post;
    CHECK(p.judgement.points == 1);
    CHECK(score(post) == "1-0");
    CHECK(post.stats[0].pots == 1);
    CHECK(count(post, Ball::red) == 1);
    CHECK(place_of(post, Ball::red, 0) == -1);
}

TEST_CASE("S2.7: a ball that drops in a foul stroke is pocketed, not potted: nothing is scored") {
    // The cue ball follows the red in.
    const FrameState s = frame({ball(Ball::cue, 0.0, 0.5), ball(Ball::red, 0.0, 0.7), ball(Ball::red, 0.6, -0.4, 1),
                                spotted(Ball::black)});
    const Played p = play(s, follow(kToLeft, 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(dropped(p.record, 0));
    REQUIRE(drops(p.record) == 2);

    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "in_off 4");
    CHECK(p.judgement.points == 0);
    CHECK(score(post) == "0-4");
    CHECK(post.stats[0].pots == 0);
    CHECK(post.stats[0].highest_break == 0);
    CHECK(count(post, Ball::red) == 1);
}

TEST_CASE("S2.8: a break is what the striker pots in successive strokes of one turn") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, kObject.x, kObject.y),
                                ball(Ball::black, 0.0, 0.15), ball(Ball::red, -0.6, -0.2, 1)});
    const Played red = play(s, strike(kToLeft, 2.0));
    REQUIRE(first_hit(red.record, 0) == 1);
    REQUIRE(dropped(red.record, 1));
    REQUIRE(drops(red.record) == 1);
    CHECK(red.judgement.post.break_points == 1);

    // The black lies on the same line, towards the right middle pocket.
    REQUIRE(follows(red, s));
    const FrameState& s2 = red.judgement.post;
    const int black = place_of(s2, Ball::black);
    REQUIRE(black >= 0);
    const Played colour = play(s2, strike(aim_at_ball(s2, black), 2.5), nominate(Ball::black));
    REQUIRE(first_hit(colour.record, s2.cue_ball) == black);
    REQUIRE(dropped(colour.record, black));
    REQUIRE(drops(colour.record) == 1);
    CHECK(colour.judgement.points == 7);
    CHECK(colour.judgement.post.break_points == 8);
    CHECK(colour.judgement.post.striker == 0);

    REQUIRE(follows(colour, s2));
    const FrameState& s3 = colour.judgement.post;
    const int last = place_of(s3, Ball::red, 1);
    REQUIRE(last >= 0);
    const Played safety = play(s3, strike(aim_at_ball(s3, last), 0.4));
    REQUIRE(first_hit(safety.record, s3.cue_ball) == last);
    REQUIRE(drops(safety.record) == 0);

    const FrameState& post = safety.judgement.post;
    CHECK(fouls_text(safety.judgement) == "");
    CHECK(post.striker == 1);
    CHECK(post.break_points == 0);
    CHECK(score(post) == "8-0");
    CHECK(post.stats[0].highest_break == 8);
    CHECK(post.stats[0].pots == 2);
    CHECK(post.stats[0].shots == 3);
    CHECK(post.stats[1].highest_break == 0);
}

TEST_CASE("S2.9(a)(i): the cue ball is in hand before a frame starts") {
    const FrameState s = new_frame(1, phys::opening_rack(3, rig().params), 3);
    REQUIRE(s.balls.size() == 22);
    CHECK(s.cue_in_hand);
    CHECK(s.striker == 1);
    CHECK(s.balls[std::size_t(s.cue_ball)].ball == Ball::cue);
    CHECK(same(cue_pos(s), table::default_cue_ball_position()));
}

TEST_CASE("S2.9(a)(ii): a pocketed cue ball is in hand for the next stroke") {
    const FrameState s = in_off_position();
    const Played p = play_in_off(s);

    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "in_off 4");
    CHECK(post.cue_in_hand);
    CHECK(count(post, Ball::cue) == 1);
    CHECK(count(post, Ball::red) == 2);
    CHECK(table::in_d(cue_pos(post)));
    // Nothing else is in the D, so it waits at its usual place (docs/rules/README.md, "In hand").
    CHECK(same(cue_pos(post), table::default_cue_ball_position()));

    FrameState next = post;
    CHECK(decide_now(next, DecisionKind::play, 1));
    CHECK(next.cue_in_hand);
    std::string error;
    CHECK(place_cue_ball(next, {-1.2, 0.1}, *rig().oracle, error));
    CHECK(same(cue_pos(next), {-1.2, 0.1}));
    CHECK(next.cue_in_hand);
}

TEST_CASE("S2.9(b)(i): played from in hand, the cue ball is in play where it stops") {
    FrameState s = frame({ball(Ball::cue, -1.2, 0.1), ball(Ball::red, -0.6, 0.1), spotted(Ball::black)});
    s.cue_in_hand = true;

    const Played legal = play(s, strike(kToTop, 0.6));
    REQUIRE(first_hit(legal.record, 0) == 1);
    REQUIRE(drops(legal.record) == 0);
    CHECK(fouls_text(legal.judgement) == "");
    CHECK_FALSE(legal.judgement.post.cue_in_hand);
    CHECK(same(cue_pos(legal.judgement.post), rest_of(legal.record, 0)));

    // A stroke from the D that hits nothing is a foul, but the cue ball was still played from in
    // hand: the next stroke is from where it lies (S3.10(f)).
    const Played missed = play(s, strike(kToBaulk, 0.15));
    REQUIRE(first_hit(missed.record, 0) == -1);
    REQUIRE(drops(missed.record) == 0);
    CHECK(has_foul(missed.judgement, FoulKind::no_hit));
    CHECK_FALSE(missed.judgement.post.cue_in_hand);
    CHECK(same(cue_pos(missed.judgement.post), rest_of(missed.record, 0)));
}

TEST_CASE("S2.9(c): whoever plays next is in hand, the offender included when put back in") {
    const FrameState s = in_off_position();
    const Played p = play_in_off(s);

    FrameState again = p.judgement.post;
    CHECK(decide_now(again, DecisionKind::play_again, 1));
    CHECK(again.striker == 0);
    CHECK(again.cue_in_hand);
    std::string error;
    CHECK(place_cue_ball(again, {-1.1, -0.2}, *rig().oracle, error));
    CHECK(same(cue_pos(again), {-1.1, -0.2}));
}

TEST_CASE("S2.10(a): a cue ball in play is not in hand and cannot be placed") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.3, 0.3), spotted(Ball::black)});
    std::string error;
    CHECK_FALSE(place_cue_ball(s, {-1.2, 0.1}, *rig().oracle, error));
    CHECK_FALSE(error.empty());
    CHECK(same(cue_pos(s), kCue));
    CHECK_FALSE(s.cue_in_hand);
}

TEST_CASE("S2.10(b): a red is out of play once potted") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, kObject.x, kObject.y),
                                ball(Ball::red, 0.6, -0.4, 1), spotted(Ball::black)});
    const Played p = play(s, strike(kToLeft, 2.0));
    REQUIRE(dropped(p.record, 1));
    REQUIRE(drops(p.record) == 1);

    const FrameState& post = p.judgement.post;
    CHECK(post.balls.size() == 3);
    CHECK(place_of(post, Ball::red, 0) == -1);
    CHECK(lies_at(post, Ball::red, {0.6, -0.4}, 1));
}

TEST_CASE("S2.10(c): a colour potted while reds remain is in play again on its spot") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::blue, kObject.x, kObject.y),
                          ball(Ball::red, 0.6, -0.4)});
    s.on = {OnKind::any_colour, Ball::yellow};
    const Played p = play(s, strike(kToLeft, 2.0), nominate(Ball::blue));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(drops(p.record) == 1);

    const FrameState& post = p.judgement.post;
    CHECK(p.judgement.points == 5);
    CHECK(count(post, Ball::blue) == 1);
    CHECK(lies_at(post, Ball::blue, table::spot(Ball::blue)));
}

TEST_CASE("S2.16: penalty points go to the player who did not offend") {
    for (int who : {0, 1}) {
        CAPTURE(who);
        FrameState s = black_first();
        s.striker = who;
        s.scores = {12, 20};
        const Played p = play(s, strike(kToTop, 0.6));
        REQUIRE(first_hit(p.record, 0) == 1);
        REQUIRE(drops(p.record) == 0);

        const FrameState& post = p.judgement.post;
        CHECK(p.judgement.penalty == 7);
        CHECK(p.judgement.points == 0);
        CHECK(score(post) == (who == 0 ? "12-27" : "19-20"));
        CHECK(post.last_penalty == 7);
        CHECK(post.stats[std::size_t(1 - who)].points_from_fouls == 7);
        CHECK(post.stats[std::size_t(who)].points_from_fouls == 0);
        CHECK(post.stats[std::size_t(who)].fouls == 1);
        bool announced = false;
        for (const Call& c : p.judgement.calls) announced = announced || (c.kind == CallKind::foul && c.value == 7);
        CHECK(announced);
    }
}
