#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "scenario.hpp"

// Section 3 Rule 10 (fouls): what a foul does to the score, the break and the next stroke. Written
// from the rulebook and docs/rules/README.md; docs/rules/clauses-t2.md maps every clause to its
// test here.
//
// REQUIRE states what physically happened, read from the shot record; CHECK states the ruling.

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

double apart(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

bool at(Vec2 a, Vec2 b) { return apart(a, b) < 1e-9; }

// Off the table, so a comparison with it fails.
const Vec2 kNowhere{99.0, 99.0};

Vec2 where(const Position& p, Ball b, int index = 0) {
    const int place = place_of(p, b, index);
    return place < 0 ? kNowhere : p.balls[std::size_t(place)].pos;
}

Vec2 cue_ball(const Position& p) {
    const bool listed = p.cue_ball >= 0 && std::size_t(p.cue_ball) < p.balls.size();
    return listed ? p.balls[std::size_t(p.cue_ball)].pos : kNowhere;
}

int pocket_named(const char* name) {
    const std::vector<phys::Pocket>& pockets = rig().table.pockets;
    for (std::size_t i = 0; i < pockets.size(); ++i) {
        if (pockets[i].name == name) return int(i);
    }
    return -1;
}

// `back` metres short of the top-right pocket's drop centre on the table's diagonal: balls placed
// with this are in line with the pocket.
BallOnTable on_diagonal(Ball b, double back, int index = 0) {
    const Vec2 c = rig().table.pockets[std::size_t(pocket_named("top_right"))].drop_centre;
    const double k = back / std::sqrt(2.0);
    return ball(b, c.x - k, c.y + k, index);
}

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

// The pocket a ball dropped into; -1 while it is on the table.
int pocket_of(const phys::ShotRecord& r, int place) {
    const phys::BallState& b = r.final_state.balls[std::size_t(place)];
    return b.motion == phys::Motion::pocketed ? b.pocket : -1;
}

int potted_count(const phys::ShotRecord& r) {
    return int(std::count_if(r.final_state.balls.begin(), r.final_state.balls.end(),
                             [](const phys::BallState& b) { return b.motion == phys::Motion::pocketed; }));
}

int foul_value(const Judgement& j, FoulKind kind) {
    for (const Foul& f : j.fouls) {
        if (f.kind == kind) return f.value;
    }
    return 0;
}

// The blue straight in front of the top-right pocket with the cue ball behind it, two reds
// elsewhere, and player 0 at the table 25 to 10 up.
FrameState blue_over_the_pocket() {
    FrameState s = frame({on_diagonal(Ball::cue, 0.6), on_diagonal(Ball::blue, 0.3), ball(Ball::red, 0.5, 0.4, 0),
                          ball(Ball::red, 0.3, -0.5, 1), spotted(Ball::yellow), spotted(Ball::green),
                          spotted(Ball::brown), spotted(Ball::pink), spotted(Ball::black)});
    s.scores = {25, 10};
    return s;
}

// The same with a red just potted in a break of 17, so a colour of choice is on.
FrameState blue_over_the_pocket_mid_break() {
    FrameState s = blue_over_the_pocket();
    s.on = {OnKind::any_colour, Ball::yellow};
    s.break_points = 17;
    return s;
}

// The blue is potted and the cue ball, played with top, follows it into the same pocket.
Played pot_the_blue_and_follow_in(const FrameState& s, ShotInput input) {
    phys::CueStrike c = strike(aim_at_ball(s, 1), 1.5);
    c.tip_y = 0.5;
    Played p = play(s, c, input);
    const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].ball == 1);
    REQUIRE(pocket_of(p.record, 1) == pocket_named("top_right"));
    REQUIRE(pocket_of(p.record, s.cue_ball) >= 0);
    REQUIRE(potted_count(p.record) == 2);
    return p;
}

// Red on, and the black lies between the cue ball and a red that hangs over the top-right pocket.
FrameState black_before_the_red() {
    return frame({on_diagonal(Ball::cue, 0.7), on_diagonal(Ball::black, 0.4), on_diagonal(Ball::red, 0.25, 0),
                  ball(Ball::red, 0.5, 0.4, 1), spotted(Ball::yellow), spotted(Ball::green), spotted(Ball::brown),
                  spotted(Ball::blue), spotted(Ball::pink)});
}

