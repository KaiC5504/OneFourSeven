// Spec 9.1: the referee's judgement where the rulebook leaves it to a human eye, as
// docs/rules/README.md and core/data/referee_params.json fix it: the push stroke either side of
// each limit, the order a miss is decided in under the television standard, the lenient setting,
// and the stalemate thresholds. Written from those documents, never from the engine. REQUIRE
// states a premise, read from the raw shot record or worked out by hand here; CHECK states the
// decision.

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
using scenario::place_of;
using scenario::spotted;

namespace {

constexpr double kR = table::kBallRadius;
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegree = kPi / 180.0;
// The positions are built along this line, clear of every spot.
constexpr double kLine = 0.5;

const TableOracle& oracle() { return *scenario::rig().oracle; }
const RefParams& ref() { return scenario::rig().ref; }

// When the cue ball first touched an object ball, from the raw record; -1 when it touched none.
double first_contact_s(const phys::ShotRecord& r, int cue) {
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind == phys::EventKind::ball_ball && (e.a == cue || e.b == cue)) return e.t;
    }
    return -1.0;
}

int first_touched(const phys::ShotRecord& r, int cue) {
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind != phys::EventKind::ball_ball) continue;
        if (e.a == cue) return e.b;
        if (e.b == cue) return e.a;
    }
    return -1;
}

// The cut at the first contact, from the raw record: the angle between the stroke's line and the
// line of centres.
double cut_at_contact(const phys::ShotRecord& r, int cue, int object, double aim) {
    const double t = first_contact_s(r, cue);
    const phys::BallState c = r.ball_at(cue, t);
    const phys::BallState o = r.ball_at(object, t);
    return std::abs(std::remainder(std::atan2(o.r.y - c.r.y, o.r.x - c.r.x) - aim, 2.0 * kPi));
}

Vec2 rest(const phys::ShotRecord& r, int place) {
    const phys::BallState& b = r.final_state.balls[std::size_t(place)];
    return {b.r.x, b.r.y};
}

bool nothing_pocketed(const phys::ShotRecord& r) {
    for (const phys::BallState& b : r.final_state.balls) {
        if (b.motion == phys::Motion::pocketed) return false;
    }
    return true;
}

// For positions built here; the ball must be listed.
Vec2 at(const Position& p, Ball b) { return p.balls[std::size_t(place_of(p, b))].pos; }

// Every object ball that is not a `b`: the balls that can be in the way of one.
std::vector<Vec2> all_but(const Position& p, Ball b) {
    std::vector<Vec2> out;
    for (const BallOnTable& x : p.balls) {
        if (x.ball != Ball::cue && x.ball != b) out.push_back(x.pos);
    }
    return out;
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

bool no_part_open(Vec2 cue, Vec2 target, const std::vector<Vec2>& others) {
    for (int i = -50; i <= 50; ++i) {
        if (open(cue, target, others, i / 50.0)) return false;
    }
    return true;
}

std::string name(Pending p) { return std::string(pending_name(p)); }

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

// A red straight ahead of the cue ball with `gap` metres between their surfaces.
FrameState red_ahead(double gap) {
    return scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::red, -0.5 + 2.0 * kR + gap, kLine),
                            spotted(Ball::black)});
}

// A red a tenth of a millimetre from the cue ball and round its side, placed so that a cue ball
// played straight up the table meets it `cut` radians off the line of centres.
FrameState red_beside(double cut) {
    const double d = 2.0 * kR + 1e-4;
    const double off = std::asin(2.0 * kR * std::sin(cut) / d);
    return scenario::frame({ball(Ball::cue, -0.5, kLine),
                            ball(Ball::red, -0.5 + d * std::cos(off), kLine - d * std::sin(off)),
                            spotted(Ball::black)});
}

phys::CueStrike up_the_table(double speed) { return scenario::strike(0.0, speed); }

// The last red a metre from the cue ball with nothing between them and every colour on its spot:
// 35 points on the table.
FrameState open_red() {
    return scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::red, 0.5, kLine), spotted(Ball::yellow),
                            spotted(Ball::green), spotted(Ball::brown), spotted(Ball::blue), spotted(Ball::pink),
                            spotted(Ball::black)});
}

