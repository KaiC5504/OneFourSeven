// Any level, any position (spec 10.2 step 6, opponent.hpp, docs/opponent/README.md): the stroke
// is one the engine accepts, its `why` says what it is, and one seed gives one stroke.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
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

const char* const kPositions[] = {"straight_red", "cut_red",  "long_red",  "colours",   "black_only",
                                  "last_red",     "after_red", "in_hand", "snookered", "racked"};
const std::uint64_t kSeeds[] = {1, 2, 3};

FrameState named(const std::string& name) { return name == "racked" ? rig::racked(1) : rig::position(name.c_str()); }

std::vector<const Level*> both() { return {&rig::levels().beginner, &rig::levels().maximum}; }

// The 147's own budget is tens of thousands of simulations; Beginner keeps its own.
Think budget(const Level& level) {
    Think t;
    if (level.maximum) t.max_sims = 1500;
    return t;
}

Stroke choose(const FrameState& s, const Level& level, std::uint64_t seed) {
    Rng rng(seed);
    return choose_stroke(s, level, rig::env(), rng, budget(level));
}

// One choice per (position, level, seed): several tests read the same strokes.
const Stroke& chosen(const std::string& name, const Level& level, std::uint64_t seed) {
    static std::map<std::tuple<std::string, std::string, std::uint64_t>, Stroke> cache;
    const auto key = std::make_tuple(name, level.id, seed);
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, choose(named(name), level, seed)).first;
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

bool finite(const phys::CueStrike& c) {
    return std::isfinite(c.aim_rad) && std::isfinite(c.speed_m_s) && std::isfinite(c.tip_x) &&
           std::isfinite(c.tip_y) && std::isfinite(c.elevation_rad);
}

bool offered(const std::vector<rules::DecisionKind>& options, rules::DecisionKind k) {
    return std::find(options.begin(), options.end(), k) != options.end();
}

void check_accepted(const FrameState& s, const Stroke& st) {
    const phys::PhysicsParams& params = rules::scenario::rig().params;
    if (st.decides) {
        CHECK(offered(rules::options(s, s.striker), st.decision));
        return;
    }
    CHECK(st.places_cue == s.cue_in_hand);
    if (st.places_cue) {
        CHECK(std::isfinite(st.cue_pos.x));
        CHECK(std::isfinite(st.cue_pos.y));
        CHECK(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, st.cue_pos));
    }
    if (s.on.kind == rules::OnKind::any_colour) {
        CHECK(st.input.has_nomination);
        if (st.input.has_nomination) {
            const std::vector<Ball> open = rules::nominations(s);
            CHECK(std::find(open.begin(), open.end(), st.input.nominated) != open.end());
        }
    }
    CHECK(finite(st.strike));
    CHECK(finite(st.meant));
    CHECK(st.strike.speed_m_s > 0.0);
    CHECK(st.strike.speed_m_s <= params.max_cue_speed_m_s);
    CHECK(std::hypot(st.strike.tip_x, st.strike.tip_y) <= kTipLimit * params.miscue_limit + 1e-12);
}

// The stroke played as a match plays it: placed when in hand, simulated, judged.
struct Result {
    bool placed = false;
    rules::Judgement judgement;
};

Result play_stroke(const FrameState& s, const Stroke& stroke) {
    Result out;
    FrameState f = s;
    std::string error;
    if (stroke.places_cue && !rules::place_cue_ball(f, stroke.cue_pos, rig::env().oracle, error)) return out;
    out.placed = true;
    out.judgement = rules::scenario::play(f, stroke.strike, stroke.input).judgement;
    return out;
}

// Only the black left, the striker more than seven ahead, no break under way.
FrameState black_and_ahead(int lead) {
    FrameState s = rig::position("black_only");
    s.scores = {20 + lead, 20};
    s.break_points = 0;
    return s;
}

}  // namespace

TEST_CASE("spec 10.2 step 6: from every named position and the break-off, either level's stroke is one the engine accepts") {
    for (const Level* level : both()) {
        for (const std::string name : kPositions) {
            for (std::uint64_t seed : kSeeds) {
                CAPTURE(level->id);
                CAPTURE(name);
                CAPTURE(seed);
                const FrameState s = named(name);
                check_accepted(s, chosen(name, *level, seed));
            }
        }
    }
}

TEST_CASE("spec 10.2 step 6: every stroke chosen is placed and judged as a match plays it, the cue ball never refused") {
    for (const Level* level : both()) {
        for (const std::string name : kPositions) {
            CAPTURE(level->id);
            CAPTURE(name);
            const FrameState s = named(name);
            const Stroke& st = chosen(name, *level, 1);
            if (st.decides) continue;
            const Result r = play_stroke(s, st);
            CHECK(r.placed);
            CHECK(r.judgement.post.strokes == s.strokes + 1);
        }
    }
}

