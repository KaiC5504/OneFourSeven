#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

// GDScript's door into ofs::log::Logger. Static methods only, so the Log autoload calls
// OfsLog.emit(...) without an instance.
class OfsLog : public godot::RefCounted {
    GDCLASS(OfsLog, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    // Sets the context and attaches a FileSink under log_dir (an absolute OS path, e.g.
    // ProjectSettings.globalize_path("user://logs")). Returns the session id. Calling it
    // again replaces the sink. Logs log.dropped when the sink's size cap deleted lines that
    // had not reached the PC yet, and app.error kind "log" when the sink can't write.
    static godot::String start(const godot::String &build, const godot::String &platform,
                               const godot::String &log_dir, int64_t max_file_bytes);
    static godot::String session();
    // level: "debug" | "info" | "warn" | "error". data is converted with JSON semantics
    // (Godot int -> integer, float -> number, Array/Dictionary nested), at most 64 levels
    // deep and 10,000 values; a key that collides with another's text gets "#2", "#3", ...
    static void emit(const godot::String &subsystem, const godot::String &level,
                     const godot::String &event, const godot::Dictionary &data);
    static void flush();
    static godot::String file_path();
    // ofs::params::content_hash of a JSON text; "" when the text is not valid JSON.
    static godot::String content_hash(const godot::String &json_text);
    static godot::String core_version();

    // Logs app.error kind "log" when the FileSink reports a new error; emit() and OfsDevlink
    // call it, so a write failure later in the session is reported once.
    static void check_sink();
    // C++ only: where start() put the session file, empty before start().
    static std::string sink_directory();
    static std::string sink_file_name();

    // Test-only: stands in for FileSink::error() until called with "".
    static void set_test_sink_error(const godot::String &message);
    // Test-only: opens a FileSink for session in log_dir with the given cap, as start() does,
    // and returns how many not-yet-uploaded lines its pruning deleted; the next event is
    // followed by log.dropped, as after start().
    static int64_t test_prune(const godot::String &log_dir, const godot::String &session, int64_t max_file_bytes);
};

// Upload bookkeeping shared by OfsDevlink and OfsLog::start (docs/devlink-protocol.md,
// "What gets uploaded"): for each session file, the byte offset the PC acknowledged.
namespace ofs_upload {

constexpr const char *kStateFile = "upload_state.json";

struct SessionFile {
    std::string name;  // "<session>.jsonl"
    uint64_t size = 0;
};

using Offsets = std::map<std::string, uint64_t>;

std::string join(const std::string &directory, const std::string &name);
// The *.jsonl files in directory, oldest first (session ids start with their UTC start time).
std::vector<SessionFile> list(const std::string &directory);
// Missing state is empty, not an error; error explains a state file that exists but can't be used.
Offsets load(const std::string &directory, std::string &error);
bool save(const std::string &directory, const Offsets &offsets, std::string &error);
// '\n' bytes in [from, to) of the file, -1 when it can't be read.
int64_t count_lines(const std::string &path, uint64_t from, uint64_t to);

}  // namespace ofs_upload
