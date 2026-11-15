// Contract stub (M3): replaced by the binding's builder.

#include "ofs_match.h"

using namespace godot;

void OfsMatch::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "physics", "referee_json", "standin_json"), &OfsMatch::configure);
    ClassDB::bind_method(D_METHOD("is_ready"), &OfsMatch::is_ready);
    ClassDB::bind_method(D_METHOD("start", "config", "match_id"), &OfsMatch::start);
    ClassDB::bind_method(D_METHOD("in_progress"), &OfsMatch::in_progress);
    ClassDB::bind_method(D_METHOD("abandon"), &OfsMatch::abandon);
    ClassDB::bind_method(D_METHOD("match_state"), &OfsMatch::match_state);
    ClassDB::bind_method(D_METHOD("frame"), &OfsMatch::frame);
    ClassDB::bind_method(D_METHOD("balls"), &OfsMatch::balls);
    ClassDB::bind_method(D_METHOD("cue_ball"), &OfsMatch::cue_ball);
    ClassDB::bind_method(D_METHOD("place_cue", "x", "y"), &OfsMatch::place_cue);
    ClassDB::bind_method(D_METHOD("nearest_cue_placement", "x", "y"), &OfsMatch::nearest_cue_placement);
    ClassDB::bind_method(D_METHOD("judge", "record", "input", "context"), &OfsMatch::judge);
    ClassDB::bind_method(D_METHOD("decide", "kind", "player"), &OfsMatch::decide);
    ClassDB::bind_method(D_METHOD("next_frame"), &OfsMatch::next_frame);
    ClassDB::bind_method(D_METHOD("standin_shot", "seed", "context"), &OfsMatch::standin_shot);
    ClassDB::bind_method(D_METHOD("standin_choice", "player", "seed"), &OfsMatch::standin_choice);
    ClassDB::bind_method(D_METHOD("last_error"), &OfsMatch::last_error);
}

namespace {

Dictionary refused() {
    Dictionary out;
    out["ok"] = false;
    out["error"] = "not implemented";
    return out;
}

}  // namespace

bool OfsMatch::configure(const Ref<OfsPhysics> &, const String &, const String &) { return false; }
bool OfsMatch::is_ready() const { return ready_; }
Dictionary OfsMatch::start(const Dictionary &, const String &) { return refused(); }
bool OfsMatch::in_progress() const { return started_ && !match_.over; }
void OfsMatch::abandon() {}
Dictionary OfsMatch::match_state() const { return Dictionary(); }
Dictionary OfsMatch::frame() const { return Dictionary(); }
Array OfsMatch::balls() const { return Array(); }
int64_t OfsMatch::cue_ball() const { return 0; }
Dictionary OfsMatch::place_cue(double, double) { return refused(); }
Dictionary OfsMatch::nearest_cue_placement(double, double) const { return Dictionary(); }
Dictionary OfsMatch::judge(const Ref<OfsShotRecord> &, const Dictionary &, const Dictionary &) { return refused(); }
Dictionary OfsMatch::decide(const String &, int64_t) { return refused(); }
Dictionary OfsMatch::next_frame() { return refused(); }
Dictionary OfsMatch::standin_shot(int64_t, const Dictionary &) { return refused(); }
String OfsMatch::standin_choice(int64_t, int64_t) { return String(); }
String OfsMatch::last_error() const { return last_error_; }
