#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "ofs/physics/rack.hpp"
#include "ofs/rng.hpp"
#include "rules/spotting.hpp"
#include "scenario.hpp"

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

constexpr double kD = table::kBallDiameter;

const TableOracle& oracle() { return *rig().oracle; }

// The referee's touching gap, whatever referee_params.json makes it.
double gap() { return rig().ref.touching_gap_m; }
// The centre distance of a ball put as near another as S3.7 allows.
double beside() { return kD + gap() + kJustClear; }

double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

void check_at(const BallOnTable& b, Ball colour, Vec2 pos) {
    CHECK(b.ball == colour);
    CHECK(b.pos.x == doctest::Approx(pos.x).epsilon(1e-12));
    CHECK(b.pos.y == doctest::Approx(pos.y).epsilon(1e-12));
}

// S3.7(i): none of `spotted` touches a ball of `on_table` or another of `spotted`.
void check_none_touching(const std::vector<BallOnTable>& on_table, const std::vector<BallOnTable>& placed) {
    for (std::size_t i = 0; i < placed.size(); ++i) {
        for (const BallOnTable& other : on_table) CHECK(dist(placed[i].pos, other.pos) > kD + gap());
        for (std::size_t j = i + 1; j < placed.size(); ++j) CHECK(dist(placed[i].pos, placed[j].pos) > kD + gap());
    }
}

// A red on every spot but those of `free`, numbered from 0.
std::vector<BallOnTable> reds_on_spots(std::vector<Ball> free = {}) {
    std::vector<BallOnTable> out;
    for (Ball c : {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        bool skip = false;
        for (Ball f : free) skip = skip || f == c;
        if (!skip) out.push_back({Ball::red, int(out.size()), table::spot(c)});
    }
    return out;
}

// Reds on the centre line from x = `from`, `step` apart, the last no further than `to`.
void add_row(std::vector<BallOnTable>& balls, double from, double to, double step) {
    for (double x = from; x <= to + 1e-9; x += step) balls.push_back({Ball::red, int(balls.size()), {x, 0.0}});
}

}  // namespace

TEST_CASE("S3.7: a colour goes on its own spot, in the order asked for") {
    const std::vector<BallOnTable> on_table = {ball(Ball::cue, -0.2, 0.4), ball(Ball::red, 0.3, -0.5)};
    const std::vector<Ball> colours = {Ball::brown, Ball::black, Ball::yellow, Ball::pink, Ball::green, Ball::blue};
    const auto placed = oracle().spot(on_table, colours);
    REQUIRE(placed.size() == colours.size());
    for (std::size_t i = 0; i < colours.size(); ++i) {
        check_at(placed[i], colours[i], table::spot(colours[i]));
        CHECK(placed[i].index == 0);
    }
    CHECK(oracle().spot(on_table, {}).empty());
}

TEST_CASE("S2.18: a spot is occupied when a ball on it would touch another") {
    REQUIRE(gap() > 1.0e-9);
    const Vec2 black = table::spot(Ball::black);
    // A red whose surface is twice the touching gap from where the black's would be leaves the
    // spot free.
    auto placed = oracle().spot({ball(Ball::red, black.x + kD + 2.0 * gap(), 0.0)}, {Ball::black});
    REQUIRE(placed.size() == 1);
    check_at(placed[0], Ball::black, black);
    // At half the gap they would be touching.
    placed = oracle().spot({ball(Ball::red, black.x + kD + 0.5 * gap(), 0.0)}, {Ball::black});
    REQUIRE(placed.size() == 1);
    check_at(placed[0], Ball::black, table::spot(Ball::pink));
}

TEST_CASE("S2.18: at the start of a frame the pink spot is free for the pink, the pack untouched") {
    // The apex red is racked a few hundredths of a millimetre clear of the pink (spec 7.7). The
    // referee's touching gap has to be smaller than that, or a pink potted off the break could
    // not go back on its own spot.
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        std::vector<BallOnTable> on_table;
        for (const phys::BallState& b : phys::opening_rack(seed, rig().params).balls) {
            if (b.ball != Ball::pink) on_table.push_back({b.ball, b.index, {b.r.x, b.r.y}});
        }
        CAPTURE(seed);
        const auto placed = oracle().spot(on_table, {Ball::pink});
        REQUIRE(placed.size() == 1);
        check_at(placed[0], Ball::pink, table::spot(Ball::pink));
    }
}

