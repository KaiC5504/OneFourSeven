#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "scenario.hpp"

// Section 3 Rules 6 (two balls hit at once) and 8 (touching ball), written from the rulebook and
// docs/rules/README.md. docs/rules/clauses-t2.md maps every clause to its test here.
//
// REQUIRE states what physically happened, read from the shot record; CHECK states the ruling.

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

constexpr double kR = table::kBallRadius;
const double kPi = std::acos(-1.0);

double rad(double degrees) { return degrees * kPi / 180.0; }

double apart(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

struct Contact {
    double t = 0.0;
    int ball = -1;
};

// Every object ball the cue ball touched, in order, straight from the event list.
std::vector<Contact> cue_contacts(const phys::ShotRecord& r, int cue) {
    std::vector<Contact> out;
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind != phys::EventKind::ball_ball) continue;
        if (e.a == cue) out.push_back({e.t, e.b});
        if (e.b == cue) out.push_back({e.t, e.a});
    }
    return out;
}

Vec2 rest(const phys::ShotRecord& r, int place) {
    const phys::BallState& b = r.final_state.balls[std::size_t(place)];
    return {b.r.x, b.r.y};
}

bool anything_potted(const phys::ShotRecord& r) {
    return std::any_of(r.final_state.balls.begin(), r.final_state.balls.end(),
                       [](const phys::BallState& b) { return b.motion == phys::Motion::pocketed; });
}

// Nothing moved the ball: it lies where the stroke found it.
bool stayed(const Position& pre, const phys::ShotRecord& r, int place) {
    return apart(pre.balls[std::size_t(place)].pos, rest(r, place)) < 1e-12;
}

