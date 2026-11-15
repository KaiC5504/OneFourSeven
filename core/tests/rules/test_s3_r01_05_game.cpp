// WPBSA Section 3 Rules 1 to 3: the game's description and ball values, the position of the balls
// at the start, and the mode of play. A REQUIRE is a premise read from the simulator's own record
// (never from observe()), or a check that the frame carried on from it; a CHECK is the rule's
// outcome. Where only the arithmetic matters the facts are hand-made.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ofs/physics/rack.hpp"
#include "ofs/rules/match.hpp"
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

// An in-off: the cue ball cuts the object ball and runs on into the left middle pocket, the
// object ball staying up.
constexpr Vec2 kInOffCue{-0.0869, 0.5170};
constexpr Vec2 kInOffObject{0.10, 0.80};
constexpr Vec2 kInOffAim{0.0500, 0.7839};

const Ball kColours[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};

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

double apart(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

bool same(Vec2 a, Vec2 b) { return apart(a, b) < 1e-9; }

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

std::string frame_over_call(const std::vector<Call>& calls) {
    for (const Call& c : calls) {
        if (c.kind == CallKind::frame_over) return std::to_string(c.value) + " " + c.detail;
    }
    return "";
}

bool decide_now(FrameState& s, DecisionKind kind, int player) {
    std::vector<Call> calls;
    std::string error;
    return decide(s, {kind, player}, *rig().oracle, rig().ref, calls, error);
}

std::vector<BallOnTable> six_colours() {
    std::vector<BallOnTable> out;
    for (Ball c : kColours) out.push_back(spotted(c));
    return out;
}

// The six colours on their spots, except `moved`, which lies at `where`.
std::vector<BallOnTable> colours_with(Ball moved, Vec2 where) {
    std::vector<BallOnTable> out;
    for (Ball c : kColours) out.push_back(c == moved ? ball(c, where.x, where.y) : spotted(c));
    return out;
}

std::vector<BallOnTable> with(std::vector<BallOnTable> first, const std::vector<BallOnTable>& more) {
    first.insert(first.end(), more.begin(), more.end());
    return first;
}

// A stroke nobody simulated: the cue ball hit the ball at `place` first and that ball alone
// went into a pocket.
ShotFacts pot_facts(const Position& p, int place) {
    ShotFacts f = quiet_facts(p);
    f.first_hit = {place};
    f.first_hit_t = 0.2;
    f.closest_gap_m[std::size_t(place)] = 0.0;
    f.pots = {{place, 2, 0.5}};
    f.cue_travel_m = 0.4;
    f.object_travel_m = 0.4;
    f.pot_attempt = true;
    return f;
}

Judgement pots(const FrameState& s, Ball b, int index = 0) {
    const int place = place_of(s, b, index);
    REQUIRE(place >= 0);
    const ShotInput input = s.on.kind == OnKind::any_colour ? nominate(b) : ShotInput{};
    return judge(s, input, pot_facts(s, place), *rig().oracle, rig().ref);
}

FrameState opening(std::uint64_t seed, int breaker = 0) {
    return new_frame(breaker, phys::opening_rack(seed, rig().params), seed);
}

// The frame in play is conceded by `player`; what the match makes of it.
MatchEvent concede(MatchState& m, int player) {
    std::vector<Call> calls;
    std::string error;
    MatchEvent event = MatchEvent::none;
    CHECK(match_decide(m, {DecisionKind::concede_frame, player}, *rig().oracle, rig().ref, rig().params, calls, event,
                       error));
    return event;
}

MatchConfig config(int best_of, int first_breaker, std::uint64_t seed) {
    MatchConfig c;
    c.best_of = best_of;
    c.first_breaker = first_breaker;
    c.seed = seed;
    c.concede_any_time = {true, true};
    return c;
}

}  // namespace