// The same with the brown on the line half-way to the red, hiding all of it.
FrameState hidden_red() {
    FrameState s = open_red();
    s.balls[std::size_t(place_of(s, Ball::brown))].pos = {0.0, kLine};
    return s;
}

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

// A soft stroke straight away from the red: the cue ball rolls a few centimetres back along the
// line and touches nothing.
phys::CueStrike roll_away() { return scenario::strike(kPi, 0.1); }

// Facts for an escape that hit nothing and came no nearer the ball at `place` than `gap` metres.
ShotFacts passed_by(const Position& p, int place, double gap) {
    ShotFacts f = scenario::quiet_facts(p);
    f.closest_gap_m[std::size_t(place)] = gap;
    f.cue_travel_m = 2.5;
    return f;
}

RefParams lenient(double gap) {
    RefParams r = ref();
    r.miss_lenient_gap_m = gap;
    return r;
}

// A red and the black with the cue ball a metre from the red: nothing to stop the players
// trading safeties for ever.
FrameState safety_frame() {
    return scenario::frame({ball(Ball::cue, -0.5, kLine), ball(Ball::red, 0.5, kLine, 0), spotted(Ball::black)});
}

// Facts for a safety: the cue ball finds the first red, which ends `dx` metres along the table
// from where it lay; nothing else moves and nothing is potted.
ShotFacts safety(const FrameState& s, double dx) {
    ShotFacts f = scenario::quiet_facts(s);
    const std::size_t red = std::size_t(place_of(s, Ball::red));
    f.first_hit = {int(red)};
    f.first_hit_t = 1.0;
    f.closest_gap_m[red] = 0.0;
    f.rest[red].x += dx;
    f.object_travel_m = std::abs(dx);
    f.cue_travel_m = 1.0;
    return f;
}

// Whether a stroke can be played from `s`. When it cannot, a check fails and the scenario stops.
bool playable(const FrameState& s) {
    const bool ok = !s.over && !s.restart && s.pending == Pending::none && s.cue_ball >= 0 &&
                    std::size_t(s.cue_ball) < s.balls.size() && place_of(s, Ball::red) >= 0;
    CHECK_MESSAGE(ok, "the frame is not waiting for a stroke");
    return ok;
}

// `n` quiet safeties, the players in turn, the red nudged 10 cm one way and then back. False when
// the frame stops taking strokes before the last of them. `last` receives the last judgement.
bool exchange(FrameState& s, int n, const RefParams& r, Judgement* last = nullptr) {
    for (int i = 0; i < n; ++i) {
        if (!playable(s)) return false;
        Judgement j = judge(s, {}, safety(s, i % 2 == 0 ? 0.1 : -0.1), oracle(), r);
        s = j.post;
        if (last != nullptr) *last = std::move(j);
    }
    return true;
}

}  // namespace

TEST_CASE("spec 9.1 push stroke: a first contact inside the tip's contact time is a push") {
    const FrameState pre = red_ahead(0.0006);
    REQUIRE(0.0006 > ref().touching_gap_m);
    const scenario::Played played = scenario::play(pre, up_the_table(0.5));
    const double t = first_contact_s(played.record, pre.cue_ball);
    REQUIRE(first_touched(played.record, pre.cue_ball) == place_of(pre, Ball::red));
    REQUIRE(t > 0.0);
    REQUIRE(t < ref().push_contact_time_s);
    REQUIRE(nothing_pocketed(played.record));

    const Judgement& j = played.judgement;
    CHECK(played.facts.push);
    CHECK(fouls_text(j) == "push_stroke 4");
    CHECK(j.penalty == 4);
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "");
    CHECK(choices_text(j.post, 1) == "play play_again");
}

