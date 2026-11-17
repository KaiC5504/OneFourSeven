// The error at the cushion (README "The search", Played): the error drawn can turn the aim to where
// the cue must be raised higher; the stroke played still clears it and differs from the one meant
// only by what was drawn.

#include <doctest/doctest.h>

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
using rules::Ball;
using rules::FrameState;
using rules::scenario::ball;

namespace {

constexpr double R = table::kBallRadius;
// The right side cushion's nose (test_shots_pots.cpp).
constexpr double kRightNose = -0.889;

double least_elevation(const FrameState& s, const Stroke& st) {
    FrameState from = s;
    std::string error;
    if (st.places_cue) REQUIRE(rules::place_cue_ball(from, st.cue_pos, rig::env().oracle, error));
    return phys::min_cue_elevation(rules::scenario::table_state(from), from.cue_ball, st.strike.aim_rad,
                                   rules::scenario::rig().table, rules::scenario::rig().params);
}

}  // namespace

TEST_CASE("Beginner on the cushion: every stroke clears the cushion as played, and meant is raised with it") {
    const Level& level = rig::levels().beginner;
    // The cue ball touching the right cushion, a red straight up the table for the middle left, and
    // one at a shallow angle off the cushion for the top right.
    const std::vector<FrameState> positions = {
        rules::scenario::frame({ball(Ball::cue, 0.0, kRightNose + R + 0.0005), ball(Ball::red, 0.0, 0.3)}),
        rules::scenario::frame({ball(Ball::cue, -0.5, kRightNose + R + 0.0005), ball(Ball::red, 0.8, -0.7)}),
        rules::scenario::frame(
            {ball(Ball::cue, 0.0, -0.3), ball(Ball::red, 0.0, 0.5), ball(Ball::brown, 0.0, -0.3 - 2.0 * R - 0.002)}),
    };
    int raised = 0;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const FrameState& s = positions[i];
        for (std::uint64_t seed = 1; seed <= 25; ++seed) {
            CAPTURE(i);
            CAPTURE(seed);
            Rng rng(seed);
            const Stroke st = choose_stroke(s, level, rig::env(), rng);
            REQUIRE_FALSE(st.decides);
            CHECK(st.strike.elevation_rad == st.meant.elevation_rad);
            CHECK(st.strike.elevation_rad >= least_elevation(s, st) - 1e-12);
            CHECK(st.strike.elevation_rad <= rules::scenario::rig().params.max_elevation_rad);
            if (st.strike.elevation_rad > 0.0) ++raised;
        }
    }
    CHECK(raised > 0);
}

TEST_CASE("the 147 on the cushion plays the stroke it meant, the cue clear, whatever the seed") {
    const Level& level = rig::levels().maximum;
    const FrameState s =
        rules::scenario::frame({ball(Ball::cue, 0.0, kRightNose + R + 0.0005), ball(Ball::red, 0.0, 0.3)});
    Think t;
    t.max_sims = 600;
    Rng first_rng(1);
    const Stroke first = choose_stroke(s, level, rig::env(), first_rng, t);
    REQUIRE_FALSE(first.decides);
    CHECK(first.strike.elevation_rad >= least_elevation(s, first) - 1e-12);
    for (const std::uint64_t seed : {2u, 3u, 1000u}) {
        Rng rng(seed);
        const Stroke st = choose_stroke(s, level, rig::env(), rng, t);
        CHECK(st.strike.aim_rad == first.strike.aim_rad);
        CHECK(st.strike.speed_m_s == first.strike.speed_m_s);
        CHECK(st.strike.tip_x == first.strike.tip_x);
        CHECK(st.strike.tip_y == first.strike.tip_y);
        CHECK(st.strike.elevation_rad == first.strike.elevation_rad);
        CHECK(st.strike.elevation_rad == st.meant.elevation_rad);
    }
}
