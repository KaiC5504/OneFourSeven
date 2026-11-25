// A level with tactics plays the stand-in's stroke only when it found nothing legal, and then says
// so; the break-off is only from the pack; and one line of opponent.decision carries what the
// choice turned on (README "Stage 2": "Worth", "Tactics").

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ofs/opponent/budget.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/opponent/standin.hpp"
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
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

Tactic tactic(const FrameState& s, const Level& level) {
    Think think;
    Budget budget(level, think);
    Rng judgement(7);
    return choose_tactic(s, level, rig::env(), budget, judgement);
}

rules::Judgement as_meant(const FrameState& s, bool places, rules::Vec2 cue, const phys::CueStrike& meant,
                          const rules::ShotInput& input) {
    const Env env = rig::env();
    FrameState f = s;
    std::string error;
    if (places) REQUIRE(rules::place_cue_ball(f, cue, env.oracle, error));
    phys::ShotRecord record;
    const Trial trial = try_strike(f, f.balls[std::size_t(f.cue_ball)].pos, meant, env, &record);
    REQUIRE(trial.clean);
    return rules::judge_shot(f, input, record, env.table, env.oracle, env.ref);
}

// The first exhibition table: fifteen reds, open, nowhere near a pack.
FrameState open_table(bool in_hand) {
    FrameState s = exhibition_frame(rig::levels().exhibitions.front(), 0, 1);
    if (in_hand) {
        s.cue_in_hand = true;
        s.balls[std::size_t(s.cue_ball)].pos = {table::baulk_line_x() - 0.05, 0.1};
    }
    return s;
}

double value_of(const std::string& target) {
    if (target.rfind("red", 0) == 0) return 1.0;
    for (const rules::Ball b : {rules::Ball::yellow, rules::Ball::green, rules::Ball::brown, rules::Ball::blue,
                                rules::Ball::pink, rules::Ball::black}) {
        if (table::ball_name(b) == target) return double(table::ball_value(b));
    }
    return 0.0;
}

// A level with fewer simulations, so the Debug suite stays quick.
Level quick(const Level& from, int sims) {
    Level l = from;
    l.max_sims = std::min(l.max_sims, sims);
    return l;
}

}  // namespace

TEST_CASE("break-off: with the cue ball in hand and fifteen reds split, a level pots; from the pack it breaks") {
    const rig::Levels& l = rig::levels();
    const FrameState split = open_table(true);
    REQUIRE(split.on.kind == rules::OnKind::red);
    REQUIRE_FALSE(pack_intact(split));
    const FrameState racked = rig::racked(1);
    REQUIRE(pack_intact(racked));
    for (const Level& level : {quick(l.club, 800), quick(l.world, 1500), quick(l.maximum, 1500)}) {
        CAPTURE(level.id);
        Rng rng(42);
        const Stroke st = choose_stroke(split, level, rig::env(), rng);
        CAPTURE(st.why.dump());
        CHECK(text(st.why, "kind") == "pot");
        CHECK(st.places_cue);

        Rng again(42);
        const Stroke opening = choose_stroke(racked, level, rig::env(), again);
        CHECK(text(opening.why, "kind") == "break");

        Budget budget(level, Think{});
        Rng judgement(7);
        CHECK(choose_tactic(split, level, rig::env(), budget, judgement).kind != "break");
    }
    // One red gone and the rest still together is no pack either.
    FrameState fourteen = racked;
    const auto red = std::find_if(fourteen.balls.begin(), fourteen.balls.end(),
                                  [](const rules::BallOnTable& b) { return b.ball == rules::Ball::red; });
    REQUIRE(red != fourteen.balls.end());
    fourteen.balls.erase(red);
    CHECK_FALSE(pack_intact(fourteen));
}

TEST_CASE("tactic found: a ball on seen only between two others is played at fuller than half ball") {
    const FrameState s = rig::position("log_safety_between");
    const rig::Levels& l = rig::levels();
    for (const Level* level : {&l.club, &l.amateur}) {
        CAPTURE(level->id);
        const Tactic t = tactic(s, *level);
        CAPTURE(t.why.dump());
        REQUIRE(t.found);
        CHECK(t.kind == "safety");
        const rules::Judgement j = as_meant(s, t.places_cue, t.cue_pos, t.meant, t.input);
        CHECK(j.fouls.empty());
        CHECK(j.post.striker != s.striker);

        Rng rng(3);
        const Stroke st = choose_stroke(s, *level, rig::env(), rng);
        CHECK(st.why.contains("gain"));
        CHECK_FALSE(st.why.contains("tactic"));
    }
}

