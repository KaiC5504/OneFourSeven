#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
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

// The 147's whole budget is seconds in a Debug build.
Think base(const Level& level) {
    Think t;
    if (level.maximum) t.max_sims = 1500;
    return t;
}

Stroke choose(const FrameState& s, const Level& level, const Think& think) {
    Rng rng(9);
    return choose_stroke(s, level, rig::env(), rng, think);
}

bool same(const Stroke& a, const Stroke& b) {
    const auto strike = [](const phys::CueStrike& x, const phys::CueStrike& y) {
        return x.aim_rad == y.aim_rad && x.speed_m_s == y.speed_m_s && x.tip_x == y.tip_x && x.tip_y == y.tip_y &&
               x.elevation_rad == y.elevation_rad;
    };
    return a.decides == b.decides && strike(a.strike, b.strike) && strike(a.meant, b.meant) &&
           a.input.has_nomination == b.input.has_nomination && a.input.nominated == b.input.nominated &&
           a.cue_pos.x == b.cue_pos.x && a.cue_pos.y == b.cue_pos.y && a.why == b.why;
}

// The stroke is one the engine takes: placed, declared, within the cue's limits, and judged.
void check_accepted(const FrameState& s, const Stroke& st) {
    const phys::PhysicsParams& params = rules::scenario::rig().params;
    REQUIRE_FALSE(st.decides);
    CHECK(st.places_cue == s.cue_in_hand);
    FrameState from = s;
    std::string error;
    if (st.places_cue) REQUIRE(rules::place_cue_ball(from, st.cue_pos, rig::env().oracle, error));
    if (s.on.kind == rules::OnKind::any_colour) {
        CHECK(st.input.has_nomination);
        const std::vector<rules::Ball> open = rules::nominations(s);
        CHECK(std::find(open.begin(), open.end(), st.input.nominated) != open.end());
    }
    CHECK(st.strike.speed_m_s > 0.0);
    CHECK(st.strike.speed_m_s <= params.max_cue_speed_m_s);
    CHECK(std::hypot(st.strike.tip_x, st.strike.tip_y) <= kTipLimit * params.miscue_limit + 1e-12);
    const double least = phys::min_cue_elevation(rules::scenario::table_state(from), from.cue_ball, st.strike.aim_rad,
                                                 rules::scenario::rig().table, params);
    CHECK(st.strike.elevation_rad >= least - 1e-12);
    CHECK(st.strike.elevation_rad <= params.max_elevation_rad);
    CHECK(rules::scenario::play(from, st.strike, st.input).judgement.post.strokes == s.strokes + 1);
    CHECK(st.why.contains("level"));
}

}  // namespace

TEST_CASE("one simulation, one step or a cancel already set still give a stroke, and say what stopped it") {
    const std::atomic<bool> cancel{true};
    for (const Level* level : both()) {
        for (const char* name : {"after_red", "straight_red", "in_hand", "snookered"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            const FrameState s = rig::position(name);
            const Stroke whole = choose(s, *level, base(*level));

            Think one_sim;
            one_sim.max_sims = 1;
            const Stroke by_sims = choose(s, *level, one_sim);
            check_accepted(s, by_sims);
            CHECK(by_sims.steps <= 1);
            if (whole.steps > 1) CHECK(by_sims.stopped == "sims");

            Think one_step = base(*level);
            one_step.max_steps = 1;
            const Stroke by_steps = choose(s, *level, one_step);
            check_accepted(s, by_steps);
            CHECK(by_steps.steps <= 1);
            if (whole.steps > 1) CHECK(by_steps.stopped == "steps");

            Think stopped = base(*level);
            stopped.cancel = &cancel;
            const Stroke cancelled = choose(s, *level, stopped);
            check_accepted(s, cancelled);
            CHECK(cancelled.stopped == "cancelled");
            CHECK(cancelled.steps == 0);
            CHECK_FALSE(cancelled.fallback.empty());
            // The 147 never plays a pot it did not plan.
            if (level->maximum) CHECK(cancelled.why.value("kind", std::string()) != "pot");
        }
    }
}

TEST_CASE("the first step is always taken, so a stopped decision is made again from its steps") {
    // A decision logged as stopped is re-run with max_steps = its steps, and 0 would be no limit.
    Think one_sim;
    one_sim.max_sims = 1;
    one_sim.max_ms = 1.0e-9;
    for (const Level* level : both()) {
        CAPTURE(level->id);
        const FrameState s = rig::position("after_red");
        const Stroke cut = choose(s, *level, one_sim);
        CHECK(cut.steps == 1);
        CHECK(cut.stopped == "sims");
        Think again;
        again.max_steps = cut.steps;
        CHECK(same(cut, choose(s, *level, again)));
    }
}

TEST_CASE("max_steps = k takes k steps, and the same call gives the same stroke") {
    for (const Level* level : both()) {
        for (const char* name : {"after_red", "straight_red"}) {
            const FrameState s = rig::position(name);
            const Stroke whole = choose(s, *level, base(*level));
            for (const int k : {1, 2, 5, 20}) {
                CAPTURE(level->id);
                CAPTURE(name);
                CAPTURE(k);
                Think t = base(*level);
                t.max_steps = k;
                const Stroke a = choose(s, *level, t);
                const Stroke b = choose(s, *level, t);
                if (whole.steps > k) {
                    CHECK(a.steps == k);
                    CHECK(a.stopped == "steps");
                } else {
                    CHECK(a.steps == whole.steps);
                    CHECK(a.stopped == whole.stopped);
                }
                CHECK(same(a, b));
                CHECK(a.sims == b.sims);
                check_accepted(s, a);
            }
        }
    }
}

TEST_CASE("a run's steps given back as max_steps make the same stroke again") {
    for (const Level* level : both()) {
        for (const char* name : {"after_red", "last_red", "colours"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            const FrameState s = rig::position(name);
            const Stroke whole = choose(s, *level, base(*level));
            REQUIRE(whole.steps > 0);
            Think t = base(*level);
            t.max_steps = whole.steps;
            const Stroke again = choose(s, *level, t);
            CHECK(same(whole, again));
            CHECK(again.steps == whole.steps);

            // As ofs_sim_cli opponent makes it again: the steps alone, the level's own simulations.
            Think steps_only;
            steps_only.max_steps = whole.steps;
            if (whole.stopped != "done") CHECK(same(whole, choose(s, *level, steps_only)));
        }
    }
}

TEST_CASE("a clock with time to spare and a cancel flag left alone change nothing") {
    const std::atomic<bool> unset{false};
    const FrameState s = rig::position("after_red");
    for (const Level* level : both()) {
        CAPTURE(level->id);
        const Stroke plain = choose(s, *level, base(*level));
        Think t = base(*level);
        t.cancel = &unset;
        t.max_ms = 1.0e9;
        const Stroke watched = choose(s, *level, t);
        CHECK(same(plain, watched));
        CHECK(watched.steps == plain.steps);
        CHECK((watched.stopped == "done" || watched.stopped == "sims"));
    }
}

TEST_CASE("the simulations counted include the stand-in's, and max_sims 0 is the level's own") {
    const Level& beginner = rig::levels().beginner;
    const FrameState snookered = rig::position("snookered");
    const Stroke out = choose(snookered, beginner, Think{});
    CHECK(out.sims > 0);
    CHECK(out.steps == 0);
    CHECK(out.stopped == "done");

    const FrameState s = rig::position("straight_red");
    Think own;
    own.max_sims = beginner.max_sims;
    CHECK(same(choose(s, beginner, Think{}), choose(s, beginner, own)));
}
