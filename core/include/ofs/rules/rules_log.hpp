#pragma once

// The rules engine's log lines (spec 16.10; fields in core/data/log_schema.json). Every referee
// decision carries what it was made from, so tools/sim_cli referee re-judges it on the PC from the
// one line.
//
// `context` is the caller's own: {"match_id", "frame", "shot_id"} from the game, whatever
// identifies the run from a tool. Its keys are copied onto the line's top level, except where the
// line has a field of that name.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/rules/engine.hpp"
#include "ofs/rules/match.hpp"
#include "ofs/rules/referee_params.hpp"

namespace ofs::rules {

// rules.decision (info): {context..., "pre": frame, "input", "facts", "judgement" (judgement_json
// without "post"), "after": {"striker", "scores", "on", "free_ball", "cue_in_hand", "pending",
// "decider", "over", "winner", "end_reason"}, "post_hash": frame_hash(post), "ref": ref_params_values}.
void log_decision(const FrameState& pre, const ShotInput& input, const ShotFacts& facts, const Judgement& j,
                  const RefParams& ref, const nlohmann::json& context);

// rules.choice (info when ok, warn when refused): {context..., "before": frame, "player", "kind",
// "ok", "error", "options": what options() offered that player, "calls", "after" (as above),
// "post_hash"}.
void log_choice(const FrameState& before, Decision d, bool ok, const std::string& error, const FrameState& after,
                const std::vector<Call>& calls, const nlohmann::json& context);

// rules.cue_placed (info when ok, warn when refused): {context..., "player", "x", "y", "ok", "error"}.
void log_cue_placed(const FrameState& s, Vec2 pos, bool ok, const std::string& error, const nlohmann::json& context);

// match.started (info): {context..., "config", "breaker"}.
void log_match_started(const MatchState& m, const nlohmann::json& context);
// frame.started (info): {context..., "number", "breaker", "seed" (decimal string), "reracks"}.
void log_frame_started(const MatchState& m, const nlohmann::json& context);
// frame.ended (info): {context..., "summary"}: the last entry of history.
void log_frame_ended(const MatchState& m, const nlohmann::json& context);
// match.ended (info): {context..., "winner", "frames", "frames_played", "reason": "frames" |
// "conceded" | "abandoned"}.
void log_match_ended(const MatchState& m, const std::string& reason, const nlohmann::json& context);

// rules.invalid (warn): {"what", "detail"}: an input the engine refused or could not make sense of,
// with the keys of the scoped log context on this thread beside them.
void log_invalid(const std::string& what, const std::string& detail);

// While one lives, rules.invalid lines logged on its thread carry `context`'s keys ({"match_id",
// "frame", "shot_id"}): the engine refuses deep inside judge or next_frame, where none of them is
// known. Scopes nest: an inner one's keys win until it ends.
class ScopedLogContext {
public:
    explicit ScopedLogContext(const nlohmann::json& context);
    ~ScopedLogContext();
    ScopedLogContext(const ScopedLogContext&) = delete;
    ScopedLogContext& operator=(const ScopedLogContext&) = delete;

private:
    nlohmann::json previous_;
};

// The innermost live scope's context on this thread; an empty object when there is none.
const nlohmann::json& scoped_log_context();

// Re-judges a rules.decision line's data: judge(pre, input, facts) with its "ref" and a geometry
// oracle for `table`, compared with the logged judgement and post_hash. Returns "" when they
// agree, else one line a person can read saying what differs first.
std::string rejudge(const nlohmann::json& decision_data, const phys::TableShape& table);

}  // namespace ofs::rules
