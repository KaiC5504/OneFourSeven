#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "scenario.hpp"

// Section 3 Rule 11 (penalty values): each foul that can happen in the game, with its exact value
// against several balls on. Written from the rulebook and docs/rules/README.md;
// docs/rules/clauses-t2.md maps every clause to its test here.
//
// Each foul is played once on the real table (REQUIRE states what physically happened, CHECK the
// ruling); its values against the other balls on are then judged from facts written by hand, since
// only the arithmetic differs.

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

constexpr double kR = table::kBallRadius;
const double kPi = std::acos(-1.0);

const std::vector<Ball> kColours = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};

std::string name(Ball b) { return std::string(table::ball_name(b)); }

int pocket_named(const char* pocket) {
    const std::vector<phys::Pocket>& pockets = rig().table.pockets;
    for (std::size_t i = 0; i < pockets.size(); ++i) {
        if (pockets[i].name == pocket) return int(i);
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

// The cue ball in the open, three reds and the six colours on their spots; player 0 to play,
// 10 to 6 up.
FrameState with_reds() {
    FrameState s = frame({ball(Ball::cue, -0.4, 0.35), ball(Ball::red, 0.45, 0.4, 0), ball(Ball::red, 0.6, -0.35, 1),
                          ball(Ball::red, 1.1, 0.45, 2), spotted(Ball::yellow), spotted(Ball::green),
                          spotted(Ball::brown), spotted(Ball::blue), spotted(Ball::pink), spotted(Ball::black)});
    s.scores = {10, 6};
    return s;
}

// The same after a red has been potted: a colour of the striker's choice is on.
FrameState with_choice() {
    FrameState s = with_reds();
    s.on = {OnKind::any_colour, Ball::yellow};
    return s;
}

// The reds gone: `lowest` is on, with every colour above it, each on its spot.
FrameState colours_from(Ball lowest) {
    std::vector<BallOnTable> balls = {ball(Ball::cue, -0.4, 0.35)};
    for (Ball c : kColours) {
        if (table::ball_value(c) >= table::ball_value(lowest)) balls.push_back(spotted(c));
    }
    FrameState s = frame(std::move(balls));
    s.scores = {10, 6};
    return s;
}

// A free ball has been called.
FrameState free_ball(FrameState s) {
    s.free_ball = true;
    return s;
}

int place(const FrameState& s, Ball b, int index = 0) { return place_of(s, b, index); }

// Facts for a stroke whose first impact was on these balls and in which nothing else happened.
ShotFacts hit(const FrameState& s, std::vector<int> first) {
    ShotFacts f = quiet_facts(s);
    std::sort(first.begin(), first.end());
    for (int b : first) {
        REQUIRE(b >= 0);
        f.closest_gap_m[std::size_t(b)] = 0.0;
    }
    f.first_hit = std::move(first);
    f.first_hit_t = 0.3;
    f.cue_travel_m = 1.5;
    f.object_travel_m = 1.0;
    return f;
}

ShotFacts and_pocketed(ShotFacts f, int ball_place) {
    REQUIRE(ball_place >= 0);
    f.pots.push_back({ball_place, 1, 1.0 + 0.1 * double(f.pots.size())});
    return f;
}

ShotFacts and_in_off(ShotFacts f) {
    f.cue_potted = true;
    f.cue_pocket = 0;
    return f;
}

ShotFacts and_pushed(ShotFacts f) {
    f.push = true;
    return f;
}

Judgement ruled(const FrameState& s, const ShotFacts& facts, ShotInput input = {}) {
    return judge(s, input, facts, *rig().oracle, rig().ref);
}

// The stroke was a foul under `kind` at `value`, and that is what it cost.
void check_penalty(const std::string& what, const FrameState& pre, const Judgement& j, FoulKind kind, int value) {
    CAPTURE(what);
    CHECK(foul_value(j, kind) == value);
    CHECK(j.penalty == value);
    CHECK(j.points == 0);
    CHECK(j.post.scores[0] == pre.scores[0]);
    CHECK(j.post.scores[1] == pre.scores[1] + value);
}

// A legal pot of the red over the top-right pocket; `further` is another red in the open.
FrameState red_over_the_pocket() {
    return frame({on_diagonal(Ball::cue, 0.6), on_diagonal(Ball::red, 0.3, 0), ball(Ball::red, 1.2, -0.3, 1),
                  ball(Ball::red, 0.5, 0.45, 2), spotted(Ball::yellow), spotted(Ball::green), spotted(Ball::brown),
                  spotted(Ball::blue), spotted(Ball::pink), spotted(Ball::black)});
}

}  // namespace

TEST_CASE("S3.11(a)(vi): a stroke that hits no ball costs the value of the ball on, four at least") {
    SUBCASE("played: red on, the cue ball stops in the open") {
        const FrameState s = with_reds();
        const Played p = play(s, strike(kPi / 2.0, 0.2));
        REQUIRE(cue_contacts(p.record, s.cue_ball).empty());
        REQUIRE(potted_count(p.record) == 0);
        REQUIRE(std::abs(p.record.final_state.balls[0].r.y - s.balls[0].pos.y) > 0.05);

        CHECK(p.judgement.fouls.size() == 1);
        check_penalty("red on", s, p.judgement, FoulKind::no_hit, 4);
        CHECK(p.judgement.post.pending == Pending::after_foul);
    }
    SUBCASE("each colour on in its turn") {
        const std::pair<Ball, int> rows[] = {{Ball::yellow, 4}, {Ball::green, 4}, {Ball::brown, 4},
                                             {Ball::blue, 5},   {Ball::pink, 6},  {Ball::black, 7}};
        for (const auto& [on, value] : rows) {
            const FrameState s = colours_from(on);
            check_penalty(name(on) + " on", s, ruled(s, quiet_facts(s)), FoulKind::no_hit, value);
        }
    }
    SUBCASE("each colour declared after a red") {
        const std::pair<Ball, int> rows[] = {{Ball::yellow, 4}, {Ball::green, 4}, {Ball::brown, 4},
                                             {Ball::blue, 5},   {Ball::pink, 6},  {Ball::black, 7}};
        for (const auto& [declared, value] : rows) {
            const FrameState s = with_choice();
            check_penalty(name(declared) + " declared", s, ruled(s, quiet_facts(s), nominate(declared)),
                          FoulKind::no_hit, value);
        }
    }
    SUBCASE("a free ball nominated: still the value of the ball on") {
        const FrameState reds = free_ball(with_reds());
        check_penalty("red on, black as the free ball", reds, ruled(reds, quiet_facts(reds), nominate(Ball::black)),
                      FoulKind::no_hit, 4);
        const FrameState blue = free_ball(colours_from(Ball::blue));
        check_penalty("blue on, black as the free ball", blue, ruled(blue, quiet_facts(blue), nominate(Ball::black)),
                      FoulKind::no_hit, 5);
    }
}

TEST_CASE("S3.11(a)(vii): the cue ball in a pocket costs the value of the ball on, four at least") {
    SUBCASE("played: a red potted and the cue ball follows it in") {
        const FrameState s = red_over_the_pocket();
        phys::CueStrike c = strike(aim_at_ball(s, 1), 1.5);
        c.tip_y = 0.5;
        const Played p = play(s, c);
        const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
        REQUIRE_FALSE(hits.empty());
        REQUIRE(hits[0].ball == 1);
        REQUIRE(pocket_of(p.record, 1) == pocket_named("top_right"));
        REQUIRE(pocket_of(p.record, s.cue_ball) >= 0);
        REQUIRE(potted_count(p.record) == 2);

        CHECK(p.judgement.fouls.size() == 1);
        CHECK(foul_value(p.judgement, FoulKind::in_off) == 4);
        CHECK(p.judgement.penalty == 4);
        CHECK(p.judgement.points == 0);
        CHECK(p.judgement.post.scores[0] == 0);
        CHECK(p.judgement.post.scores[1] == 4);
        // S3.3(i): the red stays down.
        CHECK(place_of(p.judgement.post, Ball::red, 0) == -1);
        CHECK(p.judgement.post.cue_in_hand);
    }
    SUBCASE("red on, after hitting a red") {
        const FrameState s = with_reds();
        check_penalty("red on", s, ruled(s, and_in_off(hit(s, {place(s, Ball::red, 0)}))), FoulKind::in_off, 4);
    }
    SUBCASE("each colour on in its turn, after hitting it") {
        const std::pair<Ball, int> rows[] = {{Ball::yellow, 4}, {Ball::green, 4}, {Ball::brown, 4},
                                             {Ball::blue, 5},   {Ball::pink, 6},  {Ball::black, 7}};
        for (const auto& [on, value] : rows) {
            const FrameState s = colours_from(on);
            check_penalty(name(on) + " on", s, ruled(s, and_in_off(hit(s, {place(s, on)}))), FoulKind::in_off, value);
        }
    }
    SUBCASE("each colour declared after a red, after hitting it") {
        const std::pair<Ball, int> rows[] = {{Ball::yellow, 4}, {Ball::green, 4}, {Ball::brown, 4},
                                             {Ball::blue, 5},   {Ball::pink, 6},  {Ball::black, 7}};
        for (const auto& [declared, value] : rows) {
            const FrameState s = with_choice();
            check_penalty(name(declared) + " declared", s,
                          ruled(s, and_in_off(hit(s, {place(s, declared)})), nominate(declared)), FoulKind::in_off,
                          value);
        }
    }
    SUBCASE("a colour hit after a red with nothing said is the one the striker was on") {
        const FrameState s = with_choice();
        check_penalty("pink hit, nothing declared", s, ruled(s, and_in_off(hit(s, {place(s, Ball::pink)}))),
                      FoulKind::in_off, 6);
        check_penalty("yellow hit, nothing declared", s, ruled(s, and_in_off(hit(s, {place(s, Ball::yellow)}))),
                      FoulKind::in_off, 4);
    }
}

TEST_CASE("S3.11(a)(viii): the cue ball left snookered behind the free ball costs the value of the ball on") {
    // The stroke hits the free ball and scores nothing; at rest the free ball lies on the line from
    // the cue ball to the only ball on, with three object balls or more still on the table.
    const Vec2 cue_lie{0.2, 0.5};
    const Vec2 free_lie{0.5, 0.5};
    const Vec2 on_lie{1.0, 0.5};

    SUBCASE("red on, the blue taken as the free ball") {
        FrameState s = frame({ball(Ball::cue, -0.4, 0.35), ball(Ball::red, on_lie.x, on_lie.y, 0),
                              ball(Ball::blue, 0.3, 0.3), spotted(Ball::yellow), spotted(Ball::green),
                              spotted(Ball::brown), spotted(Ball::pink), spotted(Ball::black)});
        s.scores = {10, 6};
        s.free_ball = true;
        ShotFacts facts = hit(s, {place(s, Ball::blue)});
        facts.rest[0] = cue_lie;
        facts.rest[std::size_t(place(s, Ball::blue))] = free_lie;
        check_penalty("red on", s, ruled(s, facts, nominate(Ball::blue)), FoulKind::free_ball_snooker, 4);
    }
    SUBCASE("brown on, the pink taken as the free ball") {
        FrameState s = frame({ball(Ball::cue, -0.4, 0.35), ball(Ball::brown, on_lie.x, on_lie.y),
                              spotted(Ball::blue), ball(Ball::pink, 0.3, 0.3), spotted(Ball::black)});
        s.scores = {10, 6};
        s.free_ball = true;
        ShotFacts facts = hit(s, {place(s, Ball::pink)});
        facts.rest[0] = cue_lie;
        facts.rest[std::size_t(place(s, Ball::pink))] = free_lie;
        check_penalty("brown on", s, ruled(s, facts, nominate(Ball::pink)), FoulKind::free_ball_snooker, 4);
    }
    SUBCASE("blue on, the pink taken as the free ball") {
        FrameState s = frame({ball(Ball::cue, -0.4, 0.35), ball(Ball::blue, on_lie.x, on_lie.y),
                              ball(Ball::pink, 0.3, 0.3), spotted(Ball::black)});
        s.scores = {10, 6};
        s.free_ball = true;
        ShotFacts facts = hit(s, {place(s, Ball::pink)});
        facts.rest[0] = cue_lie;
        facts.rest[std::size_t(place(s, Ball::pink))] = free_lie;
        check_penalty("blue on", s, ruled(s, facts, nominate(Ball::pink)), FoulKind::free_ball_snooker, 5);
    }
}

TEST_CASE("S3.11(b)(iii): pocketing a ball not on costs the ball on or that ball, whichever is higher") {
    SUBCASE("played: red on, a red struck onto the black, which goes in") {
        const FrameState s = frame({on_diagonal(Ball::cue, 0.6), on_diagonal(Ball::red, 0.32, 0),
                                    on_diagonal(Ball::black, 0.2), ball(Ball::red, 0.5, 0.45, 1),
                                    spotted(Ball::yellow), spotted(Ball::green), spotted(Ball::brown),
                                    spotted(Ball::blue), spotted(Ball::pink)});
        const Played p = play(s, strike(aim_at_ball(s, 1), 1.5));
        const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
        REQUIRE_FALSE(hits.empty());
        REQUIRE(hits[0].ball == 1);
        REQUIRE(pocket_of(p.record, 2) == pocket_named("top_right"));
        REQUIRE(potted_count(p.record) == 1);

        const Judgement& j = p.judgement;
        CHECK(j.fouls.size() == 1);
        if (j.fouls.size() == 1) CHECK(j.fouls[0].ball == 2);
        check_penalty("red on, black pocketed", s, j, FoulKind::pot_not_on, 7);
    }
    SUBCASE("red on, a red hit first") {
        const std::pair<Ball, int> rows[] = {{Ball::yellow, 4}, {Ball::green, 4}, {Ball::brown, 4},
                                             {Ball::blue, 5},   {Ball::pink, 6},  {Ball::black, 7}};
        for (const auto& [pocketed, value] : rows) {
            const FrameState s = with_reds();
            check_penalty(name(pocketed) + " pocketed", s,
                          ruled(s, and_pocketed(hit(s, {place(s, Ball::red, 0)}), place(s, pocketed))),
                          FoulKind::pot_not_on, value);
        }
    }
    SUBCASE("a colour on in its turn, hit first") {
        struct Row {
            Ball on;
            Ball pocketed;
            int value;
        };
        const Row rows[] = {{Ball::yellow, Ball::green, 4}, {Ball::yellow, Ball::blue, 5},
                            {Ball::yellow, Ball::black, 7}, {Ball::brown, Ball::blue, 5},
                            {Ball::blue, Ball::pink, 6},    {Ball::blue, Ball::black, 7},
                            {Ball::pink, Ball::black, 7}};
        for (const Row& row : rows) {
            const FrameState s = colours_from(row.on);
            check_penalty(name(row.on) + " on, " + name(row.pocketed) + " pocketed", s,
                          ruled(s, and_pocketed(hit(s, {place(s, row.on)}), place(s, row.pocketed))),
                          FoulKind::pot_not_on, row.value);
        }
    }
    SUBCASE("a colour declared after a red, hit first") {
        struct Row {
            Ball declared;
            Ball pocketed;
            int value;
        };
        // The ball on is the dearer in the first three, the ball pocketed in the last two.
        const Row rows[] = {{Ball::black, Ball::red, 7},  {Ball::yellow, Ball::red, 4}, {Ball::pink, Ball::blue, 6},
                            {Ball::blue, Ball::pink, 6},  {Ball::green, Ball::black, 7}};
        for (const Row& row : rows) {
            const FrameState s = with_choice();
            check_penalty(name(row.declared) + " declared, " + name(row.pocketed) + " pocketed", s,
                          ruled(s, and_pocketed(hit(s, {place(s, row.declared)}), place(s, row.pocketed)),
                                nominate(row.declared)),
                          FoulKind::pot_not_on, row.value);
        }
    }
}

TEST_CASE("S3.11(b)(iv): hitting a ball not on first costs the ball on or that ball, whichever is higher") {
    SUBCASE("played: red on, the cue ball runs into the blue") {
        const FrameState s = with_reds();
        const int blue = place(s, Ball::blue);
        const Played p = play(s, strike(aim_at_ball(s, blue), 0.5));
        const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
        REQUIRE_FALSE(hits.empty());
        REQUIRE(hits[0].ball == blue);
        REQUIRE(potted_count(p.record) == 0);

        const Judgement& j = p.judgement;
        CHECK(j.fouls.size() == 1);
        if (j.fouls.size() == 1) CHECK(j.fouls[0].ball == blue);
        check_penalty("red on, blue hit", s, j, FoulKind::first_hit_not_on, 5);
    }
    SUBCASE("red on") {
        const std::pair<Ball, int> rows[] = {{Ball::yellow, 4}, {Ball::green, 4}, {Ball::brown, 4},
                                             {Ball::blue, 5},   {Ball::pink, 6},  {Ball::black, 7}};
        for (const auto& [struck, value] : rows) {
            const FrameState s = with_reds();
            check_penalty(name(struck) + " hit", s, ruled(s, hit(s, {place(s, struck)})), FoulKind::first_hit_not_on,
                          value);
        }
    }
    SUBCASE("a colour on in its turn") {
        struct Row {
            Ball on;
            Ball struck;
            int value;
        };
        const Row rows[] = {{Ball::yellow, Ball::green, 4}, {Ball::yellow, Ball::brown, 4},
                            {Ball::yellow, Ball::blue, 5},  {Ball::yellow, Ball::pink, 6},
                            {Ball::yellow, Ball::black, 7}, {Ball::green, Ball::brown, 4},
                            {Ball::brown, Ball::blue, 5},   {Ball::blue, Ball::pink, 6},
                            {Ball::blue, Ball::black, 7},   {Ball::pink, Ball::black, 7}};
        for (const Row& row : rows) {
            const FrameState s = colours_from(row.on);
            check_penalty(name(row.on) + " on, " + name(row.struck) + " hit", s,
                          ruled(s, hit(s, {place(s, row.struck)})), FoulKind::first_hit_not_on, row.value);
        }
    }
    SUBCASE("a colour declared after a red") {
        struct Row {
            Ball declared;
            Ball struck;
            int value;
        };
        // A red hit by mistake with a colour declared is a ball not on like any other, not (d)(iii).
        const Row rows[] = {{Ball::yellow, Ball::green, 4}, {Ball::yellow, Ball::black, 7},
                            {Ball::black, Ball::yellow, 7}, {Ball::green, Ball::blue, 5},
                            {Ball::pink, Ball::blue, 6},    {Ball::yellow, Ball::red, 4},
                            {Ball::blue, Ball::red, 5},     {Ball::black, Ball::red, 7}};
        for (const Row& row : rows) {
            const FrameState s = with_choice();
            const Judgement j = ruled(s, hit(s, {place(s, row.struck)}), nominate(row.declared));
            check_penalty(name(row.declared) + " declared, " + name(row.struck) + " hit", s, j,
                          FoulKind::first_hit_not_on, row.value);
            CHECK_FALSE(has_foul(j, FoulKind::reds_in_succession));
        }
    }
    SUBCASE("a free ball nominated and another ball hit first") {
        const FrameState reds = free_ball(with_reds());
        check_penalty("red on, yellow nominated, pink hit", reds,
                      ruled(reds, hit(reds, {place(reds, Ball::pink)}), nominate(Ball::yellow)),
                      FoulKind::first_hit_not_on, 6);
        check_penalty("red on, yellow nominated, a red hit", reds,
                      ruled(reds, hit(reds, {place(reds, Ball::red, 0)}), nominate(Ball::yellow)),
                      FoulKind::first_hit_not_on, 4);
        const FrameState blue = free_ball(colours_from(Ball::blue));
        check_penalty("blue on, black nominated, the blue hit", blue,
                      ruled(blue, hit(blue, {place(blue, Ball::blue)}), nominate(Ball::black)),
                      FoulKind::first_hit_not_on, 5);
        check_penalty("blue on, black nominated, the pink hit", blue,
                      ruled(blue, hit(blue, {place(blue, Ball::pink)}), nominate(Ball::black)),
                      FoulKind::first_hit_not_on, 6);
    }
}

TEST_CASE("S3.11(b)(v): a push stroke costs the ball on or the ball pushed, whichever is higher") {
    SUBCASE("played: red on, a red a hair's breadth from the cue ball, struck full") {
        const FrameState s = frame({ball(Ball::cue, 0.3, 0.2), ball(Ball::red, 0.3 + 2.0 * kR + 0.0003, 0.2, 0),
                                    ball(Ball::red, 0.9, -0.4, 1), spotted(Ball::yellow), spotted(Ball::green),
                                    spotted(Ball::brown), spotted(Ball::blue), spotted(Ball::pink),
                                    spotted(Ball::black)});
        const Played p = play(s, strike(0.0, 1.5));
        const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
        REQUIRE_FALSE(hits.empty());
        REQUIRE(hits[0].ball == 1);
        // The tip is still on the cue ball when it reaches the red.
        REQUIRE(hits[0].t > 0.0);
        REQUIRE(hits[0].t < rig().ref.push_contact_time_s);
        REQUIRE(potted_count(p.record) == 0);

        check_penalty("red on, red pushed", s, p.judgement, FoulKind::push_stroke, 4);
    }
    SUBCASE("red on") {
        const FrameState s = with_reds();
        check_penalty("a red pushed", s, ruled(s, and_pushed(hit(s, {place(s, Ball::red, 0)}))), FoulKind::push_stroke,
                      4);
        check_penalty("the pink pushed", s, ruled(s, and_pushed(hit(s, {place(s, Ball::pink)}))),
                      FoulKind::push_stroke, 6);
    }
    SUBCASE("a colour on in its turn") {
        const std::pair<Ball, int> rows[] = {{Ball::yellow, 4}, {Ball::blue, 5}, {Ball::pink, 6}, {Ball::black, 7}};
        for (const auto& [on, value] : rows) {
            const FrameState s = colours_from(on);
            check_penalty(name(on) + " on and pushed", s, ruled(s, and_pushed(hit(s, {place(s, on)}))),
                          FoulKind::push_stroke, value);
        }
    }
    SUBCASE("a colour declared after a red") {
        const FrameState s = with_choice();
        check_penalty("black declared and pushed", s,
                      ruled(s, and_pushed(hit(s, {place(s, Ball::black)})), nominate(Ball::black)),
                      FoulKind::push_stroke, 7);
        check_penalty("green declared and pushed", s,
                      ruled(s, and_pushed(hit(s, {place(s, Ball::green)})), nominate(Ball::green)),
                      FoulKind::push_stroke, 4);
    }
}

TEST_CASE("S3.11(c): two balls hit at once cost the ball on or the higher of the two, whichever is higher") {
    // Played on the table in the S3.6 scenarios; here the values.
    SUBCASE("red on") {
        struct Row {
            Ball first;
            Ball second;
            int value;
        };
        const Row rows[] = {{Ball::red, Ball::yellow, 4},   {Ball::red, Ball::blue, 5},   {Ball::red, Ball::pink, 6},
                            {Ball::red, Ball::black, 7},    {Ball::yellow, Ball::green, 4},
                            {Ball::pink, Ball::black, 7}};
        for (const Row& row : rows) {
            const FrameState s = with_reds();
            check_penalty(name(row.first) + " and " + name(row.second), s,
                          ruled(s, hit(s, {place(s, row.first), place(s, row.second)})), FoulKind::simultaneous_hit,
                          row.value);
        }
    }
    SUBCASE("a colour on in its turn") {
        struct Row {
            Ball on;
            Ball other;
            int value;
        };
        const Row rows[] = {{Ball::yellow, Ball::green, 4}, {Ball::yellow, Ball::blue, 5},
                            {Ball::blue, Ball::pink, 6},    {Ball::blue, Ball::black, 7},
                            {Ball::pink, Ball::black, 7}};
        for (const Row& row : rows) {
            const FrameState s = colours_from(row.on);
            check_penalty(name(row.on) + " on, with the " + name(row.other), s,
                          ruled(s, hit(s, {place(s, row.on), place(s, row.other)})), FoulKind::simultaneous_hit,
                          row.value);
        }
    }
    SUBCASE("a colour declared after a red") {
        struct Row {
            Ball declared;
            Ball other;
            int value;
        };
        const Row rows[] = {{Ball::blue, Ball::pink, 6},    {Ball::blue, Ball::yellow, 5},
                            {Ball::black, Ball::yellow, 7}, {Ball::yellow, Ball::green, 4},
                            {Ball::blue, Ball::red, 5}};
        for (const Row& row : rows) {
            const FrameState s = with_choice();
            check_penalty(name(row.declared) + " declared, with the " + name(row.other), s,
                          ruled(s, hit(s, {place(s, row.declared), place(s, row.other)}), nominate(row.declared)),
                          FoulKind::simultaneous_hit, row.value);
        }
    }
    SUBCASE("two reds with a colour declared: the reds are not on") {
        const FrameState s = with_choice();
        const Judgement j =
            ruled(s, hit(s, {place(s, Ball::red, 0), place(s, Ball::red, 1)}), nominate(Ball::pink));
        CHECK(j.penalty == 6);
        CHECK(j.post.scores[1] == s.scores[1] + 6);
    }
    SUBCASE("the free ball with a ball that is not on") {
        const FrameState s = free_ball(with_reds());
        check_penalty("red on, yellow nominated, with the pink", s,
                      ruled(s, hit(s, {place(s, Ball::yellow), place(s, Ball::pink)}), nominate(Ball::yellow)),
                      FoulKind::simultaneous_hit, 6);
    }
    SUBCASE("the pairs the rule allows cost nothing") {
        const FrameState reds = with_reds();
        const Judgement two_reds = ruled(reds, hit(reds, {place(reds, Ball::red, 0), place(reds, Ball::red, 1)}));
        CHECK(two_reds.fouls.empty());
        CHECK(two_reds.penalty == 0);
        const FrameState called = free_ball(with_reds());
        const Judgement with_free = ruled(
            called, hit(called, {place(called, Ball::red, 0), place(called, Ball::yellow)}), nominate(Ball::yellow));
        CHECK(with_free.fouls.empty());
        CHECK(with_free.penalty == 0);
    }
}

TEST_CASE("S3.11(d)(iii): a red played at straight after a red costs seven") {
    const FrameState s = red_over_the_pocket();
    const Played first = play(s, strike(aim_at_ball(s, 1), 1.5));
    const std::vector<Contact> hits = cue_contacts(first.record, s.cue_ball);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].ball == 1);
    REQUIRE(pocket_of(first.record, 1) == pocket_named("top_right"));
    REQUIRE(potted_count(first.record) == 1);

    const FrameState& mid = first.judgement.post;
    CHECK(first.judgement.points == 1);
    CHECK(mid.striker == 0);
    CHECK(mid.pending == Pending::none);
    CHECK(mid.on.kind == OnKind::any_colour);
    const int next_red = place_of(mid, Ball::red, 1);
    CHECK(next_red >= 0);
    if (mid.on.kind != OnKind::any_colour || mid.pending != Pending::none || next_red < 0) return;

    // Nothing declared, and the next ball the cue ball meets is another red.
    const Played second = play(mid, strike(aim_at_ball(mid, next_red), 0.45));
    const std::vector<Contact> second_hits = cue_contacts(second.record, mid.cue_ball);
    REQUIRE_FALSE(second_hits.empty());
    REQUIRE(second_hits[0].ball == next_red);
    REQUIRE(potted_count(second.record) == 0);

    const Judgement& j = second.judgement;
    CHECK(foul_value(j, FoulKind::reds_in_succession) == 7);
    CHECK(j.penalty == 7);
    CHECK(j.points == 0);
    CHECK(j.post.scores[0] == 1);
    CHECK(j.post.scores[1] == 7);
    CHECK(j.post.on.kind == OnKind::red);
}

