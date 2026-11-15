// S3.12 (snookered after a foul: the free ball) and S3.13 (play again), one scenario per clause,
// written from the rulebook and docs/rules/README.md, never from the engine. REQUIRE states a
// premise, read from the raw shot record or worked out by hand here; CHECK states what the rule
// then demands.

#include <doctest/doctest.h>

#include <algorithm>
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
// The snookers are built along this line, clear of every spot.
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

std::vector<Vec2> colours_of(const Position& p) {
    std::vector<Vec2> out;
    for (const BallOnTable& b : p.balls) {
        if (b.ball != Ball::cue && b.ball != Ball::red) out.push_back(b.pos);
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

// S2.17 by hand: the straight stroke from `cue` to that part of `target` passes every ball of `others`.
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

// S2.17(a) by hand: from every point of the D, taken 2 cm apart, an edge of `target` is covered.
bool hidden_from_the_d(Vec2 target, const std::vector<Vec2>& others) {
    for (int i = 0; i <= 15; ++i) {
        for (int k = -15; k <= 15; ++k) {
            const Vec2 p{table::baulk_line_x() - 0.02 * i, 0.02 * k};
            if (table::in_d(p) && both_edges_open(p, target, others)) return false;
        }
    }
    return true;
}

std::string name(Pending p) { return std::string(pending_name(p)); }
std::string name(OnKind k) { return std::string(on_kind_name(k)); }
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

bool offers(const FrameState& s, int player, DecisionKind kind) {
    const std::vector<DecisionKind> all = options(s, player);
    return std::find(all.begin(), all.end(), kind) != all.end();
}

std::string nominations_text(const FrameState& s) {
    std::string out;
    for (Ball b : nominations(s)) {
        if (!out.empty()) out += " ";
        out += std::string(table::ball_name(b));
    }
    return out;
}

std::string spotted_text(const Judgement& j) {
    std::string out;
    for (const BallOnTable& b : j.spotted) {
        if (!out.empty()) out += " ";
        out += std::string(table::ball_name(b.ball));
    }
    return out;
}

// `first` called, and `then` called after it.
bool in_order(const std::vector<Call>& calls, CallKind first, CallKind then) {
    const auto a = std::find_if(calls.begin(), calls.end(), [&](const Call& c) { return c.kind == first; });
    return a != calls.end() && std::any_of(a, calls.end(), [&](const Call& c) { return c.kind == then; });
}

// The decider's choice. False, after failing a check, when the engine refuses it.
bool choose(FrameState& s, DecisionKind kind) {
    std::vector<Call> calls;
    std::string error;
    const bool ok = decide(s, {kind, s.decider}, oracle(), ref(), calls, error);
    CHECK_MESSAGE(ok, std::string(decision_name(kind)), " refused: ", error);
    return ok;
}

// Whether the next stroke can be played from `s` with the balls where the raw record left them.
// When it cannot, a check has failed and the scenario stops there.
bool lies_as(const FrameState& s, const Position& expected) {
    const bool ok = !s.over && s.pending == Pending::none && same_balls(s, expected, 1e-9);
    CHECK_MESSAGE(ok, "the frame is not waiting for a stroke with the balls where the last one left them");
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

// One red with the brown between it and the cue ball; the black well away.
FrameState behind_the_brown(Vec2 brown = {0.2, kLine}, Vec2 red = {0.5, kLine}) {
    return scenario::frame({ball(Ball::cue, -0.2, kLine), ball(Ball::brown, brown.x, brown.y),
                            ball(Ball::red, red.x, red.y), ball(Ball::black, -0.6, 0.2)});
}

// A soft stroke straight away from the brown: the cue ball rolls a hand's width back along the
// line and touches nothing.
phys::CueStrike roll_away() { return scenario::strike(kPi, 0.15); }

// The cue ball close behind `free`, with `on` beyond it on the same line, and FREE BALL called.
FrameState tucked_behind(Ball free, Ball on) {
    FrameState s = scenario::frame({ball(Ball::cue, -0.1, kLine), ball(free, 0.0, kLine), ball(on, 0.8, kLine)});
    s.free_ball = true;
    return s;
}

phys::CueStrike roll_up() { return scenario::strike(0.0, 0.2); }

// The premise of every roll-up: `free` hit first, nothing pocketed, `on` unmoved, and the cue ball
// at rest with no part of `on` to be hit past `free`.
void require_left_behind(const phys::ShotRecord& r, const Position& pre, Ball free, Ball on) {
    REQUIRE(first_touched(r, pre.cue_ball) == place_of(pre, free));
    REQUIRE(nothing_pocketed(r));
    REQUIRE(stayed(r, pre, place_of(pre, on)));
    REQUIRE(no_part_open(rest(r, pre.cue_ball), at(pre, on), {rest(r, place_of(pre, free))}));
}

// The last red up the table with the blue between it and baulk; the cue ball near the baulk
// cushion, about to be played into the corner pocket beside it.
FrameState before_the_in_off(Vec2 blue) {
    return scenario::frame({ball(Ball::cue, -1.3, 0.5), ball(Ball::red, 1.2, 0.0), ball(Ball::blue, blue.x, blue.y),
                            spotted(Ball::black)});
}

phys::CueStrike into_the_baulk_pocket(const Position& p) {
    for (const phys::Pocket& pocket : scenario::rig().table.pockets) {
        if (pocket.name == "bottom_left") return scenario::strike(scenario::aim_at(p, pocket.drop_centre), 1.0);
    }
    FAIL("the table has no bottom_left pocket");
    return {};
}

// A free ball on the line into the left middle pocket, the cue ball behind it.
phys::CueStrike up_the_line(double speed) { return scenario::strike(kPi / 2.0, speed); }

}  // namespace

TEST_CASE("S3.12: a foul that leaves the next player snookered on the only red brings FREE BALL") {
    const FrameState pre = behind_the_brown();
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    REQUIRE(no_part_open(rest(played.record, pre.cue_ball), at(pre, Ball::red), colours_of(pre)));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.penalty == 4);
    CHECK(j.post.scores[0] == 0);
    CHECK(j.post.scores[1] == 4);
    CHECK(name(j.post.pending) == "after_foul");
    CHECK(j.post.decider == 1);
    CHECK(j.post.striker == 1);
    CHECK(j.post.offender == 0);
    CHECK(j.post.free_ball);
    CHECK(in_order(j.calls, CallKind::foul, CallKind::free_ball));
}

TEST_CASE("S3.12: no FREE BALL when the ball on can be hit on both edges") {
    const FrameState pre = behind_the_brown({0.2, kLine}, {0.5, 0.8});
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    REQUIRE(both_edges_open(rest(played.record, pre.cue_ball), at(pre, Ball::red), colours_of(pre)));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(name(j.post.pending) == "after_foul");
    CHECK_FALSE(j.post.free_ball);
    CHECK_FALSE(scenario::has_call(j.calls, CallKind::free_ball));
}

TEST_CASE("S3.12: one covered edge of the ball on is a snooker, and brings FREE BALL") {
    // The brown sits 6 cm off the line: the middle of the red can be hit, one edge cannot (S2.17).
    const FrameState pre = behind_the_brown({0.2, kLine + 0.06});
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    const Vec2 cue = rest(played.record, pre.cue_ball);
    const Vec2 red = at(pre, Ball::red);
    REQUIRE(open(cue, red, colours_of(pre), 0.0));
    REQUIRE(open(cue, red, colours_of(pre), 1.0) != open(cue, red, colours_of(pre), -1.0));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.post.free_ball);
    CHECK(in_order(j.calls, CallKind::foul, CallKind::free_ball));
}

