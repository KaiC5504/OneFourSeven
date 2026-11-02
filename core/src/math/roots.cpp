#include "ofs/math/roots.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

// Real roots are isolated rather than read off a closed form. Between consecutive critical points
// p is monotone and holds at most one root, which Halley steps on the original polynomial,
// safeguarded by bisection, find to rounding. A double root is a critical point where |p| is
// within rounding of zero, so tangent contacts come out as accurate as simple roots, where
// Ferrari's resolvent (and the factorisation in pooltool's ptmath/roots/quartic.py, Orellana &
// De Michele 2020) loses half the digits or reports a complex pair. A quartic's critical points
// are a cubic's roots, taken from the closed form (Numerical Recipes 5.6) and polished.

namespace ofs::math {

namespace {

constexpr double kEps = std::numeric_limits<double>::epsilon();
constexpr double kTiny = std::numeric_limits<double>::min();
// |p(x)| at or below kZeroTol eps sum_k |p_k| |x|^k can't be told from zero: Horner's error bound
// is about 2n eps times that sum, and the coefficients carry their own rounding.
constexpr double kZeroTol = 16.0;
// A leading term this small next to the rest, at the scale of the remaining roots, only adds a
// root some 1e18 times further out than all of them: rounding noise, not a real degree.
constexpr double kNegligible = 1.0e-18;
// A Halley step this small (relative) leaves an error of order its cube: converged.
constexpr double kConverged = 1.0e-9;

// Evaluation is always at degree 4 over zero-padded coefficients, which rounds exactly as the
// shorter Horner sum would and unrolls.

double horner(const double* p, double x) { return (((p[4] * x + p[3]) * x + p[2]) * x + p[1]) * x + p[0]; }

void horner2(const double* p, double x, double& f, double& df) {
    f = p[4];
    df = 0.0;
    for (int k = 3; k >= 0; --k) {
        df = df * x + f;
        f = f * x + p[k];
    }
}

void horner3(const double* p, double x, double& f, double& df, double& ddf) {
    f = p[4];
    df = 0.0;
    ddf = 0.0;
    for (int k = 3; k >= 0; --k) {
        ddf = ddf * x + df;
        df = df * x + f;
        f = f * x + p[k];
    }
    ddf *= 2.0;
}

double zero_tolerance(const double* p, double x) {
    const double ax = std::abs(x);
    const double m = (((std::abs(p[4]) * ax + std::abs(p[3])) * ax + std::abs(p[2])) * ax + std::abs(p[1])) * ax +
                     std::abs(p[0]);
    return kZeroTol * kEps * m;
}

void derivative(const double* p, double* dp) {
    for (int k = 1; k <= 4; ++k) dp[k - 1] = p[k] * k;
    dp[4] = 0.0;
}

// The e with r < 2^e that std::frexp gives, read from the bits: frexp and ldexp are library calls
// on MSVC, and this runs for every polynomial.
int exponent_above(double r) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &r, sizeof bits);
    const int biased = static_cast<int>((bits >> 52) & 0x7ff);
    if (biased == 0) {
        int e = 0;
        std::frexp(r, &e);
        return e;
    }
    return biased - 1022;
}

double pow2(int e) {
    if (e < -1022 || e > 1023) return std::ldexp(1.0, e);
    const std::uint64_t bits = static_cast<std::uint64_t>(e + 1023) << 52;
    double r = 0.0;
    std::memcpy(&r, &bits, sizeof r);
    return r;
}

// 2^ceil(log2(r) / j) or more, without fractional powers.
int pow2_root_exponent(double r, int j) {
    const int e = exponent_above(r);
    return e >= 0 ? (e + j - 1) / j : -((-e) / j);
}

// Fujiwara's bound, |x| <= 2 max_k |p_k / p_n|^(1/(n-k)) with p_0 halved, rounded up to a power of
// two. Zero when every lower coefficient is zero.
double root_bound(const double* p, int n) {
    int e_max = INT_MIN;
    const double inv = 1.0 / p[n];
    for (int k = 0; k < n; ++k) {
        double r = std::abs(p[k] * inv);
        if (k == 0) r *= 0.5;
        if (r == 0.0) continue;
        e_max = std::max(e_max, pow2_root_exponent(r, n - k));
    }
    return e_max == INT_MIN ? 0.0 : pow2(e_max + 1);
}

