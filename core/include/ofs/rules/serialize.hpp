#pragma once

// JSON forms of the rules types: what the logs carry, what the Godot binding hands the game as
// Dictionaries, and what tools/sim_cli reads back. Doubles at full precision; a state round-trips
// exactly. Keys are the struct's member names and enums their names (types.hpp), except:
//
//   ball:      {"ball": "red", "index": 3, "x": 0.1, "y": 0.2}       (the game's ball list entry)
//   on:        {"kind": "red"} | {"kind": "any_colour"} | {"kind": "colour", "colour": "blue"}
//   position:  {"balls": [ball], "cue_ball", "cue_in_hand", "on", "free_ball", "touching": [place]}
//   frame:     position's keys, then "seed" (a decimal string: JSON numbers can't carry every
//              uint64), "striker", "scores": [a, b], "break_points", "stats": [stats, stats],
//              "strokes", "respotted_black", "concede_any_time": [bool, bool], "pending", "decider",
//              "offender", "last_penalty", "last_miss", "original": position, "miss_run",
//              "miss_warned", "quiet_strokes", "rerack_accepted", "rerack_deadline", "over",
//              "winner", "end_reason", "restart"
//              and "miss_streak" when it is above 0 (absent reads as 0)
//   input:     {"nominated": "blue"}, "" for none
//   facts:     member names; "pots": [{"ball", "pocket", "t"}], "rest": [[x, y], ...]
//   foul:      {"kind", "clause", "value", "ball"}
//   call:      {"kind", "value", "detail"}
//   judgement: {"post": frame, "fouls": [foul], "penalty", "miss", "miss_reason", "points",
//               "nominated": "blue" | "", "free_ball_used", "spotted": [ball], "calls": [call]}
//   decision:  {"kind", "player"}
//   config:    {"best_of", "first_breaker", "seed" (decimal string), "concede_any_time"}
//   summary:   member names; "reason" is the end reason's name
//   match:     {"config", "frames": [a, b], "frame_number", "breaker", "reracks", "frame",
//               "history": [summary], "over", "winner"}
//
// core/tests/fixtures/rules/example_frame.json and example_judgement.json are these forms in full.

#include <string>

#include <nlohmann/json.hpp>

#include "ofs/rules/engine.hpp"
#include "ofs/rules/facts.hpp"
#include "ofs/rules/match.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::rules {

nlohmann::json ball_json(const BallOnTable& b);
nlohmann::json position_json(const Position& p);
nlohmann::json frame_json(const FrameState& s);
nlohmann::json input_json(const ShotInput& in);
nlohmann::json facts_json(const ShotFacts& f);
nlohmann::json foul_json(const Foul& f);
nlohmann::json calls_json(const std::vector<Call>& calls);
nlohmann::json judgement_json(const Judgement& j);
nlohmann::json decision_json(const Decision& d);
nlohmann::json config_json(const MatchConfig& c);
nlohmann::json summary_json(const FrameSummary& s);
nlohmann::json match_json(const MatchState& m);

// frame_json plus "view", what a scoreboard and the controls show without knowing the rules:
// {"points_remaining", "required": [penalty_points_required(0), (1)], "on_places": places_on,
//  "nominations": [colour names], "options": [[decision names for player 0], [for player 1]],
//  "leader": 0 | 1 | -1}
nlohmann::json frame_view_json(const FrameState& s);

// Each false with a reason on anything malformed: a missing key, a wrong type, an unknown name, a
// place out of range, a non-finite number. `out` is unspecified then.
bool position_from_json(const nlohmann::json& j, Position& out, std::string& error);
bool frame_from_json(const nlohmann::json& j, FrameState& out, std::string& error);
bool input_from_json(const nlohmann::json& j, ShotInput& out, std::string& error);
bool facts_from_json(const nlohmann::json& j, ShotFacts& out, std::string& error);
bool decision_from_json(const nlohmann::json& j, Decision& out, std::string& error);
bool config_from_json(const nlohmann::json& j, MatchConfig& out, std::string& error);
bool match_from_json(const nlohmann::json& j, MatchState& out, std::string& error);

// ofs::params::content_hash of frame_json(s): two states are the same state when these agree.
std::string frame_hash(const FrameState& s);

}  // namespace ofs::rules