// How far `p` lies from the straight path between a and b.
double off_path(Vec2 a, Vec2 b, Vec2 p) {
    const double along = ((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / (apart(a, b) * apart(a, b));
    const double t = std::clamp(along, 0.0, 1.0);
    return apart(p, {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)});
}

// `balls` and every colour not among them, each on its own spot.
std::vector<BallOnTable> rest_spotted(std::vector<BallOnTable> balls) {
    for (Ball c : {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        const bool listed = std::any_of(balls.begin(), balls.end(), [c](const BallOnTable& b) { return b.ball == c; });
        if (!listed) balls.push_back(spotted(c));
    }
    return balls;
}

int foul_value(const Judgement& j, FoulKind kind) {
    for (const Foul& f : j.fouls) {
        if (f.kind == kind) return f.value;
    }
    return 0;
}

// A fair stroke that scored nothing: the other player comes to the table with nothing to decide.
void check_fair_no_score(const FrameState& pre, const Judgement& j) {
    CHECK(j.fouls.empty());
    CHECK(j.penalty == 0);
    CHECK(j.points == 0);
    CHECK_FALSE(has_call(j.calls, CallKind::foul));
    CHECK(j.post.pending == Pending::none);
    CHECK(j.post.striker == 1 - pre.striker);
    CHECK(j.post.scores[0] == pre.scores[0]);
    CHECK(j.post.scores[1] == pre.scores[1]);
}

// A foul stroke whose penalty is `value`, incurred by `kind`: the other player has the points and
// the choice of who plays.
void check_foul(const FrameState& pre, const Judgement& j, FoulKind kind, int value) {
    const std::size_t offender = std::size_t(pre.striker);
    CHECK(foul_value(j, kind) == value);
    CHECK(j.penalty == value);
    CHECK(j.points == 0);
    CHECK(has_call(j.calls, CallKind::foul));
    CHECK(j.post.scores[offender] == pre.scores[offender]);
    CHECK(j.post.scores[1 - offender] == pre.scores[1 - offender] + value);
    CHECK(j.post.pending == Pending::after_foul);
    CHECK(j.post.decider == 1 - pre.striker);
}

// Rule 6. The cue ball runs up the centre line at two balls side by side, each the same distance
// off the line, so its first impact is on both at one instant.
constexpr double kPairOffset = kR + 0.002;

BallOnTable cue_on_centre_line() { return ball(Ball::cue, 0.2, 0.0); }
BallOnTable upper(Ball b, int index = 0) { return ball(b, 0.5, kPairOffset, index); }
BallOnTable lower(Ball b, int index = 0) { return ball(b, 0.5, -kPairOffset, index); }

phys::CueStrike up_the_centre_line() { return strike(0.0, 1.0); }

// The cue ball's first impact was on exactly these two balls, at one instant by the referee's
// own measure, and nothing went into a pocket.
void require_struck_together(const phys::ShotRecord& r, int cue, int a, int b) {
    const std::vector<Contact> hits = cue_contacts(r, cue);
    REQUIRE(hits.size() >= 2);
    REQUIRE(std::min(hits[0].ball, hits[1].ball) == std::min(a, b));
    REQUIRE(std::max(hits[0].ball, hits[1].ball) == std::max(a, b));
    REQUIRE(hits[1].t - hits[0].t <= rig().ref.simultaneous_hit_s);
    if (hits.size() > 2) REQUIRE(hits[2].t - hits[0].t > rig().ref.simultaneous_hit_s);
    REQUIRE_FALSE(anything_potted(r));
}

// Rule 8. The cue ball starts with a ball against it.
const Vec2 kCue{0.3, 0.2};

// The centre of a ball touching the cue ball at kCue, `degrees` round from +x.
Vec2 against_cue(double degrees) {
    return {kCue.x + 2.0 * kR * std::cos(rad(degrees)), kCue.y + 2.0 * kR * std::sin(rad(degrees))};
}

// A point `distance` from kCue in that direction.
Vec2 out_from_cue(double degrees, double distance) {
    return {kCue.x + distance * std::cos(rad(degrees)), kCue.y + distance * std::sin(rad(degrees))};
}

bool touching_at_start(const Position& p, int place) {
    return std::abs(apart(p.balls[std::size_t(p.cue_ball)].pos, p.balls[std::size_t(place)].pos) - 2.0 * kR) < 1e-12;
}

// The slowest cue speed that still carries the cue ball to a ball along `aim`: it arrives with
// nothing left and comes to rest against that ball.
double speed_to_just_reach(const FrameState& s, double aim) {
    double lo = 0.02;
    double hi = 2.0;
    for (int i = 0; i < 50; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (cue_contacts(simulate(s, strike(aim, mid)), s.cue_ball).empty()) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return hi;
}

}  // namespace

TEST_CASE("S3.6: two reds struck at the same instant, red on, is a fair stroke") {
    const FrameState s =
        frame(rest_spotted({cue_on_centre_line(), upper(Ball::red, 0), lower(Ball::red, 1)}));
    const Played p = play(s, up_the_centre_line());
    require_struck_together(p.record, s.cue_ball, 1, 2);

    check_fair_no_score(s, p.judgement);
    CHECK(p.judgement.post.on.kind == OnKind::red);
}

TEST_CASE("S3.6: a red and a colour struck at the same instant is a foul") {
    const FrameState s = frame(
        rest_spotted({cue_on_centre_line(), upper(Ball::red, 0), lower(Ball::black), ball(Ball::red, -0.5, 0.5, 1)}));
    const Played p = play(s, up_the_centre_line());
    require_struck_together(p.record, s.cue_ball, 1, 2);

    // S3.11(c): the higher of the two, the black.
    check_foul(s, p.judgement, FoulKind::simultaneous_hit, 7);
    CHECK(p.judgement.post.on.kind == OnKind::red);
}

TEST_CASE("S3.6: the nominated free ball and a ball on struck at the same instant is a fair stroke") {
    FrameState s = frame(
        rest_spotted({cue_on_centre_line(), upper(Ball::red, 0), lower(Ball::blue), ball(Ball::red, -0.5, 0.5, 1)}));
    s.free_ball = true;
    const Played p = play(s, up_the_centre_line(), nominate(Ball::blue));
    require_struck_together(p.record, s.cue_ball, 1, 2);
    // The blue ends well off the cue ball's path to either red, so S3.12(b)(ii) is not in play.
    REQUIRE(off_path(rest(p.record, 0), rest(p.record, 1), rest(p.record, 2)) > 4.0 * kR);
    REQUIRE(off_path(rest(p.record, 0), rest(p.record, 3), rest(p.record, 2)) > 4.0 * kR);

    check_fair_no_score(s, p.judgement);
    CHECK(p.judgement.free_ball_used);
    CHECK_FALSE(p.judgement.post.free_ball);
    CHECK(p.judgement.post.on.kind == OnKind::red);
}

TEST_CASE("S3.6: the colour on and another colour struck at the same instant is a foul") {
    const FrameState s = frame(
        {cue_on_centre_line(), upper(Ball::brown), lower(Ball::blue), spotted(Ball::pink), spotted(Ball::black)});
    REQUIRE(s.on.kind == OnKind::colour);
    REQUIRE(s.on.colour == Ball::brown);
    const Played p = play(s, up_the_centre_line());
    require_struck_together(p.record, s.cue_ball, 1, 2);

    // S3.11(c): the brown on is 4, the blue with it 5.
    check_foul(s, p.judgement, FoulKind::simultaneous_hit, 5);
    CHECK(p.judgement.post.on.kind == OnKind::colour);
    CHECK(p.judgement.post.on.colour == Ball::brown);
}

TEST_CASE("S3.6: the declared colour and another colour struck at the same instant is a foul") {
    FrameState s = frame(rest_spotted({cue_on_centre_line(), upper(Ball::blue), lower(Ball::pink),
                                       ball(Ball::red, -0.5, 0.5, 0), ball(Ball::red, -0.5, -0.5, 1)}));
    s.on = {OnKind::any_colour, Ball::yellow};
    const Played p = play(s, up_the_centre_line(), nominate(Ball::blue));
    require_struck_together(p.record, s.cue_ball, 1, 2);

    // S3.11(c): the blue declared is 5, the pink with it 6.
    check_foul(s, p.judgement, FoulKind::simultaneous_hit, 6);
    CHECK(p.judgement.post.on.kind == OnKind::red);
}

TEST_CASE("S3.6: a red struck a moment before a colour is a fair stroke") {
    const FrameState s = frame(rest_spotted({cue_on_centre_line(), upper(Ball::red, 0),
                                             ball(Ball::black, 0.503, -kPairOffset), ball(Ball::red, -0.5, 0.5, 1)}));
    const Played p = play(s, up_the_centre_line());
    const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
    REQUIRE(hits.size() >= 2);
    REQUIRE(hits[0].ball == 1);
    REQUIRE(hits[1].ball == 2);
    REQUIRE(hits[1].t - hits[0].t > 1000.0 * rig().ref.simultaneous_hit_s);
    REQUIRE_FALSE(anything_potted(p.record));

    check_fair_no_score(s, p.judgement);
}

TEST_CASE("S3.8(a): the cue ball coming to rest against a red is called a touching ball") {
    const FrameState s = frame(
        rest_spotted({ball(Ball::cue, 0.2, 0.3), ball(Ball::red, 0.4, 0.3, 0), ball(Ball::red, 0.9, -0.4, 1)}));
    const Played p = play(s, strike(0.0, speed_to_just_reach(s, 0.0)));
    const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].ball == 1);
    const double gap = apart(rest(p.record, 0), rest(p.record, 1)) - 2.0 * kR;
    REQUIRE(gap > -1e-9);
    REQUIRE(gap < rig().ref.touching_gap_m);
    REQUIRE_FALSE(anything_potted(p.record));

    const FrameState& post = p.judgement.post;
    check_fair_no_score(s, p.judgement);
    CHECK(post.on.kind == OnKind::red);
    CHECK(has_call(p.judgement.calls, CallKind::touching_ball));
    CHECK(post.touching == std::vector<int>{place_of(post, Ball::red, 0)});
}