// The degree once leading coefficients that are negligible at the problem's scale are dropped
// (and zeroed). The scale is S, the root bound of the polynomial without its leading term: p_n is
// negligible when |p_n| S^n is far below the rest at S, so dropping it moves no root of the rest
// and loses only a root far beyond all of them. A pure relative test (|p_n| against max |p_k|)
// would wrongly drop the leading term of a polynomial whose roots are all large.
int effective_degree(double* p, int n) {
    while (n > 0) {
        if (p[n] == 0.0) {
            --n;
            continue;
        }
        int r = n - 1;
        while (r > 0 && p[r] == 0.0) --r;
        if (r == 0) return n;  // without p_n there are no roots, or only x = 0
        const double s = root_bound(p, r);
        if (s == 0.0) return n;
        double rest = 0.0;
        double sk = 1.0;  // S^(k - r), accumulated downwards from k = r
        for (int k = r; k >= 0; --k) {
            rest += std::abs(p[k]) * sk;
            sk /= s;
        }
        double lead = std::abs(p[n]);
        for (int k = r; k < n; ++k) lead *= s;
        if (lead > kNegligible * rest) return n;
        p[n] = 0.0;
        --n;
    }
    return 0;
}

// Real roots of p_2 x^2 + p_1 x + p_0 (p_2 != 0) where it changes sign, ascending.
int quadratic_crossings(const double* p, double* out) {
    const double disc = p[1] * p[1] - 4.0 * p[2] * p[0];
    if (!(disc > 0.0)) return 0;
    const double q = -0.5 * (p[1] + std::copysign(std::sqrt(disc), p[1]));
    const double r1 = q / p[2];
    const double r2 = p[0] / q;
    out[0] = std::min(r1, r2);
    out[1] = std::max(r1, r2);
    return out[0] == out[1] ? 1 : 2;
}

// The cubic's real roots from the closed form: accurate to rounding for well separated roots, to
// about sqrt(eps) near a double root, which rounding can also turn into a complex pair.
int cubic_estimates(const double* p, double* out) {
    const double b3 = p[2] / p[3] / 3.0;
    const double c = p[1] / p[3];
    const double d = p[0] / p[3];
    const double q = b3 * b3 - c / 3.0;
    const double r = b3 * (b3 * b3 - 0.5 * c) + 0.5 * d;
    const double q3 = q * q * q;
    const double r2 = r * r;
    if (r2 < q3) {
        // cos(a -+ 2pi/3) from cos a and sin a, with a = theta / 3 in [0, pi/3].
        const double sq = std::sqrt(q);
        const double ca = std::cos(std::acos(std::clamp(r / (sq * q), -1.0, 1.0)) / 3.0);
        const double sa = std::sqrt(std::max(0.0, 1.0 - ca * ca)) * 1.7320508075688772935;
        out[0] = -2.0 * sq * ca - b3;
        out[1] = sq * (ca - sa) - b3;
        out[2] = sq * (ca + sa) - b3;
        return 3;
    }
    const double a = -std::copysign(std::cbrt(std::abs(r) + std::sqrt(r2 - q3)), r);
    out[0] = a + (a == 0.0 ? 0.0 : q / a) - b3;
    return 1;
}

// Newton steps from a close estimate until |p| stops shrinking or is down to rounding.
double polish(const double* p, double x) {
    double f = 0.0;
    double df = 0.0;
    horner2(p, x, f, df);
    for (int it = 0; it < 6 && df != 0.0 && std::abs(f) > zero_tolerance(p, x); ++it) {
        const double xn = x - f / df;
        double fn = 0.0;
        double dfn = 0.0;
        horner2(p, xn, fn, dfn);
        if (!(std::abs(fn) < std::abs(f))) break;
        x = xn;
        f = fn;
        df = dfn;
    }
    return x;
}

// A start next to the critical point c: p(c + h) ~ p(c) + p''(c) h^2 / 2, exact for a double root.
double start_near_critical(const double* p, double c, double fc, double toward) {
    double f = 0.0;
    double df = 0.0;
    double curv = 0.0;
    horner3(p, c, f, df, curv);
    if (curv == 0.0 || fc * curv >= 0.0) return toward;
    const double h = std::sqrt(-2.0 * fc / curv);
    return toward > c ? c + h : c - h;
}

