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
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/opponent/tactics.hpp"
#include "ofs/rules/engine.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using nlohmann::json;
using rules::DecisionKind;
using rules::FrameState;

namespace {

constexpr int kSims = 400;
constexpr const char* kCostly = "t3_miss_costly";
// Speeds of the grid a miss's cost is proved over, from the least that pots to the cue's top.
constexpr int kRungs = 12;

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

// README "Worth", the giveaway, recomputed from outside: one side's miss is the planned stroke
// with its aim turned `side` (+1 or -1) by 2, then 4, then 8 margins, the first at which the
// target stays out, read for the other player by `level`, with any foul's points added.
struct Miss {
    int turn = 0;  // margins turned; 0 when it dropped even at 8
    double read = 0.0;
};

Miss miss_side(const FrameState& s, const Pot& pot, const phys::CueStrike& planned, double side, const Level& level) {
    for (int turn : {2, 4, 8}) {
        phys::CueStrike c = planned;
        c.aim_rad += side * turn * pot.margin_rad;
        phys::ShotRecord record;
        const Trial t = try_strike(s, pot.cue, c, rig::env(), &record);
        if (std::find(t.potted.begin(), t.potted.end(), pot.target) != t.potted.end()) continue;
        const rules::Judgement j =
            rules::judge_shot(s, pot.input, record, rig::env().table, rig::env().oracle, rig::env().ref);
        Miss m;
        m.turn = turn;
        // A miss that pots another ball keeps the turn: it leaves the other player nothing.
        if (j.post.striker == s.striker && j.fouls.empty()) return m;
        FrameState after = j.post;
        after.pending = rules::Pending::none;
        m.read = read_position(after, level, rig::env()).worth + (j.fouls.empty() ? 0 : j.penalty);
        return m;
    }
    return {};
}

// The side that turns the cue ball's line towards the object ball's centre: a fuller contact.
double thicker(const FrameState& s, const Pot& pot) {
    const rules::Vec2 o = s.balls[std::size_t(pot.target)].pos;
    const double cross = (pot.ghost.x - pot.cue.x) * (o.y - pot.cue.y) - (pot.ghost.y - pot.cue.y) * (o.x - pot.cue.x);
    return cross >= 0.0 ? 1.0 : -1.0;
}

struct Way {
    Spec spec;
    bool pots = false;
    Miss thick;
    Miss thin;
    double giveaway() const { return 0.5 * (thick.read + thin.read); }
};

// The pot a level reads best; every other pot it lists it reads under safety_below, so it is
// never played while a tactic is found.
Pot tempting(const FrameState& s, const Level& level) {
    const std::vector<Pot> listed = pots(s, rig::env(), level.max_cut_rad);
    REQUIRE_FALSE(listed.empty());
    std::size_t best = 0;
    for (std::size_t i = 1; i < listed.size(); ++i) {
        if (pot_chance(level, listed[i], Spec{}, listed[i].margin_rad) >
            pot_chance(level, listed[best], Spec{}, listed[best].margin_rad)) {
            best = i;
        }
    }
    for (std::size_t i = 0; i < listed.size(); ++i) {
        if (i != best) REQUIRE(pot_chance(level, listed[i], Spec{}, listed[i].margin_rad) < level.safety_below);
    }
    return listed[best];
}

// The ways of striking `pot` within the level's reach: speeds from the least that pots to the
// cue's top in `rungs` steps, each centre ball and with the tip at max_tip and half of it in each
// of the four directions, the elevation as solve_pot gives it. `pots` is true when solve_pot finds
// the aim and the referee calls no foul on it.
std::vector<Way> ways(const FrameState& s, const Pot& pot, const Level& level, int rungs) {
    const Env env = rig::env();
    const double least = least_pot_speed(pot, env);
    const double top = env.params.max_cue_speed_m_s;
    const double t = level.max_tip;
    const double h = 0.5 * t;
    const std::array<std::array<double, 2>, 9> tips{
        {{0.0, 0.0}, {t, 0.0}, {-t, 0.0}, {0.0, t}, {0.0, -t}, {h, 0.0}, {-h, 0.0}, {0.0, h}, {0.0, -h}}};
    const double thick = thicker(s, pot);
    std::vector<Way> out;
    for (int i = 0; i < rungs; ++i) {
        for (const auto& tip : tips) {
            Way w;
            w.spec.speed_m_s = least + (top - least) * double(i) / double(rungs - 1);
            w.spec.tip_x = tip[0];
            w.spec.tip_y = tip[1];
            const SolvedPot solved = solve_pot(s, pot, w.spec, env, false, 60);
            if (solved.found && solved.record) {
                const rules::Judgement j =
                    rules::judge_shot(s, pot.input, *solved.record, env.table, env.oracle, env.ref);
                w.pots = j.fouls.empty();
            }
            if (w.pots) {
                w.thick = miss_side(s, pot, solved.trial.strike, thick, level);
                w.thin = miss_side(s, pot, solved.trial.strike, -thick, level);
            }
            out.push_back(w);
        }
    }
    return out;
}

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

// The six colours of t3_colours_close, yellow on, the striker (0) needing exactly `points`
// penalty points and free to concede at any time, so that conceding is among its options.
FrameState needing(int points) {
    FrameState s = rig::position("t3_colours_close");
    REQUIRE(s.striker == 0);
    s.concede_any_time = {true, true};
    s.scores = {10, 10 + rules::points_remaining(s) + points};
    REQUIRE(rules::penalty_points_required(s, 0) == points);
    return s;
}

// after_foul's choice for player 0, the scores set so that it needs exactly `points`.
FrameState fouled_needing(int points) {
    const int far = 90;
    const int over = rules::penalty_points_required(after_foul("colours", {6, far}), 0);
    REQUIRE(over > points);
    const FrameState s = after_foul("colours", {6, far - (over - points)});
    REQUIRE(rules::penalty_points_required(s, 0) == points);
    return s;
}

}  // namespace