TEST_CASE("S3.11(d)(iii): a red played at straight after a free ball taken as a red costs seven") {
    FrameState s = frame({on_diagonal(Ball::cue, 0.6), on_diagonal(Ball::blue, 0.3), ball(Ball::red, 1.2, -0.3, 0),
                          ball(Ball::red, 0.5, 0.45, 1), spotted(Ball::yellow), spotted(Ball::green),
                          spotted(Ball::brown), spotted(Ball::pink), spotted(Ball::black)});
    s.free_ball = true;
    const Played first = play(s, strike(aim_at_ball(s, 1), 1.5), nominate(Ball::blue));
    const std::vector<Contact> hits = cue_contacts(first.record, s.cue_ball);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].ball == 1);
    REQUIRE(pocket_of(first.record, 1) == pocket_named("top_right"));
    REQUIRE(potted_count(first.record) == 1);

    // S3.12(c): the free ball scores as a red and comes back; a colour is then on.
    const FrameState& mid = first.judgement.post;
    CHECK(first.judgement.points == 1);
    CHECK(mid.striker == 0);
    CHECK(mid.pending == Pending::none);
    CHECK(mid.on.kind == OnKind::any_colour);
    const int next_red = place_of(mid, Ball::red, 0);
    CHECK(next_red >= 0);
    if (mid.on.kind != OnKind::any_colour || mid.pending != Pending::none || next_red < 0) return;

    const Played second = play(mid, strike(aim_at_ball(mid, next_red), 0.45));
    const std::vector<Contact> second_hits = cue_contacts(second.record, mid.cue_ball);
    REQUIRE_FALSE(second_hits.empty());
    REQUIRE(second_hits[0].ball == next_red);
    REQUIRE(potted_count(second.record) == 0);

    const Judgement& j = second.judgement;
    CHECK(foul_value(j, FoulKind::reds_in_succession) == 7);
    CHECK(j.penalty == 7);
    CHECK(j.post.scores[0] == 1);
    CHECK(j.post.scores[1] == 7);
}

