// The levels' risk (spec 10.1, 10.2 steps 4 and 5, 10.3, 10.4; docs/opponent/README.md "Worth",
// "The score"): the cost of a miss, the score's pull on a pot, concessions, and the ladder of
// execution from Beginner to World class.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/budget.hpp"
#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/opponent/tactics.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using nlohmann::json;
using rules::DecisionKind;
using rules::FrameState;

namespace {

constexpr int kSims = 400;

std::vector<const Level*> tactical() {
    const rig::Levels& l = rig::levels();
    return {&l.club, &l.amateur, &l.pro, &l.world};
}

Think sims(int n) {
    Think t;
    t.max_sims = n;
    return t;
}

Level sharp(const Level& level) {
    Level l = level;
    l.judgement_sigma = 0.0;
    return l;
}

Stroke choose(const FrameState& s, const Level& level, std::uint64_t seed = 1, const Think& think = sims(kSims)) {
    Rng rng(seed);
    return choose_stroke(s, level, rig::env(), rng, think);
}

std::string text(const json& j, const char* key) {
    if (!j.is_object()) return "";
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

bool offered(const std::vector<DecisionKind>& options, DecisionKind k) {
    return std::find(options.begin(), options.end(), k) != options.end();
}

bool tactic_found(const FrameState& s, const Level& level) {
    Think think = sims(kSims);
    Budget budget(level, think);
    Rng judgement(1);
    return choose_tactic(s, level, rig::env(), budget, judgement).found;
}

double best_chance(const FrameState& s, const Level& level) {
    double best = 0.0;
    for (const Pot& p : pots(s, rig::env(), level.max_cut_rad)) {
        best = std::max(best, pot_chance(level, p, Spec{}, p.margin_rad));
    }
    return best;
}

bool concedes(DecisionKind k) { return k == DecisionKind::concede_frame || k == DecisionKind::concede_match; }

// The other player (1) fouls from `name` by hitting nothing, four away to player 0, who may
// concede at any time: player 0's choice then holds the concessions beside play, play again and
// replace.
FrameState after_foul(const char* name, std::array<int, 2> scores) {
    FrameState s = rig::position(name);
    s.striker = 1;
    s.scores = scores;
    s.concede_any_time = {true, false};
    const rules::Judgement j = rules::scenario::play(s, rules::scenario::strike(1.5707963267948966, 0.4)).judgement;
    REQUIRE(j.post.pending == rules::Pending::after_foul);
    REQUIRE(j.post.decider == 0);
    REQUIRE(rules::scenario::has_foul(j, rules::FoulKind::no_hit));
    return j.post;
}

}  // namespace

TEST_CASE("README Worth, spec 10.2 step 4: a tempting pot whose miss leaves an easy red is turned down with miss_weight 1 and played with 0") {
    const FrameState s = rig::position("t3_cut_tempting");
    Level careful = sharp(rig::levels().amateur);
    careful.miss_weight = 1.0;
    Level reckless = careful;
    reckless.miss_weight = 0.0;
    const std::vector<Pot> listed = pots(s, rig::env(), careful.max_cut_rad);
    REQUIRE(listed.size() == 1);
    const double chance = pot_chance(careful, listed[0], Spec{}, listed[0].margin_rad);
    CAPTURE(chance);
    REQUIRE(chance >= careful.safety_below);
    REQUIRE(chance < 0.5);
    REQUIRE(tactic_found(s, careful));
    // The level's own budget: the pot must be weighed after the tactics have had their steps.
    for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2)}) {
        CAPTURE(seed);
        const Stroke plays = choose(s, reckless, seed, Think{});
        const Stroke declines = choose(s, careful, seed, Think{});
        CAPTURE(plays.why.dump());
        CAPTURE(declines.why.dump());
        CHECK(text(plays.why, "kind") == "pot");
        CHECK(text(plays.why, "target") == "red 0");
        CHECK(text(declines.why, "kind") != "pot");
    }
}

TEST_CASE("README The score: ahead by more than the points left, World class takes no pot under an even chance while it has a tactic") {
    const FrameState s = rig::position("t3_long_ahead");
    REQUIRE(s.scores[0] - s.scores[1] > rules::points_remaining(s));
    const Level world = sharp(rig::levels().world);
    REQUIRE(world.score_aware == 1.0);
    const double chance = best_chance(s, world);
    CAPTURE(chance);
    REQUIRE(chance >= world.safety_below);
    REQUIRE(chance < 0.5);
    REQUIRE(tactic_found(s, world));
    for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2)}) {
        CAPTURE(seed);
        const Stroke st = choose(s, world, seed);
        CAPTURE(st.why.dump());
        CHECK_FALSE(st.decides);
        CHECK(text(st.why, "kind") != "pot");
    }
}

