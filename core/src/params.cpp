#include "ofs/params.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace ofs::params {

namespace {

// Python can't hold invalid UTF-8 in a str, so there is no matching hash to produce.
bool valid_utf8(const std::string& s) {
    std::size_t i = 0;
    const std::size_t n = s.size();
    const auto byte = [&s](std::size_t at) { return static_cast<unsigned char>(s[at]); };
    const auto cont = [&](std::size_t at) { return at < n && (byte(at) & 0xC0u) == 0x80u; };
    while (i < n) {
        const unsigned char c = byte(i);
        if (c < 0x80u) {
            i += 1;
        } else if (c >= 0xC2u && c <= 0xDFu) {
            if (!cont(i + 1)) return false;
            i += 2;
        } else if (c >= 0xE0u && c <= 0xEFu) {
            if (!cont(i + 1) || !cont(i + 2)) return false;
            const unsigned char c1 = byte(i + 1);
            if ((c == 0xE0u && c1 < 0xA0u) || (c == 0xEDu && c1 > 0x9Fu)) return false;
            i += 3;
        } else if (c >= 0xF0u && c <= 0xF4u) {
            if (!cont(i + 1) || !cont(i + 2) || !cont(i + 3)) return false;
            const unsigned char c1 = byte(i + 1);
            if ((c == 0xF0u && c1 < 0x90u) || (c == 0xF4u && c1 > 0x8Fu)) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

// The escapes Python's json.dumps(ensure_ascii=False) uses; everything else is raw.
bool write_string(std::string& out, const std::string& s) {
    if (!valid_utf8(s)) return false;
    out += '"';
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20u) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(c));
                    out += escaped;
                } else {
                    out += ch;
                }
        }
    }
    out += '"';
    return true;
}

bool write_float(std::string& out, double value) {
    if (!std::isfinite(value)) return false;
    char text[40];
    const int length = std::snprintf(text, sizeof text, "%.17g", value);
    if (length <= 0 || length >= static_cast<int>(sizeof text)) return false;
    // printf honours LC_NUMERIC, which a host app may have set to a comma locale. Anything
    // that isn't a digit, sign or exponent is the decimal point.
    const std::size_t start = out.size();
    for (int i = 0; i < length; ++i) {
        const char c = text[i];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == 'e') {
            out += c;
        } else if (out.size() == start || out.back() != '.') {
            out += '.';
        }
    }
    if (out.find_first_of(".e", start) == std::string::npos) {
        out += ".0";
    }
    return true;
}

bool write_value(std::string& out, const nlohmann::json& value) {
    using Type = nlohmann::json::value_t;
    switch (value.type()) {
        case Type::null: out += "null"; return true;
        case Type::boolean: out += value.get<bool>() ? "true" : "false"; return true;
        case Type::number_integer: out += std::to_string(value.get<std::int64_t>()); return true;
        case Type::number_unsigned: out += std::to_string(value.get<std::uint64_t>()); return true;
        case Type::number_float: return write_float(out, value.get<double>());
        case Type::string: return write_string(out, value.get_ref<const std::string&>());
        case Type::array: {
            out += '[';
            bool first = true;
            for (const auto& element : value) {
                if (!first) out += ',';
                first = false;
                if (!write_value(out, element)) return false;
            }
            out += ']';
            return true;
        }
        case Type::object: {
            // nlohmann::json keeps keys in a std::map: byte order, which for UTF-8 is the
            // code point order Python's sort_keys uses.
            out += '{';
            bool first = true;
            for (const auto& [key, element] : value.items()) {
                if (!first) out += ',';
                first = false;
                if (!write_string(out, key)) return false;
                out += ':';
                if (!write_value(out, element)) return false;
            }
            out += '}';
            return true;
        }
        case Type::binary:
        case Type::discarded: return false;
    }
    return false;
}

}  // namespace

std::string canonical(const nlohmann::json& value) {
    std::string out;
    if (!write_value(out, value)) return {};
    return out;
}

std::string content_hash(const nlohmann::json& value) {
    const std::string text = canonical(value);
    if (text.empty()) return {};
    return fnv1a64_hex(text);
}

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