TEST_CASE("S3.8(a): resting against two colours after potting a red, both are indicated and the choice stays open") {
    // The black on its spot, the pink close by, and room for the cue ball to lie against both.
    const Vec2 black = table::spot(Ball::black);
    const Vec2 lie{black.x - 2.0 * kR, 0.0};
    const Vec2 pink{lie.x - kR, kR * std::sqrt(3.0)};
    REQUIRE(std::abs(apart(lie, black) - 2.0 * kR) < 1e-12);
    REQUIRE(std::abs(apart(lie, pink) - 2.0 * kR) < 1e-12);
    const FrameState s = frame(rest_spotted({ball(Ball::cue, 0.2, 0.3), ball(Ball::red, 0.5, 0.3, 0),
                                             ball(Ball::red, 0.9, -0.4, 1), ball(Ball::pink, pink.x, pink.y)}));
    ShotFacts facts = quiet_facts(s);
    facts.first_hit = {1};
    facts.first_hit_t = 0.3;
    facts.closest_gap_m[1] = 0.0;
    facts.pots.push_back({1, 1, 1.0});
    facts.rest[0] = lie;
    facts.cue_travel_m = 1.5;
    facts.object_travel_m = 1.0;
    const Judgement j = judge(s, {}, facts, *rig().oracle, rig().ref);

    const FrameState& post = j.post;
    CHECK(j.fouls.empty());
    CHECK(j.points == 1);
    CHECK(post.striker == 0);
    CHECK(post.on.kind == OnKind::any_colour);
    CHECK(has_call(j.calls, CallKind::touching_ball));
    std::vector<int> both = {place_of(post, Ball::pink), place_of(post, Ball::black)};
    std::sort(both.begin(), both.end());
    CHECK(post.touching == both);
    // The striker still says which colour they are on.
    CHECK(nominations(post).size() == 6);
}

