#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "test_standin_support.hpp"

using namespace ofs;
using namespace ofs::opponent;
using namespace ofs::opponent::standin_test;

namespace {

struct Tally {
    int n = 0;
    int scored = 0;
    int fouls = 0;
    int knew = 0;  // fouls it expected when it chose the stroke
    double us = 0.0;
    double worst_us = 0.0;
};

const char* const kKinds[] = {"pot", "safety", "escape", "break"};

void print(const char* title, const Tally by_kind[4]) {
    std::printf("%s\n", title);
    for (int k = 0; k < 4; ++k) {
        const Tally& t = by_kind[k];
        if (t.n == 0) continue;
        std::printf("  %-7s %4d strokes, %4d scored, %4d fouls (%d expected), choose_shot mean %5.0f us, "
                    "slowest %5.0f us\n",
                    kKinds[k], t.n, t.scored, t.fouls, t.knew, t.us / t.n, t.worst_us);
    }
}

// The stand-in's stroke from `s`, played and added to its kind's tally.
rules::scenario::Played tally(const rules::FrameState& s, const StandinParams& sp, std::uint64_t seed,
                              Tally by_kind[4]) {
    Rng rng(seed);
    const auto t0 = std::chrono::steady_clock::now();
    const StandinShot shot = choose(s, sp, rng);
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    rules::FrameState placed;
    const std::string why_not = refusal(s, shot, placed);
    if (!why_not.empty()) std::printf("  refused: %s\n", why_not.c_str());
    rules::scenario::Played played = play(placed, shot, seed);
    const std::string kind = shot.why.value("kind", "?");
    for (int k = 0; k < 4; ++k) {
        if (kind != kKinds[k]) continue;
        Tally& t = by_kind[k];
        ++t.n;
        t.scored += played.judgement.points > 0 ? 1 : 0;
        t.fouls += played.judgement.fouls.empty() ? 0 : 1;
        t.knew += shot.why.value("penalty", 0) != 0 ? 1 : 0;
        t.us += us;
        t.worst_us = std::max(t.worst_us, us);
    }
    return played;
}

}  // namespace

// Run with -ns -tc="stand-in report": what the stand-in does with each kind of position, for
// tuning standin.cpp's constants and standin_params.json.
TEST_CASE("stand-in report" * doctest::skip()) {
    const rules::scenario::Rig& r = rig();

    Tally exact_breaks[4];
    Tally breaks[4];
    double rest_x = 0.0;
    double spread = 0.0;
    int in_baulk = 0;
    const int racks = 300;
    for (int i = 0; i < racks; ++i) {
        const std::uint64_t seed = 1 + std::uint64_t(i);
        const rules::FrameState s = rules::new_frame(0, phys::opening_rack(seed, r.params), seed);
        const auto played = tally(s, exact(), seed, exact_breaks);
        const Vec2 cue = played.facts.rest[std::size_t(s.cue_ball)];
        rest_x += cue.x;
        spread += played.facts.object_travel_m;
        in_baulk += cue.x < table::baulk_line_x() + 0.3 ? 1 : 0;
        tally(s, standin_params(), seed, breaks);
    }
    print("break-offs, played as aimed", exact_breaks);
    std::printf("  the cue ball rests at x %.2f m on average, %d of %d within 0.3 m of baulk; the reds move %.2f m\n",
                rest_x / racks, in_baulk, racks, spread / racks);
    print("break-offs, with its error", breaks);

    Tally open[4];
    Tally colours[4];
    Tally hidden[4];
    Tally hand[4];
    Rng layouts(147);
    for (int i = 0; i < 400; ++i) {
        const std::uint64_t seed = 1000 + std::uint64_t(i);
        tally(scattered(layouts, 1 + i % 15), standin_params(), seed, open);
        tally(colours_only(layouts, kColours[i % 6]), standin_params(), seed, colours);
        tally(snookered(layouts), standin_params(), seed, hidden);
        tally(in_hand(scattered(layouts, 1 + i % 15)), standin_params(), seed, hand);
    }
    print("open tables", open);
    print("the colours", colours);
    print("snookered on the last red", hidden);
    print("in hand", hand);
}
