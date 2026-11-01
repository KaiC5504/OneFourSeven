#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "ofs/log.hpp"

namespace ofs::log {

// The schema format is described at the top of core/data/log_schema.json.
nlohmann::json load_schema(const std::string& path);

// Empty when the event matches its schema entry; otherwise every reason it doesn't, joined
// by "; ": schema not loaded, unknown event, wrong subsystem, level not allowed, data not an
// object, missing required field, wrong field type, or a schema entry that is malformed or
// names an unknown type.
std::optional<std::string> check(const Event& event, const nlohmann::json& schema);

}  // namespace ofs::log