TEST_CASE("S3.12: no FREE BALL while another red is in the open") {
    FrameState pre = behind_the_brown();
    pre.balls.push_back(ball(Ball::red, -0.6, 0.8, 1));
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    const Vec2 cue = rest(played.record, pre.cue_ball);
    REQUIRE(no_part_open(cue, at(pre, Ball::red, 0), colours_of(pre)));
    REQUIRE(both_edges_open(cue, at(pre, Ball::red, 1), colours_of(pre)));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK_FALSE(j.post.free_ball);
    CHECK_FALSE(scenario::has_call(j.calls, CallKind::free_ball));
}

TEST_CASE("S3.12 with S2.17(a): in hand after an in-off, FREE BALL when the red is hidden from the whole D") {
    const FrameState pre = before_the_in_off({0.7, 0.0});
    const scenario::Played played = scenario::play(pre, into_the_baulk_pocket(pre));
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    REQUIRE(pocketed(played.record, pre.cue_ball));
    REQUIRE(hidden_from_the_d(at(pre, Ball::red), colours_of(pre)));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4, in_off 4");
    CHECK(j.penalty == 4);
    CHECK(j.post.cue_in_hand);
    CHECK(j.post.free_ball);
    CHECK(in_order(j.calls, CallKind::foul, CallKind::free_ball));
}

TEST_CASE("S3.12 with S2.17(a): in hand, no FREE BALL when some of the D sees the red") {
    // The blue hides the red from the middle of the D, and from where the cue ball is put by
    // default, but not from the D's right-hand side.
    const FrameState pre = before_the_in_off({0.7, 0.05});
    const scenario::Played played = scenario::play(pre, into_the_baulk_pocket(pre));
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    REQUIRE(pocketed(played.record, pre.cue_ball));
    REQUIRE_FALSE(both_edges_open(table::d_centre(), at(pre, Ball::red), colours_of(pre)));
    REQUIRE_FALSE(both_edges_open(table::default_cue_ball_position(), at(pre, Ball::red), colours_of(pre)));
    REQUIRE_FALSE(hidden_from_the_d(at(pre, Ball::red), colours_of(pre)));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4, in_off 4");
    CHECK(j.post.cue_in_hand);
    CHECK_FALSE(j.post.free_ball);
    CHECK_FALSE(scenario::has_call(j.calls, CallKind::free_ball));
}

