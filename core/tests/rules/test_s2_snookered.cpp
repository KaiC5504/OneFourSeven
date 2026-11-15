// WPBSA Section 2: snookered, spot occupied and push stroke. The snooker cases are positions, so
// most use hand-made facts (a stroke that moved nothing, or one that left the balls where the test
// says) judged with the real oracle; REQUIREs there are the test's own geometry. Where a stroke is
// simulated, a REQUIRE is read from the simulator's record, never from observe().

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
constexpr double kDiameter = table::kBallDiameter;

// A straight pot into the left middle pocket, played at kToLeft.
constexpr Vec2 kCue{0.0, 0.3};
constexpr Vec2 kObject{0.0, 0.6};

int first_hit(const phys::ShotRecord& r, int cue) {
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind != phys::EventKind::ball_ball) continue;
        if (e.a == cue) return e.b;
        if (e.b == cue) return e.a;
    }
    return -1;
}

// When the cue ball first touched another ball; -1 when it never did.
double first_hit_time(const phys::ShotRecord& r, int cue) {
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind == phys::EventKind::ball_ball && (e.a == cue || e.b == cue)) return e.t;
    }
    return -1.0;
}

bool dropped(const phys::ShotRecord& r, int place) {
    return r.final_state.balls[std::size_t(place)].motion == phys::Motion::pocketed;
}

int drops(const phys::ShotRecord& r) {
    int n = 0;
    for (const phys::BallState& b : r.final_state.balls) n += b.motion == phys::Motion::pocketed ? 1 : 0;
    return n;
}

bool same(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y) < 1e-9; }

bool lies_at(const Position& p, Ball b, Vec2 where, int index = 0) {
    const int place = place_of(p, b, index);
    return place >= 0 && same(p.balls[std::size_t(place)].pos, where);
}

std::string score(const FrameState& s) { return std::to_string(s.scores[0]) + "-" + std::to_string(s.scores[1]); }

