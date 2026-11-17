// Finding the stroke that pots on the real physics (spec 10.2 step 2, solve.hpp). Every pot is
// taken from pots_from; every claim about a stroke is checked by playing it again with try_strike.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::Vec2;
using rules::scenario::place_of;

namespace {

constexpr double kWideCut = 1.35;
const char* const kFixtures[] = {"straight_red", "cut_red", "long_red"};

int top_left() {
    const auto& pockets = rules::scenario::rig().table.pockets;
    for (std::size_t i = 0; i < pockets.size(); ++i) {
        if (pockets[i].name == "top_left") return int(i);
    }
    return -1;
}

Vec2 cue_pos(const rules::Position& p) { return p.balls[std::size_t(p.cue_ball)].pos; }

// The red's pot into the top-left corner; found false when pots_from does not list it.
struct Setup {
    rules::FrameState s;
    Pot pot;
    bool found = false;
};

Setup red_into_top_left(const char* name) {
    Setup out;
    out.s = rig::position(name);
    const int red = place_of(out.s, Ball::red);
    for (const Pot& p : pots_from(out.s, cue_pos(out.s), rig::env(), kWideCut)) {
        if (p.target == red && p.pocket == top_left()) {
            out.pot = p;
            out.found = true;
            break;
        }
    }
    return out;
}

bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// The pot as solve_pot promises it: the target met first, dropped in its pocket, the cue ball up.
bool pots_it(const Trial& t, const Pot& pot) {
    return t.first_hit == pot.target && contains(t.potted, pot.target) && !t.cue_potted;
}

Spec centre(double speed) { return Spec{speed, 0.0, 0.0, 0.0}; }

bool same_strike(const phys::CueStrike& a, const phys::CueStrike& b) {
    return a.aim_rad == b.aim_rad && a.speed_m_s == b.speed_m_s && a.tip_x == b.tip_x && a.tip_y == b.tip_y &&
           a.elevation_rad == b.elevation_rad;
}

}  // namespace

TEST_CASE("spec 10.2 step 2: a centre-ball pot at 2 m/s is solved for the straight, cut and long reds") {
    for (const char* name : kFixtures) {
        CAPTURE(name);
        const Setup set = red_into_top_left(name);
        REQUIRE(set.found);
        const SolvedPot solved = solve_pot(set.s, set.pot, centre(2.0), rig::env(), false, 200);
        REQUIRE(solved.found);
        CHECK(solved.trial.clean);
        CHECK(solved.trial.first_hit == set.pot.target);
        CHECK(contains(solved.trial.potted, set.pot.target));
        CHECK_FALSE(solved.trial.cue_potted);
        CHECK(solved.trial.strike.speed_m_s == 2.0);
        CHECK(solved.trial.strike.tip_x == 0.0);
        CHECK(solved.trial.strike.tip_y == 0.0);
        CHECK(solved.trial.strike.elevation_rad == 0.0);
    }
}

TEST_CASE("README, how a stroke is chosen: the solved trial is the stroke; try_strike of its strike does the same") {
    for (const char* name : kFixtures) {
        CAPTURE(name);
        const Setup set = red_into_top_left(name);
        REQUIRE(set.found);
        const SolvedPot solved = solve_pot(set.s, set.pot, centre(2.0), rig::env(), false, 200);
        REQUIRE(solved.found);
        const Trial again = try_strike(set.s, set.pot.cue, solved.trial.strike, rig::env());
        CHECK(again.first_hit == solved.trial.first_hit);
        CHECK(again.potted == solved.trial.potted);
        CHECK(again.cue_potted == solved.trial.cue_potted);
        CHECK(again.cue_cushions == solved.trial.cue_cushions);
        CHECK(again.cannon == solved.trial.cannon);
        CHECK(again.t_end == solved.trial.t_end);
        REQUIRE(again.rest.size() == solved.trial.rest.size());
        for (std::size_t i = 0; i < again.rest.size(); ++i) {
            CHECK(again.rest[i].x == solved.trial.rest[i].x);
            CHECK(again.rest[i].y == solved.trial.rest[i].y);
        }
    }
}

