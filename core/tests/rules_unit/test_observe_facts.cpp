#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ofs/physics/rack.hpp"
#include "ofs/physics/shot_record.hpp"
#include "scenario.hpp"

// observe(): what was hit, what went in, where the balls rest, how close the cue ball came and how
// far everything travelled, on shots through the real simulator.

using namespace ofs::rules;
namespace sc = ofs::rules::scenario;
namespace phys = ofs::phys;

namespace {

constexpr double kD = ofs::table::kBallDiameter;
constexpr double kPi = 3.14159265358979323846;

ShotFacts see(const Position& p, const phys::ShotRecord& r) { return observe(p, r, sc::rig().table, sc::rig().ref); }

int pocket_named(const std::string& name) {
    const auto& pockets = sc::rig().table.pockets;
    for (std::size_t i = 0; i < pockets.size(); ++i) {
        if (pockets[i].name == name) return int(i);
    }
    return -1;
}

double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

Vec2 final_xy(const phys::ShotRecord& r, int ball) {
    const ofs::Vec3& p = r.final_state.balls[std::size_t(ball)].r;
    return {p.x, p.y};
}

double first_event_t(const phys::ShotRecord& r, phys::EventKind kind, int ball) {
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind == kind && (e.a == ball || e.b == ball)) return e.t;
    }
    return -1.0;
}

int count_events(const phys::ShotRecord& r, phys::EventKind kind, int ball) {
    int count = 0;
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind == kind && (e.a == ball || e.b == ball)) ++count;
    }
    return count;
}

bool cue_touched(const phys::ShotRecord& r, int cue, int ball) {
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind == phys::EventKind::ball_ball && ((e.a == cue && e.b == ball) || (e.a == ball && e.b == cue))) {
            return true;
        }
    }
    return false;
}

bool same(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

// The record read by brute force: every ball's smallest gap to the cue ball while both are on the
// table, and the cue ball's path length as a polyline. Sampled at every segment start and, between
// them, wherever the fastest ball has moved `step_m`. A potted ball is sampled one last time
// where it dropped (its pocket event starts a segment there).
struct Sampled {
    std::vector<double> gap;
    double travel = 0.0;
};

Sampled sample(const phys::ShotRecord& r, int cue, double step_m) {
    const std::size_t n = r.segments.size();
    std::vector<double> starts;
    for (const std::vector<phys::Segment>& segs : r.segments) {
        for (const phys::Segment& seg : segs) starts.push_back(seg.t0);
    }
    std::sort(starts.begin(), starts.end());
    Sampled out;
    out.gap.assign(n, 1e300);
    std::vector<bool> gone(n, false);
    const ofs::Vec3 start = r.ball_at(cue, 0.0).r;
    Vec2 last{start.x, start.y};
    std::size_t next = 0;
    double t = 0.0;
    for (;;) {
        const phys::TableState now = r.state_at(t);
        const phys::BallState& c = now.balls[std::size_t(cue)];
        double fastest = 0.0;
        for (const phys::BallState& b : now.balls) fastest = std::max(fastest, std::hypot(b.v.x, b.v.y));
        if (!gone[std::size_t(cue)]) {
            out.travel += std::hypot(c.r.x - last.x, c.r.y - last.y);
            last = {c.r.x, c.r.y};
            for (std::size_t i = 0; i < n; ++i) {
                const phys::BallState& b = now.balls[i];
                if (gone[i]) continue;
                out.gap[i] = std::min(out.gap[i], std::hypot(c.r.x - b.r.x, c.r.y - b.r.y) - kD);
            }
        }
        for (std::size_t i = 0; i < n; ++i) gone[i] = now.balls[i].motion == phys::Motion::pocketed;
        if (t >= r.t_end) break;
        double to = t + std::clamp(step_m / std::max(fastest, 1e-9), 1e-6, 5e-3);
        while (next < starts.size() && starts[next] <= t) ++next;
        if (next < starts.size() && starts[next] < to) to = starts[next];
        t = std::min(to, r.t_end);
    }
    for (double& g : out.gap) g = std::max(g, 0.0);
    return out;
}

}  // namespace