TEST_CASE("S3.1(a): a red is worth 1 and the colours 2 to 7") {
    {
        const FrameState s = frame(with({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, kObject.x, kObject.y),
                                         ball(Ball::red, 0.6, -0.4, 1)},
                                        six_colours()));
        const Played p = play(s, strike(kToLeft, 2.0));
        REQUIRE(first_hit(p.record, 0) == 1);
        REQUIRE(dropped(p.record, 1));
        REQUIRE(drops(p.record) == 1);
        CHECK(p.judgement.points == 1);
        CHECK(score(p.judgement.post) == "1-0");
    }
    int value = 2;
    for (Ball c : kColours) {
        CAPTURE(std::string(table::ball_name(c)));
        FrameState s = frame(with({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.6, -0.4)}, colours_with(c, kObject)));
        s.on = {OnKind::any_colour, Ball::yellow};
        const int place = place_of(s, c);
        const Played p = play(s, strike(kToLeft, 2.0), nominate(c));
        REQUIRE(first_hit(p.record, 0) == place);
        REQUIRE(dropped(p.record, place));
        REQUIRE(drops(p.record) == 1);
        CHECK(fouls_text(p.judgement) == "");
        CHECK(p.judgement.points == value);
        CHECK(score(p.judgement.post) == std::to_string(value) + "-0");
        ++value;
    }
}

TEST_CASE("S3.1(b): a turn scores by reds and colours alternately, then the colours in ascending order") {
    struct Step {
        Ball ball;
        int index;
        int points;
        const char* on_after;
    };
    const Step steps[] = {
        {Ball::red, 0, 1, "any_colour"},      {Ball::black, 0, 7, "red"},           {Ball::red, 1, 1, "any_colour"},
        {Ball::pink, 0, 6, "colour yellow"},  {Ball::yellow, 0, 2, "colour green"}, {Ball::green, 0, 3, "colour brown"},
        {Ball::brown, 0, 4, "colour blue"},   {Ball::blue, 0, 5, "colour pink"},    {Ball::pink, 0, 6, "colour black"},
    };
    FrameState s = frame(with({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.3, 0.3), ball(Ball::red, -0.3, 0.3, 1)},
                              six_colours()));
    int total = 0;
    int stroke = 0;
    for (const Step& step : steps) {
        CAPTURE(++stroke);
        const Judgement j = pots(s, step.ball, step.index);
        total += step.points;
        CHECK(fouls_text(j) == "");
        CHECK(j.points == step.points);
        CHECK(on_text(j.post) == step.on_after);
        CHECK(j.post.striker == 0);
        CHECK(j.post.scores[0] == total);
        CHECK(j.post.break_points == total);
        CHECK_FALSE(j.post.over);
        s = j.post;
    }
    CHECK(count(s, Ball::red) == 0);
    CHECK(s.balls.size() == 2);

    const Judgement last = pots(s, Ball::black);
    CHECK(last.points == 7);
    CHECK(score(last.post) == "42-0");
    CHECK(last.post.over);
    CHECK(last.post.winner == 0);
}

TEST_CASE("S3.1(c): the points of a scoring stroke go on the striker's score") {
    FrameState red = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, kObject.x, kObject.y),
                            ball(Ball::red, 0.6, -0.4, 1), spotted(Ball::black)});
    red.striker = 1;
    red.scores = {20, 31};
    const Played one = play(red, strike(kToLeft, 2.0));
    REQUIRE(dropped(one.record, 1));
    REQUIRE(drops(one.record) == 1);
    CHECK(score(one.judgement.post) == "20-32");

    FrameState black = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, kObject.x, kObject.y),
                              ball(Ball::red, 0.6, -0.4)});
    black.on = {OnKind::any_colour, Ball::yellow};
    black.striker = 1;
    black.scores = {20, 32};
    black.break_points = 1;
    const Played seven = play(black, strike(kToLeft, 2.0), nominate(Ball::black));
    REQUIRE(dropped(seven.record, 1));
    REQUIRE(drops(seven.record) == 1);
    CHECK(score(seven.judgement.post) == "20-39");
    CHECK(seven.judgement.post.break_points == 8);
}

