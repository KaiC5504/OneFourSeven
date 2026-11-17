// sim_cli tournament: two levels of the computer opponent play frames against each other under the
// full rules, and what each did is counted against spec 10.1 (spec 16.4). docs/opponent/README.md
// says what is counted and how.

#include <cstdio>

#include "sim_cli.hpp"

namespace sim_cli {

int cmd_tournament(const std::vector<std::string>&) {
    std::fputs("tournament: not built yet\n", stderr);
    return kExitUsage;
}

}  // namespace sim_cli