TEST_CASE("spec 9.1 push stroke: a first contact just after the tip's contact time is fair") {
    const FrameState pre = red_ahead(0.0008);
    const scenario::Played played = scenario::play(pre, up_the_table(0.5));
    REQUIRE(first_touched(played.record, pre.cue_ball) == place_of(pre, Ball::red));
    REQUIRE(first_contact_s(played.record, pre.cue_ball) > ref().push_contact_time_s);
    REQUIRE(first_contact_s(played.record, pre.cue_ball) < 1.25 * ref().push_contact_time_s);
    REQUIRE(nothing_pocketed(played.record));

    const Judgement& j = played.judgement;
    CHECK_FALSE(played.facts.push);
    CHECK(fouls_text(j) == "");
    CHECK(name(j.post.pending) == "none");
    CHECK(j.post.striker == 1);
}

TEST_CASE("spec 9.1 push stroke: the gap that was a push is fair at a slower pace") {
    const FrameState pre = red_ahead(0.0006);
    const scenario::Played played = scenario::play(pre, up_the_table(0.3));
    REQUIRE(first_touched(played.record, pre.cue_ball) == place_of(pre, Ball::red));
    REQUIRE(first_contact_s(played.record, pre.cue_ball) > ref().push_contact_time_s);
    REQUIRE(nothing_pocketed(played.record));

    const Judgement& j = played.judgement;
    CHECK_FALSE(played.facts.push);
    CHECK(fouls_text(j) == "");
    CHECK(j.post.striker == 1);
}

TEST_CASE("spec 9.1 push stroke: the gap that was fair is a push at a faster pace") {
    const FrameState pre = red_ahead(0.0008);
    const scenario::Played played = scenario::play(pre, up_the_table(0.8));
    const double t = first_contact_s(played.record, pre.cue_ball);
    REQUIRE(first_touched(played.record, pre.cue_ball) == place_of(pre, Ball::red));
    REQUIRE(t > 0.0);
    REQUIRE(t < ref().push_contact_time_s);
    REQUIRE(nothing_pocketed(played.record));

    const Judgement& j = played.judgement;
    CHECK(played.facts.push);
    CHECK(fouls_text(j) == "push_stroke 4");
}

TEST_CASE("spec 9.1 push stroke: a very fine edge of an almost touching ball is no push") {
    const FrameState pre = red_beside(85.0 * kDegree);
    const int red = place_of(pre, Ball::red);
    REQUIRE(dist(at(pre, Ball::cue), at(pre, Ball::red)) - 2.0 * kR > ref().touching_gap_m);
    const scenario::Played played = scenario::play(pre, up_the_table(1.0));
    const double t = first_contact_s(played.record, pre.cue_ball);
    REQUIRE(first_touched(played.record, pre.cue_ball) == red);
    REQUIRE(t > 0.0);
    REQUIRE(t < ref().push_contact_time_s);
    REQUIRE(cut_at_contact(played.record, pre.cue_ball, red, 0.0) > ref().push_fine_edge_rad + 2.0 * kDegree);
    REQUIRE(nothing_pocketed(played.record));

    const Judgement& j = played.judgement;
    CHECK_FALSE(played.facts.push);
    CHECK(fouls_text(j) == "");
    CHECK(j.post.striker == 1);
}

TEST_CASE("spec 9.1 push stroke: a thicker contact on an almost touching ball is a push") {
    const FrameState pre = red_beside(75.0 * kDegree);
    const int red = place_of(pre, Ball::red);
    REQUIRE(dist(at(pre, Ball::cue), at(pre, Ball::red)) - 2.0 * kR > ref().touching_gap_m);
    const scenario::Played played = scenario::play(pre, up_the_table(1.0));
    const double t = first_contact_s(played.record, pre.cue_ball);
    REQUIRE(first_touched(played.record, pre.cue_ball) == red);
    REQUIRE(t > 0.0);
    REQUIRE(t < ref().push_contact_time_s);
    REQUIRE(cut_at_contact(played.record, pre.cue_ball, red, 0.0) < ref().push_fine_edge_rad - 2.0 * kDegree);
    REQUIRE(nothing_pocketed(played.record));

    const Judgement& j = played.judgement;
    CHECK(played.facts.push);
    CHECK(fouls_text(j) == "push_stroke 4");
}

