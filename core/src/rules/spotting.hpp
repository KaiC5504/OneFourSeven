#pragma once

// S3.7 for the geometry oracle; oracle.hpp's TableOracle::spot documents the rule.

#include <vector>

#include "ofs/rules/types.hpp"

namespace ofs::rules {

// A ball put as near as possible to another (a colour with every spot taken, the cue ball brought
// to the nearest legal place in the D) sits this far beyond touching: more than rounding or a trip
// through 32-bit floats can take back, and far less than anyone can see.
inline constexpr double kJustClear = 1.0e-6;

// Entries of `colours` that are not colours are left out of the result.
std::vector<BallOnTable> spot_colours(const std::vector<BallOnTable>& on_table, const std::vector<Ball>& colours,
                                      double touching_gap_m);

}  // namespace ofs::rules
