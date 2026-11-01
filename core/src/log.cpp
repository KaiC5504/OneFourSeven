#include "ofs/log.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <random>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <share.h>
#endif

namespace ofs::log {

namespace fs = std::filesystem;

std::string_view level_name(Level level) {
    switch (level) {
        case Level::debug: return "debug";
        case Level::info: return "info";
        case Level::warn: return "warn";
        case Level::error: return "error";
    }
    return "info";
}

bool parse_level(std::string_view name, Level& out) {
    for (Level level : {Level::debug, Level::info, Level::warn, Level::error}) {
        if (name == level_name(level)) {
            out = level;
            return true;
        }
    }
    return false;
}

std::string to_jsonl(const Event& event) {
    nlohmann::ordered_json line;
    line["ts"] = event.ts;
    line["session"] = event.session;
    line["build"] = event.build;
    line["platform"] = event.platform;
    line["subsystem"] = event.subsystem;
    line["level"] = level_name(event.level);
    line["event"] = event.event;
    line["data"] = event.data;
    // dump() writes NaN and infinities as null. Invalid UTF-8 becomes U+FFFD rather than
    // throwing, so an event with a mangled string from the engine still reaches the log.
    return line.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
}

bool from_jsonl(std::string_view line, Event& out) {
    const auto j = nlohmann::json::parse(line.begin(), line.end(), nullptr, false);
    if (!j.is_object() || j.size() != 8) {
        return false;
    }
    const auto string_at = [&j](const char* key, std::string& dest) {
        const auto it = j.find(key);
        if (it == j.end() || !it->is_string()) {
            return false;
        }
        dest = it->get<std::string>();
        return true;
    };
    Event event;
    std::string level;
    const auto ts = j.find("ts");
    const auto data = j.find("data");
    if (ts == j.end() || !ts->is_number() || data == j.end() || !data->is_object()) {
        return false;
    }
    event.ts = ts->get<double>();
    // The parser turns an out-of-range literal such as 1e999 into infinity.
    if (!std::isfinite(event.ts)) {
        return false;
    }
    if (!string_at("session", event.session) || !string_at("build", event.build) ||
        !string_at("platform", event.platform) || !string_at("subsystem", event.subsystem) ||
        !string_at("level", level) || !string_at("event", event.event) ||
        !parse_level(level, event.level)) {
        return false;
    }
    event.data = *data;
    out = std::move(event);
    return true;
}

std::string new_session_id() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &utc);
    std::random_device rd;
    char suffix[8];
    std::snprintf(suffix, sizeof suffix, "%06x", static_cast<unsigned>(rd() & 0xffffffu));
    return std::string(stamp) + "-" + suffix;
}

namespace {

std::FILE* open_append(const fs::path& path, int& err) {
#ifdef _WIN32
    // Wide API so a non-ASCII user folder works whatever the ANSI code page. Shared, not
    // _wfopen_s, which locks other readers out of a log that is meant to be tailed.
    std::FILE* file = _wfsopen(path.c_str(), L"ab", _SH_DENYNO);
    err = file == nullptr ? errno : 0;
    return file;
#else
    std::FILE* file = std::fopen(path.c_str(), "ab");
    err = file == nullptr ? errno : 0;
    return file;
#endif
}

void prune(const fs::path& directory, const fs::path& keep, std::size_t max_total_bytes) {
    std::error_code ec;
    std::vector<std::pair<fs::path, std::uintmax_t>> files;
    std::uintmax_t total = 0;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entry_ec;
        if (!it->is_regular_file(entry_ec) || it->path().extension() != ".jsonl") {
            continue;
        }
        const std::uintmax_t size = it->file_size(entry_ec);
        if (entry_ec) {
            continue;
        }
        files.emplace_back(it->path(), size);
        total += size;
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.first.filename() < b.first.filename(); });
    for (const auto& [path, size] : files) {
        if (total <= max_total_bytes) {
            break;
        }
        if (path.filename() == keep.filename()) {
            continue;
        }
        std::error_code remove_ec;
        if (fs::remove(path, remove_ec)) {
            total -= size;
        }
    }
}

}  // namespace

struct FileSink::Impl {
    std::mutex mutex;
    std::string path;
    std::FILE* file = nullptr;
    // Why the file could not be opened; the sink then drops every write.
    std::string error;
};

FileSink::FileSink(std::string directory, std::string session, std::size_t max_total_bytes)
    : impl_(std::make_unique<Impl>()) {
    const fs::path dir = fs::u8path(directory);
    const fs::path file = dir / fs::u8path(session + ".jsonl");
    impl_->path = file.u8string();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        impl_->error = "create_directories failed: " + ec.message();
        return;
    }
    prune(dir, file, max_total_bytes);
    int err = 0;
    impl_->file = open_append(file, err);
    if (impl_->file == nullptr) {
        impl_->error = "open failed: " + std::error_code(err, std::generic_category()).message();
    }
}

FileSink::~FileSink() {
    if (impl_->file != nullptr) {
        std::fclose(impl_->file);
    }
}

void FileSink::write(const Event&, const std::string& line) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->file == nullptr) {
        return;
    }
    std::fwrite(line.data(), 1, line.size(), impl_->file);
    std::fputc('\n', impl_->file);
    std::fflush(impl_->file);
}

