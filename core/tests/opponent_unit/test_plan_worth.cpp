// A pot's worth for a level with error (README "Worth"): what a miss gives away, the chance from
// noisy strokes, and the plan a level with depth looks along. Each on a level written out here,
// so the tuning of the level files does not move it.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::FrameState;

namespace {

// Club's starting values, with no tactics, misreading, floor or concession.
Level fixed() {
    Level l;
    l.id = "test";
    l.name = "Test";
    l.tier = 2;
    l.aim_sigma_rad = 0.0016;
    l.aim_sigma_per_cut = 0.6;
    l.aim_sigma_per_m = 0.6;
    l.awkward_factor = 1.4;
    l.speed_sigma = 0.1;
    l.tip_sigma = 0.06;
    l.max_cut_rad = 1.2;
    l.max_tip = 0.25;
    l.speeds = 3;
    l.spins = 3;
    l.position_weight = 0.3;
    l.max_sims = 400;
    l.score_aware = 0.2;
    return l;
}

Stroke choose(const FrameState& s, const Level& level, std::uint64_t seed = 1) {
    Rng rng(seed);
    return choose_stroke(s, level, rig::env(), rng);
}

std::string text(const nlohmann::json& why, const char* key) { return why.value(key, std::string()); }

}  // namespace

TEST_CASE("the cost of a miss turns a level away from the pot whose miss leaves the other player in") {
    const FrameState s = rig::position("pl_miss_gives_away");
    Level careless = fixed();
    Level careful = fixed();
    careful.miss_weight = 1.0;

    const Stroke takes = choose(s, careless);
    REQUIRE(text(takes.why, "kind") == "pot");
    CHECK(text(takes.why, "target") == "red 5");
    CHECK(takes.why.value("pocket", -1) == 5);
    CHECK_FALSE(takes.why.contains("giveaway"));

    const Stroke turns = choose(s, careful);
    REQUIRE(text(turns.why, "kind") == "pot");
    CHECK(text(turns.why, "target") != "red 5");
    REQUIRE(turns.why.contains("giveaway"));
    const double chance = turns.why["chance"].get<double>();
    // What it chose costs it little when it misses, and is worth less than the other was without
    // the cost: it turned away for the miss, not for the pot.
    CHECK((1.0 - chance) * turns.why["giveaway"].get<double>() < 1.0);
    CHECK(turns.why["worth"].get<double>() < takes.why["worth"].get<double>());
    // Each candidate made simulated one miss: more simulations than without.
    CHECK(turns.sims > takes.sims);
}

TEST_CASE("with miss_weight 0 no miss is simulated") {
    const FrameState s = rig::position("straight_red");
    Level level = fixed();
    const Stroke st = choose(s, level);
    level.miss_weight = 1e-9;
    const Stroke costed = choose(s, level);
    REQUIRE(text(st.why, "kind") == "pot");
    CHECK_FALSE(st.why.contains("miss_turn"));
    CHECK(costed.steps == st.steps);
    // One to three turned strokes a side for each candidate made, within its step.
    CHECK(costed.sims >= st.sims + 2 * costed.candidates);
    CHECK(costed.sims <= st.sims + 6 * costed.candidates);
}

TEST_CASE("the giveaway is the mean of a thick and a thin miss, each turned 2, 4 or 8 margins until the ball stays out") {
    int missed_both = 0;
    for (const char* name : {"straight_red", "cut_red", "long_red", "pl_miss_gives_away"}) {
        CAPTURE(name);
        const FrameState s = rig::position(name);
        Level level = fixed();
        level.miss_weight = 1.0;
        const Stroke st = choose(s, level);
        REQUIRE(text(st.why, "kind") == "pot");
        const nlohmann::json& turns = st.why["miss_turn"];
        REQUIRE(turns.is_array());
        REQUIRE(turns.size() == 2);
        MESSAGE(std::string(name) << ": missed thick at " << turns[0] << " and thin at " << turns[1]
                                  << " margins, giving away " << st.why["giveaway"].get<double>());
        for (const nlohmann::json& turn : turns) {
            const double t = turn.get<double>();
            CHECK((t == 0.0 || t == 2.0 || t == 4.0 || t == 8.0));
        }
        missed_both += turns[0].get<double>() > 0.0 && turns[1].get<double>() > 0.0 ? 1 : 0;
        CHECK(st.why["giveaway"].get<double>() >= 0.0);
    }
    CHECK(missed_both >= 3);
}

