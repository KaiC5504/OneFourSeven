#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ofs/physics/record_query.hpp"
#include "scenario.hpp"

// Section 3 Rules 7 (spotting colours) and 9 (ball on the edge of a pocket), written from the
// rulebook and docs/rules/README.md. docs/rules/clauses-t2.md maps every clause to its test here.
//
// REQUIRE states what physically happened, read from the shot record; CHECK states the ruling.

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

constexpr double kR = table::kBallRadius;

const std::vector<Ball> kColours = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};

double apart(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

bool at(Vec2 a, Vec2 b) { return apart(a, b) < 1e-9; }

// Off the table, so a comparison with it fails.
const Vec2 kNowhere{99.0, 99.0};

Vec2 where(const Position& p, Ball b, int index = 0) {
    const int place = place_of(p, b, index);
    return place < 0 ? kNowhere : p.balls[std::size_t(place)].pos;
}

// The cue ball out of everything's way and every colour on its own spot except those in `moved`.
std::vector<BallOnTable> colours_on_spots_except(const std::vector<Ball>& moved) {
    std::vector<BallOnTable> balls = {ball(Ball::cue, -0.4, 0.35)};
    for (Ball c : kColours) {
        if (std::find(moved.begin(), moved.end(), c) == moved.end()) balls.push_back(spotted(c));
    }
    return balls;
}

BallOnTable red_at(Vec2 p, int index = 0) { return ball(Ball::red, p.x, p.y, index); }

// Where the referee puts one colour; kNowhere when the oracle gives nothing back.
Vec2 spot_one(const std::vector<BallOnTable>& on_table, Ball colour) {
    const std::vector<BallOnTable> placed = rig().oracle->spot(on_table, {colour});
    CHECK(placed.size() == 1);
    if (placed.size() != 1) return kNowhere;
    CHECK(placed[0].ball == colour);
    return placed[0].pos;
}

double clearance(Vec2 p, const std::vector<BallOnTable>& others) {
    double least = 1.0;
    for (const BallOnTable& b : others) least = std::min(least, apart(p, b.pos) - 2.0 * kR);
    return least;
}

// S3.7(g), (i): as near as it can go: clear of every ball by the referee's own measure of
// touching, and within half a millimetre of the nearest.
void check_just_clear(Vec2 p, const std::vector<BallOnTable>& others) {
    const double gap = clearance(p, others);
    CHECK(gap >= rig().ref.touching_gap_m - 1e-12);
    CHECK(gap <= 5.0e-4);
}

int pocket_named(const char* name) {
    const std::vector<phys::Pocket>& pockets = rig().table.pockets;
    for (std::size_t i = 0; i < pockets.size(); ++i) {
        if (pockets[i].name == name) return int(i);
    }
    return -1;
}

// `back` metres short of the top-right pocket's drop centre on the table's diagonal: a ball there
// and the cue ball further back are in line with the pocket.
Vec2 on_diagonal(double back) {
    const Vec2 c = rig().table.pockets[std::size_t(pocket_named("top_right"))].drop_centre;
    const double k = back / std::sqrt(2.0);
    return {c.x - k, c.y + k};
}

BallOnTable on_diagonal(Ball b, double back, int index = 0) {
    const Vec2 p = on_diagonal(back);
    return ball(b, p.x, p.y, index);
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

// The cue ball hit `object` first and sent it into the top-right pocket.
void require_potted_first(const phys::ShotRecord& r, int cue, int object) {
    const std::vector<Contact> hits = cue_contacts(r, cue);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].ball == object);
    REQUIRE(pocket_of(r, object) == pocket_named("top_right"));
}

phys::CueStrike follow_through(double aim, double speed) {
    phys::CueStrike c = strike(aim, speed);
    c.tip_y = 0.5;
    return c;
}

int foul_value(const Judgement& j, FoulKind kind) {
    for (const Foul& f : j.fouls) {
        if (f.kind == kind) return f.value;
    }
    return 0;
}