TEST_CASE("S3.7(e): its own spot occupied, a colour goes on the highest-value spot available") {
    const Vec2 black = table::spot(Ball::black);
    std::vector<BallOnTable> on_table = {ball(Ball::red, black.x + 0.01, 0.02)};
    auto placed = oracle().spot(on_table, {Ball::black});
    REQUIRE(placed.size() == 1);
    check_at(placed[0], Ball::black, table::spot(Ball::pink));

    // The pink on its spot as well: the blue spot is the next.
    on_table.push_back(spotted(Ball::pink));
    placed = oracle().spot(on_table, {Ball::black});
    check_at(placed[0], Ball::black, table::spot(Ball::blue));

    // The yellow, its spot taken, goes on the black spot when that is free.
    placed = oracle().spot({ball(Ball::red, table::spot(Ball::yellow).x, table::spot(Ball::yellow).y)},
                           {Ball::yellow});
    check_at(placed[0], Ball::yellow, black);
}

TEST_CASE("S3.7(e), (j): a colour whose own spot is free takes it before a displaced colour is placed") {
    // A red on the black spot. The pink and the brown have their own spots, whatever their
    // value; the black then finds the pink spot taken and goes on the blue spot.
    const std::vector<BallOnTable> on_table = {ball(Ball::red, table::spot(Ball::black).x, 0.0)};
    const auto placed = oracle().spot(on_table, {Ball::brown, Ball::pink, Ball::black});
    REQUIRE(placed.size() == 3);
    check_at(placed[0], Ball::brown, table::spot(Ball::brown));
    check_at(placed[1], Ball::pink, table::spot(Ball::pink));
    check_at(placed[2], Ball::black, table::spot(Ball::blue));
    check_none_touching(on_table, placed);
}

TEST_CASE("S3.7(f): colours whose own spots are occupied are spotted highest value first") {
    // Reds on the black and pink spots. The black has first choice and takes the blue spot, the
    // highest free; the pink takes the next, the brown spot. The green, whose spot is free, is
    // already on it.
    const std::vector<BallOnTable> on_table = {ball(Ball::red, table::spot(Ball::black).x, 0.0, 0),
                                               ball(Ball::red, table::spot(Ball::pink).x, 0.0, 1)};
    auto placed = oracle().spot(on_table, {Ball::pink, Ball::green, Ball::black});
    REQUIRE(placed.size() == 3);
    check_at(placed[0], Ball::pink, table::spot(Ball::brown));
    check_at(placed[1], Ball::green, table::spot(Ball::green));
    check_at(placed[2], Ball::black, table::spot(Ball::blue));
    check_none_touching(on_table, placed);

    // With the brown to be spotted too, its own spot is not there for the pink: the green spot
    // is taken as well, so the pink goes on the yellow spot.
    placed = oracle().spot(on_table, {Ball::pink, Ball::green, Ball::black, Ball::brown});
    REQUIRE(placed.size() == 4);
    check_at(placed[0], Ball::pink, table::spot(Ball::yellow));
    check_at(placed[2], Ball::black, table::spot(Ball::blue));
    check_at(placed[3], Ball::brown, table::spot(Ball::brown));
    check_none_touching(on_table, placed);
}

