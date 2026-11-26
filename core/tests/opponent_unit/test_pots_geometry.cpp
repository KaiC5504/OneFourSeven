#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <string>

#include "ofs/opponent/pots.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::scenario::ball;
using rules::scenario::place_of;

namespace {

constexpr double kR = table::kBallRadius;
constexpr double kWide = 1.3;  // wider than any cut the tests play

int pocket(const char* name) {
    const auto& pockets = rules::scenario::rig().table.pockets;
    for (std::size_t i = 0; i < pockets.size(); ++i) {
        if (pockets[i].name == name) return int(i);
    }
    FAIL("no pocket named " << name);
    return -1;
}

const Pot* find(const std::vector<Pot>& pots, int target, int pocket_index) {
    for (const Pot& p : pots) {
        if (p.target == target && p.pocket == pocket_index) return &p;
    }
    return nullptr;
}

rules::Vec2 cue_of(const rules::Position& p) { return p.balls[std::size_t(p.cue_ball)].pos; }

// Kept to the end of the run: the tests hold pointers into a list past the statement that made it.
const std::vector<Pot>& from_cue(const rules::FrameState& s, double max_cut = kWide) {
    static std::deque<std::vector<Pot>> kept;
    kept.push_back(pots_from(s, cue_of(s), rig::env(), max_cut));
    return kept.back();
}

double dist(rules::Vec2 a, rules::Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

}  // namespace

TEST_CASE("straight_red: a straight pot into the top left, the ghost two radii behind the red") {
    const rules::FrameState s = rig::position("straight_red");
    const int red = place_of(s, Ball::red);
    const std::vector<Pot> list = from_cue(s);
    const Pot* pot = find(list, red, pocket("top_left"));
    REQUIRE(pot != nullptr);
    CHECK(pot->cut_rad < 0.03);
    const rules::Vec2 o = s.balls[std::size_t(red)].pos;
    const double to_aim = dist(pot->aim_point, o);
    CHECK(pot->ghost.x == doctest::Approx(o.x - 2.0 * kR * (pot->aim_point.x - o.x) / to_aim).epsilon(1e-12));
    CHECK(pot->ghost.y == doctest::Approx(o.y - 2.0 * kR * (pot->aim_point.y - o.y) / to_aim).epsilon(1e-12));
    CHECK(pot->aim_rad == doctest::Approx(std::atan2(pot->ghost.y - s.balls[0].pos.y, pot->ghost.x - s.balls[0].pos.x)));
    CHECK(pot->cue_travel_m == doctest::Approx(dist(cue_of(s), pot->ghost)));
    CHECK(pot->object_travel_m == doctest::Approx(to_aim));
    CHECK(pot->value == 1);
    CHECK_FALSE(pot->input.has_nomination);
    CHECK_FALSE(pot->awkward);
    // 1.14 m from cue ball to pocket in all: a medium pot.
    const double length = pot->cue_travel_m + pot->object_travel_m;
    CHECK(std::string(band(*pot)) == (length < kMediumFrom_m ? "short" : "medium"));
    CHECK(list.front().target == red);
    CHECK(list.front().pocket == pocket("top_left"));
    MESSAGE("straight_red: margin " << pot->margin_rad << " cut " << pot->cut_rad << " aim point (" << pot->aim_point.x
                                    << ", " << pot->aim_point.y << ") length " << length);
}

TEST_CASE("cut_red: the same red cut about 55 degrees into the top left") {
    const rules::FrameState s = rig::position("cut_red");
    const int red = place_of(s, Ball::red);
    const Pot* pot = nullptr;
    const std::vector<Pot> list = from_cue(s);
    pot = find(list, red, pocket("top_left"));
    REQUIRE(pot != nullptr);
    CHECK(std::abs(pot->cut_rad - 0.96) < 0.06);
    const rules::FrameState straight = rig::position("straight_red");
    const Pot* full = find(from_cue(straight), red, pocket("top_left"));
    REQUIRE(full != nullptr);
    CHECK(pot->margin_rad < full->margin_rad);
    MESSAGE("cut_red: margin " << pot->margin_rad << " cut " << pot->cut_rad << " aim point (" << pot->aim_point.x << ", "
                               << pot->aim_point.y << ")");

    // A cut wider than the limit is not listed.
    CHECK(find(from_cue(s, pot->cut_rad - 0.01), red, pocket("top_left")) == nullptr);
    CHECK(find(from_cue(s, pot->cut_rad + 0.01), red, pocket("top_left")) != nullptr);
}

TEST_CASE("long_red: a long pot") {
    const rules::FrameState s = rig::position("long_red");
    const Pot* pot = find(from_cue(s), place_of(s, Ball::red), pocket("top_left"));
    REQUIRE(pot != nullptr);
    CHECK(std::string(band(*pot)) == "long");
    MESSAGE("long_red: margin " << pot->margin_rad << " cut " << pot->cut_rad << " aim point (" << pot->aim_point.x
                                << ", " << pot->aim_point.y << ") length " << pot->cue_travel_m + pot->object_travel_m);
}

TEST_CASE("snookered: no pots") {
    const rules::FrameState s = rig::position("snookered");
    CHECK(from_cue(s).empty());
    CHECK(pots(s, rig::env(), kWide).empty());
}

TEST_CASE("colours: only the ball on, with no nomination") {
    const rules::FrameState s = rig::position("colours");
    const std::vector<Pot> list = from_cue(s);
    REQUIRE_FALSE(list.empty());
    for (const Pot& p : list) {
        CHECK(p.target == place_of(s, Ball::yellow));
        CHECK(p.value == 2);
        CHECK_FALSE(p.input.has_nomination);
    }
}

TEST_CASE("after_red: every colour, each declared and worth its own value; the black into both top corners") {
    const rules::FrameState s = rig::position("after_red");
    const std::vector<Pot> list = from_cue(s);
    REQUIRE_FALSE(list.empty());
    for (const Pot& p : list) {
        const Ball b = s.balls[std::size_t(p.target)].ball;
        CHECK(b != Ball::red);
        CHECK(b != Ball::cue);
        CHECK(p.input.has_nomination);
        CHECK(p.input.nominated == b);
        CHECK(p.value == table::ball_value(b));
    }
    // The black is open to both top corners; from this cue ball, below and left of it, the top
    // left would be a cut past a quarter turn.
    const int black = place_of(s, Ball::black);
    const std::vector<int> open = pockets_open_to(s, black, rig::env());
    CHECK(std::find(open.begin(), open.end(), pocket("top_left")) != open.end());
    CHECK(std::find(open.begin(), open.end(), pocket("top_right")) != open.end());
    CHECK(find(list, black, pocket("top_right")) != nullptr);
    CHECK(find(list, black, pocket("top_left")) == nullptr);
    rules::FrameState below = s;
    below.balls[std::size_t(s.cue_ball)].pos = {1.25, -0.3};
    CHECK(find(from_cue(below), black, pocket("top_left")) != nullptr);
}

TEST_CASE("a free ball: the balls not on are listed, worth the ball on and declared") {
    rules::FrameState s = rules::scenario::frame({ball(Ball::cue, 0.5, 0.0), ball(Ball::red, -1.0, 0.5),
                                                  ball(Ball::blue, 1.2, 0.3), ball(Ball::pink, 1.2, -0.3)});
    s.free_ball = true;
    const std::vector<Pot> list = from_cue(s);
    bool blue = false;
    bool pink = false;
    for (const Pot& p : list) {
        const Ball b = s.balls[std::size_t(p.target)].ball;
        CHECK(p.value == 1);
        if (b == Ball::red) {
            CHECK_FALSE(p.input.has_nomination);
        } else {
            CHECK(p.input.has_nomination);
            CHECK(p.input.nominated == b);
            blue = blue || b == Ball::blue;
            pink = pink || b == Ball::pink;
        }
    }
    CHECK(blue);
    CHECK(pink);
    s.free_ball = false;
    for (const Pot& p : from_cue(s)) CHECK(s.balls[std::size_t(p.target)].ball == Ball::red);

    // On a colour the free ball is worth that colour.
    rules::FrameState colour = rules::scenario::frame({ball(Ball::cue, 0.5, 0.0), ball(Ball::green, -1.0, 0.5),
                                                       ball(Ball::blue, 1.2, 0.3)});
    colour.free_ball = true;
    const std::vector<Pot> colour_list = from_cue(colour);
    CHECK(std::any_of(colour_list.begin(), colour_list.end(), [](const Pot& p) { return p.target == 2; }));
    for (const Pot& p : colour_list) CHECK(p.value == 3);
}

// The next three are next_target.gd's own cases (test_next_target.gd), held here too.
TEST_CASE("a red on a corner's axis goes along it, and a 30 degree cut aims at the ghost ball") {
    constexpr double kDiag = 0.70710678118654752;
    const phys::Pocket& corner = rules::scenario::rig().table.pockets[std::size_t(pocket("top_left"))];
    const rules::Vec2 o{table::kPlayingLength / 2.0 - 0.4 * kDiag, table::kPlayingWidth / 2.0 - 0.4 * kDiag};
    const rules::Vec2 t{corner.drop_centre.x - kDiag * corner.drop_radius, corner.drop_centre.y - kDiag * corner.drop_radius};
    const double to_t = dist(t, o);
    const rules::Vec2 g{o.x - 2.0 * kR * (t.x - o.x) / to_t, o.y - 2.0 * kR * (t.y - o.y) / to_t};
    for (const double side : {1.0, -1.0}) {
        CAPTURE(side);
        const double line = (45.0 + 30.0 * side) * 3.14159265358979323846 / 180.0;
        const rules::Vec2 cue{g.x - 0.6 * std::cos(line), g.y - 0.6 * std::sin(line)};
        const rules::FrameState s = rules::scenario::frame({ball(Ball::cue, cue.x, cue.y), ball(Ball::red, o.x, o.y)});
        const Pot* pot = find(from_cue(s), 1, pocket("top_left"));
        REQUIRE(pot != nullptr);
        CHECK(pot->aim_rad == doctest::Approx(line).epsilon(1e-9));
        CHECK(pot->cut_rad == doctest::Approx(30.0 * 3.14159265358979323846 / 180.0).epsilon(1e-9));
        CHECK(pot->aim_point.x == doctest::Approx(t.x).epsilon(1e-9));
        CHECK(pot->aim_point.y == doctest::Approx(t.y).epsilon(1e-9));
    }
}

TEST_CASE("no straight pot where the jaws leave no way in: only the one along the cushion") {
    const rules::Vec2 red{table::kPlayingLength / 2.0 - 0.5, table::kPlayingWidth / 2.0 - kR - 0.003};
    const rules::FrameState s =
        rules::scenario::frame({ball(Ball::cue, red.x - 0.4, red.y), ball(Ball::red, red.x, red.y)});
    const std::vector<Pot> list = from_cue(s);
    const Pot* pot = find(list, 1, pocket("top_left"));
    REQUIRE(pot != nullptr);
    CHECK(pot->along_cushion);
    const std::vector<int> open = pockets_open_to(s, 1, rig::env());
    CHECK(std::find(open.begin(), open.end(), pocket("top_left")) == open.end());
}

TEST_CASE("into a middle pocket from no more than 50 degrees off square") {
    const phys::Pocket& middle = rules::scenario::rig().table.pockets[std::size_t(pocket("middle_left"))];
    const rules::Vec2 mouth{middle.drop_centre.x, middle.drop_centre.y - middle.drop_radius};
    for (const double off : {40.0, 60.0, 80.0}) {
        CAPTURE(off);
        const double a = off * 3.14159265358979323846 / 180.0;
        const rules::Vec2 red{mouth.x - 0.4 * std::sin(a), mouth.y - 0.4 * std::cos(a)};
        const rules::FrameState probe =
            rules::scenario::frame({ball(Ball::cue, -1.0, -0.5), ball(Ball::red, red.x, red.y)});
        const std::vector<int> open = pockets_open_to(probe, 1, rig::env());
        const bool goes_in = std::find(open.begin(), open.end(), pocket("middle_left")) != open.end();
        CHECK(goes_in == (off < 70.0));
        // Straight behind the red on the line it would take, or on the line to the mouth.
        double ux = std::sin(a);
        double uy = std::cos(a);
        if (goes_in) {
            for (const Pot& p : pots_from(probe, {red.x - 0.4 * ux, red.y - 0.4 * uy}, rig::env(), 3.0)) {
                if (p.pocket == pocket("middle_left")) {
                    ux = (p.aim_point.x - red.x) / p.object_travel_m;
                    uy = (p.aim_point.y - red.y) / p.object_travel_m;
                }
            }
        }
        const rules::FrameState s =
            rules::scenario::frame({ball(Ball::cue, red.x - 0.4 * ux, red.y - 0.4 * uy), ball(Ball::red, red.x, red.y)});
        const Pot* pot = find(from_cue(s), 1, pocket("middle_left"));
        if (off < 50.0) {
            REQUIRE(pot != nullptr);
            CHECK(pot->cut_rad < 1e-6);
        } else {
            CHECK(pot == nullptr);
        }
    }
}

TEST_CASE("the margin falls as 1 over the cue travel along the same line") {
    const rules::FrameState s = rig::position("straight_red");
    const int red = place_of(s, Ball::red);
    const rules::Vec2 o = s.balls[std::size_t(red)].pos;
    rules::FrameState alone = rules::scenario::frame({ball(Ball::cue, 0, 0), ball(Ball::red, o.x, o.y)});
    const Pot* first = find(from_cue(s), red, pocket("top_left"));
    REQUIRE(first != nullptr);
    const double ux = (first->aim_point.x - o.x) / first->object_travel_m;
    const double uy = (first->aim_point.y - o.y) / first->object_travel_m;
    double previous = 0.0;
    double margin_times_travel = 0.0;
    for (const double back : {0.3, 0.6, 1.2, 2.0}) {
        CAPTURE(back);
        const rules::Vec2 cue{first->ghost.x - back * ux, first->ghost.y - back * uy};
        const std::vector<Pot> list = pots_from(alone, cue, rig::env(), kWide);
        const Pot* pot = find(list, 1, pocket("top_left"));
        REQUIRE(pot != nullptr);
        CHECK(pot->cut_rad < 1e-9);
        CHECK(pot->cue_travel_m == doctest::Approx(back));
        if (previous > 0.0) {
            CHECK(pot->margin_rad < previous);
            CHECK(pot->margin_rad * pot->cue_travel_m == doctest::Approx(margin_times_travel).epsilon(1e-9));
        }
        previous = pot->margin_rad;
        margin_times_travel = pot->margin_rad * pot->cue_travel_m;
    }
}

TEST_CASE("a ball beside either path narrows the margin; on it, the pot is gone") {
    const rules::FrameState s = rig::position("straight_red");
    const int red = place_of(s, Ball::red);
    const rules::Vec2 o = s.balls[std::size_t(red)].pos;
    const rules::Vec2 c = cue_of(s);
    const auto table_with = [&](rules::Vec2 at) {
        return rules::scenario::frame({ball(Ball::cue, c.x, c.y), ball(Ball::red, o.x, o.y), ball(Ball::blue, at.x, at.y)});
    };
    const rules::FrameState open = rules::scenario::frame({ball(Ball::cue, c.x, c.y), ball(Ball::red, o.x, o.y)});
    const Pot* clear = find(from_cue(open), 1, pocket("top_left"));
    REQUIRE(clear != nullptr);
    const double margin = clear->margin_rad;

    // The object ball's leg.
    const double ux = (clear->aim_point.x - o.x) / clear->object_travel_m;
    const double uy = (clear->aim_point.y - o.y) / clear->object_travel_m;
    const double along_o = 0.3;
    const double gap = 2.0 * kR + 0.002;
    const std::vector<Pot> beside_o = from_cue(table_with({o.x + along_o * ux - gap * uy, o.y + along_o * uy + gap * ux}));
    const Pot* squeezed = find(beside_o, 1, pocket("top_left"));
    REQUIRE(squeezed != nullptr);
    CHECK(squeezed->margin_rad < margin);
    CHECK(squeezed->margin_rad == doctest::Approx(0.002 / along_o * 2.0 * kR / clear->cue_travel_m).epsilon(0.01));
    CHECK(find(from_cue(table_with({o.x + along_o * ux, o.y + along_o * uy})), 1, pocket("top_left")) == nullptr);

    // The cue ball's leg.
    const double vx = (clear->ghost.x - c.x) / clear->cue_travel_m;
    const double vy = (clear->ghost.y - c.y) / clear->cue_travel_m;
    // Half a millimetre clear 0.3 m along: 1.7 milliradians of room, under the pocket's own margin.
    const double along_c = 0.3;
    const double tight = 2.0 * kR + 0.0005;
    const std::vector<Pot> beside_c = from_cue(table_with({c.x + along_c * vx + tight * vy, c.y + along_c * vy - tight * vx}));
    const Pot* narrowed = find(beside_c, 1, pocket("top_left"));
    REQUIRE(narrowed != nullptr);
    CHECK(narrowed->margin_rad < margin);
    CHECK(narrowed->margin_rad == doctest::Approx(0.0005 / along_c).epsilon(0.01));
    CHECK(find(from_cue(table_with({c.x + along_c * vx, c.y + along_c * vy})), 1, pocket("top_left")) == nullptr);
}

TEST_CASE("easiest first, and the same list every time") {
    for (const char* name : {"after_red", "colours", "straight_red", "black_only", "last_red"}) {
        CAPTURE(name);
        const rules::FrameState s = rig::position(name);
        const std::vector<Pot> a = pots(s, rig::env(), kWide);
        const std::vector<Pot> b = pots(s, rig::env(), kWide);
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK(a[i].target == b[i].target);
            CHECK(a[i].pocket == b[i].pocket);
            CHECK(a[i].margin_rad == b[i].margin_rad);
            CHECK(a[i].aim_rad == b[i].aim_rad);
            CHECK(a[i].margin_rad > 0.0);
            CHECK(a[i].cut_rad <= kWide);
            if (i == 0) continue;
            const Pot& x = a[i - 1];
            const Pot& y = a[i];
            const bool ordered = x.margin_rad > y.margin_rad ||
                                 (x.margin_rad == y.margin_rad &&
                                  (x.target < y.target || (x.target == y.target && x.pocket < y.pocket)));
            CHECK(ordered);
        }
    }
}