TEST_CASE("S3.11(d)(iii): the value is seven whichever red is hit and whatever else happens") {
    const FrameState s = with_choice();
    check_penalty("a red hit, nothing declared", s, ruled(s, hit(s, {place(s, Ball::red, 2)})),
                  FoulKind::reds_in_succession, 7);
    check_penalty("a red hit and an in-off, nothing declared", s,
                  ruled(s, and_in_off(hit(s, {place(s, Ball::red, 0)}))), FoulKind::reds_in_succession, 7);
}

TEST_CASE("S3.11(d)(vi): a foul after potting a red, before any colour is declared, costs seven") {
    SUBCASE("played: nothing declared and the cue ball stops in the open") {
        const FrameState s = with_choice();
        const Played p = play(s, strike(kPi / 2.0, 0.2));
        REQUIRE(cue_contacts(p.record, s.cue_ball).empty());
        REQUIRE(potted_count(p.record) == 0);

        check_penalty("nothing declared, nothing hit", s, p.judgement, FoulKind::before_nominating, 7);
        CHECK_FALSE(p.judgement.nominated.has_nomination);
        CHECK(p.judgement.post.on.kind == OnKind::red);
    }
    SUBCASE("nothing declared, nothing hit, and the cue ball in a pocket") {
        const FrameState s = with_choice();
        check_penalty("in-off with nothing hit", s, ruled(s, and_in_off(quiet_facts(s))),
                      FoulKind::before_nominating, 7);
    }
    SUBCASE("a colour declared: the same stroke costs that colour's value") {
        const FrameState s = with_choice();
        const Judgement j = ruled(s, quiet_facts(s), nominate(Ball::yellow));
        check_penalty("yellow declared, nothing hit", s, j, FoulKind::no_hit, 4);
        CHECK_FALSE(has_foul(j, FoulKind::before_nominating));
    }
    SUBCASE("a colour hit first with nothing said was the declaration") {
        const FrameState s = with_choice();
        const Judgement j = ruled(s, and_in_off(hit(s, {place(s, Ball::green)})));
        check_penalty("green hit, in-off", s, j, FoulKind::in_off, 4);
        CHECK_FALSE(has_foul(j, FoulKind::before_nominating));
        CHECK(j.nominated.has_nomination);
        CHECK(j.nominated.nominated == Ball::green);
    }
}

