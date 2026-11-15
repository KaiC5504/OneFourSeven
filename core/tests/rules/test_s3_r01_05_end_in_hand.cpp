// WPBSA Section 3 Rules 4 and 5: the end of a frame (the black alone, the re-spotted black) and
// playing from in hand. A REQUIRE is a premise read from the simulator's own record (never from
// observe()); a CHECK is the rule's outcome. Where only a choice or the lots matter the facts are
// hand-made.

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
constexpr double kDiameter = table::kBallDiameter;

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

double apart(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

bool same(Vec2 a, Vec2 b) { return apart(a, b) < 1e-9; }

bool lies_at(const Position& p, Ball b, Vec2 where, int index = 0) {
    const int place = place_of(p, b, index);
    return place >= 0 && same(p.balls[std::size_t(place)].pos, where);
}

Vec2 cue_pos(const Position& p) { return p.balls[std::size_t(p.cue_ball)].pos; }

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

// The black alone, a straight pot from the left middle pocket.
FrameState black_to_pot(int first, int second) {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, kObject.x, kObject.y)});
    s.scores = {first, second};
    return s;
}

Played pot_black(const FrameState& s) {
    Played p = play(s, strike(kToLeft, 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 1));
    REQUIRE(drops(p.record) == 1);
    return p;
}

// The black alone and in the open; the cue ball rolled away from it touches nothing.
FrameState black_to_miss(int first, int second) {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, 0.6, 0.3)});
    s.scores = {first, second};
    return s;
}

Played miss_black(const FrameState& s) {
    Played p = play(s, strike(kToBaulk, 0.15));
    REQUIRE(first_hit(p.record, 0) == -1);
    REQUIRE(drops(p.record) == 0);
    return p;
}

// The black potted on level terms, not simulated: the frame waits on the lots.
Judgement tie_on_the_black(std::uint64_t seed) {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, kObject.x, kObject.y)});
    s.scores = {28, 35};
    s.seed = seed;
    s.strokes = 60;
    ShotFacts f = quiet_facts(s);
    f.first_hit = {1};
    f.first_hit_t = 0.2;
    f.closest_gap_m[1] = 0.0;
    f.pots = {{1, 2, 0.5}};
    f.cue_travel_m = 0.4;
    f.object_travel_m = 0.4;
    f.pot_attempt = true;
    return judge(s, {}, f, *rig().oracle, rig().ref);
}

void check_respotted(const Judgement& j) {
    const FrameState& post = j.post;
    CHECK_FALSE(post.over);
    CHECK(post.winner == -1);
    CHECK(post.respotted_black);
    CHECK(post.balls.size() == 2);
    CHECK(lies_at(post, Ball::black, table::spot(Ball::black)));
    CHECK(post.cue_in_hand);
    CHECK(table::in_d(cue_pos(post)));
    CHECK(on_text(post) == "colour black");
    CHECK(post.pending == Pending::toss);
    CHECK((post.decider == 0 || post.decider == 1));
    bool called = false;
    for (const Call& c : j.calls) called = called || (c.kind == CallKind::respotted_black && c.value == post.decider);
    CHECK(called);
    CHECK_FALSE(has_call(j.calls, CallKind::frame_over));
    CHECK(offered(post, post.decider) == "play_first,play_second");
    CHECK(offered(post, 1 - post.decider) == "");
}

// The cue ball in hand, nothing in baulk: a red up the table and the black on its spot.
FrameState in_hand() {
    FrameState s = frame({ball(Ball::cue, table::default_cue_ball_position().x, table::default_cue_ball_position().y),
                          ball(Ball::red, 0.5, 0.3), spotted(Ball::black)});
    s.cue_in_hand = true;
    return s;
}

}  // namespace