TEST_CASE("a red hit full: the first impact, the resting places and the gaps") {
    const FrameState s = sc::frame({sc::ball(Ball::cue, -0.5, 0.2), sc::ball(Ball::red, 0.1, 0.2),
                                    sc::ball(Ball::red, 0.6, -0.4, 1)});
    const phys::ShotRecord r = sc::simulate(s, sc::strike(sc::aim_at_ball(s, 1), 0.4));
    const ShotFacts f = see(s, r);

    CHECK(f.first_hit == std::vector<int>{1});
    CHECK(f.first_hit_t > 0.0);
    CHECK(f.first_hit_t == first_event_t(r, phys::EventKind::ball_ball, 0));
    CHECK(f.pots.empty());
    CHECK_FALSE(f.cue_potted);
    CHECK(f.cue_pocket == -1);
    CHECK_FALSE(f.push);
    CHECK(f.touching_moved.empty());
    CHECK_FALSE(f.anomaly);

    REQUIRE(f.rest.size() == 3);
    for (int i = 0; i < 3; ++i) CHECK(same(f.rest[std::size_t(i)], final_xy(r, i)));
    // Both balls run on along the line of the shot; the red that was not touched stays put.
    CHECK(f.rest[1].x > 0.2);
    CHECK(f.rest[1].y == doctest::Approx(0.2).epsilon(1e-12));
    CHECK(f.rest[0].x > -0.5 + 0.6 - kD);
    CHECK(f.rest[0].x < f.rest[1].x - kD);
    CHECK(same(f.rest[2], s.balls[2].pos));

    REQUIRE(f.closest_gap_m.size() == 3);
    CHECK(f.closest_gap_m[0] == 0.0);
    CHECK(f.closest_gap_m[1] == 0.0);
    // The cue ball never leaves y = 0.2 and stops short of the other red's x, so it is nearest
    // where it rests.
    REQUIRE(f.rest[0].x < 0.6);
    CHECK(f.closest_gap_m[2] == doctest::Approx(dist(f.rest[0], {0.6, -0.4}) - kD).epsilon(1e-9));

    CHECK(f.cue_travel_m == doctest::Approx(f.rest[0].x + 0.5).epsilon(1e-9));
    CHECK(f.object_travel_m == doctest::Approx(f.rest[1].x - 0.1).epsilon(1e-9));
}

TEST_CASE("nothing hit: how close the cue ball passed each ball") {
    // The cue ball runs along y = 0 from x = -1. One red sits 0.1 m off that line, one behind the
    // cue ball, and the pink ahead of where it stops.
    const FrameState s = sc::frame({sc::ball(Ball::cue, -1.0, 0.0), sc::ball(Ball::red, -0.2, 0.1),
                                    sc::ball(Ball::red, -1.3, 0.05, 1), sc::ball(Ball::pink, 1.2, -0.3)});
    const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 0.5));
    const ShotFacts f = see(s, r);

    CHECK(f.first_hit.empty());
    CHECK(f.first_hit_t == 0.0);
    CHECK(f.pots.empty());
    CHECK_FALSE(f.push);
    CHECK_FALSE(f.pot_attempt);
    CHECK_FALSE(f.anomaly);
    REQUIRE(f.rest[0].x > 0.0);
    REQUIRE(f.rest[0].x < 1.0);
    CHECK(f.rest[0].y == 0.0);

    REQUIRE(f.closest_gap_m.size() == 4);
    CHECK(f.closest_gap_m[0] == 0.0);
    // Passed: the centres are 0.1 m apart as the cue ball goes by.
    CHECK(std::abs(f.closest_gap_m[1] - (0.1 - kD)) < 1e-3);
    CHECK(f.closest_gap_m[1] == doctest::Approx(0.1 - kD).epsilon(1e-9));
    // Left behind: never nearer than at the start.
    CHECK(f.closest_gap_m[2] == doctest::Approx(std::hypot(0.3, 0.05) - kD).epsilon(1e-9));
    // Not reached: nearest where the cue ball stops.
    CHECK(f.closest_gap_m[3] == doctest::Approx(dist(f.rest[0], {1.2, -0.3}) - kD).epsilon(1e-9));

    CHECK(f.cue_travel_m == doctest::Approx(f.rest[0].x + 1.0).epsilon(1e-9));
    CHECK(f.object_travel_m == 0.0);
    for (int i = 1; i < 4; ++i) CHECK(same(f.rest[std::size_t(i)], s.balls[std::size_t(i)].pos));
}

