// sim_cli opponent: the opponent's logged decisions made again (spec 16.10). An opponent.decision
// line carries its level, seed and the steps it took; the frame it was made from is the "pre" of
// the rules.decision line with the same shot_id.

#include <cstdio>

#include "sim_cli.hpp"

namespace sim_cli {

int cmd_opponent(const std::vector<std::string>&) {
    std::fputs("opponent: not built yet\n", stderr);
    return kExitUsage;
}

}  // namespace sim_cli
