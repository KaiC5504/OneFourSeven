#pragma once

// Structured JSONL logging (spec 16.10). The event names and fields are defined in
// core/data/log_schema.json; docs/log-schema.md is the readable version.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace ofs::log {

enum class Level { debug, info, warn, error };

std::string_view level_name(Level level);
// Accepts the names level_name returns; anything else is false.
bool parse_level(std::string_view name, Level& out);

struct Event {
    double ts = 0.0;  // seconds since the Unix epoch, UTC, millisecond precision or better
    std::string session;
    std::string build;
    std::string platform;
    std::string subsystem;
    Level level = Level::info;
    std::string event;
    nlohmann::json data = nlohmann::json::object();
};

// One line, no trailing newline. Keys always in the order ts, session, build, platform,
// subsystem, level, event, data, so logs from every platform diff cleanly.
std::string to_jsonl(const Event& event);
// Parses a line written by to_jsonl (or by the GDScript side). False on anything else.
bool from_jsonl(std::string_view line, Event& out);

// "20261102-153012-a1b2c3": UTC start time plus 6 random hex digits.
std::string new_session_id();

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const Event& event, const std::string& line) = 0;
    virtual void flush() {}
};

// Writes <directory>/<session>.jsonl, flushing every line so a crash loses nothing already
// emitted. On construction deletes the oldest session files until the directory holds at
// most max_total_bytes, never deleting the current session.
class FileSink : public Sink {
public:
    FileSink(std::string directory, std::string session, std::size_t max_total_bytes);
    ~FileSink() override;
    void write(const Event& event, const std::string& line) override;
    void flush() override;
    const std::string& path() const;
    // Why the sink is inert or the last write failed; empty while healthy.
    std::string error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Recent lines kept in memory for upload over the phone link. When over max_bytes the
// oldest lines are dropped and counted.
class RingBuffer : public Sink {
public:
    explicit RingBuffer(std::size_t max_bytes);
    ~RingBuffer() override;
    void write(const Event& event, const std::string& line) override;
    // Removes and returns up to max_lines of the oldest lines.
    std::vector<std::string> take(std::size_t max_lines);
    // Puts lines taken earlier back at the front, e.g. after a failed upload.
    void restore(std::vector<std::string> lines);
    std::size_t size_lines() const;
    std::uint64_t dropped() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct Context {
    std::string session;
    std::string build;
    std::string platform;
};

// Process-wide, thread-safe. Events emitted before configure() carry empty context.
class Logger {
public:
    static Logger& instance();

    void configure(Context context);
    Context context() const;
    void add_sink(std::shared_ptr<Sink> sink);
    void clear_sinks();

    void emit(std::string_view subsystem, Level level, std::string_view event,
              nlohmann::json data = nlohmann::json::object());
    // For lines built elsewhere (the GDScript side); the line is validated by from_jsonl
    // and rewritten through to_jsonl, so every sink sees one canonical format.
    bool emit_line(std::string_view line);
    void flush();

private:
    Logger();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

inline void info(std::string_view subsystem, std::string_view event, nlohmann::json data = nlohmann::json::object()) {
    Logger::instance().emit(subsystem, Level::info, event, std::move(data));
}
inline void warn(std::string_view subsystem, std::string_view event, nlohmann::json data = nlohmann::json::object()) {
    Logger::instance().emit(subsystem, Level::warn, event, std::move(data));
}
inline void error(std::string_view subsystem, std::string_view event, nlohmann::json data = nlohmann::json::object()) {
    Logger::instance().emit(subsystem, Level::error, event, std::move(data));
}

// Logs app.error {kind: "assert", message: expr, file, line, function}, flushes, then
// aborts in debug builds; release builds log and carry on.
void assert_failed(const char* expr, const char* file, int line, const char* function);

}  // namespace ofs::log

#define OFS_ASSERT(expr) \
    ((expr) ? static_cast<void>(0) : ::ofs::log::assert_failed(#expr, __FILE__, __LINE__, __func__))
