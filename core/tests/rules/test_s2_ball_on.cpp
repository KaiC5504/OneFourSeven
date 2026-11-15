// WPBSA Section 2: ball on, nominated ball, free ball and miss, each shown by play. A REQUIRE is a
// premise read from the simulator's own record (never from observe()), or a check that the frame
// carried on from it; a CHECK is the rule's outcome.

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "scenario.hpp"

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kToTop = 0.0;         // +x
constexpr double kToLeft = kPi / 2.0;  // +y: along the line between the middle pockets
constexpr double kToBaulk = kPi;
constexpr double kToRight = -kPi / 2.0;

// A straight pot into the left middle pocket, played at kToLeft.
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

std::string places_text(const std::vector<int>& places) {
    std::string out;
    for (int p : places) {
        if (!out.empty()) out += ",";
        out += std::to_string(p);
    }
    return out;
}

std::string balls_text(const std::vector<Ball>& balls) {
    std::string out;
    for (Ball b : balls) {
        if (!out.empty()) out += ",";
        out += std::string(table::ball_name(b));
    }
    return out;
}

// "black", or "" when the stroke was judged with no nomination.
std::string nominated_text(const Judgement& j) {
    return j.nominated.has_nomination ? std::string(table::ball_name(j.nominated.nominated)) : std::string();
}

bool decide_now(FrameState& s, DecisionKind kind, int player) {
    std::vector<Call> calls;
    std::string error;
    return decide(s, {kind, player}, *rig().oracle, rig().ref, calls, error);
}

std::vector<BallOnTable> six_colours() {
    return {spotted(Ball::yellow), spotted(Ball::green), spotted(Ball::brown),
            spotted(Ball::blue),   spotted(Ball::pink),  spotted(Ball::black)};
}

// The cue ball tucked behind the brown with the only red beyond it: snookered, so a foul that
// leaves this (or leaves the cue ball further back on the same line) brings a free ball. The
// brown, hit full from there, runs past the red into the left middle pocket.
constexpr Vec2 kBrown{0.0, 0.38};
constexpr Vec2 kHiddenRed{0.08, 0.80};

}  // namespace

TEST_CASE("S2.11: with red on, every red is a ball on and any of them may be hit first") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.3, 0.3), ball(Ball::red, -0.3, 0.3, 1),
                                spotted(Ball::blue), spotted(Ball::black)});
    CHECK(on_text(s) == "red");
    CHECK(places_text(places_on(s)) == "1,2");

    const Played one = play(s, strike(kToTop, 0.6));
    REQUIRE(first_hit(one.record, 0) == 1);
    REQUIRE(drops(one.record) == 0);
    CHECK(fouls_text(one.judgement) == "");

    const Played other = play(s, strike(kToBaulk, 0.6));
    REQUIRE(first_hit(other.record, 0) == 2);
    REQUIRE(drops(other.record) == 0);
    CHECK(fouls_text(other.judgement) == "");
}

TEST_CASE("S2.11: after a red every colour could be on, and the one declared may be hit first") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::blue, 0.3, 0.3), ball(Ball::pink, -0.3, 0.3),
                          ball(Ball::red, 0.6, -0.4), spotted(Ball::black)});
    s.on = {OnKind::any_colour, Ball::yellow};
    CHECK(places_text(places_on(s)) == "1,2,4");

    const Played blue = play(s, strike(kToTop, 0.6), nominate(Ball::blue));
    REQUIRE(first_hit(blue.record, 0) == 1);
    REQUIRE(drops(blue.record) == 0);
    CHECK(fouls_text(blue.judgement) == "");

    const Played pink = play(s, strike(kToBaulk, 0.6), nominate(Ball::pink));
    REQUIRE(first_hit(pink.record, 0) == 2);
    REQUIRE(drops(pink.record) == 0);
    CHECK(fouls_text(pink.judgement) == "");
}

TEST_CASE("S2.11: with the reds gone only the lowest colour left is on") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::green, 0.3, 0.3),
                                ball(Ball::brown, -0.3, 0.3), spotted(Ball::black)});
    CHECK(on_text(s) == "colour green");
    CHECK(places_text(places_on(s)) == "1");

    const Played green = play(s, strike(kToTop, 0.6));
    REQUIRE(first_hit(green.record, 0) == 1);
    REQUIRE(drops(green.record) == 0);
    CHECK(fouls_text(green.judgement) == "");

    const Played brown = play(s, strike(kToBaulk, 0.6));
    REQUIRE(first_hit(brown.record, 0) == 2);
    REQUIRE(drops(brown.record) == 0);
    CHECK(fouls_text(brown.judgement) == "first_hit_not_on 4");
}