TEST_CASE("tactic found: with a ball behind the ball on that any firm contact pots, it is rolled up to") {
    const FrameState s = rig::position("log_safety_black_behind");
    const Tactic t = tactic(s, rig::levels().club);
    CAPTURE(t.why.dump());
    REQUIRE(t.found);
    CHECK(t.kind == "safety");
    const rules::Judgement j = as_meant(s, t.places_cue, t.cue_pos, t.meant, t.input);
    CHECK(j.fouls.empty());
    CHECK(j.post.striker != s.striker);
}

TEST_CASE("tactic found: a level that plays the stand-in's stroke says the tactic found nothing and what it tried") {
    const Level& club = rig::levels().club;
    REQUIRE(club.tactics > 0);
    // Snookered, and Club goes off no cushion: the stand-in's escape.
    const FrameState s = rig::position("t3_escape_one");
    Rng rng(3);
    const Stroke st = choose_stroke(s, club, rig::env(), rng);
    CAPTURE(st.why.dump());
    CHECK(text(st.why, "kind") == "escape");
    CHECK_FALSE(st.why.contains("gain"));
    REQUIRE(st.why.contains("tactic"));
    const json& found = st.why["tactic"];
    CHECK(found["found"] == false);
    CHECK(found["candidates"] == 0);
    CHECK(found["stopped"] == "done");
    CHECK(found["direct"] == false);

    // Stopped by the steps it was allowed, it says that.
    Level one_try = rig::levels().pro;
    one_try.safety_tries = 1;
    one_try.max_tip = 0.0;
    const Tactic t = tactic(rig::position("t3_escape_ring"), one_try);
    if (!t.found) CHECK(t.why["stopped"] == "tries");

    // Beginner has no tactics: its line is as it was.
    Rng other(3);
    CHECK_FALSE(choose_stroke(s, rig::levels().beginner, rig::env(), other).why.contains("tactic"));
}

TEST_CASE("decision line: a tactic played with a pot to be had says the pot's chance, its score and the floor") {
    const rig::Levels& l = rig::levels();
    int seen = 0;
    for (const char* name : {"t3_cut_tempting", "t3_long_ahead", "t3_colours_ahead", "t3_cushion_tight", "long_red",
                             "cut_red", "t3_miss_costly"}) {
        for (const Level* from : {&l.club, &l.amateur, &l.pro}) {
            const Level level = quick(*from, 900);
            const FrameState s = rig::position(name);
            Rng rng(21);
            const Stroke st = choose_stroke(s, level, rig::env(), rng);
            if (text(st.why, "kind") == "pot" || !st.why.contains("declined")) continue;
            CAPTURE(name);
            CAPTURE(level.id);
            CAPTURE(st.why.dump());
            ++seen;
            REQUIRE(st.why.contains("declined_worth"));
            REQUIRE(st.why.contains("floor"));
            CHECK(st.why["floor"].get<double>() == pot_floor(s, level));
            const double chance = st.why["declined"].get<double>();
            const double score = st.why["declined_worth"].get<double>();
            // Why the pot was not played is in the line: its chance under the floor, or its score
            // no better than the tactic's worth.
            CHECK((chance < st.why["floor"].get<double>() || score <= st.why["worth"].get<double>()));
        }
    }
    CHECK(seen > 0);
}

TEST_CASE("decision line: a pot says what its leave was read as worth, and a planned pot what its pace and spin cost") {
    const rig::Levels& l = rig::levels();
    for (const char* name : {"straight_red", "after_red", "colours"}) {
        for (const Level* from : {&l.club, &l.amateur, &l.world}) {
            // Nothing but the pot's own terms in the worth.
            Level level = quick(*from, 900);
            level.tactics = 0;
            level.judgement_sigma = 0.0;
            level.miss_weight = 0.0;
            CAPTURE(name);
            CAPTURE(level.id);
            const FrameState s = rig::position(name);
            Rng rng(8);
            const Stroke st = choose_stroke(s, level, rig::env(), rng);
            CAPTURE(st.why.dump());
            REQUIRE(text(st.why, "kind") == "pot");
            REQUIRE(st.why["leave"].contains("worth"));
            const double leave = st.why["leave"]["worth"].get<double>();
            CHECK(leave >= 0.0);
            const double value = value_of(text(st.why, "target"));
            const double w = level.position_weight;
            const double chance = st.why["chance"].get<double>();
            double cost = 0.0;
            if (level.depth > 0) {
                REQUIRE(st.why.contains("cost"));
                cost = st.why["cost"].get<double>();
                CHECK(cost >= 0.0);
            } else {
                CHECK_FALSE(st.why.contains("cost"));
            }
            const double expected = chance * (value + kBreakOn * (1.0 - w) + w * std::max(0.0, leave - cost));
            CHECK(st.why["worth"].get<double>() == doctest::Approx(expected).epsilon(1e-9));
        }
    }
}
