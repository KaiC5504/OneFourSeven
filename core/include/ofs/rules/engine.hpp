#pragma once

// The rules engine (spec 9.2): pure and deterministic. The match UI, the opponent, the tools and
// the tests all go through these functions. docs/rules/README.md is the reading of the rulebook
// they implement.
//
// One stroke: place_cue_ball() when in hand, simulate from the state's balls, then judge_shot().
// Its post state either leaves the striker (or the next player) to play, or is pending a decision:
// options() lists what `decider` may choose and decide() applies it.

#include <cstdint>
#include <string>
#include <vector>

#include "ofs/physics/shot_record.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/physics/types.hpp"
#include "ofs/rules/facts.hpp"
#include "ofs/rules/oracle.hpp"
#include "ofs/rules/referee_params.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::rules {

struct Judgement {
    FrameState post;
    // Every foul in the stroke, highest value first; `penalty` is the first one's (S3.10(g)).
    std::vector<Foul> fouls;
    int penalty = 0;
    bool miss = false;
    // Why a miss was or was not called on a foul that failed to hit a ball on first: "S3.14(c)",
    // "S3.14(d)", "S3.14(a)" (snookered, no clear path), "S3.14(a)(i)" (penalty points required),
    // "S3.14(a)(ii)" (impossible to hit), "lenient" (RefParams::miss_lenient_gap_m); "" otherwise.
    std::string miss_reason;
    int points = 0;  // scored by the striker in this stroke
    // The nomination the stroke was judged with: the declared colour, or the first ball hit where
    // one was open and none declared. has_nomination false when none applied.
    ShotInput nominated;
    bool free_ball_used = false;  // the nominated ball was a free ball
    std::vector<BallOnTable> spotted;  // colours spotted after the stroke, where they went (S3.7)
    std::vector<Call> calls;           // in the order a referee would announce them
};

// A frame about to start (S3.3(c)): `rack` is phys::opening_rack's table (cue ball first), the
// breaker is in hand with the cue ball at table::default_cue_ball_position(), red is on.
FrameState new_frame(int breaker, const phys::TableState& rack, std::uint64_t seed);

// Places of the balls on, or that could be on, lowest first: every red when red is on, every
// colour when a colour of choice is on, the one colour otherwise. A free ball is not among them
// (S3.12(a)(i)).
std::vector<int> places_on(const Position& p);
// The colours the striker may declare for the next stroke, by value: every colour on the table
// when a colour of choice is on; every object ball not on when FREE BALL was called; else empty.
std::vector<Ball> nominations(const FrameState& s);
// The value of the balls left: 8 per red plus 27 while reds remain, 7 more when a colour of choice
// is on; the remaining colours' values once the reds are gone; 7 with a re-spotted black.
int points_remaining(const FrameState& s);
// How many penalty points `player` needs beyond the balls left to draw level: their deficit minus
// points_remaining, 0 when that is not positive. "Requires penalty points" (S3.14(a)(i), S4.2(b))
// is this being above 0.
int penalty_points_required(const FrameState& s, int player);

// Puts the in-hand cue ball at `pos` (S3.5, spec 8.6). False with `error`, the state unchanged,
// when the cue ball is not in hand, the frame is over or pending, or the oracle refuses the place.
bool place_cue_ball(FrameState& s, Vec2 pos, const TableOracle& oracle, std::string& error);

// The referee's decision on one stroke from its facts. Precondition: `pre` is not over and not
// pending; otherwise the result's post is `pre` unchanged with no calls, and rules.invalid is
// logged. Never throws.
Judgement judge(const FrameState& pre, const ShotInput& input, const ShotFacts& facts, const TableOracle& oracle,
                const RefParams& ref);
// observe() then judge().
Judgement judge_shot(const FrameState& pre, const ShotInput& input, const phys::ShotRecord& record,
                     const phys::TableShape& table, const TableOracle& oracle, const RefParams& ref);

// What `player` may choose now, in the order the game offers them: after a foul [play, play_again,
// replace (only after a miss)] for the decider; [accept_rerack, refuse_rerack] or [play_first,
// play_second] for the decider of those; and with nothing pending, for either player, claim_frame
// (the striker, S2.1(b)), concede_frame (when allowed) and concede_match. Empty when the frame is
// over.
std::vector<DecisionKind> options(const FrameState& s, int player);
// Applies one of options(). False with `error`, the state unchanged, for anything else. `calls`
// receives what the referee announces as a result (frame_over, rerack, free_ball, ...).
bool decide(FrameState& s, Decision d, const TableOracle& oracle, const RefParams& ref, std::vector<Call>& calls,
            std::string& error);

}  // namespace ofs::rules
