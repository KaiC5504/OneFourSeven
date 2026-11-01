#include "ofs_log.h"

// Contract stub: agent C replaces every body in this file.

using namespace godot;

void OfsLog::_bind_methods() {
    ClassDB::bind_static_method("OfsLog", D_METHOD("start", "build", "platform", "log_dir", "max_file_bytes", "max_ring_bytes"), &OfsLog::start);
    ClassDB::bind_static_method("OfsLog", D_METHOD("session"), &OfsLog::session);
    ClassDB::bind_static_method("OfsLog", D_METHOD("emit", "subsystem", "level", "event", "data"), &OfsLog::emit);
    ClassDB::bind_static_method("OfsLog", D_METHOD("flush"), &OfsLog::flush);
    ClassDB::bind_static_method("OfsLog", D_METHOD("file_path"), &OfsLog::file_path);
    ClassDB::bind_static_method("OfsLog", D_METHOD("take_batch", "max_lines"), &OfsLog::take_batch);
    ClassDB::bind_static_method("OfsLog", D_METHOD("restore_batch", "lines"), &OfsLog::restore_batch);
    ClassDB::bind_static_method("OfsLog", D_METHOD("queued_lines"), &OfsLog::queued_lines);
    ClassDB::bind_static_method("OfsLog", D_METHOD("dropped_total"), &OfsLog::dropped_total);
    ClassDB::bind_static_method("OfsLog", D_METHOD("content_hash", "json_text"), &OfsLog::content_hash);
    ClassDB::bind_static_method("OfsLog", D_METHOD("core_version"), &OfsLog::core_version);
}

String OfsLog::start(const String &, const String &, const String &, int64_t, int64_t) { return String(); }
String OfsLog::session() { return String(); }
void OfsLog::emit(const String &, const String &, const String &, const Dictionary &) {}
void OfsLog::flush() {}
String OfsLog::file_path() { return String(); }
PackedStringArray OfsLog::take_batch(int64_t) { return PackedStringArray(); }
void OfsLog::restore_batch(const PackedStringArray &) {}
int64_t OfsLog::queued_lines() { return 0; }
int64_t OfsLog::dropped_total() { return 0; }
String OfsLog::content_hash(const String &) { return String(); }
String OfsLog::core_version() { return String(); }
