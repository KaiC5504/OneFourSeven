#include "ofs/log_schema.hpp"

// Contract stub: agent A replaces every body in this file.

namespace ofs::log {

nlohmann::json load_schema(const std::string&) { return nlohmann::json::object(); }
std::optional<std::string> check(const Event&, const nlohmann::json&) { return std::nullopt; }

}  // namespace ofs::log
