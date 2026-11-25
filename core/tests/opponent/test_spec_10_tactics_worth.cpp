// What a tactic is worth and what it may spend (docs/opponent/README.md "Worth", "The score",
// "Tactics"; budget.hpp): its worth recomputed from the position it leaves, the score's two
// formulas, the steps it takes and the same stroke again from them.

#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ofs/opponent/budget.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/opponent/tactics.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using nlohmann::json;
using rules::FrameState;

namespace {

constexpr int kSims = 400;

const Env& env() {
    static const PocketMouths mouths(rules::scenario::rig().table);
    static const Env e = [] {
        Env x = rig::env();
        x.mouths = &mouths;
        return x;
    }();
    return e;
}

std::vector<const Level*> tactical() {
    const rig::Levels& l = rig::levels();
    return {&l.club, &l.amateur, &l.pro, &l.world};
}

Level sharp(const Level& level) {
    Level l = level;
    l.judgement_sigma = 0.0;
    return l;
}

Think sims(int n) {
    Think t;
    t.max_sims = n;
    return t;
}

FrameState pack() {
    FrameState s = rig::racked(1);
    s.cue_in_hand = false;
    return s;
}

FrameState named(const std::string& name) {
    if (name == "racked") return rig::racked(1);
    if (name == "pack") return pack();
    return rig::position(name.c_str());
}

struct Run {
    Tactic t;
    int steps = 0;
    int sims = 0;
    std::string stopped;
};

Run run(const FrameState& s, const Level& level, const Think& think, std::uint64_t seed = 1) {
    Budget budget(level, think);
    Rng judgement(seed);
    Run r;
    r.t = choose_tactic(s, level, env(), budget, judgement);
    r.steps = budget.steps;
    r.sims = budget.sims;
    r.stopped = budget.stopped;
    return r;
}

bool same_strike(const phys::CueStrike& a, const phys::CueStrike& b) {
    return a.aim_rad == b.aim_rad && a.speed_m_s == b.speed_m_s && a.tip_x == b.tip_x && a.tip_y == b.tip_y &&
           a.elevation_rad == b.elevation_rad;
}

bool same_tactic(const Tactic& a, const Tactic& b) {
    return a.found == b.found && a.kind == b.kind && same_strike(a.meant, b.meant) && a.places_cue == b.places_cue &&
           a.cue_pos.x == b.cue_pos.x && a.cue_pos.y == b.cue_pos.y &&
           a.input.has_nomination == b.input.has_nomination && a.input.nominated == b.input.nominated &&
           a.worth == b.worth;
}

// The position the tactic leaves, played as meant and judged.
bool left(const FrameState& s, const Tactic& t, rules::Judgement& out) {
    FrameState f = s;
    std::string error;
    if (t.places_cue && !rules::place_cue_ball(f, t.cue_pos, env().oracle, error)) return false;
    phys::ShotRecord record;
    try_strike(f, f.balls[std::size_t(f.cue_ball)].pos, t.meant, env(), &record);
    out = rules::judge_shot(f, t.input, record, env().table, env().oracle, env().ref);
    return true;
}

double number(const json& j, const char* key) {
    REQUIRE(j.is_object());
    REQUIRE(j.contains(key));
    REQUIRE(j[key].is_number());
    return j[key].get<double>();
}

// README "The score": gain - leave - risk, unless the striker is ahead by more than the points
// left or needs snookers.
double expected_worth(const FrameState& s, double a, double gain, double leave, double risk) {
    const int lead = s.scores[std::size_t(s.striker)] - s.scores[std::size_t(1 - s.striker)];
    const int remaining = rules::points_remaining(s);
    if (lead > remaining) return (1.0 - a) * gain - leave - (1.0 + a) * risk;
    if (-lead > remaining) return (1.0 + a) * gain - (1.0 - a) * leave - risk;
    return gain - leave - risk;
}

// Checks the chosen tactic's worth against the position it leaves; false when the turn did not
// pass to the other player (nothing to read for them).
bool check_worth(const FrameState& s, const Level& level, const Tactic& t) {
    CAPTURE(t.kind);
    CAPTURE(t.why.dump());
    rules::Judgement j;
    REQUIRE(left(s, t, j));
    if (j.post.over || j.post.pending != rules::Pending::none || j.post.striker == s.striker) return false;
    const double leave = read_position(j.post, level, env()).worth;
    const double gain = number(t.why, "gain");
    const double risk = number(t.why, "risk");
    const double want = expected_worth(s, level.score_aware, gain, leave, risk);
    CAPTURE(gain);
    CAPTURE(leave);
    CAPTURE(risk);
    CHECK(std::fabs(t.worth - want) <= 1e-9);
    return true;
}

}  // namespace

TEST_CASE("README Tactics: the others it lists are no better than the one it chose, as it misread them") {
    for (const Level* level : tactical()) {
        for (const std::string name : {"pack", "long_red", "colours", "t3_snooker_pink", "t3_escape_one"}) {
            for (std::uint64_t seed : {std::uint64_t(1), std::uint64_t(2)}) {
                CAPTURE(level->id);
                CAPTURE(name);
                CAPTURE(seed);
                const FrameState s = named(name);
                const Run r = run(s, *level, sims(kSims), seed);
                if (name == "t3_escape_one" && level->escape_cushions == 0) {
                    CHECK_FALSE(r.t.found);
                    continue;
                }
                REQUIRE(r.t.found);
                REQUIRE(r.t.why.contains("others"));
                const json& others = r.t.why["others"];
                REQUIRE(others.is_array());
                CHECK(others.size() <= 3);
                for (const json& o : others) CHECK(number(o, "worth") <= r.t.worth + 1e-12);
                CHECK(number(r.t.why, "worth") == doctest::Approx(r.t.worth).epsilon(1e-12));
                CHECK(r.t.candidates >= 1 + int(others.size()));
            }
        }
    }
}

