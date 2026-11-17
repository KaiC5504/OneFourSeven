#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "ofs/opponent/exhibition.hpp"
#include "ofs/opponent/level.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/physics/params.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/rules/match.hpp"
#include "ofs/rules/oracle.hpp"
#include "ofs/rules/referee_params.hpp"

#include "ofs_physics.h"

// One match for the game (spec 9.2, 11): the rules engine, the match around it and the stand-in
// opponent, as Dictionaries in the JSON forms of ofs/rules/serialize.hpp. Players are 0 and 1; the
// game decides which is the person.
//
// Every method that can fail returns a Dictionary with "ok": false and "error", or an empty value
// as noted, logs why (rules.invalid, or app.error for an exception) and leaves the match as it
// was. Nothing throws into Godot.
class OfsMatch : public godot::RefCounted {
    GDCLASS(OfsMatch, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    // Out of line, where OpponentJob is whole. The destructor stops any thinking first: the
    // worker never outlives the engine.
    OfsMatch();
    ~OfsMatch() override;

    // Takes the physics (it must be configured) and its table, and the texts of
    // referee_params.json and standin_params.json. False when anything is refused, keeping the
    // previous configuration; either way logs rules.configured. Not allowed while a match is in
    // progress (false). The physics' values are read again at every call that racks, judges or
    // asks the stand-in, so values changed live reach the match at its next stroke.
    bool configure(const godot::Ref<OfsPhysics> &physics, const godot::String &referee_json,
                   const godot::String &standin_json);
    bool is_ready() const;

    // config: {best_of: int, first_breaker: int (-1 lots, 0, 1), seed: an int from 0 or a decimal
    // String (as match_state() gives it back), concede_any_time: [bool, bool], person: 0 or 1
    // (optional, copied onto match.started)}. A match in progress is abandoned first (match.ended
    // "abandoned"). Starts frame 1 and logs match.started and frame.started with match_id on them.
    // Returns {ok, error, match: match_state()}.
    godot::Dictionary start(const godot::Dictionary &config, const godot::String &match_id);
    // A match was started and neither finished nor abandoned.
    bool in_progress() const;
    // Ends the match in progress with no winner (the player quit) and logs match.ended with
    // reason "abandoned"; match_state() then shows it over with winner -1. Nothing happens
    // without one.
    void abandon();

    // match_json with "frame" as frame(); empty before start.
    godot::Dictionary match_state() const;
    // frame_view_json of the frame in play; empty before start.
    godot::Dictionary frame() const;
    // The frame's balls as the game's ball list ({ball, index, x, y}), and the cue ball's place.
    godot::Array balls() const;
    int64_t cue_ball() const;

    // Puts the in-hand cue ball at (x, y); logs rules.cue_placed. {ok, error, x, y}.
    godot::Dictionary place_cue(double x, double y);
    // The legal placement nearest (x, y) for the in-hand cue ball: {x, y}. Empty when not ready,
    // no match is in progress or the point is not finite; each distinct refusal is logged once, 64
    // at most a session, as this is asked on every drag.
    godot::Dictionary nearest_cue_placement(double x, double y) const;

    // The referee's decision on the stroke `record` holds, which must have been simulated from
    // balls() and cue_ball(): a record that wasn't is refused, one a simulator guard stopped is
    // still judged. input: {nominated: String, "" for none}; context: {shot_id}. Logs
    // rules.decision, and frame.ended, match.ended or frame.started when the stroke brings them.
    // Returns {ok, error, judgement: judgement_json with "post" left out, event:
    // match_event_name, frame: frame()}.
    godot::Dictionary judge(const godot::Ref<OfsShotRecord> &record, const godot::Dictionary &input,
                            const godot::Dictionary &context);
    // Applies one of the frame view's options for `player`; logs rules.choice and the frame and
    // match lines it brings. {ok, error, calls: [call], event, frame}.
    godot::Dictionary decide(const godot::String &kind, int64_t player);
    // Racks the next frame after one has ended; logs frame.started. {ok, error, frame}.
    godot::Dictionary next_frame();

    // The stand-in's stroke for the striker; logs opponent.decision with `context` ({shot_id}) and
    // the seed, so the choice can be made again from the log. {ok, error, places_cue, cue: [x,
    // y], nominated, strike: {aim_rad, tip_x, tip_y, elevation_rad, speed_m_s}, why, us}. The cue
    // ball is not placed and nothing is judged: the game calls place_cue, simulates and calls
    // judge as for a person's stroke. When the stand-in would rather claim or concede, the answer
    // is {ok, error, decision: <decision name>, why, us} with no strike, and the line no shot_id:
    // the game calls decide with it.
    godot::Dictionary standin_shot(int64_t seed, const godot::Dictionary &context);
    // The stand-in's choice among the options for `player`, as a decision name; "" when there are
    // none or the call is refused. A choice not offered is replaced by the first option and logged
    // as a warning. Logs opponent.decision with the seed and what it weighed (`why`).
    godot::String standin_choice(int64_t player, int64_t seed);

    // The computer opponent's levels (spec 10.1) and the exhibition tables. `levels` maps a level's
    // id to the text of its file (core/data/opponent_<id>.json); exhibitions_json is
    // exhibitions.json. False, keeping what was configured, when anything is refused; either way
    // logs rules.configured with "levels" (each level_json) and "exhibitions" (their names).
    // Needs configure() first and is not allowed while a match is in progress.
    bool configure_opponent(const godot::Dictionary &levels, const godot::String &exhibitions_json);
    // [{id, name, tier, maximum}], by tier with the 147 last; empty before configure_opponent.
    godot::Array levels() const;
    // The exhibition tables' names, in the file's order.
    godot::PackedStringArray exhibitions() const;

    // start()'s config also takes "level" (a level id, copied onto match.started; absent or ""
    // for none) and "exhibition" (an index into exhibitions(), best_of 1 only): the frame then
    // starts from that table with the player who is not "person" at the table (player 1 without
    // one), red on, and exhibition.started is logged.

    // Starts `level` thinking about the striker's stroke on a worker thread and returns at once:
    // true when it started (opponent.think is logged with `context`'s shot_id and the seed).
    // False, with last_error, for an unknown level, when no stroke can be played now, or while an
    // earlier answer is still thinking or waiting to be taken. budget: {max_sims, max_steps,
    // max_ms}, each optional. max_sims and max_steps: 0 or absent leave the level's own and no
    // limit; max_ms: absent is 1500 (spec 10.6), 0 or negative no clock stop.
    bool opponent_think(const godot::String &level, int64_t seed, const godot::Dictionary &context,
                        const godot::Dictionary &budget);
    // Thinking was started and its answer has not been taken or cancelled.
    bool opponent_thinking() const;
    // The answer once it is ready, in standin_shot's form plus "level", "meant" (the strike as
    // planned) and "think" ({candidates, sims, steps, depth, stopped, ms}); empty while it is
    // still thinking or when nothing was asked, and never waits. Taking it logs
    // opponent.decision, and opponent.maximum and opponent.fallback when they apply. {ok: false,
    // error} when the frame is no longer the one it was asked about, the thinking threw, or the
    // stroke could not be played; the answer is spent either way.
    godot::Dictionary opponent_take();
    // Stops the thinking and drops its answer; logs opponent.cancelled when there was any.
    // abandon(), start() and freeing the engine do this too.
    void opponent_cancel(const godot::String &why);
    // A level's choice among the options for `player`, as standin_choice (it is quick: the main
    // thread).
    godot::String opponent_choice(const godot::String &level, int64_t player, int64_t seed);

    // Why the last call failed; "" when it didn't.
    godot::String last_error() const;

private:
    // Why the match can't be acted on: not configured, none started, or it is over. "" when it can.
    std::string not_in_progress() const;
    // The same, and why no stroke can be played or chosen: the frame is over or a decision is pending.
    std::string no_stroke_now() const;
    // {"match_id", "frame"} for `m` and, when there is one, "shot_id": on every line a match logs.
    nlohmann::json log_context(const ofs::rules::MatchState &m, const godot::String &shot_id = godot::String()) const;
    // log_context and "params_hash", the physics values a frame.started's rack was laid with.
    nlohmann::json rack_context(const ofs::rules::MatchState &m, const godot::String &shot_id = godot::String()) const;
    // log_context of the match in play for a ScopedLogContext; empty before a match is started.
    nlohmann::json scope_context(const godot::String &shot_id = godot::String()) const;
    // params_ and params_hash_ from physics_ as it is configured now.
    void refresh_params();
    // Sets last_error and logs rules.invalid: the call, what was wrong, what it was given, the match.
    void refusal(const char *call, const std::string &why, const nlohmann::json &given) const;
    godot::Dictionary refused(const char *call, const std::string &why, const nlohmann::json &given) const;
    // For a caught exception: sets last_error and logs app.error.
    godot::Dictionary thrown(const char *call, const std::string &message) const;
    // The configured level with this id; null when there is none.
    const ofs::opponent::Level *find_level(const std::string &id) const;
    // standin_choice (no level) and opponent_choice, as `call`.
    godot::String choose(const char *call, const std::string *level, int64_t player, int64_t seed);
    // opponent_cancel without its guard.
    void cancel_thinking(const std::string &why);
    // opponent.maximum for the 147 about to play the frame in play, when its state has changed.
    void log_maximum(const nlohmann::json &stroke_why, const nlohmann::json &context);

    // One level's thinking: what the worker was handed, and what it gives back.
    struct OpponentJob;

    bool ready_ = false;
    bool started_ = false;
    godot::Ref<OfsPhysics> physics_;
    ofs::phys::PhysicsParams params_;
    std::string params_hash_;
    std::shared_ptr<const ofs::phys::TableShape> table_;
    ofs::rules::RefParams ref_;
    ofs::opponent::StandinParams standin_;
    std::unique_ptr<ofs::rules::TableOracle> oracle_;
    ofs::rules::MatchState match_;
    godot::String match_id_;
    std::vector<ofs::opponent::Level> levels_;  // by tier, the 147 last
    std::vector<ofs::opponent::Exhibition> exhibitions_;
    std::unique_ptr<OpponentJob> job_;
    // opponent.maximum is logged when this changes within one frame: "" (not yet on), "on", "gone".
    std::string maximum_state_;
    std::string maximum_frame_;  // the frame maximum_state_ belongs to
    mutable godot::String last_error_;
};
