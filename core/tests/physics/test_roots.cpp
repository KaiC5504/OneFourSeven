#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "ofs/math/roots.hpp"
#include "ofs/rng.hpp"

using namespace ofs::math;

namespace {

constexpr double kRel = 1e-10;

// Highest degree first, from a leading coefficient, real roots and complex pairs (re, im).
std::vector<double> from_factors(double lead, const std::vector<double>& real,
                                 const std::vector<std::pair<double, double>>& pairs = {}) {
    std::vector<double> c{lead};
    auto times = [&](const std::vector<double>& f) {
        std::vector<double> out(c.size() + f.size() - 1, 0.0);
        for (std::size_t i = 0; i < c.size(); ++i) {
            for (std::size_t j = 0; j < f.size(); ++j) out[i + j] += c[i] * f[j];
        }
        c = out;
    };
    for (double r : real) times({1.0, -r});
    for (auto [re, im] : pairs) times({1.0, -2.0 * re, re * re + im * im});
    return c;
}

int solve(const std::vector<double>& c, double* roots) {
    switch (c.size()) {
        case 3: return solve_quadratic(c[0], c[1], c[2], roots);
        case 4: return solve_cubic(c[0], c[1], c[2], c[3], roots);
        default: return solve_quartic(c[0], c[1], c[2], c[3], c[4], roots);
    }
}

void check_roots(const std::vector<double>& c, std::vector<double> want, double rel = kRel) {
    std::sort(want.begin(), want.end());
    want.erase(std::unique(want.begin(), want.end()), want.end());
    double got[4] = {};
    const int n = solve(c, got);
    REQUIRE(n == static_cast<int>(want.size()));
    for (int i = 0; i < n; ++i) {
        CAPTURE(i);
        CAPTURE(got[i]);
        CAPTURE(want[i]);
        CHECK(std::abs(got[i] - want[i]) <= rel * std::abs(want[i]));
    }
}

}  // namespace

TEST_CASE("solve_quadratic: distinct, double, complex and cancelling roots") {
    check_roots(from_factors(2.0, {-3.0, 0.5}), {-3.0, 0.5});
    check_roots(from_factors(1.0, {0.1, 0.1}), {0.1});
    check_roots(from_factors(-4.0, {7.0, 7.0}), {7.0});
    check_roots(from_factors(1.0, {}, {{1.0, 0.5}}), {});
    // b^2 >> 4ac: the textbook formula loses the small root entirely.
    check_roots({1.0, -1.0e8, 1.0}, {1.0e8 - 1.0e-8, 1.0e-8});
    check_roots({3.0, 0.0, -12.0}, {-2.0, 2.0});
}

TEST_CASE("solve_quadratic: roots at 0 and degenerate leading coefficients") {
    check_roots({2.0, -6.0, 0.0}, {0.0, 3.0});
    check_roots({5.0, 0.0, 0.0}, {0.0});
    check_roots({0.0, 2.0, -1.0}, {0.5});
    check_roots({1.0e-20, 2.0, -1.0}, {0.5});
    double r[2] = {};
    CHECK(solve_quadratic(0.0, 0.0, 1.0, r) == 0);
    CHECK(solve_quadratic(0.0, 0.0, 0.0, r) == 0);
}

TEST_CASE("solve_cubic: distinct, repeated and complex roots") {
    check_roots(from_factors(1.0, {-1.0, 2.0, 5.0}), {-1.0, 2.0, 5.0});
    check_roots(from_factors(-3.0, {1.5, 1.5, -0.25}), {1.5, -0.25});
    check_roots(from_factors(0.5, {4.0}, {{-1.0, 2.0}}), {4.0});
    check_roots({1.0, 0.0, -1.0, 0.0}, {-1.0, 0.0, 1.0});
    // A triple root is conditioned like eps^(1/3); one root, close.
    double r[3] = {};
    REQUIRE(solve_cubic(1.0, -3.0, 3.0, -1.0, r) == 1);
    CHECK(std::abs(r[0] - 1.0) < 1e-4);
}

TEST_CASE("solve_cubic: leading coefficient zero or negligible falls back to the quadratic") {
    check_roots({0.0, 1.0, -3.0, 2.0}, {1.0, 2.0});
    check_roots({1.0e-20, 1.0, -3.0, 2.0}, {1.0, 2.0});
    // Not negligible when every root is large: the scale decides, not the size of the coefficient.
    check_roots(from_factors(1.0e-18, {1.0e6, 2.0e6, 3.0e6}), {1.0e6, 2.0e6, 3.0e6});
}