TEST_CASE("S3.11(a)(v): nothing can be played from outside the D: no such place is accepted") {
    FrameState s = with_reds();
    const Vec2 start = table::default_cue_ball_position();
    s.balls[0].pos = start;
    s.cue_in_hand = true;
    std::string error;

    // Just above the baulk line, and behind it but outside the semicircle.
    CHECK_FALSE(place_cue_ball(s, {table::baulk_line_x() + 0.01, 0.0}, *rig().oracle, error));
    CHECK_FALSE(place_cue_ball(s, {table::baulk_line_x() - 0.3, 0.1}, *rig().oracle, error));
    CHECK(s.cue_in_hand);
    CHECK(std::hypot(s.balls[0].pos.x - start.x, s.balls[0].pos.y - start.y) < 1e-12);
    CHECK(place_cue_ball(s, {table::baulk_line_x() - 0.1, 0.1}, *rig().oracle, error));
}

TEST_CASE("S3.10(j), S3.11(b)(vii): the cue ball in hand cannot be put on or against a ball in play") {
    FrameState s = with_reds();
    const Vec2 start = table::default_cue_ball_position();
    s.balls[0].pos = start;
    s.cue_in_hand = true;
    std::string error;

    const Vec2 brown = table::spot(Ball::brown);
    CHECK_FALSE(place_cue_ball(s, brown, *rig().oracle, error));
    CHECK_FALSE(place_cue_ball(s, {brown.x - kR, brown.y}, *rig().oracle, error));
    CHECK_FALSE(place_cue_ball(s, {brown.x - 2.0 * kR, brown.y}, *rig().oracle, error));
    CHECK(std::hypot(s.balls[0].pos.x - start.x, s.balls[0].pos.y - start.y) < 1e-12);
    CHECK(place_cue_ball(s, {brown.x - 2.0 * kR - 0.001, brown.y}, *rig().oracle, error));
}

TEST_CASE("S3.11: no foul costs less than four") {
    // The balls worth less than four, as the ball on and as the ball concerned.
    const FrameState yellow = colours_from(Ball::yellow);
    CHECK(ruled(yellow, quiet_facts(yellow)).penalty == 4);
    CHECK(ruled(yellow, hit(yellow, {place(yellow, Ball::green)})).penalty == 4);
    const ShotFacts brown_in = and_pocketed(hit(yellow, {place(yellow, Ball::yellow)}), place(yellow, Ball::brown));
    CHECK(ruled(yellow, brown_in).penalty == 4);
    const FrameState reds = with_reds();
    CHECK(ruled(reds, quiet_facts(reds)).penalty == 4);
    CHECK(ruled(reds, and_in_off(hit(reds, {place(reds, Ball::red, 1)}))).penalty == 4);
    const FrameState choice = with_choice();
    const ShotFacts red_in = and_pocketed(hit(choice, {place(choice, Ball::green)}), place(choice, Ball::red, 0));
    CHECK(ruled(choice, red_in, nominate(Ball::green)).penalty == 4);
}
