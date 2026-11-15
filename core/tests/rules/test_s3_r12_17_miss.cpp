// S3.14 (foul and a miss), one scenario per paragraph, written from the rulebook and
// docs/rules/README.md, never from the engine. The owner chose the television standard: every
// failure to hit a ball on first is a miss unless S3.14(a)(i) or (a)(ii) excuses it. REQUIRE
// states a premise, read from the raw shot record or worked out by hand here; CHECK states what
// the rule then demands.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "scenario.hpp"

using namespace ofs;
using namespace ofs::rules;
using scenario::ball;
using scenario::nominate;
using scenario::place_of;
using scenario::spotted;

namespace {

constexpr double kR = table::kBallRadius;
constexpr double kPi = 3.14159265358979323846;
// The positions are built along this line, clear of every spot.
constexpr double kLine = 0.5;

const TableOracle& oracle() { return *scenario::rig().oracle; }
const RefParams& ref() { return scenario::rig().ref; }

// The first object ball the cue ball touched, from the raw record; -1 when it touched none.
int first_touched(const phys::ShotRecord& r, int cue) {
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind != phys::EventKind::ball_ball) continue;
        if (e.a == cue) return e.b;
        if (e.b == cue) return e.a;
    }
    return -1;
}

Vec2 rest(const phys::ShotRecord& r, int place) {
    const phys::BallState& b = r.final_state.balls[std::size_t(place)];
    return {b.r.x, b.r.y};
}

bool pocketed(const phys::ShotRecord& r, int place) {
    return r.final_state.balls[std::size_t(place)].motion == phys::Motion::pocketed;
}

bool nothing_pocketed(const phys::ShotRecord& r) {
    for (std::size_t i = 0; i < r.final_state.balls.size(); ++i) {
        if (pocketed(r, int(i))) return false;
    }
    return true;
}

bool same(Vec2 a, Vec2 b, double tol = 0.0) { return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol; }

bool stayed(const phys::ShotRecord& r, const Position& pre, int place) {
    return !pocketed(r, place) && same(rest(r, place), pre.balls[std::size_t(place)].pos);
}

// Where a stroke that pocketed nothing leaves the balls, from the raw record.
Position left_by(Position pre, const phys::ShotRecord& r) {
    for (std::size_t i = 0; i < pre.balls.size(); ++i) pre.balls[i].pos = rest(r, int(i));
    return pre;
}

// For positions built here; the ball must be listed.
Vec2 at(const Position& p, Ball b, int index = 0) { return p.balls[std::size_t(place_of(p, b, index))].pos; }

bool lies_at(const Position& p, Ball b, Vec2 pos, double tol = 0.0) {
    const int place = place_of(p, b);
    return place >= 0 && same(p.balls[std::size_t(place)].pos, pos, tol);
}

// Every object ball that is not a `b`: the balls that can be in the way of one.
std::vector<Vec2> all_but(const Position& p, Ball b) {
    std::vector<Vec2> out;
    for (const BallOnTable& x : p.balls) {
        if (x.ball != Ball::cue && x.ball != b) out.push_back(x.pos);
    }
    return out;
}

// The same balls in the same places, whatever their order in the two lists.
bool same_balls(const Position& a, const Position& b, double tol = 0.0) {
    if (a.balls.size() != b.balls.size()) return false;
    for (const BallOnTable& x : a.balls) {
        const int place = place_of(b, x.ball, x.index);
        if (place < 0 || !same(x.pos, b.balls[std::size_t(place)].pos, tol)) return false;
    }
    return true;
}

double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The cue ball's centre as it meets `target`, having come straight from `cue`: along the line of
// centres for edge 0, grazing one extreme edge or the other for +1 and -1.
Vec2 meeting(Vec2 cue, Vec2 target, double edge) {
    const double d = dist(cue, target);
    const double turn = edge * std::acos(2.0 * kR / d);
    const double ux = (cue.x - target.x) / d;
    const double uy = (cue.y - target.y) / d;
    return {target.x + 2.0 * kR * (ux * std::cos(turn) - uy * std::sin(turn)),
            target.y + 2.0 * kR * (ux * std::sin(turn) + uy * std::cos(turn))};
}

// Whether a ball at `c` stops the cue ball's centre travelling straight from `from` to `to`.
bool blocks(Vec2 c, Vec2 from, Vec2 to) {
    const double lx = to.x - from.x;
    const double ly = to.y - from.y;
    const double t = std::clamp(((c.x - from.x) * lx + (c.y - from.y) * ly) / (lx * lx + ly * ly), 0.0, 1.0);
    return dist(c, {from.x + t * lx, from.y + t * ly}) < 2.0 * kR;
}

// By hand: the straight stroke from `cue` to that part of `target` passes every ball of `others`.
bool open(Vec2 cue, Vec2 target, const std::vector<Vec2>& others, double edge) {
    const Vec2 to = meeting(cue, target, edge);
    return std::none_of(others.begin(), others.end(), [&](Vec2 c) { return blocks(c, cue, to); });
}

bool both_edges_open(Vec2 cue, Vec2 target, const std::vector<Vec2>& others) {
    return open(cue, target, others, 1.0) && open(cue, target, others, -1.0);
}

bool no_part_open(Vec2 cue, Vec2 target, const std::vector<Vec2>& others) {
    for (int i = -50; i <= 50; ++i) {
        if (open(cue, target, others, i / 50.0)) return false;
    }
    return true;
}

