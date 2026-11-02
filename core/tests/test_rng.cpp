#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>

#include "ofs/rng.hpp"

// Expected values come from an independent Python port of splitmix64 + xoshiro256**
// (Blackman & Vigna, prng.di.unimi.it). If one changes, every logged seed replays differently.

TEST_CASE("Rng reproduces xoshiro256** seeded by splitmix64") {
    ofs::Rng zero(0);
    CHECK(zero.next_u64() == 0x99ec5f36cb75f2b4ULL);
    CHECK(zero.next_u64() == 0xbf6e1f784956452aULL);
    CHECK(zero.next_u64() == 0x1a5f849d4933e6e0ULL);

    ofs::Rng seeded(147);
    CHECK(seeded.next_u64() == 0x6dd371d580ef4cffULL);
    CHECK(seeded.next_u64() == 0xc5d3318eb3558106ULL);
    CHECK(seeded.next_u64() == 0x044fe63dc1899db8ULL);
}

TEST_CASE("uniform takes the top 53 bits") {
    ofs::Rng r(147);
    CHECK(r.uniform() == 0.42900763952723964);
    CHECK(r.uniform() == 0.7727538083448362);
}

TEST_CASE("uniform stays in [lo, hi) and normal is finite with roughly unit spread") {
    ofs::Rng r(7);
    double sum = 0.0;
    double sum2 = 0.0;
    constexpr int kN = 20000;
    for (int i = 0; i < kN; ++i) {
        const double u = r.uniform(-2.0, 3.0);
        CHECK(u >= -2.0);
        CHECK(u < 3.0);
        const double n = r.normal();
        REQUIRE(std::isfinite(n));
        sum += n;
        sum2 += n * n;
    }
    CHECK(std::abs(sum / kN) < 0.05);
    CHECK(std::abs(sum2 / kN - 1.0) < 0.05);
}

TEST_CASE("the same seed gives the same sequence") {
    ofs::Rng a(123456789);
    ofs::Rng b(123456789);
    for (int i = 0; i < 1000; ++i) CHECK(a.next_u64() == b.next_u64());
}
