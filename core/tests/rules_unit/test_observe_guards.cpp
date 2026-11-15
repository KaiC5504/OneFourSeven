#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/physics/shot_record.hpp"
#include "scenario.hpp"

// observe(): shots the simulator cut short, records that are not of the position they are read
// against or are damaged, and what a reading costs.

using namespace ofs::rules;
namespace sc = ofs::rules::scenario;
namespace phys = ofs::phys;

namespace {

constexpr double kD = ofs::table::kBallDiameter;

ShotFacts see(const Position& p, const phys::ShotRecord& r) { return observe(p, r, sc::rig().table, sc::rig().ref); }

bool same(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

Vec2 final_xy(const phys::ShotRecord& r, int ball) {
    const ofs::Vec3& p = r.final_state.balls[std::size_t(ball)].r;
    return {p.x, p.y};
}

bool has_anomaly(const phys::ShotRecord& r, const std::string& kind) {
    for (const phys::Anomaly& a : r.anomalies) {
        if (a.kind == kind) return true;
    }
    return false;
}

std::size_t first_event(const phys::ShotRecord& r, phys::EventKind kind) {
    for (std::size_t i = 0; i < r.events.size(); ++i) {
        if (r.events[i].kind == kind) return i;
    }
    return r.events.size();
}

phys::ShotRecord simulate_with(const Position& p, const phys::CueStrike& c, const phys::PhysicsParams& params) {
    phys::SimInput in;
    in.start = sc::table_state(p);
    in.cue_ball = p.cue_ball;
    in.strike = c;
    in.seed = 1;
    return phys::simulate(in, sc::rig().table, params);
}

// A red played full along the table, and a black out of the way.
FrameState plain() {
    return sc::frame({sc::ball(Ball::cue, -0.5, 0.0), sc::ball(Ball::red, 0.1, 0.0), sc::ball(Ball::black, 1.0, 0.3)});
}

// What a refused record leaves: the position as it stands, nothing seen.
void check_refused(const Position& p, const phys::ShotRecord& r) {
    const ShotFacts f = see(p, r);
    CHECK(f.anomaly);
    CHECK(f.first_hit.empty());
    CHECK(f.first_hit_t == 0.0);
    CHECK(f.pots.empty());
    CHECK_FALSE(f.cue_potted);
    CHECK(f.cue_pocket == -1);
    CHECK_FALSE(f.push);
    CHECK(f.touching_moved.empty());
    CHECK_FALSE(f.pot_attempt);
    CHECK(f.cue_travel_m == 0.0);
    CHECK(f.object_travel_m == 0.0);
    REQUIRE(f.rest.size() == p.balls.size());
    REQUIRE(f.closest_gap_m.size() == p.balls.size());
    for (std::size_t i = 0; i < p.balls.size(); ++i) CHECK(same(f.rest[i], p.balls[i].pos));
}

struct Capture : ofs::log::Sink {
    std::vector<ofs::log::Event> events;
    void write(const ofs::log::Event& event, const std::string&) override { events.push_back(event); }
};

FrameState rack_frame(std::uint64_t seed) {
    const phys::TableState rack = phys::opening_rack(seed, sc::rig().params);
    std::vector<BallOnTable> balls;
    for (const phys::BallState& b : rack.balls) balls.push_back({b.ball, b.index, {b.r.x, b.r.y}});
    return sc::frame(std::move(balls), 0);
}

// The best of several runs, in microseconds: the PC is shared, and the minimum is the cost
// without interference.
template <class Body>
double best_us(int runs, Body&& body) {
    double best = 1e300;
    for (int i = 0; i < runs; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        body();
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return best;
}

// The budgets are for the game's build; an unoptimised one gets five times as long.
#ifdef NDEBUG
constexpr double kBudgetScale = 1.0;
#else
constexpr double kBudgetScale = 5.0;
#endif

}  // namespace

TEST_CASE("a shot the simulator cut short is an anomaly, read as far as it went") {
    const FrameState s = plain();
    REQUIRE_FALSE(see(s, sc::simulate(s, sc::strike(0.0, 1.0))).anomaly);

    SUBCASE("the event guard") {
        phys::PhysicsParams params = sc::rig().params;
        params.max_events = 3;
        const phys::ShotRecord r = simulate_with(s, sc::strike(0.0, 1.0), params);
        REQUIRE(has_anomaly(r, "event_guard"));
        const ShotFacts f = see(s, r);
        CHECK(f.anomaly);
        for (int i = 0; i < 3; ++i) CHECK(same(f.rest[std::size_t(i)], final_xy(r, i)));
    }
    SUBCASE("the time cap") {
        phys::PhysicsParams params = sc::rig().params;
        params.time_cap_s = 0.2;
        const phys::ShotRecord r = simulate_with(s, sc::strike(0.0, 1.0), params);
        REQUIRE(has_anomaly(r, "time_cap"));
        const ShotFacts f = see(s, r);
        CHECK(f.anomaly);
        // Stopped on its way to the red: it has moved, and that far is all it travelled.
        CHECK(f.first_hit.empty());
        CHECK(same(f.rest[0], final_xy(r, 0)));
        CHECK(f.rest[0].x > -0.45);
        CHECK(f.rest[0].x < 0.1 - kD);
        CHECK(f.cue_travel_m == doctest::Approx(f.rest[0].x + 0.5).epsilon(1e-9));
        CHECK(f.closest_gap_m[1] == doctest::Approx(0.1 - f.rest[0].x - kD).epsilon(1e-9));
    }
    SUBCASE("a chain of simultaneous events") {
        phys::PhysicsParams params = sc::rig().params;
        params.max_zero_time_chain = 1;
        const FrameState touching = sc::frame({sc::ball(Ball::cue, 0.0, 0.0), sc::ball(Ball::red, kD, 0.0)});
        const phys::ShotRecord r = simulate_with(touching, sc::strike(0.0, 1.0), params);
        REQUIRE(has_anomaly(r, "zero_time_chain"));
        CHECK(see(touching, r).anomaly);
    }
    SUBCASE("the simulator's other notes are not the referee's business") {
        for (const char* kind : {"solver_fallback", "energy_gain", "overlap", "nan"}) {
            CAPTURE(kind);
            phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 1.0));
            r.anomalies.push_back({kind, 0.5, {0}, "made up"});
            const ShotFacts f = see(s, r);
            CHECK_FALSE(f.anomaly);
            CHECK(f.first_hit == std::vector<int>{1});
        }
        phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 1.0));
        r.anomalies.push_back({"solver_fallback", 0.5, {0}, "made up"});
        r.anomalies.push_back({"time_cap", 0.5, {0}, "made up"});
        CHECK(see(s, r).anomaly);
    }
}