TEST_CASE("a straight pot into a middle pocket") {
    const int pocket = pocket_named("middle_left");
    REQUIRE(pocket >= 0);
    const Vec2 mouth = sc::rig().table.pockets[std::size_t(pocket)].drop_centre;
    // Stun from close range, so the cue ball does not follow the red in.
    const FrameState s = sc::frame({sc::ball(Ball::cue, mouth.x, 0.3), sc::ball(Ball::red, mouth.x, 0.6),
                                    sc::ball(Ball::black, 1.0, 0.0)});
    phys::CueStrike c = sc::strike(sc::aim_at(s, mouth), 1.2);
    c.tip_y = -0.25;
    const phys::ShotRecord r = sc::simulate(s, c);
    const ShotFacts f = see(s, r);

    CHECK(f.first_hit == std::vector<int>{1});
    REQUIRE(f.pots.size() == 1);
    CHECK(f.pots[0].ball == 1);
    CHECK(f.pots[0].pocket == pocket);
    CHECK(f.pots[0].t == first_event_t(r, phys::EventKind::pocket, 1));
    CHECK(f.pots[0].t > f.first_hit_t);
    CHECK_FALSE(f.cue_potted);
    CHECK(f.cue_pocket == -1);
    CHECK(f.pot_attempt);
    CHECK_FALSE(f.push);
    CHECK_FALSE(f.anomaly);
    // A potted ball's entry is where it started.
    CHECK(same(f.rest[1], s.balls[1].pos));
    CHECK(same(f.rest[0], final_xy(r, 0)));
    CHECK(same(f.rest[2], s.balls[2].pos));
    CHECK(f.closest_gap_m[1] == 0.0);
    // The red is the only object ball that moved: from its place to the middle of the pocket.
    CHECK(f.object_travel_m == doctest::Approx(mouth.y - 0.6).epsilon(1e-12));
}

TEST_CASE("a straight pot into a corner pocket") {
    const int pocket = pocket_named("top_left");
    REQUIRE(pocket >= 0);
    const Vec2 mouth = sc::rig().table.pockets[std::size_t(pocket)].drop_centre;
    const double diagonal = std::sqrt(0.5);
    const Vec2 red{mouth.x - 0.4 * diagonal, mouth.y - 0.4 * diagonal};
    const Vec2 cue{mouth.x - 0.7 * diagonal, mouth.y - 0.7 * diagonal};
    // The cue ball is listed last: places are the position's, whatever the order.
    const FrameState s = sc::frame({sc::ball(Ball::red, red.x, red.y), sc::ball(Ball::blue, 0.0, 0.0),
                                    sc::ball(Ball::cue, cue.x, cue.y)}, 2);
    phys::CueStrike c = sc::strike(sc::aim_at(s, mouth), 1.2);
    c.tip_y = -0.25;
    const phys::ShotRecord r = sc::simulate(s, c);
    const ShotFacts f = see(s, r);

    CHECK(f.first_hit == std::vector<int>{0});
    REQUIRE(f.pots.size() == 1);
    CHECK(f.pots[0].ball == 0);
    CHECK(f.pots[0].pocket == pocket);
    CHECK(f.pots[0].t > f.first_hit_t);
    CHECK_FALSE(f.cue_potted);
    CHECK(f.pot_attempt);
    CHECK(same(f.rest[0], s.balls[0].pos));
    CHECK(same(f.rest[2], final_xy(r, 2)));
    CHECK(f.closest_gap_m[2] == 0.0);
    CHECK(f.closest_gap_m[0] == 0.0);
    CHECK(f.closest_gap_m[1] > 0.5);
    CHECK(f.object_travel_m == doctest::Approx(0.4).epsilon(1e-9));
}