TEST_CASE("S2.11: a ball on that may not be hit first may still be potted") {
    // A free ball is nominated, so the red may not take the first impact; the yellow is played
    // on to it and the red goes in.
    FrameState s = frame({ball(Ball::cue, 0.0, 0.2), ball(Ball::yellow, 0.0, 0.45), ball(Ball::red, 0.0, 0.7),
                          spotted(Ball::black)});
    s.free_ball = true;
    CHECK(places_text(places_on(s)) == "2");

    const Played p = play(s, strike(kToLeft, 2.0), nominate(Ball::yellow));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 2));
    REQUIRE(drops(p.record) == 1);

    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "");
    CHECK(p.judgement.points == 1);
    CHECK(p.judgement.free_ball_used);
    CHECK(nominated_text(p.judgement) == "yellow");
    CHECK(score(post) == "1-0");
    CHECK(count(post, Ball::red) == 0);
    CHECK(p.judgement.spotted.empty());
    CHECK(lies_at(post, Ball::yellow, rest_of(p.record, 1)));
    CHECK(post.striker == 0);
    CHECK(on_text(post) == "any_colour");
}

TEST_CASE("S2.12(a): the colour the striker declares is the ball on for that stroke") {
    std::vector<BallOnTable> balls{ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, kObject.x, kObject.y),
                                   ball(Ball::red, 0.6, -0.4), spotted(Ball::blue), spotted(Ball::pink)};
    FrameState s = frame(balls);
    s.on = {OnKind::any_colour, Ball::yellow};
    const Played p = play(s, strike(kToLeft, 2.0), nominate(Ball::black));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(drops(p.record) == 1);

    CHECK(fouls_text(p.judgement) == "");
    CHECK(nominated_text(p.judgement) == "black");
    CHECK_FALSE(p.judgement.free_ball_used);
    CHECK(p.judgement.points == 7);
    CHECK(score(p.judgement.post) == "7-0");
    CHECK(lies_at(p.judgement.post, Ball::black, table::spot(Ball::black)));
}

TEST_CASE("S2.12(a): hitting another colour than the one declared is a foul at the higher of the two values") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::blue, 0.3, 0.3), ball(Ball::red, 0.6, -0.4),
                          spotted(Ball::yellow), spotted(Ball::black)});
    s.on = {OnKind::any_colour, Ball::yellow};

    const Played black = play(s, strike(kToTop, 0.6), nominate(Ball::black));
    REQUIRE(first_hit(black.record, 0) == 1);
    REQUIRE(drops(black.record) == 0);
    CHECK(fouls_text(black.judgement) == "first_hit_not_on 7");
    CHECK(black.judgement.penalty == 7);
    CHECK(nominated_text(black.judgement) == "black");
    CHECK(score(black.judgement.post) == "0-7");

    const Played yellow = play(s, strike(kToTop, 0.6), nominate(Ball::yellow));
    REQUIRE(first_hit(yellow.record, 0) == 1);
    REQUIRE(drops(yellow.record) == 0);
    CHECK(fouls_text(yellow.judgement) == "first_hit_not_on 5");
    CHECK(yellow.judgement.penalty == 5);
    CHECK(score(yellow.judgement.post) == "0-5");
}

TEST_CASE("S2.12: with nothing declared, the first colour hit is taken as the one nominated") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::pink, kObject.x, kObject.y),
                          ball(Ball::red, 0.6, -0.4), spotted(Ball::black)});
    s.on = {OnKind::any_colour, Ball::yellow};
    const Played p = play(s, strike(kToLeft, 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(drops(p.record) == 1);

    CHECK(fouls_text(p.judgement) == "");
    CHECK(nominated_text(p.judgement) == "pink");
    CHECK(p.judgement.points == 6);
    CHECK(lies_at(p.judgement.post, Ball::pink, table::spot(Ball::pink)));
}

TEST_CASE("S2.12: a declaration where none is open, or of a ball that cannot be declared, counts for nothing") {
    const FrameState red_on = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.3, 0.3), spotted(Ball::black)});
    const Played red = play(red_on, strike(kToTop, 0.6), nominate(Ball::black));
    REQUIRE(first_hit(red.record, 0) == 1);
    REQUIRE(drops(red.record) == 0);
    CHECK(fouls_text(red.judgement) == "");
    CHECK(nominated_text(red.judgement) == "");

    FrameState colour_on = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::blue, kObject.x, kObject.y),
                                  ball(Ball::red, 0.6, -0.4), spotted(Ball::black)});
    colour_on.on = {OnKind::any_colour, Ball::yellow};
    const Played blue = play(colour_on, strike(kToLeft, 2.0), nominate(Ball::red));
    REQUIRE(first_hit(blue.record, 0) == 1);
    REQUIRE(dropped(blue.record, 1));
    REQUIRE(drops(blue.record) == 1);
    CHECK(fouls_text(blue.judgement) == "");
    CHECK(nominated_text(blue.judgement) == "blue");
    CHECK(blue.judgement.points == 5);
}