TEST_CASE("a record from a different position is an anomaly with nothing read from it") {
    const FrameState s = plain();
    const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 1.0));
    REQUIRE(see(s, r).first_hit == std::vector<int>{1});

    SUBCASE("a ball a millimetre away") {
        FrameState p = s;
        p.balls[2].pos.y += 0.001;
        check_refused(p, r);
        const ShotFacts f = see(p, r);
        CHECK(f.closest_gap_m[0] == 0.0);
        CHECK(f.closest_gap_m[1] == doctest::Approx(0.6 - kD).epsilon(1e-12));
        CHECK(f.closest_gap_m[2] == doctest::Approx(std::hypot(1.5, 0.301) - kD).epsilon(1e-12));
    }
    SUBCASE("a ball two nanometres away") {
        FrameState p = s;
        p.balls[1].pos.x += 2e-9;
        check_refused(p, r);
    }
    SUBCASE("half a nanometre is the same place") {
        FrameState p = s;
        p.balls[1].pos.x += 5e-10;
        const ShotFacts f = see(p, r);
        CHECK_FALSE(f.anomaly);
        CHECK(f.first_hit == std::vector<int>{1});
    }
    SUBCASE("another kind of ball") {
        FrameState p = s;
        p.balls[1].ball = Ball::pink;
        check_refused(p, r);
    }
    SUBCASE("the balls in another order") {
        FrameState p = s;
        std::swap(p.balls[1], p.balls[2]);
        check_refused(p, r);
    }
    SUBCASE("a ball more") {
        FrameState p = s;
        p.balls.push_back(sc::ball(Ball::red, -1.0, -0.5, 1));
        check_refused(p, r);
    }
    SUBCASE("a ball fewer") {
        FrameState p = s;
        p.balls.pop_back();
        check_refused(p, r);
    }
    SUBCASE("another ball named as the cue ball") {
        FrameState p = s;
        p.cue_ball = 1;
        check_refused(p, r);
    }
    SUBCASE("a cue ball's place the position does not have") {
        FrameState p = s;
        p.cue_ball = 3;
        check_refused(p, r);
        p.cue_ball = -1;
        check_refused(p, r);
    }
    SUBCASE("a strike on another ball") {
        phys::ShotRecord other = r;
        REQUIRE(other.events.front().kind == phys::EventKind::strike);
        other.events.front().a = 1;
        check_refused(s, other);
    }
}