// The stroke spotted exactly this colour, here.
void check_spotted(const Judgement& j, Ball colour, Vec2 expected) {
    CHECK(j.spotted.size() == 1);
    if (j.spotted.size() == 1) {
        CHECK(j.spotted[0].ball == colour);
        CHECK(at(j.spotted[0].pos, expected));
    }
    CHECK(at(where(j.post, colour), expected));
}

}  // namespace

TEST_CASE("S3.7(j): each colour is spotted on its own spot when that is free") {
    const std::vector<BallOnTable> on_table = {ball(Ball::cue, -0.4, 0.35)};
    const std::vector<BallOnTable> placed = rig().oracle->spot(on_table, kColours);
    CHECK(placed.size() == 6);
    if (placed.size() != 6) return;
    // Section 1's measurements: the baulk line 737 mm from the baulk cushion, the D 292 mm in
    // radius, the pink midway between the centre spot and the top cushion, the black 324 mm from it.
    const Vec2 expected[6] = {{-1.0475, -0.292}, {-1.0475, 0.292}, {-1.0475, 0.0},
                              {0.0, 0.0},        {0.89225, 0.0},   {1.4605, 0.0}};
    for (std::size_t i = 0; i < 6; ++i) {
        CAPTURE(i);
        CHECK(placed[i].ball == kColours[i]);
        CHECK(at(placed[i].pos, expected[i]));
    }
}

TEST_CASE("S3.7(e): a colour whose spot is taken goes on the highest-value spot that is free") {
    SUBCASE("the black, with the blue spot the highest free") {
        std::vector<BallOnTable> on_table = colours_on_spots_except({Ball::black, Ball::blue});
        on_table.push_back(red_at(table::spot(Ball::black)));
        on_table.push_back(ball(Ball::blue, -0.5, 0.5));
        CHECK(at(spot_one(on_table, Ball::black), table::spot(Ball::blue)));
    }
    SUBCASE("the black, with only the yellow spot free") {
        std::vector<BallOnTable> on_table = colours_on_spots_except({Ball::black, Ball::yellow});
        on_table.push_back(red_at(table::spot(Ball::black)));
        on_table.push_back(ball(Ball::yellow, -0.5, 0.5));
        CHECK(at(spot_one(on_table, Ball::black), table::spot(Ball::yellow)));
    }
    SUBCASE("the yellow goes the length of the table to a free black spot") {
        std::vector<BallOnTable> on_table = colours_on_spots_except({Ball::yellow, Ball::black});
        on_table.push_back(red_at(table::spot(Ball::yellow)));
        on_table.push_back(ball(Ball::black, 0.5, 0.5));
        CHECK(at(spot_one(on_table, Ball::yellow), table::spot(Ball::black)));
    }
    SUBCASE("the brown takes the pink spot, not the nearer blue spot") {
        std::vector<BallOnTable> on_table = colours_on_spots_except({Ball::brown, Ball::blue, Ball::pink});
        on_table.push_back(red_at(table::spot(Ball::brown)));
        on_table.push_back(ball(Ball::blue, -0.5, 0.5));
        on_table.push_back(ball(Ball::pink, 0.5, 0.5));
        CHECK(at(spot_one(on_table, Ball::brown), table::spot(Ball::pink)));
    }
}

TEST_CASE("S2.18, S3.7(e), (i): a spot is taken when a ball placed on it would touch another") {
    const Vec2 pink = table::spot(Ball::pink);
    std::vector<BallOnTable> on_table = colours_on_spots_except({Ball::pink, Ball::blue});
    on_table.push_back(ball(Ball::blue, -0.5, 0.5));

    SUBCASE("a red overlapping the spot without covering it") {
        on_table.push_back(red_at({pink.x + 0.03, pink.y}));
        CHECK(at(spot_one(on_table, Ball::pink), table::spot(Ball::blue)));
    }
    SUBCASE("a red exactly a ball's width away: the pink would touch it") {
        on_table.push_back(red_at({pink.x, pink.y + 2.0 * kR}));
        CHECK(at(spot_one(on_table, Ball::pink), table::spot(Ball::blue)));
    }
    SUBCASE("a red a millimetre further off leaves the spot free") {
        on_table.push_back(red_at({pink.x, pink.y + 2.0 * kR + 0.001}));
        CHECK(at(spot_one(on_table, Ball::pink), pink));
    }
}

