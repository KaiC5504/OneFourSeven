// Contract stub (M3): replaced by the tools builder.

#include <cstdio>

#include "sim_cli.hpp"

namespace sim_cli {

int cmd_referee(const std::vector<std::string>&) {
    std::fputs("referee: not implemented\n", stderr);
    return kExitUsage;
}

}  // namespace sim_cli