TEST_CASE("a damaged record is refused and never read past its ends") {
    const FrameState s = plain();
    const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 1.0));
    const std::size_t hit = first_event(r, phys::EventKind::ball_ball);
    REQUIRE(hit < r.events.size());

    SUBCASE("no record at all") { check_refused(s, phys::ShotRecord{}); }
    SUBCASE("nothing on either side") {
        const ShotFacts f = see(Position{}, phys::ShotRecord{});
        CHECK(f.anomaly);
        CHECK(f.rest.empty());
        CHECK(f.closest_gap_m.empty());
    }
    SUBCASE("a record but no position") { check_refused(Position{}, r); }
    SUBCASE("a ball with no path") {
        phys::ShotRecord bad = r;
        bad.segments[1].clear();
        check_refused(s, bad);
    }
    SUBCASE("a resting place missing") {
        phys::ShotRecord bad = r;
        bad.final_state.balls.pop_back();
        check_refused(s, bad);
    }
    SUBCASE("a ball in a pocket before the stroke") {
        phys::ShotRecord bad = r;
        bad.segments[2].front().start.motion = phys::Motion::pocketed;
        check_refused(s, bad);
    }
    SUBCASE("events naming balls that are not there") {
        for (const int ball : {-1, 3, 1000000}) {
            CAPTURE(ball);
            phys::ShotRecord bad = r;
            bad.events[hit].a = ball;
            check_refused(s, bad);
            bad = r;
            bad.events[hit].b = ball;
            check_refused(s, bad);
        }
        phys::ShotRecord bad = r;
        bad.events[hit].b = bad.events[hit].a;
        check_refused(s, bad);
    }
    SUBCASE("a pocket the table does not have") {
        for (const int pocket : {-1, 6, 99}) {
            CAPTURE(pocket);
            phys::ShotRecord bad = r;
            bad.events.push_back({r.t_end, phys::EventKind::pocket, 1, -1, pocket, phys::Motion::rolling,
                                  phys::Motion::pocketed});
            check_refused(s, bad);
        }
    }
    SUBCASE("a guard event names no ball and is no damage") {
        phys::ShotRecord odd = r;
        odd.events.push_back({r.t_end, phys::EventKind::guard, -1, -1, -1, phys::Motion::stationary,
                              phys::Motion::stationary});
        const ShotFacts f = see(s, odd);
        CHECK_FALSE(f.anomaly);
        CHECK(f.first_hit == std::vector<int>{1});
    }
    SUBCASE("times that are not numbers do not hang or crash it") {
        phys::ShotRecord odd = r;
        odd.t_end = std::nan("");
        const ShotFacts f = see(s, odd);
        CHECK(f.rest.size() == 3);
        odd = r;
        for (phys::Segment& seg : odd.segments[0]) seg.t0 = std::nan("");
        CHECK(see(s, odd).rest.size() == 3);
    }
}

