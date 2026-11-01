// Reads JSONL on stdin and prints ofs::params::content_hash of each line's JSON value, one
// per line: "error" when the line isn't JSON or the value has no canonical form. One
// process for the whole stream, so tools/test_devlink.py can check thousands of values.

#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "ofs/params.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

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
        const auto value = nlohmann::json::parse(line, nullptr, false);
        const std::string hash = value.is_discarded() ? std::string() : ofs::params::content_hash(value);
        const std::string out = (hash.empty() ? std::string("error") : hash) + "\n";
        std::fwrite(out.data(), 1, out.size(), stdout);
        // Lets a caller that writes a line and waits for its answer work too.
        std::fflush(stdout);
    }
    return 0;
}