TEST_CASE("S3.1(d): penalty points go on the opponent's score, and the offender keeps the break made") {
    FrameState s = frame({ball(Ball::cue, kInOffCue.x, kInOffCue.y), ball(Ball::red, kInOffObject.x, kInOffObject.y),
                          ball(Ball::red, 0.6, -0.4, 1), spotted(Ball::black)});
    s.scores = {8, 4};
    s.break_points = 8;
    const Played p = play(s, strike(aim_at(s, kInOffAim), 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 0));
    REQUIRE(drops(p.record) == 1);

    CHECK(fouls_text(p.judgement) == "in_off 4");
    CHECK(p.judgement.penalty == 4);
    CHECK(score(p.judgement.post) == "8-8");
    CHECK(p.judgement.post.break_points == 0);
    CHECK(p.judgement.post.stats[1].points_from_fouls == 4);
}

TEST_CASE("S3.1(e): the points left on the table, and who needs penalty points to catch up") {
    FrameState s = opening(11);
    REQUIRE(s.balls.size() == 22);
    CHECK(points_remaining(s) == 147);

    // The first red gone and a colour to come.
    s.balls.erase(s.balls.begin() + 1);
    s.on = {OnKind::any_colour, Ball::yellow};
    CHECK(points_remaining(s) == 146);

    FrameState one_red = frame(with({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.3, 0.3)}, six_colours()));
    CHECK(points_remaining(one_red) == 35);
    one_red.scores = {0, 35};
    CHECK(penalty_points_required(one_red, 0) == 0);
    CHECK(penalty_points_required(one_red, 1) == 0);
    one_red.scores = {0, 36};
    CHECK(penalty_points_required(one_red, 0) == 1);
    CHECK(penalty_points_required(one_red, 1) == 0);
    one_red.scores = {50, 10};
    CHECK(penalty_points_required(one_red, 0) == 0);
    CHECK(penalty_points_required(one_red, 1) == 5);

    FrameState last_colour = frame(with({ball(Ball::cue, kCue.x, kCue.y)}, six_colours()));
    last_colour.on = {OnKind::any_colour, Ball::yellow};
    CHECK(points_remaining(last_colour) == 34);

    const int left[] = {27, 25, 22, 18, 13, 7};
    for (int from = 0; from < 6; ++from) {
        CAPTURE(from);
        std::vector<BallOnTable> balls{ball(Ball::cue, kCue.x, kCue.y)};
        for (int c = from; c < 6; ++c) balls.push_back(spotted(kColours[c]));
        CHECK(points_remaining(frame(balls)) == left[from]);
    }

    FrameState respot = frame({ball(Ball::cue, kCue.x, kCue.y), spotted(Ball::black)});
    respot.respotted_black = true;
    respot.scores = {40, 40};
    CHECK(points_remaining(respot) == 7);
}

TEST_CASE("S3.1(e): a snooker laid by a legal stroke is no foul and brings no free ball") {
    // The red is played away and the cue ball tucks in behind the black.
    const FrameState s = frame({ball(Ball::cue, 0.0, -0.3), ball(Ball::black, -0.2, 0.2), ball(Ball::red, 0.3, -0.3)});
    ShotFacts f = quiet_facts(s);
    f.first_hit = {2};
    f.first_hit_t = 0.2;
    f.closest_gap_m[2] = 0.0;
    f.rest[0] = {-0.5, 0.2};
    f.rest[2] = {0.4, 0.2};
    f.cue_travel_m = 1.2;
    f.object_travel_m = 0.6;
    const Judgement j = judge(s, {}, f, *rig().oracle, rig().ref);

    CHECK(rig().oracle->snookered(j.post.balls, j.post.cue_ball, places_on(j.post), false));
    CHECK(fouls_text(j) == "");
    CHECK(j.post.striker == 1);
    CHECK(j.post.pending == Pending::none);
    CHECK_FALSE(has_call(j.calls, CallKind::free_ball));
    CHECK_FALSE(j.post.free_ball);
    CHECK(nominations(j.post).empty());
}

