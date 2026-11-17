// The pots a position offers, by geometry alone (spec 10.2 step 1, docs/opponent/README.md "A pot's
// geometry", pots.hpp). Positions are the named fixtures or built here; nothing is simulated.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ofs/opponent/pots.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::Vec2;
using rules::scenario::ball;
using rules::scenario::place_of;

namespace {

constexpr double R = table::kBallRadius;
constexpr double kWideCut = 1.35;

int pocket_index(const char* name) {
    const auto& pockets = rules::scenario::rig().table.pockets;
    for (std::size_t i = 0; i < pockets.size(); ++i) {
        if (pockets[i].name == name) return int(i);
    }
    return -1;
}

const Pot* find_pot(const std::vector<Pot>& pots, int target, int pocket) {
    for (const Pot& p : pots) {
        if (p.target == target && p.pocket == pocket) return &p;
    }
    return nullptr;
}

Vec2 sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 scale(Vec2 a, double k) { return {a.x * k, a.y * k}; }
double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
double len(Vec2 a) { return std::hypot(a.x, a.y); }
Vec2 unit(Vec2 a) { return scale(a, 1.0 / len(a)); }
Vec2 perp(Vec2 a) { return {-a.y, a.x}; }

Vec2 cue_pos(const rules::Position& p) { return p.balls[std::size_t(p.cue_ball)].pos; }

bool same_pot(const Pot& a, const Pot& b) {
    return a.target == b.target && a.pocket == b.pocket && a.cue.x == b.cue.x && a.cue.y == b.cue.y &&
           a.aim_point.x == b.aim_point.x && a.aim_point.y == b.aim_point.y && a.ghost.x == b.ghost.x &&
           a.ghost.y == b.ghost.y && a.aim_rad == b.aim_rad && a.cut_rad == b.cut_rad &&
           a.cue_travel_m == b.cue_travel_m && a.object_travel_m == b.object_travel_m &&
           a.margin_rad == b.margin_rad && a.awkward == b.awkward && a.value == b.value &&
           a.input.has_nomination == b.input.has_nomination && a.input.nominated == b.input.nominated;
}

// A cue ball, one red and nothing else.
rules::FrameState lone_red(Vec2 cue, Vec2 red) {
    return rules::scenario::frame({ball(Ball::cue, cue.x, cue.y), ball(Ball::red, red.x, red.y)});
}

}  // namespace

TEST_CASE("README, a pot's geometry: the straight red's pot into the top-left corner") {
    const rules::FrameState s = rig::position("straight_red");
    const int red = place_of(s, Ball::red);
    const Vec2 o = s.balls[std::size_t(red)].pos;
    const Vec2 c = cue_pos(s);
    const std::vector<Pot> pots = pots_from(s, c, rig::env(), kWideCut);
    const Pot* pot = find_pot(pots, red, pocket_index("top_left"));
    REQUIRE(pot != nullptr);

    CHECK(pot->cut_rad < 0.03);
    CHECK(pot->margin_rad > 0.0);
    CHECK(pot->cue.x == c.x);
    CHECK(pot->cue.y == c.y);
    // The ghost is 2R from the red, on the far side from the aim point, all three in line.
    CHECK(std::abs(len(sub(pot->ghost, o)) - 2.0 * R) < 1e-9);
    const Vec2 to_aim = sub(pot->aim_point, o);
    const Vec2 from_ghost = sub(o, pot->ghost);
    CHECK(std::abs(cross(unit(from_ghost), unit(to_aim))) < 1e-9);
    CHECK(dot(from_ghost, to_aim) > 0.0);
    const Vec2 g = sub(pot->ghost, c);
    CHECK(std::abs(pot->aim_rad - std::atan2(g.y, g.x)) < 1e-9);
    CHECK(std::abs(pot->cue_travel_m - len(g)) < 1e-9);
    CHECK(std::abs(pot->object_travel_m - len(to_aim)) < 1e-9);
}

TEST_CASE("README, a pot's geometry: the aim point lies in the mouth of its own pocket (within 0.2 m of its drop centre)") {
    const rules::FrameState s = rig::position("straight_red");
    const std::vector<Pot> pots = pots_from(s, cue_pos(s), rig::env(), kWideCut);
    REQUIRE_FALSE(pots.empty());
    const auto& pockets = rules::scenario::rig().table.pockets;
    for (const Pot& pot : pots) {
        REQUIRE(pot.pocket >= 0);
        REQUIRE(pot.pocket < int(pockets.size()));
        double nearest = 1e9;
        int nearest_pocket = -1;
        for (std::size_t i = 0; i < pockets.size(); ++i) {
            const double d = len(sub(pot.aim_point, pockets[i].drop_centre));
            if (d < nearest) {
                nearest = d;
                nearest_pocket = int(i);
            }
        }
        CHECK(nearest_pocket == pot.pocket);
        CHECK(nearest < 0.2);
    }
}