TEST_CASE("README Worth: with judgement_sigma 0 the chosen tactic's worth is gain - leave - risk, the leave read for the other player by the level") {
    int checked = 0;
    for (const Level* loaded : tactical()) {
        const Level level = sharp(*loaded);
        for (const std::string name : {"pack", "long_red", "t3_cut_tempting", "t3_snooker_pink", "colours"}) {
            CAPTURE(level.id);
            CAPTURE(name);
            const FrameState s = named(name);
            const int lead = s.scores[0] - s.scores[1];
            REQUIRE(std::abs(lead) <= rules::points_remaining(s));
            const Run r = run(s, level, sims(kSims));
            REQUIRE(r.t.found);
            if (check_worth(s, level, r.t)) ++checked;
        }
    }
    CHECK(checked >= 12);
}

TEST_CASE("README The score: ahead by more than the points left, a tactic is worth (1 - a) gain - leave - (1 + a) risk") {
    const FrameState s = named("t3_colours_ahead");
    REQUIRE(s.scores[0] - s.scores[1] > rules::points_remaining(s));
    int checked = 0;
    for (const Level* loaded : {&rig::levels().amateur, &rig::levels().pro, &rig::levels().world}) {
        const Level level = sharp(*loaded);
        CAPTURE(level.id);
        REQUIRE(level.score_aware > 0.0);
        const Run r = run(s, level, sims(kSims));
        REQUIRE(r.t.found);
        if (check_worth(s, level, r.t)) ++checked;
    }
    CHECK(checked >= 2);
}

TEST_CASE("README The score: needing snookers, a tactic is worth (1 + a) gain - (1 - a) leave - risk") {
    const FrameState s = named("t3_colours_behind");
    REQUIRE(s.scores[1] - s.scores[0] > rules::points_remaining(s));
    int checked = 0;
    for (const Level* loaded : {&rig::levels().amateur, &rig::levels().pro, &rig::levels().world}) {
        const Level level = sharp(*loaded);
        CAPTURE(level.id);
        const Run r = run(s, level, sims(kSims));
        REQUIRE(r.t.found);
        if (check_worth(s, level, r.t)) ++checked;
    }
    CHECK(checked >= 2);
}

TEST_CASE("README Tactics: it weighs no more than safety_tries strokes, one step each") {
    for (const Level* level : tactical()) {
        for (const std::string name : {"pack", "long_red", "racked"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            const Run r = run(named(name), *level, Think{});
            REQUIRE(r.t.found);
            CHECK(r.steps >= 1);
            CHECK(r.steps <= level->safety_tries);
            CHECK(r.t.candidates <= level->safety_tries);
            CHECK(r.t.candidates <= r.steps);
        }
    }
}

TEST_CASE("budget.hpp, README Budget: max_steps stops it with the best so far, and the steps it took give the same stroke again") {
    for (const Level* level : {&rig::levels().club, &rig::levels().amateur, &rig::levels().world}) {
        for (const std::string name : {"pack", "long_red", "racked"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            const FrameState s = named(name);
            const Think base = sims(kSims);
            const Run whole = run(s, *level, base);
            REQUIRE(whole.t.found);
            REQUIRE(whole.steps > 3);

            Think cut = base;
            cut.max_steps = 3;
            const Run part = run(s, *level, cut);
            CHECK(part.steps == 3);
            CHECK(part.stopped == "steps");
            REQUIRE(part.t.found);
            CHECK(part.t.candidates <= 3);
            CHECK(same_tactic(part.t, run(s, *level, cut).t));

            Think again = base;
            again.max_steps = whole.steps;
            const Run replayed = run(s, *level, again);
            CHECK(replayed.steps == whole.steps);
            CHECK(same_tactic(whole.t, replayed.t));
            CHECK(replayed.t.why.dump() == whole.t.why.dump());
        }
    }
}

TEST_CASE("tactics.hpp: the same frame, level, stream and budget give the same tactic") {
    for (const Level* level : tactical()) {
        for (const std::string name : {"pack", "t3_snooker_pink", "snookered"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            const FrameState s = named(name);
            const Run a = run(s, *level, sims(kSims), 5);
            const Run b = run(s, *level, sims(kSims), 5);
            if (name != "snookered" || level->escape_cushions > 0) REQUIRE(a.t.found);
            CHECK(same_tactic(a.t, b.t));
            CHECK(a.steps == b.steps);
            CHECK(a.sims == b.sims);
            CHECK(a.t.why.dump() == b.t.why.dump());
        }
    }
}

TEST_CASE("README Tactics: a budget cancelled before it starts finds nothing where an open one finds a stroke") {
    const std::atomic<bool> cancel{true};
    for (const Level* level : tactical()) {
        for (const std::string name : {"pack", "racked"}) {
            CAPTURE(level->id);
            CAPTURE(name);
            REQUIRE(run(named(name), *level, sims(kSims)).t.found);
            Think t;
            t.cancel = &cancel;
            const Run r = run(named(name), *level, t);
            CHECK_FALSE(r.t.found);
            CHECK(r.steps == 0);
        }
    }
}