std::string name(Pending p) { return std::string(pending_name(p)); }
std::string name(OnKind k) { return std::string(on_kind_name(k)); }
std::string name(EndReason r) { return std::string(end_reason_name(r)); }
std::string name(Ball b) { return std::string(table::ball_name(b)); }

std::string fouls_text(const Judgement& j) {
    std::string out;
    for (const Foul& f : j.fouls) {
        if (!out.empty()) out += ", ";
        out += std::string(foul_name(f.kind)) + " " + std::to_string(f.value);
    }
    return out;
}

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

std::string nominations_text(const FrameState& s) {
    std::string out;
    for (Ball b : nominations(s)) {
        if (!out.empty()) out += " ";
        out += std::string(table::ball_name(b));
    }
    return out;
}

// The decider's choice; `said` receives what the referee then announces. False, after failing a
// check, when the engine refuses it.
bool choose(FrameState& s, DecisionKind kind, std::vector<Call>* said = nullptr) {
    std::vector<Call> calls;
    std::string error;
    const bool ok = decide(s, {kind, s.decider}, oracle(), ref(), calls, error);
    CHECK_MESSAGE(ok, std::string(decision_name(kind)), " refused: ", error);
    if (said != nullptr) *said = calls;
    return ok;
}

// Whether the next stroke can be played from `s` with the balls where `expected` has them. When
// it cannot, a check has failed and the scenario stops there.
bool lies_as(const FrameState& s, const Position& expected) {
    const bool ok = !s.over && s.pending == Pending::none && same_balls(s, expected, 1e-9);
    CHECK_MESSAGE(ok, "the frame is not waiting for a stroke with the balls where they should lie");
    return ok;
}

// Facts for a stroke whose first impact was on `first`, with every ball back where it began.
ShotFacts struck(const Position& p, std::vector<int> first) {
    ShotFacts f = scenario::quiet_facts(p);
    for (int place : first) f.closest_gap_m[std::size_t(place)] = 0.0;
    f.first_hit = std::move(first);
    f.first_hit_t = 0.5;
    f.cue_travel_m = 1.0;
    return f;
}

// The last red a metre from the cue ball with nothing between them and every colour on its spot:
// 35 points on the table.
FrameState open_red() {
    return scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::red, 0.5, kLine), spotted(Ball::yellow),
                            spotted(Ball::green), spotted(Ball::brown), spotted(Ball::blue), spotted(Ball::pink),
                            spotted(Ball::black)});
}

// The same with the brown half-way to the red, `off` metres to one side of the line. On the line
// it hides the whole red. At 4 cm it covers the middle of the red and leaves the far edge; at 6 cm
// the line of centres is free and only the near edge is covered.
FrameState red_past_brown(double off) {
    FrameState s = open_red();
    s.balls[std::size_t(place_of(s, Ball::brown))].pos = {0.0, kLine + off};
    return s;
}

// A soft stroke straight away from the red: the cue ball rolls a few centimetres back along the
// line and touches nothing.
phys::CueStrike roll_away() { return scenario::strike(kPi, 0.1); }

// A soft stroke square to the line: the cue ball rolls a hand's length off it and touches nothing.
phys::CueStrike roll_aside() { return scenario::strike(kPi / 2.0, 0.2); }

// The last red walled in by the six colours, each a fifth of a millimetre from it: no stroke,
// direct or off any cushion, can reach it. The yellow faces the cue ball.
FrameState walled_in_red() {
    std::vector<BallOnTable> balls{ball(Ball::cue, -0.2, kLine), ball(Ball::red, 0.5, kLine)};
    const Ball ring[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};
    for (int i = 0; i < 6; ++i) {
        const double a = kPi + i * kPi / 3.0;
        balls.push_back(
            ball(ring[i], 0.5 + (2.0 * kR + 2e-4) * std::cos(a), kLine + (2.0 * kR + 2e-4) * std::sin(a)));
    }
    return scenario::frame(balls);
}

// By hand: the gap between each colour and the next round the red is far too small for a ball.
bool walled_in(const Position& p) {
    const std::vector<Vec2> ring = all_but(p, Ball::red);
    for (std::size_t i = 0; i < ring.size(); ++i) {
        if (dist(ring[i], ring[(i + 1) % ring.size()]) - 2.0 * kR > 1e-3) return false;
        if (dist(ring[i], at(p, Ball::red)) - 2.0 * kR > 1e-3) return false;
    }
    return ring.size() == 6;
}

// Two reds and the brown: the near red sits just off the line of centres to the far one.
FrameState two_reds(Vec2 brown) {
    return scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::red, 0.5, kLine, 0),
                            ball(Ball::red, 0.0, kLine + 0.03, 1), ball(Ball::brown, brown.x, brown.y),
                            spotted(Ball::yellow), spotted(Ball::green), spotted(Ball::blue), spotted(Ball::pink),
                            spotted(Ball::black)});
}

// A red has just been potted, so a colour of the striker's choice is on. The black lies behind
// the last red; the blue is in the open.
FrameState colour_of_choice() {
    FrameState s = scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::red, 0.0, kLine),
                                    ball(Ball::black, 0.5, kLine), ball(Ball::blue, -0.5, 0.1), spotted(Ball::yellow),
                                    spotted(Ball::green), spotted(Ball::brown), spotted(Ball::pink)});
    s.on = BallOn{OnKind::any_colour, Ball::yellow};
    s.scores = {1, 0};
    s.break_points = 1;
    return s;
}