// The cue ball hits the black first; the black sends the red in; the black and the cue ball stay
// on the table.
Played play_onto_the_black(const FrameState& s) {
    Played p = play(s, strike(aim_at_ball(s, 1), 1.5));
    const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].ball == 1);
    REQUIRE(pocket_of(p.record, 2) == pocket_named("top_right"));
    REQUIRE(potted_count(p.record) == 1);
    return p;
}

}  // namespace

TEST_CASE("S3.10: FOUL is called, with the penalty, when the stroke is judged") {
    const FrameState s = blue_over_the_pocket_mid_break();
    const Played p = pot_the_blue_and_follow_in(s, nominate(Ball::blue));
    const Judgement& j = p.judgement;

    CHECK(j.fouls.size() == 1);
    CHECK(foul_value(j, FoulKind::in_off) == 5);
    CHECK(j.penalty == 5);
    CHECK_FALSE(j.calls.empty());
    if (!j.calls.empty()) {
        CHECK(j.calls[0].kind == CallKind::foul);
        CHECK(j.calls[0].value == 5);
    }
    CHECK_FALSE(j.miss);
    CHECK(j.post.pending == Pending::after_foul);
    CHECK(j.post.offender == 0);
    CHECK(j.post.decider == 1);
    CHECK(j.post.last_penalty == 5);
}

TEST_CASE("S3.10(b): the stroke is judged when it is complete: a red that drops after the foul stays down") {
    const FrameState s = black_before_the_red();
    const Played p = play_onto_the_black(s);
    const Judgement& j = p.judgement;

    CHECK(j.fouls.size() == 1);
    if (j.fouls.size() == 1) {
        CHECK(j.fouls[0].kind == FoulKind::first_hit_not_on);
        CHECK(j.fouls[0].value == 7);
        CHECK(j.fouls[0].ball == 1);
    }
    CHECK(j.penalty == 7);
    CHECK(j.points == 0);
    CHECK(j.spotted.empty());
    CHECK(place_of(j.post, Ball::red, 0) == -1);
    CHECK(place_of(j.post, Ball::red, 1) >= 0);
    CHECK(j.post.balls.size() == s.balls.size() - 1);
    // The black lies where the stroke left it.
    CHECK(at(where(j.post, Ball::black), {p.record.final_state.balls[1].r.x, p.record.final_state.balls[1].r.y}));
}

TEST_CASE("S3.10(d): a colour pocketed in a foul stroke is put back on its spot") {
    const FrameState s = blue_over_the_pocket_mid_break();
    const Played p = pot_the_blue_and_follow_in(s, nominate(Ball::blue));
    const Judgement& j = p.judgement;

    CHECK(j.spotted.size() == 1);
    if (j.spotted.size() == 1) {
        CHECK(j.spotted[0].ball == Ball::blue);
        CHECK(at(j.spotted[0].pos, table::spot(Ball::blue)));
    }
    CHECK(at(where(j.post, Ball::blue), table::spot(Ball::blue)));
}

TEST_CASE("S3.10(e): the break's earlier points stand; the ball pocketed in the foul stroke scores nothing") {
    const FrameState s = blue_over_the_pocket_mid_break();
    const Played p = pot_the_blue_and_follow_in(s, nominate(Ball::blue));
    const Judgement& j = p.judgement;

    CHECK(j.points == 0);
    CHECK(j.post.scores[0] == 25);
    CHECK(j.post.scores[1] == 15);
    CHECK(j.post.break_points == 0);
}

TEST_CASE("S3.10(f): after a foul that leaves the cue ball on the table, play goes on from where it lies") {
    const FrameState s = black_before_the_red();
    const Played p = play_onto_the_black(s);
    REQUIRE(pocket_of(p.record, s.cue_ball) == -1);
    const Vec2 lie{p.record.final_state.balls[0].r.x, p.record.final_state.balls[0].r.y};

    CHECK(p.judgement.penalty == 7);
    CHECK_FALSE(p.judgement.post.cue_in_hand);
    CHECK(at(cue_ball(p.judgement.post), lie));

    // Whoever plays next plays from there.
    for (DecisionKind choice : {DecisionKind::play, DecisionKind::play_again}) {
        FrameState next = p.judgement.post;
        std::vector<Call> calls;
        std::string error;
        CHECK(decide(next, {choice, 1}, *rig().oracle, rig().ref, calls, error));
        CHECK_FALSE(next.cue_in_hand);
        CHECK(at(cue_ball(next), lie));
    }
}