TEST_CASE("S3.7(f): two colours whose spots are both taken: the higher value is spotted first") {
    // Reds on the black and pink spots; the blue and brown spots are free.
    std::vector<BallOnTable> on_table = colours_on_spots_except({Ball::black, Ball::pink, Ball::blue, Ball::brown});
    on_table.push_back(red_at(table::spot(Ball::black), 0));
    on_table.push_back(red_at(table::spot(Ball::pink), 1));
    on_table.push_back(ball(Ball::blue, -0.5, 0.5));
    on_table.push_back(ball(Ball::brown, -0.5, -0.5));

    // Whichever way round they are asked for, the black gets the better spot.
    for (const std::vector<Ball>& asked : {std::vector<Ball>{Ball::pink, Ball::black},
                                           std::vector<Ball>{Ball::black, Ball::pink}}) {
        const std::vector<BallOnTable> placed = rig().oracle->spot(on_table, asked);
        CHECK(placed.size() == 2);
        if (placed.size() != 2) continue;
        for (const BallOnTable& b : placed) {
            CHECK((b.ball == Ball::black || b.ball == Ball::pink));
            CHECK(at(b.pos, table::spot(b.ball == Ball::black ? Ball::blue : Ball::brown)));
        }
        CHECK(placed[0].ball == asked[0]);
        CHECK(placed[1].ball == asked[1]);
    }
}

TEST_CASE("S3.7(e), (f), (j): a colour whose own spot is free goes on it, whatever else is being spotted") {
    // Only the black's spot is taken. The pink's is its own (j); (e) and (f) move a colour only
    // when its own spot is occupied, so the black may not take the pink spot from the pink.
    std::vector<BallOnTable> on_table = colours_on_spots_except({Ball::black, Ball::pink, Ball::blue});
    on_table.push_back(red_at(table::spot(Ball::black)));
    on_table.push_back(ball(Ball::blue, -0.5, 0.5));

    const std::vector<BallOnTable> placed = rig().oracle->spot(on_table, {Ball::pink, Ball::black});
    CHECK(placed.size() == 2);
    if (placed.size() != 2) return;
    CHECK(at(placed[0].pos, table::spot(Ball::pink)));
    CHECK(at(placed[1].pos, table::spot(Ball::blue)));
}

TEST_CASE("S3.7(g): every spot taken: as near its own spot as it will go, straight towards the top cushion") {
    for (Ball colour : kColours) {
        const std::string name(table::ball_name(colour));
        CAPTURE(name);
        const Vec2 own = table::spot(colour);
        std::vector<BallOnTable> on_table = colours_on_spots_except({colour});
        on_table.push_back(red_at(own));
        const Vec2 p = spot_one(on_table, colour);
        // On the line from the spot to the nearest part of the top cushion: the same distance
        // across the table, further up it.
        CHECK(std::abs(p.y - own.y) < 1e-9);
        CHECK(p.x > own.x);
        CHECK(p.x - own.x == doctest::Approx(2.0 * kR).epsilon(0.01));
        check_just_clear(p, on_table);
    }
}

TEST_CASE("S3.7(g): the nearest place is measured from the balls in the way, not from the spot") {
    const Vec2 blue = table::spot(Ball::blue);
    std::vector<BallOnTable> on_table = colours_on_spots_except({Ball::blue});

    SUBCASE("a red beside the spot") {
        on_table.push_back(red_at({blue.x + 0.01, blue.y + 0.02}));
        const Vec2 p = spot_one(on_table, Ball::blue);
        CHECK(std::abs(p.y - blue.y) < 1e-9);
        // Where a ball on the line just clears that red: 0.01 + sqrt(0.0525^2 - 0.02^2).
        CHECK(p.x - blue.x == doctest::Approx(0.058541).epsilon(0.01));
        check_just_clear(p, on_table);
    }
    SUBCASE("a second red behind the first with no room between them") {
        on_table.push_back(red_at(blue, 0));
        on_table.push_back(red_at({blue.x + 0.06, blue.y}, 1));
        const Vec2 p = spot_one(on_table, Ball::blue);
        CHECK(std::abs(p.y - blue.y) < 1e-9);
        CHECK(p.x - blue.x == doctest::Approx(0.06 + 2.0 * kR).epsilon(0.01));
        check_just_clear(p, on_table);
    }
}

