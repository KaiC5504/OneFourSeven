// ofs_sim_cli: the physics on the command line (spec 16.1, 16.10, 21 M1). tools/sim_cli/README.md
// describes every command.

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "sim_cli.hpp"

namespace {

const char* const kUsage =
    "usage: ofs_sim_cli <command> [options]\n"
    "\n"
    "  simulate <position.json | rack:SEED> [--strike JSON] [--no-strike] [--seed N]\n"
    "           [--params P] [--table T] [--out record.json] [--log out.jsonl]\n"
    "  replay <log.jsonl | bundle.json> [--line N] [--tolerance M] [--table T] [--force-table]\n"
    "         [--update]\n"
    "  fuzz [--shots N] [--seed S] [--threads T] [--out DIR] [--hang-seconds S] [--params P] [--table T]\n"
    "       [--inject-anomaly KIND]\n"
    "  bench [--check] [--baseline FILE] [--write-baseline] [--runs N] [--budget-us U]\n"
    "        [--params P] [--table T] [--force-table]\n"
    "  calibrate --out DIR [--breaks N] [--params P] [--table T]\n"
    "\n"
    "Exit codes: 0 ok, 1 a check failed, 2 bad arguments or input.\n";

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "help") {
        std::fputs(kUsage, argc < 2 ? stderr : stdout);
        return argc < 2 ? sim_cli::kExitUsage : sim_cli::kExitOk;
    }
    const std::string command = argv[1];
    const std::vector<std::string> args(argv + 2, argv + argc);
    try {
        int code = -1;
        if (command == "simulate") code = sim_cli::cmd_simulate(args);
        if (command == "replay") code = sim_cli::cmd_replay(args);
        if (command == "fuzz") code = sim_cli::cmd_fuzz(args);
        if (command == "bench") code = sim_cli::cmd_bench(args);
        if (command == "calibrate") code = sim_cli::cmd_calibrate(args);
        if (code >= 0) {
            std::fflush(stdout);
            return code;
        }
    } catch (const std::exception& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "%s: unexpected error: %s\n", command.c_str(), e.what());
        return sim_cli::kExitUsage;
    }
    std::fprintf(stderr, "unknown command '%s'\n\n%s", command.c_str(), kUsage);
    return sim_cli::kExitUsage;
}
