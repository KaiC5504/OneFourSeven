#include "ofs_log.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/rect2i.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "ofs/log.hpp"
#include "ofs/params.hpp"
#include "ofs/version.hpp"

using namespace godot;
namespace fs = std::filesystem;

namespace {

std::mutex g_sinks_mutex;
// Guarded by g_sinks_mutex.
std::shared_ptr<ofs::log::FileSink> g_file_sink;
std::string g_log_dir;
std::string g_reported_sink_error;
bool g_test_sink_error_set = false;
std::string g_test_sink_error;
// Pruned lines found by start(), logged after the first event so app.start stays first.
int64_t g_pending_dropped = 0;
int64_t g_dropped_total = 0;

// Guards against self-referencing Arrays and Dictionaries, which would recurse forever, and
// against one call turning a huge container into a line nobody can upload.
constexpr int kMaxDepth = 64;
constexpr int kMaxValues = 10000;
constexpr const char *kTruncated = "<truncated>";

struct Budget {
    int left = kMaxValues;

    bool take() {
        if (left <= 0) {
            return false;
        }
        --left;
        return true;
    }
};

std::string to_std(const String &text) {
    const CharString utf8 = text.utf8();
    return std::string(utf8.get_data(), size_t(utf8.length()));
}

String from_std(std::string_view text) {
    return String::utf8(text.data(), int64_t(text.size()));
}

nlohmann::json to_json(const Variant &value, int depth, Budget &budget);
nlohmann::json dictionary_to_json(const Dictionary &dictionary, int depth, Budget &budget);

template <typename Packed, typename Convert>
nlohmann::json packed_to_json(const Variant &value, Budget &budget, Convert convert) {
    const Packed packed = value;
    nlohmann::json out = nlohmann::json::array();
    for (int64_t i = 0; i < packed.size(); ++i) {
        if (!budget.take()) {
            out.push_back(kTruncated);
            break;
        }
        out.push_back(convert(packed[i]));
    }
    return out;
}

template <typename Packed>
nlohmann::json packed_numbers(const Variant &value, Budget &budget) {
    return packed_to_json<Packed>(value, budget, [](const auto &number) { return number; });
}

nlohmann::json array_to_json(const Array &array, int depth, Budget &budget) {
    nlohmann::json out = nlohmann::json::array();
    for (int64_t i = 0; i < array.size(); ++i) {
        if (budget.left <= 0) {
            out.push_back(kTruncated);
            break;
        }
        out.push_back(to_json(array[i], depth + 1, budget));
    }
    return out;
}

nlohmann::json to_json(const Variant &value, int depth, Budget &budget) {
    if (depth > kMaxDepth) {
        return "<too deep>";
    }
    if (!budget.take()) {
        return kTruncated;
    }
    switch (value.get_type()) {
        case Variant::NIL:
            return nullptr;
        case Variant::BOOL:
            return bool(value);
        case Variant::INT:
            return int64_t(value);
        case Variant::FLOAT:
            return double(value);
        case Variant::STRING:
        case Variant::STRING_NAME:
            return to_std(String(value));
        case Variant::VECTOR2: {
            const Vector2 v = value;
            return {double(v.x), double(v.y)};
        }
        case Variant::VECTOR2I: {
            const Vector2i v = value;
            return {int64_t(v.x), int64_t(v.y)};
        }
        case Variant::VECTOR3: {
            const Vector3 v = value;
            return {double(v.x), double(v.y), double(v.z)};
        }
        case Variant::VECTOR3I: {
            const Vector3i v = value;
            return {int64_t(v.x), int64_t(v.y), int64_t(v.z)};
        }
        case Variant::RECT2: {
            const Rect2 r = value;
            return {double(r.position.x), double(r.position.y), double(r.size.x), double(r.size.y)};
        }
        case Variant::RECT2I: {
            const Rect2i r = value;
            return {int64_t(r.position.x), int64_t(r.position.y), int64_t(r.size.x), int64_t(r.size.y)};
        }
        case Variant::ARRAY:
            return array_to_json(value, depth, budget);
        case Variant::DICTIONARY:
            return dictionary_to_json(value, depth, budget);
        case Variant::PACKED_BYTE_ARRAY:
            return packed_numbers<PackedByteArray>(value, budget);
        case Variant::PACKED_INT32_ARRAY:
            return packed_numbers<PackedInt32Array>(value, budget);
        case Variant::PACKED_INT64_ARRAY:
            return packed_numbers<PackedInt64Array>(value, budget);
        case Variant::PACKED_FLOAT32_ARRAY:
            return packed_to_json<PackedFloat32Array>(value, budget, [](float f) { return double(f); });
        case Variant::PACKED_FLOAT64_ARRAY:
            return packed_numbers<PackedFloat64Array>(value, budget);
        case Variant::PACKED_STRING_ARRAY:
            return packed_to_json<PackedStringArray>(value, budget, [](const String &s) { return to_std(s); });
        case Variant::PACKED_VECTOR2_ARRAY:
            return packed_to_json<PackedVector2Array>(value, budget, [](const Vector2 &v) {
                return nlohmann::json{double(v.x), double(v.y)};
            });
        case Variant::PACKED_VECTOR3_ARRAY:
            return packed_to_json<PackedVector3Array>(value, budget, [](const Vector3 &v) {
                return nlohmann::json{double(v.x), double(v.y), double(v.z)};
            });
        default:
            return to_std(value.stringify());
    }
}

nlohmann::json dictionary_to_json(const Dictionary &dictionary, int depth, Budget &budget) {
    nlohmann::json out = nlohmann::json::object();
    if (depth > kMaxDepth) {
        return out;
    }
    const Array keys = dictionary.keys();
    for (int64_t i = 0; i < keys.size(); ++i) {
        if (budget.left <= 0) {
            out[kTruncated] = true;
            break;
        }
        const Variant key = keys[i];
        const Variant::Type type = key.get_type();
        const std::string text = to_std((type == Variant::STRING || type == Variant::STRING_NAME) ? String(key)
                                                                                              : key.stringify());
        // 7 and "7" are different Godot keys but one JSON key; keep both values.
        std::string name = text;
        for (int n = 2; out.contains(name); ++n) {
            name = text + "#" + std::to_string(n);
        }
        out[name] = to_json(dictionary[key], depth + 1, budget);
    }
    return out;
}

// FileSink deletes the oldest session files when the directory is over its cap. Lines in
// them the PC never got are counted first, reading only the files about to go.
std::shared_ptr<ofs::log::FileSink> open_sink(const std::string &directory, const std::string &session,
                                              size_t max_bytes, int64_t &pruned_lines) {
    pruned_lines = 0;
    const std::vector<ofs_upload::SessionFile> files = ofs_upload::list(directory);
    uint64_t total = 0;
    for (const auto &file : files) {
        total += file.size;
    }
    const std::string keep = session + ".jsonl";
    std::vector<std::pair<std::string, int64_t>> at_risk;
    if (total > max_bytes) {
        std::string ignored;
        const ofs_upload::Offsets offsets = ofs_upload::load(directory, ignored);
        for (const auto &file : files) {
            if (total <= max_bytes) {
                break;
            }
            if (file.name == keep) {
                continue;
            }
            total -= file.size;
            const auto it = offsets.find(file.name);
            const uint64_t from = it == offsets.end() ? 0 : std::min(it->second, file.size);
            const int64_t lines =
                from < file.size ? ofs_upload::count_lines(ofs_upload::join(directory, file.name), from, file.size) : 0;
            at_risk.emplace_back(file.name, lines);
        }
    }
    auto sink = std::make_shared<ofs::log::FileSink>(directory, session, max_bytes);
    if (!at_risk.empty()) {
        std::set<std::string> remaining;
        for (const auto &file : ofs_upload::list(directory)) {
            remaining.insert(file.name);
        }
        for (const auto &[name, lines] : at_risk) {
            if (lines > 0 && remaining.count(name) == 0) {
                pruned_lines += lines;
            }
        }
    }
    return sink;
}

}  // namespace

