// The oracle against slower code written straight from the definitions, over random tables.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "ofs/rng.hpp"
#include "scenario.hpp"

using namespace ofs;
using namespace ofs::rules;
using namespace ofs::rules::scenario;

namespace {

constexpr double kD = table::kBallDiameter;
constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfX = table::kPlayingLength / 2.0 - table::kBallRadius;
constexpr double kHalfY = table::kPlayingWidth / 2.0 - table::kBallRadius;

const TableOracle& oracle() { return *rig().oracle; }

double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// `count` balls, none overlapping, the cue ball at place 0, then reds, then up to six colours.
// They are drawn from a patch `spread` wide about a random point, so that small patches give
// crowded tables where balls hide each other.
std::vector<BallOnTable> scattered(Rng& rng, int count, int reds, double spread) {
    const Ball colours[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};
    const Vec2 mid{rng.uniform(-1.2, 1.2), rng.uniform(-0.5, 0.5)};
    std::vector<BallOnTable> balls;
    while (int(balls.size()) < count) {
        const Vec2 p{mid.x + rng.uniform(-spread, spread), mid.y + rng.uniform(-spread, spread)};
        bool room = std::abs(p.x) <= kHalfX && std::abs(p.y) <= kHalfY;
        for (const BallOnTable& b : balls) room = room && dist(p, b.pos) > kD + 1.0e-4;
        if (!room) continue;
        const int n = int(balls.size());
        balls.push_back(n == 0 ? ball(Ball::cue, p.x, p.y) : n <= reds ? ball(Ball::red, p.x, p.y, n - 1)
                                                                        : ball(colours[n - reds - 1], p.x, p.y));
    }
    return balls;
}

bool is_on(const std::vector<int>& on, int place) { return std::find(on.begin(), on.end(), place) != on.end(); }

double point_to_segment(Vec2 p, Vec2 a, Vec2 b) {
    const double len2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
    const double t = std::clamp(((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / len2, 0.0, 1.0);
    return dist(p, {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)});
}

// S2.17 by the book: the balls not on within a diameter of either straight path from the cue
// ball to where it would touch the ball at `place` at an extreme edge.
std::vector<int> obstructors(const std::vector<BallOnTable>& balls, const std::vector<int>& on, int place) {
    const Vec2 c = balls[0].pos;
    const Vec2 b = balls[std::size_t(place)].pos;
    const double bearing = std::atan2(b.y - c.y, b.x - c.x);
    const double half = std::asin(kD / dist(c, b));
    const double len = std::sqrt(dist(c, b) * dist(c, b) - kD * kD);
    std::vector<int> out;
    for (int k = 1; k < int(balls.size()); ++k) {
        if (is_on(on, k)) continue;
        for (double a : {bearing - half, bearing + half}) {
            const Vec2 edge{c.x + len * std::cos(a), c.y + len * std::sin(a)};
            if (point_to_segment(balls[std::size_t(k)].pos, c, edge) < kD - 1.0e-12) {
                out.push_back(k);
                break;
            }
        }
    }
    return out;
}

// The first ball a cue ball sent from `from` along `a` touches before going `limit`, or -1, and
// how far it went.
int first_touched(const std::vector<BallOnTable>& balls, Vec2 from, double a, double limit, double* at = nullptr) {
    const Vec2 dir{std::cos(a), std::sin(a)};
    int touched = -1;
    double best = limit;
    for (int k = 1; k < int(balls.size()); ++k) {
        // |from + t dir - ball|^2 = D^2, the smaller root.
        const double wx = balls[std::size_t(k)].pos.x - from.x;
        const double wy = balls[std::size_t(k)].pos.y - from.y;
        const double b = wx * dir.x + wy * dir.y;
        const double disc = b * b - (wx * wx + wy * wy - kD * kD);
        if (disc <= 0.0 || b <= 0.0) continue;
        const double t = b - std::sqrt(disc);
        if (t < best) {
            best = t;
            touched = k;
        }
    }
    if (at != nullptr) *at = best;
    return touched;
}

// A straight nose of the real table at this place on a cushion: `along` is y on the top and
// baulk cushions, x on the side ones.
bool on_nose(bool end_cushion, bool positive, double along) {
    for (const phys::LineCushion& l : rig().table.lines) {
        const double n = end_cushion ? l.normal.x : l.normal.y;
        if (std::abs(n) < 0.999 || (n < 0.0) != positive) continue;
        const double a = end_cushion ? l.p1.y : l.p1.x;
        const double b = end_cushion ? l.p2.y : l.p2.x;
        if (along >= std::min(a, b) && along <= std::max(a, b)) return true;
    }
    return false;
}

// The cue ball sent along `a` with ideal bounces off at most `cushions` noses: the first ball it
// touches is on.
bool route_hits_on(const std::vector<BallOnTable>& balls, const std::vector<int>& on, double a, int cushions) {
    Vec2 from = balls[0].pos;
    for (int bounce = 0;; ++bounce) {
        const double cx = std::cos(a);
        const double sy = std::sin(a);
        const double tx = cx > 0.0 ? (kHalfX - from.x) / cx : cx < 0.0 ? (-kHalfX - from.x) / cx : 1e9;
        const double ty = sy > 0.0 ? (kHalfY - from.y) / sy : sy < 0.0 ? (-kHalfY - from.y) / sy : 1e9;
        const double leg = std::min(tx, ty);
        const int touched = first_touched(balls, from, a, leg);
        if (touched >= 0) return is_on(on, touched);
        if (bounce == cushions) return false;
        from = {from.x + leg * cx, from.y + leg * sy};
        if (tx < ty) {
            if (!on_nose(true, cx > 0.0, from.y)) return false;
            a = kPi - a;
        } else {
            if (!on_nose(false, sy > 0.0, from.x)) return false;
            a = -a;
        }
    }
}

}  // namespace

TEST_CASE("snookered and snookering_balls agree with the definition on random tables") {
    Rng rng(1);
    int snookers = 0;
    int effective = 0;
    for (int table_no = 0; table_no < 600; ++table_no) {
        const int reds = int(rng.uniform(0.0, 15.99));
        const auto balls = scattered(rng, 1 + reds + 6, reds, table_no % 3 == 0 ? 0.25 : 2.0);
        std::vector<std::vector<int>> on_sets;
        std::vector<int> every_red;
        for (int i = 1; i <= reds; ++i) every_red.push_back(i);
        if (reds > 0) on_sets.push_back(every_red);
        on_sets.push_back({reds + 1});
        on_sets.push_back({reds + 1, reds + 2, reds + 3, reds + 4, reds + 5, reds + 6});
        for (const std::vector<int>& on : on_sets) {
            bool snookered = true;
            bool same = true;
            std::vector<int> expected;
            for (std::size_t i = 0; i < on.size(); ++i) {
                std::vector<int> mine = obstructors(balls, on, on[i]);
                if (mine.empty()) snookered = false;
                double nearest = 1e9;
                for (int k : mine) nearest = std::min(nearest, dist(balls[0].pos, balls[std::size_t(k)].pos));
                mine.erase(std::remove_if(mine.begin(), mine.end(),
                                          [&](int k) {
                                              return dist(balls[0].pos, balls[std::size_t(k)].pos) > nearest + 1e-9;
                                          }),
                           mine.end());
                if (i == 0) expected = mine;
                same = same && mine == expected;
            }
            if (!snookered || !same) expected.clear();
            CAPTURE(table_no);
            CAPTURE(on.size());
            CHECK(oracle().snookered(balls, 0, on, false) == snookered);
            CHECK(oracle().snookering_balls(balls, 0, on) == expected);
            snookers += snookered;
            effective += !expected.empty();
        }
    }
    // The tables are crowded enough to try both answers.
    CHECK(snookers > 100);
    CHECK(effective > 50);
}

TEST_CASE("clear_path agrees with casting rays on random tables") {
    Rng rng(2);
    int clear = 0;
    int closed = 0;
    for (int table_no = 0; table_no < 400; ++table_no) {
        const int reds = int(rng.uniform(0.0, 15.99));
        const auto balls = scattered(rng, 1 + reds + 6, reds, table_no % 4 == 3 ? 2.0 : 0.2);
        const Vec2 c = balls[0].pos;
        // Whatever is first touched changes only where a ray grazes a ball or reaches the place
        // where the cue ball would touch two at once: rays just either side of each of those,
        // and a coarse fan besides.
        std::vector<double> rays;
        for (int i = 0; i < 720; ++i) rays.push_back(2.0 * kPi * i / 720);
        for (std::size_t i = 1; i < balls.size(); ++i) {
            const Vec2 b = balls[i].pos;
            const double bearing = std::atan2(b.y - c.y, b.x - c.x);
            const double half = std::asin(std::min(1.0, kD / dist(c, b)));
            for (double a : {bearing - half, bearing + half}) {
                rays.push_back(a - 1e-7);
                rays.push_back(a + 1e-7);
            }
            for (std::size_t j = i + 1; j < balls.size(); ++j) {
                const Vec2 q = balls[j].pos;
                const double apart = dist(b, q);
                if (apart >= 2.0 * kD) continue;
                const double up = std::sqrt(kD * kD - apart * apart / 4.0);
                for (double sign : {1.0, -1.0}) {
                    const Vec2 both{(b.x + q.x) / 2.0 - sign * up * (q.y - b.y) / apart,
                                    (b.y + q.y) / 2.0 + sign * up * (q.x - b.x) / apart};
                    const double a = std::atan2(both.y - c.y, both.x - c.x);
                    rays.push_back(a - 1e-7);
                    rays.push_back(a + 1e-7);
                }
            }
        }
        std::vector<int> first;
        for (double a : rays) first.push_back(first_touched(balls, c, a, 1e9));

        std::vector<std::vector<int>> on_sets = {{reds + 1}, {reds + 2, reds + 5}};
        if (reds > 0) on_sets.push_back({1});
        if (reds > 2) on_sets.push_back({1, 2, 3});
        for (const std::vector<int>& on : on_sets) {
            bool expected = false;
            for (int k : first) expected = expected || (k >= 0 && is_on(on, k));
            CAPTURE(table_no);
            CAPTURE(on[0]);
            CHECK(oracle().clear_path(balls, 0, on) == expected);
            (expected ? clear : closed) += 1;
        }
    }
    CHECK(clear > 100);
    CHECK(closed > 100);
}

TEST_CASE("can_hit: every escape it finds is there, and it misses few that a fine sweep finds") {
    Rng rng(3);
    int escapes = 0;
    int impossible = 0;
    int missed = 0;
    for (int table_no = 0; table_no < 150; ++table_no) {
        const int reds = int(rng.uniform(0.0, 8.99));
        const auto balls = scattered(rng, 1 + reds + 6, reds, 0.16);
        const std::vector<int> on = {reds + 1};
        if (oracle().clear_path(balls, 0, on)) continue;
        bool found = false;
        for (int i = 0; i < 40000 && !found; ++i) {
            found = route_hits_on(balls, on, 2.0 * kPi * i / 40000, rig().ref.escape_max_cushions);
        }
        const bool can = oracle().can_hit(balls, 0, on);
        CAPTURE(table_no);
        // A route the oracle found could be too fine for the sweep; none is on these tables.
        CHECK((!can || found));
        escapes += can;
        impossible += !found;
        missed += found && !can;
    }
    CHECK(escapes > 30);
    CHECK(impossible > 3);
    // The oracle aims at a few points across each ball, so it can miss a route through a gap
    // narrower than their spacing: 4 of 620 on 2000 such tables, none on these 150.
    CHECK(missed <= 2);
}
