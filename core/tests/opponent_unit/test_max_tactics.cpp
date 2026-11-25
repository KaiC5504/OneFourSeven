// The 147's strokes that are not pots are the tactics' (README "The 147", "Stage 2": "The
// order"): its waiting safety, its escapes and its break-off, played with no error.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/standin.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::BallOnTable;
using rules::FrameState;
using rules::OnKind;
using rules::scenario::ball;
using rules::scenario::spotted;

namespace {

const Level& maximum() { return rig::levels().maximum; }

// Debug builds simulate slowly: the 147 gets a part of its budget, its tactics all of theirs.
Think sims(int n = 800) {
    Think t;
    t.max_sims = n;
    return t;
}

Stroke choose(const FrameState& s, std::uint64_t seed = 1, const Think& think = sims()) {
    Rng rng(seed);
    return choose_stroke(s, maximum(), rig::env(), rng, think);
}

std::string word(const Stroke& st, const char* key) { return st.why.value(key, std::string()); }

bool same(const phys::CueStrike& a, const phys::CueStrike& b) {
    return a.aim_rad == b.aim_rad && a.speed_m_s == b.speed_m_s && a.tip_x == b.tip_x && a.tip_y == b.tip_y &&
           a.elevation_rad == b.elevation_rad;
}

bool is_tactic(const Stroke& st) {
    const std::string kind = word(st, "kind");
    return (kind == "safety" || kind == "snooker" || kind == "escape" || kind == "break") &&
           st.why.contains("thickness");
}

rules::scenario::Played play(const FrameState& s, bool places_cue, rules::Vec2 cue, const phys::CueStrike& strike,
                             rules::ShotInput input) {
    FrameState from = s;
    std::string error;
    if (places_cue) REQUIRE(rules::place_cue_ball(from, cue, rig::env().oracle, error));
    return rules::scenario::play(from, strike, input);
}

// What the stroke leaves the other player, as `reader` reads it: a foul's points and the table,
// played by whoever the foul leaves to choose.
double handed_over(const FrameState& s, const rules::scenario::Played& p, const Level& reader) {
    FrameState post = p.judgement.post;
    if (post.over) return 0.0;
    double out = p.judgement.fouls.empty() ? 0.0 : double(p.judgement.penalty);
    if (post.pending == rules::Pending::after_foul) post.striker = post.decider;
    post.pending = rules::Pending::none;
    if (post.striker == s.striker) return out;
    return out + read_position(post, reader, rig::env()).worth;
}

// The stand-in's waiting stroke as the 147 played it in stage 1: no pots, no error.
StandinShot standin_waiting(const FrameState& s) {
    const Env env = rig::env();
    StandinParams sp = env.standin;
    sp.aim_error_rad = 0.0;
    sp.speed_error = 0.0;
    sp.concede_required_points = std::numeric_limits<int>::max();
    sp.max_cut_rad = -1.0;
    Rng own(7);
    const rules::scenario::Rig& r = rules::scenario::rig();
    return choose_shot(s, r.table, r.params, env.oracle, env.ref, sp, own);
}

bool has_pot_of(const FrameState& s, Ball needed) {
    for (const Pot& pot : pots(s, rig::env(), maximum().max_cut_rad)) {
        if (s.balls[std::size_t(pot.target)].ball == needed) return true;
    }
    return false;
}

// Positions after the stand-in's break-off with every red still on and no red the 147 could
// pot: it waits.
std::vector<FrameState> waiting_positions(int count) {
    const Env env = rig::env();
    const rules::scenario::Rig& r = rules::scenario::rig();
    std::vector<FrameState> out;
    for (std::uint64_t seed = 1; int(out.size()) < count && seed < 200; ++seed) {
        FrameState s = rig::racked(seed);
        Rng rng(500 + seed);
        const StandinShot off = choose_shot(s, r.table, r.params, env.oracle, env.ref, env.standin, rng);
        if (off.decides) continue;
        std::string error;
        if (off.places_cue && !rules::place_cue_ball(s, off.cue_pos, env.oracle, error)) continue;
        const FrameState after = rules::scenario::play(s, off.strike, off.input, seed).judgement.post;
        if (after.over || after.pending != rules::Pending::none || after.striker != 1) continue;
        if (!maximum_on(after) || has_pot_of(after, Ball::red)) continue;
        out.push_back(after);
    }
    return out;
}

constexpr double kGap = 2.0 * table::kBallRadius + 0.001;

// After a red with the maximum on: the black ringed by reds so it has no pocket, the blue dead
// straight into a middle pocket.
FrameState black_shut_blue_open() {
    std::vector<BallOnTable> balls{ball(Ball::cue, 0.03, -0.45), spotted(Ball::blue),  spotted(Ball::yellow),
                                   spotted(Ball::green),         spotted(Ball::brown), spotted(Ball::pink),
                                   spotted(Ball::black)};
    const table::Vec2 at = table::spot(Ball::black);
    for (int i = 0; i < 6; ++i) {
        const double a = i * 3.14159265358979323846 / 3.0;
        balls.push_back(ball(Ball::red, at.x + kGap * std::cos(a), at.y + kGap * std::sin(a), i));
    }
    for (int i = 0; i < 8; ++i) balls.push_back(ball(Ball::red, -1.5, -0.6 + 0.1 * i, 6 + i));
    FrameState s = rules::scenario::frame(std::move(balls));
    s.on = {OnKind::any_colour, Ball::yellow};
    s.break_points = 1;
    s.scores = {1, 0};
    return s;
}

}  // namespace