// The isolation scan over [lo, hi]: roots where p changes sign, and stretches where |p| stays
// within rounding of zero, once each, at the point of smallest |p| (a double root, a root at either
// end). `est` are estimates of roots to start from where one falls in a stretch.
int isolate(const double* p, int n, double lo, double hi, const double* est, int n_est, double* out, int max_out) {
    double x[6] = {};
    int m = 0;
    x[m++] = lo;
    m += detail::critical_points(p, n, lo, hi, x + 1);
    x[m++] = hi;

    double f[6] = {};
    int s[6] = {};
    for (int i = 0; i < m; ++i) {
        f[i] = horner(p, x[i]);
        s[i] = std::abs(f[i]) <= zero_tolerance(p, x[i]) ? 0 : (f[i] > 0.0 ? 1 : -1);
    }

    int count = 0;
    auto emit = [&](double r) {
        if (count > 0 && std::abs(r - out[count - 1]) <= 4.0 * kEps * std::max(std::abs(r), std::abs(out[count - 1]))) {
            return;
        }
        out[count++] = r;
    };

    bool in_run = false;
    double run_x = 0.0;
    double run_abs = 0.0;
    for (int i = 0; i < m && count < max_out; ++i) {
        if (s[i] == 0) {
            if (!in_run || std::abs(f[i]) < run_abs) {
                in_run = true;
                run_x = x[i];
                run_abs = std::abs(f[i]);
            }
            continue;
        }
        if (in_run) {
            emit(run_x);
            in_run = false;
            if (count >= max_out) break;
        }
        if (i + 1 < m && s[i + 1] != 0 && s[i] != s[i + 1]) {
            double guess = x[i] - f[i] * (x[i + 1] - x[i]) / (f[i + 1] - f[i]);
            bool estimated = false;
            for (int k = 0; k < n_est && !estimated; ++k) {
                estimated = est[k] > x[i] && est[k] < x[i + 1];
                if (estimated) guess = est[k];
            }
            const bool left_critical = i > 0;
            const bool right_critical = i + 2 < m;
            if (!estimated && left_critical && (!right_critical || std::abs(f[i]) <= std::abs(f[i + 1]))) {
                guess = start_near_critical(p, x[i], f[i], guess);
            } else if (!estimated && right_critical) {
                guess = start_near_critical(p, x[i + 1], f[i + 1], guess);
            }
            emit(detail::bracketed_root(p, x[i], f[i], x[i + 1], guess));
        }
    }
    if (in_run && count < max_out) emit(run_x);
    return count;
}

int isolate_any(const double* p, int n, double lo, double hi, double* out, int max_out) {
    double est[3] = {};
    const int n_est = n == 3 ? cubic_estimates(p, est) : 0;
    return isolate(p, n, lo, hi, est, n_est, out, max_out);
}

// Every real root of p (lowest first, five coefficients, nominal degree n), ascending, a repeated
// root once.
int solve_lowest_first(double* p, int n, double* roots) {
    n = effective_degree(p, n);
    if (n == 0) return 0;

    // Exact zeros come out by deflation, so the scan never chases a root at 0 down to denormals.
    int zeros = 0;
    while (n > 0 && p[0] == 0.0) {
        for (int k = 0; k < n; ++k) p[k] = p[k + 1];
        p[n] = 0.0;
        --n;
        ++zeros;
    }

    int count = 0;
    if (n == 1) {
        roots[count++] = -p[0] / p[1];
    } else if (n == 2) {
        const double disc = p[1] * p[1] - 4.0 * p[2] * p[0];
        const double disc_tol = 4.0 * kEps * (p[1] * p[1] + std::abs(4.0 * p[2] * p[0]));
        if (std::abs(disc) <= disc_tol) {
            roots[count++] = -p[1] / (2.0 * p[2]);
        } else if (disc > 0.0) {
            count = quadratic_crossings(p, roots);
        }
    } else if (n > 2) {
        const double b = root_bound(p, n);
        count = isolate_any(p, n, -b, b, roots, n);
    }

    if (zeros > 0) {
        int at = 0;
        while (at < count && roots[at] < 0.0) ++at;
        if (at == count || roots[at] != 0.0) {
            for (int i = count; i > at; --i) roots[i] = roots[i - 1];
            roots[at] = 0.0;
            ++count;
        }
    }
    return count;
}

}  // namespace