TEST_CASE("S3.1(g)(i), S2.2: the match goes to the player first to the frames required") {
    MatchState m = start_match(config(3, 0, 5), rig().params);
    CHECK(m.frame_number == 1);
    CHECK_FALSE(m.over);

    CHECK(concede(m, 1) == MatchEvent::frame_ended);
    CHECK(m.frames[0] == 1);
    CHECK(m.frames[1] == 0);
    CHECK_FALSE(m.over);
    REQUIRE(m.history.size() == 1);
    CHECK(m.history[0].number == 1);
    CHECK(m.history[0].winner == 0);
    CHECK(m.history[0].reason == EndReason::conceded);

    CHECK(next_frame(m, rig().params));
    CHECK(m.frame_number == 2);
    CHECK(concede(m, 0) == MatchEvent::frame_ended);
    CHECK(m.frames[0] == 1);
    CHECK(m.frames[1] == 1);
    CHECK_FALSE(m.over);

    CHECK(next_frame(m, rig().params));
    CHECK(m.frame_number == 3);
    CHECK(concede(m, 1) == MatchEvent::match_ended);
    CHECK(m.frames[0] == 2);
    CHECK(m.frames[1] == 1);
    CHECK(m.over);
    CHECK(m.winner == 0);
    CHECK(m.history.size() == 3);
    CHECK_FALSE(next_frame(m, rig().params));
}

TEST_CASE("S3.1(g)(i): a match is over as soon as it cannot be caught") {
    MatchState m = start_match(config(3, 1, 9), rig().params);
    CHECK(concede(m, 0) == MatchEvent::frame_ended);
    CHECK(next_frame(m, rig().params));
    CHECK(concede(m, 0) == MatchEvent::match_ended);
    CHECK(m.over);
    CHECK(m.winner == 1);
    CHECK(m.frames[1] == 2);
    CHECK(m.frames[0] == 0);
    CHECK_FALSE(next_frame(m, rig().params));
}

TEST_CASE("S3.2(a), S2.4: a frame starts with the cue ball in hand and the twenty-one object balls set") {
    const FrameState s = opening(21, 1);
    REQUIRE(s.balls.size() == 22);
    CHECK(s.cue_ball == 0);
    CHECK(count(s, Ball::cue) == 1);
    CHECK(count(s, Ball::red) == 15);
    for (Ball c : kColours) CHECK(count(s, c) == 1);
    CHECK(s.cue_in_hand);
    CHECK(same(cue_pos(s), table::default_cue_ball_position()));
    CHECK(table::in_d(cue_pos(s)));
    CHECK(on_text(s) == "red");
    CHECK_FALSE(s.free_ball);
    CHECK(s.striker == 1);
    CHECK(score(s) == "0-0");
    CHECK(s.break_points == 0);
    CHECK(s.strokes == 0);
    CHECK(s.pending == Pending::none);
    CHECK_FALSE(s.over);
    CHECK(s.winner == -1);
    CHECK_FALSE(s.respotted_black);
}

TEST_CASE("S3.2(a)(ii), S1.1(f): the six colours start on their spots") {
    // The spots from the rulebook's own dimensions: the baulk line 737 mm from the bottom cushion,
    // the D's radius 292 mm, the black 324 mm from the top cushion, on a 3569 mm playing area.
    const double half = 3.569 / 2.0;
    const double baulk = -half + 0.737;
    const FrameState s = opening(4);
    REQUIRE(s.balls.size() == 22);
    CHECK(lies_at(s, Ball::yellow, {baulk, -0.292}));  // the right of the D seen from baulk
    CHECK(lies_at(s, Ball::green, {baulk, 0.292}));
    CHECK(lies_at(s, Ball::brown, {baulk, 0.0}));
    CHECK(lies_at(s, Ball::blue, {0.0, 0.0}));
    CHECK(lies_at(s, Ball::pink, {half / 2.0, 0.0}));
    CHECK(lies_at(s, Ball::black, {half - 0.324, 0.0}));
}

