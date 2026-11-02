// Stub from the M1 contracts commit; the tools agent replaces it. The gate's physics lane already
// calls `fuzz` and `bench`, so they succeed until then.

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc >= 2 && (std::strcmp(argv[1], "fuzz") == 0 || std::strcmp(argv[1], "bench") == 0)) {
        std::printf("%s: stub (M1 contracts)\n", argv[1]);
        return 0;
    }
    std::fprintf(stderr, "usage: ofs_sim_cli simulate|replay|fuzz|bench ...\n");
    return 2;
}