namespace detail {

// Where p' changes sign in the open interval (lo, hi), ascending (a double root of p' may appear
// too; it only splits a monotone stretch in two).
int critical_points(const double* p, int n, double lo, double hi, double* out) {
    double dp[5] = {};
    derivative(p, dp);
    const int nd = effective_degree(dp, n - 1);
    double r[3] = {};
    int nr = 0;
    if (nd == 1) {
        r[nr++] = -dp[0] / dp[1];
    } else if (nd == 2) {
        nr = quadratic_crossings(dp, r);
    } else if (nd == 3) {
        nr = cubic_estimates(dp, r);
        // Estimates are within ~sqrt(eps) of the root, so only those near the window can be in it.
        const double margin = 1.0e-6 * (std::abs(lo) + std::abs(hi));
        for (int i = 0; i < nr; ++i) {
            if (r[i] > lo - margin && r[i] < hi + margin) r[i] = polish(dp, r[i]);
        }
        std::sort(r, r + nr);
    }
    int count = 0;
    for (int i = 0; i < nr; ++i) {
        if (r[i] > lo && r[i] < hi && (count == 0 || r[i] != out[count - 1])) out[count++] = r[i];
    }
    return count;
}

// The root in the bracket [x1, x2], where p(x1) and p(x2) are nonzero with opposite signs, from
// `guess`. Halley steps, with bisection whenever a step would leave the bracket or fails to halve
// the previous one (the safeguard of rtsafe, Numerical Recipes 9.4).
double bracketed_root(const double* p, double x1, double f1, double x2, double guess) {
    double xl = x1;  // p(xl) < 0 < p(xh)
    double xh = x2;
    if (f1 > 0.0) std::swap(xl, xh);
    double x = guess;
    if (!(x > std::min(x1, x2) && x < std::max(x1, x2))) x = 0.5 * (x1 + x2);
    double step_old = std::abs(x2 - x1);
    for (int it = 0; it < 100; ++it) {
        double f = 0.0;
        double df = 0.0;
        double ddf = 0.0;
        horner3(p, x, f, df, ddf);
        if (f == 0.0) return x;
        if (f < 0.0) {
            xl = x;
        } else {
            xh = x;
        }
        const double den = 2.0 * df * df - f * ddf;
        double xn = den != 0.0 ? x - 2.0 * f * df / den : x;
        const bool inside = xn > std::min(xl, xh) && xn < std::max(xl, xh);
        if (inside && std::abs(xn - x) <= kConverged * std::abs(xn) + kTiny) return xn;
        if (!inside || std::abs(xn - x) > 0.5 * step_old) {
            // Steps that stop shrinking near the root are rounding noise in p: x is as good as it gets.
            if (std::abs(f) <= zero_tolerance(p, x)) return x;
            xn = 0.5 * (xl + xh);
            if (xn == xl || xn == xh) return xn;
        }
        step_old = std::abs(xn - x);
        x = xn;
    }
    return x;
}

}  // namespace detail

int solve_quadratic(double a, double b, double c, double roots[2]) {
    double p[5] = {c, b, a, 0.0, 0.0};
    return solve_lowest_first(p, 2, roots);
}

int solve_cubic(double a, double b, double c, double d, double roots[3]) {
    double p[5] = {d, c, b, a, 0.0};
    return solve_lowest_first(p, 3, roots);
}

int solve_quartic(double a, double b, double c, double d, double e, double roots[4]) {
    double p[5] = {e, d, c, b, a};
    return solve_lowest_first(p, 4, roots);
}

double first_quartic_root_in(double a, double b, double c, double d, double e, double lo, double hi) {
    if (!(lo <= hi)) return hi + 1.0;
    double p[5] = {e, d, c, b, a};
    const int n = effective_degree(p, 4);
    if (n == 0) return hi + 1.0;
    double root = 0.0;
    return isolate_any(p, n, lo, hi, &root, 1) == 1 ? root : hi + 1.0;
}

}  // namespace ofs::math