TEST_CASE("S3.12(a)(i): electing to play, the next player may nominate any ball that is not on") {
    const FrameState pre = behind_the_brown();
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
    REQUIRE(no_part_open(rest(played.record, pre.cue_ball), at(pre, Ball::red), colours_of(pre)));

    FrameState s = played.judgement.post;
    if (!choose(s, DecisionKind::play)) return;
    CHECK(name(s.pending) == "none");
    CHECK(s.striker == 1);
    CHECK(s.free_ball);
    CHECK(nominations_text(s) == "brown black");
}

TEST_CASE("S3.12(a)(i): in the colours, the colour on is not among the free balls") {
    FrameState s = scenario::frame({ball(Ball::cue, -0.2, kLine), ball(Ball::green, 0.2, kLine),
                                    ball(Ball::yellow, 0.5, kLine), spotted(Ball::brown), spotted(Ball::blue),
                                    spotted(Ball::pink), spotted(Ball::black)});
    REQUIRE(no_part_open(at(s, Ball::cue), at(s, Ball::yellow), {at(s, Ball::green)}));
    s.free_ball = true;
    CHECK(nominations_text(s) == "green brown blue pink black");
}

TEST_CASE("S3.12(a)(i): with nothing declared, the ball hit first is taken as the free ball") {
    FrameState pre = behind_the_brown();
    pre.free_ball = true;
    ShotFacts facts = struck(pre, {place_of(pre, Ball::brown)});
    const Vec2 cue{0.35, 0.8};
    REQUIRE(both_edges_open(cue, at(pre, Ball::red), colours_of(pre)));
    facts.rest[std::size_t(pre.cue_ball)] = cue;

    const Judgement j = judge(pre, {}, facts, oracle(), ref());
    CHECK(fouls_text(j) == "");
    CHECK(j.nominated.has_nomination);
    CHECK(name(j.nominated.nominated) == "brown");
    CHECK(j.free_ball_used);
    CHECK(j.post.striker == 1);
}

TEST_CASE("S3.12(a)(ii): the nominated free ball is played as the ball on") {
    const FrameState pre = behind_the_brown();
    const scenario::Played foul = scenario::play(pre, roll_away());
    REQUIRE(first_touched(foul.record, pre.cue_ball) == -1);
    REQUIRE(no_part_open(rest(foul.record, pre.cue_ball), at(pre, Ball::red), colours_of(pre)));

    const Position left = left_by(pre, foul.record);
    const int black = place_of(left, Ball::black);
    const phys::CueStrike at_the_black = scenario::strike(scenario::aim_at_ball(left, black), 0.4);
    const phys::ShotRecord dry = scenario::simulate(left, at_the_black);
    REQUIRE(first_touched(dry, left.cue_ball) == black);
    REQUIRE(nothing_pocketed(dry));
    // The black ends nowhere near the line to the red: it leaves no snooker of its own.
    REQUIRE(both_edges_open(rest(dry, left.cue_ball), at(left, Ball::red), {rest(dry, black)}));

    FrameState s = foul.judgement.post;
    if (!choose(s, DecisionKind::play) || !lies_as(s, left)) return;
    const Judgement j = scenario::play(s, at_the_black, nominate(Ball::black)).judgement;
    CHECK(fouls_text(j) == "");
    CHECK(j.nominated.has_nomination);
    CHECK(name(j.nominated.nominated) == "black");
    CHECK(j.free_ball_used);
    CHECK(j.points == 0);
    CHECK(name(j.post.pending) == "none");
    CHECK(j.post.striker == 0);
    CHECK(name(j.post.on.kind) == "red");
    CHECK_FALSE(j.post.free_ball);
}

TEST_CASE("S3.12(a)(ii): a foul with a free ball nominated costs the value of the ball on, not the free ball's") {
    FrameState pre = scenario::frame({ball(Ball::cue, -0.2, kLine), ball(Ball::black, 0.2, kLine),
                                      ball(Ball::pink, 0.5, kLine)});
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::pink), {at(pre, Ball::black)}));
    pre.free_ball = true;

    // The black is hit first and the cue ball then runs into a pocket.
    ShotFacts facts = struck(pre, {place_of(pre, Ball::black)});
    facts.cue_potted = true;
    facts.cue_pocket = 0;
    const Judgement j = judge(pre, nominate(Ball::black), facts, oracle(), ref());
    CHECK(fouls_text(j) == "in_off 6");
    CHECK(j.penalty == 6);
}