TEST_CASE("README, a pot's geometry: the cut red into the top-left corner is cut 0.85 to 1.05 rad, by the angle between G - C and A - O") {
    const rules::FrameState s = rig::position("cut_red");
    const int red = place_of(s, Ball::red);
    const Vec2 o = s.balls[std::size_t(red)].pos;
    const Vec2 c = cue_pos(s);
    const std::vector<Pot> pots = pots_from(s, c, rig::env(), kWideCut);
    const Pot* pot = find_pot(pots, red, pocket_index("top_left"));
    REQUIRE(pot != nullptr);
    CHECK(pot->cut_rad > 0.85);
    CHECK(pot->cut_rad < 1.05);
    const Vec2 gc = sub(pot->ghost, c);
    const Vec2 ao = sub(pot->aim_point, o);
    const double angle = std::acos(std::clamp(dot(unit(gc), unit(ao)), -1.0, 1.0));
    CHECK(std::abs(pot->cut_rad - angle) < 1e-9);
    CHECK(std::abs(len(sub(pot->ghost, o)) - 2.0 * R) < 1e-9);
}

TEST_CASE("README, a pot's geometry: a ball on the object ball's line to the pocket takes the pot away; moved clear, it is back") {
    rules::FrameState s = rig::position("straight_red");
    const int red = place_of(s, Ball::red);
    const int top_left = pocket_index("top_left");
    const std::vector<Pot> open = pots_from(s, cue_pos(s), rig::env(), kWideCut);
    const Pot* pot = find_pot(open, red, top_left);
    REQUIRE(pot != nullptr);

    const int blue = place_of(s, Ball::blue);
    const Vec2 o = s.balls[std::size_t(red)].pos;
    s.balls[std::size_t(blue)].pos = scale(add(o, pot->aim_point), 0.5);
    CHECK(find_pot(pots_from(s, cue_pos(s), rig::env(), kWideCut), red, top_left) == nullptr);

    s.balls[std::size_t(blue)].pos = {-0.5, -0.5};
    CHECK(find_pot(pots_from(s, cue_pos(s), rig::env(), kWideCut), red, top_left) != nullptr);
}

TEST_CASE("README, a pot's geometry: a ball on the cue ball's line to the ghost takes the pot away; moved clear, it is back") {
    rules::FrameState s = rig::position("straight_red");
    const int red = place_of(s, Ball::red);
    const int top_left = pocket_index("top_left");
    const std::vector<Pot> open = pots_from(s, cue_pos(s), rig::env(), kWideCut);
    const Pot* pot = find_pot(open, red, top_left);
    REQUIRE(pot != nullptr);

    const int blue = place_of(s, Ball::blue);
    s.balls[std::size_t(blue)].pos = scale(add(cue_pos(s), pot->ghost), 0.5);
    CHECK(find_pot(pots_from(s, cue_pos(s), rig::env(), kWideCut), red, top_left) == nullptr);

    s.balls[std::size_t(blue)].pos = {-0.5, -0.5};
    CHECK(find_pot(pots_from(s, cue_pos(s), rig::env(), kWideCut), red, top_left) != nullptr);
}

TEST_CASE("README, a pot's geometry: a middle pocket is refused to a ball coming at it far off square, offered square on") {
    const int middle_left = pocket_index("middle_left");
    // About 81 degrees off square: the red runs nearly along the cushion towards the middle.
    const Vec2 shallow_red{0.9, 0.75};
    const Vec2 shallow_dir = unit(sub(Vec2{0.0, 0.889}, shallow_red));
    const rules::FrameState shallow = lone_red(sub(shallow_red, scale(shallow_dir, 0.35)), shallow_red);
    CHECK(find_pot(pots_from(shallow, cue_pos(shallow), rig::env(), kWideCut), 1, middle_left) == nullptr);

    const rules::FrameState square = lone_red({0.0, -0.2}, {0.0, 0.5});
    const Pot* pot = find_pot(pots_from(square, cue_pos(square), rig::env(), kWideCut), 1, middle_left);
    REQUIRE(pot != nullptr);
    CHECK(pot->cut_rad < 0.03);
}

