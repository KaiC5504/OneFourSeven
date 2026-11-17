#pragma once

// The pots a position offers (spec 10.2 step 1): geometry only, nothing simulated. The margin is
// the one the game's next-ball pick uses (game/scripts/play/next_target.gd, fitted to the owner's
// logged shots), so the opponent and the suggestion read a pot the same way, with one difference:
// the opponent also lists a ball run along a cushion into the corner at its end (along_cushion),
// which the suggestion to the person leaves out.

#include <vector>

#include "ofs/opponent/env.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::opponent {

struct Pot {
    int target = -1;        // place of the object ball
    int pocket = -1;        // TableShape::pockets index
    rules::Vec2 cue;        // where the cue ball is played from: its position, or a place in the D
    rules::Vec2 aim_point;  // the point in the pocket's mouth the object ball is sent to
    rules::Vec2 ghost;      // the cue ball's centre at contact for a pot along that line with no throw
    double aim_rad = 0.0;   // from `cue` to `ghost`
    double cut_rad = 0.0;   // between the cue ball's line and the object ball's; 0 is straight
    double cue_travel_m = 0.0;     // `cue` to `ghost`
    double object_travel_m = 0.0;  // the object ball to `aim_point`
    // The aim error either way, in radians at the cue, that still pots by geometry: the pocket's
    // half-width as the object ball sees it, brought back to the cue ball, and no more than the
    // room past the nearest ball beside either path. Always above 0 for a listed pot.
    double margin_rad = 0.0;
    // Cueing is hampered: a cushion or a ball stands within kAwkward_m behind the cue ball on the
    // line of the cue (spec 10.3).
    bool awkward = false;
    // The ball has no straight line into the pocket: it lies beside a cushion and is sent along it
    // to the cushion's end, where it drops off the far jaw. aim_point is that end, and margin_rad
    // is from what the simulator was measured to take there (pots.cpp).
    bool along_cushion = false;
    int value = 1;           // what the pot scores: the ball's value, or the ball on's for a free ball
    rules::ShotInput input;  // the declaration the stroke needs; has_nomination false when none does
};

// Room behind the cue ball, centre to the obstruction's face, under which cueing counts as awkward.
inline constexpr double kAwkward_m = 0.12;

// Bands of a pot's length, cue_travel_m + object_travel_m: "short" under kMediumFrom_m, "long"
// from kLongFrom_m, "medium" between. Spec 10.1's "medium shots" and "long pots" are these.
inline constexpr double kMediumFrom_m = 1.0;
inline constexpr double kLongFrom_m = 2.2;
const char* band(const Pot& pot);

// Every pot of a ball that may be played from position `p` with the cue ball at `cue`: each ball
// on (rules::places_on; each colour, declared, when a colour of choice is on; with a free ball
// also each ball that may be declared, worth the ball on) into each pocket, where the object ball
// has a clear run to the pocket, the cue ball a clear run to the ghost, the cut is no wider than
// max_cut_rad and margin_rad is above 0. A ball with no straight line into a corner pocket is
// listed along a cushion that ends there when it lies close enough beside it (along_cushion), its
// run and the cue ball's clear as for any pot. Easiest first (margin_rad, then place, then pocket).
std::vector<Pot> pots_from(const rules::Position& p, rules::Vec2 cue, const Env& env, double max_cut_rad);

// For the striker of `s` (not over, nothing pending): pots_from its cue ball; with the cue ball in
// hand, from each of cue_placements(s, env, kPlacements), every pot keeping the place it is from.
inline constexpr int kPlacements = 9;
std::vector<Pot> pots(const rules::FrameState& s, const Env& env, double max_cut_rad);

// Up to `count` legal places in the D to play from (rules::TableOracle::cue_placement_legal),
// spread over it: the brown, yellow and green sides and the points between, the listed position
// first when it is legal. Never empty: the D always has room.
std::vector<rules::Vec2> cue_placements(const rules::Position& p, const Env& env, int count);

// Pockets the ball at `place` can be potted into from somewhere: it has a clear straight run to
// the pocket's mouth, as a pot's object ball must, whatever the cue ball's position. A run along
// a cushion is not one. Indices into TableShape::pockets, lowest first.
std::vector<int> pockets_open_to(const rules::Position& p, int place, const Env& env);

// Reds with at least one open pocket: the reds a break can go on with. A red in the pack counts
// for nothing until it is opened.
int open_reds(const rules::Position& p, const Env& env);

}  // namespace ofs::opponent