TEST_CASE("an in-off with nothing hit: the cue ball's pocket and how far it went") {
    const int pocket = pocket_named("middle_left");
    REQUIRE(pocket >= 0);
    const phys::Pocket& mouth = sc::rig().table.pockets[std::size_t(pocket)];
    const FrameState s = sc::frame({sc::ball(Ball::cue, mouth.drop_centre.x, 0.1), sc::ball(Ball::red, 0.5, 0.3)});
    const phys::ShotRecord r = sc::simulate(s, sc::strike(kPi / 2.0, 1.0));
    const ShotFacts f = see(s, r);

    CHECK(f.cue_potted);
    CHECK(f.cue_pocket == pocket);
    CHECK(f.first_hit.empty());
    CHECK(f.pots.empty());
    CHECK_FALSE(f.pot_attempt);
    CHECK_FALSE(f.anomaly);
    // In hand afterwards: its entry is where it started, like any potted ball's.
    CHECK(same(f.rest[0], s.balls[0].pos));
    // Up the pocket's own line until its centre crosses the drop circle. Rolling across the nap
    // it drifts a fraction of a millimetre towards the top on the way.
    CHECK(f.cue_travel_m == doctest::Approx(mouth.drop_centre.y - mouth.drop_radius - 0.1).epsilon(1e-5));
    // It passed the red 0.5 m to the side.
    CHECK(f.closest_gap_m[1] == doctest::Approx(0.5 - kD).epsilon(1e-9));
    CHECK(f.object_travel_m == 0.0);
}

TEST_CASE("an in-off behind a potted red: both pockets, the red's first") {
    const int pocket = pocket_named("middle_right");
    REQUIRE(pocket >= 0);
    const Vec2 mouth = sc::rig().table.pockets[std::size_t(pocket)].drop_centre;
    const FrameState s = sc::frame({sc::ball(Ball::cue, mouth.x, 0.0), sc::ball(Ball::red, mouth.x, -0.6)});
    phys::CueStrike c = sc::strike(sc::aim_at(s, mouth), 1.5);
    c.tip_y = 0.3;
    const phys::ShotRecord r = sc::simulate(s, c);
    const ShotFacts f = see(s, r);

    CHECK(f.first_hit == std::vector<int>{1});
    REQUIRE(f.pots.size() == 1);
    CHECK(f.pots[0].ball == 1);
    CHECK(f.pots[0].pocket == pocket);
    CHECK(f.cue_potted);
    CHECK(f.cue_pocket == pocket);
    CHECK(f.pots[0].t < first_event_t(r, phys::EventKind::pocket, 0));
    CHECK(f.pot_attempt);
    CHECK(same(f.rest[0], s.balls[0].pos));
    CHECK(same(f.rest[1], s.balls[1].pos));
    CHECK(f.object_travel_m == doctest::Approx(-0.6 - mouth.y).epsilon(1e-12));
}

