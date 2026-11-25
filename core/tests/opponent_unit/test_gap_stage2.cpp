// README clauses of "Stage 2" that no other test held.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/budget.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/level.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/opponent/tactics.hpp"
#include "ofs/rules/engine.hpp"

#include "opponent/choice.hpp"
#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using nlohmann::json;
using rules::FrameState;

namespace {

std::string text(const json& j, const char* key) {
    if (!j.is_object()) return "";
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

// A planning level with nothing else moving the worth: no misreading, no miss cost, no noisy
// strokes, no tactics.
Level plain_planner(double w) {
    Level l = rig::levels().world;
    l.judgement_sigma = 0.0;
    l.miss_weight = 0.0;
    l.noisy_sims = 0;
    l.tactics = 0;
    l.position_weight = w;
    l.max_sims = 1500;
    return l;
}

struct Outcome {
    Trial trial;
    rules::Judgement judgement;
};

Outcome as_meant(const FrameState& s, bool places, rules::Vec2 cue, const phys::CueStrike& meant,
                 const rules::ShotInput& input) {
    FrameState f = s;
    std::string error;
    if (places) REQUIRE(rules::place_cue_ball(f, cue, rig::env().oracle, error));
    phys::ShotRecord record;
    Outcome o;
    o.trial = try_strike(f, f.balls[std::size_t(f.cue_ball)].pos, meant, rig::env(), &record);
    o.judgement = rules::judge_shot(f, input, record, rig::env().table, rig::env().oracle, rig::env().ref);
    return o;
}

}  // namespace

TEST_CASE("gap, README Worth: with equal leaves (the last black ends the frame) a planning level plays the least pace, centre ball") {
    const FrameState s = rig::position("black_only");
    for (const double w : {0.5, 1.0}) {
        CAPTURE(w);
        const Level level = plain_planner(w);
        REQUIRE(level.depth >= 1);
        Rng rng(3);
        const Stroke st = choose_stroke(s, level, rig::env(), rng);
        CAPTURE(st.why.dump());
        REQUIRE(text(st.why, "kind") == "pot");
        CHECK(st.meant.tip_x == 0.0);
        CHECK(st.meant.tip_y == 0.0);
        const double chance = st.why["chance"].get<double>();
        // Every leave is 0, so the cost is all that tells the strokes apart: the one charged nothing.
        CHECK(st.why["worth"].get<double>() == doctest::Approx(chance * (7.0 + kBreakOn * (1.0 - w))).epsilon(1e-12));
        const int pocket = st.why["pocket"].get<int>();
        for (const Pot& p : pots(s, rig::env(), level.max_cut_rad)) {
            if (p.pocket != pocket) continue;
            CHECK(st.meant.speed_m_s == doctest::Approx(least_pot_speed(p, rig::env())).epsilon(1e-9));
        }
    }
}

TEST_CASE("gap, README Worth: the pace-and-spin charge comes off the leave only; with position_weight 0 a pot is worth its chance times its value and kBreakOn") {
    for (const char* name : {"straight_red", "after_red", "black_only"}) {
        CAPTURE(name);
        const FrameState s = rig::position(name);
        const Level level = plain_planner(0.0);
        Rng rng(5);
        const Stroke st = choose_stroke(s, level, rig::env(), rng);
        CAPTURE(st.why.dump());
        REQUIRE(text(st.why, "kind") == "pot");
        const std::string target = text(st.why, "target");
        const double value = target.rfind("red", 0) == 0 ? 1.0
                             : target == "black"         ? 7.0
                             : target == "pink"          ? 6.0
                             : target == "blue"          ? 5.0
                             : target == "brown"         ? 4.0
                             : target == "green"         ? 3.0
                                                         : 2.0;
        CHECK(st.why["worth"].get<double>() ==
              doctest::Approx(st.why["chance"].get<double>() * (value + kBreakOn)).epsilon(1e-12));
    }
}

TEST_CASE("gap, README Worth: a pot with nothing to follow is not marked down for its pace or spin") {
    // One speed, well up the ladder: charged in full, the last black was worth less by the weight
    // times that pace's cost.
    const FrameState s = rig::position("black_only");
    for (const double w : {0.5, 1.0}) {
        CAPTURE(w);
        Level level = plain_planner(w);
        level.speeds = 1;
        level.spins = 1;
        Rng rng(3);
        const Stroke st = choose_stroke(s, level, rig::env(), rng);
        CAPTURE(st.why.dump());
        REQUIRE(text(st.why, "kind") == "pot");
        REQUIRE(st.why["cost"].get<double>() > 0.0);
        CHECK(st.why["leave"]["worth"].get<double>() == 0.0);
        CHECK(st.why["worth"].get<double>() ==
              doctest::Approx(st.why["chance"].get<double>() * (7.0 + kBreakOn * (1.0 - w))).epsilon(1e-12));
    }
}

TEST_CASE("gap, README Worth: a planned pot is never worth less than its chance times its own points") {
    // A leave whose pots all cost more than they bring counts 0, as a leave with no pot does.
    for (const char* name : {"straight_red", "after_red", "colours", "cut_red", "last_red", "long_red"}) {
        for (const double w : {0.5, 1.0}) {
            CAPTURE(name);
            CAPTURE(w);
            Level level = plain_planner(w);
            level.max_sims = 900;
            const FrameState s = rig::position(name);
            Rng rng(5);
            const Stroke st = choose_stroke(s, level, rig::env(), rng);
            CAPTURE(st.why.dump());
            if (text(st.why, "kind") != "pot") continue;
            const std::string target = text(st.why, "target");
            const double value = target.rfind("red", 0) == 0 ? 1.0
                                 : target == "black"         ? 7.0
                                 : target == "pink"          ? 6.0
                                 : target == "blue"          ? 5.0
                                 : target == "brown"         ? 4.0
                                 : target == "green"         ? 3.0
                                                             : 2.0;
            const double own = st.why["chance"].get<double>() * (value + kBreakOn * (1.0 - w));
            CHECK(st.why["worth"].get<double>() >= own * (1.0 - 1e-12));
            CHECK(st.why["leave"]["worth"].get<double>() >= 0.0);
        }
    }
}

TEST_CASE("gap, README Tactics: snookered behind a colour with a free ball, every level with tactics plays a legal stroke and nominates") {
    FrameState s = rig::position("t3_escape_one");
    s.free_ball = true;
    REQUIRE(rig::env().oracle.snookered(s.balls, s.cue_ball, rules::places_on(s), false));
    for (const Level* level : {&rig::levels().club, &rig::levels().amateur, &rig::levels().pro, &rig::levels().world}) {
        CAPTURE(level->id);
        Think think;
        think.max_sims = 600;
        Budget budget(*level, think);
        Rng judgement(1);
        const Tactic t = choose_tactic(s, *level, rig::env(), budget, judgement);
        CAPTURE(t.why.dump());
        REQUIRE(t.found);
        CHECK(t.kind != "escape");  // the free ball can be hit directly
        CHECK(t.input.has_nomination);
        const Outcome o = as_meant(s, t.places_cue, t.cue_pos, t.meant, t.input);
        CHECK(o.trial.clean);
        CHECK(o.judgement.fouls.empty());

        Rng rng(7);
        const Stroke st = choose_stroke(s, *level, rig::env(), rng, think);
        CAPTURE(st.why.dump());
        CHECK(st.fallback.empty());
        CHECK(st.input.has_nomination);
        const Outcome m = as_meant(s, st.places_cue, st.cue_pos, st.meant, st.input);
        CHECK(m.judgement.fouls.empty());
    }
}

TEST_CASE("gap, README Tactics: after a foul and a miss put back, the offender's escape from the same snooker still hits a ball on") {
    FrameState s = rig::position("t3_escape_one");
    // Straight back into the brown behind the cue ball: a foul, and a miss.
    const rules::scenario::Played p =
        rules::scenario::play(s, rules::scenario::strike(3.141592653589793, 0.6), {}, 1);
    CAPTURE(rules::judgement_json(p.judgement).dump());
    REQUIRE_FALSE(p.judgement.fouls.empty());
    FrameState after = p.judgement.post;
    REQUIRE(after.pending == rules::Pending::after_foul);
    const std::vector<rules::DecisionKind> open = rules::options(after, after.decider);
    REQUIRE(std::find(open.begin(), open.end(), rules::DecisionKind::replace) != open.end());
    std::vector<rules::Call> calls;
    std::string error;
    REQUIRE(rules::decide(after, {rules::DecisionKind::replace, after.decider}, rig::env().oracle, rig::env().ref,
                          calls, error));
    REQUIRE(after.striker == 0);
    REQUIRE(after.pending == rules::Pending::none);
    for (const Level* level : {&rig::levels().amateur, &rig::levels().pro, &rig::levels().world}) {
        CAPTURE(level->id);
        Think think;
        think.max_sims = 600;
        Budget budget(*level, think);
        Rng judgement(1);
        const Tactic t = choose_tactic(after, *level, rig::env(), budget, judgement);
        CAPTURE(t.why.dump());
        REQUIRE(t.found);
        CHECK(t.kind == "escape");
        CHECK(t.why.value("hit", false));
        const Outcome o = as_meant(after, t.places_cue, t.cue_pos, t.meant, t.input);
        CHECK(o.judgement.fouls.empty());
        Rng rng(9);
        const Stroke st = choose_stroke(after, *level, rig::env(), rng, think);
        CHECK(text(st.why, "kind") == "escape");
        CHECK(st.fallback.empty());
    }
}

TEST_CASE("gap, README The order: the tactic takes at most safety_tries steps, the pots the rest; cut at the tactic's steps no pot is made") {
    for (const Level* from : {&rig::levels().club, &rig::levels().amateur, &rig::levels().pro, &rig::levels().world}) {
        for (const char* name : {"straight_red", "after_red"}) {
            for (const int tries : {from->safety_tries, 5}) {
                Level level = *from;
                level.safety_tries = tries;
                CAPTURE(level.id);
                CAPTURE(name);
                CAPTURE(tries);
                const FrameState s = rig::position(name);
                Think think;
                think.max_sims = 900;
                Budget budget(level, think);
                Rng judgement(stream_seed(Rng(4), 0));
                const Tactic t = choose_tactic(s, level, rig::env(), budget, judgement);
                REQUIRE(t.found);
                const int tactic_steps = budget.steps;
                CHECK(tactic_steps <= tries);

                Rng whole_rng(4);
                const Stroke whole = choose_stroke(s, level, rig::env(), whole_rng, think);
                CHECK(whole.steps > tactic_steps);

                Think cut = think;
                cut.max_steps = tactic_steps;
                Rng cut_rng(4);
                const Stroke part = choose_stroke(s, level, rig::env(), cut_rng, cut);
                CAPTURE(part.why.dump());
                CHECK(part.steps == tactic_steps);
                CHECK(part.stopped == "steps");
                CHECK(text(part.why, "kind") != "pot");
                CHECK(part.why.value("pots", -1) == 0);
                CHECK(part.why.value("worth", 1e9) == doctest::Approx(t.worth));
            }
        }
    }
}

TEST_CASE("gap, level.cpp: stage 2's values load at either end of their range and are refused past it, by name") {
    struct Edge {
        const char* key;
        double low;
        double high;
        bool integer;
    };
    const Edge edges[] = {{"tactics", 0, 3, true},          {"safety_tries", 0, 1000, true}, {"escape_cushions", 0, 2, true},
                          {"miss_weight", 0.0, 1.0, false}, {"noisy_sims", 0, 64, true},     {"score_aware", 0.0, 1.0, false},
                          {"concede_snookers", 0, 10, true}, {"max_tip", 0.0, 0.53, false}};
    const json base = rules::scenario::data_file("opponent_world.json");
    for (const Edge& e : edges) {
        CAPTURE(e.key);
        for (const double v : {e.low, e.high}) {
            json f = base;
            f["values"][e.key] = e.integer ? json(int(v)) : json(v);
            Level out;
            std::vector<std::string> problems;
            CHECK(load_level(f, out, problems));
        }
        for (const double v : {e.integer ? e.low - 1 : e.low - 1e-9, e.integer ? e.high + 1 : e.high + 1e-9}) {
            json f = base;
            f["values"][e.key] = e.integer ? json(int(v)) : json(v);
            Level out;
            out.id = "untouched";
            std::vector<std::string> problems;
            CHECK_FALSE(load_level(f, out, problems));
            CHECK(out.id == "untouched");
            REQUIRE(problems.size() == 1);
            CHECK(problems[0].rfind(std::string(e.key) + ":", 0) == 0);
        }
    }
    for (const char* file : {"opponent_beginner.json", "opponent_club.json", "opponent_amateur.json", "opponent_pro.json",
                             "opponent_world.json", "opponent_maximum.json", "opponent_person.json"}) {
        CAPTURE(file);
        Level out;
        std::vector<std::string> problems;
        CHECK(load_level(rules::scenario::data_file(file), out, problems));
        CHECK(problems.empty());
    }
}