TEST_CASE("S3.4(a), S2.1(d), S2.5(e): on the black alone, potting it ends the frame for the higher score") {
    struct Case {
        int first;
        int second;
        const char* final_score;
    };
    for (const Case& c : {Case{40, 35, "47-35"}, Case{30, 35, "37-35"}}) {
        CAPTURE(c.final_score);
        const Played p = pot_black(black_to_pot(c.first, c.second));
        const FrameState& post = p.judgement.post;
        CHECK(fouls_text(p.judgement) == "");
        CHECK(p.judgement.points == 7);
        CHECK(score(post) == c.final_score);
        CHECK(post.over);
        CHECK(post.winner == 0);
        CHECK(post.end_reason == EndReason::black);
        CHECK(frame_over_call(p.judgement.calls) == "0 black");
        CHECK(offered(post, 0) == "");
        CHECK(offered(post, 1) == "");
    }
}

TEST_CASE("S3.4(a), S2.1(d): on the black alone, a foul ends the frame for the higher score") {
    const Played p = miss_black(black_to_miss(40, 35));
    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "no_hit 7");
    CHECK(p.judgement.penalty == 7);
    CHECK(score(post) == "40-42");
    CHECK(post.over);
    CHECK(post.winner == 1);
    CHECK(post.end_reason == EndReason::black);
    CHECK(frame_over_call(p.judgement.calls) == "1 black");
}

TEST_CASE("S3.4(a): a stroke at the last black that neither pots nor fouls leaves the frame open") {
    FrameState s = frame({ball(Ball::cue, kCue.x, kCue.y), ball(Ball::black, 0.3, 0.3)});
    s.scores = {30, 35};
    const Played p = play(s, strike(kToTop, 0.6));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(drops(p.record) == 0);

    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "");
    CHECK_FALSE(post.over);
    CHECK(post.striker == 1);
    CHECK(post.pending == Pending::none);
    CHECK(on_text(post) == "colour black");
    CHECK_FALSE(post.respotted_black);
    CHECK(score(post) == "30-35");
}

TEST_CASE("S3.4(a), (b)(i)-(iii), S2.9(a)(iv): the black potted to level the scores is re-spotted, the cue ball in hand") {
    const Played p = pot_black(black_to_pot(28, 35));
    CHECK(p.judgement.points == 7);
    CHECK(score(p.judgement.post) == "35-35");
    check_respotted(p.judgement);
}

TEST_CASE("S3.4(a), (b): a foul on the last black that levels the scores brings the re-spotted black too") {
    const Played p = miss_black(black_to_miss(37, 30));
    CHECK(fouls_text(p.judgement) == "no_hit 7");
    CHECK(score(p.judgement.post) == "37-37");
    check_respotted(p.judgement);
}

TEST_CASE("S3.4(b)(ii): the lots can fall to either player, and one frame always draws the same") {
    bool either[2] = {false, false};
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        CAPTURE(seed);
        const Judgement j = tie_on_the_black(seed);
        const Judgement again = tie_on_the_black(seed);
        REQUIRE(j.post.pending == Pending::toss);
        REQUIRE((j.post.decider == 0 || j.post.decider == 1));
        CHECK(again.post.decider == j.post.decider);
        either[j.post.decider] = true;
    }
    CHECK(either[0]);
    CHECK(either[1]);
}

TEST_CASE("S3.4(b)(ii), (iii): the winner of the lots chooses who plays the re-spotted black, from in hand") {
    const Judgement j = tie_on_the_black(12);
    REQUIRE(j.post.pending == Pending::toss);
    const int winner = j.post.decider;
    REQUIRE((winner == 0 || winner == 1));

    FrameState loser_chooses = j.post;
    CHECK_FALSE(decide_now(loser_chooses, DecisionKind::play_first, 1 - winner));
    CHECK(loser_chooses.pending == Pending::toss);

    FrameState first = j.post;
    CHECK(decide_now(first, DecisionKind::play_first, winner));
    CHECK(first.pending == Pending::none);
    CHECK(first.striker == winner);
    CHECK(first.cue_in_hand);
    CHECK(first.respotted_black);
    CHECK(on_text(first) == "colour black");
    CHECK(score(first) == "35-35");
    CHECK_FALSE(first.over);
    std::string error;
    CHECK(place_cue_ball(first, {-1.2, 0.1}, *rig().oracle, error));

    FrameState second = j.post;
    CHECK(decide_now(second, DecisionKind::play_second, winner));
    CHECK(second.pending == Pending::none);
    CHECK(second.striker == 1 - winner);
    CHECK(second.cue_in_hand);
}

