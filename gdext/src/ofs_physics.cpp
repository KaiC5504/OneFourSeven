#include "ofs_physics.h"

#include <utility>

using namespace godot;

void OfsShotRecord::_bind_methods() {
    ClassDB::bind_method(D_METHOD("t_end"), &OfsShotRecord::t_end);
    ClassDB::bind_method(D_METHOD("ball_count"), &OfsShotRecord::ball_count);
    ClassDB::bind_method(D_METHOD("shot_id"), &OfsShotRecord::shot_id);
    ClassDB::bind_method(D_METHOD("state_at", "t"), &OfsShotRecord::state_at);
    ClassDB::bind_method(D_METHOD("events"), &OfsShotRecord::events);
    ClassDB::bind_method(D_METHOD("path", "ball", "max_step_s"), &OfsShotRecord::path);
    ClassDB::bind_method(D_METHOD("strike_outcome"), &OfsShotRecord::strike_outcome);
    ClassDB::bind_method(D_METHOD("final_balls"), &OfsShotRecord::final_balls);
    ClassDB::bind_method(D_METHOD("anomalies"), &OfsShotRecord::anomalies);
    ClassDB::bind_method(D_METHOD("sim_us"), &OfsShotRecord::sim_us);
    BIND_CONSTANT(kStride);
}

double OfsShotRecord::t_end() const { return record_.t_end; }
int OfsShotRecord::ball_count() const { return int(record_.final_state.balls.size()); }
String OfsShotRecord::shot_id() const { return shot_id_; }
PackedFloat64Array OfsShotRecord::state_at(double) const { return {}; }
Array OfsShotRecord::events() const { return {}; }
PackedVector2Array OfsShotRecord::path(int, double) const { return {}; }
Dictionary OfsShotRecord::strike_outcome() const { return {}; }
Array OfsShotRecord::final_balls() const { return {}; }
Array OfsShotRecord::anomalies() const { return {}; }
double OfsShotRecord::sim_us() const { return record_.sim_us; }

void OfsShotRecord::set_record(ofs::phys::ShotRecord record, std::shared_ptr<const ofs::phys::TableShape> table,
                               String shot_id) {
    record_ = std::move(record);
    table_ = std::move(table);
    shot_id_ = shot_id;
}

void OfsPhysics::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "params_json", "params_hash", "table_json"), &OfsPhysics::configure);
    ClassDB::bind_method(D_METHOD("is_ready"), &OfsPhysics::is_ready);
    ClassDB::bind_method(D_METHOD("params_hash"), &OfsPhysics::params_hash);
    ClassDB::bind_method(D_METHOD("table_hash"), &OfsPhysics::table_hash);
    ClassDB::bind_method(D_METHOD("simulate", "balls", "cue_ball", "strike", "seed", "context"), &OfsPhysics::simulate);
    ClassDB::bind_method(D_METHOD("predict", "balls", "cue_ball", "strike"), &OfsPhysics::predict);
    ClassDB::bind_method(D_METHOD("min_cue_elevation", "balls", "cue_ball", "strike"), &OfsPhysics::min_cue_elevation);
    ClassDB::bind_method(D_METHOD("table_shape"), &OfsPhysics::table_shape);
    ClassDB::bind_method(D_METHOD("params_values"), &OfsPhysics::params_values);
    ClassDB::bind_method(D_METHOD("opening_rack", "seed"), &OfsPhysics::opening_rack);
    ClassDB::bind_static_method("OfsPhysics", D_METHOD("member_json", "json_text", "key"), &OfsPhysics::member_json);
}

bool OfsPhysics::configure(const String &, const String &, const String &) { return false; }
bool OfsPhysics::is_ready() const { return ready_; }
String OfsPhysics::params_hash() const { return params_hash_; }
String OfsPhysics::table_hash() const { return table_hash_; }

Ref<OfsShotRecord> OfsPhysics::simulate(const Array &, int, const Dictionary &, int64_t, const Dictionary &) {
    return {};
}

Ref<OfsShotRecord> OfsPhysics::predict(const Array &, int, const Dictionary &) { return {}; }
double OfsPhysics::min_cue_elevation(const Array &, int, const Dictionary &) const { return 0.0; }
Dictionary OfsPhysics::table_shape() const { return {}; }
Dictionary OfsPhysics::params_values() const { return {}; }
Array OfsPhysics::opening_rack(int64_t) const { return {}; }
String OfsPhysics::member_json(const String &, const String &) { return {}; }