void OfsLog::_bind_methods() {
    ClassDB::bind_static_method("OfsLog", D_METHOD("start", "build", "platform", "log_dir", "max_file_bytes"), &OfsLog::start);
    ClassDB::bind_static_method("OfsLog", D_METHOD("session"), &OfsLog::session);
    ClassDB::bind_static_method("OfsLog", D_METHOD("emit", "subsystem", "level", "event", "data"), &OfsLog::emit);
    ClassDB::bind_static_method("OfsLog", D_METHOD("flush"), &OfsLog::flush);
    ClassDB::bind_static_method("OfsLog", D_METHOD("file_path"), &OfsLog::file_path);
    ClassDB::bind_static_method("OfsLog", D_METHOD("content_hash", "json_text"), &OfsLog::content_hash);
    ClassDB::bind_static_method("OfsLog", D_METHOD("core_version"), &OfsLog::core_version);
    ClassDB::bind_static_method("OfsLog", D_METHOD("set_test_sink_error", "message"), &OfsLog::set_test_sink_error);
    ClassDB::bind_static_method("OfsLog", D_METHOD("test_prune", "log_dir", "session", "max_file_bytes"), &OfsLog::test_prune);
}

String OfsLog::start(const String &build, const String &platform, const String &log_dir, int64_t max_file_bytes) {
    auto &logger = ofs::log::Logger::instance();
    const std::string session = ofs::log::new_session_id();
    logger.configure({session, to_std(build), to_std(platform)});

    const std::string directory = to_std(log_dir);
    int64_t pruned = 0;
    auto file_sink = open_sink(directory, session, size_t(std::max<int64_t>(max_file_bytes, 0)), pruned);
    {
        std::lock_guard<std::mutex> lock(g_sinks_mutex);
        logger.clear_sinks();
        logger.add_sink(file_sink);
        g_file_sink = std::move(file_sink);
        g_log_dir = directory;
        g_reported_sink_error.clear();
        g_pending_dropped += pruned;
    }
    return from_std(session);
}