TEST_CASE("S3.12(b)(i): another colour hit before the nominated free ball is a foul") {
    // The pink hides the red here. The next player nominates the black and plays into the pink.
    FrameState pre = behind_the_brown();
    pre.balls[std::size_t(place_of(pre, Ball::brown))].ball = Ball::pink;
    const scenario::Played foul = scenario::play(pre, roll_away());
    REQUIRE(first_touched(foul.record, pre.cue_ball) == -1);

    const Position left = left_by(pre, foul.record);
    const phys::CueStrike into_the_pink = scenario::strike(0.0, 0.5);
    const phys::ShotRecord dry = scenario::simulate(left, into_the_pink);
    REQUIRE(first_touched(dry, left.cue_ball) == place_of(left, Ball::pink));
    REQUIRE(nothing_pocketed(dry));

    FrameState s = foul.judgement.post;
    if (!choose(s, DecisionKind::play) || !lies_as(s, left)) return;
    const Judgement j = scenario::play(s, into_the_pink, nominate(Ball::black)).judgement;
    CHECK(fouls_text(j) == "first_hit_not_on 6");
    CHECK(j.penalty == 6);
    CHECK(j.points == 0);
    CHECK(j.post.scores[0] == 6);
}

TEST_CASE("S3.12(b)(i): a ball on hit before the nominated free ball is a foul too") {
    FrameState pre = behind_the_brown();
    pre.free_ball = true;
    ShotFacts facts = struck(pre, {place_of(pre, Ball::red)});
    // The cue ball comes to rest in the open, so the only question is what it hit first.
    const Vec2 cue{0.35, 0.8};
    REQUIRE(both_edges_open(cue, at(pre, Ball::red), colours_of(pre)));
    facts.rest[std::size_t(pre.cue_ball)] = cue;

    const Judgement j = judge(pre, nominate(Ball::brown), facts, oracle(), ref());
    CHECK(fouls_text(j) == "first_hit_not_on 4");
    CHECK(j.penalty == 4);
    // S2.21(b): not hitting the nominated free ball first is a miss, and the brown stood in full view.
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
}

TEST_CASE("S3.12(b)(i): the nominated free ball and a ball on hit at once is a fair stroke") {
    FrameState pre = behind_the_brown();
    pre.free_ball = true;
    ShotFacts facts = struck(pre, {place_of(pre, Ball::brown), place_of(pre, Ball::red)});
    // The cue ball runs through to the open table, clear of any snooker behind the brown.
    const Vec2 cue{0.35, 0.8};
    REQUIRE(both_edges_open(cue, at(pre, Ball::red), colours_of(pre)));
    facts.rest[std::size_t(pre.cue_ball)] = cue;

    const Judgement j = judge(pre, nominate(Ball::brown), facts, oracle(), ref());
    CHECK(fouls_text(j) == "");
    CHECK(j.free_ball_used);
    CHECK(name(j.post.pending) == "none");
    CHECK(j.post.striker == 1);
}

TEST_CASE("S3.12(b)(ii): leaving the cue ball snookered behind the nominated free ball is a foul") {
    FrameState pre = tucked_behind(Ball::brown, Ball::red);
    pre.balls.push_back(ball(Ball::black, -0.6, 0.2));
    const scenario::Played played = scenario::play(pre, roll_up(), nominate(Ball::brown));
    require_left_behind(played.record, pre, Ball::brown, Ball::red);

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "free_ball_snooker 4");
    CHECK(j.penalty == 4);
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "");
    CHECK(j.post.scores[1] == 4);
    // The offender's opponent now stands snookered after a foul in turn.
    CHECK(calls_text(j.calls) == "foul free_ball");
    CHECK(j.post.free_ball);
    CHECK(choices_text(j.post, 1) == "play play_again");
}

TEST_CASE("S3.12(b)(ii) with S2.17(b)(i): no foul when another ball, nearer the cue ball, is the snooker") {
    FrameState pre = scenario::frame({ball(Ball::cue, -0.1, kLine), ball(Ball::blue, 0.2, kLine),
                                      ball(Ball::brown, 0.5, kLine), ball(Ball::red, 0.8, kLine),
                                      ball(Ball::black, -0.6, 0.2)});
    pre.free_ball = true;
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), {at(pre, Ball::blue)}));
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), {at(pre, Ball::brown)}));

    const Judgement j =
        judge(pre, nominate(Ball::brown), struck(pre, {place_of(pre, Ball::brown)}), oracle(), ref());
    CHECK(fouls_text(j) == "");
    CHECK(name(j.post.pending) == "none");
    CHECK(j.post.striker == 1);
}

TEST_CASE("S3.12(b)(ii) with S2.17(b)(i): a foul when the free ball is the nearer of two in the way") {
    FrameState pre = scenario::frame({ball(Ball::cue, -0.1, kLine), ball(Ball::brown, 0.2, kLine),
                                      ball(Ball::blue, 0.5, kLine), ball(Ball::red, 0.8, kLine),
                                      ball(Ball::black, -0.6, 0.2)});
    pre.free_ball = true;
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), {at(pre, Ball::brown)}));

    const Judgement j =
        judge(pre, nominate(Ball::brown), struck(pre, {place_of(pre, Ball::brown)}), oracle(), ref());
    CHECK(fouls_text(j) == "free_ball_snooker 4");
}