TEST_CASE("README Worth, spec 10.2 step 4: a tempting pot whose every miss leaves the other player in is turned down with miss_weight 1 and played with 0") {
    const FrameState s = rig::position(kCostly);
    Level careful = sharp(rig::levels().amateur);
    careful.miss_weight = 1.0;
    Level reckless = careful;
    reckless.miss_weight = 0.0;
    const Pot pot = tempting(s, careful);
    const double chance = pot_chance(careful, pot, Spec{}, pot.margin_rad);
    CAPTURE(chance);
    REQUIRE(chance >= careful.safety_below);
    REQUIRE(chance < 0.5);
    REQUIRE(tactic_found(s, careful));

    // The claim is over the level's reach, not its list of candidates (which tuning moves): every
    // stroke of the grid that makes the pot gives away a lot on a miss.
    int potting = 0;
    for (const Way& w : ways(s, pot, careful, kRungs)) {
        if (!w.pots) continue;
        ++potting;
        CAPTURE(w.spec.speed_m_s);
        CAPTURE(w.spec.tip_x);
        CAPTURE(w.spec.tip_y);
        CAPTURE(w.thick.read);
        CAPTURE(w.thin.read);
        CHECK(w.giveaway() >= 8.0);
    }
    CHECK(potting >= 20);

    const std::string target = "red " + std::to_string(s.balls[std::size_t(pot.target)].index);
    // The level's own budget: the pot must be weighed after the tactics have had their steps.
    for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2)}) {
        CAPTURE(seed);
        const Stroke plays = choose(s, reckless, seed, Think{});
        const Stroke declines = choose(s, careful, seed, Think{});
        CAPTURE(plays.why.dump());
        CAPTURE(declines.why.dump());
        CHECK(text(plays.why, "kind") == "pot");
        CHECK(text(plays.why, "target") == target);
        CHECK_FALSE(plays.why.contains("giveaway"));
        CHECK(text(declines.why, "kind") != "pot");
    }
}

TEST_CASE("README Worth: a pot whose misses leave nothing is a shot to nothing, played with miss_weight 1, its giveaway small") {
    const FrameState s = rig::position("t3_cut_tempting");
    Level careful = sharp(rig::levels().amateur);
    careful.miss_weight = 1.0;
    const Pot pot = tempting(s, careful);
    REQUIRE(pot_chance(careful, pot, Spec{}, pot.margin_rad) >= careful.safety_below);
    REQUIRE(tactic_found(s, careful));
    for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2)}) {
        CAPTURE(seed);
        const Stroke st = choose(s, careful, seed, Think{});
        CAPTURE(st.why.dump());
        REQUIRE(text(st.why, "kind") == "pot");
        CHECK(text(st.why, "target") == "red 0");
        REQUIRE(st.why.contains("giveaway"));
        REQUIRE(st.why["giveaway"].is_number());
        const double giveaway = st.why["giveaway"].get<double>();
        CHECK(giveaway < 4.0);
        REQUIRE(st.why.contains("miss_turn"));
        REQUIRE(st.why["miss_turn"].is_array());
        REQUIRE(st.why["miss_turn"].size() == 2);

        // The same giveaway recomputed from outside for the stroke it planned (to within 1%: the
        // reading from outside differs from the search's own in the last places).
        const double thick = thicker(s, pot);
        const Miss a = miss_side(s, pot, st.meant, thick, careful);
        const Miss b = miss_side(s, pot, st.meant, -thick, careful);
        CHECK(st.why["miss_turn"][0].get<double>() == double(a.turn));
        CHECK(st.why["miss_turn"][1].get<double>() == double(b.turn));
        CHECK(giveaway == doctest::Approx(0.5 * (a.read + b.read)).epsilon(0.01));
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

TEST_CASE("README The score, spec 10.4: needing snookers past concede_snookers, every new level's copy concedes at its turn and when asked") {
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
    for (const Level* loaded : tactical()) {
        // The files' own values are the tuning's; any from 1 to 4 concedes here.
        Level conceding = *loaded;
        conceding.concede_snookers = 3;
        const Level* level = &conceding;
        CAPTURE(level->id);
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

TEST_CASE("README The score, spec 10.4: a level with concede_snookers n concedes needing n snookers or more and not needing fewer; with 0 never") {
    // A snooker is worth 4 to 7 penalty points, so these need the same number however they are
    // counted (0, 1, 2), or at least 3 either way (15: 3 or 4; 23: 4 or 6).
    const std::array<int, 5> points{0, 4, 8, 15, 23};
    for (const int p : points) {
        CAPTURE(p);
        const int least = (p + 6) / 7;
        const FrameState s = needing(p);
        const std::vector<DecisionKind> open = rules::options(s, s.striker);
        REQUIRE(offered(open, DecisionKind::concede_frame));
        const FrameState fouled = fouled_needing(p);
        const std::vector<DecisionKind> asked = rules::options(fouled, 0);
        REQUIRE(offered(asked, DecisionKind::concede_frame));
        for (const Level* loaded : tactical()) {
            for (int n = 0; n <= 3; ++n) {
                Level level = *loaded;
                level.concede_snookers = n;
                CAPTURE(level.id);
                CAPTURE(n);
                const bool expected = n > 0 && least >= n;
                const Stroke st = choose(s, level, 1, sims(40));
                CHECK(st.decides == expected);
                if (expected) CHECK(st.decision == DecisionKind::concede_frame);
                Rng rng(1);
                CHECK(concedes(choose_option(fouled, 0, asked, level, rig::env(), rng)) == expected);
            }
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
