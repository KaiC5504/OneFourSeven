// The thinking budget (spec 10.6, opponent.hpp's Think, docs/opponent/README.md "The search"): the
// search stops where it is told, says why, never comes back empty, and a logged decision is made
// again from the steps it took.

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/opponent.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using nlohmann::json;
using rules::Ball;
using rules::FrameState;

namespace {

constexpr std::uint64_t kSeed = 7;

std::vector<const Level*> both() { return {&rig::levels().beginner, &rig::levels().maximum}; }

FrameState named(const std::string& name) { return name == "racked" ? rig::racked(1) : rig::position(name.c_str()); }

Stroke choose(const FrameState& s, const Level& level, const Think& think, std::uint64_t seed = kSeed) {
    Rng rng(seed);
    return choose_stroke(s, level, rig::env(), rng, think);
}

Think sims(int n) {
    Think t;
    t.max_sims = n;
    return t;
}

// Each level's stroke from after_red with its whole budget, worked out once: the 147's is tens of
// thousands of simulations.
const Stroke& full(const Level& level) {
    static std::map<std::string, Stroke> cache;
    auto it = cache.find(level.id);
    if (it == cache.end()) it = cache.emplace(level.id, choose(named("after_red"), level, Think{})).first;
    return it->second;
}

std::string text(const json& j, const char* key) {
    if (!j.is_object()) return "";
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

bool same_strike(const phys::CueStrike& a, const phys::CueStrike& b) {
    return a.aim_rad == b.aim_rad && a.speed_m_s == b.speed_m_s && a.tip_x == b.tip_x && a.tip_y == b.tip_y &&
           a.elevation_rad == b.elevation_rad;
}

bool same_stroke(const Stroke& a, const Stroke& b) {
    return a.decides == b.decides && same_strike(a.strike, b.strike) && same_strike(a.meant, b.meant) &&
           a.input.has_nomination == b.input.has_nomination && a.input.nominated == b.input.nominated &&
           a.places_cue == b.places_cue && a.cue_pos.x == b.cue_pos.x && a.cue_pos.y == b.cue_pos.y;
}

bool finite(const phys::CueStrike& c) {
    return std::isfinite(c.aim_rad) && std::isfinite(c.speed_m_s) && std::isfinite(c.tip_x) &&
           std::isfinite(c.tip_y) && std::isfinite(c.elevation_rad);
}

void check_accepted(const FrameState& s, const Stroke& st) {
    const phys::PhysicsParams& params = rules::scenario::rig().params;
    if (st.decides) {
        const std::vector<rules::DecisionKind> open = rules::options(s, s.striker);
        CHECK(std::find(open.begin(), open.end(), st.decision) != open.end());
        return;
    }
    CHECK(st.places_cue == s.cue_in_hand);
    if (st.places_cue) CHECK(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, st.cue_pos));
    if (s.on.kind == rules::OnKind::any_colour) CHECK(st.input.has_nomination);
    CHECK(finite(st.strike));
    CHECK(st.strike.speed_m_s > 0.0);
    CHECK(st.strike.speed_m_s <= params.max_cue_speed_m_s);
    CHECK(std::hypot(st.strike.tip_x, st.strike.tip_y) <= kTipLimit * params.miscue_limit + 1e-12);

    FrameState f = s;
    std::string error;
    if (st.places_cue) CHECK(rules::place_cue_ball(f, st.cue_pos, rig::env().oracle, error));
}

}  // namespace

TEST_CASE("spec 10.6, Think: one simulation allowed, it still answers, stopped by sims, having run far fewer") {
    const FrameState s = named("after_red");
    for (const Level* level : both()) {
        CAPTURE(level->id);
        const Stroke& whole = full(*level);
        const Stroke cut = choose(s, *level, sims(1));
        check_accepted(s, cut);
        if (whole.steps > 1) CHECK(cut.stopped == "sims");
        CHECK(cut.sims < whole.sims);
        if (level->maximum) CHECK(cut.sims * 10 <= whole.sims);
    }
}

TEST_CASE("spec 10.6, Think: one step allowed, it still answers, stopped by steps after one") {
    const FrameState s = named("after_red");
    for (const Level* level : both()) {
        CAPTURE(level->id);
        const Stroke& whole = full(*level);
        Think t;
        t.max_steps = 1;
        const Stroke cut = choose(s, *level, t);
        check_accepted(s, cut);
        if (whole.steps > 1) {
            CHECK(cut.stopped == "steps");
            CHECK(cut.steps == 1);
            CHECK(cut.sims < whole.sims);
        }
        if (level->maximum) CHECK(cut.sims * 10 <= whole.sims);
    }
}

TEST_CASE("spec 10.6, Think: cancelled before it starts, it still answers, says cancelled and runs far fewer") {
    const FrameState s = named("after_red");
    const std::atomic<bool> cancel{true};
    for (const Level* level : both()) {
        CAPTURE(level->id);
        const Stroke& whole = full(*level);
        Think t;
        t.cancel = &cancel;
        const Stroke cut = choose(s, *level, t);
        check_accepted(s, cut);
        CHECK(cut.stopped == "cancelled");
        CHECK(cut.sims < whole.sims);
        if (level->maximum) CHECK(cut.sims * 10 <= whole.sims);
    }
}

