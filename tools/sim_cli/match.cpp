// Contract stub (M3): replaced by the tools builder.

#include <cstdio>

#include "sim_cli.hpp"

namespace sim_cli {

int cmd_match(const std::vector<std::string>&) {
    std::fputs("match: not implemented\n", stderr);
    return kExitUsage;
}

}  // namespace sim_cli