// The open red missed by player 0 with the scores at `scores` before the stroke.
Judgement open_red_missed_at(std::array<int, 2> scores) {
    FrameState pre = open_red();
    pre.scores = scores;
    REQUIRE(both_edges_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    return played.judgement;
}

void check_frame_lost_on_three_misses(const Judgement& j, int winner) {
    CHECK(j.miss);
    CHECK(calls_text(j.calls) == "foul miss frame_over");
    CHECK(j.post.over);
    CHECK(j.post.winner == winner);
    CHECK(name(j.post.end_reason) == "three_misses");
    for (const Call& c : j.calls) {
        if (c.kind != CallKind::frame_over) continue;
        CHECK(c.value == winner);
        CHECK(c.detail == "three_misses");
    }
}

}  // namespace

TEST_CASE("S3.14(a): a failed escape from a snooker is FOUL AND A MISS") {
    const FrameState pre = red_past_brown(0.0);
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    // The cue ball stops on the same line, so the next player is snookered in turn.
    REQUIRE(no_part_open(rest(played.record, pre.cue_ball), at(pre, Ball::red), all_but(pre, Ball::red)));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.penalty == 4);
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(a)");
    CHECK(calls_text(j.calls) == "foul miss free_ball");
    CHECK(name(j.post.pending) == "after_foul");
    CHECK(j.post.last_miss);
    CHECK(j.post.last_penalty == 4);
    CHECK(j.post.scores[1] == 4);
    CHECK(j.post.stats[0].misses == 1);
    CHECK(j.post.miss_run == 0);
    CHECK(choices_text(j.post, 1) == "play play_again replace");
}

TEST_CASE("S3.14(a)(i): no miss when the striker needed penalty points before the stroke") {
    const Judgement j = open_red_missed_at({0, 36});
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(i)");
    CHECK(calls_text(j.calls) == "foul");
    CHECK_FALSE(j.post.last_miss);
    CHECK(j.post.scores[1] == 40);
    CHECK(choices_text(j.post, 1) == "play play_again");
}

TEST_CASE("S3.14(a)(i): no miss when the other player needed penalty points before the stroke") {
    const Judgement j = open_red_missed_at({36, 0});
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(i)");
    CHECK(choices_text(j.post, 1) == "play play_again");
}

TEST_CASE("S3.14(a)(i): no miss when the stroke's own penalty leaves the striker needing penalty points") {
    // 32 behind with 35 on the table before the stroke, 36 behind after it.
    const Judgement j = open_red_missed_at({0, 32});
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(i)");
    CHECK(j.post.scores[1] == 36);
    CHECK(choices_text(j.post, 1) == "play play_again");
}

TEST_CASE("S3.14(a)(i): still a miss while the penalty leaves the scores able to be levelled") {
    // 35 behind after the stroke with 35 on the table: nobody needs penalty points.
    const Judgement j = open_red_missed_at({0, 31});
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
    CHECK(j.post.scores[1] == 35);
    CHECK(choices_text(j.post, 1) == "play play_again replace");
}

TEST_CASE("S3.14(a)(i): no miss when the stroke, pocketing the last red in the foul, leaves the striker needing them") {
    // 30 behind with 35 on the table. The foul costs 5 and the red it pockets stays down, which
    // leaves 27: the need arises from the stroke, though not from its penalty alone.
    FrameState pre = scenario::frame({ball(Ball::cue, 0.0, 0.45), ball(Ball::blue, 0.0, 0.6),
                                      ball(Ball::red, 0.0, 0.75), spotted(Ball::yellow), spotted(Ball::green),
                                      spotted(Ball::brown), spotted(Ball::pink), spotted(Ball::black)});
    pre.scores = {0, 30};
    const scenario::Played played = scenario::play(pre, scenario::strike(kPi / 2.0, 1.2));
    REQUIRE(first_touched(played.record, pre.cue_ball) == place_of(pre, Ball::blue));
    REQUIRE(pocketed(played.record, place_of(pre, Ball::red)));
    REQUIRE_FALSE(pocketed(played.record, place_of(pre, Ball::blue)));
    REQUIRE_FALSE(pocketed(played.record, pre.cue_ball));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "first_hit_not_on 5");
    CHECK(j.post.scores[1] == 35);
    CHECK(place_of(j.post, Ball::red) == -1);
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(i)");
    CHECK(choices_text(j.post, 1) == "play play_again");
}

TEST_CASE("S3.14(a)(ii): no miss when the ball on cannot be hit and the stroke is played at it") {
    const FrameState pre = walled_in_red();
    REQUIRE(walled_in(pre));
    const scenario::Played played = scenario::play(pre, scenario::strike(0.0, 1.0));
    REQUIRE(first_touched(played.record, pre.cue_ball) == place_of(pre, Ball::yellow));
    REQUIRE(nothing_pocketed(played.record));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "first_hit_not_on 4");
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(ii)");
    CHECK_FALSE(j.post.last_miss);
    CHECK(choices_text(j.post, 1) == "play play_again");
}

