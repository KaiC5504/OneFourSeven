#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "ofs/physics/shot_record.hpp"
#include "scenario.hpp"

// observe(): the first impact when two balls are met at once (S3.6), the push stroke (S2.19),
// touching balls that moved (S3.8(b)) and what counts as an attempt to pot.

using namespace ofs::rules;
namespace sc = ofs::rules::scenario;
namespace phys = ofs::phys;

namespace {

constexpr double kR = ofs::table::kBallRadius;
constexpr double kD = ofs::table::kBallDiameter;
constexpr double kPi = 3.14159265358979323846;

ShotFacts see(const Position& p, const phys::ShotRecord& r) { return observe(p, r, sc::rig().table, sc::rig().ref); }

bool same(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

// The cue ball at the origin and a red `gap` from its surface along +x.
FrameState pair_with_gap(double gap) {
    return sc::frame({sc::ball(Ball::cue, 0.0, 0.0), sc::ball(Ball::red, kD + gap, 0.0),
                      sc::ball(Ball::black, 1.2, 0.4)});
}

// The cut the cue ball makes on a ball `gap` away when it is sent `off_line` radians off the line
// of centres: the angle at contact between its line and the line of centres.
double cut_at_contact(double gap, double off_line) { return std::asin((kD + gap) * std::sin(off_line) / kD); }

}  // namespace

TEST_CASE("two reds met at the same instant are both the first impact; a millimetre apart, only one") {
    // The cue ball comes straight down the middle of a pair a ball's width apart and half a
    // millimetre clear of each other.
    const double half = kR + 0.0005;
    SUBCASE("level") {
        const FrameState s = sc::frame({sc::ball(Ball::cue, -0.3, 0.0), sc::ball(Ball::red, 0.0, half),
                                        sc::ball(Ball::red, 0.0, -half, 1)});
        const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 2.0));
        const ShotFacts f = see(s, r);
        CHECK(f.first_hit == std::vector<int>{1, 2});
        CHECK(f.first_hit_t > 0.0);
        CHECK_FALSE(f.push);
        CHECK(f.closest_gap_m[1] == 0.0);
        CHECK(f.closest_gap_m[2] == 0.0);
    }
    SUBCASE("level, the cue ball listed last") {
        const FrameState s = sc::frame({sc::ball(Ball::red, 0.0, -half), sc::ball(Ball::red, 0.0, half, 1),
                                        sc::ball(Ball::cue, -0.3, 0.0)}, 2);
        const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 2.0));
        const ShotFacts f = see(s, r);
        CHECK(f.first_hit == std::vector<int>{0, 1});
    }
    SUBCASE("the second red a millimetre further on") {
        const FrameState s = sc::frame({sc::ball(Ball::cue, -0.3, 0.0), sc::ball(Ball::red, 0.0, half),
                                        sc::ball(Ball::red, 0.001, -half, 1)});
        const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 2.0));
        const ShotFacts f = see(s, r);
        CHECK(f.first_hit == std::vector<int>{1});
        // It is hit a moment later all the same.
        CHECK(f.closest_gap_m[2] == 0.0);
    }
    SUBCASE("the first red a millimetre further on") {
        const FrameState s = sc::frame({sc::ball(Ball::cue, -0.3, 0.0), sc::ball(Ball::red, 0.001, half),
                                        sc::ball(Ball::red, 0.0, -half, 1)});
        const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 2.0));
        const ShotFacts f = see(s, r);
        CHECK(f.first_hit == std::vector<int>{2});
    }
    SUBCASE("a wider window takes the later contact in") {
        const FrameState s = sc::frame({sc::ball(Ball::cue, -0.3, 0.0), sc::ball(Ball::red, 0.0, half),
                                        sc::ball(Ball::red, 0.001, -half, 1)});
        const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 2.0));
        RefParams wide = sc::rig().ref;
        wide.simultaneous_hit_s = 1e-3;
        const ShotFacts f = observe(s, r, sc::rig().table, wide);
        CHECK(f.first_hit == std::vector<int>{1, 2});
    }
}