TEST_CASE("S2.12: which balls may be declared") {
    std::vector<BallOnTable> all = six_colours();
    all.insert(all.begin(), ball(Ball::cue, kCue.x, kCue.y));
    all.push_back(ball(Ball::red, 0.6, -0.4));

    FrameState s = frame(all);
    CHECK(balls_text(nominations(s)) == "");

    s.on = {OnKind::any_colour, Ball::yellow};
    CHECK(balls_text(nominations(s)) == "yellow,green,brown,blue,pink,black");

    // A free ball with red on: any colour, never a red.
    s.on = {OnKind::red, Ball::yellow};
    s.free_ball = true;
    CHECK(balls_text(nominations(s)) == "yellow,green,brown,blue,pink,black");

    FrameState colours = frame({ball(Ball::cue, kCue.x, kCue.y), spotted(Ball::green), spotted(Ball::brown),
                                spotted(Ball::blue), spotted(Ball::pink), spotted(Ball::black)});
    CHECK(on_text(colours) == "colour green");
    CHECK(balls_text(nominations(colours)) == "");
    colours.free_ball = true;
    CHECK(balls_text(nominations(colours)) == "brown,blue,pink,black");
}

TEST_CASE("S2.13: snookered after a foul, the next player may nominate any ball and play it as the ball on") {
    const FrameState s = frame({ball(Ball::cue, 0.0, 0.30), ball(Ball::brown, kBrown.x, kBrown.y),
                                ball(Ball::red, kHiddenRed.x, kHiddenRed.y), spotted(Ball::black)});
    // Rolled back along the line through the brown, touching nothing.
    const Played foul = play(s, strike(kToRight, 0.15));
    REQUIRE(first_hit(foul.record, 0) == -1);
    REQUIRE(drops(foul.record) == 0);
    REQUIRE(std::abs(rest_of(foul.record, 0).x) < 0.005);
    REQUIRE(rest_of(foul.record, 0).y < 0.25);

    CHECK(fouls_text(foul.judgement) == "no_hit 4");
    CHECK(has_call(foul.judgement.calls, CallKind::free_ball));
    CHECK(foul.judgement.post.free_ball);
    CHECK(foul.judgement.post.pending == Pending::after_foul);
    CHECK(foul.judgement.post.decider == 1);

    FrameState next = foul.judgement.post;
    REQUIRE(decide_now(next, DecisionKind::play, 1));
    REQUIRE(same(cue_pos(next), rest_of(foul.record, 0)));
    CHECK(next.free_ball);
    CHECK(next.striker == 1);
    CHECK(on_text(next) == "red");
    CHECK(balls_text(nominations(next)) == "brown,black");

    const int brown = place_of(next, Ball::brown);
    const int red = place_of(next, Ball::red);
    REQUIRE(brown >= 0);
    REQUIRE(red >= 0);
    const Played free = play(next, strike(aim_at_ball(next, brown), 2.0), nominate(Ball::brown));
    REQUIRE(first_hit(free.record, next.cue_ball) == brown);
    REQUIRE(dropped(free.record, brown));
    REQUIRE(drops(free.record) == 1);
    REQUIRE(same(rest_of(free.record, red), kHiddenRed));

    const FrameState& post = free.judgement.post;
    CHECK(fouls_text(free.judgement) == "");
    CHECK(free.judgement.free_ball_used);
    CHECK(nominated_text(free.judgement) == "brown");
    // It takes the value of the ball on, a red, and goes back on its spot.
    CHECK(free.judgement.points == 1);
    CHECK(score(post) == "0-5");
    CHECK(lies_at(post, Ball::brown, table::spot(Ball::brown)));
    CHECK(count(post, Ball::red) == 1);
    CHECK(post.striker == 1);
    CHECK(on_text(post) == "any_colour");
    CHECK_FALSE(post.free_ball);
}

TEST_CASE("S2.13: there is no free ball for an offender asked to play again") {
    const FrameState s = frame({ball(Ball::cue, 0.0, 0.30), ball(Ball::brown, kBrown.x, kBrown.y),
                                ball(Ball::red, kHiddenRed.x, kHiddenRed.y), spotted(Ball::black)});
    const Played foul = play(s, strike(kToRight, 0.15));
    REQUIRE(first_hit(foul.record, 0) == -1);
    REQUIRE(drops(foul.record) == 0);
    REQUIRE(std::abs(rest_of(foul.record, 0).x) < 0.005);
    CHECK(has_call(foul.judgement.calls, CallKind::free_ball));

    FrameState again = foul.judgement.post;
    CHECK(decide_now(again, DecisionKind::play_again, 1));
    CHECK(again.striker == 0);
    CHECK_FALSE(again.free_ball);
    CHECK(balls_text(nominations(again)) == "");
}

