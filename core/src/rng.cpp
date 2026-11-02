#include "ofs/rng.hpp"

#include <cmath>

namespace ofs {

namespace {

std::uint64_t splitmix64(std::uint64_t& x) {
    std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

constexpr double kTwoPi = 6.283185307179586476925286766559;

}  // namespace

Rng::Rng(std::uint64_t seed) {
    std::uint64_t x = seed;
    for (auto& s : s_) s = splitmix64(x);
}

std::uint64_t Rng::next_u64() {
    const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
}

double Rng::uniform() { return static_cast<double>(next_u64() >> 11) * 0x1.0p-53; }

double Rng::uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }

double Rng::normal() {
    // 1 - uniform() is in (0, 1], so the log is finite.
    const double u1 = 1.0 - uniform();
    const double u2 = uniform();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(kTwoPi * u2);
}

}  // namespace ofs