std::string fouls_text(const Judgement& j) {
    std::string out;
    for (const Foul& f : j.fouls) {
        if (!out.empty()) out += ", ";
        out += std::string(foul_name(f.kind)) + " " + std::to_string(f.value);
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

std::vector<int> reds(const Position& p) {
    std::vector<int> out;
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        if (p.balls[i].ball == Ball::red) out.push_back(int(i));
    }
    return out;
}

// The test's own geometry, so a position is known to be what its name says before the oracle
// is asked. Margins are several millimetres everywhere.
double off_line(Vec2 p, Vec2 a, Vec2 b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / (dx * dx + dy * dy);
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Where the cue ball's centre is as it clips `on` at its extreme edge on the left (+1) or the
// right (-1), seen from `cue`.
Vec2 edge_contact(Vec2 cue, Vec2 on, int side) {
    const double d = std::hypot(on.x - cue.x, on.y - cue.y);
    const Vec2 u{(on.x - cue.x) / d, (on.y - cue.y) / d};
    return {on.x - side * kDiameter * u.y, on.y + side * kDiameter * u.x};
}

// `blocker` is in the way of the cue ball travelling from `cue` to `target`.
bool blocks(Vec2 cue, Vec2 target, Vec2 blocker) { return off_line(blocker, cue, target) < kDiameter; }

Vec2 at(const Position& p, int place) { return p.balls[std::size_t(place)].pos; }

// A stroke that touched nothing and moved nothing worth the name: a foul, after which the next
// player is given a free ball exactly when the position is a snooker.
Judgement misses_everything(const FrameState& s) { return judge(s, {}, quiet_facts(s), *rig().oracle, rig().ref); }

// The cue ball hit the red at `red` and went in the top left pocket, every other ball staying put.
Judgement goes_in_off(const FrameState& s, int red) {
    ShotFacts f = quiet_facts(s);
    f.first_hit = {red};
    f.first_hit_t = 0.3;
    f.closest_gap_m[std::size_t(red)] = 0.0;
    f.cue_potted = true;
    f.cue_pocket = 0;
    f.cue_travel_m = 0.8;
    return judge(s, {}, f, *rig().oracle, rig().ref);
}

// The blue (place 1), nominated as a free ball with red on, was hit first, nothing was potted
// and the balls came to rest as `rest` lists them. Only the resting places matter to the rule, so
// the stroke is not simulated: the cue ball and the blue start a little way off, clear of the rest.
Judgement after_free_ball(const FrameState& rest) {
    FrameState pre = rest;
    pre.free_ball = true;
    for (std::size_t i : {std::size_t(0), std::size_t(1)}) {
        pre.balls[i].pos.x -= 0.25;
        pre.balls[i].pos.y -= 0.35;
    }
    ShotFacts f = quiet_facts(pre);
    for (std::size_t i = 0; i < rest.balls.size(); ++i) f.rest[i] = rest.balls[i].pos;
    f.first_hit = {1};
    f.first_hit_t = 0.2;
    f.closest_gap_m[1] = 0.0;
    f.cue_travel_m = 0.4;
    f.object_travel_m = 0.3;
    return judge(pre, nominate(Ball::blue), f, *rig().oracle, rig().ref);
}

std::string snookering(const FrameState& rest) {
    return places_text(rig().oracle->snookering_balls(rest.balls, rest.cue_ball, reds(rest)));
}

}  // namespace

TEST_CASE("S2.17: a ball not on dead in line with the only red is a snooker") {
    const FrameState s = frame({ball(Ball::cue, -0.5, 0.2), ball(Ball::black, -0.2, 0.2), ball(Ball::red, 0.4, 0.2)});
    REQUIRE(blocks(at(s, 0), edge_contact(at(s, 0), at(s, 2), +1), at(s, 1)));
    REQUIRE(blocks(at(s, 0), edge_contact(at(s, 0), at(s, 2), -1), at(s, 1)));

    CHECK(rig().oracle->snookered(s.balls, s.cue_ball, reds(s), false));
    const Judgement j = misses_everything(s);
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK(has_call(j.calls, CallKind::free_ball));
    CHECK(j.post.free_ball);
}

TEST_CASE("S2.17: one extreme edge obstructed is a snooker, though the ball on can be hit full") {
    const FrameState s = frame({ball(Ball::cue, -0.5, 0.2), ball(Ball::black, -0.2, 0.1425), ball(Ball::red, 0.4, 0.2)});
    REQUIRE(blocks(at(s, 0), edge_contact(at(s, 0), at(s, 2), -1), at(s, 1)));
    REQUIRE_FALSE(blocks(at(s, 0), edge_contact(at(s, 0), at(s, 2), +1), at(s, 1)));
    REQUIRE_FALSE(blocks(at(s, 0), at(s, 2), at(s, 1)));

    CHECK(rig().oracle->snookered(s.balls, s.cue_ball, reds(s), false));
    const Judgement j = misses_everything(s);
    CHECK(has_call(j.calls, CallKind::free_ball));
    CHECK(j.post.free_ball);
}

TEST_CASE("S2.17: one red open at both extreme edges is no snooker, whatever hides the others") {
    const FrameState s = frame({ball(Ball::cue, -0.5, 0.2), ball(Ball::black, -0.2, 0.2), ball(Ball::red, 0.4, 0.2),
                                ball(Ball::red, 0.0, -0.5, 1)});
    REQUIRE(blocks(at(s, 0), at(s, 2), at(s, 1)));
    REQUIRE_FALSE(blocks(at(s, 0), edge_contact(at(s, 0), at(s, 3), +1), at(s, 1)));
    REQUIRE_FALSE(blocks(at(s, 0), edge_contact(at(s, 0), at(s, 3), -1), at(s, 1)));

    CHECK_FALSE(rig().oracle->snookered(s.balls, s.cue_ball, reds(s), false));
    const Judgement j = misses_everything(s);
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK_FALSE(has_call(j.calls, CallKind::free_ball));
    CHECK_FALSE(j.post.free_ball);
}

TEST_CASE("S2.17: only balls not on obstruct: a red hidden behind another red is not snookered") {
    // The black hides one edge of the near red, which alone would be a snooker; the far red is
    // behind the near one but clear of the black on both edges.
    const std::vector<BallOnTable> near_only{ball(Ball::cue, -0.6, 0.3), ball(Ball::black, -0.3, 0.23),
                                             ball(Ball::red, 0.0, 0.3)};
    std::vector<BallOnTable> both = near_only;
    both.push_back(ball(Ball::red, 0.6, 0.32, 1));
    const FrameState one = frame(near_only);
    const FrameState two = frame(both);
    REQUIRE(blocks(at(two, 0), edge_contact(at(two, 0), at(two, 2), -1), at(two, 1)));
    REQUIRE(blocks(at(two, 0), at(two, 3), at(two, 2)));
    REQUIRE_FALSE(blocks(at(two, 0), edge_contact(at(two, 0), at(two, 3), +1), at(two, 1)));
    REQUIRE_FALSE(blocks(at(two, 0), edge_contact(at(two, 0), at(two, 3), -1), at(two, 1)));

    CHECK(rig().oracle->snookered(one.balls, one.cue_ball, reds(one), false));
    CHECK(has_call(misses_everything(one).calls, CallKind::free_ball));

    CHECK_FALSE(rig().oracle->snookered(two.balls, two.cue_ball, reds(two), false));
    const Judgement j = misses_everything(two);
    CHECK_FALSE(has_call(j.calls, CallKind::free_ball));
    CHECK_FALSE(j.post.free_ball);
}

TEST_CASE("S2.17(a): in hand, the cue ball is snookered when no place in the D has both edges clear") {
    // The red sits just behind the black near the top cushion: from the middle of the D both edges
    // are hidden, from its corners one still is.
    const FrameState s = frame({ball(Ball::cue, 1.60, 0.4), ball(Ball::black, 1.545, 0.0), ball(Ball::red, 1.60, 0.0)});
    const double baulk = table::baulk_line_x();
    for (Vec2 from : {Vec2{baulk, 0.0}, Vec2{baulk, table::kDRadius}, Vec2{baulk, -table::kDRadius},
                      Vec2{baulk - table::kDRadius, 0.0}}) {
        CAPTURE(from.x);
        CAPTURE(from.y);
        const bool left = blocks(from, edge_contact(from, at(s, 2), +1), at(s, 1));
        const bool right = blocks(from, edge_contact(from, at(s, 2), -1), at(s, 1));
        REQUIRE((left || right));
    }

    CHECK(rig().oracle->snookered(s.balls, s.cue_ball, reds(s), true));
    const Judgement j = goes_in_off(s, 2);
    CHECK(fouls_text(j) == "in_off 4");
    CHECK(j.post.cue_in_hand);
    CHECK(has_call(j.calls, CallKind::free_ball));
    CHECK(j.post.free_ball);
}

TEST_CASE("S2.17(a): in hand, one clear place in the D means no snooker") {
    // The brown hides the red from the place the cue ball waits at, not from the rest of the D.
    const Vec2 waiting = table::default_cue_ball_position();
    const Vec2 red{table::baulk_line_x(), 0.6};
    const Vec2 elsewhere{-1.2, 0.2};
    REQUIRE(table::in_d(elsewhere));
    REQUIRE(blocks(waiting, red, table::spot(Ball::brown)));
    REQUIRE_FALSE(blocks(elsewhere, edge_contact(elsewhere, red, +1), table::spot(Ball::brown)));
    REQUIRE_FALSE(blocks(elsewhere, edge_contact(elsewhere, red, -1), table::spot(Ball::brown)));

    const FrameState in_play = frame({ball(Ball::cue, waiting.x, waiting.y), spotted(Ball::brown),
                                      ball(Ball::red, red.x, red.y)});
    CHECK(rig().oracle->snookered(in_play.balls, in_play.cue_ball, reds(in_play), false));
    CHECK(has_call(misses_everything(in_play).calls, CallKind::free_ball));

    const FrameState s = frame({ball(Ball::cue, -0.6, 0.6), spotted(Ball::brown), ball(Ball::red, red.x, red.y)});
    CHECK_FALSE(rig().oracle->snookered(in_play.balls, in_play.cue_ball, reds(in_play), true));
    const Judgement j = goes_in_off(s, 2);
    CHECK(fouls_text(j) == "in_off 4");
    CHECK(j.post.cue_in_hand);
    CHECK(same(at(j.post, j.post.cue_ball), waiting));
    CHECK_FALSE(has_call(j.calls, CallKind::free_ball));
    CHECK_FALSE(j.post.free_ball);
}

TEST_CASE("S2.17(b)(i): of two balls in the way, the one nearer the cue ball is the effective snookering ball") {
    // The free ball alone hides the red: the striker has snookered behind it.
    const FrameState alone = frame({ball(Ball::cue, -0.5, 0.2), ball(Ball::blue, -0.2, 0.2), ball(Ball::red, 0.4, 0.2)});
    CHECK(snookering(alone) == "1");
    const Judgement behind_free_ball = after_free_ball(alone);
    CHECK(fouls_text(behind_free_ball) == "free_ball_snooker 4");
    CHECK(behind_free_ball.penalty == 4);
    CHECK_FALSE(behind_free_ball.miss);
    CHECK(score(behind_free_ball.post) == "0-4");

    // The black is nearer: it is the snookering ball, and the free ball behind it is no foul.
    const FrameState black_nearer = frame({ball(Ball::cue, -0.5, 0.2), ball(Ball::blue, 0.0, 0.2),
                                           ball(Ball::red, 0.4, 0.2), ball(Ball::black, -0.3, 0.2)});
    CHECK(snookering(black_nearer) == "3");
    const Judgement legal = after_free_ball(black_nearer);
    CHECK(fouls_text(legal) == "");
    CHECK(legal.post.striker == 1);
    CHECK(legal.post.pending == Pending::none);
    CHECK_FALSE(legal.post.free_ball);

    const FrameState blue_nearer = frame({ball(Ball::cue, -0.5, 0.2), ball(Ball::blue, -0.3, 0.2),
                                          ball(Ball::red, 0.4, 0.2), ball(Ball::black, 0.0, 0.2)});
    CHECK(snookering(blue_nearer) == "1");
    CHECK(fouls_text(after_free_ball(blue_nearer)) == "free_ball_snooker 4");
}

TEST_CASE("S2.17(b)(ii): obstructing balls the same distance from the cue ball are all effective") {
    const FrameState s = frame({ball(Ball::cue, -0.5, 0.0), ball(Ball::blue, -0.2, 0.03), ball(Ball::red, 0.4, 0.0),
                                ball(Ball::black, -0.2, -0.03)});
    REQUIRE(blocks(at(s, 0), at(s, 2), at(s, 1)));
    REQUIRE(blocks(at(s, 0), at(s, 2), at(s, 3)));
    REQUIRE(std::hypot(at(s, 1).x - at(s, 0).x, at(s, 1).y - at(s, 0).y) ==
            std::hypot(at(s, 3).x - at(s, 0).x, at(s, 3).y - at(s, 0).y));

    CHECK(snookering(s) == "1,3");
    CHECK(fouls_text(after_free_ball(s)) == "free_ball_snooker 4");
}

TEST_CASE("S2.17(c): reds hidden by different balls leave no effective snookering ball") {
    // One red behind the free ball, the other behind the black: a snooker, but not by the free ball.
    const FrameState split = frame({ball(Ball::cue, -0.5, 0.1), ball(Ball::blue, -0.2, 0.25), ball(Ball::red, 0.1, 0.4),
                                    ball(Ball::black, -0.2, -0.05), ball(Ball::red, 0.1, -0.2, 1)});
    REQUIRE(blocks(at(split, 0), at(split, 2), at(split, 1)));
    REQUIRE(blocks(at(split, 0), at(split, 4), at(split, 3)));
    REQUIRE_FALSE(blocks(at(split, 0), edge_contact(at(split, 0), at(split, 2), -1), at(split, 3)));
    REQUIRE_FALSE(blocks(at(split, 0), edge_contact(at(split, 0), at(split, 4), +1), at(split, 1)));

    CHECK(rig().oracle->snookered(split.balls, split.cue_ball, reds(split), false));
    CHECK(snookering(split) == "");
    const Judgement j = after_free_ball(split);
    CHECK(fouls_text(j) == "");
    CHECK(j.post.striker == 1);

    // Both reds behind the free ball: it is the snookering ball.
    const FrameState together = frame({ball(Ball::cue, -0.5, 0.1), ball(Ball::blue, -0.2, 0.1), ball(Ball::red, 0.1, 0.12),
                                       spotted(Ball::black), ball(Ball::red, 0.4, 0.08, 1)});
    REQUIRE(blocks(at(together, 0), at(together, 2), at(together, 1)));
    REQUIRE(blocks(at(together, 0), at(together, 4), at(together, 1)));
    CHECK(snookering(together) == "1");
    CHECK(fouls_text(after_free_ball(together)) == "free_ball_snooker 4");
}

TEST_CASE("S2.17(e): a cushion in the way is no snooker") {
    // The red is in the jaws of the top left pocket and the cue ball near the same side cushion:
    // the cushion's nose keeps the cue ball off the red's centre and its near edge.
    const FrameState s = frame({ball(Ball::cue, 0.5, 0.85), ball(Ball::red, 1.74, 0.87), spotted(Ball::black)});
    const double rail = table::kPlayingWidth / 2.0 - table::kBallRadius;
    const double centre_path_y_at_jaw = 0.85 + (0.87 - 0.85) * (1.6 - 0.5) / (1.74 - 0.5);
    REQUIRE(centre_path_y_at_jaw > rail);
    REQUIRE_FALSE(blocks(at(s, 0), edge_contact(at(s, 0), at(s, 1), +1), at(s, 2)));
    REQUIRE_FALSE(blocks(at(s, 0), edge_contact(at(s, 0), at(s, 1), -1), at(s, 2)));

    CHECK_FALSE(rig().oracle->snookered(s.balls, s.cue_ball, reds(s), false));
    const Judgement j = misses_everything(s);
    CHECK(fouls_text(j) == "no_hit 4");
    CHECK_FALSE(has_call(j.calls, CallKind::free_ball));
    CHECK_FALSE(j.post.free_ball);
}

TEST_CASE("S2.18: a spot is occupied when a ball put on it would touch another") {
    const Vec2 spot = table::spot(Ball::black);
    // A red overlapping the place, and one a hair's breadth from a ball put there.
    for (double gap : {-0.0025, 2.0e-6}) {
        CAPTURE(gap);
        FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, kObject.x, kObject.y),
                              ball(Ball::red, spot.x + kDiameter + gap, spot.y)});
        s.on = {OnKind::any_colour, Ball::yellow};
        const Played p = play(s, strike(kToLeft, 2.0), nominate(Ball::black));
        REQUIRE(first_hit(p.record, 0) == 1);
        REQUIRE(dropped(p.record, 1));
        REQUIRE(drops(p.record) == 1);

        CHECK(p.judgement.points == 7);
        CHECK_FALSE(lies_at(p.judgement.post, Ball::black, spot));
        // With every other spot empty it goes on the pink's, the highest left (S3.7(e)).
        CHECK(lies_at(p.judgement.post, Ball::black, table::spot(Ball::pink)));
    }
}