TEST_CASE("S3.12(b)(ii) with S2.17(b)(ii): a foul when the free ball is one of two equally near in the way") {
    FrameState pre = scenario::frame({ball(Ball::cue, -0.2, 0.0), ball(Ball::brown, 0.1, 0.03),
                                      ball(Ball::blue, 0.1, -0.03), ball(Ball::red, 0.6, 0.0),
                                      ball(Ball::black, -0.6, 0.5)});
    pre.free_ball = true;
    REQUIRE(dist(at(pre, Ball::cue), at(pre, Ball::brown)) == dist(at(pre, Ball::cue), at(pre, Ball::blue)));
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), {at(pre, Ball::brown)}));
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), {at(pre, Ball::blue)}));

    const Judgement j =
        judge(pre, nominate(Ball::brown), struck(pre, {place_of(pre, Ball::brown)}), oracle(), ref());
    CHECK(fouls_text(j) == "free_ball_snooker 4");
}

TEST_CASE("S3.12(b)(ii) with S2.17(c): no foul when the free ball hides one red and another ball hides the other") {
    FrameState pre = scenario::frame({ball(Ball::cue, -0.2, 0.2), ball(Ball::brown, 0.1, 0.2),
                                      ball(Ball::red, 0.4, 0.2, 0), ball(Ball::blue, -0.2, 0.5),
                                      ball(Ball::red, -0.2, 0.8, 1), spotted(Ball::black)});
    pre.free_ball = true;
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red, 0), {at(pre, Ball::brown)}));
    REQUIRE(both_edges_open(at(pre, Ball::cue), at(pre, Ball::red, 0), {at(pre, Ball::blue)}));
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red, 1), {at(pre, Ball::blue)}));
    REQUIRE(both_edges_open(at(pre, Ball::cue), at(pre, Ball::red, 1), {at(pre, Ball::brown)}));

    const Judgement j =
        judge(pre, nominate(Ball::brown), struck(pre, {place_of(pre, Ball::brown)}), oracle(), ref());
    CHECK(fouls_text(j) == "");
    CHECK(j.post.striker == 1);
}

TEST_CASE("S3.12(b)(ii): a foul when every red lies behind the nominated free ball") {
    FrameState pre = scenario::frame({ball(Ball::cue, -0.2, 0.2), ball(Ball::brown, 0.1, 0.2),
                                      ball(Ball::red, 0.4, 0.2, 0), ball(Ball::red, 0.7, 0.2, 1),
                                      spotted(Ball::black)});
    pre.free_ball = true;
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red, 0), {at(pre, Ball::brown)}));
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red, 1), {at(pre, Ball::brown)}));

    const Judgement j =
        judge(pre, nominate(Ball::brown), struck(pre, {place_of(pre, Ball::brown)}), oracle(), ref());
    CHECK(fouls_text(j) == "free_ball_snooker 4");
}

TEST_CASE("S3.12(b)(ii): no foul after a stroke that scored") {
    FrameState pre = scenario::frame({ball(Ball::cue, -0.2, kLine), ball(Ball::brown, 0.2, kLine),
                                      ball(Ball::red, 0.5, kLine), spotted(Ball::black)});
    pre.free_ball = true;
    const int brown = place_of(pre, Ball::brown);
    // The brown is potted and goes back on its spot; the cue ball stops in baulk with the spotted
    // brown between it and the red.
    ShotFacts facts = struck(pre, {brown});
    facts.pots.push_back({brown, 4, 2.0});
    const Vec2 spot = table::spot(Ball::brown);
    const Vec2 red = at(pre, Ball::red);
    const double back = 0.3 / dist(red, spot);
    const Vec2 cue{spot.x - back * (red.x - spot.x), spot.y - back * (red.y - spot.y)};
    REQUIRE(no_part_open(cue, red, {spot}));
    facts.rest[std::size_t(pre.cue_ball)] = cue;

    const Judgement j = judge(pre, nominate(Ball::brown), facts, oracle(), ref());
    CHECK(fouls_text(j) == "");
    CHECK(j.points == 1);
    CHECK(lies_at(j.post, Ball::brown, spot));
    CHECK(j.post.striker == 0);
}

TEST_CASE("S3.12(b)(ii): with only pink and black left the snooker behind the free ball is allowed") {
    const FrameState pre = tucked_behind(Ball::black, Ball::pink);
    const scenario::Played played = scenario::play(pre, roll_up(), nominate(Ball::black));
    require_left_behind(played.record, pre, Ball::black, Ball::pink);

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "");
    CHECK(j.free_ball_used);
    CHECK(name(j.post.pending) == "none");
    CHECK(j.post.striker == 1);
    CHECK(name(j.post.on.kind) == "colour");
    CHECK(name(j.post.on.colour) == "pink");
    CHECK_FALSE(j.post.free_ball);
}