TEST_CASE("S3.2(a)(i): the reds start as a tight triangle behind the pink, its base square to the top cushion") {
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        CAPTURE(seed);
        const FrameState s = opening(seed);
        REQUIRE(s.balls.size() == 22);
        const Vec2 pink = table::spot(Ball::pink);
        std::vector<Vec2> reds;
        for (const BallOnTable& b : s.balls) {
            if (b.ball == Ball::red) reds.push_back(b.pos);
        }
        REQUIRE(reds.size() == 15);
        std::sort(reds.begin(), reds.end(), [](Vec2 a, Vec2 b) { return a.x < b.x; });

        // The apex: on the centre line, towards the top cushion from the pink, a hair clear of it.
        const Vec2 apex = reds[0];
        CHECK(std::abs(apex.y) < 1.0e-4);
        CHECK(apex.x > pink.x);
        const double apex_gap = apart(apex, pink) - kDiameter;
        CHECK(apex_gap > 0.0);
        CHECK(apex_gap < 2.0e-4);

        // Rows of 1 to 5, each a row's depth behind the last and level across.
        const double depth = kDiameter * std::sqrt(3.0) / 2.0;
        std::size_t first = 0;
        for (std::size_t row = 0; row < 5; ++row) {
            CAPTURE(row);
            for (std::size_t i = first; i <= first + row; ++i) {
                CHECK(std::abs(reds[i].x - (apex.x + double(row) * depth)) < 5.0e-4);
            }
            first += row + 1;
        }
        // Tightly packed, nothing overlapping: each red has a neighbour within a hair.
        for (std::size_t i = 0; i < reds.size(); ++i) {
            double nearest = 1.0;
            for (std::size_t k = 0; k < reds.size(); ++k) {
                if (k == i) continue;
                nearest = std::min(nearest, apart(reds[i], reds[k]));
            }
            CHECK(nearest > kDiameter);
            CHECK(nearest < kDiameter + 2.0e-4);
        }
        // Equilateral: the three corners are the same distance apart.
        std::vector<Vec2> base(reds.end() - 5, reds.end());
        std::sort(base.begin(), base.end(), [](Vec2 a, Vec2 b) { return a.y < b.y; });
        const double side = apart(base.front(), base.back());
        CHECK(std::abs(apart(apex, base.front()) - side) < 5.0e-4);
        CHECK(std::abs(apart(apex, base.back()) - side) < 5.0e-4);
        CHECK(std::abs(side - 4.0 * kDiameter) < 1.0e-3);
        CHECK(base.back().x < table::top_cushion_x() - table::kBallRadius);
    }
}

TEST_CASE("S3.2(a)(i), S2.18: the apex red does not occupy the pink spot: a pink potted with the pack untouched goes back") {
    for (std::uint64_t seed = 0; seed < 16; ++seed) {
        CAPTURE(seed);
        FrameState s = opening(seed);
        REQUIRE(s.balls.size() == 22);
        s.cue_in_hand = false;
        s.balls[0].pos = {0.3, -0.5};
        s.on = {OnKind::any_colour, Ball::yellow};
        const int apex = place_of(s, Ball::red, 0);
        const int pink = place_of(s, Ball::pink);
        REQUIRE(apex >= 0);
        REQUIRE(pink >= 0);
        const double apex_gap = apart(s.balls[std::size_t(apex)].pos, s.balls[std::size_t(pink)].pos) - kDiameter;
        CAPTURE(apex_gap);
        CAPTURE(rig().ref.touching_gap_m);
        REQUIRE(apex_gap > 0.0);

        const Judgement j = pots(s, Ball::pink);
        CHECK(j.points == 6);
        CHECK(lies_at(j.post, Ball::pink, table::spot(Ball::pink)));
    }
}

TEST_CASE("S3.3: who breaks is settled by lot, or as the players agree") {
    bool either[2] = {false, false};
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        CAPTURE(seed);
        const MatchState m = start_match(config(3, -1, seed), rig().params);
        const MatchState again = start_match(config(3, -1, seed), rig().params);
        REQUIRE((m.breaker == 0 || m.breaker == 1));
        CHECK(again.breaker == m.breaker);
        CHECK(m.frame.striker == m.breaker);
        either[m.breaker] = true;
    }
    CHECK(either[0]);
    CHECK(either[1]);

    for (int agreed : {0, 1}) {
        CAPTURE(agreed);
        const MatchState m = start_match(config(3, agreed, 7), rig().params);
        CHECK(m.breaker == agreed);
        CHECK(m.frame.striker == agreed);
        CHECK(m.frame_number == 1);
        CHECK(m.frame.cue_in_hand);
    }
}