TEST_CASE("a touching ball played into is a push and has moved") {
    for (const double gap : {0.0, 2e-6}) {
        CAPTURE(gap);
        REQUIRE(gap < sc::rig().ref.touching_gap_m);
        const FrameState s = pair_with_gap(gap);
        const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 1.0));
        const ShotFacts f = see(s, r);
        CHECK(f.first_hit == std::vector<int>{1});
        CHECK(f.first_hit_t < 1e-4);
        CHECK(f.push);
        CHECK(f.touching_moved == std::vector<int>{1});
        CHECK(f.rest[1].x > kD + gap + 0.05);
        CHECK(f.closest_gap_m[1] == 0.0);
        CHECK_FALSE(f.anomaly);
    }
}

TEST_CASE("a touching ball played away from is neither a push nor moved") {
    for (const double gap : {0.0, 2e-6}) {
        for (const double aim : {kPi, 0.75 * kPi, -0.6 * kPi}) {
            CAPTURE(gap);
            CAPTURE(aim);
            const FrameState s = pair_with_gap(gap);
            const phys::ShotRecord r = sc::simulate(s, sc::strike(aim, 1.0));
            const ShotFacts f = see(s, r);
            CHECK(f.first_hit.empty());
            CHECK_FALSE(f.push);
            CHECK(f.touching_moved.empty());
            CHECK(same(f.rest[1], s.balls[1].pos));
            // It was touching when the stroke began, and that is as near as the cue ball came.
            CHECK(f.closest_gap_m[1] == doctest::Approx(gap).epsilon(1e-6));
            CHECK(f.closest_gap_m[1] < sc::rig().ref.touching_gap_m);
            CHECK(f.cue_travel_m > 0.1);
        }
    }
}

TEST_CASE("a touching ball clipped at a fine edge has moved, though the contact is no push") {
    // S2.19(b) excuses the fine edge; S3.8(b) still wants the touching ball left where it is.
    const double off_line = 85.0 * kPi / 180.0;
    REQUIRE(off_line > sc::rig().ref.push_fine_edge_rad);
    const FrameState s = pair_with_gap(0.0);
    const phys::ShotRecord r = sc::simulate(s, sc::strike(off_line, 1.5));
    const ShotFacts f = see(s, r);
    CHECK(f.first_hit == std::vector<int>{1});
    CHECK(f.first_hit_t == 0.0);
    CHECK_FALSE(f.push);
    CHECK(f.touching_moved == std::vector<int>{1});
    CHECK_FALSE(same(f.rest[1], s.balls[1].pos));
}

TEST_CASE("a ball called touching across a 0.002 mm gap, clipped at a fine edge, has moved too") {
    // The cue ball reaches it some hundredth of a millisecond after the strike, having gone a
    // fortieth of a millimetre: still its way out, as far as a straight line can go to a ball that
    // near.
    const double gap = 2e-6;
    const double off_line = 85.0 * kPi / 180.0;
    REQUIRE(gap < sc::rig().ref.touching_gap_m);
    REQUIRE(cut_at_contact(gap, off_line) > sc::rig().ref.push_fine_edge_rad);
    const FrameState s = pair_with_gap(gap);
    const phys::ShotRecord r = sc::simulate(s, sc::strike(off_line, 1.5));
    const ShotFacts f = see(s, r);
    CHECK(f.first_hit == std::vector<int>{1});
    CHECK(f.first_hit_t > 1000.0 * sc::rig().ref.simultaneous_hit_s);
    CHECK_FALSE(f.push);
    CHECK(f.touching_moved == std::vector<int>{1});
}

