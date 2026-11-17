// The budget at its edges (README "The search", Budget): the clock alone, a cancel from another
// thread while the search runs, and decisions stopped on simulations made again from their steps.

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/physics/cue.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::FrameState;

namespace {

std::vector<const Level*> both() { return {&rig::levels().beginner, &rig::levels().maximum}; }

Stroke choose(const FrameState& s, const Level& level, const Think& think, std::uint64_t seed = 9) {
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
           a.places_cue == b.places_cue && a.cue_pos.x == b.cue_pos.x && a.cue_pos.y == b.cue_pos.y && a.why == b.why;
}

void check_accepted(const FrameState& s, const Stroke& st) {
    const phys::PhysicsParams& params = rules::scenario::rig().params;
    REQUIRE_FALSE(st.decides);
    CHECK(st.places_cue == s.cue_in_hand);
    FrameState from = s;
    std::string error;
    if (st.places_cue) REQUIRE(rules::place_cue_ball(from, st.cue_pos, rig::env().oracle, error));
    if (s.on.kind == rules::OnKind::any_colour) CHECK(st.input.has_nomination);
    CHECK(st.strike.speed_m_s >= kLeastSpeed_m_s);
    CHECK(st.strike.speed_m_s <= params.max_cue_speed_m_s);
    CHECK(std::hypot(st.strike.tip_x, st.strike.tip_y) <= kTipLimit * params.miscue_limit + 1e-12);
    const double least = phys::min_cue_elevation(rules::scenario::table_state(from), from.cue_ball, st.strike.aim_rad,
                                                 rules::scenario::rig().table, params);
    CHECK(st.strike.elevation_rad >= least - 1e-12);
    CHECK(rules::scenario::play(from, st.strike, st.input).judgement.post.strokes == s.strokes + 1);
}

// One candidate is one attempt: up to two rounds of solve_pot (kSolveSims) and a judged stroke.
constexpr int kMostSimsOfAStep = 14;

}  // namespace

TEST_CASE("a clock that has run out stops the search on time after its first step, and the steps make it again") {
    for (const Level* level : both()) {
        for (const char* name : {"after_red", "straight_red"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            const FrameState s = rig::position(name);
            Think t;
            t.max_ms = 1.0e-9;
            const Stroke cut = choose(s, *level, t);
            check_accepted(s, cut);
            CHECK(cut.steps == 1);
            CHECK(cut.stopped == "time");
            CHECK(cut.fallback.empty());
            Think again;
            again.max_steps = cut.steps;
            CHECK(same(cut, choose(s, *level, again)));
        }
    }
}

TEST_CASE("a cancel set from another thread mid-search stops the 147 at its next step with a legal stroke") {
    const FrameState s = exhibition_frame(rig::levels().exhibitions.front(), 0, 1);
    const Level& level = rig::levels().maximum;
    std::atomic<bool> cancel{false};
    Think t;
    t.cancel = &cancel;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        cancel.store(true);
    });
    const Stroke st = choose(s, level, t);
    stopper.join();
    CAPTURE(st.steps);
    CHECK(st.stopped == "cancelled");
    CHECK(st.steps > 0);
    check_accepted(s, st);
    if (st.why.value("kind", std::string()) == "pot") {
        // The 147 plays only what it planned, cut short or not.
        CHECK(st.strike.aim_rad == st.meant.aim_rad);
        CHECK(st.fallback.empty());
        const rules::Judgement j = rules::scenario::play(s, st.strike, st.input).judgement;
        CHECK(j.fouls.empty());
        CHECK(j.points == 1);
    }
}

TEST_CASE("a cancel set from another thread mid-search stops Beginner with a legal stroke") {
    const FrameState s = exhibition_frame(rig::levels().exhibitions.front(), 0, 1);
    const Level& level = rig::levels().beginner;
    std::atomic<bool> cancel{false};
    Think t;
    t.cancel = &cancel;
    t.max_sims = 1000000;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        cancel.store(true);
    });
    const Stroke st = choose(s, level, t);
    stopper.join();
    CAPTURE(st.steps);
    CAPTURE(st.stopped);
    check_accepted(s, st);
    CHECK((st.stopped == "cancelled" || st.stopped == "done"));
}

TEST_CASE("Beginner stopped on simulations is made again from its steps alone, for twenty seeds") {
    const Level& level = rig::levels().beginner;
    int stopped_on_sims = 0;
    for (const char* name : {"straight_red", "cut_red", "after_red", "in_hand", "last_red"}) {
        const FrameState s = rig::position(name);
        for (std::uint64_t seed = 1; seed <= 20; ++seed) {
            for (const int most : {1, 6}) {
                CAPTURE(name);
                CAPTURE(seed);
                CAPTURE(most);
                Think small;
                small.max_sims = most;
                const Stroke first = choose(s, level, small, seed);
                if (first.stopped == "sims") ++stopped_on_sims;
                Think steps;
                steps.max_steps = first.steps;
                const Stroke again = choose(s, level, steps, seed);
                CHECK(same(first, again));
                CHECK(again.steps == first.steps);
                CHECK(again.sims == first.sims);
            }
        }
    }
    CHECK(stopped_on_sims >= 80);
}

TEST_CASE("the 147 stopped on simulations is made again from its steps alone, whatever the seed") {
    const Level& level = rig::levels().maximum;
    const FrameState exhibition = exhibition_frame(rig::levels().exhibitions.front(), 0, 1);
    for (const FrameState& s : {rig::position("after_red"), rig::position("in_hand"), exhibition}) {
        Think small;
        small.max_sims = 250;
        const Stroke first = choose(s, level, small, 1);
        CAPTURE(first.steps);
        CHECK(first.stopped == "sims");
        Think steps;
        steps.max_steps = first.steps;
        for (const std::uint64_t seed : {1u, 17u, 99u}) {
            CAPTURE(seed);
            const Stroke again = choose(s, level, steps, seed);
            CHECK(same(first, again));
            CHECK(again.steps == first.steps);
        }
    }
}

TEST_CASE("a pot chosen within max_sims passes it by no more than the step in hand") {
    for (const Level* level : both()) {
        for (const char* name : {"straight_red", "after_red", "in_hand"}) {
            const FrameState s = rig::position(name);
            for (const int most : {1, 2, 7, 30, 90}) {
                CAPTURE(level->id);
                CAPTURE(name);
                CAPTURE(most);
                Think t;
                t.max_sims = most;
                const Stroke st = choose(s, *level, t);
                if (st.why.value("kind", std::string()) != "pot") continue;
                CHECK(st.sims <= most - 1 + kMostSimsOfAStep);
            }
        }
    }
}
