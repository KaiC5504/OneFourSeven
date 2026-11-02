#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "ofs/physics/rack.hpp"

using namespace ofs::phys;

namespace {

constexpr double kR = ofs::table::kBallRadius;
constexpr std::size_t kFirstRed = 1;
constexpr std::size_t kPink = 1 + 15 + 4;

double dist(const BallState& a, const BallState& b) { return std::hypot(a.r.x - b.r.x, a.r.y - b.r.y); }

// Neighbouring reds by the triangle's lattice: those one spacing apart in the touching layout.
std::vector<std::pair<std::size_t, std::size_t>> red_neighbours() {
    const std::vector<ofs::table::BallPlacement> layout = ofs::table::opening_layout(0.0);
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (std::size_t i = kFirstRed; i < kFirstRed + 15; ++i) {
        for (std::size_t j = i + 1; j < kFirstRed + 15; ++j) {
            const double d = std::hypot(layout[i].position.x - layout[j].position.x,
                                        layout[i].position.y - layout[j].position.y);
            if (d < 2.0 * kR * 1.01) out.emplace_back(i, j);
        }
    }
    return out;
}

}  // namespace

TEST_CASE("rack has the opening layout's balls in order, colours and cue ball on their spots") {
    const PhysicsParams p;
    const TableState s = opening_rack(147, p);
    const std::vector<ofs::table::BallPlacement> layout = ofs::table::opening_layout(0.0);
    REQUIRE(s.balls.size() == layout.size());
    for (std::size_t i = 0; i < layout.size(); ++i) {
        CAPTURE(i);
        CHECK(s.balls[i].ball == layout[i].ball);
        CHECK(s.balls[i].index == layout[i].index);
        CHECK(s.balls[i].r.z == 0.0);
        CHECK(s.balls[i].v == ofs::Vec3{});
        CHECK(s.balls[i].w == ofs::Vec3{});
        CHECK(s.balls[i].motion == Motion::stationary);
        CHECK(s.balls[i].pocket == -1);
        if (layout[i].ball != ofs::table::Ball::red) {
            CHECK(s.balls[i].r.x == layout[i].position.x);
            CHECK(s.balls[i].r.y == layout[i].position.y);
        }
    }
}

TEST_CASE("rack gaps between neighbouring reds stay in range and nothing touches, over 1000 seeds") {
    const PhysicsParams p;
    const auto neighbours = red_neighbours();
    REQUIRE(neighbours.size() == 30);
    double lowest = 1.0;
    double highest = 0.0;
    for (std::uint64_t seed = 0; seed < 1000; ++seed) {
        CAPTURE(seed);
        const TableState s = opening_rack(seed, p);
        for (const auto& [i, j] : neighbours) {
            const double gap = dist(s.balls[i], s.balls[j]) - 2.0 * kR;
            lowest = std::min(lowest, gap);
            highest = std::max(highest, gap);
            CHECK(gap >= p.rack_gap_min_m);
            CHECK(gap <= p.rack_gap_max_m);
        }
        for (std::size_t i = 0; i < s.balls.size(); ++i) {
            for (std::size_t j = i + 1; j < s.balls.size(); ++j) {
                if (dist(s.balls[i], s.balls[j]) <= 2.0 * kR) FAIL_CHECK("balls " << i << " and " << j << " touch");
            }
        }
        // Rules 3.2(a)(i), 2.18: the apex is close to the pink but never touches it.
        const double apex_gap = dist(s.balls[kFirstRed], s.balls[kPink]) - 2.0 * kR;
        CHECK(apex_gap >= p.rack_gap_min_m);
        CHECK(apex_gap <= p.rack_gap_max_m);
    }
    // The gaps use most of the range rather than collapsing to one value.
    const double range = p.rack_gap_max_m - p.rack_gap_min_m;
    CHECK(lowest < p.rack_gap_min_m + 0.25 * range);
    CHECK(highest > p.rack_gap_max_m - 0.25 * range);
}

TEST_CASE("rack gaps within one rack differ, so pack collisions come in a defined order") {
    const PhysicsParams p;
    const auto neighbours = red_neighbours();
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        CAPTURE(seed);
        const TableState s = opening_rack(seed, p);
        std::vector<double> gaps;
        for (const auto& [i, j] : neighbours) gaps.push_back(dist(s.balls[i], s.balls[j]));
        std::sort(gaps.begin(), gaps.end());
        CHECK(std::adjacent_find(gaps.begin(), gaps.end()) == gaps.end());
    }
}

TEST_CASE("rack is deterministic per seed and differs between seeds") {
    const PhysicsParams p;
    const TableState a = opening_rack(147, p);
    const TableState b = opening_rack(147, p);
    const TableState c = opening_rack(148, p);
    bool any_differs = false;
    for (std::size_t i = 0; i < a.balls.size(); ++i) {
        CHECK(a.balls[i].r == b.balls[i].r);
        any_differs = any_differs || a.balls[i].r != c.balls[i].r;
    }
    CHECK(any_differs);
}

TEST_CASE("rack follows the gap parameters") {
    PhysicsParams p;
    p.rack_gap_min_m = 3e-4;
    p.rack_gap_max_m = 6e-4;
    const auto neighbours = red_neighbours();
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        const TableState s = opening_rack(seed, p);
        for (const auto& [i, j] : neighbours) {
            const double gap = dist(s.balls[i], s.balls[j]) - 2.0 * kR;
            CHECK(gap >= 3e-4);
            CHECK(gap <= 6e-4);
        }
    }
    p.rack_gap_min_m = 5e-5;
    p.rack_gap_max_m = 5e-5;
    const TableState fixed = opening_rack(9, p);
    for (const auto& [i, j] : neighbours) {
        CHECK(dist(fixed.balls[i], fixed.balls[j]) - 2.0 * kR == doctest::Approx(5e-5).epsilon(1e-9));
    }
}