TEST_CASE("spec 10.2 step 2: the straight red is potted with left side, right side and screw") {
    const Setup set = red_into_top_left("straight_red");
    REQUIRE(set.found);
    for (const Spec spec : {Spec{2.0, 0.3, 0.0, 0.0}, Spec{2.0, -0.3, 0.0, 0.0}, Spec{2.0, 0.0, -0.4, 0.0}}) {
        CAPTURE(spec.tip_x);
        CAPTURE(spec.tip_y);
        const SolvedPot solved = solve_pot(set.s, set.pot, spec, rig::env(), false, 200);
        REQUIRE(solved.found);
        CHECK(pots_it(solved.trial, set.pot));
        CHECK(solved.trial.strike.tip_x == spec.tip_x);
        CHECK(solved.trial.strike.tip_y == spec.tip_y);
        CHECK(pots_it(try_strike(set.s, set.pot.cue, solved.trial.strike, rig::env()), set.pot));
    }
}

TEST_CASE("spec 10.2 step 2: side is allowed for: the most forgiving aim moves one way for right-hand side, the other for left") {
    const Setup set = red_into_top_left("cut_red");
    REQUIRE(set.found);
    const SolvedPot plain = solve_pot(set.s, set.pot, centre(2.0), rig::env(), true, 400);
    const SolvedPot right = solve_pot(set.s, set.pot, Spec{2.0, 0.3, 0.0, 0.0}, rig::env(), true, 400);
    const SolvedPot left = solve_pot(set.s, set.pot, Spec{2.0, -0.3, 0.0, 0.0}, rig::env(), true, 400);
    REQUIRE(plain.found);
    REQUIRE(right.found);
    REQUIRE(left.found);
    const double r = right.trial.strike.aim_rad - plain.trial.strike.aim_rad;
    const double l = left.trial.strike.aim_rad - plain.trial.strike.aim_rad;
    CHECK(std::abs(r) > 1e-4);
    CHECK(std::abs(l) > 1e-4);
    CHECK(r * l < 0.0);

    // The centre-ball aim struck with side on does not play the same stroke.
    phys::CueStrike with_side = plain.trial.strike;
    with_side.tip_x = 0.3;
    const Trial t = try_strike(set.s, set.pot.cue, with_side, rig::env());
    const std::size_t target = std::size_t(set.pot.target);
    REQUIRE(t.rest.size() == set.s.balls.size());
    REQUIRE(plain.trial.rest.size() == set.s.balls.size());
    const bool same = t.first_hit == plain.trial.first_hit && t.potted == plain.trial.potted &&
                      t.rest[target].x == plain.trial.rest[target].x && t.rest[target].y == plain.trial.rest[target].y;
    CHECK_FALSE(same);
}

TEST_CASE("solve.hpp: the measured window is open, and the aim turned 0.7 of it either way still pots") {
    for (const char* name : {"straight_red", "cut_red"}) {
        CAPTURE(name);
        const Setup set = red_into_top_left(name);
        REQUIRE(set.found);
        const SolvedPot solved = solve_pot(set.s, set.pot, centre(2.0), rig::env(), true, 400);
        REQUIRE(solved.found);
        CHECK(solved.window_rad > 0.0);
        CHECK(pots_it(solved.trial, set.pot));
        for (const double side : {-0.7, 0.7}) {
            CAPTURE(side);
            phys::CueStrike turned = solved.trial.strike;
            turned.aim_rad += side * solved.window_rad;
            CHECK(pots_it(try_strike(set.s, set.pot.cue, turned, rig::env()), set.pot));
        }
    }
}

TEST_CASE("solve.hpp: without measure_window the window is 0") {
    const Setup set = red_into_top_left("straight_red");
    REQUIRE(set.found);
    const SolvedPot solved = solve_pot(set.s, set.pot, centre(2.0), rig::env(), false, 200);
    CHECK(solved.window_rad == 0.0);
}

TEST_CASE("solve.hpp: never more simulations than max_sims, and it still returns") {
    const Setup set = red_into_top_left("cut_red");
    REQUIRE(set.found);
    for (const int budget : {1, 4, 20}) {
        for (const bool window : {false, true}) {
            CAPTURE(budget);
            CAPTURE(window);
            const SolvedPot solved = solve_pot(set.s, set.pot, centre(2.0), rig::env(), window, budget);
            CHECK(solved.sims <= budget);
            CHECK(solved.sims >= 0);
            if (solved.found) CHECK(pots_it(solved.trial, set.pot));
        }
    }
}