TEST_CASE("S3.8(a): against two colours of choice and declaring neither, playing away from both costs seven") {
    FrameState s = frame(rest_spotted(
        {ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, against_cue(0.0).x, against_cue(0.0).y),
         ball(Ball::pink, against_cue(120.0).x, against_cue(120.0).y), ball(Ball::red, 0.9, -0.4, 0)}));
    s.on = {OnKind::any_colour, Ball::yellow};
    s.touching = {1, 2};
    REQUIRE(touching_at_start(s, 1));
    REQUIRE(touching_at_start(s, 2));
    const Played p = play(s, strike(rad(240.0), 0.5));
    REQUIRE(cue_contacts(p.record, s.cue_ball).empty());
    REQUIRE(stayed(s, p.record, 1));
    REQUIRE(stayed(s, p.record, 2));
    REQUIRE_FALSE(anything_potted(p.record));

    // Nobody can tell which of the two the striker was on: S3.11(d)(vi).
    check_foul(s, p.judgement, FoulKind::before_nominating, 7);
    CHECK_FALSE(p.judgement.nominated.has_nomination);
}

TEST_CASE("S3.8(b): playing into the touching red moves it: a push stroke") {
    FrameState s = frame(rest_spotted({ball(Ball::cue, kCue.x, kCue.y),
                                       ball(Ball::red, against_cue(0.0).x, against_cue(0.0).y, 0),
                                       ball(Ball::red, 0.9, -0.4, 1)}));
    s.touching = {1};
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(0.0, 0.5));
    REQUIRE(apart(s.balls[1].pos, rest(p.record, 1)) > 0.05);
    REQUIRE_FALSE(anything_potted(p.record));

    check_foul(s, p.judgement, FoulKind::push_stroke, 4);
    CHECK(p.judgement.post.on.kind == OnKind::red);
}

TEST_CASE("S3.8(b): a glancing stroke that moves the touching black, declared, is a push stroke of seven") {
    FrameState s = frame(rest_spotted({ball(Ball::cue, kCue.x, kCue.y),
                                       ball(Ball::black, against_cue(0.0).x, against_cue(0.0).y),
                                       ball(Ball::red, 0.9, -0.4, 0)}));
    s.on = {OnKind::any_colour, Ball::yellow};
    s.touching = {1};
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(rad(45.0), 0.5), nominate(Ball::black));
    REQUIRE(apart(s.balls[1].pos, rest(p.record, 1)) > 0.05);
    REQUIRE_FALSE(anything_potted(p.record));

    check_foul(s, p.judgement, FoulKind::push_stroke, 7);
}

