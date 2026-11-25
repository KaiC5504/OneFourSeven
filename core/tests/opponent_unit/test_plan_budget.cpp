// The four new levels against the budget (README "Budget", "The order"): every decision made again
// from the steps it took, and each way a decision stops.

#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/opponent.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::FrameState;

namespace {

std::vector<const Level*> new_levels() {
    return {&rig::levels().club, &rig::levels().amateur, &rig::levels().pro, &rig::levels().world};
}

Stroke choose(const FrameState& s, const Level& level, const Think& think, std::uint64_t seed) {
    Rng rng(seed);
    return choose_stroke(s, level, rig::env(), rng, think);
}

bool same(const Stroke& a, const Stroke& b) {
    const auto strike = [](const phys::CueStrike& x, const phys::CueStrike& y) {
        return x.aim_rad == y.aim_rad && x.speed_m_s == y.speed_m_s && x.tip_x == y.tip_x && x.tip_y == y.tip_y &&
               x.elevation_rad == y.elevation_rad;
    };
    return a.decides == b.decides && strike(a.strike, b.strike) && strike(a.meant, b.meant) &&
           a.input.has_nomination == b.input.has_nomination && a.input.nominated == b.input.nominated &&
           a.places_cue == b.places_cue && a.cue_pos.x == b.cue_pos.x && a.cue_pos.y == b.cue_pos.y && a.why == b.why &&
           a.steps == b.steps && a.sims == b.sims && a.depth == b.depth;
}

std::vector<FrameState> positions() {
    return {rig::position("after_red"), rig::position("in_hand"), rig::position("pl_black_or_brown"),
            exhibition_frame(rig::levels().exhibitions.front(), 0, 1)};
}

}  // namespace

TEST_CASE("each new level stopped on simulations is made again from its steps alone") {
    for (const Level* level : new_levels()) {
        for (const FrameState& s : positions()) {
            for (const std::uint64_t seed : {3u, 11u}) {
                CAPTURE(level->id);
                CAPTURE(seed);
                Think small;
                small.max_sims = 120;
                const Stroke first = choose(s, *level, small, seed);
                CHECK(first.fallback.empty());
                Think steps;
                steps.max_steps = first.steps;
                CHECK(same(first, choose(s, *level, steps, seed)));
            }
        }
    }
}

TEST_CASE("a level that plans, its stages cut short or run whole, is made again from its steps") {
    for (const Level* from : {&rig::levels().amateur, &rig::levels().world}) {
        Level level = *from;
        // Few enough simulations to finish a stage or two in a debug build.
        level.max_sims = 500;
        for (const FrameState& s : positions()) {
            CAPTURE(level.id);
            const Stroke first = choose(s, level, {}, 5);
            Think steps;
            steps.max_steps = first.steps;
            CHECK(same(first, choose(s, level, steps, 5)));
            if (first.why.value("kind", std::string()) == "pot") {
                CHECK(first.depth == int(first.why["plan"].size()));
                CHECK(first.depth <= level.depth);
            }
        }
    }
}

TEST_CASE("the ways a new level's decision stops") {
    const FrameState s = rig::position("straight_red");
    for (const Level* level : new_levels()) {
        CAPTURE(level->id);
        Think whole;
        whole.max_sims = 1000000;
        Level open = *level;
        open.max_sims = 1000000;
        CHECK(choose(s, open, whole, 1).stopped == "done");

        Think sims;
        sims.max_sims = 3;
        CHECK(choose(s, *level, sims, 1).stopped == "sims");

        Think steps;
        steps.max_steps = 2;
        const Stroke two = choose(s, *level, steps, 1);
        CHECK(two.stopped == "steps");
        CHECK(two.steps == 2);

        Think clock;
        clock.max_ms = 1e-9;
        const Stroke one = choose(s, *level, clock, 1);
        CHECK(one.stopped == "time");
        CHECK(one.steps == 1);
        CHECK(one.fallback.empty());

        std::atomic<bool> cancel{true};
        Think stop;
        stop.cancel = &cancel;
        const Stroke none = choose(s, *level, stop, 1);
        CHECK(none.stopped == "cancelled");
        CHECK(none.steps == 0);
        CHECK(none.fallback == "cancelled before the first step");
        CHECK_FALSE(none.decides);
    }
}
