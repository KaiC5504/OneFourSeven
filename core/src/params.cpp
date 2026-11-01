#include "ofs/params.hpp"

#include <cstdint>
#include <cstdio>

namespace ofs::params {

std::string canonical(const nlohmann::json& value) { return value.dump(); }

std::string content_hash(const nlohmann::json& value) { return fnv1a64_hex(canonical(value)); }

std::string fnv1a64_hex(std::string_view bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (const char c : bytes) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ull;
    }
    char out[17];
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(hash));
    return out;
}

}  // namespace ofs::params