TEST_CASE("S3.3(a): the players keep their order of play through the frame") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.3, 0.3), spotted(Ball::black)});
    FrameState now = s;
    int expected = 0;
    for (int stroke = 1; stroke <= 4; ++stroke) {
        CAPTURE(stroke);
        const int red = place_of(now, Ball::red);
        REQUIRE(red >= 0);
        CHECK(now.striker == expected);
        const Played p = play(now, strike(aim_at_ball(now, red), 0.5));
        REQUIRE(first_hit(p.record, now.cue_ball) == red);
        REQUIRE(drops(p.record) == 0);
        CHECK(fouls_text(p.judgement) == "");
        REQUIRE(follows(p, now));
        expected = 1 - expected;
        CHECK(p.judgement.post.striker == expected);
        now = p.judgement.post;
    }
}

TEST_CASE("S3.3(b): the players break in turn, frame by frame") {
    MatchState m = start_match(config(5, 1, 3), rig().params);
    const int breakers[] = {1, 0, 1, 0};
    for (int f = 0; f < 4; ++f) {
        CAPTURE(f);
        CHECK(m.frame_number == f + 1);
        CHECK(m.breaker == breakers[f]);
        CHECK(m.frame.striker == breakers[f]);
        CHECK(m.frame.cue_in_hand);
        CHECK(m.frame.balls.size() == 22);
        // The frames are shared, so the match runs on.
        CHECK(concede(m, f % 2) == MatchEvent::frame_ended);
        REQUIRE(m.history.size() == std::size_t(f + 1));
        CHECK(m.history[std::size_t(f)].breaker == breakers[f]);
        CHECK(next_frame(m, rig().params));
    }
    CHECK(m.frame_number == 5);
    CHECK(m.breaker == 1);
}

TEST_CASE("S3.3(c): the breaker plays from in hand, and the first stroke starts the frame") {
    FrameState s = opening(7);
    REQUIRE(s.balls.size() == 22);
    CHECK(s.cue_in_hand);
    CHECK(s.strokes == 0);

    const Vec2 from{table::baulk_line_x(), -0.2};
    std::string error;
    CHECK(place_cue_ball(s, from, *rig().oracle, error));
    s.balls[0].pos = from;
    const Played p = play(s, strike(aim_at_ball(s, 15), 2.0));
    const int hit = first_hit(p.record, 0);
    REQUIRE(hit >= 1);
    REQUIRE(s.balls[std::size_t(hit)].ball == Ball::red);
    REQUIRE(drops(p.record) == 0);

    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "");
    CHECK(post.strokes == 1);
    CHECK_FALSE(post.cue_in_hand);
    CHECK(same(cue_pos(post), rest_of(p.record, 0)));
    CHECK(post.balls.size() == 22);
    CHECK(post.striker == 1);
    CHECK(on_text(post) == "red");
    CHECK(score(post) == "0-0");
}

TEST_CASE("S3.3(g): each turn starts on a red while a red is left") {
    // The striker has potted a red and now fails at the black.
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, 0.3, 0.3), ball(Ball::red, 0.6, -0.4)});
    s.on = {OnKind::any_colour, Ball::yellow};
    s.scores = {1, 0};
    s.break_points = 1;
    const Played p = play(s, strike(kToTop, 0.6), nominate(Ball::black));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(drops(p.record) == 0);

    CHECK(fouls_text(p.judgement) == "");
    CHECK(p.judgement.post.striker == 1);
    CHECK(on_text(p.judgement.post) == "red");
    CHECK(p.judgement.post.break_points == 0);
}

TEST_CASE("S3.3(g): every red potted in the one stroke scores") {
    // The first red is played on to the second; both go in.
    const FrameState s = frame({ball(Ball::cue, 0.0, 0.1), ball(Ball::red, 0.0, 0.35), ball(Ball::red, 0.0, 0.8, 1),
                                ball(Ball::red, 0.6, -0.4, 2), spotted(Ball::black)});
    const Played p = play(s, strike(kToLeft, 1.6));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(dropped(p.record, 2));
    REQUIRE(drops(p.record) == 2);

    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "");
    CHECK(p.judgement.points == 2);
    CHECK(score(post) == "2-0");
    CHECK(count(post, Ball::red) == 1);
    CHECK(post.striker == 0);
    CHECK(on_text(post) == "any_colour");
}

