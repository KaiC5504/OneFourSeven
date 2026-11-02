#pragma once

#include <cstdint>

namespace ofs {

// Seeded and bit-identical on every platform and standard library: xoshiro256** seeded through
// splitmix64, with its own distributions, because std::uniform_real_distribution and friends
// are implementation-defined (spec 7.11 C13, 16.10 determinism).
class Rng {
public:
    explicit Rng(std::uint64_t seed);

    std::uint64_t next_u64();
    // [0, 1) with 53 random bits.
    double uniform();
    // [lo, hi).
    double uniform(double lo, double hi);
    // Standard normal (Box-Muller; only sqrt, log and cos, each deterministic on one platform).
    double normal();

private:
    std::uint64_t s_[4];
};

}  // namespace ofs