TEST_CASE("S3.8(b), S2.19(b): moving a touching ball that is not on is a push stroke at that ball's value") {
    const FrameState s = frame(rest_spotted({ball(Ball::cue, kCue.x, kCue.y),
                                             ball(Ball::blue, against_cue(0.0).x, against_cue(0.0).y),
                                             ball(Ball::red, 0.9, -0.4, 0)}));
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(0.0, 0.5));
    REQUIRE(apart(s.balls[1].pos, rest(p.record, 1)) > 0.05);
    REQUIRE_FALSE(anything_potted(p.record));

    // S3.11(b)(v): red on is 4, the blue concerned 5.
    check_foul(s, p.judgement, FoulKind::push_stroke, 5);
}

TEST_CASE("S3.8(b), S2.19: played away, off a cushion and back onto the touching red is a fair stroke") {
    // The red lies under the cue ball, a hand's width from the side cushion. Nothing moves it as
    // the cue ball leaves; when the cue ball returns the tip is long gone, and a red is on.
    const Vec2 cue{0.5, 0.75};
    FrameState s = frame(rest_spotted(
        {ball(Ball::cue, cue.x, cue.y), ball(Ball::red, cue.x, cue.y - 2.0 * kR, 0), ball(Ball::red, 0.9, -0.4, 1)}));
    s.touching = {1};
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(rad(90.0), 0.8));
    double cushion_t = -1.0;
    for (const phys::ShotEvent& e : p.record.events) {
        if (e.kind == phys::EventKind::ball_line && e.a == s.cue_ball) {
            cushion_t = e.t;
            break;
        }
    }
    REQUIRE(cushion_t > 0.0);
    const phys::BallState red_then = p.record.ball_at(1, cushion_t);
    REQUIRE(apart(s.balls[1].pos, {red_then.r.x, red_then.r.y}) < 1e-12);
    const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].ball == 1);
    REQUIRE(hits[0].t > cushion_t);
    REQUIRE(apart(s.balls[1].pos, rest(p.record, 1)) > 0.05);
    REQUIRE_FALSE(anything_potted(p.record));

    check_fair_no_score(s, p.judgement);
    CHECK_FALSE(has_foul(p.judgement, FoulKind::push_stroke));
}

TEST_CASE("S3.8(c)(i): playing away from a touching red and hitting nothing else is a fair stroke") {
    FrameState s = frame(rest_spotted({ball(Ball::cue, kCue.x, kCue.y),
                                       ball(Ball::red, against_cue(0.0).x, against_cue(0.0).y, 0),
                                       ball(Ball::red, 0.9, -0.4, 1)}));
    s.touching = {1};
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(rad(135.0), 0.5));
    REQUIRE(cue_contacts(p.record, s.cue_ball).empty());
    REQUIRE(stayed(s, p.record, 1));
    REQUIRE(apart(kCue, rest(p.record, 0)) > 0.3);
    REQUIRE_FALSE(anything_potted(p.record));

    check_fair_no_score(s, p.judgement);
    CHECK(p.judgement.post.on.kind == OnKind::red);
    CHECK(p.judgement.post.touching.empty());
}

TEST_CASE("S3.8(c)(i): after playing away from a touching red, hitting a colour first is no foul") {
    const Vec2 black = out_from_cue(135.0, 0.3);
    FrameState s = frame(rest_spotted({ball(Ball::cue, kCue.x, kCue.y),
                                       ball(Ball::red, against_cue(0.0).x, against_cue(0.0).y, 0),
                                       ball(Ball::black, black.x, black.y), ball(Ball::red, 0.9, -0.4, 1)}));
    s.touching = {1};
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(rad(135.0), 0.5));
    const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].ball == 2);
    REQUIRE(stayed(s, p.record, 1));
    REQUIRE_FALSE(anything_potted(p.record));

    check_fair_no_score(s, p.judgement);
}