TEST_CASE("a touching ball played away from and met again on the way back is not listed") {
    // The red touches the cue ball on the baulk side. The cue ball goes square to the top cushion
    // and comes straight back onto it: the red moves, but not as the cue ball left it (S3.8(b)).
    const FrameState s = sc::frame({sc::ball(Ball::cue, 1.2, 0.0), sc::ball(Ball::red, 1.2 - kD, 0.0),
                                    sc::ball(Ball::black, 0.0, 0.6)});
    const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 2.0));
    const ShotFacts f = see(s, r);
    double cushion_t = -1.0;
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind == phys::EventKind::ball_line && e.a == 0 && cushion_t < 0.0) cushion_t = e.t;
    }
    REQUIRE(cushion_t > 0.0);
    REQUIRE(f.first_hit == std::vector<int>{1});
    REQUIRE(f.first_hit_t > cushion_t);
    REQUIRE_FALSE(same(f.rest[1], s.balls[1].pos));
    CHECK(f.touching_moved.empty());
    CHECK_FALSE(f.push);
    CHECK(f.closest_gap_m[1] == 0.0);
}

TEST_CASE("a touching ball that another ball moves later is not listed") {
    // No strike: the cue ball rolls away from the red it touches, and a second red rolls into
    // that red from the side.
    const FrameState s = sc::frame({sc::ball(Ball::cue, 0.0, 0.0), sc::ball(Ball::red, kD, 0.0),
                                    sc::ball(Ball::red, kD, 0.4, 1)});
    phys::SimInput in;
    in.start = sc::table_state(s);
    in.has_strike = false;
    in.start.balls[0].v = {-0.3, 0.0, 0.0};
    in.start.balls[0].w = {0.0, -0.3 / kR, 0.0};
    in.start.balls[2].v = {0.0, -0.5, 0.0};
    in.start.balls[2].w = {0.5 / kR, 0.0, 0.0};
    const phys::ShotRecord r = phys::simulate(in, sc::rig().table, sc::rig().params);
    const ShotFacts f = see(s, r);
    REQUIRE_FALSE(f.anomaly);
    REQUIRE(f.rest[0].x < -0.3);
    REQUIRE(f.rest[1].y < -0.05);
    CHECK(f.first_hit.empty());
    CHECK(f.touching_moved.empty());
}

TEST_CASE("a ball half a millimetre away played full is a push") {
    const FrameState s = pair_with_gap(0.0005);
    const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 2.0));
    const ShotFacts f = see(s, r);
    CHECK(f.first_hit == std::vector<int>{1});
    CHECK(f.first_hit_t > 0.0);
    CHECK(f.first_hit_t < sc::rig().ref.push_contact_time_s);
    CHECK(f.push);
    // Not a touching ball: the gap is ten times the touching gap.
    CHECK(f.touching_moved.empty());
}

TEST_CASE("the same ball clipped at a fine edge is no push") {
    // Sent 80 degrees off the line of centres the cue ball meets the red at a cut of 83.8, inside
    // a millisecond at this pace: only the fine edge excuses it. At 60 degrees off it is a push.
    const double gap = 0.0005;
    const double fine = 80.0 * kPi / 180.0;
    const double thick = 60.0 * kPi / 180.0;
    REQUIRE(cut_at_contact(gap, fine) > sc::rig().ref.push_fine_edge_rad);
    REQUIRE(cut_at_contact(gap, thick) < sc::rig().ref.push_fine_edge_rad);
    const FrameState s = pair_with_gap(gap);

    const ShotFacts edge = see(s, sc::simulate(s, sc::strike(fine, 5.0)));
    CHECK(edge.first_hit == std::vector<int>{1});
    CHECK(edge.first_hit_t > 0.0);
    CHECK(edge.first_hit_t < sc::rig().ref.push_contact_time_s);
    CHECK_FALSE(edge.push);

    const ShotFacts full = see(s, sc::simulate(s, sc::strike(thick, 5.0)));
    CHECK(full.first_hit == std::vector<int>{1});
    CHECK(full.first_hit_t < sc::rig().ref.push_contact_time_s);
    CHECK(full.push);

    // The threshold is the referee's value: with the fine edge set past this cut, it is a push.
    RefParams strict = sc::rig().ref;
    strict.push_fine_edge_rad = 85.0 * kPi / 180.0;
    CHECK(observe(s, sc::simulate(s, sc::strike(fine, 5.0)), sc::rig().table, strict).push);
}