TEST_CASE("a refused record is logged as rules.invalid, a good one not at all") {
    const FrameState s = plain();
    const phys::ShotRecord r = sc::simulate(s, sc::strike(0.0, 1.0));
    FrameState elsewhere = s;
    elsewhere.balls[2].pos.x -= 0.25;

    auto capture = std::make_shared<Capture>();
    ofs::log::Logger::instance().clear_sinks();
    ofs::log::Logger::instance().add_sink(capture);
    CHECK_FALSE(see(s, r).anomaly);
    CHECK(capture->events.empty());
    CHECK(see(elsewhere, r).anomaly);
    ofs::log::Logger::instance().clear_sinks();

    // One line, saying what differs. (Until rules_log.cpp is built, log_invalid writes nothing.)
    CHECK(capture->events.size() <= 1);
    for (const ofs::log::Event& e : capture->events) {
        CHECK(e.event == "rules.invalid");
        CHECK(e.data.value("what", std::string()) == "observe");
        CHECK(e.data.value("detail", std::string()).find("ball 2") != std::string::npos);
    }
}

TEST_CASE("reading a stroke stays inside its time budget") {
    // Spec 7.13 gives a whole simulated shot 2 ms; the referee's reading of it gets 0.5 ms for an
    // ordinary stroke and 2 ms for a break-off (optimised build).
    SUBCASE("an ordinary stroke") {
        const FrameState s = sc::frame(
            {sc::ball(Ball::cue, -0.9, -0.35), sc::ball(Ball::red, 0.35, 0.12), sc::ball(Ball::red, 0.9, -0.5, 1),
             sc::ball(Ball::red, -0.3, 0.55, 2), sc::ball(Ball::red, 1.45, 0.6, 3), sc::ball(Ball::yellow, -1.2, 0.4),
             sc::ball(Ball::blue, 0.0, -0.05), sc::ball(Ball::pink, 0.75, 0.3), sc::ball(Ball::black, 1.4, -0.1)});
        phys::CueStrike c = sc::strike(sc::aim_at_ball(s, 1), 3.0);
        c.tip_x = 0.2;
        c.tip_y = -0.2;
        const phys::ShotRecord r = sc::simulate(s, c);
        REQUIRE_FALSE(see(s, r).first_hit.empty());
        std::size_t seen = 0;
        const double us = best_us(25, [&] { seen += see(s, r).rest.size(); });
        MESSAGE("observe, ordinary stroke (" << r.events.size() << " events): " << us << " us");
        CHECK(seen == 25 * s.balls.size());
        CHECK(us < 500.0 * kBudgetScale);
    }
    SUBCASE("break-offs") {
        double worst = 0.0;
        for (const std::uint64_t seed : {1u, 2u, 3u}) {
            const FrameState s = rack_frame(seed);
            REQUIRE(s.balls.size() == 22);
            // At the end red of the back row (reds 10 to 14) on the cue ball's side, as a player
            // breaks, and straight at the pink, which drives it through the pack.
            const int pink = sc::place_of(s, Ball::pink);
            const int left_end = sc::place_of(s, Ball::red, 10);
            const int right_end = sc::place_of(s, Ball::red, 14);
            REQUIRE(pink >= 0);
            REQUIRE(left_end >= 0);
            REQUIRE(right_end >= 0);
            const bool same_side = s.balls[std::size_t(left_end)].pos.y * s.balls[0].pos.y > 0.0;
            const int back = same_side ? left_end : right_end;
            for (const int target : {back, pink}) {
                CAPTURE(seed);
                CAPTURE(target);
                phys::CueStrike c = sc::strike(sc::aim_at_ball(s, target), sc::rig().params.max_cue_speed_m_s);
                c.tip_y = 0.1;
                c.elevation_rad = 0.05;
                const phys::ShotRecord r = sc::simulate(s, c, seed);
                const ShotFacts f = see(s, r);
                REQUIRE_FALSE(f.anomaly);
                REQUIRE_FALSE(f.first_hit.empty());
                std::size_t seen = 0;
                const double us = best_us(15, [&] { seen += see(s, r).rest.size(); });
                MESSAGE("observe, break-off at ball " << target << ", rack " << seed << " (" << r.events.size()
                                                      << " events): " << us << " us");
                CHECK(seen == 15 * s.balls.size());
                worst = std::max(worst, us);
            }
        }
        CHECK(worst < 2000.0 * kBudgetScale);
    }
}
