#include "ofs/log_schema.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace ofs::log {

namespace {

// Like type_name(), but tells integers from floats so a mismatch message is useful.
std::string json_kind(const nlohmann::json& value) {
    if (value.is_number_integer()) return "integer";
    if (value.is_number_float()) return "number";
    return value.type_name();
}

// Empty when value has the schema type; otherwise the reason.
std::string type_mismatch(const nlohmann::json& value, const std::string& type) {
    bool ok = false;
    if (type == "string") ok = value.is_string();
    else if (type == "number") ok = value.is_number();
    else if (type == "integer") ok = value.is_number_integer();
    else if (type == "boolean") ok = value.is_boolean();
    else if (type == "object") ok = value.is_object();
    else if (type == "array") ok = value.is_array();
    else if (type == "any") ok = true;
    else return "has unknown schema type '" + type + "'";
    return ok ? std::string() : "should be " + type + ", got " + json_kind(value);
}

}  // namespace

nlohmann::json load_schema(const std::string& path) {
    try {
        std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
        if (!in) {
            return nlohmann::json::object();
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto schema = nlohmann::json::parse(text, nullptr, false);
        if (!schema.is_object()) {
            return nlohmann::json::object();
        }
        return schema;
    } catch (...) {
        return nlohmann::json::object();
    }
}

namespace {

std::optional<std::string> check_unguarded(const Event& event, const nlohmann::json& schema) {
    const auto events = schema.find("events");
    if (!schema.is_object() || events == schema.end() || !events->is_object()) {
        return "schema not loaded";
    }
    const auto entry = events->find(event.event);
    if (entry == events->end() || !entry->is_object()) {
        return "unknown event '" + event.event + "'";
    }

    const std::string prefix = "event '" + event.event + "': ";
    std::string problems;
    const auto add = [&problems, &prefix](const std::string& problem) {
        problems += (problems.empty() ? prefix : "; ") + problem;
    };

    const std::string subsystem = entry->value("subsystem", std::string());
    if (event.subsystem != subsystem) {
        add("subsystem '" + event.subsystem + "' should be '" + subsystem + "'");
    }

    const std::string level(level_name(event.level));
    const auto levels = entry->find("levels");
    bool level_ok = false;
    if (levels != entry->end() && levels->is_array()) {
        for (const auto& allowed : *levels) {
            level_ok = level_ok || (allowed.is_string() && allowed.get<std::string>() == level);
        }
    }
    if (!level_ok) {
        add("level '" + level + "' not allowed (allowed: " +
            (levels != entry->end() ? levels->dump() : std::string("none")) + ")");
    }

    if (!event.data.is_object()) {
        add("data should be object, got " + json_kind(event.data));
        return problems;
    }
    const auto fields = entry->find("fields");
    if (fields != entry->end() && fields->is_object()) {
        for (const auto& [name, spec] : fields->items()) {
            const auto value = event.data.find(name);
            if (value == event.data.end()) {
                if (spec.value("required", false)) {
                    add("missing required field '" + name + "'");
                }
                continue;
            }
            const std::string mismatch = type_mismatch(*value, spec.value("type", std::string("any")));
            if (!mismatch.empty()) {
                add("field '" + name + "' " + mismatch);
            }
        }
    }

    if (problems.empty()) {
        return std::nullopt;
    }
    return problems;
}

}  // namespace

std::optional<std::string> check(const Event& event, const nlohmann::json& schema) {
    // value() throws on a schema entry of the wrong shape; report that instead.
    try {
        return check_unguarded(event, schema);
    } catch (const std::exception& e) {
        return "event '" + event.event + "': malformed schema entry: " + e.what();
    }
}

}  // namespace ofs::log