TEST_CASE("pots.hpp: snookered offers no pot") {
    const rules::FrameState s = rig::position("snookered");
    CHECK(pots(s, rig::env(), kWideCut).empty());
    CHECK(pots_from(s, cue_pos(s), rig::env(), kWideCut).empty());
}

TEST_CASE("pots.hpp: with the yellow on, only the yellow is listed, undeclared, worth 2") {
    const rules::FrameState s = rig::position("colours");
    const std::vector<Pot> list = pots(s, rig::env(), kWideCut);
    REQUIRE_FALSE(list.empty());
    const int yellow = place_of(s, Ball::yellow);
    for (const Pot& pot : list) {
        CHECK(pot.target == yellow);
        CHECK(pot.value == 2);
        CHECK_FALSE(pot.input.has_nomination);
    }
}

TEST_CASE("pots.hpp: after a red only colours are listed, each declared as itself and worth its own value") {
    const rules::FrameState s = rig::position("after_red");
    const std::vector<Pot> list = pots(s, rig::env(), kWideCut);
    REQUIRE_FALSE(list.empty());
    for (const Pot& pot : list) {
        REQUIRE(pot.target >= 0);
        REQUIRE(pot.target < int(s.balls.size()));
        const Ball b = s.balls[std::size_t(pot.target)].ball;
        CHECK(b != Ball::red);
        CHECK(b != Ball::cue);
        CHECK(pot.input.has_nomination);
        CHECK(pot.input.nominated == b);
        CHECK(pot.value == table::ball_value(b));
    }
}

TEST_CASE("pots.hpp: with red on only reds are listed, worth 1, undeclared") {
    for (const char* name : {"straight_red", "long_red", "cut_red"}) {
        CAPTURE(name);
        const rules::FrameState s = rig::position(name);
        const std::vector<Pot> list = pots(s, rig::env(), kWideCut);
        REQUIRE_FALSE(list.empty());
        for (const Pot& pot : list) {
            REQUIRE(pot.target >= 0);
            REQUIRE(pot.target < int(s.balls.size()));
            CHECK(s.balls[std::size_t(pot.target)].ball == Ball::red);
            CHECK(pot.value == 1);
            CHECK_FALSE(pot.input.has_nomination);
        }
    }
}

TEST_CASE("pots.hpp: with a free ball called, a colour may be potted as the ball on, nominated and worth 1") {
    rules::FrameState s = rules::scenario::frame({ball(Ball::cue, 0.8084, 0.206), ball(Ball::green, 1.3, 0.55),
                                                  ball(Ball::red, -1.2, -0.6)});
    s.free_ball = true;
    const int green = place_of(s, Ball::green);
    const std::vector<Pot> list = pots(s, rig::env(), kWideCut);
    const Pot* pot = find_pot(list, green, pocket_index("top_left"));
    REQUIRE(pot != nullptr);
    CHECK(pot->value == 1);
    CHECK(pot->input.has_nomination);
    CHECK(pot->input.nominated == Ball::green);
}

TEST_CASE("pots.hpp: with the cue ball not in hand, pots is pots_from the cue ball") {
    const rules::FrameState s = rig::position("straight_red");
    const std::vector<Pot> a = pots(s, rig::env(), kWideCut);
    const std::vector<Pot> b = pots_from(s, cue_pos(s), rig::env(), kWideCut);
    REQUIRE_FALSE(a.empty());
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(same_pot(a[i], b[i]));
}

TEST_CASE("pots.hpp: easiest first, by margin then place then pocket, and the same list twice") {
    const rules::FrameState s = exhibition_frame(rig::levels().exhibitions.at(0), 0, 1);
    const std::vector<Pot> list = pots(s, rig::env(), kWideCut);
    REQUIRE(list.size() >= 2);
    for (std::size_t i = 1; i < list.size(); ++i) {
        const Pot& a = list[i - 1];
        const Pot& b = list[i];
        CAPTURE(i);
        const bool ordered = a.margin_rad > b.margin_rad ||
                             (a.margin_rad == b.margin_rad &&
                              std::make_pair(a.target, a.pocket) < std::make_pair(b.target, b.pocket));
        CHECK(ordered);
    }
    const std::vector<Pot> again = pots(s, rig::env(), kWideCut);
    REQUIRE(again.size() == list.size());
    for (std::size_t i = 0; i < list.size(); ++i) CHECK(same_pot(list[i], again[i]));
}