TEST_CASE("spec 9.1 miss order: a run of misses comes before the scores") {
    FrameState pre = open_red();
    pre.scores = {0, 40};
    pre.miss_run = 1;
    const scenario::Played played = scenario::play(pre, roll_away());
    REQUIRE(first_touched(played.record, pre.cue_ball) == -1);

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(d)");
}

TEST_CASE("spec 9.1 miss order: the scores come before a ball on that cannot be hit") {
    FrameState pre = walled_in_red();
    pre.scores = {0, 40};
    const scenario::Played played = scenario::play(pre, up_the_table(1.0));
    REQUIRE(first_touched(played.record, pre.cue_ball) == place_of(pre, Ball::yellow));
    REQUIRE(nothing_pocketed(played.record));

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "first_hit_not_on 4");
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(i)");
}

TEST_CASE("spec 9.1 miss order: the scores come before a clear path and before a snooker") {
    for (const FrameState& position : {open_red(), hidden_red()}) {
        FrameState pre = position;
        pre.scores = {0, 40};
        const scenario::Played played = scenario::play(pre, roll_away());
        REQUIRE(first_touched(played.record, pre.cue_ball) == -1);

        const Judgement& j = played.judgement;
        CHECK(fouls_text(j) == "no_hit 4");
        CHECK_FALSE(j.miss);
        CHECK(j.miss_reason == "S3.14(a)(i)");
    }
}

TEST_CASE("spec 9.1 miss order: a ball on that cannot be hit comes before the snooker it is") {
    const FrameState pre = walled_in_red();
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));
    const scenario::Played played = scenario::play(pre, up_the_table(1.0));
    REQUIRE(first_touched(played.record, pre.cue_ball) == place_of(pre, Ball::yellow));
    REQUIRE(nothing_pocketed(played.record));

    const Judgement& j = played.judgement;
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "S3.14(a)(ii)");
}

TEST_CASE("spec 9.1 miss order: a clear path is a miss under S3.14(c), a snooker under S3.14(a)") {
    const FrameState clear = open_red();
    REQUIRE(open(at(clear, Ball::cue), at(clear, Ball::red), all_but(clear, Ball::red), 0.0));
    const scenario::Played seen = scenario::play(clear, roll_away());
    REQUIRE(first_touched(seen.record, clear.cue_ball) == -1);
    CHECK(seen.judgement.miss);
    CHECK(seen.judgement.miss_reason == "S3.14(c)");

    const FrameState hidden = hidden_red();
    REQUIRE(no_part_open(at(hidden, Ball::cue), at(hidden, Ball::red), all_but(hidden, Ball::red)));
    const scenario::Played unseen = scenario::play(hidden, roll_away());
    REQUIRE(first_touched(unseen.record, hidden.cue_ball) == -1);
    CHECK(unseen.judgement.miss);
    CHECK(unseen.judgement.miss_reason == "S3.14(a)");
}

TEST_CASE("spec 9.1 miss, television standard: a failed escape is a miss however near it came") {
    REQUIRE(ref().miss_lenient_gap_m == 0.0);
    const FrameState pre = hidden_red();
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));

    // The cue ball comes round off a cushion and passes a millimetre from the red.
    const Judgement j = judge(pre, {}, passed_by(pre, place_of(pre, Ball::red), 0.001), oracle(), ref());
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(a)");
    CHECK(scenario::has_call(j.calls, CallKind::miss));
    CHECK(choices_text(j.post, 1) == "play play_again replace");
}

TEST_CASE("spec 9.1 miss: the same judgement whichever player is at the table") {
    std::array<Judgement, 2> judged;
    for (int striker : {0, 1}) {
        FrameState pre = hidden_red();
        pre.striker = striker;
        pre.concede_any_time = {true, false};
        const scenario::Played played = scenario::play(pre, roll_away());
        REQUIRE(first_touched(played.record, pre.cue_ball) == -1);
        judged[std::size_t(striker)] = played.judgement;

        const Judgement& j = played.judgement;
        CHECK(fouls_text(j) == "no_hit 4");
        CHECK(j.miss);
        CHECK(j.miss_reason == "S3.14(a)");
        CHECK(j.post.offender == striker);
        CHECK(j.post.decider == 1 - striker);
        CHECK(j.post.scores[std::size_t(1 - striker)] == 4);
        CHECK(choices_text(j.post, 1 - striker) == "play play_again replace");
    }
    CHECK(calls_text(judged[0].calls) == calls_text(judged[1].calls));
}

