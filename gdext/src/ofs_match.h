#pragma once

#include <memory>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

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
    // Takes the physics' parameters and table (it must be configured) and the texts of
    // referee_params.json and standin_params.json. False when anything is refused, keeping the
    // previous configuration; either way logs rules.configured. Not allowed while a match is in
    // progress (false).
    bool configure(const godot::Ref<OfsPhysics> &physics, const godot::String &referee_json,
                   const godot::String &standin_json);
    bool is_ready() const;

    // config: {best_of: int, first_breaker: int (-1 lots, 0, 1), seed: int, concede_any_time:
    // [bool, bool]}. Starts frame 1 and logs match.started and frame.started with match_id on
    // them. Returns {ok, error, match: match_state()}.
    godot::Dictionary start(const godot::Dictionary &config, const godot::String &match_id);
    // A match was started and neither finished nor abandoned.
    bool in_progress() const;
    // Ends the match in progress with no winner (the player quit) and logs match.ended with
    // reason "abandoned". Nothing happens without one.
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
    // The legal placement nearest (x, y) for the in-hand cue ball: {x, y}. Empty when not ready or
    // no match is in progress.
    godot::Dictionary nearest_cue_placement(double x, double y) const;

    // The referee's decision on the stroke `record` holds, which must have been simulated from
    // balls() and cue_ball(). input: {nominated: String, "" for none}; context: {shot_id}. Logs
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

    // The stand-in's stroke for the striker; logs opponent.decision with `context` ({shot_id}).
    // {ok, error, places_cue, cue: [x, y], nominated, strike: {aim_rad, tip_x, tip_y,
    // elevation_rad, speed_m_s}, why, us}. The cue ball is not placed and nothing is judged: the
    // game calls place_cue, simulates and calls judge as for a person's stroke. When the stand-in
    // would rather claim or concede, the answer is {ok, error, decision: <decision name>, why, us}
    // with no strike: the game calls decide with it.
    godot::Dictionary standin_shot(int64_t seed, const godot::Dictionary &context);
    // The stand-in's choice among the options for `player`, as a decision name; "" when there are
    // none. Logs opponent.decision.
    godot::String standin_choice(int64_t player, int64_t seed);

    // Why the last call failed; "" when it didn't.
    godot::String last_error() const;

private:
    bool ready_ = false;
    bool started_ = false;
    ofs::phys::PhysicsParams params_;
    std::shared_ptr<const ofs::phys::TableShape> table_;
    ofs::rules::RefParams ref_;
    ofs::opponent::StandinParams standin_;
    std::unique_ptr<ofs::rules::TableOracle> oracle_;
    ofs::rules::MatchState match_;
    godot::String match_id_;
    mutable godot::String last_error_;
};
