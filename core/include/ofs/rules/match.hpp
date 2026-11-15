#pragma once

// A match: best of N frames (spec 9, 11.1), the breaker alternating (S3.3(b)).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ofs/physics/params.hpp"
#include "ofs/rules/engine.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::rules {

struct MatchConfig {
    int best_of = 1;         // odd, 1 to 35
    int first_breaker = -1;  // 0 or 1; -1 draws lots from the seed (S3.3)
    std::uint64_t seed = 0;  // racks, lots and the re-spotted black's toss all derive from it
    std::array<bool, 2> concede_any_time{};
};

struct FrameSummary {
    int number = 0;  // from 1
    int breaker = 0;
    int winner = -1;
    EndReason reason = EndReason::none;
    std::array<int, 2> scores{};
    std::array<PlayerStats, 2> stats{};
    int strokes = 0;
    int reracks = 0;
};

struct MatchState {
    MatchConfig config;
    std::array<int, 2> frames{};  // frames won
    int frame_number = 0;         // of the frame in play, from 1
    int breaker = 0;              // of the frame in play
    int reracks = 0;              // of the frame in play (S3.17)
    FrameState frame;
    std::vector<FrameSummary> history;  // finished frames
    bool over = false;
    int winner = -1;
};

enum class MatchEvent : std::uint8_t {
    none,
    reracked,     // S3.17(b): the frame was racked again, scores nil, the same breaker
    frame_ended,  // history has its summary; next_frame() racks the next
    match_ended,
};

std::string_view match_event_name(MatchEvent e);

// False with `error` when best_of is even or out of range, or first_breaker is not -1, 0 or 1.
bool valid_config(const MatchConfig& c, std::string& error);

// Frame 1 racked and waiting for its break-off. Precondition: valid_config.
MatchState start_match(const MatchConfig& config, const phys::PhysicsParams& params);

// Takes a judgement's post state, or the frame after decide(), into the match: a finished frame is
// counted and summarised, the match ends when a player has more than half of best_of, and a frame
// marked `restart` is racked again.
MatchEvent settle(MatchState& m, const phys::PhysicsParams& params);

// decide() on the frame in play, then settle(). concede_match ends the match for the other player
// whatever the frames stand at.
bool match_decide(MatchState& m, Decision d, const TableOracle& oracle, const RefParams& ref,
                  const phys::PhysicsParams& params, std::vector<Call>& calls, MatchEvent& event, std::string& error);

// Racks the next frame, the other player breaking (S3.3(b)). False when the frame in play is not
// over or the match is.
bool next_frame(MatchState& m, const phys::PhysicsParams& params);

// The seed frame `number`'s rack and lots use (reracks counted in), so a logged match replays.
std::uint64_t frame_seed(const MatchConfig& config, int number, int reracks);

}  // namespace ofs::rules
