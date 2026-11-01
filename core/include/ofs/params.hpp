#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace ofs::params {

// Canonical text, the rule in docs/devlink-protocol.md that tools/devlink.py implements
// too: keys sorted, no whitespace, UTF-8 unescaped, strings escaped as Python's
// json.dumps(ensure_ascii=False) does, integers in decimal, other numbers as "%.17g" (plus
// ".0" when that has no '.' or 'e'). Empty when the value has no canonical form: NaN or an
// infinity, invalid UTF-8 (lone surrogates included), or a binary value. The protocol also
// refuses integers outside int64/uint64, but nlohmann has already parsed those into doubles
// that canonical can't tell from floats; core/tools/params_hash.cpp catches them while
// parsing.
std::string canonical(const nlohmann::json& value);

// FNV-1a 64 over the canonical text, as 16 lowercase hex digits; empty when canonical is.
// Identifies a parameter set in logs; not a security measure.
std::string content_hash(const nlohmann::json& value);
std::string fnv1a64_hex(std::string_view bytes);

}  // namespace ofs::params