TEST_CASE("S3.7(i): a gap a ball only just fits is no place to spot a colour") {
    // Two reds on the line with 2 micrometres more than a ball between them: a colour put there
    // would be touching both, by the referee's measure.
    const Vec2 blue = table::spot(Ball::blue);
    const double second = 4.0 * kR + 2.0e-6;
    REQUIRE(second - 4.0 * kR < rig().ref.touching_gap_m);
    std::vector<BallOnTable> on_table = colours_on_spots_except({Ball::blue});
    on_table.push_back(red_at(blue, 0));
    on_table.push_back(red_at({blue.x + second, blue.y}, 1));

    const Vec2 p = spot_one(on_table, Ball::blue);
    CHECK(std::abs(p.y - blue.y) < 1e-9);
    CHECK(p.x - blue.x == doctest::Approx(second + 2.0 * kR).epsilon(0.01));
    check_just_clear(p, on_table);
}

TEST_CASE("S3.7(h): the black, with no room above its spot, goes below it on the centre line") {
    // Every spot taken and reds from the black spot to the top cushion, too close for a ball
    // between any two or behind the last.
    const Vec2 black = table::spot(Ball::black);
    std::vector<BallOnTable> on_table = colours_on_spots_except({Ball::black});
    int index = 0;
    for (double up : {0.0, 0.058, 0.116, 0.174, 0.232, 0.290}) on_table.push_back(red_at({black.x + up, 0.0}, index++));
    REQUIRE(black.x + 0.290 + 2.0 * kR > table::top_cushion_x() - kR);

    const Vec2 p = spot_one(on_table, Ball::black);
    CHECK(std::abs(p.y) < 1e-9);
    CHECK(black.x - p.x == doctest::Approx(2.0 * kR).epsilon(0.01));
    check_just_clear(p, on_table);
}

TEST_CASE("S3.7(h): the pink, with no room above its spot, goes below it on the centre line") {
    // Every spot taken, the black on its own, and reds along the centre line from the pink spot
    // to the top cushion.
    const Vec2 pink = table::spot(Ball::pink);
    const Vec2 black = table::spot(Ball::black);
    std::vector<BallOnTable> on_table = colours_on_spots_except({Ball::pink});
    int index = 0;
    for (double up : {0.0, 0.1, 0.2, 0.3, 0.4, 0.5}) on_table.push_back(red_at({pink.x + up, 0.0}, index++));
    for (double up : {0.1, 0.2}) on_table.push_back(red_at({black.x + up, 0.0}, index++));
    const double last = pink.x + 0.85;
    on_table.push_back(red_at({last, 0.0}, index++));
    REQUIRE(last <= table::top_cushion_x() - kR);
    REQUIRE(last + 2.0 * kR > table::top_cushion_x() - kR);
    // No two neighbours along the line leave room for a ball, and none overlap.
    std::vector<double> xs;
    for (const BallOnTable& b : on_table) {
        if (std::abs(b.pos.y) < 1e-12 && b.pos.x >= pink.x) xs.push_back(b.pos.x);
    }
    std::sort(xs.begin(), xs.end());
    REQUIRE(xs.size() == 10);
    for (std::size_t i = 1; i < xs.size(); ++i) {
        REQUIRE(xs[i] - xs[i - 1] > 2.0 * kR);
        REQUIRE(xs[i] - xs[i - 1] < 4.0 * kR);
    }

    const Vec2 p = spot_one(on_table, Ball::pink);
    CHECK(std::abs(p.y) < 1e-9);
    CHECK(pink.x - p.x == doctest::Approx(2.0 * kR).epsilon(0.01));
    check_just_clear(p, on_table);
}

