// Beginner (spec 10.1 row 1, 10.3; docs/opponent/README.md "The search"): its error drawn once and
// said in its why, pots that go in only some of the time and less often when long or cut, a pot
// played whenever there is one, and mostly centre ball.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using nlohmann::json;
using rules::Ball;
using rules::FrameState;

namespace {

const char* const kPositions[] = {"straight_red", "cut_red",  "long_red",  "colours",   "black_only",
                                  "last_red",     "after_red", "in_hand", "snookered", "racked"};
constexpr double kPi = 3.14159265358979323846;

FrameState named(const std::string& name) { return name == "racked" ? rig::racked(1) : rig::position(name.c_str()); }

const Level& beginner() { return rig::levels().beginner; }

Stroke choose(const FrameState& s, std::uint64_t seed) {
    Rng rng(seed);
    return choose_stroke(s, beginner(), rig::env(), rng);
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

bool scored_clean(const Result& r) { return r.placed && r.judgement.points > 0 && r.judgement.fouls.empty(); }

struct Tally {
    int strokes = 0;
    int potted = 0;      // scored, no foul
    int pots_tried = 0;  // why.kind "pot"
    double rate() const { return strokes > 0 ? double(potted) / strokes : 0.0; }
};

// Seeds 1..n from one position, each stroke chosen and played; counted once per position.
const Tally& tally(const std::string& name, int n) {
    static std::map<std::pair<std::string, int>, Tally> cache;
    const auto key = std::make_pair(name, n);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Tally t;
    const FrameState s = named(name);
    for (std::uint64_t seed = 1; seed <= std::uint64_t(n); ++seed) {
        const Stroke st = choose(s, seed);
        ++t.strokes;
        if (text(st.why, "kind") == "pot") ++t.pots_tried;
        if (!st.decides && scored_clean(play_stroke(s, st))) ++t.potted;
    }
    return cache.emplace(key, t).first->second;
}

// "red 3" or "black" back to its place in `s`; -1 when it names no ball there.
int place_named(const FrameState& s, const std::string& target) {
    std::istringstream in(target);
    std::string word;
    int index = 0;
    in >> word;
    if (!(in >> index)) index = 0;
    Ball b = Ball::cue;
    if (!table::parse_ball(word, b)) return -1;
    return rules::scenario::place_of(s, b, index);
}

}  // namespace

TEST_CASE("spec 10.3, README the search: Beginner's strike is its meant stroke plus why.drawn") {
    const phys::PhysicsParams& params = rules::scenario::rig().params;
    int checked = 0;
    for (const std::string name : {"straight_red", "long_red", "cut_red"}) {
        const FrameState s = named(name);
        for (std::uint64_t seed = 1; seed <= 10; ++seed) {
            CAPTURE(name);
            CAPTURE(seed);
            const Stroke st = choose(s, seed);
            if (st.decides || text(st.why, "kind") != "pot") continue;
            ++checked;
            CHECK_FALSE(same_strike(st.strike, st.meant));
            REQUIRE(st.why.contains("drawn"));
            const json& d = st.why["drawn"];
            for (const char* key : {"aim_rad", "speed", "tip_x", "tip_y"}) {
                CAPTURE(key);
                REQUIRE(d.contains(key));
                REQUIRE(d[key].is_number());
            }
            const double aim_off = std::remainder(st.strike.aim_rad - st.meant.aim_rad - d["aim_rad"].get<double>(),
                                                  2.0 * kPi);
            CHECK(std::fabs(aim_off) < 1e-9);
            const double speed = st.strike.speed_m_s;
            if (speed > kLeastSpeed_m_s + 1e-12 && speed < params.max_cue_speed_m_s - 1e-12) {
                CHECK(speed == doctest::Approx(st.meant.speed_m_s * (1.0 + d["speed"].get<double>())).epsilon(1e-9));
            }
            if (std::hypot(st.strike.tip_x, st.strike.tip_y) < kTipLimit * params.miscue_limit - 1e-12) {
                CHECK(st.strike.tip_x == doctest::Approx(st.meant.tip_x + d["tip_x"].get<double>()).epsilon(1e-9));
                CHECK(st.strike.tip_y == doctest::Approx(st.meant.tip_y + d["tip_y"].get<double>()).epsilon(1e-9));
            }
            CHECK(st.strike.elevation_rad == st.meant.elevation_rad);
        }
    }
    CHECK(checked >= 15);
}

TEST_CASE("spec 10.1: Beginner pots the straight red some of the time, not always and not never") {
    const Tally& t = tally("straight_red", 300);
    CAPTURE(t.potted);
    CAPTURE(t.pots_tried);
    CHECK(t.rate() >= 0.20);
    CHECK(t.rate() <= 0.98);
}

TEST_CASE("spec 10.3: Beginner pots a long red less often than a short straight one") {
    const Tally& straight = tally("straight_red", 300);
    const Tally& long_red = tally("long_red", 300);
    CAPTURE(straight.potted);
    CAPTURE(long_red.potted);
    CHECK(long_red.rate() < straight.rate());
}

TEST_CASE("spec 10.3: Beginner pots a 55 degree cut less often than the same red straight") {
    const Tally& straight = tally("straight_red", 300);
    const Tally& cut = tally("cut_red", 300);
    CAPTURE(straight.potted);
    CAPTURE(cut.potted);
    CHECK(cut.rate() < straight.rate());
}

TEST_CASE("spec 10.1: Beginner rarely plays safe: with a straight red on it goes for the pot") {
    const Tally& t = tally("straight_red", 300);
    CAPTURE(t.pots_tried);
    CHECK(t.pots_tried >= 240);
}

TEST_CASE("spec 10.1, README the search: Beginner's planned tip offset stays within its max_tip") {
    for (const std::string name : kPositions) {
        const FrameState s = named(name);
        for (std::uint64_t seed = 1; seed <= 5; ++seed) {
            CAPTURE(name);
            CAPTURE(seed);
            const Stroke st = choose(s, seed);
            if (st.decides) continue;
            CHECK(std::hypot(st.meant.tip_x, st.meant.tip_y) <= beginner().max_tip + 1e-12);
        }
    }
}

TEST_CASE("spec 10.2 step 6: Beginner's error is drawn afresh: ten seeds, ten different strokes") {
    const FrameState s = named("straight_red");
    std::vector<phys::CueStrike> seen;
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
        const Stroke st = choose(s, seed);
        REQUIRE_FALSE(st.decides);
        for (const phys::CueStrike& other : seen) CHECK_FALSE(same_strike(other, st.strike));
        seen.push_back(st.strike);
    }
}

