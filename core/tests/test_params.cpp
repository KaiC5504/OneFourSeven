#include <doctest/doctest.h>

#include <clocale>
#include <cstdint>
#include <cstdio>
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

TEST_CASE("canonical floats follow the protocol's %.17g rule") {
    // Expected strings are Python's '%.17g' % x, with ".0" added when that has no '.' or 'e'.
    const struct {
        double value;
        const char* text;
    } floats[] = {
        {0.1, "0.10000000000000001"},
        {369.0019722661402, "369.00197226614023"},
        {1e15, "1000000000000000.0"},
        {-1e15, "-1000000000000000.0"},
        {1234567890123456.0, "1234567890123456.0"},
        {9999999999999998.0, "9999999999999998.0"},
        {1e16, "10000000000000000.0"},
        {1e17, "1e+17"},
        {123456789012345680.0, "1.2345678901234568e+17"},
        {999999999999999.9, "999999999999999.88"},
        {1e-7, "9.9999999999999995e-08"},
        {1e-5, "1.0000000000000001e-05"},
        {0.0001, "0.0001"},
        {5e-324, "4.9406564584124654e-324"},
        {1e-320, "9.9998886718268301e-321"},
        {2.2250738585072014e-308, "2.2250738585072014e-308"},
        {1.7976931348623157e308, "1.7976931348623157e+308"},
        {-0.0, "-0.0"},
        {0.0, "0.0"},
        {1.0, "1.0"},
        {2.5, "2.5"},
        {100.0, "100.0"},
        {12345.678, "12345.678"},
        {-1.5e-10, "-1.5e-10"},
        {1.0 / 3.0, "0.33333333333333331"},
        {0.1 + 0.2, "0.30000000000000004"},
        {1e21, "1e+21"},
        {1e100, "1e+100"},
    };
    for (const auto& f : floats) {
        const std::string text = f.text;
        CAPTURE(text);
        CHECK(canonical(nlohmann::json(f.value)) == text);
        // %.17g round-trips, so the canonical text is a fixed point.
        CHECK(canonical(nlohmann::json::parse(text)) == text);
    }
}

TEST_CASE("canonical integers are decimal, whatever their size") {
    CHECK(canonical(nlohmann::json(3)) == "3");
    CHECK(canonical(nlohmann::json(0)) == "0");
    CHECK(canonical(nlohmann::json(-7)) == "-7");
    CHECK(canonical(nlohmann::json(std::numeric_limits<std::int64_t>::max())) == "9223372036854775807");
    CHECK(canonical(nlohmann::json(std::numeric_limits<std::int64_t>::min())) == "-9223372036854775808");
    CHECK(canonical(nlohmann::json(std::numeric_limits<std::uint64_t>::max())) == "18446744073709551615");
    CHECK(canonical(nlohmann::json::parse("1000000000000000000")) == "1000000000000000000");
    // Python's json.loads also reads these as int 0, float 100.0 and float 1.0.
    CHECK(canonical(nlohmann::json::parse("-0")) == "0");
    CHECK(canonical(nlohmann::json::parse("1e2")) == "100.0");
    CHECK(canonical(nlohmann::json::parse("1.0")) == "1.0");
}

TEST_CASE("a value with floats hashes as tools/devlink.py does") {
    const auto value = nlohmann::json::parse(R"({"x":[0.1,369.0019722661402,1e15,-0.0,5]})");
    CHECK(canonical(value) == R"({"x":[0.10000000000000001,369.00197226614023,1000000000000000.0,-0.0,5]})");
    CHECK(content_hash(value) == "c7c7a27577cad9d3");
}

TEST_CASE("canonical and content_hash refuse values with no canonical form") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const nlohmann::json refused[] = {
        nlohmann::json(nan),
        nlohmann::json(inf),
        nlohmann::json(-inf),
        nlohmann::json{{"a", {1, 2, {{"deep", inf}}}}},
        nlohmann::json::binary({1, 2, 3}),
    };
    for (const auto& value : refused) {
        CAPTURE(value.type_name());
        CHECK(canonical(value).empty());
        CHECK(content_hash(value).empty());
    }
}

TEST_CASE("canonical refuses invalid UTF-8 in strings and keys") {
    const char* invalid[] = {
        "a\xff" "b",         // 0xff never appears in UTF-8
        "\xc0\x80",          // overlong NUL
        "\xe0\x80\xaf",      // overlong '/'
        "\xed\xa0\x80",      // UTF-16 surrogate
        "\xf4\x90\x80\x80",  // past U+10FFFF
        "\xe4\xb8",          // truncated
        "\x80",              // stray continuation byte
    };
    for (const char* bytes : invalid) {
        CAPTURE(nlohmann::json(bytes).dump(-1, ' ', true, nlohmann::json::error_handler_t::replace));
        CHECK(canonical(nlohmann::json(bytes)).empty());
        CHECK(canonical(nlohmann::json{{bytes, 1}}).empty());
    }
    CHECK(canonical(nlohmann::json("\xf4\x8f\xbf\xbf")) == "\"\xf4\x8f\xbf\xbf\"");
    CHECK(canonical(nlohmann::json("\xee\x80\x80")) == "\"\xee\x80\x80\"");
}

TEST_CASE("canonical escapes control characters as Python does") {
    std::string all;
    for (int c = 0; c < 0x20; ++c) all.push_back(static_cast<char>(c));
    all += "\x7f\"\\/";
    // json.dumps(all, ensure_ascii=False): short escapes where JSON has them, lowercase
    // \u00xx for the rest, DEL and '/' raw.
    const std::string python =
        R"("\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f)"
        R"(\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c)"
        R"(\u001d\u001e\u001f)" "\x7f" R"(\"\\/")";
    CHECK(canonical(nlohmann::json(all)) == python);
}

TEST_CASE("canonical floats ignore a comma-decimal C locale") {
    const std::string saved = std::setlocale(LC_NUMERIC, nullptr);
    bool switched = false;
    for (const char* name : {"de-DE", "de_DE.UTF-8", "de_DE", "German"}) {
        if (std::setlocale(LC_NUMERIC, name) != nullptr) {
            switched = true;
            break;
        }
    }
    REQUIRE_MESSAGE(switched, "no German locale installed to test with");
    char probe[16];
    std::snprintf(probe, sizeof probe, "%.1f", 0.5);
    const std::string printed = probe;
    const std::string text = canonical(nlohmann::json::array({0.5, 0.1, 1e-7}));
    std::setlocale(LC_NUMERIC, saved.c_str());
    CHECK(printed == "0,5");
    CHECK(text == "[0.5,0.10000000000000001,9.9999999999999995e-08]");
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