TEST_CASE("S3.7: a colour potted after a red is spotted before the next stroke") {
    FrameState s = frame({on_diagonal(Ball::cue, 0.6), on_diagonal(Ball::black, 0.3), ball(Ball::red, 0.5, 0.4, 0),
                          spotted(Ball::yellow), spotted(Ball::green), spotted(Ball::brown), spotted(Ball::blue),
                          spotted(Ball::pink)});
    s.on = {OnKind::any_colour, Ball::yellow};
    const Played p = play(s, strike(aim_at_ball(s, 1), 1.5), nominate(Ball::black));
    require_potted_first(p.record, s.cue_ball, 1);
    REQUIRE(potted_count(p.record) == 1);

    CHECK(p.judgement.fouls.empty());
    CHECK(p.judgement.points == 7);
    CHECK(p.judgement.post.scores[0] == 7);
    check_spotted(p.judgement, Ball::black, table::spot(Ball::black));
    CHECK(p.judgement.post.striker == 0);
    CHECK(p.judgement.post.on.kind == OnKind::red);
}

TEST_CASE("S3.7(e): the black potted while a red lies on its spot comes back on the blue spot") {
    const Vec2 black_spot = table::spot(Ball::black);
    FrameState s = frame({on_diagonal(Ball::cue, 0.6), on_diagonal(Ball::black, 0.3),
                          ball(Ball::red, black_spot.x, black_spot.y, 0), ball(Ball::blue, -0.5, 0.5),
                          spotted(Ball::yellow), spotted(Ball::green), spotted(Ball::brown), spotted(Ball::pink)});
    s.on = {OnKind::any_colour, Ball::yellow};
    const Played p = play(s, strike(aim_at_ball(s, 1), 1.5), nominate(Ball::black));
    require_potted_first(p.record, s.cue_ball, 1);
    REQUIRE(potted_count(p.record) == 1);
    // Nothing disturbed the red on the black spot.
    REQUIRE(apart({p.record.final_state.balls[2].r.x, p.record.final_state.balls[2].r.y}, black_spot) < 1e-12);

    CHECK(p.judgement.points == 7);
    check_spotted(p.judgement, Ball::black, table::spot(Ball::blue));
}

TEST_CASE("S3.7: a colour pocketed in a foul stroke is spotted, also once the reds are gone") {
    const FrameState s = frame({on_diagonal(Ball::cue, 0.6), on_diagonal(Ball::yellow, 0.3), spotted(Ball::green),
                                spotted(Ball::brown), spotted(Ball::blue), spotted(Ball::pink), spotted(Ball::black)});
    REQUIRE(s.on.kind == OnKind::colour);
    REQUIRE(s.on.colour == Ball::yellow);
    const Played p = play(s, follow_through(aim_at_ball(s, 1), 1.5));
    require_potted_first(p.record, s.cue_ball, 1);
    REQUIRE(pocket_of(p.record, s.cue_ball) >= 0);
    REQUIRE(potted_count(p.record) == 2);

    CHECK(foul_value(p.judgement, FoulKind::in_off) == 4);
    CHECK(p.judgement.penalty == 4);
    CHECK(p.judgement.points == 0);
    CHECK(p.judgement.post.scores[0] == 0);
    CHECK(p.judgement.post.scores[1] == 4);
    check_spotted(p.judgement, Ball::yellow, table::spot(Ball::yellow));
    CHECK(p.judgement.post.on.kind == OnKind::colour);
    CHECK(p.judgement.post.on.colour == Ball::yellow);
}

TEST_CASE("S3.7: a colour potted in its turn once the reds are gone stays off the table") {
    const FrameState s = frame({on_diagonal(Ball::cue, 0.6), on_diagonal(Ball::yellow, 0.3), spotted(Ball::green),
                                spotted(Ball::brown), spotted(Ball::blue), spotted(Ball::pink), spotted(Ball::black)});
    const Played p = play(s, strike(aim_at_ball(s, 1), 1.5));
    require_potted_first(p.record, s.cue_ball, 1);
    REQUIRE(potted_count(p.record) == 1);

    CHECK(p.judgement.fouls.empty());
    CHECK(p.judgement.points == 2);
    CHECK(p.judgement.spotted.empty());
    CHECK(place_of(p.judgement.post, Ball::yellow) == -1);
    CHECK(p.judgement.post.striker == 0);
    CHECK(p.judgement.post.on.kind == OnKind::colour);
    CHECK(p.judgement.post.on.colour == Ball::green);
}