TEST_CASE("solve_quartic: distinct, double and complex roots") {
    check_roots({1.0, -10.0, 35.0, -50.0, 24.0}, {1.0, 2.0, 3.0, 4.0});
    check_roots(from_factors(2.0, {-0.7, 0.2, 1.1, 9.0}), {-0.7, 0.2, 1.1, 9.0});
    check_roots(from_factors(1.0, {1.0, 1.0, 2.0, 2.0}), {1.0, 2.0});
    check_roots(from_factors(-1.5, {0.3, 0.3, -2.0, 5.0}), {0.3, -2.0, 5.0});
    check_roots(from_factors(1.0, {-1.0, 3.0}, {{0.5, 0.25}}), {-1.0, 3.0});
    check_roots(from_factors(1.0, {}, {{0.5, 0.25}, {-2.0, 1.0}}), {});
    check_roots(from_factors(1.0, {2.0, 2.0}, {{0.0, 1.0}}), {2.0});
}

TEST_CASE("solve_quartic: roots at 0 and degenerate leading coefficients") {
    check_roots({1.0, 0.0, -1.0, 0.0, 0.0}, {-1.0, 0.0, 1.0});
    check_roots({1.0, -2.0, 0.0, 0.0, 0.0}, {0.0, 2.0});
    check_roots({0.0, 1.0, -6.0, 11.0, -6.0}, {1.0, 2.0, 3.0});
    check_roots({1.0e-20, 1.0, -6.0, 11.0, -6.0}, {1.0, 2.0, 3.0});
    check_roots({0.0, 0.0, 1.0, -3.0, 2.0}, {1.0, 2.0});
    check_roots({0.0, 0.0, 0.0, 4.0, -1.0}, {0.25});
    double r[4] = {};
    CHECK(solve_quartic(0.0, 0.0, 0.0, 0.0, 3.0, r) == 0);
}

TEST_CASE("solve_quartic: near-double and spread roots, which need polishing") {
    check_roots(from_factors(1.0, {1.0, 1.0001, 2.0, 3.0}), {1.0, 1.0001, 2.0, 3.0});
    check_roots(from_factors(1.0, {1.0e-3, 1.0, 1.0e3, 1.0e5}), {1.0e-3, 1.0, 1.0e3, 1.0e5});
    // A near-tangent contact: |d|^2 - (2R)^2 for a ball passing 1e-7 m inside contact.
    const double R2 = 0.0525;
    const double depth = 1.0e-7;
    const double y = R2 - depth;
    const double half = std::sqrt(R2 * R2 - y * y);
    check_roots({0.0, 0.0, 1.0, -2.0, 1.0 + y * y - R2 * R2}, {1.0 - half, 1.0 + half}, 1e-9);
}

TEST_CASE("solve_quartic: coefficient scale doesn't matter") {
    for (double s : {1.0e-150, 1.0e-30, 1.0e30, 1.0e150}) {
        CAPTURE(s);
        check_roots({s, -10.0 * s, 35.0 * s, -50.0 * s, 24.0 * s}, {1.0, 2.0, 3.0, 4.0});
    }
    check_roots(from_factors(1.0, {1.0e-6, 2.0e-6, 3.0e-6, 5.0e-6}), {1.0e-6, 2.0e-6, 3.0e-6, 5.0e-6});
    check_roots(from_factors(1.0, {1.0e6, 2.0e6, 3.0e6, 5.0e6}), {1.0e6, 2.0e6, 3.0e6, 5.0e6});
}