TEST_CASE("noisy chance is within sampling error of pot_chance on an open pot") {
    const FrameState s = rig::position("straight_red");
    Level level = fixed();
    // Aim error alone, wide enough that the pot is about even; one way of striking it.
    level.aim_sigma_rad = 0.004;
    level.speed_sigma = 0.0;
    level.tip_sigma = 0.0;
    level.speeds = 1;
    level.spins = 1;
    level.max_tip = 0.0;
    const Stroke margin = choose(s, level);
    level.noisy_sims = 300;
    const Stroke noisy = choose(s, level);
    REQUIRE(text(margin.why, "kind") == "pot");
    REQUIRE(text(noisy.why, "kind") == "pot");
    CHECK_FALSE(margin.why.contains("chance_by"));
    CHECK(text(noisy.why, "chance_by") == "sims");
    CHECK(noisy.meant.aim_rad == margin.meant.aim_rad);
    // The geometric margin is narrower than the pocket takes (README "Error"): the chance to
    // compare with is pot_chance on the window the simulator measures for this very stroke.
    const Pot pot = pots(s, rig::env(), level.max_cut_rad).front();
    REQUIRE(pot.pocket == margin.why.value("pocket", -1));
    const Spec spec{noisy.meant.speed_m_s, noisy.meant.tip_x, noisy.meant.tip_y, noisy.meant.elevation_rad};
    Pot from = pot;
    from.aim_rad = noisy.meant.aim_rad;
    const SolvedPot solved = solve_pot(s, from, spec, rig::env(), true, 60);
    REQUIRE(solved.found);
    const double p = pot_chance(level, pot, spec, solved.window_rad);
    const double share = noisy.why["chance"].get<double>();
    MESSAGE("straight red: the margin reads " << margin.why["chance"].get<double>() << ", the measured window "
                                              << p << ", 300 noisy strokes pot " << share);
    REQUIRE(p > 0.2);
    REQUIRE(p < 0.95);
    const double sampling = std::sqrt(p * (1.0 - p) / 300.0);
    // Three standard errors, and a little for the window measured to the solver's tolerance.
    CHECK(std::abs(share - p) < 3.0 * sampling + 0.03);
    CHECK(noisy.sims >= margin.sims + 300);
}

TEST_CASE("noisy chance is lower where the margin flatters: it counts the speed and tip error too") {
    const FrameState s = rig::position("cut_red");
    Level level = fixed();
    // An aim the margin calls certain, with a cue whose pace and tip wander widely.
    level.aim_sigma_rad = 1e-6;
    level.aim_sigma_per_cut = 0.0;
    level.aim_sigma_per_m = 0.0;
    level.speed_sigma = 0.35;
    level.tip_sigma = 0.35;
    level.speeds = 1;
    level.spins = 1;
    level.max_tip = 0.0;
    const Stroke margin = choose(s, level);
    level.noisy_sims = 200;
    const Stroke noisy = choose(s, level);
    REQUIRE(text(margin.why, "kind") == "pot");
    REQUIRE(text(noisy.why, "kind") == "pot");
    const double p = margin.why["chance"].get<double>();
    const double share = noisy.why["chance"].get<double>();
    MESSAGE("cut red: the margin reads " << p << ", 200 noisy strokes pot " << share);
    CHECK(p > 0.99);
    CHECK(share < p - 0.1);
}

TEST_CASE("the noisy strokes have their own stream: the stroke's error is still the seed's first four draws") {
    const FrameState s = rig::position("straight_red");
    Level level = fixed();
    level.noisy_sims = 20;
    Rng rng(21);
    const Stroke st = choose_stroke(s, level, rig::env(), rng);
    REQUIRE(text(st.why, "kind") == "pot");
    Rng same(21);
    for (int i = 0; i < 4; ++i) same.normal();
    CHECK(rng.next_u64() == same.next_u64());
    // And they are the same strokes for the same seed.
    CHECK(choose(s, level, 21).why == st.why);
}

TEST_CASE("depth 1 prefers the pot that leaves the next ball over the one worth more now") {
    const FrameState s = rig::position("pl_black_or_brown");
    Level level = fixed();
    level.position_weight = 0.6;
    level.max_sims = 1500;
    const Stroke now = choose(s, level);
    level.depth = 1;
    const Stroke ahead = choose(s, level);
    REQUIRE(text(now.why, "kind") == "pot");
    REQUIRE(text(ahead.why, "kind") == "pot");
    CHECK(text(now.why, "target") == "black");
    CHECK(now.depth == 0);
    CHECK(now.why["plan"].empty());

    CHECK(text(ahead.why, "target") != "black");
    CHECK(ahead.depth == 1);
    REQUIRE(ahead.why["plan"].size() == 1);
    CHECK(ahead.why["plan"][0].value("target", std::string()) == "yellow");
    // It leaves the yellow far easier than the black would have.
    CHECK(ahead.why["leave"]["chance"].get<double>() > now.why["leave"]["chance"].get<double>() + 0.3);
}