TEST_CASE("S3.14(a)(ii): with the ball on impossible to hit, a stroke not played at it is still a miss") {
    // The rulebook takes an attempt for granted only of a stroke played at the ball on with the
    // strength to have reached it. docs/rules/README.md excuses every stroke in such a position.
    const FrameState pre = walled_in_red();
    REQUIRE(walled_in(pre));
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    REQUIRE(dist(rest(played.record, pre.cue_ball), at(pre, Ball::red)) > dist(at(pre, Ball::cue), at(pre, Ball::red)));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK(choices_text(j.post, 1) == "play play_again replace");
}

TEST_CASE("S3.14(b): after FOUL AND A MISS the non-offender may play, pass the position back or have it replaced") {
    const FrameState pre = open_red();
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    const Position left = left_by(pre, played.record);

    const Judgement& j = played.judgement;
    CHECK(j.miss);
    CHECK(name(j.post.pending) == "after_foul");
    CHECK(j.post.decider == 1);
    CHECK(choices_text(j.post, 1) == "play play_again replace");

    FrameState s = j.post;
    if (!choose(s, DecisionKind::play)) return;
    CHECK(name(s.pending) == "none");
    CHECK(s.striker == 1);
    CHECK(name(s.on.kind) == "red");
    CHECK(same_balls(s, left, 1e-9));
}

TEST_CASE("S3.14(b): a foul after a fair first contact is no miss and offers no replacement") {
    const FrameState pre = open_red();
    ShotFacts facts = struck(pre, {place_of(pre, Ball::red)});
    facts.cue_potted = true;
    facts.cue_pocket = 0;

    const Judgement j = judge(pre, {}, facts, oracle(), ref());
    CHECK(fouls_text(j) == "in_off 4");
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "");
    CHECK_FALSE(scenario::has_call(j.calls, CallKind::miss));
    CHECK_FALSE(j.post.last_miss);
    CHECK(choices_text(j.post, 1) == "play play_again");
}

TEST_CASE("S3.14(b): asked to play again from the position left, the offender plays the balls as they lie") {
    const FrameState pre = open_red();
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    const Position left = left_by(pre, played.record);

    FrameState s = played.judgement.post;
    if (!choose(s, DecisionKind::play_again)) return;
    CHECK(name(s.pending) == "none");
    CHECK(s.striker == 0);
    CHECK(name(s.on.kind) == "red");
    CHECK(same_balls(s, left, 1e-9));
    CHECK(s.scores[1] == 4);
}

TEST_CASE("S3.14(b)(i): replaced with red on, every ball is back where it lay and the penalty stands") {
    const FrameState pre = scenario::frame({ball(Ball::cue, 0.0, 0.55), ball(Ball::blue, 0.0, 0.7),
                                            ball(Ball::red, 0.5, 0.3), spotted(Ball::yellow), spotted(Ball::green),
                                            spotted(Ball::brown), spotted(Ball::pink), spotted(Ball::black)});
    REQUIRE(both_edges_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));
    // Straight into the blue, which drops into the middle pocket behind it.
    const scenario::Played played = scenario::play(pre, scenario::strike(kPi / 2.0, 1.2));
    const int blue = place_of(pre, Ball::blue);
    REQUIRE(first_touched(played.record, pre.cue_ball) == blue);
    REQUIRE(pocketed(played.record, blue));
    REQUIRE_FALSE(pocketed(played.record, pre.cue_ball));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "pot_not_on 5, first_hit_not_on 5");
    CHECK(j.penalty == 5);
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
    CHECK(lies_at(j.post, Ball::blue, table::spot(Ball::blue)));
    CHECK(lies_at(j.post, Ball::cue, rest(played.record, pre.cue_ball), 1e-9));

    FrameState s = j.post;
    if (!choose(s, DecisionKind::replace)) return;
    CHECK(same_balls(s, pre));
    CHECK(name(s.pending) == "none");
    CHECK(s.striker == 0);
    CHECK(name(s.on.kind) == "red");
    CHECK_FALSE(s.cue_in_hand);
    CHECK_FALSE(s.free_ball);
    CHECK(s.scores[0] == 0);
    CHECK(s.scores[1] == 5);
}

TEST_CASE("S3.14(b): a red pocketed in the stroke goes back with the rest") {
    const FrameState pre = scenario::frame({ball(Ball::cue, 0.0, 0.45), ball(Ball::blue, 0.0, 0.6),
                                            ball(Ball::red, 0.0, 0.75), spotted(Ball::yellow), spotted(Ball::green),
                                            spotted(Ball::brown), spotted(Ball::pink), spotted(Ball::black)});
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));
    // Into the blue, which knocks the red in and stays up.
    const scenario::Played played = scenario::play(pre, scenario::strike(kPi / 2.0, 1.2));
    const int blue = place_of(pre, Ball::blue);
    REQUIRE(first_touched(played.record, pre.cue_ball) == blue);
    REQUIRE(pocketed(played.record, place_of(pre, Ball::red)));
    REQUIRE_FALSE(pocketed(played.record, blue));
    REQUIRE_FALSE(pocketed(played.record, pre.cue_ball));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "first_hit_not_on 5");
    CHECK(j.points == 0);
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(a)");
    // In the position left the red stays down and the colours are on in order.
    CHECK(place_of(j.post, Ball::red) == -1);
    CHECK(name(j.post.on.kind) == "colour");
    CHECK(name(j.post.on.colour) == "yellow");

    FrameState s = j.post;
    if (!choose(s, DecisionKind::replace)) return;
    CHECK(same_balls(s, pre));
    CHECK(s.striker == 0);
    CHECK(name(s.on.kind) == "red");
    CHECK(s.scores[1] == 5);
}