TEST_CASE("S3.10(f): after an in-off the next stroke is played from in hand") {
    const FrameState s = blue_over_the_pocket_mid_break();
    const Played p = pot_the_blue_and_follow_in(s, nominate(Ball::blue));

    CHECK(p.judgement.post.cue_in_hand);
    CHECK(table::in_d(cue_ball(p.judgement.post)));

    for (DecisionKind choice : {DecisionKind::play, DecisionKind::play_again}) {
        FrameState next = p.judgement.post;
        std::vector<Call> calls;
        std::string error;
        CHECK(decide(next, {choice, 1}, *rig().oracle, rig().ref, calls, error));
        CHECK(next.cue_in_hand);
        // A place in the D, clear of the brown, green and yellow.
        const Vec2 chosen{table::baulk_line_x() - 0.1, 0.1};
        CHECK(place_cue_ball(next, chosen, *rig().oracle, error));
        CHECK(at(cue_ball(next), chosen));
    }
}

TEST_CASE("S3.10(g): several fouls in one stroke cost the highest penalty among them, once") {
    SUBCASE("red on: the blue hit first and pocketed, and an in-off") {
        const FrameState s = blue_over_the_pocket();
        REQUIRE(s.on.kind == OnKind::red);
        const Played p = pot_the_blue_and_follow_in(s, {});
        const Judgement& j = p.judgement;

        CHECK(j.penalty == 5);
        CHECK(j.post.scores[0] == 25);
        CHECK(j.post.scores[1] == 15);
        // Highest first; equal values in the order of FoulKind.
        CHECK(j.fouls.size() == 3);
        if (j.fouls.size() == 3) {
            CHECK(j.fouls[0].kind == FoulKind::pot_not_on);
            CHECK(j.fouls[0].value == 5);
            CHECK(j.fouls[0].ball == 1);
            CHECK(j.fouls[1].kind == FoulKind::first_hit_not_on);
            CHECK(j.fouls[1].value == 5);
            CHECK(j.fouls[1].ball == 1);
            CHECK(j.fouls[2].kind == FoulKind::in_off);
            CHECK(j.fouls[2].value == 4);
        }
        if (!j.calls.empty()) CHECK(j.calls[0].value == 5);
    }
    SUBCASE("red on: the yellow hit first, the pink pocketed, and an in-off") {
        FrameState s = blue_over_the_pocket();
        ShotFacts facts = quiet_facts(s);
        const int yellow = place_of(s, Ball::yellow);
        const int pink = place_of(s, Ball::pink);
        facts.first_hit = {yellow};
        facts.first_hit_t = 0.3;
        facts.closest_gap_m[std::size_t(yellow)] = 0.0;
        facts.pots.push_back({pink, 1, 1.0});
        facts.cue_potted = true;
        facts.cue_pocket = 0;
        facts.cue_travel_m = 2.0;
        facts.object_travel_m = 2.0;
        const Judgement j = judge(s, {}, facts, *rig().oracle, rig().ref);

        CHECK(j.penalty == 6);
        CHECK(j.post.scores[1] == 16);
        CHECK(j.fouls.size() == 3);
        if (j.fouls.size() == 3) {
            CHECK(j.fouls[0].kind == FoulKind::pot_not_on);
            CHECK(j.fouls[0].value == 6);
            CHECK(j.fouls[1].kind == FoulKind::in_off);
            CHECK(j.fouls[1].value == 4);
            CHECK(j.fouls[2].kind == FoulKind::first_hit_not_on);
            CHECK(j.fouls[2].value == 4);
        }
    }
}

TEST_CASE("S3.10(h)(i): the penalty points go to the other player; the offender's score is untouched") {
    FrameState s = black_before_the_red();
    s.scores = {31, 12};
    s.striker = 1;
    const Played p = play_onto_the_black(s);
    const FrameState& post = p.judgement.post;

    CHECK(post.scores[1] == 12);
    CHECK(post.scores[0] == 38);
    CHECK(post.offender == 1);
    CHECK(post.decider == 0);
    CHECK(post.striker == 0);
    CHECK(post.last_penalty == 7);
    CHECK(post.break_points == 0);
    CHECK(post.stats[1].fouls == s.stats[1].fouls + 1);
    CHECK(post.stats[0].points_from_fouls == s.stats[0].points_from_fouls + 7);
}