String OfsLog::session() {
    return from_std(ofs::log::Logger::instance().context().session);
}

void OfsLog::emit(const String &subsystem, const String &level, const String &event, const Dictionary &data) {
    nlohmann::json json;
    try {
        Budget budget;
        json = dictionary_to_json(data, 0, budget);
    } catch (const std::exception &e) {
        json = {{"conversion_error", e.what()}};
    }
    ofs::log::Level parsed = ofs::log::Level::error;
    const std::string level_text = to_std(level);
    if (!ofs::log::parse_level(level_text, parsed)) {
        // A mistyped level must not lose the event, so it goes out as an error naming the level.
        json["invalid_level"] = level_text;
    }
    ofs::log::Logger::instance().emit(to_std(subsystem), parsed, to_std(event), std::move(json));
    check_sink();
}

void OfsLog::flush() {
    ofs::log::Logger::instance().flush();
}

String OfsLog::file_path() {
    std::lock_guard<std::mutex> lock(g_sinks_mutex);
    if (!g_file_sink) {
        return String();
    }
    return from_std(g_file_sink->path());
}

String OfsLog::content_hash(const String &json_text) {
    // Bad input is ordinary here, so parse without exceptions. canonical() refuses what JSON
    // can't carry (1e999 parses to infinity); that is "" too.
    const nlohmann::json value = nlohmann::json::parse(to_std(json_text), nullptr, false);
    if (value.is_discarded()) {
        return String();
    }
    try {
        return from_std(ofs::params::content_hash(value));
    } catch (const std::exception &) {
        return String();
    }
}

String OfsLog::core_version() {
    return from_std(ofs::core_version());
}

void OfsLog::check_sink() {
    int64_t dropped = 0;
    int64_t dropped_total = 0;
    std::string message;
    {
        std::lock_guard<std::mutex> lock(g_sinks_mutex);
        if (g_pending_dropped > 0) {
            dropped = g_pending_dropped;
            g_dropped_total += dropped;
            dropped_total = g_dropped_total;
            g_pending_dropped = 0;
        }
        const std::string error =
            g_test_sink_error_set ? g_test_sink_error : (g_file_sink ? g_file_sink->error() : std::string());
        if (error != g_reported_sink_error) {
            g_reported_sink_error = error;
            if (!error.empty()) {
                message = (g_file_sink ? g_file_sink->path() : std::string("log file")) + ": " + error;
            }
        }
    }
    auto &logger = ofs::log::Logger::instance();
    if (dropped > 0) {
        logger.emit("log", ofs::log::Level::warn, "log.dropped", {{"count", dropped}, {"total_dropped", dropped_total}});
    }
    if (!message.empty()) {
        // The line below may be headed for the very file that failed.
        std::fprintf(stderr, "OfsLog: %s\n", message.c_str());
        logger.emit("app", ofs::log::Level::error, "app.error", {{"kind", "log"}, {"message", message}});
    }
}