TEST_CASE("README, why: Beginner's pot names a pot the position offers, with its band and its cut") {
    int checked = 0;
    for (const std::string name : {"straight_red", "cut_red", "long_red", "colours"}) {
        const FrameState s = named(name);
        const std::vector<Pot> listed = pots(s, rig::env(), beginner().max_cut_rad);
        for (std::uint64_t seed = 1; seed <= 5; ++seed) {
            CAPTURE(name);
            CAPTURE(seed);
            const Stroke st = choose(s, seed);
            if (st.decides || text(st.why, "kind") != "pot") continue;
            REQUIRE(st.why.contains("pocket"));
            REQUIRE(st.why["pocket"].is_number_integer());
            const int target = place_named(s, text(st.why, "target"));
            const int pocket = st.why["pocket"].get<int>();
            CAPTURE(text(st.why, "target"));
            CAPTURE(pocket);
            const Pot* match = nullptr;
            for (const Pot& p : listed) {
                if (p.target == target && p.pocket == pocket) {
                    match = &p;
                    break;
                }
            }
            REQUIRE(match != nullptr);
            ++checked;
            CHECK(text(st.why, "band") == band(*match));
            REQUIRE(st.why.contains("cut_rad"));
            CHECK(st.why["cut_rad"].get<double>() == doctest::Approx(match->cut_rad).epsilon(1e-6));
        }
    }
    CHECK(checked >= 10);
}