TEST_CASE("S3.14(b)(ii): replaced in the colours, the same colour is on") {
    const FrameState pre = scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::yellow, 0.5, kLine),
                                            spotted(Ball::green), spotted(Ball::brown), spotted(Ball::blue),
                                            spotted(Ball::pink), spotted(Ball::black)});
    REQUIRE(both_edges_open(at(pre, Ball::cue), at(pre, Ball::yellow), all_but(pre, Ball::yellow)));
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");

    FrameState s = j.post;
    if (!choose(s, DecisionKind::replace)) return;
    CHECK(same_balls(s, pre));
    CHECK(s.striker == 0);
    CHECK(name(s.on.kind) == "colour");
    CHECK(name(s.on.colour) == "yellow");
    CHECK(s.scores[1] == 4);
}

TEST_CASE("S3.14(b)(iii) with S3.13(a)(ii): replaced with a colour of choice, the offender may choose another") {
    const FrameState pre = colour_of_choice();
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::black), {at(pre, Ball::red)}));
    const scenario::Played played = scenario::play(pre, roll_away(), nominate(Ball::black));
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);

    const int blue = place_of(pre, Ball::blue);
    const phys::CueStrike at_the_blue = scenario::strike(scenario::aim_at_ball(pre, blue), 0.4);
    const phys::ShotRecord dry = scenario::simulate(pre, at_the_blue);
    REQUIRE(first_touched(dry, pre.cue_ball) == blue);
    REQUIRE(nothing_pocketed(dry));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 7");
    CHECK(j.miss);

    FrameState s = j.post;
    if (!choose(s, DecisionKind::replace)) return;
    CHECK(same_balls(s, pre));
    CHECK(s.striker == 0);
    CHECK(name(s.on.kind) == "any_colour");
    CHECK(nominations_text(s) == "yellow green brown blue pink black");
    CHECK(s.scores[1] == 7);
    if (!lies_as(s, pre)) return;

    const Judgement again = scenario::play(s, at_the_blue, nominate(Ball::blue)).judgement;
    CHECK(fouls_text(again) == "");
    CHECK(name(again.nominated.nominated) == "blue");
    CHECK(name(again.post.pending) == "none");
    CHECK(again.post.striker == 1);
    CHECK(name(again.post.on.kind) == "red");
}

TEST_CASE("S3.14(b): a stroke from in hand is replaced in hand") {
    FrameState pre = scenario::frame({ball(Ball::cue, -1.2, 0.15), ball(Ball::red, 0.5, kLine), spotted(Ball::black)});
    pre.cue_in_hand = true;
    REQUIRE(table::in_d(at(pre, Ball::cue)));
    REQUIRE(both_edges_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));
    const scenario::Played played = scenario::play(pre, scenario::strike(0.0, 0.2));
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    REQUIRE_FALSE(pocketed(played.record, pre.cue_ball));
    const Vec2 elsewhere{table::baulk_line_x() - 0.1, -0.1};
    REQUIRE(table::in_d(elsewhere));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK_FALSE(j.post.cue_in_hand);

    FrameState s = j.post;
    if (!choose(s, DecisionKind::replace)) return;
    CHECK(same_balls(s, pre));
    CHECK(s.striker == 0);
    CHECK(s.cue_in_hand);
    // In hand again, the offender may start from anywhere in the D.
    std::string error;
    CHECK(place_cue_ball(s, elsewhere, oracle(), error));
    CHECK(lies_at(s, Ball::cue, elsewhere));
}

TEST_CASE("S3.14(b): the free ball the offender had comes back with the position") {
    FrameState pre = red_past_brown(0.0);
    pre.free_ball = true;
    // The brown is nominated and missed, the cue ball rolling out from behind it.
    const scenario::Played played = scenario::play(pre, roll_aside(), nominate(Ball::brown));
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    REQUIRE(both_edges_open(rest(played.record, pre.cue_ball), at(pre, Ball::red), all_but(pre, Ball::red)));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK_FALSE(j.post.free_ball);

    FrameState s = j.post;
    if (!choose(s, DecisionKind::replace)) return;
    CHECK(same_balls(s, pre));
    CHECK(s.striker == 0);
    CHECK(s.free_ball);
    CHECK(nominations_text(s) == "yellow green brown blue pink black");
}

TEST_CASE("S3.14(c): a clear path to the ball on makes a failure FOUL AND A MISS") {
    const FrameState pre = open_red();
    REQUIRE(both_edges_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    REQUIRE(both_edges_open(rest(played.record, pre.cue_ball), at(pre, Ball::red), all_but(pre, Ball::red)));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
    CHECK(calls_text(j.calls) == "foul miss");
    CHECK(j.post.last_miss);
    CHECK(j.post.stats[0].misses == 1);
}

TEST_CASE("S3.14(c): a clear path to any part of the ball on is enough") {
    const FrameState pre = red_past_brown(0.04);
    const std::vector<Vec2> colours = all_but(pre, Ball::red);
    REQUIRE_FALSE(open(at(pre, Ball::cue), at(pre, Ball::red), colours, 0.0));
    REQUIRE(open(at(pre, Ball::cue), at(pre, Ball::red), colours, 1.0) !=
            open(at(pre, Ball::cue), at(pre, Ball::red), colours, -1.0));
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
    CHECK(j.post.miss_run == 0);
}

TEST_CASE("S3.14(c): a clear path to a ball that could be on counts") {
    // The declared black is hidden; the blue, which could have been declared, is not.
    const FrameState pre = colour_of_choice();
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::black), {at(pre, Ball::red)}));
    REQUIRE(both_edges_open(at(pre, Ball::cue), at(pre, Ball::blue), all_but(pre, Ball::blue)));
    const scenario::Played played = scenario::play(pre, roll_away(), nominate(Ball::black));
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 7");
    CHECK(j.penalty == 7);
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
    CHECK(j.post.scores[1] == 7);
}