TEST_CASE("S3.12(b)(ii) as updated in August 2024: with black and one other colour left it is allowed") {
    const FrameState pre = tucked_behind(Ball::black, Ball::blue);
    const scenario::Played played = scenario::play(pre, roll_up(), nominate(Ball::black));
    require_left_behind(played.record, pre, Ball::black, Ball::blue);

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "");
    CHECK(name(j.post.pending) == "none");
    CHECK(j.post.striker == 1);
    CHECK(name(j.post.on.colour) == "blue");
}

TEST_CASE("S3.12(b)(ii): with three object balls left the foul stands") {
    FrameState pre = tucked_behind(Ball::black, Ball::blue);
    pre.balls.push_back(spotted(Ball::pink));
    const scenario::Played played = scenario::play(pre, roll_up(), nominate(Ball::black));
    require_left_behind(played.record, pre, Ball::black, Ball::blue);

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "free_ball_snooker 5");
    CHECK(j.penalty == 5);
}

TEST_CASE("S3.12(c): a potted free ball scores the value of the ball on and is spotted") {
    FrameState pre = scenario::frame({ball(Ball::cue, 0.0, 0.55), ball(Ball::brown, 0.0, 0.7),
                                      ball(Ball::blue, -0.3, 0.55), ball(Ball::red, -0.6, 0.55),
                                      spotted(Ball::black)});
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), {at(pre, Ball::blue)}));
    pre.free_ball = true;
    const scenario::Played played = scenario::play(pre, up_the_line(1.2), nominate(Ball::brown));
    const int brown = place_of(pre, Ball::brown);
    REQUIRE(first_touched(played.record, pre.cue_ball) == brown);
    REQUIRE(pocketed(played.record, brown));
    REQUIRE_FALSE(pocketed(played.record, pre.cue_ball));
    REQUIRE(stayed(played.record, pre, place_of(pre, Ball::red)));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "");
    CHECK(j.free_ball_used);
    CHECK(name(j.nominated.nominated) == "brown");
    CHECK(j.points == 1);
    CHECK(j.post.scores[0] == 1);
    CHECK(j.post.break_points == 1);
    CHECK(spotted_text(j) == "brown");
    CHECK(lies_at(j.post, Ball::brown, table::spot(Ball::brown)));
    CHECK(name(j.post.pending) == "none");
    CHECK(j.post.striker == 0);
    CHECK(name(j.post.on.kind) == "any_colour");
    CHECK_FALSE(j.post.free_ball);
}

TEST_CASE("S3.12(c): in the colours a potted free ball scores the colour on, which stays on") {
    FrameState pre = scenario::frame({ball(Ball::cue, 0.0, 0.55), ball(Ball::green, 0.0, 0.7),
                                      ball(Ball::blue, -0.3, 0.55), ball(Ball::yellow, -0.6, 0.55),
                                      spotted(Ball::brown), spotted(Ball::pink), spotted(Ball::black)});
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::yellow), {at(pre, Ball::blue)}));
    pre.free_ball = true;
    const scenario::Played played = scenario::play(pre, up_the_line(1.2), nominate(Ball::green));
    const int green = place_of(pre, Ball::green);
    REQUIRE(first_touched(played.record, pre.cue_ball) == green);
    REQUIRE(pocketed(played.record, green));
    REQUIRE_FALSE(pocketed(played.record, pre.cue_ball));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "");
    CHECK(j.points == 2);
    CHECK(j.post.scores[0] == 2);
    CHECK(spotted_text(j) == "green");
    CHECK(lies_at(j.post, Ball::green, table::spot(Ball::green)));
    CHECK(j.post.striker == 0);
    CHECK(name(j.post.on.kind) == "colour");
    CHECK(name(j.post.on.colour) == "yellow");
}

TEST_CASE("S3.12(d): a ball on potted off the nominated free ball scores and stays down") {
    FrameState pre = scenario::frame({ball(Ball::cue, 0.0, 0.45), ball(Ball::brown, 0.0, 0.6),
                                      ball(Ball::red, 0.0, 0.75), spotted(Ball::black)});
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), {at(pre, Ball::brown)}));
    pre.free_ball = true;
    const scenario::Played played = scenario::play(pre, up_the_line(1.2), nominate(Ball::brown));
    const int brown = place_of(pre, Ball::brown);
    REQUIRE(first_touched(played.record, pre.cue_ball) == brown);
    REQUIRE(pocketed(played.record, place_of(pre, Ball::red)));
    REQUIRE_FALSE(pocketed(played.record, brown));
    REQUIRE_FALSE(pocketed(played.record, pre.cue_ball));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "");
    CHECK(j.points == 1);
    CHECK(j.post.scores[0] == 1);
    CHECK(place_of(j.post, Ball::red) == -1);
    CHECK(spotted_text(j) == "");
    CHECK(lies_at(j.post, Ball::brown, rest(played.record, brown), 1e-9));
    CHECK(j.post.striker == 0);
    CHECK(name(j.post.on.kind) == "any_colour");
}

