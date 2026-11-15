#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "ofs/rng.hpp"
#include "scenario.hpp"

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

constexpr double kD = table::kBallDiameter;

// A frame's 22 balls anywhere on the table, none overlapping: the cue ball at place 0, reds at
// 1 to 15, then yellow to black.
std::vector<BallOnTable> scattered(Rng& rng) {
    std::vector<BallOnTable> balls;
    const Ball colours[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};
    while (balls.size() < 22) {
        const Vec2 p{rng.uniform(-1.7, 1.7), rng.uniform(-0.83, 0.83)};
        bool room = true;
        for (const BallOnTable& b : balls) room = room && std::hypot(p.x - b.pos.x, p.y - b.pos.y) > kD + 1.0e-4;
        if (!room) continue;
        const int n = int(balls.size());
        balls.push_back(n == 0 ? ball(Ball::cue, p.x, p.y) : n <= 15 ? ball(Ball::red, p.x, p.y, n - 1)
                                                                      : ball(colours[n - 16], p.x, p.y));
    }
    return balls;
}

// The reds racked, with the six colours in a row across the table in front of them 100 mm apart:
// no path from the D to any red passes between two of them.
std::vector<BallOnTable> walled_pack() {
    std::vector<BallOnTable> balls;
    for (const table::BallPlacement& p : table::opening_layout(1e-4)) {
        if (p.ball == Ball::cue || p.ball == Ball::red) balls.push_back({p.ball, p.index, p.position});
    }
    const Ball colours[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};
    for (int i = 0; i < 6; ++i) balls.push_back(ball(colours[i], table::spot(Ball::pink).x - 0.1, -0.25 + 0.1 * i));
    return balls;
}

// Nanoseconds per call: the best of several trials, the machine being shared.
template <typename Body>
double time_ns(int reps, Body&& body) {
    double best = 1e300;
    for (int trial = 0; trial < 5; ++trial) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < reps; ++i) body();
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / reps);
    }
    return best;
}

struct Stat {
    double sum = 0.0;
    double worst = 0.0;
    int n = 0;
    void add(double ns) {
        sum += ns;
        worst = std::max(worst, ns);
        ++n;
    }
    void print(const char* name) const {
        std::printf("%-44s mean %9.2f us   worst %9.2f us\n", name, sum / n / 1000.0, worst / 1000.0);
    }
};

}  // namespace