TEST_CASE("S3.7(g): every spot occupied, a colour goes as near its own as possible towards the top cushion") {
    const auto on_table = reds_on_spots();
    for (Ball colour : {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        CAPTURE(table::ball_value(colour));
        const auto placed = oracle().spot(on_table, {colour});
        REQUIRE(placed.size() == 1);
        // Straight up the table from its spot, just clear of the red on it.
        const Vec2 own = table::spot(colour);
        check_at(placed[0], colour, {own.x + beside(), own.y});
        check_none_touching(on_table, placed);
        CHECK(dist(placed[0].pos, own) < kD + gap() + 1.0e-5);
    }
}

TEST_CASE("S3.7(g): the nearest place towards the top cushion that is clear of every ball") {
    // A second red 80 mm above the black spot and 10 mm off the centre line is in the way of the
    // place beside the first. The black goes just beyond it: with c a diameter and the touching
    // gap, a ball on the centre line stops touching one 10 mm off it sqrt(c^2 - 10^2) along
    // (51.5 mm for c = 52.5 mm), so about 131.5 mm from its spot, and the hair more that every
    // such place gets.
    const Vec2 black = table::spot(Ball::black);
    auto on_table = reds_on_spots();
    on_table.push_back({Ball::red, 6, {black.x + 0.08, 0.01}});
    const auto placed = oracle().spot(on_table, {Ball::black});
    REQUIRE(placed.size() == 1);
    const double along = 0.08 + std::sqrt((kD + gap()) * (kD + gap()) - 0.01 * 0.01) + kJustClear;
    CHECK(along == doctest::Approx(0.1316).epsilon(1e-3));
    check_at(placed[0], Ball::black, {black.x + along, 0.0});
    check_none_touching(on_table, placed);
}

TEST_CASE("S3.7(g): past every ball in the way, while there is room before the top cushion") {
    // Reds on the centre line 99 mm apart from the black spot leave no room between them (a ball
    // needs two diameters, 105 mm) and the last, at 1.6585, leaves room beyond: a ball beside it
    // is at 1.711, short of 1.75825 where a ball touches the cushion.
    auto on_table = reds_on_spots({Ball::black});
    add_row(on_table, table::spot(Ball::black).x, 1.66, 0.099);
    const auto placed = oracle().spot(on_table, {Ball::black});
    check_at(placed[0], Ball::black, {table::spot(Ball::black).x + 2 * 0.099 + beside(), 0.0});
    check_none_touching(on_table, placed);
}

TEST_CASE("S3.7(h): black with no room towards the top cushion goes towards baulk on the centre line") {
    // Reds at the black spot (1.4605), 1.5595, 1.6585 and 1.7575 on the centre line: none of the
    // gaps takes a ball, and beyond the last the cushion (1.75825 for a ball's centre) is 0.75 mm
    // away.
    auto on_table = reds_on_spots({Ball::black});
    add_row(on_table, table::spot(Ball::black).x, 1.758, 0.099);
    REQUIRE(on_table.size() == 9);
    const auto placed = oracle().spot(on_table, {Ball::black});
    REQUIRE(placed.size() == 1);
    check_at(placed[0], Ball::black, {table::spot(Ball::black).x - beside(), 0.0});
    check_none_touching(on_table, placed);
}

TEST_CASE("S3.7(h): pink and black together, the centre line full from the pink spot to the top cushion") {
    // Reds every 96 mm from the pink spot (0.89225) to 1.75625: ten of them, the seventh 7.75 mm
    // above the black spot. The black, looked at first, finds no room above its spot and none
    // going down until it is past the red on the pink spot; the pink then goes beside the black.
    auto on_table = reds_on_spots({Ball::pink, Ball::black});
    const double pink_x = table::spot(Ball::pink).x;
    add_row(on_table, pink_x, 1.758, 0.096);
    REQUIRE(on_table.size() == 14);
    const auto placed = oracle().spot(on_table, {Ball::pink, Ball::black});
    REQUIRE(placed.size() == 2);
    check_at(placed[1], Ball::black, {pink_x - beside(), 0.0});
    check_at(placed[0], Ball::pink, {pink_x - 2.0 * beside(), 0.0});
    check_none_touching(on_table, placed);

    // The pink alone takes the place the black had.
    const auto pink = oracle().spot(on_table, {Ball::pink});
    check_at(pink[0], Ball::pink, {pink_x - beside(), 0.0});
}

TEST_CASE("S3.7: the other colours with no room above their spot go towards baulk too") {
    // The rulebook does not say. It takes more balls than a frame has: 29 on the yellow's line.
    std::vector<BallOnTable> on_table = reds_on_spots();
    const Vec2 yellow = table::spot(Ball::yellow);
    for (double x = yellow.x + 0.099; x <= 1.758; x += 0.099) on_table.push_back({Ball::red, 0, {x, yellow.y}});
    const auto placed = oracle().spot(on_table, {Ball::yellow});
    check_at(placed[0], Ball::yellow, {yellow.x - beside(), yellow.y});
    check_none_touching(on_table, placed);
}

TEST_CASE("S3.7(g), (h): the place found is the first free one along the line, on crowded tables") {
    Rng rng(37);
    const double top = table::top_cushion_x() - table::kBallRadius;
    const double baulk = table::baulk_cushion_x() + table::kBallRadius;
    int up = 0;
    int down = 0;
    for (int table_no = 0; table_no < 300; ++table_no) {
        // A red on every spot and more strewn close to the centre line: nine above the pink spot,
        // or on every third table up to five above the black spot, which often leaves no room
        // there.
        const bool packed = table_no % 3 == 2;
        const Ball colour = packed || table_no % 2 == 0 ? Ball::black : Ball::pink;
        const Vec2 own = table::spot(colour);
        auto on_table = reds_on_spots();
        for (int tries = 0; tries < 400 && on_table.size() < (packed ? 11u : 15u); ++tries) {
            const Vec2 p{rng.uniform(packed ? own.x : table::spot(Ball::pink).x, top),
                         packed ? rng.uniform(-0.04, 0.04) : rng.uniform(-0.06, 0.06)};
            bool room = true;
            for (const BallOnTable& b : on_table) room = room && dist(p, b.pos) > kD + 1.0e-4;
            if (room) on_table.push_back({Ball::red, int(on_table.size()), p});
        }
        const auto placed = oracle().spot(on_table, {colour});
        REQUIRE(placed.size() == 1);
        check_none_touching(on_table, placed);
        const double x_placed = placed[0].pos.x;
        CAPTURE(table_no);
        CHECK(placed[0].pos.y == 0.0);
        CHECK(x_placed <= top);
        CHECK(x_placed >= baulk);

        // Walking the line in 0.02 mm steps: no free place between the spot and where the colour
        // went, more than 0.1 mm short of it, nor anywhere above the spot when it went down.
        const auto free_at = [&](double x) {
            for (const BallOnTable& b : on_table) {
                if (dist({x, 0.0}, b.pos) <= kD + gap()) return false;
            }
            return true;
        };
        const bool went_up = x_placed > own.x;
        (went_up ? up : down) += 1;
        int nearer = 0;
        for (double x = own.x; x <= (went_up ? x_placed - 1.0e-4 : top); x += 2.0e-5) nearer += free_at(x);
        for (double x = own.x; !went_up && x >= x_placed + 1.0e-4; x -= 2.0e-5) nearer += free_at(x);
        CHECK(nearer == 0);
    }
    CHECK(up > 50);
    CHECK(down > 20);
}

TEST_CASE("S3.7: only colours are spotted") {
    const auto placed = oracle().spot({}, {Ball::cue, Ball::blue, Ball::red});
    REQUIRE(placed.size() == 1);
    check_at(placed[0], Ball::blue, table::spot(Ball::blue));
}

TEST_CASE("S3.7: spot_colours takes the touching gap it is given") {
    // With a 1 mm gap a red 0.5 mm clear of the black's place occupies the spot.
    const std::vector<BallOnTable> on_table = {ball(Ball::red, table::spot(Ball::black).x, kD + 5.0e-4)};
    check_at(spot_colours(on_table, {Ball::black}, 1.0e-3)[0], Ball::black, table::spot(Ball::pink));
    check_at(spot_colours(on_table, {Ball::black}, 1.0e-4)[0], Ball::black, table::spot(Ball::black));
}