TEST_CASE("pots.hpp: no pot is cut wider than max_cut_rad, and a narrower limit lists exactly the pots within it") {
    const rules::FrameState s = exhibition_frame(rig::levels().exhibitions.at(0), 0, 1);
    const std::vector<Pot> wide = pots(s, rig::env(), 1.4);
    const std::vector<Pot> narrow = pots(s, rig::env(), 0.5);
    REQUIRE_FALSE(wide.empty());
    for (const Pot& p : wide) CHECK(p.cut_rad <= 1.4);
    for (const Pot& p : narrow) {
        CHECK(p.cut_rad <= 0.5);
        bool in_wide = false;
        for (const Pot& q : wide) in_wide = in_wide || same_pot(p, q);
        CHECK(in_wide);
    }
    std::size_t within = 0;
    for (const Pot& q : wide) {
        if (q.cut_rad <= 0.5) ++within;
    }
    CHECK(narrow.size() == within);
}

TEST_CASE("pots.hpp: every listed pot has a margin above 0 and a cut of at least 0") {
    std::size_t listed = 0;
    for (const char* name : {"straight_red", "cut_red", "long_red", "colours", "after_red", "black_only"}) {
        CAPTURE(name);
        const rules::FrameState s = rig::position(name);
        for (const Pot& p : pots(s, rig::env(), kWideCut)) {
            ++listed;
            CHECK(p.margin_rad > 0.0);
            CHECK(p.cut_rad >= 0.0);
        }
    }
    CHECK(listed >= 6);
}

TEST_CASE("README, a pot's geometry: the margin falls as 1 over the cue ball's travel (within 5%), the cut unchanged") {
    const Vec2 red{1.3, 0.55};
    const int top_left = pocket_index("top_left");
    // A first cue position about 0.4 rad off the object ball's line to the corner.
    const Vec2 to_corner = unit(sub(Vec2{1.7845, 0.889}, red));
    const Vec2 rough_ghost = sub(red, scale(to_corner, 2.0 * R));
    const double back = std::atan2(to_corner.y, to_corner.x) - 0.4;
    const Vec2 dir{std::cos(back), std::sin(back)};
    const rules::FrameState first = lone_red(sub(rough_ghost, scale(dir, 0.4)), red);
    const std::vector<Pot> near_list = pots_from(first, cue_pos(first), rig::env(), kWideCut);
    const Pot* near_pot = find_pot(near_list, 1, top_left);
    REQUIRE(near_pot != nullptr);

    const Vec2 along = unit(sub(cue_pos(first), near_pot->ghost));
    for (const double d : {0.8, 1.6}) {
        CAPTURE(d);
        const Vec2 far_cue = add(near_pot->ghost, scale(along, d));
        const rules::FrameState far = lone_red(far_cue, red);
        const std::vector<Pot> far_list = pots_from(far, far_cue, rig::env(), kWideCut);
        const Pot* far_pot = find_pot(far_list, 1, top_left);
        REQUIRE(far_pot != nullptr);
        CHECK(std::abs(far_pot->cut_rad - near_pot->cut_rad) < 1e-9);
        CHECK(far_pot->margin_rad < near_pot->margin_rad);
        const double ratio =
            (far_pot->margin_rad * far_pot->cue_travel_m) / (near_pot->margin_rad * near_pot->cue_travel_m);
        CHECK(ratio == doctest::Approx(1.0).epsilon(0.05));
    }
}

TEST_CASE("README, a pot's geometry: a ball just beside the cue ball's path, clear of it, narrows the margin") {
    const Vec2 cue{0.8084, 0.206};
    const Vec2 red{1.3, 0.55};
    const int top_left = pocket_index("top_left");
    const rules::FrameState open = lone_red(cue, red);
    const std::vector<Pot> open_list = pots_from(open, cue, rig::env(), kWideCut);
    const Pot* pot = find_pot(open_list, 1, top_left);
    REQUIRE(pot != nullptr);

    // 0.3 mm of spare room, four fifths of the way to the ghost.
    const Vec2 path = sub(pot->ghost, cue);
    const Vec2 beside = add(add(cue, scale(path, 0.8)), scale(unit(perp(path)), 2.0 * R + 0.0003));
    rules::FrameState tight = open;
    tight.balls.push_back(ball(Ball::blue, beside.x, beside.y));
    const std::vector<Pot> tight_list = pots_from(tight, cue, rig::env(), kWideCut);
    const Pot* narrowed = find_pot(tight_list, 1, top_left);
    REQUIRE(narrowed != nullptr);
    CHECK(narrowed->margin_rad > 0.0);
    CHECK(narrowed->margin_rad < pot->margin_rad);
}

