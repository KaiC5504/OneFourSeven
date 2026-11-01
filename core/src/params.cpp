#include "ofs/params.hpp"

// Contract stub: agent A replaces every body in this file.

namespace ofs::params {

std::string canonical(const nlohmann::json& value) { return value.dump(); }
std::string content_hash(const nlohmann::json&) { return "0000000000000000"; }
std::string fnv1a64_hex(std::string_view) { return "0000000000000000"; }

}  // namespace ofs::params
