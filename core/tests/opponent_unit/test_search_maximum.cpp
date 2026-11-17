#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"

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

// A simulation is slow in a Debug build: the tests give the 147 a small part of its budget.
Think sims(int n = 1500) {
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

struct Played {
    rules::Judgement judgement;
    std::vector<int> potted;  // places before the stroke
};

Played play(const FrameState& s, const Stroke& stroke) {
    FrameState from = s;
    std::string error;
    if (stroke.places_cue) REQUIRE(rules::place_cue_ball(from, stroke.cue_pos, rig::env().oracle, error));
    const rules::scenario::Played p = rules::scenario::play(from, stroke.strike, stroke.input);
    Played out;
    out.judgement = p.judgement;
    for (const rules::Pot& pot : p.facts.pots) out.potted.push_back(pot.ball);
    return out;
}

// The 147 at the table until its break ends: the frame over, a stroke that does not score, or
// `most` strokes.
struct Break {
    FrameState end;
    int strokes = 0;
    int points = 0;
    std::string last_kind;
};

Break play_break(FrameState s, const Think& think, int most = 60) {
    Break out;
    const int player = s.striker;
    while (out.strokes < most && !s.over && s.pending == rules::Pending::none && s.striker == player) {
        const Stroke st = choose(s, 1, think);
        REQUIRE_FALSE(st.decides);
        ++out.strokes;
        out.last_kind = word(st, "kind");
        const Played p = play(s, st);
        if (word(st, "kind") == "pot") {
            // What it planned is what happens.
            REQUIRE(p.judgement.fouls.empty());
            REQUIRE(p.judgement.points > 0);
        }
        if (p.judgement.fouls.empty()) out.points += p.judgement.points;
        const bool scored = p.judgement.fouls.empty() && p.judgement.points > 0;
        s = p.judgement.post;
        if (!scored) break;
    }
    out.end = s;
    return out;
}

constexpr double kGap = 2.0 * table::kBallRadius + 0.001;

// Six reds round the black on its spot: it has no way to any pocket.
void ring_the_black(std::vector<BallOnTable>& balls, int first_red) {
    const table::Vec2 at = table::spot(Ball::black);
    for (int i = 0; i < 6; ++i) {
        const double a = i * 3.14159265358979323846 / 3.0;
        balls.push_back(ball(Ball::red, at.x + kGap * std::cos(a), at.y + kGap * std::sin(a), first_red + i));
    }
}

// Reds in a line across the baulk end, out of the way of everything near the top of the table.
void line_of_reds(std::vector<BallOnTable>& balls, int first_red, int count) {
    for (int i = 0; i < count; ++i) balls.push_back(ball(Ball::red, -1.5, -0.6 + 0.1 * i, first_red + i));
}

FrameState after_a_red(std::vector<BallOnTable> balls, int break_points) {
    FrameState s = rules::scenario::frame(std::move(balls));
    s.on = {OnKind::any_colour, Ball::yellow};
    s.break_points = break_points;
    s.scores = {break_points, 0};
    return s;
}

// A colour of choice with the black shut in and the blue straight into a middle pocket from the
// cue ball. `reds` of them on the table, one potted in this break.
FrameState black_tied_blue_open(int reds) {
    std::vector<BallOnTable> balls{ball(Ball::cue, 0.03, -0.45), spotted(Ball::blue),  spotted(Ball::yellow),
                                   spotted(Ball::green),         spotted(Ball::brown), spotted(Ball::pink),
                                   spotted(Ball::black)};
    ring_the_black(balls, 0);
    line_of_reds(balls, 6, reds - 6);
    return after_a_red(std::move(balls), 1);
}

}  // namespace

TEST_CASE("the 147 plays what it planned, the same whatever the seed") {
    for (const char* name : {"straight_red", "cut_red", "last_red", "snookered", "in_hand"}) {
        CAPTURE(name);
        const FrameState s = rig::position(name);
        const Stroke first = choose(s, 1);
        REQUIRE_FALSE(first.decides);
        CHECK(same(first.strike, first.meant));
        CHECK(first.fallback.empty());
        for (const std::uint64_t seed : {std::uint64_t(2), std::uint64_t(977)}) {
            const Stroke other = choose(s, seed);
            CHECK(same(other.strike, first.strike));
            CHECK(other.cue_pos.x == first.cue_pos.x);
            CHECK(other.cue_pos.y == first.cue_pos.y);
            CHECK(other.steps == first.steps);
        }
    }
}

