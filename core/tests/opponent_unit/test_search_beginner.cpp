#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::FrameState;

namespace {

const Level& beginner() { return rig::levels().beginner; }

Stroke choose(const FrameState& s, std::uint64_t seed, const Think& think = {}) {
    Rng rng(seed);
    return choose_stroke(s, beginner(), rig::env(), rng, think);
}

rules::Judgement played(const FrameState& s, const Stroke& stroke) {
    FrameState from = s;
    std::string error;
    if (stroke.places_cue) REQUIRE(rules::place_cue_ball(from, stroke.cue_pos, rig::env().oracle, error));
    return rules::scenario::play(from, stroke.strike, stroke.input).judgement;
}

int pocket_named(const char* name) {
    const phys::TableShape& table = rules::scenario::rig().table;
    for (std::size_t i = 0; i < table.pockets.size(); ++i) {
        if (table.pockets[i].name == name) return int(i);
    }
    return -1;
}

struct Count {
    int pots_tried = 0;
    int potted = 0;
};

Count over_seeds(const char* name, int seeds) {
    const FrameState s = rig::position(name);
    Count out;
    for (std::uint64_t seed = 1; seed <= std::uint64_t(seeds); ++seed) {
        const Stroke st = choose(s, seed);
        if (st.decides) continue;
        if (st.why.value("kind", std::string()) == "pot") ++out.pots_tried;
        const rules::Judgement j = played(s, st);
        if (j.fouls.empty() && j.points > 0) ++out.potted;
    }
    return out;
}

}  // namespace

TEST_CASE("Beginner on the straight red plans the red into the top-left corner") {
    const FrameState s = rig::position("straight_red");
    int pots_seen = 0;
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        CAPTURE(seed);
        const Stroke st = choose(s, seed);
        REQUIRE_FALSE(st.decides);
        if (st.why.value("kind", std::string()) != "pot") continue;
        ++pots_seen;
        CHECK(st.why.value("target", std::string()) == "red 0");
        CHECK(st.why.value("pocket", -1) == pocket_named("top_left"));
        CHECK(st.why.value("maximum", std::string()) == "off");
        CHECK(st.why["plan"].empty());
        CHECK(st.fallback.empty());
        CHECK(st.candidates > 0);

        // The stroke it meant pots the red: only its error keeps it out.
        Stroke exact = st;
        exact.strike = st.meant;
        const rules::Judgement j = played(s, exact);
        CHECK(j.fouls.empty());
        CHECK(j.points == 1);
    }
    CHECK(pots_seen >= 10);
}

TEST_CASE("Beginner's strike is the stroke it meant with what it drew added") {
    const phys::PhysicsParams& params = rules::scenario::rig().params;
    const FrameState s = rig::position("straight_red");
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        CAPTURE(seed);
        const Stroke st = choose(s, seed);
        if (st.why.value("kind", std::string()) != "pot") continue;
        const nlohmann::json& drawn = st.why["drawn"];
        CHECK(std::remainder(st.strike.aim_rad - st.meant.aim_rad - drawn["aim_rad"].get<double>(), 6.283185307179586) ==
              doctest::Approx(0.0).epsilon(1e-12));
        CHECK(drawn["aim_rad"].get<double>() != 0.0);
        const double speed = st.meant.speed_m_s * (1.0 + drawn["speed"].get<double>());
        CHECK(st.strike.speed_m_s == doctest::Approx(std::clamp(speed, kLeastSpeed_m_s, params.max_cue_speed_m_s)));
        if (std::hypot(st.strike.tip_x, st.strike.tip_y) < kTipLimit * params.miscue_limit - 1e-9) {
            CHECK(st.strike.tip_x == doctest::Approx(st.meant.tip_x + drawn["tip_x"].get<double>()));
            CHECK(st.strike.tip_y == doctest::Approx(st.meant.tip_y + drawn["tip_y"].get<double>()));
        }
        CHECK(st.strike.elevation_rad == st.meant.elevation_rad);
        CHECK(std::hypot(st.meant.tip_x, st.meant.tip_y) <= beginner().max_tip + 1e-12);
        CHECK(st.why["spec"]["speed_m_s"].get<double>() == st.meant.speed_m_s);
    }
}

TEST_CASE("Beginner goes for a pot that is on, and pots the straight red more often than the long one") {
    const Count straight = over_seeds("straight_red", 300);
    const Count far = over_seeds("long_red", 300);
    CAPTURE(straight.potted);
    CAPTURE(far.potted);
    CHECK(straight.pots_tried >= 240);
    CHECK(straight.potted > far.potted);
    CHECK(straight.potted < 300);
}

TEST_CASE("Beginner in hand places the cue ball where its pot is played from") {
    const FrameState s = rig::position("in_hand");
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        CAPTURE(seed);
        const Stroke st = choose(s, seed);
        REQUIRE_FALSE(st.decides);
        REQUIRE(st.places_cue);
        CHECK(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, st.cue_pos));
        played(s, st);
    }
}

TEST_CASE("Beginner breaks off, and plays safe where no pot is on") {
    const FrameState rack = rig::racked();
    const Stroke off = choose(rack, 3);
    REQUIRE_FALSE(off.decides);
    CHECK(off.why.value("kind", std::string()) == "break");
    CHECK(off.why.value("level", std::string()) == "beginner");
    CHECK(off.places_cue);
    played(rack, off);

    const FrameState snookered = rig::position("snookered");
    const Stroke out = choose(snookered, 3);
    REQUIRE_FALSE(out.decides);
    const std::string kind = out.why.value("kind", std::string());
    CHECK((kind == "escape" || kind == "safety"));
    CHECK(out.why.contains("drawn"));
    CHECK(out.why.value("maximum", std::string()) == "off");
    CHECK(out.fallback.empty());
}

TEST_CASE("Beginner's misreading is its own stream: the error drawn is the seed's first four normals") {
    const FrameState s = rig::position("straight_red");
    Rng rng(21);
    const Stroke st = choose_stroke(s, beginner(), rig::env(), rng);
    REQUIRE(st.why.value("kind", std::string()) == "pot");
    Rng same(21);
    for (int i = 0; i < 4; ++i) same.normal();
    CHECK(rng.next_u64() == same.next_u64());
}
