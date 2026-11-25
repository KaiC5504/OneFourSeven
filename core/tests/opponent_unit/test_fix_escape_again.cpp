// An escape after a miss put back (README "Tactics"): the frame's miss_streak turns the answer, so
// the stroke that missed is not played again, and an escape reaches one cushion further. The
// positions are from logged frames where a level missed the same way many times running.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ofs/opponent/budget.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/opponent/tactics.hpp"
#include "ofs/rules/engine.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using nlohmann::json;
using rules::FrameState;

namespace {

FrameState missed(const char* name, int misses) {
    FrameState s = rig::position(name);
    s.miss_streak = misses;
    return s;
}

Tactic tactic(const FrameState& s, const Level& level) {
    Think think;
    Budget budget(level, think);
    Rng judgement(7);
    return choose_tactic(s, level, rig::env(), budget, judgement);
}

Stroke stroke(const FrameState& s, const Level& level, std::uint64_t seed = 11) {
    Rng rng(seed);
    return choose_stroke(s, level, rig::env(), rng);
}

struct Outcome {
    Trial trial;
    rules::Judgement judgement;
};

// The stroke played exactly as planned.
Outcome as_meant(const FrameState& s, bool places, rules::Vec2 cue, const phys::CueStrike& meant,
                 const rules::ShotInput& input) {
    const Env env = rig::env();
    FrameState f = s;
    std::string error;
    if (places) REQUIRE(rules::place_cue_ball(f, cue, env.oracle, error));
    phys::ShotRecord record;
    Outcome o;
    o.trial = try_strike(f, f.balls[std::size_t(f.cue_ball)].pos, meant, env, &record);
    o.judgement = rules::judge_shot(f, input, record, env.table, env.oracle, env.ref);
    return o;
}

Outcome as_meant(const FrameState& s, const Tactic& t) { return as_meant(s, t.places_cue, t.cue_pos, t.meant, t.input); }
Outcome as_meant(const FrameState& s, const Stroke& st) {
    return as_meant(s, st.places_cue, st.cue_pos, st.meant, st.input);
}

bool same(const phys::CueStrike& a, const phys::CueStrike& b) {
    return a.aim_rad == b.aim_rad && a.speed_m_s == b.speed_m_s && a.tip_x == b.tip_x && a.tip_y == b.tip_y &&
           a.elevation_rad == b.elevation_rad;
}

bool flag(const json& why, const char* key) {
    const auto it = why.find(key);
    return it != why.end() && it->is_boolean() && it->get<bool>();
}

}  // namespace

TEST_CASE("escape again: with no stroke that hits, each attempt after a miss put back is another stroke") {
    const rig::Levels& l = rig::levels();
    for (const Level* level : {&l.amateur, &l.pro}) {
        CAPTURE(level->id);
        std::vector<phys::CueStrike> played;
        for (int misses = 0; misses < 3; ++misses) {
            CAPTURE(misses);
            const FrameState s = missed("t3_escape_ring", misses);
            const Tactic t = tactic(s, *level);
            REQUIRE(t.found);
            CHECK(t.kind == "escape");
            CHECK_FALSE(flag(t.why, "hit"));
            if (!played.empty()) CHECK_FALSE(same(t.meant, played.back()));
            played.push_back(t.meant);
        }
    }
}