TEST_CASE("a gap is measured only while both balls are on the table") {
    // No strike: the cue ball rolls into the middle pocket and a red rolls in after it, 0.3 m
    // behind at the same pace. The red ends where the cue ball dropped, but was never nearer
    // than 0.3 m to it on the table.
    const int pocket = pocket_named("middle_left");
    REQUIRE(pocket >= 0);
    const double x = sc::rig().table.pockets[std::size_t(pocket)].drop_centre.x;
    const FrameState s = sc::frame({sc::ball(Ball::cue, x, 0.5), sc::ball(Ball::red, x, 0.2)});
    phys::SimInput in;
    in.start = sc::table_state(s);
    in.has_strike = false;
    for (phys::BallState& b : in.start.balls) {
        b.v = {0.0, 1.0, 0.0};
        b.w = {-1.0 / ofs::table::kBallRadius, 0.0, 0.0};
    }
    const phys::ShotRecord r = phys::simulate(in, sc::rig().table, sc::rig().params);
    const ShotFacts f = see(s, r);

    REQUIRE(f.cue_potted);
    REQUIRE(f.pots.size() == 1);
    CHECK(f.pots[0].ball == 1);
    CHECK(f.pots[0].pocket == pocket);
    CHECK(f.pots[0].t > first_event_t(r, phys::EventKind::pocket, 0));
    CHECK(f.closest_gap_m[1] == doctest::Approx(0.3 - kD).epsilon(1e-9));
    CHECK(f.first_hit.empty());
    CHECK_FALSE(f.push);
    CHECK_FALSE(f.pot_attempt);
    CHECK_FALSE(f.anomaly);
    CHECK(same(f.rest[0], s.balls[0].pos));
    CHECK(same(f.rest[1], s.balls[1].pos));
}

TEST_CASE("the cue ball's travel off a cushion and back") {
    // Square on to the top cushion: out to where the ball touches it, and back along the same line.
    const FrameState s = sc::frame({sc::ball(Ball::cue, 0.5, 0.2), sc::ball(Ball::red, -1.0, -0.5)});
    const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 0.8));
    const ShotFacts f = see(s, r);

    const double contact_x = ofs::table::top_cushion_x() - ofs::table::kBallRadius;
    REQUIRE(count_events(r, phys::EventKind::ball_line, 0) == 1);
    REQUIRE(f.rest[0].x < contact_x - 0.05);
    CHECK(f.rest[0].y == doctest::Approx(0.2).epsilon(1e-9));
    CHECK(f.cue_travel_m == doctest::Approx((contact_x - 0.5) + (contact_x - f.rest[0].x)).epsilon(1e-9));
    CHECK(f.cue_travel_m > dist(f.rest[0], s.balls[0].pos) + 0.1);
    CHECK(f.first_hit.empty());
    CHECK(f.object_travel_m == 0.0);
}

TEST_CASE("object travel is each ball's start to rest, a potted ball's to its pocket, summed") {
    const int pocket = pocket_named("middle_left");
    REQUIRE(pocket >= 0);
    const Vec2 mouth = sc::rig().table.pockets[std::size_t(pocket)].drop_centre;
    // The cue ball pots the first red and screws back into the second.
    const FrameState s = sc::frame({sc::ball(Ball::cue, mouth.x, 0.45), sc::ball(Ball::red, mouth.x, 0.6),
                                    sc::ball(Ball::red, mouth.x, 0.2, 1), sc::ball(Ball::green, -1.2, 0.3)});
    phys::CueStrike c = sc::strike(sc::aim_at(s, mouth), 2.0);
    c.tip_y = -0.45;
    const phys::ShotRecord r = sc::simulate(s, c);
    const ShotFacts f = see(s, r);

    REQUIRE(f.pots.size() == 1);
    CHECK(f.pots[0].ball == 1);
    REQUIRE(cue_touched(r, 0, 2));
    const double second = dist(final_xy(r, 2), s.balls[2].pos);
    REQUIRE(second > 0.01);
    CHECK(f.object_travel_m == doctest::Approx((mouth.y - 0.6) + second).epsilon(1e-12));
    CHECK(f.first_hit == std::vector<int>{1});
    CHECK(f.closest_gap_m[2] == 0.0);
    CHECK(same(f.rest[3], s.balls[3].pos));
    // Out to the first red and back past its start: the way out is covered twice.
    REQUIRE(f.rest[0].y < 0.45);
    CHECK(f.cue_travel_m > dist(f.rest[0], s.balls[0].pos) + 2.0 * (0.15 - kD) - 1e-6);
    CHECK(f.cue_travel_m == doctest::Approx(sample(r, 0, 5e-4).travel).epsilon(1e-5));
}

