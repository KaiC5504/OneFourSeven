#include "ofs_log.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
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

namespace {

std::mutex g_sinks_mutex;
std::shared_ptr<ofs::log::FileSink> g_file_sink;
std::shared_ptr<ofs::log::RingBuffer> g_ring;

// Guards against self-referencing Arrays and Dictionaries, which would recurse forever.
constexpr int kMaxDepth = 64;

std::shared_ptr<ofs::log::RingBuffer> ring() {
    std::lock_guard<std::mutex> lock(g_sinks_mutex);
    return g_ring;
}

std::string to_std(const String &text) {
    const CharString utf8 = text.utf8();
    return std::string(utf8.get_data(), size_t(utf8.length()));
}

String from_std(std::string_view text) {
    return String::utf8(text.data(), int64_t(text.size()));
}

nlohmann::json to_json(const Variant &value, int depth);
nlohmann::json dictionary_to_json(const Dictionary &dictionary, int depth);

template <typename Packed>
nlohmann::json packed_numbers(const Variant &value) {
    const Packed packed = value;
    nlohmann::json out = nlohmann::json::array();
    for (int64_t i = 0; i < packed.size(); ++i) {
        out.push_back(packed[i]);
    }
    return out;
}

nlohmann::json array_to_json(const Array &array, int depth) {
    nlohmann::json out = nlohmann::json::array();
    for (int64_t i = 0; i < array.size(); ++i) {
        out.push_back(to_json(array[i], depth + 1));
    }
    return out;
}

nlohmann::json to_json(const Variant &value, int depth) {
    if (depth > kMaxDepth) {
        return "<too deep>";
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
            return array_to_json(value, depth);
        case Variant::DICTIONARY:
            return dictionary_to_json(value, depth);
        case Variant::PACKED_BYTE_ARRAY:
            return packed_numbers<PackedByteArray>(value);
        case Variant::PACKED_INT32_ARRAY:
            return packed_numbers<PackedInt32Array>(value);
        case Variant::PACKED_INT64_ARRAY:
            return packed_numbers<PackedInt64Array>(value);
        case Variant::PACKED_FLOAT32_ARRAY:
            return packed_numbers<PackedFloat32Array>(value);
        case Variant::PACKED_FLOAT64_ARRAY:
            return packed_numbers<PackedFloat64Array>(value);
        case Variant::PACKED_STRING_ARRAY: {
            const PackedStringArray packed = value;
            nlohmann::json out = nlohmann::json::array();
            for (int64_t i = 0; i < packed.size(); ++i) {
                out.push_back(to_std(packed[i]));
            }
            return out;
        }
        case Variant::PACKED_VECTOR2_ARRAY: {
            const PackedVector2Array packed = value;
            nlohmann::json out = nlohmann::json::array();
            for (int64_t i = 0; i < packed.size(); ++i) {
                out.push_back({double(packed[i].x), double(packed[i].y)});
            }
            return out;
        }
        case Variant::PACKED_VECTOR3_ARRAY: {
            const PackedVector3Array packed = value;
            nlohmann::json out = nlohmann::json::array();
            for (int64_t i = 0; i < packed.size(); ++i) {
                out.push_back({double(packed[i].x), double(packed[i].y), double(packed[i].z)});
            }
            return out;
        }
        default:
            return to_std(value.stringify());
    }
}

nlohmann::json dictionary_to_json(const Dictionary &dictionary, int depth) {
    nlohmann::json out = nlohmann::json::object();
    if (depth > kMaxDepth) {
        return out;
    }
    const Array keys = dictionary.keys();
    for (int64_t i = 0; i < keys.size(); ++i) {
        const Variant key = keys[i];
        const Variant::Type type = key.get_type();
        const String name = (type == Variant::STRING || type == Variant::STRING_NAME) ? String(key) : key.stringify();
        out[to_std(name)] = to_json(dictionary[key], depth + 1);
    }
    return out;
}

}  // namespace

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

String OfsLog::start(const String &build, const String &platform, const String &log_dir, int64_t max_file_bytes,
                     int64_t max_ring_bytes) {
    auto &logger = ofs::log::Logger::instance();
    const std::string session = ofs::log::new_session_id();
    logger.configure({session, to_std(build), to_std(platform)});

    auto file_sink = std::make_shared<ofs::log::FileSink>(to_std(log_dir), session,
                                                          size_t(std::max<int64_t>(max_file_bytes, 0)));
    auto ring_buffer = std::make_shared<ofs::log::RingBuffer>(size_t(std::max<int64_t>(max_ring_bytes, 0)));
    {
        std::lock_guard<std::mutex> lock(g_sinks_mutex);
        logger.clear_sinks();
        logger.add_sink(file_sink);
        logger.add_sink(ring_buffer);
        g_file_sink = std::move(file_sink);
        g_ring = std::move(ring_buffer);
    }
    return from_std(session);
}

String OfsLog::session() {
    return from_std(ofs::log::Logger::instance().context().session);
}

void OfsLog::emit(const String &subsystem, const String &level, const String &event, const Dictionary &data) {
    nlohmann::json json = dictionary_to_json(data, 0);
    ofs::log::Level parsed = ofs::log::Level::error;
    const std::string level_text = to_std(level);
    if (!ofs::log::parse_level(level_text, parsed)) {
        // A mistyped level must not lose the event, so it goes out as an error naming the level.
        json["invalid_level"] = level_text;
    }
    ofs::log::Logger::instance().emit(to_std(subsystem), parsed, to_std(event), std::move(json));
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

PackedStringArray OfsLog::take_batch(int64_t max_lines) {
    PackedStringArray out;
    const auto buffer = ring();
    if (!buffer || max_lines <= 0) {
        return out;
    }
    for (const std::string &line : buffer->take(size_t(max_lines))) {
        out.push_back(from_std(line));
    }
    return out;
}

void OfsLog::restore_batch(const PackedStringArray &lines) {
    const auto buffer = ring();
    if (!buffer) {
        return;
    }
    std::vector<std::string> restored;
    restored.reserve(size_t(lines.size()));
    for (int64_t i = 0; i < lines.size(); ++i) {
        restored.push_back(to_std(lines[i]));
    }
    buffer->restore(std::move(restored));
}

int64_t OfsLog::queued_lines() {
    const auto buffer = ring();
    return buffer ? int64_t(buffer->size_lines()) : 0;
}

int64_t OfsLog::dropped_total() {
    const auto buffer = ring();
    return buffer ? int64_t(buffer->dropped()) : 0;
}

String OfsLog::content_hash(const String &json_text) {
    // Exceptions are off in the extension (godot-cpp), so parse without them; nlohmann
    // rejects invalid UTF-8 while parsing, which keeps the later dump() from throwing.
    const nlohmann::json value = nlohmann::json::parse(to_std(json_text), nullptr, false);
    if (value.is_discarded()) {
        return String();
    }
    return from_std(ofs::params::content_hash(value));
}

String OfsLog::core_version() {
    return from_std(ofs::core_version());
}