TEST_CASE("spec 9.1 miss, lenient setting: a failed escape that passed within the gap is a foul only") {
    const FrameState pre = hidden_red();
    REQUIRE(no_part_open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red)));

    const Judgement j = judge(pre, {}, passed_by(pre, place_of(pre, Ball::red), 0.015), oracle(), lenient(0.02));
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "lenient");
    CHECK_FALSE(scenario::has_call(j.calls, CallKind::miss));
    CHECK(choices_text(j.post, 1) == "play play_again");
}

TEST_CASE("spec 9.1 miss, lenient setting: a failed escape that passed outside the gap is a miss") {
    const FrameState pre = hidden_red();
    const Judgement j = judge(pre, {}, passed_by(pre, place_of(pre, Ball::red), 0.025), oracle(), lenient(0.02));
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(a)");
    CHECK(choices_text(j.post, 1) == "play play_again replace");
}

TEST_CASE("spec 9.1 miss, lenient setting: hitting the snookering ball first, close to the ball on, is a foul only") {
    const FrameState pre = hidden_red();
    ShotFacts facts = passed_by(pre, place_of(pre, Ball::red), 0.015);
    const int brown = place_of(pre, Ball::brown);
    facts.first_hit = {brown};
    facts.first_hit_t = 1.0;
    facts.closest_gap_m[std::size_t(brown)] = 0.0;

    const Judgement j = judge(pre, {}, facts, oracle(), lenient(0.02));
    CHECK(fouls_text(j) == "first_hit_not_on 4");
    CHECK_FALSE(j.miss);
    CHECK(j.miss_reason == "lenient");
}

TEST_CASE("spec 9.1 miss, lenient setting: with a clear path nearness excuses nothing") {
    const FrameState pre = open_red();
    REQUIRE(open(at(pre, Ball::cue), at(pre, Ball::red), all_but(pre, Ball::red), 0.0));

    const Judgement j = judge(pre, {}, passed_by(pre, place_of(pre, Ball::red), 0.001), oracle(), lenient(0.02));
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(c)");
}

TEST_CASE("spec 9.1 miss, lenient setting: a run of misses still comes first") {
    FrameState pre = hidden_red();
    pre.miss_run = 1;

    const Judgement j = judge(pre, {}, passed_by(pre, place_of(pre, Ball::red), 0.015), oracle(), lenient(0.02));
    CHECK(j.miss);
    CHECK(j.miss_reason == "S3.14(d)");
}

TEST_CASE("spec 9.1 stalemate: a stroke that moves the balls less than the travel threshold is quiet") {
    FrameState pre = safety_frame();
    pre.quiet_strokes = 3;
    const Judgement j = judge(pre, {}, safety(pre, ref().stalemate_quiet_travel_m - 0.01), oracle(), ref());
    CHECK(j.post.quiet_strokes == 4);
}

TEST_CASE("spec 9.1 stalemate: a stroke that moves the balls more than the travel threshold is not") {
    FrameState pre = safety_frame();
    pre.quiet_strokes = 3;
    const Judgement j = judge(pre, {}, safety(pre, ref().stalemate_quiet_travel_m + 0.01), oracle(), ref());
    CHECK(j.post.quiet_strokes == 0);
}

TEST_CASE("spec 9.1 stalemate: a pot is never quiet, however little the balls moved") {
    FrameState pre = safety_frame();
    pre.balls.push_back(ball(Ball::red, 0.0, 0.85, 1));
    pre.quiet_strokes = 3;
    // The second red, hanging over the middle pocket, is rolled in.
    const int red = place_of(pre, Ball::red, 1);
    ShotFacts facts = scenario::quiet_facts(pre);
    facts.first_hit = {red};
    facts.first_hit_t = 1.0;
    facts.closest_gap_m[std::size_t(red)] = 0.0;
    facts.pots.push_back({red, 2, 1.5});
    facts.object_travel_m = 0.05;
    facts.cue_travel_m = 1.0;

    const Judgement j = judge(pre, {}, facts, oracle(), ref());
    CHECK(j.points == 1);
    CHECK(j.post.quiet_strokes == 0);
}

