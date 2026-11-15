#pragma once

// The geometry the rules ask about (S2.17 snookered, S3.14 paths, S3.8 touching, S3.7 spotting,
// S3.5 the D). An interface so the rule logic tests against a fake; make_geometry_oracle is the
// real one. Every query is a pure function of its arguments.
//
// `balls` is a ball list and `cue_ball` the cue ball's place in it; `on` lists the places of the
// balls on (engine.hpp's places_on, or the nominated ball). A ball not on is every other object
// ball. Cushions never obstruct (S2.17(e)).

#include <memory>
#include <vector>

#include "ofs/physics/table_shape.hpp"
#include "ofs/rules/referee_params.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::rules {

class TableOracle {
public:
    virtual ~TableOracle() = default;

    // S2.17: a direct stroke in a straight line to every ball of `on` is wholly or partially
    // obstructed by a ball not on; false as soon as one ball of `on` can be hit at both extreme
    // edges. With in_hand, true only when that holds from every position in the D (S2.17(a)); the
    // cue ball's listed position is then ignored. False when `on` is empty.
    virtual bool snookered(const std::vector<BallOnTable>& balls, int cue_ball, const std::vector<int>& on,
                           bool in_hand) const = 0;
    // S2.17(b), (c): the effective snookering balls, lowest place first. For each ball of `on` the
    // obstructing ball nearest the cue ball (all that are equidistant); with several balls on,
    // empty unless the same ball or balls obstruct every one. Empty when not snookered.
    virtual std::vector<int> snookering_balls(const std::vector<BallOnTable>& balls, int cue_ball,
                                              const std::vector<int>& on) const = 0;
    // S3.14(c): a clear path in a straight line from the cue ball to any part of any ball of `on`
    // (the finest edge counts).
    virtual bool clear_path(const std::vector<BallOnTable>& balls, int cue_ball, const std::vector<int>& on) const = 0;
    // S3.14(d): central, full-ball contact is available on some ball of `on`: the cue ball can
    // travel the line of centres to it with no ball in the way. For reds, a red obstructed only by
    // other reds still counts (the full diameter of a red not obstructed by a colour).
    virtual bool full_ball(const std::vector<BallOnTable>& balls, int cue_ball, const std::vector<int>& on) const = 0;
    // S3.14(a)(ii): some ball of `on` can be reached at all, directly or off at most
    // RefParams::escape_max_cushions cushions. False means it is impossible to hit the ball on.
    virtual bool can_hit(const std::vector<BallOnTable>& balls, int cue_ball, const std::vector<int>& on) const = 0;
    // S3.8: places of the object balls within RefParams::touching_gap_m of the cue ball, lowest first.
    virtual std::vector<int> touching(const std::vector<BallOnTable>& balls, int cue_ball) const = 0;

    // S3.7: where each of `colours` is spotted on a table holding `on_table` (the cue ball left
    // out while it is in hand). Every colour whose own spot is available goes on it (j). The
    // rest, highest value first (f), each go to the highest-value spot still available (e); else
    // as near its own spot as possible between that spot and the nearest part of the top cushion
    // (g); pink and black, with no room there, as near their own spot as possible on the centre
    // line towards baulk (h); never touching another ball (i). Returns the placements in the
    // order of `colours`.
    virtual std::vector<BallOnTable> spot(const std::vector<BallOnTable>& on_table,
                                          const std::vector<Ball>& colours) const = 0;

    // S3.5, spec 8.6: `pos` is on or within the lines of the D and a cue ball there would overlap
    // or touch no other ball of `balls`.
    virtual bool cue_placement_legal(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 pos) const = 0;
    // The legal placement nearest `wanted` (itself when legal). The D always has room for one.
    virtual Vec2 nearest_cue_placement(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 wanted) const = 0;
};

std::unique_ptr<TableOracle> make_geometry_oracle(const phys::TableShape& table, const RefParams& ref);

}  // namespace ofs::rules