TEST_CASE("S3.14(c): with a free ball every object ball could be on") {
    // The red is wholly hidden, as in the S3.14(a) scenario, but the brown that hides it may be
    // nominated and stands in full view.
    FrameState pre = red_past_brown(0.0);
    pre.free_ball = true;
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));
    REQUIRE(open(at(pre, Ball::cue), at(pre, Ball::brown), all_but(pre, Ball::brown), 0.0));
    // The cue ball rolls out from behind the brown, so no snooker behind the free ball is left.
    const scenario::Played played = scenario::play(pre, roll_aside(), nominate(Ball::brown));
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    REQUIRE(both_edges_open(rest(played.record, pre.cue_ball), at(pre, Ball::red), all_but(pre, Ball::red)));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
    CHECK(j.post.miss_run == 1);
}

TEST_CASE("S3.14(d): a miss with central full-ball contact open begins the run") {
    const FrameState pre = open_red();
    REQUIRE(open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red), 0.0));
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);

    const Judgement& j = played.judgement;
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
    CHECK(j.post.miss_run == 1);
    CHECK_FALSE(j.post.miss_warned);

    FrameState s = j.post;
    std::vector<Call> said;
    if (!choose(s, DecisionKind::replace, &said)) return;
    CHECK(s.miss_run == 1);
    CHECK_FALSE(s.miss_warned);
    CHECK_FALSE(scenario::has_call(said, CallKind::warning));
}

TEST_CASE("S3.14(d): a red masked only by another red counts as open to a full-ball contact") {
    const FrameState pre = two_reds({-0.25, kLine + 0.06});
    const Vec2 cue = at(pre, Ball::cue);
    const std::vector<Vec2> colours = all_but(pre, Ball::red);
    // The brown covers the middle of the near red and stands clear of the line to the far one,
    // which only the near red is on.
    REQUIRE_FALSE(open(cue, at(pre, Ball::red, 1), colours, 0.0));
    REQUIRE(open(cue, at(pre, Ball::red, 0), colours, 0.0));
    REQUIRE(blocks(at(pre, Ball::red, 1), cue, meeting(cue, at(pre, Ball::red, 0), 0.0)));
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);

    const Judgement& j = played.judgement;
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
    CHECK(j.post.miss_run == 1);
}

TEST_CASE("S3.14(d): no run when a colour covers the middle of every red") {
    const FrameState pre = two_reds({-0.25, kLine - 0.03});
    const Vec2 cue = at(pre, Ball::cue);
    const std::vector<Vec2> colours = all_but(pre, Ball::red);
    REQUIRE_FALSE(open(cue, at(pre, Ball::red, 0), colours, 0.0));
    REQUIRE_FALSE(open(cue, at(pre, Ball::red, 1), colours, 0.0));
    REQUIRE_FALSE(no_part_open(cue, at(pre, Ball::red, 1), colours));
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);

    const Judgement& j = played.judgement;
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
    CHECK(j.post.miss_run == 0);
}

TEST_CASE("S3.14(d): a miss with the cue ball touching a ball that could be on begins the run") {
    // A colour of choice is on and the cue ball touches the pink. The striker declares the black,
    // which is hidden behind the last red, and plays away from the pink without reaching anything.
    FrameState pre = scenario::frame({ball(Ball::cue, 0.0, kLine), ball(Ball::pink, 2.0 * kR, kLine),
                                      ball(Ball::red, -0.3, kLine), ball(Ball::black, -0.6, kLine)});
    pre.on = BallOn{OnKind::any_colour, Ball::yellow};
    pre.scores = {1, 0};
    pre.break_points = 1;
    const int pink = place_of(pre, Ball::pink);
    pre.touching = {pink};
    REQUIRE(dist(at(pre, Ball::cue), at(pre, Ball::pink)) - 2.0 * kR < ref().touching_gap_m);
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::black), {at(pre, Ball::red)}));
    const scenario::Played played = scenario::play(pre, roll_away(), nominate(Ball::black));
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    REQUIRE(stayed(played.record, pre, pink));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 7");
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
    CHECK(j.post.miss_run == 1);
}

