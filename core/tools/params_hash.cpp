// Reads JSONL on stdin and prints ofs::params::content_hash of each line's JSON value, one
// per line: "error" when the line isn't JSON or the protocol refuses the value. One
// process for the whole stream, so tools/test_devlink.py can check thousands of values.

#include <cstddef>
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "ofs/params.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

// nlohmann reads an integer outside int64/uint64 as the nearest double, and canonical can't
// tell that double from a float literal, so the token has to be caught while parsing. The
// lexer hands number_float the token text; only an integer token has no '.', 'e' or 'E'.
class RefusedIntegers final : public nlohmann::json_sax<nlohmann::json> {
public:
    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t& token) override {
        return token.find_first_of(".eE") != string_t::npos;
    }
    bool string(string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }
    bool start_object(std::size_t) override { return true; }
    bool key(string_t&) override { return true; }
    bool end_object() override { return true; }
    bool start_array(std::size_t) override { return true; }
    bool end_array() override { return true; }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override {
        return false;
    }
};

std::string hash_line(const std::string& line) {
    RefusedIntegers check;
    if (!nlohmann::json::sax_parse(line, &check)) return {};
    return ofs::params::content_hash(nlohmann::json::parse(line, nullptr, false));
}

bool read_line(std::string& line) {
    line.clear();
    int c = 0;
    while ((c = std::getc(stdin)) != EOF) {
        if (c == '\n') return true;
        line.push_back(static_cast<char>(c));
    }
    return !line.empty();
}

}  // namespace

int main() {
#ifdef _WIN32
    // Text mode would end the input at a Ctrl-Z byte and write CRLF line ends.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string line;
    while (read_line(line)) {
        const std::string hash = hash_line(line);
        const std::string out = (hash.empty() ? std::string("error") : hash) + "\n";
        std::fwrite(out.data(), 1, out.size(), stdout);
        // Lets a caller that writes a line and waits for its answer work too.
        std::fflush(stdout);
    }
    return 0;
}