TEST_CASE("spec 9.1 stalemate: a played safety that leaves the red half a metre on is quiet") {
    FrameState pre = scenario::frame({ball(Ball::cue, -0.3, kLine), ball(Ball::red, 0.0, kLine), spotted(Ball::black)});
    pre.quiet_strokes = 3;
    const scenario::Played played = scenario::play(pre, up_the_table(0.5));
    const int red = place_of(pre, Ball::red);
    REQUIRE(first_touched(played.record, pre.cue_ball) == red);
    REQUIRE(nothing_pocketed(played.record));
    REQUIRE(dist(rest(played.record, red), at(pre, Ball::red)) < ref().stalemate_quiet_travel_m - 0.05);
    REQUIRE(dist(rest(played.record, place_of(pre, Ball::black)), at(pre, Ball::black)) == 0.0);

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "");
    CHECK(j.post.quiet_strokes == 4);
}

TEST_CASE("spec 9.1 stalemate: the same safety played harder, the red ending well up the table, is not") {
    FrameState pre = scenario::frame({ball(Ball::cue, -0.3, kLine), ball(Ball::red, 0.0, kLine), spotted(Ball::black)});
    pre.quiet_strokes = 3;
    const scenario::Played played = scenario::play(pre, up_the_table(0.6));
    const int red = place_of(pre, Ball::red);
    REQUIRE(first_touched(played.record, pre.cue_ball) == red);
    REQUIRE(nothing_pocketed(played.record));
    REQUIRE(dist(rest(played.record, red), at(pre, Ball::red)) > ref().stalemate_quiet_travel_m + 0.05);

    const Judgement& j = played.judgement;
    CHECK(fouls_text(j) == "");
    CHECK(j.post.quiet_strokes == 0);
}

TEST_CASE("spec 9.1 stalemate: no offer one quiet stroke short of the threshold, the offer on the next") {
    FrameState s = safety_frame();
    const int offer = ref().stalemate_offer_strokes;
    Judgement last;
    if (!exchange(s, offer - 1, ref(), &last)) return;
    CHECK(calls_text(last.calls) == "");
    CHECK(name(s.pending) == "none");
    CHECK(s.quiet_strokes == offer - 1);

    if (!exchange(s, 1, ref(), &last)) return;
    CHECK(calls_text(last.calls) == "rerack_offer");
    CHECK(name(s.pending) == "rerack");
}

TEST_CASE("spec 9.1 stalemate: the thresholds are the referee values the strokes are judged with") {
    RefParams tight = ref();
    tight.stalemate_quiet_travel_m = 0.2;
    tight.stalemate_offer_strokes = 3;
    tight.stalemate_grace_strokes = 2;
    REQUIRE(0.25 < ref().stalemate_quiet_travel_m);

    // 25 cm is quiet by the file's value and not by this one.
    FrameState s = safety_frame();
    s.quiet_strokes = 2;
    const Judgement moved = judge(s, {}, safety(s, 0.25), oracle(), tight);
    CHECK(moved.post.quiet_strokes == 0);
    CHECK(name(moved.post.pending) == "none");
    s = moved.post;

    Judgement last;
    if (!exchange(s, 2, tight, &last)) return;
    CHECK(name(s.pending) == "none");
    if (!exchange(s, 1, tight, &last)) return;
    CHECK(calls_text(last.calls) == "rerack_offer");
    CHECK(name(s.pending) == "rerack");

    std::vector<Call> said;
    std::string error;
    const bool refused = decide(s, {DecisionKind::refuse_rerack, s.decider}, oracle(), tight, said, error);
    CHECK(refused);
    if (!refused) return;
    CHECK(s.rerack_deadline == 2);
    if (!exchange(s, 1, tight, &last)) return;
    CHECK_FALSE(s.restart);
    if (!exchange(s, 1, tight, &last)) return;
    CHECK(calls_text(last.calls) == "rerack");
    CHECK(s.restart);
}