TEST_CASE("the 147 draws its four normals and no more, though they change nothing") {
    const FrameState s = rig::position("straight_red");
    Rng rng(5);
    choose_stroke(s, maximum(), rig::env(), rng, sims());
    Rng same_seed(5);
    for (int i = 0; i < 4; ++i) same_seed.normal();
    CHECK(rng.next_u64() == same_seed.next_u64());
}

TEST_CASE("the 147 pots from each open position, and the referee calls nothing") {
    for (const char* name : {"straight_red", "cut_red", "long_red", "colours", "black_only"}) {
        CAPTURE(name);
        const FrameState s = rig::position(name);
        const Stroke st = choose(s);
        REQUIRE_FALSE(st.decides);
        CHECK(word(st, "kind") == "pot");
        CHECK(word(st, "maximum") == "gone");
        CHECK(word(st, "maximum_why") == "a red is off the table");
        const Played p = play(s, st);
        CHECK(p.judgement.fouls.empty());
        CHECK(p.judgement.points > 0);
        const nlohmann::json& cue = st.why["leave"]["cue"];
        const rules::Vec2 at = p.judgement.post.balls[std::size_t(p.judgement.post.cue_ball)].pos;
        if (!p.judgement.post.over) {
            CHECK(at.x == doctest::Approx(cue[0].get<double>()));
            CHECK(at.y == doctest::Approx(cue[1].get<double>()));
        }
    }
}

TEST_CASE("a pot's why has every field the README gives it") {
    const Stroke st = choose(rig::position("after_red"));
    REQUIRE(word(st, "kind") == "pot");
    for (const char* key : {"level", "kind", "target", "pocket", "band", "cut_rad", "chance", "worth", "spec", "leave",
                            "plan", "others", "drawn", "maximum"}) {
        CAPTURE(key);
        CHECK(st.why.contains(key));
    }
    CHECK(word(st, "level") == "maximum");
    CHECK(st.why["chance"].get<double>() == 1.0);
    for (const char* key : {"speed_m_s", "tip_x", "tip_y", "elevation_rad"}) CHECK(st.why["spec"].contains(key));
    for (const char* key : {"cue", "next", "chance"}) CHECK(st.why["leave"].contains(key));
    for (const char* key : {"aim_rad", "speed", "tip_x", "tip_y"}) CHECK(st.why["drawn"][key].get<double>() == 0.0);
    CHECK(st.why["others"].size() <= 3);
    CHECK(st.why["plan"].size() == std::size_t(st.depth));
    CHECK(std::hypot(st.meant.tip_x, st.meant.tip_y) <= maximum().max_tip + 1e-12);
    CHECK(st.candidates > 0);
    CHECK(st.sims > 0);
    CHECK(st.steps > 0);
}

TEST_CASE("after a red with the maximum on the 147 declares the black, pots it and has a red to follow") {
    const FrameState s = rig::position("after_red");
    REQUIRE(maximum_on(s));
    const Stroke st = choose(s, 1, sims(4000));
    REQUIRE_FALSE(st.decides);
    CHECK(word(st, "maximum") == "on");
    CHECK(st.input.has_nomination);
    CHECK(st.input.nominated == Ball::black);
    CHECK(word(st, "target") == "black");
    CHECK(st.depth >= 1);
    REQUIRE_FALSE(st.why["plan"].empty());
    CHECK(st.why["plan"][0]["target"].get<std::string>().rfind("red ", 0) == 0);
    const Played p = play(s, st);
    CHECK(p.judgement.fouls.empty());
    CHECK(p.judgement.points == 7);
    CHECK(maximum_on(p.judgement.post));
}

TEST_CASE("snookered, and from the break-off rack, the 147 plays the stand-in's stroke as it is meant") {
    const FrameState snookered = rig::position("snookered");
    const Stroke out = choose(snookered);
    REQUIRE_FALSE(out.decides);
    CHECK((word(out, "kind") == "escape" || word(out, "kind") == "safety"));
    CHECK(same(out.strike, out.meant));
    CHECK(word(out, "level") == "maximum");
    CHECK(out.fallback.empty());
    CHECK(play(snookered, out).judgement.post.strokes == snookered.strokes + 1);

    const FrameState rack = rig::racked();
    const Stroke off = choose(rack);
    REQUIRE_FALSE(off.decides);
    CHECK(word(off, "kind") == "break");
    CHECK(word(off, "maximum") == "on");
    CHECK(off.places_cue);
    CHECK(play(rack, off).judgement.fouls.empty());
}