TEST_CASE("S2.18: a ball clear of the spot, however near, leaves it free") {
    const Vec2 spot = table::spot(Ball::black);
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, kObject.x, kObject.y),
                          ball(Ball::red, spot.x + kDiameter + 0.001, spot.y)});
    s.on = {OnKind::any_colour, Ball::yellow};
    const Played p = play(s, strike(kToLeft, 2.0), nominate(Ball::black));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(drops(p.record) == 1);

    CHECK(lies_at(p.judgement.post, Ball::black, spot));
}

TEST_CASE("S2.19(b): the cue ball played straight into a ball almost touching it is a push stroke") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, kCue.x, kCue.y + kDiameter + 0.0005),
                                ball(Ball::red, 0.6, -0.4, 1), spotted(Ball::black)});
    const Played p = play(s, strike(kToLeft, 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(first_hit_time(p.record, 0) < rig().ref.push_contact_time_s);
    REQUIRE_FALSE(dropped(p.record, 0));

    CHECK(fouls_text(p.judgement) == "push_stroke 4");
    CHECK(p.judgement.points == 0);
    CHECK(score(p.judgement.post) == "0-4");
    CHECK(p.judgement.post.pending == Pending::after_foul);
}

TEST_CASE("S2.19(b): clipping a very fine edge of a ball almost touching is not a push stroke") {
    const double gap = 0.0002;
    const double off_aim = 82.0 * kPi / 180.0;
    const Vec2 cue{0.2, 0.2};
    const FrameState s = frame({ball(Ball::cue, cue.x, cue.y),
                                ball(Ball::red, cue.x + (kDiameter + gap) * std::cos(off_aim),
                                     cue.y + (kDiameter + gap) * std::sin(off_aim)),
                                spotted(Ball::black)});
    const Played p = play(s, strike(kToTop, 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    const double t = first_hit_time(p.record, 0);
    REQUIRE(t < rig().ref.push_contact_time_s);
    const phys::BallState c = p.record.ball_at(0, t);
    const phys::BallState o = p.record.ball_at(1, t);
    const double cut = std::atan2(o.r.y - c.r.y, o.r.x - c.r.x);
    REQUIRE(cut > rig().ref.push_fine_edge_rad);
    REQUIRE(drops(p.record) == 0);

    CHECK(fouls_text(p.judgement) == "");
    CHECK(p.judgement.post.striker == 1);
    CHECK(p.judgement.post.pending == Pending::none);
}