TEST_CASE("S3.14(d)(i): the second failure from the original position is a miss whatever the scores") {
    FrameState pre = open_red();
    pre.scores = {0, 28};
    REQUIRE(open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red), 0.0));
    const scenario::Played first = scenario::play(pre, roll_away());
    REQUIRE(first_touched(first.record, pre.cue_ball) == -1);
    CHECK(first.judgement.miss);
    CHECK(first.judgement.miss_reason == "S3.14(c)");
    CHECK(first.judgement.post.miss_run == 1);

    FrameState s = first.judgement.post;
    if (!choose(s, DecisionKind::replace) || !lies_as(s, pre)) return;
    // 32 behind before this stroke and 36 after it, with 35 on the table: but for the run, no miss.
    const Judgement second = scenario::play(s, roll_away()).judgement;
    CHECK(fouls_text(second) == "no_hit 4");
    CHECK(second.miss);
    CHECK(second.miss_reason == "S3.14(d)");
    CHECK(second.post.scores[1] == 36);
    CHECK(second.post.miss_run == 2);
    CHECK(choices_text(second.post, 1) == "play play_again replace");
}

TEST_CASE("S3.14(d)(i): outside a run the scores are still respected") {
    // The same scores and strokes with only an edge of the red showing: no run, so the second
    // failure falls under S3.14(a)(i).
    FrameState pre = red_past_brown(0.04);
    pre.scores = {0, 28};
    REQUIRE_FALSE(open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red), 0.0));
    REQUIRE_FALSE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));
    const scenario::Played first = scenario::play(pre, roll_away());
    REQUIRE(first_touched(first.record, pre.cue_ball) == -1);
    CHECK(first.judgement.miss);
    CHECK(first.judgement.post.miss_run == 0);

    FrameState s = first.judgement.post;
    if (!choose(s, DecisionKind::replace) || !lies_as(s, pre)) return;
    const Judgement second = scenario::play(s, roll_away()).judgement;
    CHECK(fouls_text(second) == "no_hit 4");
    CHECK_FALSE(second.miss);
    CHECK(second.miss_reason == "S3.14(a)(i)");
    CHECK(second.post.scores[1] == 36);
    CHECK(choices_text(second.post, 1) == "play play_again");
}

TEST_CASE("S3.14(d)(ii): warned on the second replacement, the frame goes on the third failure") {
    const FrameState pre = open_red();
    REQUIRE(open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red), 0.0));
    const scenario::Played first = scenario::play(pre, roll_away());
    REQUIRE(first_touched(first.record, pre.cue_ball) == -1);
    CHECK(first.judgement.miss);
    CHECK(first.judgement.post.miss_run == 1);

    FrameState s = first.judgement.post;
    std::vector<Call> said;
    if (!choose(s, DecisionKind::replace, &said) || !lies_as(s, pre)) return;
    CHECK_FALSE(scenario::has_call(said, CallKind::warning));
    CHECK_FALSE(s.miss_warned);

    const Judgement second = scenario::play(s, roll_away()).judgement;
    CHECK(second.miss);
    CHECK(calls_text(second.calls) == "foul miss");
    CHECK(second.post.miss_run == 2);
    CHECK_FALSE(second.post.over);

    s = second.post;
    if (!choose(s, DecisionKind::replace, &said) || !lies_as(s, pre)) return;
    CHECK(scenario::has_call(said, CallKind::warning));
    CHECK(s.miss_warned);
    CHECK(s.miss_run == 2);
    CHECK(s.striker == 0);

    const Judgement third = scenario::play(s, roll_away()).judgement;
    check_frame_lost_on_three_misses(third, 1);
}

TEST_CASE("S3.14(d)(ii): a third failure with the warning standing loses the frame") {
    FrameState pre = open_red();
    pre.scores = {0, 8};
    pre.miss_run = 2;
    pre.miss_warned = true;
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    check_frame_lost_on_three_misses(played.judgement, 1);
}

TEST_CASE("S3.14(d)(ii): no frame is awarded without the warning, which comes at the next replacement") {
    // The engine's referee always warns on the second replacement, so this state only arises if
    // it is handed one; the rulebook still says what to do with it.
    FrameState pre = open_red();
    pre.scores = {0, 8};
    pre.miss_run = 2;
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);

    const Judgement& j = played.judgement;
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(d)");
    CHECK_FALSE(j.post.over);
    CHECK(name(j.post.pending) == "after_foul");
    CHECK(choices_text(j.post, 1) == "play play_again replace");

    FrameState s = j.post;
    std::vector<Call> said;
    if (!choose(s, DecisionKind::replace, &said) || !lies_as(s, pre)) return;
    CHECK(scenario::has_call(said, CallKind::warning));
    CHECK(s.miss_warned);
    check_frame_lost_on_three_misses(scenario::play(s, roll_away()).judgement, 1);
}

TEST_CASE("S3.14(d)(iii): asked to play from the position left, the run ends") {
    const FrameState pre = open_red();
    const scenario::Played first = scenario::play(pre, roll_away());
    REQUIRE(first_touched(first.record, pre.cue_ball) == -1);
    const Position left = left_by(pre, first.record);
    REQUIRE(open(at(left, Ball::cue), at(left, Ball::red), all_but(left, Ball::red), 0.0));
    REQUIRE(first_touched(scenario::simulate(left, roll_away()), left.cue_ball) == -1);

    FrameState s = first.judgement.post;
    if (!choose(s, DecisionKind::replace) || !lies_as(s, pre)) return;
    const Judgement second = scenario::play(s, roll_away()).judgement;
    CHECK(second.miss);
    CHECK(second.post.miss_run == 2);

    s = second.post;
    if (!choose(s, DecisionKind::play_again)) return;
    CHECK(s.striker == 0);
    CHECK(s.miss_run == 0);
    CHECK_FALSE(s.miss_warned);
    if (!lies_as(s, left)) return;

    // The red is as open as before, so a failure here is a miss again, but the first of a new run.
    const Judgement third = scenario::play(s, roll_away()).judgement;
    CHECK(third.miss);
    CHECK(third.miss_reason == "S3.14(c)");
    CHECK(third.post.miss_run == 1);
    CHECK_FALSE(third.post.over);
}

