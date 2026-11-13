#pragma once

// What the game reads from a finished shot record beyond positions: how hard each event hit
// (spec 13.2 and 13.3 scale sound and haptics by it) and sampled paths (spec 8.4, guide level 3).

#include <cstddef>
#include <vector>

#include "ofs/physics/shot_record.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/table_geometry.hpp"

namespace ofs::phys {

// How fast event i's bodies were closing just before it, in m/s, never negative:
// - ball_ball: the relative velocity of a and b along their line of centres;
// - ball_line, ball_arc: a's velocity into the cushion or jaw along its contact normal;
// - pocket: a's speed as its centre entered the drop circle;
// - strike: the cue ball's speed just after the strike;
// - transition, nap, guard: 0.
// "Just before" is the ball's motion entering this event. The record keeps one segment per ball per
// instant (the one after all its events there), so for events sharing a time the earlier ones at
// that instant are re-resolved to recover it. An index past the event list returns 0.
double event_impact_speed(const ShotRecord& r, std::size_t i, const TableShape& t);

// A ball's centre path from t = 0 until it comes to rest or is potted, as (x, y) points: every
// segment start, plus points at most max_step_s apart in between, and the final position. When
// that would exceed max_points the step widens evenly so the result has at most max_points
// points (segment starts are kept first). Empty for an index out of range.
std::vector<table::Vec2> sample_path(const ShotRecord& r, int ball, double max_step_s, std::size_t max_points);

}  // namespace ofs::phys