TEST_CASE("S3.8(c)(i): playing away from the touching colour on, the reds gone, is a fair stroke") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::yellow, against_cue(0.0).x, against_cue(0.0).y),
                          spotted(Ball::green), spotted(Ball::brown), spotted(Ball::blue), spotted(Ball::pink),
                          spotted(Ball::black)});
    REQUIRE(s.on.kind == OnKind::colour);
    REQUIRE(s.on.colour == Ball::yellow);
    s.touching = {1};
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(rad(135.0), 0.5));
    REQUIRE(cue_contacts(p.record, s.cue_ball).empty());
    REQUIRE(stayed(s, p.record, 1));
    REQUIRE_FALSE(anything_potted(p.record));

    check_fair_no_score(s, p.judgement);
    CHECK(p.judgement.post.on.kind == OnKind::colour);
    CHECK(p.judgement.post.on.colour == Ball::yellow);
}

TEST_CASE("S3.8(c)(ii): declaring the touching colour and playing away from it is a fair stroke") {
    FrameState s = frame(rest_spotted({ball(Ball::cue, kCue.x, kCue.y),
                                       ball(Ball::black, against_cue(0.0).x, against_cue(0.0).y),
                                       ball(Ball::red, 0.9, -0.4, 0)}));
    s.on = {OnKind::any_colour, Ball::yellow};
    s.touching = {1};
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(rad(135.0), 0.5), nominate(Ball::black));
    REQUIRE(cue_contacts(p.record, s.cue_ball).empty());
    REQUIRE(stayed(s, p.record, 1));
    REQUIRE_FALSE(anything_potted(p.record));

    check_fair_no_score(s, p.judgement);
    CHECK(p.judgement.nominated.has_nomination);
    CHECK(p.judgement.nominated.nominated == Ball::black);
    CHECK(p.judgement.post.on.kind == OnKind::red);
}

TEST_CASE("S3.8(c)(ii): the only touching colour, played away from with nothing said, is taken as declared") {
    FrameState s = frame(rest_spotted({ball(Ball::cue, kCue.x, kCue.y),
                                       ball(Ball::black, against_cue(0.0).x, against_cue(0.0).y),
                                       ball(Ball::red, 0.9, -0.4, 0)}));
    s.on = {OnKind::any_colour, Ball::yellow};
    s.touching = {1};
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(rad(135.0), 0.5));
    REQUIRE(cue_contacts(p.record, s.cue_ball).empty());
    REQUIRE(stayed(s, p.record, 1));
    REQUIRE_FALSE(anything_potted(p.record));

    check_fair_no_score(s, p.judgement);
    CHECK(p.judgement.nominated.has_nomination);
    CHECK(p.judgement.nominated.nominated == Ball::black);
}

TEST_CASE("S3.8(c)(iii): declaring and first hitting another colour, the touching one left still, is a fair stroke") {
    const Vec2 blue = out_from_cue(135.0, 0.3);
    FrameState s = frame(rest_spotted({ball(Ball::cue, kCue.x, kCue.y),
                                       ball(Ball::black, against_cue(0.0).x, against_cue(0.0).y),
                                       ball(Ball::blue, blue.x, blue.y), ball(Ball::red, 0.9, -0.4, 0)}));
    s.on = {OnKind::any_colour, Ball::yellow};
    s.touching = {1};
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(rad(135.0), 0.5), nominate(Ball::blue));
    const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].ball == 2);
    REQUIRE(stayed(s, p.record, 1));
    REQUIRE_FALSE(anything_potted(p.record));

    check_fair_no_score(s, p.judgement);
    CHECK(p.judgement.nominated.nominated == Ball::blue);
}

TEST_CASE("S3.8(c): declaring another colour and missing it is a foul, whatever the cue ball was touching") {
    FrameState s = frame(rest_spotted({ball(Ball::cue, kCue.x, kCue.y),
                                       ball(Ball::black, against_cue(0.0).x, against_cue(0.0).y),
                                       ball(Ball::red, 0.9, -0.4, 0)}));
    s.on = {OnKind::any_colour, Ball::yellow};
    s.touching = {1};
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(rad(135.0), 0.5), nominate(Ball::blue));
    REQUIRE(cue_contacts(p.record, s.cue_ball).empty());
    REQUIRE(stayed(s, p.record, 1));
    REQUIRE_FALSE(anything_potted(p.record));

    // S3.11(a)(vi): the value of the blue declared.
    check_foul(s, p.judgement, FoulKind::no_hit, 5);
}