TEST_CASE("README, why: every stroke's why is an object with the level's id and a kind it names") {
    for (const Level* level : both()) {
        for (const std::string name : kPositions) {
            for (std::uint64_t seed : kSeeds) {
                CAPTURE(level->id);
                CAPTURE(name);
                CAPTURE(seed);
                const Stroke& st = chosen(name, *level, seed);
                REQUIRE(st.why.is_object());
                CHECK(text(st.why, "level") == level->id);
                const std::string kind = text(st.why, "kind");
                CAPTURE(kind);
                if (st.decides) {
                    CHECK(kind == "claim");
                } else {
                    CHECK((kind == "pot" || kind == "safety" || kind == "escape" || kind == "break"));
                }
            }
        }
    }
}

TEST_CASE("README, the search: one seed gives the same stroke, nomination, cue placement and steps again") {
    for (const Level* level : both()) {
        for (const std::string name : kPositions) {
            CAPTURE(level->id);
            CAPTURE(name);
            const Stroke& first = chosen(name, *level, 2);
            const Stroke again = choose(named(name), *level, 2);
            CHECK(first.decides == again.decides);
            CHECK(same_strike(first.strike, again.strike));
            CHECK(same_strike(first.meant, again.meant));
            CHECK(first.input.has_nomination == again.input.has_nomination);
            CHECK(first.input.nominated == again.input.nominated);
            CHECK(first.places_cue == again.places_cue);
            CHECK(first.cue_pos.x == again.cue_pos.x);
            CHECK(first.cue_pos.y == again.cue_pos.y);
            CHECK(first.steps == again.steps);
            CHECK(first.sims == again.sims);
        }
    }
}

TEST_CASE("opponent.hpp: with a colour of choice on, the stroke declares a colour on the table") {
    const FrameState s = rig::position("after_red");
    REQUIRE(s.on.kind == rules::OnKind::any_colour);
    for (const Level* level : both()) {
        for (std::uint64_t seed : kSeeds) {
            CAPTURE(level->id);
            CAPTURE(seed);
            const Stroke& st = chosen("after_red", *level, seed);
            REQUIRE_FALSE(st.decides);
            REQUIRE(st.input.has_nomination);
            const std::vector<Ball> open = rules::nominations(s);
            CHECK(std::find(open.begin(), open.end(), st.input.nominated) != open.end());
        }
    }
}

TEST_CASE("opponent.hpp: from in hand the cue ball is placed in the D, clear and legal, for both levels") {
    for (const std::string name : {"in_hand", "racked"}) {
        const FrameState s = named(name);
        REQUIRE(s.cue_in_hand);
        for (const Level* level : both()) {
            for (std::uint64_t seed : kSeeds) {
                CAPTURE(name);
                CAPTURE(level->id);
                CAPTURE(seed);
                const Stroke& st = chosen(name, *level, seed);
                REQUIRE_FALSE(st.decides);
                REQUIRE(st.places_cue);
                CHECK(table::in_d(st.cue_pos));
                CHECK(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, st.cue_pos));
            }
        }
    }
}

TEST_CASE("README, the search: with only the black left and more than seven ahead, either level claims the frame") {
    const FrameState s = black_and_ahead(20);
    const std::vector<rules::DecisionKind> open = rules::options(s, s.striker);
    REQUIRE(offered(open, rules::DecisionKind::claim_frame));
    for (const Level* level : both()) {
        CAPTURE(level->id);
        const Stroke st = choose(s, *level, 1);
        CHECK(st.decides);
        CHECK(st.decision == rules::DecisionKind::claim_frame);
        REQUIRE(st.why.is_object());
        CHECK(text(st.why, "kind") == "claim");
        CHECK(text(st.why, "level") == level->id);

        FrameState after = s;
        std::vector<rules::Call> calls;
        std::string error;
        CHECK(rules::decide(after, {st.decision, s.striker}, rig::env().oracle, rig::env().ref, calls, error));
        CHECK(after.over);
        CHECK(after.winner == s.striker);
    }
}

TEST_CASE("README, the search: seven ahead with only the black left is no claim, and the black is played") {
    const FrameState s = black_and_ahead(7);
    REQUIRE_FALSE(offered(rules::options(s, s.striker), rules::DecisionKind::claim_frame));
    for (const Level* level : both()) {
        CAPTURE(level->id);
        const Stroke st = choose(s, *level, 1);
        CHECK_FALSE(st.decides);
        check_accepted(s, st);
    }
}

TEST_CASE("spec 10.2 step 6: Beginner's error never takes its stroke outside the physics' limits") {
    const Level& beginner = rig::levels().beginner;
    for (const std::string name : {"straight_red", "long_red", "in_hand", "racked"}) {
        const FrameState s = named(name);
        for (std::uint64_t seed = 100; seed < 140; ++seed) {
            CAPTURE(name);
            CAPTURE(seed);
            Rng rng(seed);
            check_accepted(s, choose_stroke(s, beginner, rig::env(), rng));
        }
    }
}