TEST_CASE("escape again: the first attempt keeps to escape_cushions, the next may go off one more, two at most") {
    const rig::Levels& l = rig::levels();
    REQUIRE(l.amateur.escape_cushions == 1);
    for (const char* name : {"t3_escape_two", "log_escape_colours"}) {
        CAPTURE(name);
        const FrameState first = missed(name, 0);
        const Tactic one = tactic(first, l.amateur);
        CAPTURE(one.why.dump());
        REQUIRE(one.found);
        CHECK(one.kind == "escape");
        if (flag(one.why, "hit")) CHECK(one.why["cushions"].get<int>() <= 1);

        const FrameState again = missed(name, 1);
        const Tactic two = tactic(again, l.amateur);
        CAPTURE(two.why.dump());
        REQUIRE(two.found);
        CHECK(two.kind == "escape");
        CHECK(flag(two.why, "hit"));
        CHECK(two.why["cushions"].get<int>() <= 2);
        const Outcome o = as_meant(again, two);
        CHECK(o.trial.clean);
        CHECK(o.judgement.fouls.empty());
        CHECK_FALSE(same(two.meant, one.meant));
    }
    // Two cushions are the most, however long the misses run.
    for (const Level* level : {&l.amateur, &l.pro}) {
        const Tactic t = tactic(missed("t3_escape_two", 5), *level);
        REQUIRE(t.found);
        CHECK(t.why["cushions"].get<int>() <= 2);
    }
    // A level that goes off no cushion still leaves its escape to the stand-in.
    REQUIRE(l.club.escape_cushions == 0);
    CHECK_FALSE(tactic(missed("t3_escape_one", 1), l.club).found);
}

TEST_CASE("escape again: with a colour of choice on, an escape declares the colour it is simulated to meet") {
    const FrameState s = missed("log_escape_colours", 1);
    REQUIRE(s.on.kind == rules::OnKind::any_colour);
    const Tactic t = tactic(s, rig::levels().amateur);
    REQUIRE(t.found);
    REQUIRE(t.input.has_nomination);
    const Outcome o = as_meant(s, t);
    REQUIRE(o.trial.first_hit >= 0);
    CHECK(s.balls[std::size_t(o.trial.first_hit)].ball == t.input.nominated);
    CHECK(o.judgement.fouls.empty());
}

TEST_CASE("escape again: the miss the referee calls and puts back reaches the level's next decision") {
    const Env env = rig::env();
    const Level& level = rig::levels().amateur;
    const FrameState s = rig::position("log_escape_colours");
    const Stroke first = stroke(s, level);
    REQUIRE_FALSE(first.decides);
    CHECK(first.why.value("kind", std::string()) == "escape");
    const Outcome o = as_meant(s, first);
    if (o.judgement.fouls.empty()) return;  // it found a way off one cushion: nothing to put back
    REQUIRE(o.judgement.miss);
    FrameState back = o.judgement.post;
    CHECK(back.miss_streak == 1);
    std::vector<rules::Call> calls;
    std::string error;
    REQUIRE(rules::decide(back, {rules::DecisionKind::replace, back.decider}, env.oracle, env.ref, calls, error));
    REQUIRE(back.miss_streak == 1);
    REQUIRE(back.striker == s.striker);

    const Stroke second = stroke(back, level, 12);
    CAPTURE(second.why.dump());
    CHECK(second.why.value("kind", std::string()) == "escape");
    CHECK_FALSE(same(second.meant, first.meant));
    CHECK(flag(second.why, "hit"));
    CHECK(as_meant(back, second).judgement.fouls.empty());
}

TEST_CASE("escape again: a long route off two cushions is found at the pace that brings the cue ball to its ball") {
    const FrameState s = rig::position("log_escape_top_cushion");
    const Level& pro = rig::levels().pro;
    REQUIRE(pro.escape_cushions == 2);
    const Tactic t = tactic(s, pro);
    CAPTURE(t.why.dump());
    REQUIRE(t.found);
    CHECK(t.kind == "escape");
    CHECK(flag(t.why, "hit"));
    CHECK(t.why["cushions"].get<int>() == 2);
    const Outcome o = as_meant(s, t);
    CHECK(o.judgement.fouls.empty());
    CHECK(s.balls[std::size_t(o.trial.first_hit)].ball == rules::Ball::red);
}