TEST_CASE("geometry oracle timings" * doctest::skip()) {
    const TableOracle& o = *rig().oracle;
    Rng rng(147);
    int sink = 0;

    const std::vector<int> reds = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const std::vector<int> colours = {16, 17, 18, 19, 20, 21};
    const std::vector<int> one = {16};
    std::vector<int> all = reds;
    all.insert(all.end(), colours.begin(), colours.end());

    struct OnSet {
        const char* name;
        const std::vector<int>* on;
    };
    const OnSet sets[] = {{"reds", &reds}, {"one colour", &one}, {"every colour", &colours}, {"every ball", &all}};
    Stat snookered[4], snookering[4], clear[4], full[4], hit[4], in_hand[4];
    Stat touching, legal, nearest, spot;
    for (int layout = 0; layout < 300; ++layout) {
        const auto balls = scattered(rng);
        for (int s = 0; s < 4; ++s) {
            const std::vector<int>& on = *sets[s].on;
            snookered[s].add(time_ns(20, [&] { sink += o.snookered(balls, 0, on, false); }));
            snookering[s].add(time_ns(20, [&] { sink += int(o.snookering_balls(balls, 0, on).size()); }));
            clear[s].add(time_ns(20, [&] { sink += o.clear_path(balls, 0, on); }));
            full[s].add(time_ns(20, [&] { sink += o.full_ball(balls, 0, on); }));
            hit[s].add(time_ns(5, [&] { sink += o.can_hit(balls, 0, on); }));
            in_hand[s].add(time_ns(2, [&] { sink += o.snookered(balls, 0, on, true); }));
        }
        touching.add(time_ns(20, [&] { sink += int(o.touching(balls, 0).size()); }));
        const Vec2 wanted{rng.uniform(-1.5, -0.9), rng.uniform(-0.4, 0.4)};
        legal.add(time_ns(20, [&] { sink += o.cue_placement_legal(balls, 0, wanted); }));
        nearest.add(time_ns(20, [&] { sink += o.nearest_cue_placement(balls, 0, wanted).x > 0.0; }));
        spot.add(time_ns(20, [&] { sink += int(o.spot(balls, {Ball::black, Ball::pink, Ball::blue}).size()); }));
    }
    std::printf("22 balls scattered, 300 layouts\n");
    for (int s = 0; s < 4; ++s) {
        std::printf("on: %s\n", sets[s].name);
        snookered[s].print("  snookered");
        snookering[s].print("  snookering_balls");
        clear[s].print("  clear_path");
        full[s].print("  full_ball");
        hit[s].print("  can_hit");
        in_hand[s].print("  snookered in hand");
    }
    touching.print("touching");
    legal.print("cue_placement_legal");
    nearest.print("nearest_cue_placement");
    spot.print("spot, three colours");

    // Wanted on top of a ball, with a dozen balls in the D to get clear of.
    Stat s;
    std::vector<BallOnTable> crowd = {ball(Ball::cue, 0.5, 0.5)};
    while (crowd.size() < 13) {
        const Vec2 p{table::d_centre().x - rng.uniform(0.0, 0.29), rng.uniform(-0.29, 0.29)};
        bool room = true;
        for (const BallOnTable& b : crowd) room = room && std::hypot(p.x - b.pos.x, p.y - b.pos.y) > kD + 1.0e-4;
        if (room) crowd.push_back(ball(Ball::red, p.x, p.y, int(crowd.size()) - 1));
    }
    for (std::size_t i = 1; i < crowd.size(); ++i) {
        s.add(time_ns(20, [&] { sink += o.nearest_cue_placement(crowd, 0, crowd[i].pos).x > 0.0; }));
    }
    s.print("nearest_cue_placement, 12 balls in the D");

    // The searches that cannot stop early.
    s = {};
    const auto pack = walled_pack();
    s.add(time_ns(5, [&] { sink += o.snookered(pack, 0, reds, true); }));
    s.print("in hand, 15 reds behind six colours");
    CHECK(o.snookered(pack, 0, reds, true));

    s = {};
    s.add(time_ns(5, [&] { sink += o.snookered(pack, 0, {1}, true); }));
    s.print("in hand, one of those reds on");

    s = {};
    std::vector<BallOnTable> ring = {ball(Ball::cue, -0.8, -0.3), ball(Ball::red, 0.4, 0.2)};
    const Ball ring_colours[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};
    for (int i = 0; i < 6; ++i) {
        const double a = i * 3.14159265358979 / 3.0;
        ring.push_back(ball(ring_colours[i], 0.4 + (kD + 0.001) * std::cos(a), 0.2 + (kD + 0.001) * std::sin(a)));
    }
    s.add(time_ns(20, [&] { sink += o.can_hit(ring, 0, {1}); }));
    s.print("can_hit, the last red walled in (false)");
    CHECK_FALSE(o.can_hit(ring, 0, {1}));

    s = {};
    for (int i = 0; i < 14; ++i) ring.push_back(ball(Ball::red, -1.5 + 0.2 * i, -0.7, i + 1));
    s.add(time_ns(20, [&] { sink += o.can_hit(ring, 0, {1}); }));
    s.print("can_hit, the same with 14 more balls about");

    s = {};
    std::vector<BallOnTable> opening;
    for (const table::BallPlacement& p : table::opening_layout(1e-4)) opening.push_back({p.ball, p.index, p.position});
    opening[0].pos = {table::spot(Ball::brown).x - kD - 0.001, 0.0};
    s.add(time_ns(20, [&] { sink += o.can_hit(opening, 0, reds); }));
    s.print("can_hit, tight behind the brown at the start");
    std::printf("  (%s)\n", o.can_hit(opening, 0, reds) ? "a route" : "no route");

    s = {};
    auto tucked = pack;
    tucked[0].pos = {table::spot(Ball::pink).x - 0.1 - kD - 0.001, 0.05};
    s.add(time_ns(20, [&] { sink += o.can_hit(tucked, 0, reds); }));
    s.print("can_hit, tight behind the row of colours");
    std::printf("  (%s) [sink %d]\n", o.can_hit(tucked, 0, reds) ? "a route" : "no route", sink);
}
