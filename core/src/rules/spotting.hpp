#pragma once

// S3.7 for the geometry oracle; oracle.hpp's TableOracle::spot documents the rule.

#include <vector>

#include "ofs/rules/types.hpp"

namespace ofs::rules {

std::vector<BallOnTable> spot_colours(const std::vector<BallOnTable>& on_table, const std::vector<Ball>& colours,
                                      double touching_gap_m);

}  // namespace ofs::rules