TEST_CASE("S3.7(f): the black and the pink pocketed in one foul stroke, their spots taken: black first") {
    // Reds lie on the black and pink spots; the blue and the brown are away from theirs.
    const Vec2 black_spot = table::spot(Ball::black);
    const Vec2 pink_spot = table::spot(Ball::pink);
    const FrameState s = frame({ball(Ball::cue, -0.4, 0.35), ball(Ball::red, 0.3, 0.5, 0), red_at(black_spot, 1),
                                red_at(pink_spot, 2), ball(Ball::pink, 1.5, -0.6), ball(Ball::black, 1.6, -0.7),
                                ball(Ball::blue, -0.5, 0.6), ball(Ball::brown, -0.5, -0.6), spotted(Ball::yellow),
                                spotted(Ball::green)});
    ShotFacts facts = quiet_facts(s);
    facts.first_hit = {1};
    facts.first_hit_t = 0.3;
    facts.closest_gap_m[1] = 0.0;
    facts.pots.push_back({4, 1, 1.0});
    facts.pots.push_back({5, 1, 1.2});
    facts.cue_travel_m = 1.5;
    facts.object_travel_m = 2.0;
    const Judgement j = judge(s, {}, facts, *rig().oracle, rig().ref);

    // S3.11(b)(iii), S3.10(g): the black is the dearer of the two.
    CHECK(foul_value(j, FoulKind::pot_not_on) == 7);
    CHECK(j.penalty == 7);
    CHECK(j.points == 0);
    CHECK(j.spotted.size() == 2);
    CHECK(at(where(j.post, Ball::black), table::spot(Ball::blue)));
    CHECK(at(where(j.post, Ball::pink), table::spot(Ball::brown)));
}

TEST_CASE("S3.9: a ball at rest on the brink of a pocket stays there") {
    // The clause is about a ball that drops with nothing touching it. Here a red hangs half a
    // millimetre outside the top-left pocket's drop while a stroke is played elsewhere.
    const phys::Pocket& pocket = rig().table.pockets[std::size_t(pocket_named("top_left"))];
    const double k = (pocket.drop_radius + 0.0005) / std::sqrt(2.0);
    const Vec2 brink{pocket.drop_centre.x - k, pocket.drop_centre.y - k};
    const FrameState s = frame({ball(Ball::cue, 0.2, 0.3), ball(Ball::red, 0.5, 0.3, 0),
                                ball(Ball::red, brink.x, brink.y, 1), spotted(Ball::yellow), spotted(Ball::green),
                                spotted(Ball::brown), spotted(Ball::blue), spotted(Ball::pink), spotted(Ball::black)});
    const Played p = play(s, strike(0.0, 0.4));
    const std::vector<Contact> hits = cue_contacts(p.record, s.cue_ball);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].ball == 1);
    REQUIRE(p.record.anomalies.empty());
    REQUIRE(potted_count(p.record) == 0);
    for (const phys::ShotEvent& e : p.record.events) REQUIRE((e.a != 2 && e.b != 2));
    REQUIRE(apart({p.record.final_state.balls[2].r.x, p.record.final_state.balls[2].r.y}, brink) < 1e-12);

    CHECK(p.judgement.fouls.empty());
    CHECK(p.judgement.points == 0);
    CHECK(at(where(p.judgement.post, Ball::red, 1), brink));
    CHECK(p.judgement.post.striker == 1);
}