TEST_CASE("S3.10(h)(ii): asked to play again, the offender plays the next stroke") {
    const FrameState s = blue_over_the_pocket_mid_break();
    const Played p = pot_the_blue_and_follow_in(s, nominate(Ball::blue));
    const FrameState& post = p.judgement.post;

    // The first impact was on the ball declared, so no miss and nothing to replace.
    const std::vector<DecisionKind> offered = {DecisionKind::play, DecisionKind::play_again};
    CHECK(options(post, 1) == offered);

    SUBCASE("the next player asks the offender to play again") {
        FrameState next = post;
        std::vector<Call> calls;
        std::string error;
        CHECK(decide(next, {DecisionKind::play_again, 1}, *rig().oracle, rig().ref, calls, error));
        CHECK(next.pending == Pending::none);
        CHECK(next.striker == 0);
        CHECK(next.on.kind == OnKind::red);
        CHECK(next.break_points == 0);
        CHECK(next.scores[0] == 25);
        CHECK(next.scores[1] == 15);
    }
    SUBCASE("the next player plays") {
        FrameState next = post;
        std::vector<Call> calls;
        std::string error;
        CHECK(decide(next, {DecisionKind::play, 1}, *rig().oracle, rig().ref, calls, error));
        CHECK(next.pending == Pending::none);
        CHECK(next.striker == 1);
        CHECK(next.on.kind == OnKind::red);
        CHECK(next.scores[0] == 25);
        CHECK(next.scores[1] == 15);
    }
    SUBCASE("the offender is not the one to choose") {
        FrameState next = post;
        std::vector<Call> calls;
        std::string error;
        CHECK_FALSE(decide(next, {DecisionKind::play, 0}, *rig().oracle, rig().ref, calls, error));
        CHECK(next.pending == Pending::after_foul);
    }
}

TEST_CASE("S3.10: after a foul the next stroke starts a new turn: red while one remains, else the lowest colour") {
    SUBCASE("a colour of choice was on and reds remain") {
        const FrameState s = blue_over_the_pocket_mid_break();
        const Played p = pot_the_blue_and_follow_in(s, nominate(Ball::blue));
        CHECK(p.judgement.post.on.kind == OnKind::red);
    }
    SUBCASE("a colour of choice was on after the last red") {
        FrameState s = frame({ball(Ball::cue, -0.4, 0.35), spotted(Ball::yellow), spotted(Ball::green),
                              spotted(Ball::brown), spotted(Ball::blue), spotted(Ball::pink), spotted(Ball::black)});
        s.on = {OnKind::any_colour, Ball::yellow};
        ShotFacts facts = quiet_facts(s);
        const int blue = place_of(s, Ball::blue);
        facts.first_hit = {blue};
        facts.first_hit_t = 0.3;
        facts.closest_gap_m[std::size_t(blue)] = 0.0;
        facts.cue_potted = true;
        facts.cue_pocket = 2;
        facts.cue_travel_m = 1.5;
        facts.object_travel_m = 1.0;
        const Judgement j = judge(s, nominate(Ball::blue), facts, *rig().oracle, rig().ref);
        CHECK(foul_value(j, FoulKind::in_off) == 5);
        CHECK(j.penalty == 5);
        CHECK(j.post.on.kind == OnKind::colour);
        CHECK(j.post.on.colour == Ball::yellow);
    }
    SUBCASE("the green was on and stays on") {
        const FrameState s = frame({ball(Ball::cue, -0.4, 0.35), spotted(Ball::green), spotted(Ball::brown),
                                    spotted(Ball::blue), spotted(Ball::pink), spotted(Ball::black)});
        REQUIRE(s.on.colour == Ball::green);
        const Judgement j = judge(s, {}, quiet_facts(s), *rig().oracle, rig().ref);
        CHECK(foul_value(j, FoulKind::no_hit) == 4);
        CHECK(j.post.on.kind == OnKind::colour);
        CHECK(j.post.on.colour == Ball::green);
    }
}