TEST_CASE("README, the search: stopped before anything was found, it plays the stand-in's stroke and says so") {
    const std::atomic<bool> cancel{true};
    for (const Level* level : both()) {
        for (const std::string name : {"after_red", "straight_red", "in_hand", "snookered", "racked"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            const FrameState s = named(name);
            Think t;
            t.cancel = &cancel;
            const Stroke cut = choose(s, *level, t);
            CHECK_FALSE(cut.fallback.empty());
            check_accepted(s, cut);
            REQUIRE(cut.why.is_object());
            CHECK(text(cut.why, "level") == level->id);
        }
    }
}

TEST_CASE("README, the search: max_steps = k takes exactly k steps when the search needs more, and again the same") {
    for (const Level* level : both()) {
        for (const std::string name : {"after_red", "straight_red"}) {
            const FrameState s = named(name);
            const Think base = level->maximum ? sims(1500) : Think{};
            const Stroke whole = choose(s, *level, base);
            for (int k : {1, 3, 10}) {
                CAPTURE(level->id);
                CAPTURE(name);
                CAPTURE(k);
                Think t = base;
                t.max_steps = k;
                const Stroke a = choose(s, *level, t);
                const Stroke b = choose(s, *level, t);
                if (whole.steps > k) {
                    CHECK(a.steps == k);
                    CHECK(a.stopped == "steps");
                } else {
                    CHECK(a.steps == whole.steps);
                }
                CHECK(same_stroke(a, b));
                CHECK(a.steps == b.steps);
                CHECK(a.sims == b.sims);
            }
        }
    }
}

TEST_CASE("README, the search: a run's steps given back as max_steps make the same decision again") {
    for (const Level* level : both()) {
        CAPTURE(level->id);
        const FrameState s = named("after_red");
        const Stroke& whole = full(*level);
        REQUIRE(whole.steps > 0);
        Think t;
        t.max_steps = whole.steps;
        const Stroke again = choose(s, *level, t);
        CHECK(same_stroke(whole, again));
        CHECK(again.steps == whole.steps);
    }
    const FrameState s = named("straight_red");
    const Level& beginner = rig::levels().beginner;
    const Stroke whole = choose(s, beginner, Think{});
    REQUIRE(whole.steps > 0);
    Think t;
    t.max_steps = whole.steps;
    CHECK(same_stroke(whole, choose(s, beginner, t)));
}

TEST_CASE("README, the search: a whole run with a pot on the table finishes or spends its sims, having weighed candidates") {
    for (const Level* level : both()) {
        CAPTURE(level->id);
        const Stroke& whole = full(*level);
        CAPTURE(whole.stopped);
        CHECK((whole.stopped == "done" || whole.stopped == "sims"));
        CHECK(whole.candidates > 0);
        CHECK(whole.sims > 0);
        CHECK(whole.steps > 0);
        CHECK(whole.fallback.empty());
    }
    const Stroke beginner = choose(named("straight_red"), rig::levels().beginner, Think{});
    CHECK((beginner.stopped == "done" || beginner.stopped == "sims"));
    CHECK(beginner.candidates > 0);
    CHECK(beginner.sims > 0);
    CHECK(beginner.fallback.empty());
}

TEST_CASE("README, the 147: with its whole budget it plans past this stroke, and its why gives the plan") {
    const Level& level = rig::levels().maximum;
    const Stroke& whole = full(level);
    CHECK(whole.depth >= 1);
    CHECK(whole.depth <= level.depth);
    REQUIRE(whole.why.is_object());
    REQUIRE(whole.why.contains("plan"));
    const json& plan = whole.why["plan"];
    REQUIRE(plan.is_array());
    CHECK(plan.size() >= 1);
    for (const json& step : plan) {
        CHECK(step.contains("target"));
        CHECK(step.contains("pocket"));
    }
    if (!plan.empty()) CHECK(text(plan[0], "target").rfind("red ", 0) == 0);
}

TEST_CASE("opponent.hpp, Think: a cancel flag left unset and a generous clock change nothing") {
    const FrameState s = named("after_red");
    const std::atomic<bool> unset{false};
    for (const Level* level : both()) {
        CAPTURE(level->id);
        const Think base = level->maximum ? sims(1500) : Think{};
        const Stroke plain = choose(s, *level, base);
        Think flagged = base;
        flagged.cancel = &unset;
        Think clocked = base;
        clocked.max_ms = 1.0e9;
        const Stroke a = choose(s, *level, flagged);
        const Stroke b = choose(s, *level, clocked);
        CHECK(same_stroke(plain, a));
        CHECK(same_stroke(plain, b));
        CHECK(a.steps == plain.steps);
        CHECK(b.steps == plain.steps);
        CHECK(a.stopped != "cancelled");
        CHECK(b.stopped != "time");
    }
}

TEST_CASE("opponent.hpp, Think: max_sims 0 takes the level's own max_sims") {
    const FrameState s = named("straight_red");
    const Level& beginner = rig::levels().beginner;
    const Stroke a = choose(s, beginner, Think{});
    const Stroke b = choose(s, beginner, sims(beginner.max_sims));
    CHECK(same_stroke(a, b));
    CHECK(a.sims == b.sims);
    CHECK(a.steps == b.steps);
}