TEST_CASE("S3.12(d): the same when the free ball and the ball on were hit at once") {
    FrameState pre = behind_the_brown();
    pre.free_ball = true;
    const int red = place_of(pre, Ball::red);
    ShotFacts facts = struck(pre, {place_of(pre, Ball::brown), red});
    facts.pots.push_back({red, 0, 2.0});
    facts.rest[std::size_t(pre.cue_ball)] = {0.35, 0.8};

    const Judgement j = judge(pre, nominate(Ball::brown), facts, oracle(), ref());
    CHECK(fouls_text(j) == "");
    CHECK(j.points == 1);
    CHECK(place_of(j.post, Ball::red) == -1);
    CHECK(lies_at(j.post, Ball::brown, at(pre, Ball::brown)));
    CHECK(j.post.striker == 0);
}

TEST_CASE("S3.12(e): the free ball and a red both potted: each scores, the free ball is spotted") {
    FrameState pre = scenario::frame({ball(Ball::cue, 0.0, 0.0), ball(Ball::brown, 0.0, 0.12),
                                      ball(Ball::red, 0.0, 0.75), spotted(Ball::black)});
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), {at(pre, Ball::brown)}));
    pre.free_ball = true;
    const scenario::Played played = scenario::play(pre, up_the_line(1.5), nominate(Ball::brown));
    REQUIRE(first_touched(played.record, pre.cue_ball) == place_of(pre, Ball::brown));
    REQUIRE(pocketed(played.record, place_of(pre, Ball::red)));
    REQUIRE(pocketed(played.record, place_of(pre, Ball::brown)));
    REQUIRE_FALSE(pocketed(played.record, pre.cue_ball));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "");
    CHECK(j.points == 2);
    CHECK(j.post.scores[0] == 2);
    CHECK(place_of(j.post, Ball::red) == -1);
    CHECK(spotted_text(j) == "brown");
    CHECK(lies_at(j.post, Ball::brown, table::spot(Ball::brown)));
    CHECK(j.post.striker == 0);
    CHECK(name(j.post.on.kind) == "any_colour");
}

TEST_CASE("S3.12(e): the free ball and the colour on both potted: only the colour on scores") {
    FrameState pre = scenario::frame({ball(Ball::cue, 0.0, 0.0), ball(Ball::green, 0.0, 0.12),
                                      ball(Ball::yellow, 0.0, 0.75), spotted(Ball::brown),
                                      ball(Ball::blue, 0.5, -0.5), spotted(Ball::pink), spotted(Ball::black)});
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::yellow), {at(pre, Ball::green)}));
    pre.free_ball = true;
    const scenario::Played played = scenario::play(pre, up_the_line(1.5), nominate(Ball::green));
    REQUIRE(first_touched(played.record, pre.cue_ball) == place_of(pre, Ball::green));
    REQUIRE(pocketed(played.record, place_of(pre, Ball::yellow)));
    REQUIRE(pocketed(played.record, place_of(pre, Ball::green)));
    REQUIRE_FALSE(pocketed(played.record, pre.cue_ball));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "");
    CHECK(j.points == 2);
    CHECK(j.post.scores[0] == 2);
    CHECK(place_of(j.post, Ball::yellow) == -1);
    CHECK(spotted_text(j) == "green");
    CHECK(lies_at(j.post, Ball::green, table::spot(Ball::green)));
    CHECK(j.post.striker == 0);
    CHECK(name(j.post.on.kind) == "colour");
    CHECK(name(j.post.on.colour) == "green");
}

TEST_CASE("S3.12(f): asked to play again, the offender has no free ball") {
    const FrameState pre = behind_the_brown();
    const scenario::Played foul = scenario::play(pre, roll_away());
    REQUIRE(first_touched(foul.record, pre.cue_ball) == -1);
    REQUIRE(no_part_open(rest(foul.record, pre.cue_ball), at(pre, Ball::red), colours_of(pre)));

    const Position left = left_by(pre, foul.record);
    const phys::CueStrike into_the_brown = scenario::strike(0.0, 0.5);
    REQUIRE(first_touched(scenario::simulate(left, into_the_brown), left.cue_ball) == place_of(left, Ball::brown));

    FrameState s = foul.judgement.post;
    CHECK(s.free_ball);
    if (!choose(s, DecisionKind::play_again)) return;
    CHECK(s.striker == 0);
    CHECK_FALSE(s.free_ball);
    CHECK(nominations_text(s) == "");
    if (!lies_as(s, left)) return;
    const Judgement j = scenario::play(s, into_the_brown, nominate(Ball::brown)).judgement;
    CHECK(fouls_text(j) == "first_hit_not_on 4");
    CHECK_FALSE(j.free_ball_used);
}

