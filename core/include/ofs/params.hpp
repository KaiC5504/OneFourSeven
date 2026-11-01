#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace ofs::params {

// Canonical text: keys sorted, no whitespace, UTF-8 (nlohmann's dump()). tools/devlink.py
// produces the same bytes with json.dumps(sort_keys=True, separators=(",", ":"),
// ensure_ascii=False); docs/devlink-protocol.md lists shared test vectors.
std::string canonical(const nlohmann::json& value);

// FNV-1a 64 over the canonical text, as 16 lowercase hex digits. Identifies a parameter
// set in logs; not a security measure.
std::string content_hash(const nlohmann::json& value);
std::string fnv1a64_hex(std::string_view bytes);

}  // namespace ofs::params
