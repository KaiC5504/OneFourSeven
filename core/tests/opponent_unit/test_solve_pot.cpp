#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>

#include "ofs/opponent/solve.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;

namespace {

constexpr double kR = table::kBallRadius;

int red_place(const rules::Position& p) {
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        if (p.balls[i].ball == table::Ball::red) return int(i);
    }
    return -1;
}

int pocket_index(const char* name) {
    const phys::TableShape& t = rig::env().table;
    for (std::size_t k = 0; k < t.pockets.size(); ++k) {
        if (t.pockets[k].name == name) return int(k);
    }
    return -1;
}

// pots_from's Pot built by hand: the object ball sent at the drop circle's centre.
Pot pot_of(const rules::Position& p, int target, const char* pocket) {
    Pot pot;
    pot.target = target;
    pot.pocket = pocket_index(pocket);
    pot.cue = p.balls[std::size_t(p.cue_ball)].pos;
    pot.aim_point = rig::env().table.pockets[std::size_t(pot.pocket)].drop_centre;
    const rules::Vec2 o = p.balls[std::size_t(target)].pos;
    const double ox = pot.aim_point.x - o.x;
    const double oy = pot.aim_point.y - o.y;
    pot.object_travel_m = std::hypot(ox, oy);
    const double ux = ox / pot.object_travel_m;
    const double uy = oy / pot.object_travel_m;
    pot.ghost = {o.x - 2.0 * kR * ux, o.y - 2.0 * kR * uy};
    const double gx = pot.ghost.x - pot.cue.x;
    const double gy = pot.ghost.y - pot.cue.y;
    pot.cue_travel_m = std::hypot(gx, gy);
    pot.aim_rad = std::atan2(gy, gx);
    pot.cut_rad = std::acos(std::clamp((gx * ux + gy * uy) / pot.cue_travel_m, -1.0, 1.0));
    pot.margin_rad = 0.004;
    return pot;
}

Pot red_pot(const char* fixture) {
    const rules::FrameState s = rig::position(fixture);
    return pot_of(s, red_place(s), "top_left");
}

Spec centre(double speed) {
    Spec s;
    s.speed_m_s = speed;
    return s;
}

phys::CueStrike struck(const Spec& spec, double aim) {
    phys::CueStrike c;
    c.aim_rad = aim;
    c.speed_m_s = spec.speed_m_s;
    c.tip_x = spec.tip_x;
    c.tip_y = spec.tip_y;
    c.elevation_rad = spec.elevation_rad;
    return c;
}

// The pot as solve_pot counts it, read from a fresh simulation of the stroke.
bool pots(const rules::Position& p, const Pot& pot, const phys::CueStrike& strike) {
    phys::ShotRecord record;
    const Trial t = try_strike(p, pot.cue, strike, rig::env(), &record);
    if (!t.clean || t.first_hit != pot.target || t.cue_potted) return false;
    if (std::find(t.potted.begin(), t.potted.end(), pot.target) == t.potted.end()) return false;
    return record.final_state.balls[std::size_t(pot.target)].pocket == pot.pocket;
}

// The direction the object ball leaves the cue ball in, from the record.
double departure(const phys::ShotRecord& r, int cue_ball, int target) {
    double hit = -1.0;
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind == phys::EventKind::ball_ball &&
            ((e.a == cue_ball && e.b == target) || (e.a == target && e.b == cue_ball))) {
            hit = e.t;
            break;
        }
    }
    REQUIRE(hit >= 0.0);
    const phys::Segment* leaving = nullptr;
    for (const phys::Segment& s : r.segments[std::size_t(target)]) {
        if (s.t0 == hit) leaving = &s;
    }
    REQUIRE(leaving != nullptr);
    return std::atan2(leaving->start.v.y, leaving->start.v.x);
}

const char* const kFixtures[] = {"straight_red", "cut_red", "long_red"};

}  // namespace

TEST_CASE("solve_pot pots each red centre ball at 1.5 and 3 m/s, the cue ball staying up") {
    for (const char* name : kFixtures) {
        const rules::FrameState s = rig::position(name);
        const Pot pot = red_pot(name);
        for (double speed : {1.5, 3.0}) {
            CAPTURE(name);
            CAPTURE(speed);
            const SolvedPot solved = solve_pot(s, pot, centre(speed), rig::env(), false, 40);
            REQUIRE(solved.found);
            CHECK(solved.trial.clean);
            CHECK(solved.trial.first_hit == pot.target);
            REQUIRE_FALSE(solved.trial.potted.empty());
            CHECK(solved.trial.potted.front() == pot.target);
            CHECK_FALSE(solved.trial.cue_potted);
            CHECK(solved.window_rad == 0.0);
            CHECK(solved.sims >= 1);
            CHECK(solved.sims <= 40);
            CHECK(pots(s, pot, solved.trial.strike));
        }
    }
}