TEST_CASE("README, a pot's geometry: a ball just beside the object ball's path, clear of it, narrows the margin") {
    const Vec2 cue{0.8084, 0.206};
    const Vec2 red{1.3, 0.55};
    const int top_left = pocket_index("top_left");
    const rules::FrameState open = lone_red(cue, red);
    const std::vector<Pot> open_list = pots_from(open, cue, rig::env(), kWideCut);
    const Pot* pot = find_pot(open_list, 1, top_left);
    REQUIRE(pot != nullptr);

    // 2 mm of spare room, halfway from the red to the aim point.
    const Vec2 path = sub(pot->aim_point, red);
    const Vec2 beside = add(add(red, scale(path, 0.5)), scale(unit(perp(path)), -(2.0 * R + 0.002)));
    rules::FrameState tight = open;
    tight.balls.push_back(ball(Ball::blue, beside.x, beside.y));
    const std::vector<Pot> tight_list = pots_from(tight, cue, rig::env(), kWideCut);
    const Pot* narrowed = find_pot(tight_list, 1, top_left);
    REQUIRE(narrowed != nullptr);
    CHECK(narrowed->margin_rad > 0.0);
    CHECK(narrowed->margin_rad < pot->margin_rad);
}

TEST_CASE("README, band: under 1.0 m short, from 2.2 m long, medium between") {
    const auto band_of = [](double cue_m, double object_m) {
        Pot p;
        p.cue_travel_m = cue_m;
        p.object_travel_m = object_m;
        return std::string(band(p));
    };
    CHECK(band_of(0.5, 0.49) == "short");
    CHECK(band_of(0.99, 0.0) == "short");
    CHECK(band_of(0.5, 0.5) == "medium");
    CHECK(band_of(1.0, 0.0) == "medium");
    CHECK(band_of(2.19, 0.0) == "medium");
    CHECK(band_of(1.0, 1.19) == "medium");
    CHECK(band_of(2.2, 0.0) == "long");
    CHECK(band_of(1.5, 1.5) == "long");
}

TEST_CASE("pots.hpp: the fixtures fall in the bands their names say") {
    const auto band_for = [](const char* name) {
        const rules::FrameState s = rig::position(name);
        const std::vector<Pot> list = pots_from(s, cue_pos(s), rig::env(), kWideCut);
        const Pot* pot = find_pot(list, place_of(s, Ball::red), pocket_index("top_left"));
        REQUIRE(pot != nullptr);
        return std::string(band(*pot));
    };
    CHECK(band_for("straight_red") == "medium");
    CHECK(band_for("long_red") == "long");
}

TEST_CASE("pots.hpp: a cue ball 5 cm from a cushion playing straight away from it is awkward; along the cushion or in the open it is not") {
    const int middle_left = pocket_index("middle_left");
    const int top_right = pocket_index("top_right");
    // The right side cushion's nose is at y = -0.889.
    const rules::FrameState away = lone_red({0.0, -0.839}, {0.0, 0.3});
    const std::vector<Pot> away_list = pots_from(away, cue_pos(away), rig::env(), kWideCut);
    const Pot* a = find_pot(away_list, 1, middle_left);
    REQUIRE(a != nullptr);
    CHECK(a->awkward);

    const rules::FrameState along = lone_red({-0.5, -0.839}, {0.8, -0.75});
    const std::vector<Pot> along_list = pots_from(along, cue_pos(along), rig::env(), kWideCut);
    const Pot* b = find_pot(along_list, 1, top_right);
    REQUIRE(b != nullptr);
    CHECK_FALSE(b->awkward);

    const rules::FrameState s = rig::position("straight_red");
    const std::vector<Pot> list = pots_from(s, cue_pos(s), rig::env(), kWideCut);
    const Pot* c = find_pot(list, place_of(s, Ball::red), pocket_index("top_left"));
    REQUIRE(c != nullptr);
    CHECK_FALSE(c->awkward);
}

