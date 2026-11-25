#pragma once

// The rules engine's state (spec 9, 9.2): one frame as the referee knows it. Rule numbers are the
// WPBSA Official Rules of Snooker, 2024-25 edition (S3.14 is Section 3 Rule 14);
// docs/rules/README.md says how each clause is read in a game with no human referee.
//
// Players are 0 and 1. Positions are core coordinates in metres (table_geometry.hpp). A "place" is
// a ball's position in a ball list.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ofs/table_geometry.hpp"

namespace ofs::rules {

using table::Ball;
using table::Vec2;

struct BallOnTable {
    Ball ball = Ball::cue;
    int index = 0;  // 0-14 for reds, as in table::BallPlacement; 0 otherwise
    Vec2 pos;
};

enum class OnKind : std::uint8_t {
    red,         // any red (S3.3(g))
    any_colour,  // a colour of the striker's choice, after a red (S3.3(h)(i))
    colour,      // BallOn::colour, once the reds are gone (S3.3(h)(iii))
};

struct BallOn {
    OnKind kind = OnKind::red;
    Ball colour = Ball::yellow;  // read only when kind == colour
};

// What a stroke starts from, and what a replaced miss puts back (S3.14(b)).
struct Position {
    std::vector<BallOnTable> balls;  // every ball on the table; the cue ball is always listed
    int cue_ball = 0;                // its place in `balls`
    // The striker places the cue ball in the D before the stroke (S2.9, S3.5). Its listed position
    // is then a legal suggestion and it obstructs nothing.
    bool cue_in_hand = false;
    BallOn on;
    bool free_ball = false;     // FREE BALL was called: the striker may nominate any ball (S3.12)
    std::vector<int> touching;  // places of balls on, or that could be on, touching the cue ball (S3.8(a))
};

enum class Pending : std::uint8_t {
    none,        // the striker plays
    after_foul,  // `decider` chooses play, play_again or replace (S3.10(h), S3.13, S3.14(b))
    rerack,      // `decider` answers a re-rack offer (S3.17)
    toss,        // `decider` won the lots for the re-spotted black and chooses who plays (S3.4(b)(ii))
};

enum class EndReason : std::uint8_t {
    none,
    black,            // S3.4(a): the first pot or foul with only the black left
    respotted_black,  // S3.4(b)(iv)
    claimed,          // S2.1(b): by the striker, more than seven ahead with only the black left
    awarded,          // S2.1(c): to the non-striker, more than seven ahead with only the black left
    conceded,         // S4.2
    three_misses,     // S3.14(d)(ii)
    match_conceded,
};

struct PlayerStats {
    int shots = 0;         // strokes played
    int pots = 0;          // attempts that scored
    int pot_attempts = 0;  // strokes ShotFacts::pot_attempt marked
    int fouls = 0;
    int misses = 0;
    int points_from_fouls = 0;  // penalty points received
    int highest_break = 0;
};

struct FrameState : Position {
    std::uint64_t seed = 0;  // decides the lots for a re-spotted black
    int striker = 0;         // who plays the next stroke, once nothing is pending
    std::array<int, 2> scores{};
    int break_points = 0;  // the striker's break so far (S2.8)
    std::array<PlayerStats, 2> stats{};
    int strokes = 0;  // played in this frame, by both
    bool respotted_black = false;
    // The owner's ruling: a player allowed to concede at any time; otherwise only when a player
    // requires penalty points (S4.2(b)).
    std::array<bool, 2> concede_any_time{};

    Pending pending = Pending::none;
    int decider = 0;
    // Valid while pending == after_foul. In a re-spotted black `offender` instead keeps who
    // played first, for S3.17(d).
    int offender = 0;
    int last_penalty = 0;
    bool last_miss = false;  // FOUL AND A MISS was called: `replace` is offered
    Position original;       // the position before the foul stroke