TEST_CASE("the 147's waiting stroke is a tactic's and hands over less than the stand-in's did") {
    const std::vector<FrameState> positions = waiting_positions(6);
    REQUIRE(positions.size() >= 4);
    const Level& reader = rig::levels().world;
    double tactics = 0.0;
    double standin = 0.0;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        CAPTURE(i);
        const FrameState& s = positions[i];
        const Stroke st = choose(s);
        REQUIRE_FALSE(st.decides);
        CHECK(is_tactic(st));
        CHECK(word(st, "maximum") == "on");
        CHECK(word(st, "level") == "maximum");
        CHECK(st.why.contains("drawn"));
        CHECK(same(st.strike, st.meant));
        CHECK(st.fallback.empty());
        const rules::scenario::Played mine = play(s, st.places_cue, st.cue_pos, st.strike, st.input);
        CHECK(mine.judgement.fouls.empty());
        const StandinShot old = standin_waiting(s);
        REQUIRE_FALSE(old.decides);
        const rules::scenario::Played theirs = play(s, old.places_cue, old.cue_pos, old.strike, old.input);
        tactics += handed_over(s, mine, reader);
        standin += handed_over(s, theirs, reader);
    }
    MESSAGE("handed over on " << positions.size() << " positions, as World class reads them: the 147's tactics "
                              << tactics << ", the stand-in's " << standin);
    CHECK(tactics < standin);
}

TEST_CASE("the 147's break-off is the tactics', played as meant") {
    const FrameState rack = rig::racked(3);
    const Stroke off = choose(rack);
    REQUIRE_FALSE(off.decides);
    CHECK(word(off, "kind") == "break");
    CHECK(off.why.contains("thickness"));
    CHECK(word(off, "maximum") == "on");
    CHECK(word(off, "level") == "maximum");
    CHECK(off.places_cue);
    CHECK(same(off.strike, off.meant));
    const rules::scenario::Played p = play(rack, off.places_cue, off.cue_pos, off.strike, off.input);
    CHECK(p.judgement.fouls.empty());
    CHECK(p.judgement.post.striker == 1);
}

TEST_CASE("with the maximum on and no pot of the black the 147 pots nothing else") {
    const FrameState s = black_shut_blue_open();
    REQUIRE(maximum_on(s));
    REQUIRE_FALSE(has_pot_of(s, Ball::black));
    REQUIRE(has_pot_of(s, Ball::blue));
    const Stroke st = choose(s);
    REQUIRE_FALSE(st.decides);
    CHECK(is_tactic(st));
    CHECK(word(st, "maximum") == "on");
    const rules::scenario::Played p = play(s, st.places_cue, st.cue_pos, st.strike, st.input);
    CHECK(p.facts.pots.empty());
    CHECK(p.judgement.fouls.empty());
}

TEST_CASE("the 147's tactic is the same whatever the seed, and is made again from its steps") {
    const std::vector<FrameState> positions = waiting_positions(1);
    REQUIRE_FALSE(positions.empty());
    for (const FrameState& s : {positions.front(), rig::racked(5)}) {
        const Stroke first = choose(s, 1);
        REQUIRE(is_tactic(first));
        for (const std::uint64_t seed : {std::uint64_t(2), std::uint64_t(977)}) {
            const Stroke other = choose(s, seed);
            CHECK(same(other.strike, first.strike));
            CHECK(other.cue_pos.x == first.cue_pos.x);
            CHECK(other.cue_pos.y == first.cue_pos.y);
            CHECK(other.steps == first.steps);
        }
        Think replay;
        replay.max_steps = first.steps;
        const Stroke again = choose(s, 1, replay);
        CHECK(same(again.strike, first.strike));
        CHECK(again.steps == first.steps);
    }
}