TEST_CASE("S2.21(a): the cue ball failing to reach a ball on in plain view is a miss") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.6, 0.3), spotted(Ball::black)});
    const Played p = play(s, strike(kToBaulk, 0.15));
    REQUIRE(first_hit(p.record, 0) == -1);
    REQUIRE(drops(p.record) == 0);

    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "no_hit 4");
    CHECK(p.judgement.miss);
    CHECK(p.judgement.miss_reason == "S3.14(c)");
    CHECK(has_call(p.judgement.calls, CallKind::miss));
    CHECK(post.last_miss);
    CHECK(post.stats[0].misses == 1);
    CHECK(offered(post, 1) == "play,play_again,replace");
}

TEST_CASE("S2.21(a): a ball not on taking the first impact is a miss as well") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, 0.3, 0.3), ball(Ball::red, -0.6, 0.3)});
    const Played p = play(s, strike(kToTop, 0.6));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(drops(p.record) == 0);

    CHECK(fouls_text(p.judgement) == "first_hit_not_on 7");
    CHECK(p.judgement.miss);
    CHECK(has_call(p.judgement.calls, CallKind::miss));
    CHECK(offered(p.judgement.post, 1) == "play,play_again,replace");
}

TEST_CASE("S2.21: a foul made after the ball on was hit first is not a miss") {
    const FrameState s = frame({ball(Ball::cue, kInOffCue.x, kInOffCue.y), ball(Ball::red, kInOffObject.x, kInOffObject.y),
                                ball(Ball::red, 0.6, -0.4, 1), spotted(Ball::black)});
    const Played p = play(s, strike(aim_at(s, kInOffAim), 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 0));
    REQUIRE(drops(p.record) == 1);

    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "in_off 4");
    CHECK_FALSE(p.judgement.miss);
    CHECK(p.judgement.miss_reason == "");
    CHECK_FALSE(has_call(p.judgement.calls, CallKind::miss));
    CHECK_FALSE(post.last_miss);
    CHECK(post.stats[0].misses == 0);
    CHECK(offered(post, 1) == "play,play_again");
}

TEST_CASE("S2.21(b): with a free ball nominated, another ball taking the first impact is a miss") {
    FrameState s = frame({ball(Ball::cue, 0.0, 0.176), ball(Ball::brown, kBrown.x, kBrown.y),
                          ball(Ball::red, kHiddenRed.x, kHiddenRed.y), ball(Ball::black, 0.3, 0.176)});
    s.free_ball = true;
    s.striker = 1;
    const Played p = play(s, strike(kToTop, 0.6), nominate(Ball::brown));
    REQUIRE(first_hit(p.record, 0) == 3);
    REQUIRE(drops(p.record) == 0);

    CHECK(fouls_text(p.judgement) == "first_hit_not_on 7");
    CHECK(p.judgement.miss);
    CHECK(has_call(p.judgement.calls, CallKind::miss));
    CHECK(score(p.judgement.post) == "7-0");
    CHECK(offered(p.judgement.post, 0) == "play,play_again,replace");
}

TEST_CASE("S2.21(b): the free ball and a ball on hit together is no miss and no foul") {
    FrameState s = frame({ball(Ball::cue, 0.0, 0.176), ball(Ball::brown, kBrown.x, kBrown.y),
                          ball(Ball::red, kHiddenRed.x, kHiddenRed.y), spotted(Ball::black)});
    s.free_ball = true;
    ShotFacts facts = quiet_facts(s);
    facts.first_hit = {1, 2};
    facts.first_hit_t = 0.2;
    facts.closest_gap_m[1] = 0.0;
    facts.closest_gap_m[2] = 0.0;
    // Everything ends in the open: no snooker behind the free ball.
    facts.rest[0] = {0.5, 0.0};
    facts.rest[1] = {-0.4, 0.5};
    facts.rest[2] = {0.5, 0.6};
    facts.cue_travel_m = 0.8;
    facts.object_travel_m = 0.9;
    const Judgement j = judge(s, nominate(Ball::brown), facts, *rig().oracle, rig().ref);

    CHECK(fouls_text(j) == "");
    CHECK_FALSE(j.miss);
    CHECK_FALSE(has_call(j.calls, CallKind::miss));
    CHECK(j.free_ball_used);
    CHECK(j.post.striker == 1);
    CHECK(j.post.pending == Pending::none);
}