TEST_CASE("solve_quartic matches random factored quartics") {
    ofs::Rng rng(1470);
    int checked = 0;
    for (int trial = 0; trial < 20000; ++trial) {
        const double scale = std::pow(10.0, rng.uniform(-4.0, 4.0));
        const double lead = std::pow(10.0, rng.uniform(-6.0, 6.0)) * (rng.uniform() < 0.5 ? -1.0 : 1.0);
        const int real = 2 * static_cast<int>(rng.uniform(0.0, 3.0));  // 0, 2 or 4 real roots
        std::vector<double> roots;
        while (static_cast<int>(roots.size()) < real) {
            const double r = scale * rng.uniform(-1.0, 1.0);
            bool apart = true;
            for (double o : roots) apart = apart && std::abs(r - o) > 0.02 * scale;
            if (apart) roots.push_back(r);
        }
        std::vector<std::pair<double, double>> pairs;
        while (static_cast<int>(roots.size() + 2 * pairs.size()) < 4) {
            pairs.push_back({scale * rng.uniform(-1.0, 1.0), scale * rng.uniform(0.05, 1.0)});
        }
        const std::vector<double> c = from_factors(lead, roots, pairs);
        std::sort(roots.begin(), roots.end());
        double got[4] = {};
        const int n = solve(c, got);
        CAPTURE(trial);
        REQUIRE(n == real);
        for (int i = 0; i < n; ++i) {
            // Conditioning: |dr| ~ eps sum |c_k r^k| / |p'(r)|; well-separated roots keep it near 1e-13.
            CHECK(std::abs(got[i] - roots[i]) <= kRel * scale);
        }
        ++checked;
    }
    MESSAGE("random quartics checked: " << checked);
}

TEST_CASE("first_quartic_root_in finds the smallest root in the window") {
    // (t - 1)(t - 2)(t - 3)(t - 4)
    const double c[5] = {1.0, -10.0, 35.0, -50.0, 24.0};
    auto first = [&](double lo, double hi) { return first_quartic_root_in(c[0], c[1], c[2], c[3], c[4], lo, hi); };
    CHECK(first(0.0, 10.0) == doctest::Approx(1.0).epsilon(1e-14));
    CHECK(first(1.5, 10.0) == doctest::Approx(2.0).epsilon(1e-14));
    CHECK(first(2.0, 10.0) == doctest::Approx(2.0).epsilon(1e-14));
    CHECK(first(3.5, 3.9) == 3.9 + 1.0);
    CHECK(first(4.5, 100.0) == 101.0);
    CHECK(first(5.0, 4.0) == 5.0);

    // Double root at 2 inside the window: (t - 2)^2 (t^2 + 1).
    const std::vector<double> d = from_factors(1.0, {2.0, 2.0}, {{0.0, 1.0}});
    CHECK(first_quartic_root_in(d[0], d[1], d[2], d[3], d[4], 0.0, 5.0) == doctest::Approx(2.0).epsilon(1e-14));
    // Root exactly at the start of the window.
    CHECK(first_quartic_root_in(1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 5.0) == 0.0);
    // Quadratic and linear when the top coefficients vanish.
    CHECK(first_quartic_root_in(0.0, 0.0, 1.0, 0.0, -4.0, 0.0, 5.0) == doctest::Approx(2.0).epsilon(1e-14));
    CHECK(first_quartic_root_in(0.0, 0.0, 0.0, 2.0, -1.0, 0.0, 5.0) == doctest::Approx(0.5).epsilon(1e-15));
}

TEST_CASE("solve_quartic micro-benchmark" * doctest::skip()) {
    ofs::Rng rng(7);
    constexpr int kSets = 1024;
    std::vector<double> coef(5 * kSets);
    for (int i = 0; i < kSets; ++i) {
        const std::vector<double> c = from_factors(rng.uniform(0.5, 2.0),
                                                   {rng.uniform(-2.0, 2.0), rng.uniform(-2.0, 2.0)},
                                                   {{rng.uniform(-1.0, 1.0), rng.uniform(0.1, 1.0)}});
        std::copy(c.begin(), c.end(), coef.begin() + 5 * i);
        if (i % 2 == 1) {
            const std::vector<double> r = from_factors(1.0, {rng.uniform(-2.0, -1.0), rng.uniform(-1.0, 0.0),
                                                             rng.uniform(0.0, 1.0), rng.uniform(1.0, 2.0)});
            std::copy(r.begin(), r.end(), coef.begin() + 5 * i);
        }
    }
    constexpr int kReps = 300;
    double sink = 0.0;
    double best = 1e300;
    // The best of several trials: the machine is shared, and the minimum is the cost without
    // interference.
    for (int trial = 0; trial < 7; ++trial) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int rep = 0; rep < kReps; ++rep) {
            for (int i = 0; i < kSets; ++i) {
                const double* c = &coef[5 * i];
                double r[4] = {};
                sink += solve_quartic(c[0], c[1], c[2], c[3], c[4], r) + r[0];
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    std::printf("solve_quartic: %.1f ns/call (half 2 real roots, half 4) [sink %g]\n", best / (double(kReps) * kSets),
                sink);
}