TEST_CASE("README The score, spec 10.4: needing snookers past concede_snookers, every new level concedes at its turn and when asked") {
    const FrameState s = rig::position("t3_colours_behind");
    const std::vector<DecisionKind> open = rules::options(s, s.striker);
    REQUIRE(offered(open, DecisionKind::concede_frame));
    // 23 penalty points with only colours left is at least four snookers however they are counted.
    REQUIRE(rules::penalty_points_required(s, s.striker) == 23);
    const FrameState fouled = after_foul("colours", {6, 60});
    REQUIRE(rules::penalty_points_required(fouled, 0) == 23);
    const std::vector<DecisionKind> asked = rules::options(fouled, 0);
    REQUIRE(offered(asked, DecisionKind::play));
    REQUIRE(offered(asked, DecisionKind::concede_frame));
    for (const Level* level : tactical()) {
        CAPTURE(level->id);
        REQUIRE(level->concede_snookers >= 1);
        REQUIRE(level->concede_snookers <= 3);
        const Stroke st = choose(s, *level);
        CHECK(st.decides);
        CHECK(st.decision == DecisionKind::concede_frame);
        CHECK(text(st.why, "kind") == "concede");
        CHECK(text(st.why, "level") == level->id);

        FrameState after = s;
        std::vector<rules::Call> calls;
        std::string error;
        CHECK(rules::decide(after, {st.decision, s.striker}, rig::env().oracle, rig::env().ref, calls, error));
        CHECK(after.over);
        CHECK(after.winner == 1 - s.striker);

        Rng rng(1);
        CHECK(choose_option(fouled, 0, asked, *level, rig::env(), rng) == DecisionKind::concede_frame);
    }
}

TEST_CASE("README The score: with concede_snookers 0 a level never concedes, however hopeless") {
    const FrameState s = rig::position("t3_colours_behind");
    const std::vector<DecisionKind> open = rules::options(s, s.striker);
    REQUIRE(offered(open, DecisionKind::concede_frame));
    const FrameState fouled = after_foul("colours", {6, 60});
    const std::vector<DecisionKind> asked = rules::options(fouled, 0);
    REQUIRE(offered(asked, DecisionKind::concede_frame));
    for (const Level* loaded : tactical()) {
        Level stubborn = *loaded;
        stubborn.concede_snookers = 0;
        CAPTURE(stubborn.id);
        const Stroke st = choose(s, stubborn);
        CHECK_FALSE(st.decides);
        CHECK(text(st.why, "kind") != "concede");
        Rng rng(1);
        CHECK_FALSE(concedes(choose_option(fouled, 0, asked, stubborn, rig::env(), rng)));
    }
}

TEST_CASE("README The score: needing one snooker, only the levels that concede at one do") {
    const FrameState s = rig::position("t3_colours_close");
    const std::vector<DecisionKind> open = rules::options(s, s.striker);
    REQUIRE(offered(open, DecisionKind::concede_frame));
    // Three points: one snooker however counted.
    REQUIRE(rules::penalty_points_required(s, s.striker) == 3);
    const FrameState fouled = after_foul("colours", {6, 40});
    REQUIRE(rules::penalty_points_required(fouled, 0) == 3);
    const std::vector<DecisionKind> options = rules::options(fouled, 0);
    REQUIRE(offered(options, DecisionKind::concede_frame));
    for (const Level* level : tactical()) {
        CAPTURE(level->id);
        const Stroke st = choose(s, *level);
        Rng rng(1);
        const DecisionKind asked = choose_option(fouled, 0, options, *level, rig::env(), rng);
        if (level->concede_snookers == 1) {
            CHECK(st.decides);
            CHECK(st.decision == DecisionKind::concede_frame);
            CHECK(asked == DecisionKind::concede_frame);
        } else {
            CHECK_FALSE(st.decides);
            CHECK_FALSE(concedes(asked));
        }
    }
}

TEST_CASE("spec 10.1, 10.3: one medium pot, its meant stroke played 400 times with each level's error, goes in more often at every step up the ladder") {
    const FrameState s = rig::position("colours");
    const Env env = rig::env();
    const std::vector<Pot> listed = pots(s, env, rig::levels().beginner.max_cut_rad);
    REQUIRE(listed.size() == 1);
    const Pot& pot = listed[0];
    REQUIRE(std::string(band(pot)) == "medium");
    Spec spec;
    spec.speed_m_s = 1.5 * least_pot_speed(pot, env);
    const SolvedPot solved = solve_pot(s, pot, spec, env, false, 60);
    REQUIRE(solved.found);
    const phys::CueStrike meant = solved.trial.strike;

    const rig::Levels& l = rig::levels();
    std::vector<int> potted;
    for (const Level* level : {&l.beginner, &l.club, &l.amateur, &l.pro, &l.world}) {
        int in = 0;
        for (std::uint64_t seed = 1; seed <= 400; ++seed) {
            Rng rng(seed);
            const phys::CueStrike strike = with_error(meant, *level, &pot, env, rng);
            const rules::Judgement j = rules::scenario::play(s, strike, pot.input).judgement;
            if (j.points > 0 && j.fouls.empty()) ++in;
        }
        potted.push_back(in);
    }
    CAPTURE(potted[0]);
    CAPTURE(potted[1]);
    CAPTURE(potted[2]);
    CAPTURE(potted[3]);
    CAPTURE(potted[4]);
    for (std::size_t i = 1; i < potted.size(); ++i) CHECK(potted[i] > potted[i - 1]);
}