TEST_CASE("gaps and travel agree with a brute-force reading of the record") {
    // The solved closest approach is never above the sampled one and within the sampling's own
    // error of it; the solved path length likewise against the polyline.
    struct Shot {
        double aim;
        double speed;
        double tip_x;
        double tip_y;
        double elevation;
    };
    int checked = 0;
    int both_moving = 0;  // gaps to balls that another ball set moving
    int wandering = 0;    // cue-ball paths that were not one straight line
    const auto check = [&](const FrameState& s, const Shot& shot) {
        CAPTURE(shot.aim);
        CAPTURE(shot.speed);
        phys::CueStrike c = sc::strike(shot.aim, shot.speed);
        c.tip_x = shot.tip_x;
        c.tip_y = shot.tip_y;
        c.elevation_rad = shot.elevation;
        const phys::ShotRecord r = sc::simulate(s, c, 7);
        REQUIRE(r.anomalies.empty());
        const ShotFacts f = see(s, r);
        const Sampled sampled = sample(r, 0, 5e-4);
        REQUIRE(f.closest_gap_m.size() == s.balls.size());
        for (int i = 1; i < int(s.balls.size()); ++i) {
            CAPTURE(i);
            const double got = f.closest_gap_m[std::size_t(i)];
            if (cue_touched(r, 0, i)) {
                CHECK(got == 0.0);
                continue;
            }
            CHECK(got <= sampled.gap[std::size_t(i)] + 1e-9);
            CHECK(got >= sampled.gap[std::size_t(i)] - 2e-5);
            ++checked;
            if (r.segments[std::size_t(i)].size() > 1) ++both_moving;
        }
        CHECK(f.cue_travel_m >= sampled.travel - 1e-9);
        CHECK(f.cue_travel_m <= sampled.travel + 1e-5);
        if (f.cue_travel_m > dist(f.rest[0], s.balls[0].pos) + 0.05) ++wandering;
    };

    SUBCASE("scattered balls: swerve, cushions and cannons") {
        const FrameState s = sc::frame(
            {sc::ball(Ball::cue, -0.9, -0.35), sc::ball(Ball::red, 0.35, 0.12), sc::ball(Ball::red, 0.9, -0.5, 1),
             sc::ball(Ball::red, -0.3, 0.55, 2), sc::ball(Ball::red, 1.45, 0.6, 3), sc::ball(Ball::yellow, -1.2, 0.4),
             sc::ball(Ball::blue, 0.0, -0.05), sc::ball(Ball::pink, 0.75, 0.3), sc::ball(Ball::black, 1.4, -0.1)});
        const Shot shots[] = {
            {0.10, 2.0, 0.0, 0.0, 0.0},   {0.62, 4.5, 0.35, 0.1, 0.0},  {-0.45, 6.0, -0.3, 0.2, 0.0},
            {1.35, 3.0, 0.4, -0.2, 0.35}, {2.60, 5.0, -0.4, 0.0, 0.5},  {0.36, 1.2, 0.0, -0.4, 0.0},
            {-1.20, 6.5, 0.2, 0.3, 0.1},  {3.05, 2.5, 0.45, 0.0, 0.25}, {0.02, 0.6, 0.0, 0.0, 0.0},
            {0.78, 6.5, 0.0, 0.45, 0.0},
        };
        for (const Shot& shot : shots) check(s, shot);
        CHECK(checked >= 40);
        CHECK(wandering >= 5);
    }
    SUBCASE("a loose pack: balls the cue ball never touched, set moving by others") {
        // Ten reds a millimetre apart in a triangle, colours around them.
        std::vector<BallOnTable> balls = {sc::ball(Ball::cue, -0.6, 0.1)};
        const double pitch = kD + 0.001;
        const double row_step = pitch * std::sqrt(0.75);
        int index = 0;
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k <= row; ++k) {
                balls.push_back(sc::ball(Ball::red, 0.5 + row * row_step, (k - 0.5 * row) * pitch, index++));
            }
        }
        balls.push_back(sc::ball(Ball::blue, 0.0, -0.3));
        balls.push_back(sc::ball(Ball::black, 1.45, 0.0));
        balls.push_back(sc::ball(Ball::green, -1.0, 0.3));
        const FrameState s = sc::frame(std::move(balls));
        const Shot shots[] = {
            {sc::aim_at_ball(s, 1), 4.0, 0.0, 0.0, 0.0},
            {sc::aim_at_ball(s, 2) + 0.01, 6.5, 0.2, 0.2, 0.0},
            {sc::aim_at_ball(s, 7) - 0.015, 3.0, -0.3, -0.2, 0.0},
            {sc::aim_at_ball(s, 10) + 0.02, 5.5, 0.0, 0.3, 0.1},
            {sc::aim_at_ball(s, 4), 1.5, 0.0, 0.0, 0.0},
        };
        for (const Shot& shot : shots) check(s, shot);
        CHECK(both_moving >= 20);
    }
    SUBCASE("a break-off") {
        const phys::TableState rack = phys::opening_rack(3, sc::rig().params);
        std::vector<BallOnTable> balls;
        for (const phys::BallState& b : rack.balls) balls.push_back({b.ball, b.index, {b.r.x, b.r.y}});
        const FrameState s = sc::frame(std::move(balls));
        check(s, {sc::aim_at_ball(s, sc::place_of(s, Ball::red, 14)), 6.5, 0.0, 0.1, 0.05});
        check(s, {sc::aim_at_ball(s, sc::place_of(s, Ball::pink)), 6.5, 0.0, 0.0, 0.0});
        CHECK(both_moving >= 10);
    }
    MESSAGE("gaps checked: " << checked << ", to moving balls: " << both_moving << ", wandering paths: " << wandering);
}