TEST_CASE("a ball 30 mm away played full is no push") {
    const FrameState s = pair_with_gap(0.03);
    const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 2.0));
    const ShotFacts f = see(s, r);
    CHECK(f.first_hit == std::vector<int>{1});
    CHECK(f.first_hit_t > sc::rig().ref.push_contact_time_s);
    CHECK_FALSE(f.push);
    CHECK(f.touching_moved.empty());

    // The tip's time on the ball is the referee's value too.
    RefParams slow_tip = sc::rig().ref;
    slow_tip.push_contact_time_s = 0.05;
    CHECK(observe(s, r, sc::rig().table, slow_tip).push);
    RefParams no_test = sc::rig().ref;
    no_test.push_contact_time_s = 0.0;
    const FrameState touching = pair_with_gap(0.0);
    CHECK_FALSE(observe(touching, sc::simulate(touching, sc::strike(0.0, 2.0)), sc::rig().table, no_test).push);
}

TEST_CASE("a record with no strike has no push") {
    // The cue ball already moving, half a millimetre from the red: no tip, so nothing to push with.
    const FrameState s = pair_with_gap(0.0005);
    phys::SimInput in;
    in.start = sc::table_state(s);
    in.has_strike = false;
    in.start.balls[0].v = {2.0, 0.0, 0.0};
    const phys::ShotRecord r = phys::simulate(in, sc::rig().table, sc::rig().params);
    const ShotFacts f = see(s, r);
    CHECK(f.first_hit == std::vector<int>{1});
    CHECK(f.first_hit_t < sc::rig().ref.push_contact_time_s);
    CHECK_FALSE(f.push);
    CHECK_FALSE(f.anomaly);
}

TEST_CASE("a second touching ball that stays put is not listed") {
    // Two reds touch the cue ball: it is played into one and, a little, away from the other.
    const double behind = 5.0 * kPi / 180.0;
    const FrameState s = sc::frame({sc::ball(Ball::cue, 0.0, 0.0), sc::ball(Ball::red, kD, 0.0),
                                    sc::ball(Ball::red, -kD * std::sin(behind), -kD * std::cos(behind), 1),
                                    sc::ball(Ball::red, 0.4, 0.5, 2)});
    REQUIRE(std::hypot(s.balls[2].pos.x, s.balls[2].pos.y) - kD < sc::rig().ref.touching_gap_m);
    const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 1.0));
    const ShotFacts f = see(s, r);
    CHECK(f.first_hit == std::vector<int>{1});
    CHECK(f.push);
    CHECK(f.touching_moved == std::vector<int>{1});
    CHECK(same(f.rest[2], s.balls[2].pos));
}