TEST_CASE("S3.3(h)(i): after a red the same player plays at a colour of choice, which scores and is spotted") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, kObject.x, kObject.y),
                                ball(Ball::black, 0.0, 0.15), ball(Ball::red, -0.6, -0.2, 1)});
    const Played red = play(s, strike(kToLeft, 2.0));
    REQUIRE(first_hit(red.record, 0) == 1);
    REQUIRE(dropped(red.record, 1));
    REQUIRE(drops(red.record) == 1);
    CHECK(red.judgement.post.striker == 0);
    CHECK(on_text(red.judgement.post) == "any_colour");

    REQUIRE(follows(red, s));
    const FrameState& s2 = red.judgement.post;
    const int black = place_of(s2, Ball::black);
    REQUIRE(black >= 0);
    const Played colour = play(s2, strike(aim_at_ball(s2, black), 2.5), nominate(Ball::black));
    REQUIRE(first_hit(colour.record, s2.cue_ball) == black);
    REQUIRE(dropped(colour.record, black));
    REQUIRE(drops(colour.record) == 1);

    const FrameState& post = colour.judgement.post;
    CHECK(fouls_text(colour.judgement) == "");
    CHECK(colour.judgement.points == 7);
    CHECK(score(post) == "8-0");
    CHECK(lies_at(post, Ball::black, table::spot(Ball::black)));
    CHECK(colour.judgement.spotted.size() == 1);
    CHECK(post.striker == 0);
    CHECK(on_text(post) == "red");
}

TEST_CASE("S3.3(h)(ii): the colour after the last red is still one of choice and is spotted; then the colours go in order") {
    const FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, kObject.x, kObject.y),
                                ball(Ball::black, 0.0, 0.15), spotted(Ball::yellow), spotted(Ball::pink)});
    const Played red = play(s, strike(kToLeft, 2.0));
    REQUIRE(first_hit(red.record, 0) == 1);
    REQUIRE(dropped(red.record, 1));
    REQUIRE(drops(red.record) == 1);
    CHECK(count(red.judgement.post, Ball::red) == 0);
    CHECK(on_text(red.judgement.post) == "any_colour");

    REQUIRE(follows(red, s));
    const FrameState& s2 = red.judgement.post;
    const int black = place_of(s2, Ball::black);
    REQUIRE(black >= 0);
    const Played colour = play(s2, strike(aim_at_ball(s2, black), 2.5), nominate(Ball::black));
    REQUIRE(first_hit(colour.record, s2.cue_ball) == black);
    REQUIRE(dropped(colour.record, black));
    REQUIRE(drops(colour.record) == 1);

    const FrameState& post = colour.judgement.post;
    CHECK(colour.judgement.points == 7);
    CHECK(score(post) == "8-0");
    CHECK(lies_at(post, Ball::black, table::spot(Ball::black)));
    CHECK(post.striker == 0);
    CHECK(on_text(post) == "colour yellow");
}

TEST_CASE("S3.3(h)(ii): a colour missed after the last red leaves the next player on the yellow") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, 0.3, 0.3), spotted(Ball::yellow),
                          spotted(Ball::pink)});
    s.on = {OnKind::any_colour, Ball::yellow};
    const Played p = play(s, strike(kToTop, 0.6), nominate(Ball::black));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(drops(p.record) == 0);

    CHECK(fouls_text(p.judgement) == "");
    CHECK(p.judgement.post.striker == 1);
    CHECK(on_text(p.judgement.post) == "colour yellow");
}

TEST_CASE("S3.3(h)(iii): with the reds gone the colours are on in ascending order and stay down when potted") {
    const char* next[] = {"colour green", "colour brown", "colour blue", "colour pink", "colour black"};
    for (int c = 0; c < 5; ++c) {
        const Ball colour = kColours[c];
        CAPTURE(std::string(table::ball_name(colour)));
        std::vector<BallOnTable> balls{ball(Ball::cue, kCue.x, kCue.y), ball(colour, kObject.x, kObject.y)};
        for (int higher = c + 1; higher < 6; ++higher) balls.push_back(spotted(kColours[higher]));
        const FrameState s = frame(balls);
        REQUIRE(s.on.kind == OnKind::colour);
        REQUIRE(s.on.colour == colour);
        const Played p = play(s, strike(kToLeft, 2.0));
        REQUIRE(first_hit(p.record, 0) == 1);
        REQUIRE(dropped(p.record, 1));
        REQUIRE(drops(p.record) == 1);

        const FrameState& post = p.judgement.post;
        CHECK(fouls_text(p.judgement) == "");
        CHECK(p.judgement.points == c + 2);
        CHECK(count(post, colour) == 0);
        CHECK(p.judgement.spotted.empty());
        CHECK(post.balls.size() == balls.size() - 1);
        CHECK(post.striker == 0);
        CHECK(on_text(post) == next[c]);
        CHECK_FALSE(post.over);
    }
}