TEST_CASE("escape again: an escape that pots the ball it meets is played when no other stroke is legal") {
    const FrameState s = rig::position("log_escape_pots");
    const Level& amateur = rig::levels().amateur;
    const Tactic t = tactic(s, amateur);
    CAPTURE(t.why.dump());
    REQUIRE(t.found);
    CHECK(t.kind == "escape");
    CHECK(flag(t.why, "hit"));
    CHECK(flag(t.why, "keeps_turn"));
    const Outcome o = as_meant(s, t);
    CHECK(o.judgement.fouls.empty());
    CHECK(o.judgement.post.striker == s.striker);

    // A level playing for a maximum pots only the ball it needs: it does not take such a stroke.
    Level waits = rig::levels().maximum;
    waits.max_sims = 600;
    const Tactic m = tactic(s, waits);
    CHECK_FALSE(flag(m.why, "keeps_turn"));
}

TEST_CASE("escape again: the stand-in's escape is what it was with no miss called, and looks harder after one") {
    const Env env = rig::env();
    const rules::scenario::Rig& r = rules::scenario::rig();
    const FrameState s = rig::position("log_escape_brown");
    StandinParams sp = env.standin;
    sp.aim_error_rad = 0.0;
    sp.speed_error = 0.0;

    Rng rng(5);
    const StandinShot first = choose_shot(s, r.table, r.params, env.oracle, env.ref, sp, rng);
    REQUIRE_FALSE(first.decides);
    CHECK(first.why.value("kind", std::string()) == "escape");
    // As logged: straight at the blue, the cheapest ball in the way, at the safety's pace.
    CHECK(first.strike.aim_rad == doctest::Approx(3.0172379443851973).epsilon(1e-12));
    CHECK(first.strike.speed_m_s == 1.6);
    CHECK(first.why["penalty"].get<int>() == 5);
    CHECK(first.why["sims"].get<int>() <= sp.max_sims);

    const FrameState again = missed("log_escape_brown", 1);
    Rng rng_again(5);
    const StandinShot second = choose_shot(again, r.table, r.params, env.oracle, env.ref, sp, rng_again);
    REQUIRE_FALSE(second.decides);
    CAPTURE(second.why.dump());
    CHECK_FALSE(same(second.strike, first.strike));
    CHECK(second.why["penalty"].get<int>() == 0);
    const Outcome o = as_meant(again, false, {}, second.strike, second.input);
    CHECK(o.judgement.fouls.empty());
    CHECK(again.balls[std::size_t(o.trial.first_hit)].ball == rules::Ball::brown);

    // Club goes off no cushion: its escape is the stand-in's, before a miss and after.
    const Level& club = rig::levels().club;
    const Stroke one = stroke(s, club);
    const Stroke two = stroke(again, club);
    CHECK(one.why.value("kind", std::string()) == "escape");
    CHECK(one.meant.aim_rad == doctest::Approx(first.strike.aim_rad).epsilon(1e-12));
    CHECK_FALSE(same(two.meant, one.meant));
    CHECK(as_meant(again, two).judgement.fouls.empty());
}

TEST_CASE("escape again: when every stroke the stand-in tries fouls, a miss put back takes the next that costs no more") {
    const Env env = rig::env();
    const rules::scenario::Rig& r = rules::scenario::rig();
    StandinParams sp = env.standin;
    sp.aim_error_rad = 0.0;
    sp.speed_error = 0.0;
    std::vector<phys::CueStrike> played;
    std::vector<int> penalties;
    for (int misses = 0; misses < 3; ++misses) {
        const FrameState s = missed("t3_escape_ring", misses);
        Rng rng(5);
        const StandinShot shot = choose_shot(s, r.table, r.params, env.oracle, env.ref, sp, rng);
        REQUIRE_FALSE(shot.decides);
        played.push_back(shot.strike);
        penalties.push_back(shot.why["penalty"].get<int>());
    }
    CAPTURE(penalties);
    // No stroke hits the red inside the ring: each costs a foul, and none more than the first did.
    REQUIRE(penalties[0] > 0);
    CHECK(penalties[1] <= penalties[0]);
    CHECK(penalties[2] <= penalties[0]);
}
