// What the opponent promises whatever its level (spec 10.2 step 6, 10.6): a stroke the rules
// accept, from any position, and the same stroke again from the same seed.

#include <doctest/doctest.h>

#include "ofs/opponent/opponent.hpp"
#include "ofs/physics/serialize.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;

TEST_CASE("spec 10.2: a stroke chosen twice from one seed is the same stroke") {
    const rules::FrameState s = rig::position("straight_red");
    for (const Level* level : {&rig::levels().beginner, &rig::levels().maximum}) {
        Rng a(42);
        Rng b(42);
        const Stroke first = choose_stroke(s, *level, rig::env(), a);
        const Stroke second = choose_stroke(s, *level, rig::env(), b);
        CHECK(phys::to_json(first.strike) == phys::to_json(second.strike));
        CHECK(first.input.has_nomination == second.input.has_nomination);
        CHECK(first.steps == second.steps);
    }
}

TEST_CASE("spec 10.2: with the cue ball in hand the stroke places it in the D, clear of every ball") {
    const rules::FrameState s = rig::position("in_hand");
    Rng rng(5);
    const Stroke stroke = choose_stroke(s, rig::levels().beginner, rig::env(), rng);
    REQUIRE_FALSE(stroke.decides);
    REQUIRE(stroke.places_cue);
    CHECK(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, stroke.cue_pos));
}