TEST_CASE("pot attempts: a pot, a ball that rattles in the jaws, a safety, a ball knocked away") {
    const auto& pockets = sc::rig().table.pockets;
    const double radius = sc::rig().ref.pot_attempt_radius_m;
    const auto mouth_named = [&](const char* name) {
        for (const phys::Pocket& p : pockets) {
            if (p.name == name) return p;
        }
        return phys::Pocket{};
    };
    // How near the ball came to any pocket's drop circle.
    const auto nearest_mouth = [&](const phys::ShotRecord& r, int ball) {
        double least = 1e300;
        for (double t = 0.0; t <= r.t_end; t += 1e-3) {
            const phys::BallState b = r.ball_at(ball, t);
            for (const phys::Pocket& p : pockets) {
                least = std::min(least, std::hypot(b.r.x - p.drop_centre.x, b.r.y - p.drop_centre.y) - p.drop_radius);
            }
        }
        return least;
    };
    // The same, counting only moments when it was 10 mm nearer that pocket than it started: the
    // smallest radius at which its path is an attempt.
    const auto attempt_radius = [&](const phys::ShotRecord& r, int ball) {
        double least = 1e300;
        const ofs::Vec3 from = r.ball_at(ball, 0.0).r;
        for (const phys::Pocket& p : pockets) {
            const double began = std::hypot(from.x - p.drop_centre.x, from.y - p.drop_centre.y);
            for (double t = 0.0; t <= r.t_end; t += 1e-3) {
                const phys::BallState b = r.ball_at(ball, t);
                const double d = std::hypot(b.r.x - p.drop_centre.x, b.r.y - p.drop_centre.y);
                if (d <= began - 0.010) least = std::min(least, d - p.drop_radius);
            }
        }
        return least;
    };

    SUBCASE("a red potted, whatever the radius") {
        const FrameState s = sc::frame({sc::ball(Ball::cue, 0.0, 0.3), sc::ball(Ball::red, 0.0, 0.6),
                                        sc::ball(Ball::black, 1.0, 0.0)});
        phys::CueStrike c = sc::strike(kPi / 2.0, 1.2);
        c.tip_y = -0.25;
        const phys::ShotRecord r = sc::simulate(s, c);
        const ShotFacts f = see(s, r);
        REQUIRE(f.pots.size() == 1);
        CHECK(f.pot_attempt);
        RefParams none = sc::rig().ref;
        none.pot_attempt_radius_m = 0.0;
        CHECK(observe(s, r, sc::rig().table, none).pot_attempt);
    }
    SUBCASE("a red rattled in a corner's jaws and left out") {
        // Parallel to the side cushion, into the top cushion's jaw.
        const FrameState s = sc::frame({sc::ball(Ball::cue, 0.9, 0.8), sc::ball(Ball::red, 1.3, 0.8),
                                        sc::ball(Ball::black, 0.0, -0.5)});
        phys::CueStrike c = sc::strike(0.0, 1.5);
        c.tip_y = -0.2;
        const phys::ShotRecord r = sc::simulate(s, c);
        const ShotFacts f = see(s, r);
        REQUIRE(f.pots.empty());
        REQUIRE(attempt_radius(r, 1) < radius - 0.02);
        bool jaw = false;
        for (const phys::ShotEvent& e : r.events) jaw = jaw || (e.kind == phys::EventKind::ball_arc && e.a == 1);
        REQUIRE(jaw);
        CHECK(f.first_hit == std::vector<int>{1});
        CHECK(f.pot_attempt);
    }
    SUBCASE("a safety: the red goes nowhere near a pocket") {
        const FrameState s = sc::frame({sc::ball(Ball::cue, -0.6, 0.0), sc::ball(Ball::red, 0.0, 0.1),
                                        sc::ball(Ball::black, 1.2, 0.5)});
        const phys::ShotRecord r = sc::simulate(s, sc::strike(sc::aim_at_ball(s, 1), 0.5));
        const ShotFacts f = see(s, r);
        REQUIRE(f.first_hit == std::vector<int>{1});
        REQUIRE(nearest_mouth(r, 1) > radius + 0.02);
        CHECK_FALSE(f.pot_attempt);
    }
    SUBCASE("a red beside a pocket knocked away from it is no attempt") {
        // 0.16 m from the top left pocket's drop circle, played down the top cushion: inside the
        // radius from the start, and never nearer the pocket than that.
        const FrameState s = sc::frame({sc::ball(Ball::cue, 1.72, 0.85), sc::ball(Ball::red, 1.72, 0.74),
                                        sc::ball(Ball::black, 0.0, 0.0)});
        const phys::ShotRecord r = sc::simulate(s, sc::strike(-kPi / 2.0, 0.45));
        const ShotFacts f = see(s, r);
        REQUIRE(f.first_hit == std::vector<int>{1});
        REQUIRE(f.pots.empty());
        REQUIRE(nearest_mouth(r, 1) < radius - 0.02);
        REQUIRE(f.rest[1].y < 0.74 - 0.3);
        REQUIRE(attempt_radius(r, 1) > radius + 0.02);
        CHECK_FALSE(f.pot_attempt);
    }
    SUBCASE("a red already near a pocket is an attempt once it goes 10 mm nearer") {
        // On the corner's diagonal 0.1 m from the drop circle, tapped towards it from 1 mm behind.
        const phys::Pocket mouth = mouth_named("top_left");
        const double diagonal = std::sqrt(0.5);
        const double began = mouth.drop_radius + 0.1;
        const double behind = began + kD + 0.001;
        const FrameState s = sc::frame(
            {sc::ball(Ball::cue, mouth.drop_centre.x - behind * diagonal, mouth.drop_centre.y - behind * diagonal),
             sc::ball(Ball::red, mouth.drop_centre.x - began * diagonal, mouth.drop_centre.y - began * diagonal),
             sc::ball(Ball::black, 0.0, 0.0)});
        int short_of = 0;
        int enough = 0;
        for (const double speed : {0.02, 0.045, 0.063, 0.066, 0.07, 0.075, 0.12}) {
            CAPTURE(speed);
            const phys::ShotRecord r = sc::simulate(s, sc::strike(sc::aim_at(s, mouth.drop_centre), speed));
            const ShotFacts f = see(s, r);
            REQUIRE(f.first_hit == std::vector<int>{1});
            REQUIRE(f.pots.empty());
            // It rolls straight at the pocket and stops: where it rests is the nearest it came.
            const double nearer =
                began - std::hypot(f.rest[1].x - mouth.drop_centre.x, f.rest[1].y - mouth.drop_centre.y);
            CAPTURE(nearer);
            REQUIRE(nearer > 0.0);
            if (nearer < 0.0095) {
                CHECK_FALSE(f.pot_attempt);
                ++short_of;
            } else if (nearer > 0.0105) {
                CHECK(f.pot_attempt);
                ++enough;
            }
        }
        // The taps bracket the 10 mm: 9.3 mm nearer is not an attempt, 11.8 mm is.
        CHECK(short_of >= 3);
        CHECK(enough >= 3);
    }
    SUBCASE("only the first ball hit counts: the cue ball's own path does not") {
        // On its way to a corner pocket the cue ball clips a red by a third of a millimetre; the
        // red barely leaves the middle of the table.
        const Vec2 mouth = mouth_named("top_left").drop_centre;
        const Vec2 cue{0.6, 0.1};
        const double line = std::atan2(mouth.y - cue.y, mouth.x - cue.x);
        const double along = 0.15;
        const double across = kD - 0.0003;
        const FrameState s = sc::frame(
            {sc::ball(Ball::cue, cue.x, cue.y),
             sc::ball(Ball::red, cue.x + along * std::cos(line) + across * std::sin(line),
                      cue.y + along * std::sin(line) - across * std::cos(line)),
             sc::ball(Ball::black, -1.0, 0.0)});
        const phys::ShotRecord r = sc::simulate(s, sc::strike(line, 1.2));
        const ShotFacts f = see(s, r);
        REQUIRE(f.first_hit == std::vector<int>{1});
        REQUIRE(nearest_mouth(r, 1) > radius + 0.02);
        REQUIRE(attempt_radius(r, 0) < radius - 0.02);
        CHECK_FALSE(f.pot_attempt);
    }
    SUBCASE("the radius is the referee's value") {
        const FrameState s = sc::frame({sc::ball(Ball::cue, -0.6, 0.0), sc::ball(Ball::red, 0.0, 0.1),
                                        sc::ball(Ball::black, 1.2, 0.5)});
        const phys::ShotRecord r = sc::simulate(s, sc::strike(sc::aim_at_ball(s, 1), 0.5));
        const double needed = attempt_radius(r, 1);
        REQUIRE(needed < 10.0);
        RefParams wide = sc::rig().ref;
        wide.pot_attempt_radius_m = needed + 0.01;
        CHECK(observe(s, r, sc::rig().table, wide).pot_attempt);
        wide.pot_attempt_radius_m = needed - 0.01;
        CHECK_FALSE(observe(s, r, sc::rig().table, wide).pot_attempt);
    }
}
