#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

// GDScript's door into ofs::log::Logger. Static methods only, so the Log autoload calls
// OfsLog.emit(...) without an instance.
class OfsLog : public godot::RefCounted {
    GDCLASS(OfsLog, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    // Sets the context, attaches a FileSink under log_dir (an absolute OS path, e.g.
    // ProjectSettings.globalize_path("user://logs")) and the upload RingBuffer. Returns the
    // session id. Calling it again replaces both sinks.
    static godot::String start(const godot::String &build, const godot::String &platform,
                               const godot::String &log_dir, int64_t max_file_bytes,
                               int64_t max_ring_bytes);
    static godot::String session();
    // level: "debug" | "info" | "warn" | "error". data is converted with JSON semantics
    // (Godot int -> integer, float -> number, Array/Dictionary nested).
    static void emit(const godot::String &subsystem, const godot::String &level,
                     const godot::String &event, const godot::Dictionary &data);
    static void flush();
    static godot::String file_path();
    // Upload queue (RingBuffer) for OfsDevlink.
    static godot::PackedStringArray take_batch(int64_t max_lines);
    static void restore_batch(const godot::PackedStringArray &lines);
    static int64_t queued_lines();
    static int64_t dropped_total();
    // ofs::params::content_hash of a JSON text; "" when the text is not valid JSON.
    static godot::String content_hash(const godot::String &json_text);
    static godot::String core_version();
};