TEST_CASE("S3.9(c): a red that reaches the pocket at a crawl and drops has been potted") {
    const FrameState s = frame({on_diagonal(Ball::cue, 0.35), on_diagonal(Ball::red, 0.15, 0),
                                ball(Ball::red, 0.5, 0.4, 1), spotted(Ball::yellow), spotted(Ball::green),
                                spotted(Ball::brown), spotted(Ball::blue), spotted(Ball::pink), spotted(Ball::black)});
    const Played p = play(s, strike(aim_at_ball(s, 1), 0.3));
    require_potted_first(p.record, s.cue_ball, 1);
    REQUIRE(potted_count(p.record) == 1);
    double drop_speed = -1.0;
    for (std::size_t i = 0; i < p.record.events.size(); ++i) {
        const phys::ShotEvent& e = p.record.events[i];
        if (e.kind != phys::EventKind::pocket || e.a != 1) continue;
        drop_speed = phys::event_impact_speed(p.record, i, rig().table);
    }
    REQUIRE(drop_speed > 0.0);
    REQUIRE(drop_speed < 0.25);

    CHECK(p.judgement.fouls.empty());
    CHECK(p.judgement.points == 1);
    CHECK(place_of(p.judgement.post, Ball::red, 0) == -1);
    CHECK(p.judgement.post.striker == 0);
    CHECK(p.judgement.post.on.kind == OnKind::any_colour);
}

namespace {

// After a red, the black declared, hit first and potted; the cue ball comes to rest in play at
// `cue_rest`, everything else where it lay.
Judgement black_potted_cue_resting_at(const FrameState& s, Vec2 cue_rest) {
    const int black = place_of(s, Ball::black);
    ShotFacts f = quiet_facts(s);
    f.first_hit = {black};
    f.first_hit_t = 0.4;
    f.closest_gap_m[std::size_t(black)] = 0.0;
    f.pots.push_back({black, pocket_named("top_right"), 1.2});
    f.rest[std::size_t(s.cue_ball)] = cue_rest;
    f.cue_travel_m = 1.0;
    f.object_travel_m = 0.5;
    return judge(s, nominate(Ball::black), f, *rig().oracle, rig().ref);
}

}  // namespace

TEST_CASE("S2.18, S3.7(e): the cue ball at rest on the black spot occupies it; the black goes on the pink spot") {
    // The pink lies away from its spot, so the highest-value spot that is free is the pink's.
    FrameState s = frame({ball(Ball::cue, 0.8, 0.3), ball(Ball::black, 1.3, 0.4), ball(Ball::red, -0.3, 0.6, 0),
                          ball(Ball::pink, 0.6, -0.5), spotted(Ball::yellow), spotted(Ball::green),
                          spotted(Ball::brown), spotted(Ball::blue)});
    s.on = {OnKind::any_colour, Ball::yellow};
    const Judgement j = black_potted_cue_resting_at(s, table::spot(Ball::black));
    CHECK(j.fouls.empty());
    CHECK(j.points == 7);
    CHECK_FALSE(j.post.cue_in_hand);
    CHECK(at(where(j.post, Ball::cue), table::spot(Ball::black)));
    check_spotted(j, Ball::black, table::spot(Ball::pink));
}

TEST_CASE("S2.18, S3.7(g): the cue ball at rest on the black spot, every spot taken: the black goes just behind it") {
    FrameState s = frame({ball(Ball::cue, 0.8, 0.3), ball(Ball::black, 1.3, 0.4), ball(Ball::red, -0.3, 0.6, 0),
                          spotted(Ball::yellow), spotted(Ball::green), spotted(Ball::brown), spotted(Ball::blue),
                          spotted(Ball::pink)});
    s.on = {OnKind::any_colour, Ball::yellow};
    const Judgement j = black_potted_cue_resting_at(s, table::spot(Ball::black));
    CHECK(j.fouls.empty());
    CHECK(j.points == 7);
    CHECK(at(where(j.post, Ball::cue), table::spot(Ball::black)));
    // On the line from its spot to the top cushion, as near the spot as the cue ball allows.
    const Vec2 black = where(j.post, Ball::black);
    CHECK(std::abs(black.y - table::spot(Ball::black).y) < 1e-9);
    CHECK(black.x > table::spot(Ball::black).x);
    std::vector<BallOnTable> others;
    for (const BallOnTable& b : j.post.balls) {
        if (b.ball != Ball::black) others.push_back(b);
    }
    check_just_clear(black, others);
    REQUIRE(j.spotted.size() == 1);
    CHECK(j.spotted[0].ball == Ball::black);
}
