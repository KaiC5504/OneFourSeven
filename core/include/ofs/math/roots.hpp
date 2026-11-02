#pragma once

// Real polynomial roots for event times (spec 16.1: quadratic and quartic solvers including their
// edge cases). Coefficients are highest degree first. Roots are written in ascending order and
// the count is returned; a repeated root appears once. A leading coefficient that is zero or
// negligible next to the others falls back to the lower degree. Quartic roots are polished with
// Newton steps on the original polynomial, so near-tangent contacts (double roots) are neither
// lost nor reported twice.

namespace ofs::math {

int solve_quadratic(double a, double b, double c, double roots[2]);
int solve_cubic(double a, double b, double c, double d, double roots[3]);
int solve_quartic(double a, double b, double c, double d, double e, double roots[4]);

// The smallest root in [lo, hi] of the quartic, or hi + 1 when there is none. Event detection
// only ever needs the earliest time in a window.
double first_quartic_root_in(double a, double b, double c, double d, double e, double lo, double hi);

// The pieces detect.cpp scans with, so each critical point and root is found once. Coefficients
// lowest degree first, always five of them, zero above the degree n.
namespace detail {
// Where p' changes sign in the open interval (lo, hi), ascending.
int critical_points(const double* p, int n, double lo, double hi, double* out);
// The root of p between x1 (where p is f1) and x2, which bracket a sign change.
double bracketed_root(const double* p, double x1, double f1, double x2, double guess);
}  // namespace detail

}  // namespace ofs::math