TEST_CASE("S3.3(i): a red pocketed in a foul stroke stays off the table") {
    // The cue ball follows the red in.
    const FrameState s = frame({ball(Ball::cue, 0.0, 0.5), ball(Ball::red, 0.0, 0.7), ball(Ball::red, 0.6, -0.4, 1),
                                spotted(Ball::black)});
    phys::CueStrike follow = strike(kToLeft, 2.0);
    follow.tip_y = 0.3;
    const Played p = play(s, follow);
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(dropped(p.record, 0));
    REQUIRE(drops(p.record) == 2);

    CHECK(fouls_text(p.judgement) == "in_off 4");
    CHECK(p.judgement.points == 0);
    CHECK(count(p.judgement.post, Ball::red) == 1);
    CHECK(p.judgement.spotted.empty());

    FrameState plays_on = p.judgement.post;
    CHECK(decide_now(plays_on, DecisionKind::play, 1));
    CHECK(count(plays_on, Ball::red) == 1);
    FrameState put_back = p.judgement.post;
    CHECK(decide_now(put_back, DecisionKind::play_again, 1));
    CHECK(count(put_back, Ball::red) == 1);
    CHECK(points_remaining(put_back) == 35);
}

TEST_CASE("S3.3(j): the next stroke is from where the cue ball stopped, or from in hand if it went in a pocket") {
    const FrameState open = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::red, 0.3, 0.3), spotted(Ball::black)});
    const Played safe = play(open, strike(kToTop, 0.6));
    REQUIRE(first_hit(safe.record, 0) == 1);
    REQUIRE(drops(safe.record) == 0);
    CHECK(safe.judgement.post.striker == 1);
    CHECK_FALSE(safe.judgement.post.cue_in_hand);
    CHECK(same(cue_pos(safe.judgement.post), rest_of(safe.record, 0)));
    CHECK(lies_at(safe.judgement.post, Ball::red, rest_of(safe.record, 1)));

    const FrameState s = frame({ball(Ball::cue, kInOffCue.x, kInOffCue.y), ball(Ball::red, kInOffObject.x, kInOffObject.y),
                                ball(Ball::red, 0.6, -0.4, 1), spotted(Ball::black)});
    const Played in_off = play(s, strike(aim_at(s, kInOffAim), 2.0));
    REQUIRE(first_hit(in_off.record, 0) == 1);
    REQUIRE(dropped(in_off.record, 0));
    REQUIRE(drops(in_off.record) == 1);
    CHECK(in_off.judgement.post.cue_in_hand);
    CHECK(in_off.judgement.post.striker == 1);
    CHECK(lies_at(in_off.judgement.post, Ball::red, rest_of(in_off.record, 1)));
}

TEST_CASE("S3.1(f)(i): the frame goes to the higher score, even an offender's") {
    // On the black alone the leader goes in off: seven away, and still ahead.
    FrameState s = frame({ball(Ball::cue, kInOffCue.x, kInOffCue.y), ball(Ball::black, kInOffObject.x, kInOffObject.y)});
    s.scores = {40, 30};
    const Played p = play(s, strike(aim_at(s, kInOffAim), 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 0));
    REQUIRE(drops(p.record) == 1);

    CHECK(fouls_text(p.judgement) == "in_off 7");
    CHECK(score(p.judgement.post) == "40-37");
    CHECK(p.judgement.post.over);
    CHECK(p.judgement.post.winner == 0);
    CHECK(frame_over_call(p.judgement.calls) == "0 black");
}