TEST_CASE("solve_pot allows for side and screw on the cut red") {
    const rules::FrameState s = rig::position("cut_red");
    const Pot pot = red_pot("cut_red");
    // Screw at 1.5 m/s leaves the red short of this pocket; 2.5 carries it.
    const SolvedPot plain = solve_pot(s, pot, centre(2.5), rig::env(), false, 40);
    REQUIRE(plain.found);
    struct Tip {
        double x;
        double y;
    };
    for (Tip tip : {Tip{0.3, 0.0}, Tip{-0.3, 0.0}, Tip{0.0, -0.4}}) {
        CAPTURE(tip.x);
        CAPTURE(tip.y);
        Spec spec = centre(2.5);
        spec.tip_x = tip.x;
        spec.tip_y = tip.y;
        const SolvedPot solved = solve_pot(s, pot, spec, rig::env(), false, 40);
        REQUIRE(solved.found);
        CHECK(solved.trial.strike.tip_x == tip.x);
        CHECK(solved.trial.strike.tip_y == tip.y);
        CHECK(std::abs(solved.trial.strike.aim_rad - plain.trial.strike.aim_rad) > 1.0e-5);
        CHECK(pots(s, pot, solved.trial.strike));
    }
    // Squirt sends the ball away from the side: right-hand side is allowed for by aiming right.
    Spec right = centre(2.5);
    right.tip_x = 0.3;
    Spec left = centre(2.5);
    left.tip_x = -0.3;
    const double with_right = solve_pot(s, pot, right, rig::env(), false, 40).trial.strike.aim_rad;
    const double with_left = solve_pot(s, pot, left, rig::env(), false, 40).trial.strike.aim_rad;
    CHECK(with_right < plain.trial.strike.aim_rad);
    CHECK(with_left > plain.trial.strike.aim_rad);
}

TEST_CASE("turning the aim anticlockwise turns the object ball clockwise, by about the geometric gain") {
    for (const char* name : kFixtures) {
        CAPTURE(name);
        const rules::FrameState s = rig::position(name);
        const Pot pot = red_pot(name);
        const double da = 0.002;
        phys::ShotRecord before;
        phys::ShotRecord after;
        try_strike(s, pot.cue, struck(centre(1.5), pot.aim_rad), rig::env(), &before);
        try_strike(s, pot.cue, struck(centre(1.5), pot.aim_rad + da), rig::env(), &after);
        const double measured =
            std::remainder(departure(after, s.cue_ball, pot.target) - departure(before, s.cue_ball, pot.target),
                           2.0 * 3.14159265358979323846) /
            da;
        const double geometric = 1.0 - pot.cue_travel_m / (2.0 * kR * std::cos(pot.cut_rad));
        CHECK(measured < 0.0);
        CHECK(geometric < 0.0);
        CHECK(measured / geometric == doctest::Approx(1.0).epsilon(0.35));
    }
}

TEST_CASE("the window is measured, the aim in its middle") {
    for (const char* name : kFixtures) {
        CAPTURE(name);
        const rules::FrameState s = rig::position(name);
        const Pot pot = red_pot(name);
        const Spec spec = centre(1.5);
        const SolvedPot solved = solve_pot(s, pot, spec, rig::env(), true, 80);
        REQUIRE(solved.found);
        REQUIRE(solved.window_rad > 0.0);
        CHECK(solved.sims <= 80);
        const double aim = solved.trial.strike.aim_rad;
        CHECK(pots(s, pot, struck(spec, aim + 0.8 * solved.window_rad)));
        CHECK(pots(s, pot, struck(spec, aim - 0.8 * solved.window_rad)));
        const bool wide_left = pots(s, pot, struck(spec, aim + 1.5 * solved.window_rad));
        const bool wide_right = pots(s, pot, struck(spec, aim - 1.5 * solved.window_rad));
        CHECK_FALSE((wide_left && wide_right));
    }
}

TEST_CASE("a pot with a ball in the way is not found, within the budget") {
    rules::FrameState s = rig::position("straight_red");
    const Pot pot = red_pot("straight_red");
    const rules::Vec2 red = s.balls[std::size_t(pot.target)].pos;
    rules::BallOnTable blocker;
    blocker.ball = table::Ball::red;
    blocker.index = 1;
    // Just ahead of the red on its line: a full contact stops the red dead, a thin one turns it well off.
    const double run = std::hypot(pot.aim_point.x - red.x, pot.aim_point.y - red.y);
    const double ahead = (2.0 * kR + 0.002) / run;
    blocker.pos = {red.x + ahead * (pot.aim_point.x - red.x), red.y + ahead * (pot.aim_point.y - red.y)};
    s.balls.push_back(blocker);
    for (int budget : {10, 40}) {
        const SolvedPot solved = solve_pot(s, pot, centre(1.5), rig::env(), true, budget);
        CHECK_FALSE(solved.found);
        CHECK(solved.sims <= budget);
        CHECK(solved.window_rad == 0.0);
    }
}