TEST_CASE("S3.8(d): resting against a ball that is not on is not called") {
    // The cue ball ends against the blue with red on for the next player, and, after a red is
    // potted, against a red with a colour to come.
    FrameState s = frame(rest_spotted({ball(Ball::cue, 0.2, 0.3), ball(Ball::red, 0.5, 0.3, 0),
                                       ball(Ball::red, -0.6, 0.5, 1), ball(Ball::blue, -0.3, 0.5)}));
    ShotFacts facts = quiet_facts(s);
    facts.first_hit = {1};
    facts.first_hit_t = 0.3;
    facts.closest_gap_m[1] = 0.0;
    facts.cue_travel_m = 1.5;
    facts.object_travel_m = 1.0;

    SUBCASE("a colour, with red on next") {
        facts.rest[0] = {s.balls[3].pos.x + 2.0 * kR, 0.5};
        const Judgement j = judge(s, {}, facts, *rig().oracle, rig().ref);
        check_fair_no_score(s, j);
        CHECK(j.post.on.kind == OnKind::red);
        CHECK_FALSE(has_call(j.calls, CallKind::touching_ball));
        CHECK(j.post.touching.empty());
    }
    SUBCASE("a red, with a colour of choice next") {
        facts.pots.push_back({1, 1, 1.0});
        facts.rest[0] = {s.balls[2].pos.x + 2.0 * kR, 0.5};
        const Judgement j = judge(s, {}, facts, *rig().oracle, rig().ref);
        CHECK(j.fouls.empty());
        CHECK(j.points == 1);
        CHECK(j.post.on.kind == OnKind::any_colour);
        CHECK_FALSE(has_call(j.calls, CallKind::touching_ball));
        CHECK(j.post.touching.empty());
    }
}

TEST_CASE("S3.8(d): a touching ball that is not on gives no cover: playing away and hitting nothing is a foul") {
    const FrameState s = frame(rest_spotted({ball(Ball::cue, kCue.x, kCue.y),
                                             ball(Ball::blue, against_cue(0.0).x, against_cue(0.0).y),
                                             ball(Ball::red, 0.9, -0.4, 0)}));
    REQUIRE(touching_at_start(s, 1));
    const Played p = play(s, strike(rad(135.0), 0.5));
    REQUIRE(cue_contacts(p.record, s.cue_ball).empty());
    REQUIRE(stayed(s, p.record, 1));
    REQUIRE_FALSE(anything_potted(p.record));

    check_foul(s, p.judgement, FoulKind::no_hit, 4);
}

TEST_CASE("S3.8(e): against a ball on and a ball not on, only the ball on is indicated") {
    // The cue ball ends between a red and the blue, touching both, with red on for the next player.
    const FrameState s = frame(rest_spotted({ball(Ball::cue, 0.2, 0.3), ball(Ball::red, 0.5, 0.3, 0),
                                             ball(Ball::red, -0.6, 0.5, 1), ball(Ball::blue, -0.6 + 4.0 * kR, 0.5)}));
    ShotFacts facts = quiet_facts(s);
    facts.first_hit = {1};
    facts.first_hit_t = 0.3;
    facts.closest_gap_m[1] = 0.0;
    facts.rest[0] = {-0.6 + 2.0 * kR, 0.5};
    facts.cue_travel_m = 1.5;
    facts.object_travel_m = 1.0;
    REQUIRE(std::abs(apart(facts.rest[0], s.balls[2].pos) - 2.0 * kR) < 1e-12);
    REQUIRE(std::abs(apart(facts.rest[0], s.balls[3].pos) - 2.0 * kR) < 1e-12);
    const Judgement j = judge(s, {}, facts, *rig().oracle, rig().ref);

    check_fair_no_score(s, j);
    CHECK(j.post.on.kind == OnKind::red);
    CHECK(has_call(j.calls, CallKind::touching_ball));
    CHECK(j.post.touching == std::vector<int>{place_of(j.post, Ball::red, 1)});
}