TEST_CASE("pots.hpp: a ball close behind the cue ball on the line of the cue makes it awkward") {
    const int middle_left = pocket_index("middle_left");
    rules::FrameState s = lone_red({0.0, -0.3}, {0.0, 0.5});
    const std::vector<Pot> open_list = pots_from(s, cue_pos(s), rig::env(), kWideCut);
    const Pot* open = find_pot(open_list, 1, middle_left);
    REQUIRE(open != nullptr);
    CHECK_FALSE(open->awkward);

    // 0.07 m behind, centre to centre: 0.04 m to its face.
    s.balls.push_back(ball(Ball::brown, 0.0, -0.37));
    const std::vector<Pot> list = pots_from(s, cue_pos(s), rig::env(), kWideCut);
    const Pot* blocked = find_pot(list, 1, middle_left);
    REQUIRE(blocked != nullptr);
    CHECK(blocked->awkward);
}

TEST_CASE("pots.hpp: with the cue ball in hand, pots come from several places, each legal and in the D") {
    const rules::FrameState s = rig::position("in_hand");
    REQUIRE(s.cue_in_hand);
    const std::vector<Pot> list = pots(s, rig::env(), kWideCut);
    REQUIRE_FALSE(list.empty());
    std::set<std::pair<double, double>> places;
    for (const Pot& p : list) {
        places.insert({p.cue.x, p.cue.y});
        CHECK(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, p.cue));
        CHECK(table::in_d(p.cue));
        const Vec2 g = sub(p.ghost, p.cue);
        CHECK(std::abs(p.aim_rad - std::atan2(g.y, g.x)) < 1e-9);
    }
    CHECK(places.size() > 1);
}

TEST_CASE("pots.hpp: cue_placements is never empty, keeps to count, every place legal, the listed one first when legal") {
    const rules::FrameState s = rig::position("in_hand");
    const Vec2 listed = cue_pos(s);
    REQUIRE(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, listed));
    for (const int count : {1, 3, kPlacements}) {
        CAPTURE(count);
        const std::vector<Vec2> places = cue_placements(s, rig::env(), count);
        REQUIRE_FALSE(places.empty());
        CHECK(int(places.size()) <= count);
        for (const Vec2 p : places) {
            CHECK(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, p));
            CHECK(table::in_d(p));
        }
        CHECK(std::abs(places[0].x - listed.x) < 1e-12);
        CHECK(std::abs(places[0].y - listed.y) < 1e-12);
    }
    CHECK(cue_placements(s, rig::env(), kPlacements).size() > 1);
}

TEST_CASE("pots.hpp: cue_placements with the listed place illegal still gives legal places only") {
    rules::FrameState s = rig::position("in_hand");
    // On top of the brown.
    s.balls[std::size_t(s.cue_ball)].pos = table::spot(Ball::brown);
    const std::vector<Vec2> places = cue_placements(s, rig::env(), kPlacements);
    REQUIRE_FALSE(places.empty());
    CHECK(int(places.size()) <= kPlacements);
    for (const Vec2 p : places) CHECK(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, p));
}

TEST_CASE("pots.hpp: pockets_open_to: the black on its spot is open to both top corners; a ball boxed in has none") {
    const rules::FrameState s = rig::position("black_only");
    const std::vector<int> open = pockets_open_to(s, place_of(s, Ball::black), rig::env());
    CHECK(std::find(open.begin(), open.end(), pocket_index("top_left")) != open.end());
    CHECK(std::find(open.begin(), open.end(), pocket_index("top_right")) != open.end());
    CHECK(std::is_sorted(open.begin(), open.end()));

    std::vector<rules::BallOnTable> balls{ball(Ball::cue, -1.0, 0.0), ball(Ball::red, 0.5, 0.3)};
    const Ball ring[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};
    for (int k = 0; k < 6; ++k) {
        const double a = k * 3.14159265358979323846 / 3.0;
        const double d = 2.0 * R + 0.001;
        balls.push_back(ball(ring[k], 0.5 + d * std::cos(a), 0.3 + d * std::sin(a)));
    }
    const rules::FrameState boxed = rules::scenario::frame(balls);
    CHECK(pockets_open_to(boxed, 1, rig::env()).empty());
}

TEST_CASE("pots.hpp: open_reds is 15 on the first exhibition table and at most 8 from the break-off rack") {
    const rules::FrameState open = exhibition_frame(rig::levels().exhibitions.at(0), 0, 1);
    CHECK(open_reds(open, rig::env()) == 15);
    const int racked = open_reds(rig::racked(), rig::env());
    CHECK(racked >= 0);
    CHECK(racked <= 8);
}