    // S3.14(d): FOUL AND A MISS calls in a row from one original position where central full-ball
    // contact was available, or the cue ball touched a ball that could be on.
    int miss_run = 0;
    bool miss_warned = false;

    // S3.17, by RefParams' thresholds.
    int quiet_strokes = 0;     // quiet strokes in a row (RefParams::stalemate_quiet_travel_m)
    int rerack_accepted = -1;  // the player who has accepted the offer on the table; -1 nobody
    int rerack_deadline = -1;  // strokes left for the position to change after an objection (S3.17(a)); -1 no proviso

    bool over = false;
    int winner = -1;
    EndReason end_reason = EndReason::none;
    bool restart = false;  // S3.17(b): scores nullified; the match racks again with the same breaker
};

enum class DecisionKind : std::uint8_t {
    play,           // after a foul: the non-offender plays from the position left
    play_again,     // after a foul: the offender plays again from the position left (S3.10(h))
    replace,        // after a FOUL AND A MISS: balls replaced, the offender plays again (S3.14(b))
    accept_rerack,  // S3.17
    refuse_rerack,
    play_first,     // the toss winner plays the re-spotted black first (S3.4(b)(ii))
    play_second,
    claim_frame,    // S2.1(b)
    concede_frame,  // S4.2
    concede_match,
};

struct Decision {
    DecisionKind kind = DecisionKind::play;
    int player = 0;
};

// What the striker declared (S2.12): a colour of choice after a red, or a free ball. Without one,
// the first ball hit is taken as declared where a nomination was open (docs/rules/README.md).
struct ShotInput {
    bool has_nomination = false;
    Ball nominated = Ball::yellow;
};

enum class FoulKind : std::uint8_t {
    no_hit,              // S3.11(a)(vi)
    in_off,              // S3.11(a)(vii)
    free_ball_snooker,   // S3.11(a)(viii), S3.12(b)(ii)
    pot_not_on,          // S3.11(b)(iii)
    first_hit_not_on,    // S3.11(b)(iv), S3.12(b)(i)
    push_stroke,         // S3.11(b)(v), S2.19, S3.8(b)
    simultaneous_hit,    // S3.11(c), S3.6
    reds_in_succession,  // S3.11(d)(iii)
    before_nominating,   // S3.11(d)(vi)
};

struct Foul {
    FoulKind kind = FoulKind::no_hit;
    int value = 4;
    int ball = -1;  // place in the pre-shot ball list of the ball concerned; -1 none
};

enum class CallKind : std::uint8_t {
    foul,             // FOUL; value is the penalty
    miss,             // FOUL AND A MISS, after its foul
    free_ball,        // FREE BALL (S3.12)
    touching_ball,    // TOUCHING BALL (S3.8(a))
    warning,          // S3.14(d)(ii): a further miss loses the frame
    respotted_black,  // S3.4(b); value is the toss winner
    rerack_offer,     // S3.17
    rerack,           // S3.17(b), or both players accepted
    frame_over,       // value is the winner; detail is end_reason_name
};

struct Call {
    CallKind kind = CallKind::foul;
    int value = 0;
    std::string detail;
};

// "red", "any_colour", "colour"; "none", "after_foul", ...; the enumerator's own name for each.
std::string_view on_kind_name(OnKind k);
std::string_view pending_name(Pending p);
std::string_view end_reason_name(EndReason r);
std::string_view decision_name(DecisionKind k);
std::string_view foul_name(FoulKind k);
std::string_view call_name(CallKind k);
// The clause a foul is penalised under, such as "S3.11(b)(iv)".
std::string_view foul_clause(FoulKind k);

bool parse_on_kind(std::string_view name, OnKind& out);
bool parse_pending(std::string_view name, Pending& out);
bool parse_end_reason(std::string_view name, EndReason& out);
bool parse_decision(std::string_view name, DecisionKind& out);
bool parse_foul(std::string_view name, FoulKind& out);
bool parse_call(std::string_view name, CallKind& out);

}  // namespace ofs::rules