void FileSink::flush() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->file != nullptr) {
        std::fflush(impl_->file);
    }
}

const std::string& FileSink::path() const { return impl_->path; }

struct RingBuffer::Impl {
    mutable std::mutex mutex;
    std::deque<std::string> lines;
    std::size_t bytes = 0;
    std::size_t max_bytes = 0;
    std::uint64_t dropped = 0;

    void enforce_cap() {
        while (bytes > max_bytes && !lines.empty()) {
            bytes -= lines.front().size() + 1;
            lines.pop_front();
            ++dropped;
        }
    }
};

RingBuffer::RingBuffer(std::size_t max_bytes) : impl_(std::make_unique<Impl>()) {
    impl_->max_bytes = max_bytes;
}

RingBuffer::~RingBuffer() = default;

void RingBuffer::write(const Event&, const std::string& line) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->bytes += line.size() + 1;
    impl_->lines.push_back(line);
    impl_->enforce_cap();
}

std::vector<std::string> RingBuffer::take(std::size_t max_lines) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const std::size_t n = std::min(max_lines, impl_->lines.size());
    std::vector<std::string> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        impl_->bytes -= impl_->lines.front().size() + 1;
        out.push_back(std::move(impl_->lines.front()));
        impl_->lines.pop_front();
    }
    return out;
}

void RingBuffer::restore(std::vector<std::string> lines) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        impl_->bytes += it->size() + 1;
        impl_->lines.push_front(std::move(*it));
    }
    impl_->enforce_cap();
}

std::size_t RingBuffer::size_lines() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->lines.size();
}

std::uint64_t RingBuffer::dropped() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->dropped;
}

struct Logger::Impl {
    mutable std::mutex mutex;
    Context context;
    std::vector<std::shared_ptr<Sink>> sinks;

    Event make_event(std::string_view subsystem, Level level, std::string_view name) const {
        Event event;
        const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
        event.ts = static_cast<double>(
                       std::chrono::duration_cast<std::chrono::microseconds>(since_epoch).count()) /
                   1e6;
        event.session = context.session;
        event.build = context.build;
        event.platform = context.platform;
        event.subsystem = subsystem;
        event.level = level;
        event.event = name;
        return event;
    }

    // Returns the first failure's message instead of throwing.
    std::string write_all(const Event& event, const std::string& line) {
        std::string failure;
        for (const auto& sink : sinks) {
            try {
                sink->write(event, line);
            } catch (const std::exception& e) {
                if (failure.empty()) failure = std::string("sink write failed: ") + e.what();
            } catch (...) {
                if (failure.empty()) failure = "sink write failed: unknown exception";
            }
        }
        return failure;
    }

    // Goes straight to the sinks, never back through emit, so a failing sink or
    // serializer cannot recurse.
    void write_fallback(std::string_view about, const std::string& message) noexcept {
        try {
            Event event = make_event("app", Level::error, "app.error");
            event.data = {{"kind", "log"},
                          {"message", about.empty() ? message
                                                    : "event " + std::string(about) + ": " + message}};
            const std::string line = to_jsonl(event);
            for (const auto& sink : sinks) {
                try {
                    sink->write(event, line);
                } catch (...) {
                }
            }
        } catch (...) {
        }
    }
};

Logger::Logger() : impl_(std::make_unique<Impl>()) {}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::configure(Context context) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->context = std::move(context);
}

Context Logger::context() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->context;
}

void Logger::add_sink(std::shared_ptr<Sink> sink) {
    if (!sink) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->sinks.push_back(std::move(sink));
}

void Logger::clear_sinks() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->sinks.clear();
}

void Logger::emit(std::string_view subsystem, Level level, std::string_view event,
                  nlohmann::json data) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::string failure;
    try {
        Event e = impl_->make_event(subsystem, level, event);
        // A braced {} argument arrives as null; treat it as no fields.
        if (data.is_null()) {
            data = nlohmann::json::object();
        }
        if (!data.is_object()) {
            failure = std::string("data must be an object, got ") + data.type_name();
        } else {
            e.data = std::move(data);
            failure = impl_->write_all(e, to_jsonl(e));
        }
    } catch (const std::exception& ex) {
        failure = ex.what();
    } catch (...) {
        failure = "unknown exception";
    }
    if (!failure.empty()) {
        impl_->write_fallback(event, failure);
    }
}

bool Logger::emit_line(std::string_view line) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::string failure;
    std::string name;
    try {
        Event e;
        if (!from_jsonl(line, e)) {
            return false;
        }
        name = e.event;
        failure = impl_->write_all(e, to_jsonl(e));
    } catch (const std::exception& ex) {
        failure = ex.what();
    } catch (...) {
        failure = "unknown exception";
    }
    if (!failure.empty()) {
        impl_->write_fallback(name, failure);
    }
    return true;
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& sink : impl_->sinks) {
        try {
            sink->flush();
        } catch (...) {
        }
    }
}

}  // namespace ofs::log

// Fix-round contract stub: agent fix-core replaces these.
namespace ofs::log {
std::string FileSink::error() const { return {}; }
void assert_failed(const char*, const char*, int, const char*) {}
}  // namespace ofs::log