TEST_CASE("from the last red the 147 clears the table for 147") {
    const FrameState s = rig::position("last_red");
    REQUIRE(maximum_on(s));
    const Break b = play_break(s, sims(3000));
    CHECK(b.strokes == 8);
    CHECK(b.end.over);
    CHECK(b.end.winner == 0);
    CHECK(b.end.scores[0] == 147);
}

TEST_CASE("on the exhibition table the 147 makes a break") {
    const FrameState s = exhibition_frame(rig::levels().exhibitions[0], 0, 1);
    const Break b = play_break(s, sims(1500));
    MESSAGE("the exhibition break with 1,500 simulations a stroke: " << b.points << " in " << b.strokes
                                                                      << " strokes, ending on a " << b.last_kind);
    CHECK(b.points >= 30);
}

TEST_CASE("in hand the 147 places the cue ball legally and pots the red") {
    const FrameState s = rig::position("in_hand");
    const Stroke st = choose(s);
    REQUIRE_FALSE(st.decides);
    REQUIRE(st.places_cue);
    CHECK(table::in_d(st.cue_pos));
    CHECK(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, st.cue_pos));
    CHECK(word(st, "kind") == "pot");
    const Played p = play(s, st);
    CHECK(p.judgement.fouls.empty());
    CHECK(p.judgement.points == 1);
}

TEST_CASE("with the maximum gone the 147 takes the black after a red when the black goes") {
    FrameState s = rig::position("after_red");
    s.balls.erase(s.balls.begin() + rules::scenario::place_of(s, Ball::red, 13));
    REQUIRE_FALSE(maximum_on(s));
    const Stroke st = choose(s);
    REQUIRE_FALSE(st.decides);
    CHECK(word(st, "kind") == "pot");
    CHECK(word(st, "target") == "black");
    CHECK(word(st, "maximum") == "gone");
    CHECK(word(st, "maximum_why") == "the break lost it");
    const Played p = play(s, st);
    CHECK(p.judgement.fouls.empty());
    CHECK(p.judgement.points == 7);
}

TEST_CASE("with the maximum gone and the black tied up the 147 takes a lesser colour") {
    const FrameState s = black_tied_blue_open(13);
    REQUIRE_FALSE(maximum_on(s));
    REQUIRE(pockets_open_to(s, rules::scenario::place_of(s, Ball::black), rig::env()).empty());
    const Stroke st = choose(s);
    REQUIRE_FALSE(st.decides);
    CHECK(word(st, "kind") == "pot");
    CHECK(word(st, "target") != "black");
    CHECK(st.input.has_nomination);
    const Played p = play(s, st);
    CHECK(p.judgement.fouls.empty());
    CHECK(p.judgement.points >= 2);
    CHECK(p.judgement.points == table::ball_value(st.input.nominated));
}

TEST_CASE("with the maximum on, the black tied up and the blue open, the 147 does not pot the blue") {
    const FrameState s = black_tied_blue_open(14);
    REQUIRE(maximum_on(s));
    REQUIRE(pockets_open_to(s, rules::scenario::place_of(s, Ball::black), rig::env()).empty());
    bool blue_on = false;
    for (const Pot& pot : pots(s, rig::env(), maximum().max_cut_rad)) {
        blue_on = blue_on || s.balls[std::size_t(pot.target)].ball == Ball::blue;
    }
    REQUIRE(blue_on);
    const Stroke st = choose(s);
    REQUIRE_FALSE(st.decides);
    CHECK(word(st, "kind") != "pot");
    CHECK(word(st, "maximum") == "on");
    CHECK(same(st.strike, st.meant));
    CHECK(st.input.has_nomination);
}

TEST_CASE("a maximum still on is played out: the 147 does not claim the frame at the last black") {
    FrameState s = rig::position("black_only");
    s.scores = {140, 0};
    s.break_points = 140;
    REQUIRE(maximum_on(s));
    const std::vector<rules::DecisionKind> open = rules::options(s, s.striker);
    REQUIRE(std::find(open.begin(), open.end(), rules::DecisionKind::claim_frame) != open.end());
    const Stroke st = choose(s);
    REQUIRE_FALSE(st.decides);
    CHECK(word(st, "kind") == "pot");
    const Played p = play(s, st);
    CHECK(p.judgement.post.over);
    CHECK(p.judgement.post.scores[0] == 147);

    s.break_points = 0;
    REQUIRE_FALSE(maximum_on(s));
    const Stroke claim = choose(s);
    CHECK(claim.decides);
    CHECK(claim.decision == rules::DecisionKind::claim_frame);
    CHECK(word(claim, "kind") == "claim");
    CHECK(word(claim, "level") == "maximum");
}