TEST_CASE("spec 10.2 step 2: noise-free: the same call twice gives exactly the same strike") {
    for (const char* name : kFixtures) {
        CAPTURE(name);
        const Setup set = red_into_top_left(name);
        REQUIRE(set.found);
        for (const bool window : {false, true}) {
            const SolvedPot a = solve_pot(set.s, set.pot, Spec{2.5, 0.2, -0.2, 0.0}, rig::env(), window, 300);
            const SolvedPot b = solve_pot(set.s, set.pot, Spec{2.5, 0.2, -0.2, 0.0}, rig::env(), window, 300);
            CHECK(a.found == b.found);
            CHECK(same_strike(a.trial.strike, b.trial.strike));
            CHECK(a.window_rad == b.window_rad);
            CHECK(a.sims == b.sims);
        }
    }
}

TEST_CASE("solve.hpp: least_pot_speed is within its limits, higher for the long red, and pots all three") {
    const double top = rig::env().params.max_cue_speed_m_s;
    double straight = 0.0;
    double longest = 0.0;
    for (const char* name : kFixtures) {
        CAPTURE(name);
        const Setup set = red_into_top_left(name);
        REQUIRE(set.found);
        const double v = least_pot_speed(set.pot, rig::env());
        CHECK(v >= kLeastSpeed_m_s);
        CHECK(v <= top);
        if (std::string(name) == "straight_red") straight = v;
        if (std::string(name) == "long_red") longest = v;
        const SolvedPot solved = solve_pot(set.s, set.pot, centre(v), rig::env(), false, 200);
        REQUIRE(solved.found);
        CHECK(pots_it(solved.trial, set.pot));
    }
    CHECK(longest > straight);
}

TEST_CASE("solve.hpp: least_pot_speed grows with the cut for the same distances") {
    const Setup straight = red_into_top_left("straight_red");
    REQUIRE(straight.found);
    Pot cut = straight.pot;
    cut.cut_rad = 0.9;
    CHECK(least_pot_speed(cut, rig::env()) > least_pot_speed(straight.pot, rig::env()));
}

TEST_CASE("solve.hpp: try_strike of a stroke aimed at nothing hits nothing, pots nothing and moves the cue ball") {
    const rules::FrameState s = rig::position("straight_red");
    const Vec2 start = cue_pos(s);
    // Straight towards the right-hand cushion: no ball lies within a ball's width of that line.
    phys::CueStrike strike;
    strike.aim_rad = -3.14159265358979323846 / 2.0;
    strike.speed_m_s = 0.5;
    const Trial t = try_strike(s, start, strike, rig::env());
    CHECK(t.clean);
    CHECK(t.first_hit == -1);
    CHECK(t.potted.empty());
    CHECK_FALSE(t.cue_potted);
    CHECK_FALSE(t.cannon);
    REQUIRE(t.rest.size() == s.balls.size());
    const Vec2 end = t.rest[std::size_t(s.cue_ball)];
    CHECK(std::hypot(end.x - start.x, end.y - start.y) > 0.05);
    for (std::size_t i = 0; i < s.balls.size(); ++i) {
        if (int(i) == s.cue_ball) continue;
        CHECK(t.rest[i].x == s.balls[i].pos.x);
        CHECK(t.rest[i].y == s.balls[i].pos.y);
    }
}

TEST_CASE("solve.hpp: try_strike plays from the cue position given, not the listed one") {
    const rules::FrameState s = rig::position("in_hand");
    REQUIRE(s.cue_in_hand);
    const Vec2 elsewhere{-1.1, 0.2};
    REQUIRE(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, elsewhere));
    phys::CueStrike strike;
    strike.aim_rad = 3.14159265358979323846;
    strike.speed_m_s = 0.3;
    const Trial t = try_strike(s, elsewhere, strike, rig::env());
    REQUIRE(t.rest.size() == s.balls.size());
    const Vec2 end = t.rest[std::size_t(s.cue_ball)];
    // Rolled towards baulk along y = 0.2.
    CHECK(end.x < elsewhere.x);
    CHECK(std::abs(end.y - elsewhere.y) < 0.01);
}

TEST_CASE("solve.hpp: try_strike fills the shot record when asked") {
    const Setup set = red_into_top_left("straight_red");
    REQUIRE(set.found);
    phys::CueStrike strike;
    strike.aim_rad = set.pot.aim_rad;
    strike.speed_m_s = 2.0;
    phys::ShotRecord record;
    const Trial t = try_strike(set.s, set.pot.cue, strike, rig::env(), &record);
    CHECK(record.has_strike);
    CHECK(record.final_state.balls.size() == set.s.balls.size());
    CHECK(record.t_end == t.t_end);
}
