#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <string>

#include "ofs/params.hpp"

using ofs::params::canonical;
using ofs::params::content_hash;
using ofs::params::fnv1a64_hex;

namespace {

std::string from_hex(const std::string& hex) {
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

}  // namespace

TEST_CASE("fnv1a64_hex matches the protocol vectors") {
    CHECK(fnv1a64_hex("") == "cbf29ce484222325");
    CHECK(fnv1a64_hex("a") == "af63dc4c8601ec8c");
}

TEST_CASE("canonical and content_hash match the protocol vectors") {
    const auto a = nlohmann::json::parse(R"({"b":1,"a":[true,null,"x"]})");
    CHECK(canonical(a) == R"({"a":[true,null,"x"],"b":1})");
    CHECK(content_hash(a) == "8efc364b3052045e");

    const auto b = nlohmann::json::parse(R"({"values":{},"schema_version":1})");
    CHECK(canonical(b) == R"({"schema_version":1,"values":{}})");
    CHECK(content_hash(b) == "b99af9e0a04c6e42");
}

TEST_CASE("canonical numbers match Python json.dumps") {
    // Expected strings are Python 3.13's json.dumps output for the same values.
    const struct {
        double value;
        const char* python;
    } floats[] = {
        {0.1, "0.1"},
        {1e-7, "1e-07"},
        {2.5, "2.5"},
        {1.0, "1.0"},
        {0.0, "0.0"},
        {-0.0, "-0.0"},
        {999999999999999.9, "999999999999999.9"},
        {1e16, "1e+16"},
        {123456789012345680.0, "1.2345678901234568e+17"},
        {1e21, "1e+21"},
        {1e22, "1e+22"},
        {0.0001, "0.0001"},
        {0.00001, "1e-05"},
        {5e-324, "5e-324"},
        {1.7976931348623157e308, "1.7976931348623157e+308"},
        {1.0 / 3.0, "0.3333333333333333"},
        {100.0, "100.0"},
        {12345.678, "12345.678"},
        {-1.5e-10, "-1.5e-10"},
        {0.1 + 0.2, "0.30000000000000004"},
        {1e100, "1e+100"},
    };
    for (const auto& f : floats) {
        const std::string python = f.python;
        CAPTURE(python);
        CHECK(canonical(nlohmann::json(f.value)) == f.python);
        CHECK(canonical(nlohmann::json::parse(f.python)) == f.python);
    }

    CHECK(canonical(nlohmann::json(3)) == "3");
    CHECK(canonical(nlohmann::json(0)) == "0");
    CHECK(canonical(nlohmann::json(-7)) == "-7");
    CHECK(canonical(nlohmann::json(std::numeric_limits<std::int64_t>::max())) == "9223372036854775807");
    CHECK(canonical(nlohmann::json(std::numeric_limits<std::int64_t>::min())) == "-9223372036854775808");
    CHECK(canonical(nlohmann::json(std::numeric_limits<std::uint64_t>::max())) == "18446744073709551615");
}

TEST_CASE("canonical floats in [1e15, 1e16) use an exponent, unlike Python") {
    // Python prints these positionally ("1000000000000000.0"), so tools/devlink.py has to
    // format this band the way nlohmann does for the hashes to agree.
    CHECK(canonical(nlohmann::json(1e15)) == "1e+15");
    CHECK(canonical(nlohmann::json(-1e15)) == "-1e+15");
    CHECK(canonical(nlohmann::json(1234567890123456.0)) == "1.234567890123456e+15");
    CHECK(canonical(nlohmann::json(9999999999999998.0)) == "9.999999999999998e+15");
}

TEST_CASE("canonical sorts nested keys and keeps UTF-8 unescaped") {
    const auto value = nlohmann::json::parse(
        R"({"é":"é中🎱\u0001\u007f /\\\"\n\t","a":{"z":[1,2.0,null,true]},"B":null})");
    // Python: json.dumps(..., sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    // encoded as UTF-8.
    const std::string python = from_hex(
        "7b2242223a6e756c6c2c2261223a7b227a223a5b312c322e302c6e756c6c2c747275655d7d2c22c3a9223a22"
        "c3a9e4b8adf09f8eb15c75303030317fe280a82f5c5c5c225c6e5c74227d");
    CHECK(canonical(value) == python);

    const auto nested = nlohmann::json::parse(R"({"z":{"y":{"b":2,"a":1}},"m":[{"d":0,"c":-1.5}]})");
    CHECK(canonical(nested) == R"({"m":[{"c":-1.5,"d":0}],"z":{"y":{"a":1,"b":2}}})");
}

TEST_CASE("content_hash ignores key order and whitespace") {
    const auto a = nlohmann::json::parse(R"({ "x": 1.5, "y": [1, 2] })");
    const auto b = nlohmann::json::parse(R"({"y":[1,2],"x":1.5})");
    CHECK(content_hash(a) == content_hash(b));
    CHECK(content_hash(a).size() == 16);
}