TEST_CASE("S3.12(f): asking for the balls to be replaced gives the free ball up") {
    const FrameState pre = behind_the_brown();
    const scenario::Played foul = scenario::play(pre, roll_away());
    REQUIRE(first_touched(foul.record, pre.cue_ball) == -1);
    REQUIRE(no_part_open(rest(foul.record, pre.cue_ball), at(pre, Ball::red), colours_of(pre)));

    FrameState s = foul.judgement.post;
    CHECK(s.free_ball);
    if (!choose(s, DecisionKind::replace)) return;
    CHECK(s.striker == 0);
    CHECK_FALSE(s.free_ball);
    CHECK(nominations_text(s) == "");
    CHECK(same_balls(s, pre));
}

TEST_CASE("S3.13: a request to play again cannot be taken back") {
    const FrameState pre = scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::red, 0.5, kLine),
                                            spotted(Ball::black)});
    const scenario::Played foul = scenario::play(pre, roll_away());
    REQUIRE(first_touched(foul.record, pre.cue_ball) == -1);

    FrameState s = foul.judgement.post;
    if (!choose(s, DecisionKind::play_again)) return;
    CHECK(name(s.pending) == "none");
    CHECK(s.striker == 0);
    for (DecisionKind second_thought : {DecisionKind::play, DecisionKind::replace, DecisionKind::play_again}) {
        CHECK_FALSE(offers(s, 1, second_thought));
        std::vector<Call> calls;
        std::string error;
        CHECK_FALSE(decide(s, {second_thought, 1}, oracle(), ref(), calls, error));
        CHECK(error != "");
        CHECK(name(s.pending) == "none");
        CHECK(s.striker == 0);
    }
}

TEST_CASE("S3.13: a request for the balls to be replaced cannot be taken back") {
    const FrameState pre = scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::red, 0.5, kLine),
                                            spotted(Ball::black)});
    const scenario::Played foul = scenario::play(pre, roll_away());
    REQUIRE(first_touched(foul.record, pre.cue_ball) == -1);

    FrameState s = foul.judgement.post;
    if (!choose(s, DecisionKind::replace)) return;
    CHECK(name(s.pending) == "none");
    CHECK(s.striker == 0);
    for (DecisionKind second_thought : {DecisionKind::play, DecisionKind::play_again}) {
        CHECK_FALSE(offers(s, 1, second_thought));
        std::vector<Call> calls;
        std::string error;
        CHECK_FALSE(decide(s, {second_thought, 1}, oracle(), ref(), calls, error));
        CHECK(same_balls(s, pre));
        CHECK(s.striker == 0);
    }
}

TEST_CASE("S3.13(a): asked to play again, the offender may play a different stroke at a different ball on") {
    const FrameState pre = scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::red, 0.5, kLine, 0),
                                            ball(Ball::red, -0.5, 0.1, 1), spotted(Ball::black)});
    const scenario::Played foul = scenario::play(pre, roll_away());
    REQUIRE(first_touched(foul.record, pre.cue_ball) == -1);

    const Position left = left_by(pre, foul.record);
    const int other_red = place_of(left, Ball::red, 1);
    const phys::CueStrike at_the_other = scenario::strike(scenario::aim_at_ball(left, other_red), 0.4);
    const phys::ShotRecord dry = scenario::simulate(left, at_the_other);
    REQUIRE(first_touched(dry, left.cue_ball) == other_red);
    REQUIRE(nothing_pocketed(dry));

    FrameState s = foul.judgement.post;
    if (!choose(s, DecisionKind::play_again) || !lies_as(s, left)) return;
    CHECK(s.striker == 0);
    const Judgement j = scenario::play(s, at_the_other).judgement;
    CHECK(fouls_text(j) == "");
    CHECK(name(j.post.pending) == "none");
    CHECK(j.post.striker == 1);
}

TEST_CASE("S3.13(b): asked to play again, the offender scores for the ball they pot") {
    const FrameState pre = scenario::frame({ball(Ball::cue, 0.0, 0.55), ball(Ball::red, 0.0, 0.7),
                                            spotted(Ball::black)});
    const scenario::Played foul = scenario::play(pre, scenario::strike(-kPi / 2.0, 0.1));
    REQUIRE(first_touched(foul.record, pre.cue_ball) == -1);

    const Position left = left_by(pre, foul.record);
    const int red = place_of(left, Ball::red);
    const phys::CueStrike pot = scenario::strike(scenario::aim_at_ball(left, red), 1.2);
    const phys::ShotRecord dry = scenario::simulate(left, pot);
    REQUIRE(first_touched(dry, left.cue_ball) == red);
    REQUIRE(pocketed(dry, red));
    REQUIRE_FALSE(pocketed(dry, left.cue_ball));

    FrameState s = foul.judgement.post;
    CHECK(s.scores[1] == 4);
    if (!choose(s, DecisionKind::play_again) || !lies_as(s, left)) return;
    const Judgement j = scenario::play(s, pot).judgement;
    CHECK(fouls_text(j) == "");
    CHECK(j.points == 1);
    CHECK(j.post.scores[0] == 1);
    CHECK(j.post.scores[1] == 4);
    CHECK(j.post.striker == 0);
    CHECK(name(j.post.on.kind) == "any_colour");
}