TEST_CASE("S3.4(b)(iv): on a re-spotted black the first pot wins the frame") {
    FrameState s = black_to_pot(35, 35);
    s.respotted_black = true;
    s.striker = 1;
    const Played p = pot_black(s);
    const FrameState& post = p.judgement.post;
    CHECK(score(post) == "35-42");
    CHECK(post.over);
    CHECK(post.winner == 1);
    CHECK(post.end_reason == EndReason::respotted_black);
    CHECK(frame_over_call(p.judgement.calls) == "1 respotted_black");
}

TEST_CASE("S3.4(b)(iv): on a re-spotted black the first foul loses the frame") {
    FrameState s = frame({ball(Ball::cue, -1.2, 0.1), spotted(Ball::black)});
    s.scores = {35, 35};
    s.respotted_black = true;
    s.cue_in_hand = true;
    REQUIRE(table::in_d(cue_pos(s)));
    const Played p = miss_black(s);
    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "no_hit 7");
    CHECK(score(post) == "35-42");
    CHECK(post.over);
    CHECK(post.winner == 1);
    CHECK(post.end_reason == EndReason::respotted_black);
    CHECK(frame_over_call(p.judgement.calls) == "1 respotted_black");
}

TEST_CASE("S3.4(b): a stroke at the re-spotted black that neither pots nor fouls passes the turn") {
    FrameState s = frame({ball(Ball::cue, -1.2, 0.0), spotted(Ball::black)});
    s.scores = {35, 35};
    s.respotted_black = true;
    s.cue_in_hand = true;
    const Played p = play(s, strike(kToTop, 1.5));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(drops(p.record) == 0);

    const FrameState& post = p.judgement.post;
    CHECK(fouls_text(p.judgement) == "");
    CHECK_FALSE(post.over);
    CHECK(post.respotted_black);
    CHECK(post.striker == 1);
    CHECK(post.pending == Pending::none);
    CHECK_FALSE(post.cue_in_hand);
    CHECK(on_text(post) == "colour black");
    CHECK(score(post) == "35-35");
}

TEST_CASE("S3.5: in hand, the cue ball may be placed anywhere on or within the lines of the D") {
    const double baulk = table::baulk_line_x();
    const double r = table::kDRadius;
    const Vec2 places[] = {
        {baulk, 0.0},                 // the middle of the baulk line
        {baulk, r},                   // the D's corners
        {baulk, -r},
        {baulk - r, 0.0},             // the back of the arc
        {baulk - 0.6 * r, 0.8 * r},   // on the arc
        {baulk - 0.15, 0.1},          // inside
        {baulk, 0.146},               // on the line between brown and green
    };
    for (Vec2 where : places) {
        CAPTURE(where.x);
        CAPTURE(where.y);
        REQUIRE(table::in_d(where));
        FrameState s = in_hand();
        std::string error;
        CHECK(rig().oracle->cue_placement_legal(s.balls, s.cue_ball, where));
        CHECK(place_cue_ball(s, where, *rig().oracle, error));
        CHECK(error.empty());
        CHECK(same(cue_pos(s), where));
        CHECK(s.cue_in_hand);
    }
}

TEST_CASE("S3.5, (a): a place outside the D is refused") {
    const double baulk = table::baulk_line_x();
    const double r = table::kDRadius;
    const Vec2 places[] = {
        {baulk + 0.001, 0.0},   // in front of the baulk line
        {baulk, r + 0.001},     // on the baulk line, beyond the D
        {baulk - r - 0.001, 0.0},
        {baulk - 0.25, 0.2},    // in baulk, outside the arc
        {0.0, 0.0},
    };
    for (Vec2 where : places) {
        CAPTURE(where.x);
        CAPTURE(where.y);
        REQUIRE_FALSE(table::in_d(where));
        FrameState s = in_hand();
        const Vec2 before = cue_pos(s);
        std::string error;
        CHECK_FALSE(rig().oracle->cue_placement_legal(s.balls, s.cue_ball, where));
        CHECK_FALSE(place_cue_ball(s, where, *rig().oracle, error));
        CHECK_FALSE(error.empty());
        CHECK(same(cue_pos(s), before));
        CHECK(s.cue_in_hand);
    }
}