TEST_CASE("a cue ball that stops and comes back inside one slide is measured both ways") {
    // A raised cue under the middle of the ball: it goes forward a little, the backspin stops it
    // and brings it back, all in one sliding segment whose speed passes through zero.
    const FrameState s = sc::frame({sc::ball(Ball::cue, -0.4, 0.0), sc::ball(Ball::red, 1.2, 0.5)});
    phys::CueStrike c = sc::strike(0.0, 3.0);
    c.tip_y = -0.45;
    c.elevation_rad = 1.3;
    const phys::ShotRecord r = sc::simulate(s, c);
    const ShotFacts f = see(s, r);

    REQUIRE(r.segments[0].front().start.motion == phys::Motion::sliding);
    REQUIRE(r.segments[0].front().start.v.x > 0.5);
    double furthest = -1.0;
    for (double t = 0.0; t < r.segments[0][1].t0; t += 1e-4) furthest = std::max(furthest, r.ball_at(0, t).r.x);
    REQUIRE(furthest > -0.4 + 0.1);
    REQUIRE(r.ball_at(0, r.segments[0][1].t0).r.x < furthest - 0.01);
    REQUIRE(count_events(r, phys::EventKind::ball_line, 0) == 0);
    // Along the x axis all the way: out to the turn, back to it, and on to where it rests.
    CHECK(f.cue_travel_m == doctest::Approx(2.0 * (furthest + 0.4) + (-0.4 - f.rest[0].x)).epsilon(1e-6));
    CHECK(f.cue_travel_m == doctest::Approx(sample(r, 0, 1e-4).travel).epsilon(1e-5));
    CHECK(f.first_hit.empty());
}