TEST_CASE("awkward: a cue ball tight on a cushion playing away from it, not along it; a ball close behind") {
    const double rail = -table::kPlayingWidth / 2.0 + kR + 0.01;  // the right-hand cushion
    // Up the table and away from the cushion, into the top left.
    const rules::FrameState away =
        rules::scenario::frame({ball(Ball::cue, 1.0, rail), ball(Ball::red, 1.4, 0.4)});
    const std::vector<Pot> up = from_cue(away);
    REQUIRE_FALSE(up.empty());
    for (const Pot& p : up) {
        CAPTURE(p.pocket);
        CHECK(p.awkward);
    }
    // Along the cushion to a red off it, into the top right.
    const rules::FrameState along =
        rules::scenario::frame({ball(Ball::cue, 0.5, rail), ball(Ball::red, 1.2, -0.7)});
    const Pot* run = find(from_cue(along), 1, pocket("top_right"));
    REQUIRE(run != nullptr);
    CHECK(std::sin(run->aim_rad) < 0.3);
    CHECK_FALSE(run->awkward);

    // The straight red with a blue just behind the cue ball on the line of the cue.
    const rules::FrameState s = rig::position("straight_red");
    const int red = place_of(s, Ball::red);
    const Pot* pot = find(from_cue(s), red, pocket("top_left"));
    REQUIRE(pot != nullptr);
    rules::FrameState behind = s;
    const rules::Vec2 c = cue_of(s);
    behind.balls.push_back({Ball::red, 1, {c.x - 0.09 * std::cos(pot->aim_rad), c.y - 0.09 * std::sin(pot->aim_rad)}});
    const Pot* hampered = find(from_cue(behind), red, pocket("top_left"));
    REQUIRE(hampered != nullptr);
    CHECK(hampered->awkward);
    behind.balls.back().pos = {c.x - 0.2 * std::cos(pot->aim_rad), c.y - 0.2 * std::sin(pot->aim_rad)};
    const Pot* free = find(from_cue(behind), red, pocket("top_left"));
    REQUIRE(free != nullptr);
    CHECK_FALSE(free->awkward);
}