TEST_CASE("S3.5: the cue ball cannot be placed on or against another ball") {
    const Vec2 yellow = table::spot(Ball::yellow);
    FrameState s = frame({ball(Ball::cue, -1.2, 0.1), spotted(Ball::yellow), spotted(Ball::green), spotted(Ball::brown),
                          ball(Ball::red, 0.5, 0.3)});
    s.cue_in_hand = true;
    std::string error;

    CHECK_FALSE(place_cue_ball(s, table::spot(Ball::brown), *rig().oracle, error));
    CHECK_FALSE(place_cue_ball(s, {yellow.x, yellow.y + kDiameter - 0.001}, *rig().oracle, error));
    // Touching: closer than the referee can tell from contact.
    CHECK_FALSE(place_cue_ball(s, {yellow.x, yellow.y + kDiameter + 2.0e-6}, *rig().oracle, error));
    CHECK(same(cue_pos(s), {-1.2, 0.1}));

    const Vec2 clear{yellow.x, yellow.y + kDiameter + 0.001};
    REQUIRE(table::in_d(clear));
    error.clear();
    CHECK(place_cue_ball(s, clear, *rig().oracle, error));
    CHECK(same(cue_pos(s), clear));
}

TEST_CASE("S3.5: from in hand the cue ball may be played in any direction") {
    // One red behind the cue ball, towards the baulk cushion, and one to the side.
    FrameState s = frame({ball(Ball::cue, -1.2, 0.0), ball(Ball::red, -1.55, 0.0), ball(Ball::red, -1.2, 0.5, 1),
                          spotted(Ball::black)});
    s.cue_in_hand = true;
    REQUIRE(table::in_d(cue_pos(s)));

    const Played back = play(s, strike(kToBaulk, 1.0));
    REQUIRE(first_hit(back.record, 0) == 1);
    REQUIRE(drops(back.record) == 0);
    CHECK(fouls_text(back.judgement) == "");
    CHECK_FALSE(back.judgement.post.cue_in_hand);
    CHECK(back.judgement.post.striker == 1);

    const Played across = play(s, strike(kToLeft, 0.6));
    REQUIRE(first_hit(across.record, 0) == 2);
    REQUIRE(drops(across.record) == 0);
    CHECK(fouls_text(across.judgement) == "");
    CHECK_FALSE(across.judgement.post.cue_in_hand);
}

TEST_CASE("S3.5 (spec 8.6): a pocketed cue ball waits at the legal place nearest its usual one") {
    const Vec2 usual = table::default_cue_ball_position();
    // A red sits where the cue ball would wait.
    const FrameState s = frame({ball(Ball::cue, kInOffCue.x, kInOffCue.y), ball(Ball::red, kInOffObject.x, kInOffObject.y),
                                ball(Ball::red, usual.x, usual.y, 1), spotted(Ball::black)});
    const Played p = play(s, strike(aim_at(s, kInOffAim), 2.0));
    REQUIRE(first_hit(p.record, 0) == 1);
    REQUIRE(dropped(p.record, 0));
    REQUIRE(drops(p.record) == 1);
    REQUIRE(same(rest_of(p.record, 2), usual));

    const FrameState& post = p.judgement.post;
    CHECK(post.cue_in_hand);
    const Vec2 waits = cue_pos(post);
    CHECK(table::in_d(waits));
    CHECK(apart(waits, usual) > kDiameter);
    CHECK(apart(waits, usual) < kDiameter + 0.005);
    CHECK(rig().oracle->cue_placement_legal(post.balls, post.cue_ball, waits));
}
