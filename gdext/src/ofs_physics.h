#pragma once

#include <memory>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "ofs/physics/params.hpp"
#include "ofs/physics/shot_record.hpp"
#include "ofs/physics/table_shape.hpp"

// The physics core for the game (spec 5): shots in, shot records out. All positions are core
// coordinates in metres (table_geometry.hpp); game/scripts/table_space.gd maps them to Godot.
//
// A ball list is an Array of Dictionaries {ball: String ("cue", "red", "yellow", ...), index: int
// (0-14 for reds, else 0), x: float, y: float}, the same shape OfsTable.opening_layout returns;
// balls are addressed by their position in that Array. An optional "pocket" (as final_balls gives
// it) of 0 or more takes that ball as already potted. A strike is a Dictionary {aim_rad, tip_x,
// tip_y, elevation_rad, speed_m_s} with the meanings of ofs::phys::CueStrike.

// One simulated shot. Read-only; created only by OfsPhysics.
class OfsShotRecord : public godot::RefCounted {
    GDCLASS(OfsShotRecord, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    // Doubles per ball in state_at(): x, y, vx, vy, wx, wy, wz, motion, pocket. motion is
    // ofs::phys::Motion as a number (0 stationary, 1 spinning, 2 sliding, 3 rolling, 4 pocketed);
    // pocket is the pocket index once potted, else -1.
    static constexpr int kStride = 9;

    double t_end() const;
    int ball_count() const;
    godot::String shot_id() const;
    // Every ball at time t (clamped to [0, t_end]), kStride doubles each, in ball-list order.
    // Empty, with app.error logged, if the core throws.
    godot::PackedFloat64Array state_at(double t) const;
    // Every event in time order: {t, kind (event_kind_name), a, b, body, from, to (motion
    // names), speed (record_query.hpp's event_impact_speed, m/s)}. Empty, with app.error logged,
    // if the core throws.
    godot::Array events() const;
    // record_query.hpp's sample_path, at most 2048 points. Empty for a ball out of range (logged
    // as physics.query_refused) or if the core throws (app.error).
    godot::PackedVector2Array path(int64_t ball, double max_step_s) const;
    // {v: [x, y], w: [x, y, z], elevation_used_rad, elevation_raised, squirt_rad, miscue};
    // empty when the shot had no strike.
    godot::Dictionary strike_outcome() const;
    // The ball list after the shot, in the same order, each with an extra "pocket": int
    // (-1 when still on the table) at full double precision.
    godot::Array final_balls() const;
    // [{kind, t, balls, detail}], ShotRecord::anomalies.
    godot::Array anomalies() const;
    double sim_us() const;

    // For OfsPhysics.
    void set_record(ofs::phys::ShotRecord record, std::shared_ptr<const ofs::phys::TableShape> table,
                    godot::String shot_id);

private:
    ofs::phys::ShotRecord record_;
    std::shared_ptr<const ofs::phys::TableShape> table_;
    godot::String shot_id_;
    // Built on the first events() call and handed out read-only, so callers can't change it.
    mutable godot::Array events_;
    mutable bool events_built_ = false;
};

class OfsPhysics : public godot::RefCounted {
    GDCLASS(OfsPhysics, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    // params_json is table_params.json's whole text, table_json table_shape.json's; params_hash
    // is the hash the game knows the parameters by (Params.current_hash()). False when either is
    // refused (the core logs params.invalid / table.invalid) and the previous configuration stays;
    // either way logs physics.configured.
    bool configure(const godot::String &params_json, const godot::String &params_hash,
                   const godot::String &table_json);
    bool is_ready() const;
    godot::String params_hash() const;
    godot::String table_hash() const;

    // The shot as played: seed from the caller, logged as shot.simulated with `context`
    // ({shot_id, mode, strike_mode}). null, with shot.rejected logged (the reason and the inputs as
    // passed), when not configured, the ball list or strike is malformed (unknown ball name,
    // missing key, non-finite number, a ball listed twice, cue_ball out of range) or the core
    // throws.
    godot::Ref<OfsShotRecord> simulate(const godot::Array &balls, int64_t cue_ball, const godot::Dictionary &strike,
                                       int64_t seed, const godot::Dictionary &context);
    // The noise-free prediction for guide level 3 (spec 8.4): seed 0, not logged. null when
    // simulate would refuse, with physics.query_refused logged once per distinct reason.
    godot::Ref<OfsShotRecord> predict(const godot::Array &balls, int64_t cue_ball, const godot::Dictionary &strike);
    // cue.hpp's min_cue_elevation for the strike's tip offsets (aim_rad, tip_x, tip_y read; the
    // rest ignored). 0 when not configured or the input is malformed, logged as predict's are.
    double min_cue_elevation(const godot::Array &balls, int64_t cue_ball, const godot::Dictionary &strike) const;
    // Why the last simulate, predict or min_cue_elevation call was refused; "" when it wasn't.
    godot::String last_error() const;

    // The outline the game draws (spec 6.3), from the configured table_shape.json:
    // {lines: [{name, kind ("nose" | "facing"), p1: [x, y], p2: [x, y], normal: [x, y]}], arcs:
    // [{name, centre: [x, y], radius, angle_start, angle_end, pocket (index)}], pockets: [{name,
    // corner, drop_centre: [x, y], drop_radius}], rail_height, rail_width}, in the file's order
    // (TableShape's indices, which events' `body` uses). Empty before configure succeeds.
    godot::Dictionary table_shape() const;
    // The configured parameter values by field name (physics_params_values); empty before configure.
    godot::Dictionary params_values() const;
    // rack.hpp's opening_rack as a ball list (cue ball first, reds 0-14, then the colours), with
    // the seeded micro-gaps of spec 7.7. Empty before configure.
    godot::Array opening_rack(int64_t seed) const;

    // json_text parsed and json_text[key] written back out as JSON text, keeping integers as
    // integers (GDScript's parser turns them into floats, which the parameter loader refuses for
    // integer fields). "" when json_text isn't an object or has no such key.
    static godot::String member_json(const godot::String &json_text, const godot::String &key);

private:
    godot::Ref<OfsShotRecord> run(const godot::Array &balls, int64_t cue_ball, const godot::Dictionary &strike,
                                  int64_t seed, const godot::Dictionary *context);

    bool ready_ = false;
    ofs::phys::PhysicsParams params_;
    std::shared_ptr<const ofs::phys::TableShape> table_;
    godot::String params_hash_;
    godot::String table_hash_;
    godot::Dictionary table_shape_;
    mutable godot::String last_error_;
};
