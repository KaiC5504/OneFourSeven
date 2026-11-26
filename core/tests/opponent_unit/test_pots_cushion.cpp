#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::BallOnTable;
using rules::FrameState;
using rules::Vec2;
using rules::scenario::ball;
using rules::scenario::spotted;

namespace {

constexpr double kR = table::kBallRadius;
constexpr double kPi = 3.14159265358979323846;
constexpr double kWide = 1.35;
const double kHalfX = table::kPlayingLength / 2.0;
const double kHalfY = table::kPlayingWidth / 2.0;
// pots.cpp's own numbers: the cushion's end past the corner, and what is claimed of its width for
// a ball on the cushion and for one 4 mm or more off it.
constexpr double kEnd = 0.03;
constexpr double kTouchingWidth = 0.004;
constexpr double kWidth = 0.0065;

int pocket(const char* name) {
    const auto& pockets = rig::env().table.pockets;
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

int count(const std::vector<Pot>& pots, int target, int pocket_index) {
    return int(std::count_if(pots.begin(), pots.end(),
                             [&](const Pot& p) { return p.target == target && p.pocket == pocket_index; }));
}

Vec2 cue_of(const rules::Position& p) { return p.balls[std::size_t(p.cue_ball)].pos; }

std::vector<Pot> listed(const FrameState& s) { return pots_from(s, cue_of(s), rig::env(), kWide); }

// The cue ball at place 0 and one red at place 1.
FrameState lone_red(Vec2 cue, Vec2 red) {
    return rules::scenario::frame({ball(Ball::cue, cue.x, cue.y), ball(Ball::red, red.x, red.y, 1)});
}

// A red `run` short of the top-left corner along the left-hand cushion and `gap` off it, the cue
// ball `back` behind it along the line to the cushion's end, turned `cut_deg` towards the table.
FrameState beside_left_cushion(double run, double gap, double back = 0.4, double cut_deg = 20.0) {
    const Vec2 red{kHalfX - run, kHalfY - kR - gap};
    const Vec2 end{kHalfX + kEnd, kHalfY - kR};
    const double length = std::hypot(end.x - red.x, end.y - red.y);
    const Vec2 w{(end.x - red.x) / length, (end.y - red.y) / length};
    const double turn = cut_deg * kPi / 180.0;
    const Vec2 from{-(w.x * std::cos(turn) - w.y * std::sin(turn)), -(w.x * std::sin(turn) + w.y * std::cos(turn))};
    return lone_red({red.x - 2.0 * kR * w.x + back * from.x, red.y - 2.0 * kR * w.y + back * from.y}, red);
}

// The same along the top cushion, towards the top-left corner.
FrameState beside_top_cushion(double run, double gap, double back = 0.4, double cut_deg = 20.0) {
    const Vec2 red{kHalfX - kR - gap, kHalfY - run};
    const Vec2 end{kHalfX - kR, kHalfY + kEnd};
    const double length = std::hypot(end.x - red.x, end.y - red.y);
    const Vec2 w{(end.x - red.x) / length, (end.y - red.y) / length};
    const double turn = -cut_deg * kPi / 180.0;
    const Vec2 from{-(w.x * std::cos(turn) - w.y * std::sin(turn)), -(w.x * std::sin(turn) + w.y * std::cos(turn))};
    return lone_red({red.x - 2.0 * kR * w.x + back * from.x, red.y - 2.0 * kR * w.y + back * from.y}, red);
}

FrameState mirrored(const FrameState& s, double sx, double sy) {
    FrameState out = s;
    for (BallOnTable& b : out.balls) b.pos = {sx * b.pos.x, sy * b.pos.y};
    return out;
}

// What the fixtures' pots added up to before pots along a cushion were listed.
double sum_of(const std::vector<Pot>& pots) {
    double sum = 0.0;
    for (const Pot& p : pots) {
        sum += p.margin_rad + p.aim_rad + p.cut_rad + p.cue_travel_m + p.object_travel_m + p.aim_point.x + p.aim_point.y +
               p.ghost.x + p.ghost.y;
    }
    return sum;
}

bool dropped_in(const FrameState& s, const Pot& pot, const phys::CueStrike& strike) {
    phys::ShotRecord record;
    const Trial t = try_strike(s, pot.cue, strike, rig::env(), &record);
    return t.clean && !t.cue_potted && t.first_hit == pot.target &&
           record.final_state.balls[std::size_t(pot.target)].pocket == pot.pocket;
}

}  // namespace

TEST_CASE("the fixtures' pots are what they were before a pot along a cushion was listed") {
    const struct {
        const char* name;
        int from_cue;
        double from_cue_sum;
        int all;
        double all_sum;
    } before[] = {
        {"after_red", 1, 3.043089702479282, 1, 3.043089702479282},
        {"black_only", 1, 4.1915769564299454, 1, 4.1915769564299454},
        {"colours", 1, -4.2370805557727431, 1, -4.2370805557727431},
        {"cut_red", 1, 8.5900371804491211, 1, 8.5900371804491211},
        {"in_hand", 1, 1.7579197073582957, 8, 14.132109971277426},
        {"last_red", 1, 2.1261768568641091, 1, 2.1261768568641091},
        {"long_red", 1, 8.2606973515828752, 1, 8.2606973515828752},
        {"snookered", 0, 0.0, 0, 0.0},
        {"straight_red", 1, 6.2045151451853169, 1, 6.2045151451853169},
    };
    for (const auto& b : before) {
        CAPTURE(b.name);
        const FrameState s = rig::position(b.name);
        const std::vector<Pot> from_cue = listed(s);
        const std::vector<Pot> all = pots(s, rig::env(), kWide);
        CHECK(int(from_cue.size()) == b.from_cue);
        CHECK(sum_of(from_cue) == doctest::Approx(b.from_cue_sum).epsilon(1e-13));
        CHECK(int(all.size()) == b.all);
        CHECK(sum_of(all) == doctest::Approx(b.all_sum).epsilon(1e-13));
        for (const Pot& p : all) CHECK_FALSE(p.along_cushion);
    }
}

TEST_CASE("a red on the left-hand cushion is listed along it into the corner at its end, and nowhere else") {
    const FrameState s = beside_left_cushion(0.75, 0.0);
    const Vec2 red = s.balls[1].pos;
    const std::vector<Pot> list = listed(s);
    REQUIRE(list.size() == 1);
    const Pot& pot = list.front();
    CHECK(pot.target == 1);
    CHECK(pot.pocket == pocket("top_left"));
    CHECK(pot.along_cushion);
    // Sent at the cushion's end, where a ball touching the cushion would be just past the corner.
    CHECK(pot.aim_point.x == doctest::Approx(kHalfX + kEnd));
    CHECK(pot.aim_point.y == doctest::Approx(kHalfY - kR));
    CHECK(pot.object_travel_m == doctest::Approx(0.75 + kEnd));
    CHECK(pot.ghost.x == doctest::Approx(red.x - 2.0 * kR));
    CHECK(pot.ghost.y == doctest::Approx(red.y));
    CHECK(pot.cue.x == cue_of(s).x);
    CHECK(pot.cue.y == cue_of(s).y);
    CHECK(pot.aim_rad == doctest::Approx(std::atan2(pot.ghost.y - pot.cue.y, pot.ghost.x - pot.cue.x)));
    CHECK(pot.cut_rad == doctest::Approx(20.0 * kPi / 180.0));
    CHECK(pot.cue_travel_m == doctest::Approx(0.4));
    CHECK(pot.value == 1);
    CHECK_FALSE(pot.input.has_nomination);
    CHECK_FALSE(pot.awkward);
    // The claimed width as the red sees it, brought back through the contact to the cue ball.
    CHECK(pot.margin_rad ==
          doctest::Approx(kTouchingWidth / (0.75 + kEnd) * 2.0 * kR * std::cos(pot.cut_rad) / pot.cue_travel_m));
    MESSAGE("touching, 0.75 m out: margin " << pot.margin_rad);

    // From the other side of it the red goes the other way, past the middle pocket to the baulk corner.
    FrameState other = s;
    other.balls[0].pos = {2.0 * red.x - cue_of(s).x, cue_of(s).y};
    const std::vector<Pot> back = listed(other);
    REQUIRE(back.size() == 1);
    CHECK(back.front().pocket == pocket("bottom_left"));
    CHECK(back.front().along_cushion);
    CHECK(back.front().aim_point.x == doctest::Approx(-kHalfX - kEnd));
    CHECK(back.front().object_travel_m == doctest::Approx(kHalfX + red.x + kEnd));
}

TEST_CASE("every cushion gives its pot into each corner it ends at, alike") {
    const FrameState side = beside_left_cushion(0.9, 0.003);
    const FrameState top = beside_top_cushion(0.6, 0.003);
    const Pot* first_side = nullptr;
    const std::vector<Pot> side_list = listed(side);
    first_side = find(side_list, 1, pocket("top_left"));
    REQUIRE(first_side != nullptr);
    const std::vector<Pot> top_list = listed(top);
    const Pot* first_top = find(top_list, 1, pocket("top_left"));
    REQUIRE(first_top != nullptr);
    CHECK(first_top->along_cushion);
    CHECK(first_top->aim_point.x == doctest::Approx(kHalfX - kR));
    CHECK(first_top->aim_point.y == doctest::Approx(kHalfY + kEnd));

    const struct {
        double sx;
        double sy;
        const char* name;
    } corners[] = {{1.0, 1.0, "top_left"}, {1.0, -1.0, "top_right"}, {-1.0, 1.0, "bottom_left"}, {-1.0, -1.0, "bottom_right"}};
    for (const auto& c : corners) {
        CAPTURE(c.name);
        for (const FrameState* base : {&side, &top}) {
            const FrameState s = mirrored(*base, c.sx, c.sy);
            const std::vector<Pot> list = listed(s);
            const Pot* pot = find(list, 1, pocket(c.name));
            REQUIRE(pot != nullptr);
            CHECK(pot->along_cushion);
            const Pot* like = base == &side ? first_side : first_top;
            CHECK(pot->margin_rad == doctest::Approx(like->margin_rad).epsilon(1e-12));
            CHECK(pot->cut_rad == doctest::Approx(like->cut_rad).epsilon(1e-12));
            CHECK(pot->object_travel_m == doctest::Approx(like->object_travel_m).epsilon(1e-12));
            CHECK(pot->aim_point.x == doctest::Approx(c.sx * like->aim_point.x));
            CHECK(pot->aim_point.y == doctest::Approx(c.sy * like->aim_point.y));
        }
    }
}

TEST_CASE("off the cushion the pot along it is listed until a straight line opens, and then the straight pot") {
    const int top_left = pocket("top_left");
    for (const double run : {0.2, 0.35, 0.5, 1.0, 1.6, 2.5}) {
        CAPTURE(run);
        bool straight_seen = false;
        int along = 0;
        for (int i = 0; i <= 50; ++i) {
            // Out to a quarter of the run: by a sixth of it every ball has its straight line.
            const double gap = 0.25 * (run + kEnd) * i / 50.0;
            CAPTURE(gap);
            const FrameState s = beside_left_cushion(run, gap, 0.3, 10.0);
            const std::vector<Pot> list = listed(s);
            REQUIRE(count(list, 1, top_left) == 1);
            const Pot* pot = find(list, 1, top_left);
            CHECK(pot->margin_rad > 0.0);
            if (pot->along_cushion) {
                // Never again once the straight line has opened.
                CHECK_FALSE(straight_seen);
                ++along;
                CHECK(pot->aim_point.x == doctest::Approx(kHalfX + kEnd));
                CHECK(pot->aim_point.y == doctest::Approx(kHalfY - kR));
                CHECK(gap <= 0.15 * (run + kEnd) + 1e-12);
            } else {
                straight_seen = true;
            }
        }
        CHECK(along >= 10);
        CHECK(straight_seen);
    }
}

TEST_CASE("a ball in the jaws is not listed along the cushion") {
    // The jaw starts 0.103 m short of the corner.
    const FrameState in_jaws = beside_left_cushion(0.11, 0.0, 0.3, 10.0);
    for (const Pot& p : listed(in_jaws)) CHECK_FALSE(p.along_cushion);
    const FrameState clear = beside_left_cushion(0.13, 0.0, 0.3, 10.0);
    const std::vector<Pot> list = listed(clear);
    const Pot* pot = find(list, 1, pocket("top_left"));
    REQUIRE(pot != nullptr);
    CHECK(pot->along_cushion);
}

TEST_CASE("the margin along a cushion: less for a ball on it, less further out, never more than the cue ball's room") {
    const int top_left = pocket("top_left");
    const auto margin = [&](double run, double gap) {
        const FrameState s = beside_left_cushion(run, gap);
        const std::vector<Pot> list = listed(s);
        const Pot* pot = find(list, 1, top_left);
        REQUIRE(pot != nullptr);
        REQUIRE(pot->along_cushion);
        return pot->margin_rad;
    };
    CHECK(margin(1.0, 0.0) < margin(1.0, 0.002));
    CHECK(margin(1.0, 0.002) < margin(1.0, 0.004));
    CHECK(margin(1.0, 0.004) == doctest::Approx(margin(1.0, 0.012)).epsilon(0.01));
    CHECK(margin(0.5, 0.0) > margin(1.0, 0.0));
    CHECK(margin(1.0, 0.0) > margin(2.5, 0.0));

    // 4 mm and more off the cushion: the full width claimed.
    const FrameState s = beside_left_cushion(1.0, 0.01);
    const std::vector<Pot> list = listed(s);
    const Pot* pot = find(list, 1, top_left);
    REQUIRE(pot != nullptr);
    CHECK(pot->margin_rad ==
          doctest::Approx(kWidth / pot->object_travel_m * 2.0 * kR * std::cos(pot->cut_rad) / pot->cue_travel_m));

    // A ball beside the cue ball's path narrows it as it narrows any pot.
    FrameState squeezed = s;
    const Vec2 cue = cue_of(s);
    const Vec2 half{0.5 * (cue.x + pot->ghost.x), 0.5 * (cue.y + pot->ghost.y)};
    const double dx = (pot->ghost.x - cue.x) / pot->cue_travel_m;
    const double dy = (pot->ghost.y - cue.y) / pot->cue_travel_m;
    constexpr double kSqueeze = 0.00002;
    squeezed.balls.push_back(ball(Ball::blue, half.x + (2.0 * kR + kSqueeze) * dy, half.y - (2.0 * kR + kSqueeze) * dx));
    const std::vector<Pot> narrow = listed(squeezed);
    const Pot* tight = find(narrow, 1, top_left);
    REQUIRE(tight != nullptr);
    CHECK(tight->along_cushion);
    CHECK(tight->margin_rad < 0.5 * pot->margin_rad);
    CHECK(tight->margin_rad == doctest::Approx(kSqueeze / (0.5 * pot->cue_travel_m)).epsilon(0.01));
}

TEST_CASE("a ball in the way along the cushion, or between the cue ball and the red, removes the pot") {
    const int top_left = pocket("top_left");
    const FrameState s = beside_left_cushion(1.0, 0.0);
    const Vec2 red = s.balls[1].pos;
    REQUIRE(find(listed(s), 1, top_left) != nullptr);

    FrameState on_cushion = s;
    on_cushion.balls.push_back(ball(Ball::blue, red.x + 0.5, kHalfY - kR));
    CHECK(find(listed(on_cushion), 1, top_left) == nullptr);

    // Just off the cushion further along: still within a ball's width of the run.
    FrameState beside = s;
    beside.balls.push_back(ball(Ball::blue, red.x + 0.5, kHalfY - kR - 0.04));
    CHECK(find(listed(beside), 1, top_left) == nullptr);

    FrameState at_the_end = s;
    at_the_end.balls.push_back(ball(Ball::blue, kHalfX - 0.03, kHalfY - kR - 0.01));
    CHECK(find(listed(at_the_end), 1, top_left) == nullptr);

    FrameState clear = s;
    clear.balls.push_back(ball(Ball::blue, red.x + 0.5, kHalfY - kR - 0.2));
    const std::vector<Pot> still = listed(clear);
    const Pot* pot = find(still, 1, top_left);
    REQUIRE(pot != nullptr);
    CHECK(pot->along_cushion);
    CHECK(pot->margin_rad == doctest::Approx(find(listed(s), 1, top_left)->margin_rad));

    FrameState behind = s;
    behind.balls.push_back(ball(Ball::blue, red.x - 0.5, kHalfY - kR));
    CHECK(find(listed(behind), 1, top_left) != nullptr);

    FrameState blocked = s;
    const Vec2 cue = cue_of(s);
    const std::vector<Pot> unblocked = listed(s);
    const Pot* open = find(unblocked, 1, top_left);
    blocked.balls.push_back(ball(Ball::blue, 0.5 * (cue.x + open->ghost.x), 0.5 * (cue.y + open->ghost.y)));
    CHECK(find(listed(blocked), 1, top_left) == nullptr);
}

TEST_CASE("nothing is listed along a cushion into a middle pocket") {
    const int middle_left = pocket("middle_left");
    const auto& pockets = rig::env().table.pockets;
    // Reds beside the left-hand cushion on the top half, the cue ball behind each towards the top:
    // sent along the cushion they would come to the middle pocket.
    for (const double from_middle : {0.2, 0.35, 0.5, 0.75, 1.0, 1.3}) {
        for (const double gap : {0.0, 0.002, 0.01, 0.03, 0.06}) {
            CAPTURE(from_middle);
            CAPTURE(gap);
            const Vec2 red{from_middle, kHalfY - kR - gap};
            for (const double cut_deg : {0.0, 20.0, 45.0}) {
                const double turn = cut_deg * kPi / 180.0;
                const FrameState s = lone_red({red.x + 2.0 * kR + 0.4 * std::cos(turn), red.y - 0.4 * std::sin(turn)}, red);
                const std::vector<Pot> list = listed(s);
                const Pot* middle = find(list, 1, middle_left);
                if (middle != nullptr) CHECK_FALSE(middle->along_cushion);
                for (const Pot& p : list) {
                    if (p.along_cushion) CHECK(pockets[std::size_t(p.pocket)].corner);
                }
            }
        }
    }
    // On the cushion the red has no way into the middle at all.
    const FrameState s = lone_red({0.5 + 2.0 * kR + 0.4, kHalfY - kR - 0.1}, {0.5, kHalfY - kR});
    CHECK(find(listed(s), 1, middle_left) == nullptr);
}

TEST_CASE("a pot along a cushion is listed to a cut of 70 degrees and no wider") {
    const int top_left = pocket("top_left");
    CHECK(find(listed(beside_left_cushion(1.0, 0.0, 0.4, 65.0)), 1, top_left) != nullptr);
    CHECK(find(listed(beside_left_cushion(1.0, 0.0, 0.4, 73.0)), 1, top_left) == nullptr);
    // A straight pot goes on to the level's own limit: the same red well off the cushion, the cue
    // ball turned 74 degrees from its line.
    const Vec2 red{kHalfX - 0.5, kHalfY - kR - 0.2};
    const std::vector<Pot> easy = listed(lone_red({red.x - 0.4, red.y - 0.2}, red));
    const Pot* line = find(easy, 1, top_left);
    REQUIRE(line != nullptr);
    REQUIRE_FALSE(line->along_cushion);
    const Vec2 w{(line->aim_point.x - red.x) / line->object_travel_m, (line->aim_point.y - red.y) / line->object_travel_m};
    const double turn = 74.0 * kPi / 180.0;
    const Vec2 from{-(w.x * std::cos(turn) - w.y * std::sin(turn)), -(w.x * std::sin(turn) + w.y * std::cos(turn))};
    const FrameState thin = lone_red({line->ghost.x + 0.4 * from.x, line->ghost.y + 0.4 * from.y}, red);
    const std::vector<Pot> list = listed(thin);
    const Pot* pot = find(list, 1, top_left);
    REQUIRE(pot != nullptr);
    CHECK_FALSE(pot->along_cushion);
    CHECK(pot->cut_rad == doctest::Approx(turn));
}

TEST_CASE("adding a ball on a cushion adds its pot and leaves the pots there were alone") {
    FrameState s = rig::position("straight_red");
    const std::vector<Pot> before = listed(s);
    REQUIRE(before.size() == 1);
    // On the top cushion, to go along it into the top-right corner.
    s.balls.push_back(ball(Ball::red, kHalfX - kR, -0.3, 7));
    const int added = int(s.balls.size()) - 1;
    const std::vector<Pot> after = listed(s);
    int old = 0;
    int extra = 0;
    for (const Pot& p : after) {
        if (p.target == added) {
            ++extra;
            CHECK(p.along_cushion);
            CHECK(p.pocket == pocket("top_right"));
            continue;
        }
        ++old;
        const Pot& was = before.front();
        CHECK_FALSE(p.along_cushion);
        CHECK(p.target == was.target);
        CHECK(p.pocket == was.pocket);
        CHECK(p.margin_rad == was.margin_rad);
        CHECK(p.aim_rad == was.aim_rad);
        CHECK(p.cut_rad == was.cut_rad);
        CHECK(p.aim_point.x == was.aim_point.x);
        CHECK(p.aim_point.y == was.aim_point.y);
        CHECK(p.ghost.x == was.ghost.x);
        CHECK(p.ghost.y == was.ghost.y);
    }
    CHECK(old == 1);
    CHECK(extra == 1);
}

TEST_CASE("a listed pot along a cushion is solved and potted on the simulator, across the cushion's length") {
    const Env env = rig::env();
    struct Case {
        bool side;
        double run;
    };
    const Case cases[] = {{true, 0.2}, {true, 0.5}, {true, 1.0}, {true, 1.6}, {true, 2.5}, {true, 3.2},
                          {false, 0.3}, {false, 0.8}, {false, 1.4}};
    for (const Case& c : cases) {
        // On the cushion, just off it, and well off it: a sixteenth of the run.
        for (const double gap : {0.0, 0.003, 0.06 * (c.run + kEnd)}) {
            CAPTURE(c.side);
            CAPTURE(c.run);
            CAPTURE(gap);
            const FrameState s = c.side ? beside_left_cushion(c.run, gap) : beside_top_cushion(c.run, gap);
            const std::vector<Pot> list = listed(s);
            const Pot* pot = find(list, 1, pocket("top_left"));
            REQUIRE(pot != nullptr);
            REQUIRE(pot->along_cushion);
            // A firm stroke, half as fast again as the slowest that gets there.
            Spec spec;
            spec.speed_m_s = std::min(1.5 * least_pot_speed(*pot, env), 0.95 * env.params.max_cue_speed_m_s);
            const SolvedPot solved = solve_pot(s, *pot, spec, env, true, 60);
            REQUIRE(solved.found);
            CHECK(dropped_in(s, *pot, solved.trial.strike));
            // What is listed is no more than the simulator takes.
            CHECK(solved.window_rad >= pot->margin_rad);
            MESSAGE("the " << std::string(c.side ? "left" : "top") << " cushion, " << c.run << " m out, " << (gap * 1000.0)
                           << " mm off: margin " << pot->margin_rad << ", window " << solved.window_rad << ", speed "
                           << spec.speed_m_s);
        }
    }
}

TEST_CASE("the search's own few simulations find a pot along a cushion at most of a level's speeds") {
    const Env env = rig::env();
    for (const double gap : {0.0, 0.005}) {
        CAPTURE(gap);
        const FrameState s = beside_left_cushion(1.0, gap);
        const std::vector<Pot> list = listed(s);
        const Pot* pot = find(list, 1, pocket("top_left"));
        REQUIRE(pot != nullptr);
        const double top = 0.95 * env.params.max_cue_speed_m_s;
        const double least = least_pot_speed(*pot, env);
        int found = 0;
        for (int rung = 0; rung < 8; ++rung) {
            Spec spec;
            spec.speed_m_s = least * std::pow(top / least, rung / 7.0);
            const SolvedPot solved = solve_pot(s, *pot, spec, env, false, 6);
            CHECK(solved.sims <= 6);
            if (!solved.found) continue;
            ++found;
            CHECK(dropped_in(s, *pot, solved.trial.strike));
        }
        CHECK(found >= 6);
        MESSAGE("a red " << (gap * 1000.0) << " mm off the cushion: found at " << found << " of 8 speeds");
    }
}

TEST_CASE("the 147 pots a red that lies on a cushion along it, and its why says so") {
    // The red has no other pot: any line off the cushion needs the cue ball where the cushion is.
    std::vector<BallOnTable> balls{ball(Ball::cue, 0.7, 0.55), ball(Ball::red, 1.2, kHalfY - kR, 0)};
    for (const Ball c : {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        balls.push_back(spotted(c));
    }
    const FrameState s = rules::scenario::frame(std::move(balls));
    for (const Pot& p : pots(s, rig::env(), rig::levels().maximum.max_cut_rad)) CHECK(p.along_cushion);

    Think think;
    think.max_sims = 1500;
    Rng rng(1);
    const Stroke stroke = choose_stroke(s, rig::levels().maximum, rig::env(), rng, think);
    REQUIRE_FALSE(stroke.decides);
    CHECK(stroke.why.value("kind", std::string()) == "pot");
    CHECK(stroke.why.value("target", std::string()) == "red 0");
    CHECK(stroke.why.value("pocket", -1) == pocket("top_left"));
    CHECK(stroke.why.value("along_cushion", false));
    CHECK(stroke.fallback.empty());
    const rules::scenario::Played played = rules::scenario::play(s, stroke.strike, stroke.input);
    CHECK(played.judgement.fouls.empty());
    CHECK(played.judgement.points == 1);

    // An ordinary pot's why does not carry the word.
    const FrameState open = rig::position("straight_red");
    Rng again(1);
    const Stroke plain = choose_stroke(open, rig::levels().maximum, rig::env(), again, think);
    CHECK(plain.why.value("kind", std::string()) == "pot");
    CHECK_FALSE(plain.why.contains("along_cushion"));
}

TEST_CASE("Beginner takes a pot along a cushion too, and logs it the same way") {
    std::vector<BallOnTable> balls{ball(Ball::cue, 0.9, 0.7), ball(Ball::red, 1.2, kHalfY - kR, 0)};
    for (const Ball c : {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        balls.push_back(spotted(c));
    }
    const FrameState s = rules::scenario::frame(std::move(balls));
    Rng rng(3);
    const Stroke stroke = choose_stroke(s, rig::levels().beginner, rig::env(), rng);
    REQUIRE_FALSE(stroke.decides);
    CHECK(stroke.why.value("kind", std::string()) == "pot");
    CHECK(stroke.why.value("along_cushion", false));
    CHECK(stroke.why.value("pocket", -1) == pocket("top_left"));
}
