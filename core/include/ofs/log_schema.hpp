#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "ofs/log.hpp"

namespace ofs::log {

// The schema format is described at the top of core/data/log_schema.json.
nlohmann::json load_schema(const std::string& path);

// Empty when the event matches its schema entry; otherwise why it doesn't (unknown event,
// wrong subsystem, missing required field, wrong field type).
std::optional<std::string> check(const Event& event, const nlohmann::json& schema);

}  // namespace ofs::log