TEST_CASE("solve_pot never runs more simulations than it is given") {
    for (const char* name : kFixtures) {
        const rules::FrameState s = rig::position(name);
        const Pot pot = red_pot(name);
        Spec spec = centre(1.5);
        spec.tip_x = 0.3;
        for (int budget : {0, 1, 3, 10}) {
            for (bool window : {false, true}) {
                CAPTURE(name);
                CAPTURE(budget);
                CAPTURE(window);
                const SolvedPot solved = solve_pot(s, pot, spec, rig::env(), window, budget);
                CHECK(solved.sims <= budget);
                if (solved.found) CHECK(pots(s, pot, solved.trial.strike));
            }
        }
    }
}

TEST_CASE("the same call gives the same stroke bit for bit") {
    const rules::FrameState s = rig::position("cut_red");
    const Pot pot = red_pot("cut_red");
    Spec spec = centre(2.0);
    spec.tip_x = -0.2;
    spec.tip_y = 0.3;
    const SolvedPot a = solve_pot(s, pot, spec, rig::env(), true, 60);
    const SolvedPot b = solve_pot(s, pot, spec, rig::env(), true, 60);
    REQUIRE(a.found);
    CHECK(std::memcmp(&a.trial.strike, &b.trial.strike, sizeof(phys::CueStrike)) == 0);
    CHECK(std::memcmp(&a.window_rad, &b.window_rad, sizeof(double)) == 0);
    CHECK(a.sims == b.sims);
    CHECK(a.trial.rest.size() == b.trial.rest.size());
    for (std::size_t i = 0; i < a.trial.rest.size() && i < b.trial.rest.size(); ++i) {
        CHECK(a.trial.rest[i].x == b.trial.rest[i].x);
        CHECK(a.trial.rest[i].y == b.trial.rest[i].y);
    }
}

TEST_CASE("least_pot_speed is enough, and no more than about enough") {
    const Env env = rig::env();
    double straight = 0.0;
    double longest = 0.0;
    for (const char* name : kFixtures) {
        CAPTURE(name);
        const rules::FrameState s = rig::position(name);
        const Pot pot = red_pot(name);
        const double least = least_pot_speed(pot, env);
        CHECK(least >= kLeastSpeed_m_s);
        CHECK(least <= env.params.max_cue_speed_m_s);
        CHECK(solve_pot(s, pot, centre(least), env, false, 40).found);
        if (std::string(name) == "straight_red") straight = least;
        if (std::string(name) == "long_red") {
            longest = least;
            CHECK_FALSE(solve_pot(s, pot, centre(0.6 * least), env, false, 40).found);
        }
    }
    CHECK(longest > straight);

    Pot square = red_pot("cut_red");
    square.cut_rad = 1.5707963267948966;
    CHECK(least_pot_speed(square, env) == env.params.max_cue_speed_m_s);
    Pot touching = red_pot("straight_red");
    touching.cue_travel_m = 0.0;
    touching.object_travel_m = 0.0;
    touching.cut_rad = 0.0;
    CHECK(least_pot_speed(touching, env) >= kLeastSpeed_m_s);
}

// Not a check: prints the cost of solving, for the record. Run with --no-skip.
TEST_CASE("solve_pot's cost" * doctest::skip()) {
    for (const char* name : kFixtures) {
        const rules::FrameState s = rig::position(name);
        const Pot pot = red_pot(name);
        const int runs = 20;
        double sim_us = 0.0;
        for (int i = 0; i < runs; ++i) {
            sim_us += try_strike(s, pot.cue, struck(centre(1.5), pot.aim_rad), rig::env()).sim_us;
        }
        MESSAGE((std::string(name) + ": " + std::to_string(sim_us / runs) + " us of simulation per trial"));
        for (bool window : {false, true}) {
            int sims = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < runs; ++i) sims += solve_pot(s, pot, centre(1.5), rig::env(), window, 80).sims;
            const double us =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            MESSAGE((std::string(name) + (window ? " with window: " : " without window: ") +
                    std::to_string(double(sims) / runs) + " sims per solve, " + std::to_string(us / runs) +
                    " us per solve"));
        }
    }
}