std::string OfsLog::sink_directory() {
    std::lock_guard<std::mutex> lock(g_sinks_mutex);
    return g_log_dir;
}

std::string OfsLog::sink_file_name() {
    std::lock_guard<std::mutex> lock(g_sinks_mutex);
    if (!g_file_sink) {
        return std::string();
    }
    return fs::u8path(g_file_sink->path()).filename().u8string();
}

void OfsLog::set_test_sink_error(const String &message) {
    std::lock_guard<std::mutex> lock(g_sinks_mutex);
    g_test_sink_error = to_std(message);
    g_test_sink_error_set = !message.is_empty();
}

int64_t OfsLog::test_prune(const String &log_dir, const String &session, int64_t max_file_bytes) {
    int64_t pruned = 0;
    open_sink(to_std(log_dir), to_std(session), size_t(std::max<int64_t>(max_file_bytes, 0)), pruned);
    std::lock_guard<std::mutex> lock(g_sinks_mutex);
    g_pending_dropped += pruned;
    return pruned;
}

namespace ofs_upload {

std::string join(const std::string &directory, const std::string &name) {
    return (fs::u8path(directory) / fs::u8path(name)).u8string();
}

std::vector<SessionFile> list(const std::string &directory) {
    std::vector<SessionFile> files;
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(directory), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entry_ec;
        if (!it->is_regular_file(entry_ec) || it->path().extension() != ".jsonl") {
            continue;
        }
        const std::uintmax_t size = it->file_size(entry_ec);
        if (entry_ec) {
            continue;
        }
        files.push_back({it->path().filename().u8string(), uint64_t(size)});
    }
    std::sort(files.begin(), files.end(), [](const SessionFile &a, const SessionFile &b) { return a.name < b.name; });
    return files;
}

Offsets load(const std::string &directory, std::string &error) {
    error.clear();
    Offsets offsets;
    const fs::path path = fs::u8path(join(directory, kStateFile));
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return offsets;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path.u8string();
        return offsets;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const nlohmann::json state = nlohmann::json::parse(text, nullptr, false);
    if (!state.is_object() || !state.contains("offsets") || !state.at("offsets").is_object()) {
        error = path.u8string() + " is not {\"offsets\": {...}}";
        return offsets;
    }
    for (const auto &[name, offset] : state.at("offsets").items()) {
        if (offset.is_number_unsigned()) {
            offsets[name] = offset.get<uint64_t>();
        } else {
            error = path.u8string() + ": offset of " + name + " is not a byte count";
        }
    }
    return offsets;
}

bool save(const std::string &directory, const Offsets &offsets, std::string &error) {
    nlohmann::json files = nlohmann::json::object();
    for (const auto &[name, offset] : offsets) {
        files[name] = offset;
    }
    const nlohmann::json state = {{"v", 1}, {"offsets", std::move(files)}};
    const std::string text = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const fs::path path = fs::u8path(join(directory, kStateFile));
    fs::path temp = path;
    temp += ".tmp";
    // Written aside and renamed over, so a crash mid-write can't leave half a state file.
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        if (!out) {
            error = "cannot write " + temp.u8string() + ": " + std::error_code(errno, std::generic_category()).message();
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec) {
        error = "cannot replace " + path.u8string() + ": " + ec.message();
        return false;
    }
    return true;
}

int64_t count_lines(const std::string &path, uint64_t from, uint64_t to) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) {
        return -1;
    }
    in.seekg(std::streamoff(from));
    if (!in) {
        return -1;
    }
    std::vector<char> buffer(64 * 1024);
    int64_t lines = 0;
    uint64_t left = to > from ? to - from : 0;
    while (left > 0) {
        in.read(buffer.data(), std::streamsize(std::min<uint64_t>(left, buffer.size())));
        const std::streamsize got = in.gcount();
        if (got <= 0) {
            break;
        }
        lines += std::count(buffer.data(), buffer.data() + got, '\n');
        left -= uint64_t(got);
    }
    return lines;
}

}  // namespace ofs_upload
