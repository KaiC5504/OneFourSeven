#include "ofs/math/roots.hpp"

// Stub from the M1 contracts commit; the events agent replaces it.

namespace ofs::math {

int solve_quadratic(double, double, double, double[2]) { return 0; }
int solve_cubic(double, double, double, double, double[3]) { return 0; }
int solve_quartic(double, double, double, double, double, double[4]) { return 0; }

double first_quartic_root_in(double, double, double, double, double, double, double hi) { return hi + 1.0; }

}  // namespace ofs::math