TEST_CASE("S3.14(d): the non-offender playing on ends the run") {
    const FrameState pre = open_red();
    const scenario::Played first = scenario::play(pre, roll_away());
    REQUIRE(first_touched(first.record, pre.cue_ball) == -1);
    CHECK(first.judgement.post.miss_run == 1);

    FrameState s = first.judgement.post;
    if (!choose(s, DecisionKind::play)) return;
    CHECK(s.striker == 1);
    CHECK(s.miss_run == 0);
    CHECK_FALSE(s.miss_warned);
}

TEST_CASE("S3.14(d): hitting a ball on from the replaced position ends the run and the warning") {
    FrameState pre = open_red();
    pre.scores = {0, 8};
    pre.miss_run = 2;
    pre.miss_warned = true;
    const scenario::Played played = scenario::play(pre, scenario::strike(0.0, 0.5));
    REQUIRE(first_touched(played.record, pre.cue_ball) == place_of(pre, Ball::red));
    REQUIRE(nothing_pocketed(played.record));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "");
    CHECK_FALSE(j.post.over);
    CHECK(name(j.post.pending) == "none");
    CHECK(j.post.striker == 1);
    CHECK(j.post.miss_run == 0);
    CHECK_FALSE(j.post.miss_warned);
}

TEST_CASE("S3.14(d): a foul after a fair first contact ends the run") {
    FrameState pre = open_red();
    pre.scores = {0, 4};
    pre.miss_run = 1;
    ShotFacts facts = struck(pre, {place_of(pre, Ball::red)});
    facts.cue_potted = true;
    facts.cue_pocket = 0;

    const Judgement j = judge(pre, {}, facts, oracle(), ref());
    CHECK(fouls_text(j) == "in_off 4");
    CHECK_FALSE(j.miss);
    CHECK(j.post.miss_run == 0);
    CHECK_FALSE(j.post.miss_warned);
    CHECK(choices_text(j.post, 1) == "play play_again");
}

TEST_CASE("S3.14(a)(i): with a colour of choice on, the black it may bring counts only for the striker") {
    // Player 0 has just potted the last red: a colour of choice and then the six colours are 34
    // points for player 0, but only the colours' 27 for player 1, who at 30 behind already needs
    // penalty points.
    FrameState pre = scenario::frame({ball(Ball::cue, -0.5, kLine), spotted(Ball::yellow), spotted(Ball::green),
                                      spotted(Ball::brown), spotted(Ball::blue), spotted(Ball::pink),
                                      spotted(Ball::black)});
    pre.on = BallOn{OnKind::any_colour, Ball::yellow};
    pre.scores = {60, 30};
    REQUIRE(both_edges_open(at(pre, Ball::cue), at(pre, Ball::blue), all_but(pre, Ball::blue)));

    // The blue declared and nothing hit, though the blue is in plain view.
    const Judgement j = judge(pre, nominate(Ball::blue), scenario::quiet_facts(pre), oracle(), ref());
    CHECK(fouls_text(j) == "no_hit 5");
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(i)");
    CHECK_FALSE(scenario::has_call(j.calls, CallKind::miss));
    CHECK(choices_text(j.post, 1) == "play play_again");

    // Three points nearer, player 1 needs none before or after, and the same stroke is a miss.
    pre.scores = {60, 33};
    const Judgement k = judge(pre, nominate(Ball::blue), scenario::quiet_facts(pre), oracle(), ref());
    CHECK(k.miss);
    CHECK(k.miss_reason == "S3.14(c)");
}

TEST_CASE("S3.14(d): with a free ball, a ball not on in plain view begins the run") {
    // The only red hidden behind the brown, so FREE BALL; the blue, which may be nominated, lies in
    // the open with its full face to the cue ball.
    FrameState pre = red_past_brown(0.0);
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));
    REQUIRE(open(at(pre, Ball::cue), at(pre, Ball::blue), all_but(pre, Ball::blue), 0.0));
    pre.free_ball = true;

    const Judgement j = judge(pre, nominate(Ball::blue), scenario::quiet_facts(pre), oracle(), ref());
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK(j.post.miss_run == 1);
    FrameState s = j.post;
    if (!choose(s, DecisionKind::replace)) return;
    CHECK(s.free_ball);
    if (!lies_as(s, pre)) return;

    // The second failure from the original position is a miss whatever the scores.
    const Judgement again = judge(s, nominate(Ball::blue), scenario::quiet_facts(s), oracle(), ref());
    CHECK(again.miss);
    CHECK(again.miss_reason == "S3.14(d)");
    CHECK(again.post.miss_run == 2);

    // Without the free ball the red alone could be on, and no full ball of it is there to hit.
    FrameState plain = red_past_brown(0.0);
    const Judgement k = judge(plain, {}, scenario::quiet_facts(plain), oracle(), ref());
    CHECK(k.miss);
    CHECK(k.post.miss_run == 0);
}
