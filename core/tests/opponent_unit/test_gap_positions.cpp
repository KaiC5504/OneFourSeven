// Positions the search rarely meets: a free ball, the cue ball in hand, a touching ball, the
// re-spotted black, a striker needing snookers. Each level's stroke must still be one the engine
// takes and, where it is a pot, one that pots as planned.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/physics/cue.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::BallOnTable;
using rules::FrameState;
using rules::scenario::ball;
using rules::scenario::place_of;
using rules::scenario::spotted;

namespace {

constexpr double R = table::kBallRadius;

std::vector<const Level*> both() { return {&rig::levels().beginner, &rig::levels().maximum}; }

Think budget(const Level& level) {
    Think t;
    if (level.maximum) t.max_sims = 1500;
    return t;
}

Stroke choose(const FrameState& s, const Level& level, std::uint64_t seed) {
    Rng rng(seed);
    return choose_stroke(s, level, rig::env(), rng, budget(level));
}

std::string text(const nlohmann::json& j, const char* key) {
    if (!j.is_object()) return "";
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

std::string label(const BallOnTable& b) {
    std::string out(table::ball_name(b.ball));
    if (b.ball == Ball::red) out += " " + std::to_string(b.index);
    return out;
}

int place_labelled(const FrameState& s, const std::string& name) {
    for (std::size_t i = 0; i < s.balls.size(); ++i) {
        if (label(s.balls[i]) == name) return int(i);
    }
    return -1;
}

// The stroke as the engine takes it: placed, declared when a declaration is open, within the
// cue's limits, the cue clear of what lies behind the cue ball.
void check_accepted(const FrameState& s, const Stroke& st) {
    const phys::PhysicsParams& params = rules::scenario::rig().params;
    REQUIRE_FALSE(st.decides);
    CHECK(st.places_cue == s.cue_in_hand);
    FrameState from = s;
    std::string error;
    if (st.places_cue) REQUIRE(rules::place_cue_ball(from, st.cue_pos, rig::env().oracle, error));
    if (st.input.has_nomination) {
        const std::vector<Ball> open = rules::nominations(s);
        CHECK(std::find(open.begin(), open.end(), st.input.nominated) != open.end());
    }
    CHECK(std::isfinite(st.strike.aim_rad));
    CHECK(st.strike.speed_m_s >= kLeastSpeed_m_s);
    CHECK(st.strike.speed_m_s <= params.max_cue_speed_m_s);
    CHECK(std::hypot(st.strike.tip_x, st.strike.tip_y) <= kTipLimit * params.miscue_limit + 1e-12);
    const double least = phys::min_cue_elevation(rules::scenario::table_state(from), from.cue_ball, st.strike.aim_rad,
                                                 rules::scenario::rig().table, params);
    CHECK(st.strike.elevation_rad >= least - 1e-12);
    CHECK(st.strike.elevation_rad <= params.max_elevation_rad);
}

// `stroke` played from `s` with the cue as given.
rules::scenario::Played play(const FrameState& s, const Stroke& st, const phys::CueStrike& cue) {
    FrameState from = s;
    std::string error;
    if (st.places_cue) REQUIRE(rules::place_cue_ball(from, st.cue_pos, rig::env().oracle, error));
    return rules::scenario::play(from, cue, st.input);
}

// The straight red with the red hidden tight behind the black and the pink where the red was:
// red on, the cue ball unable to see the red, a free ball called, the pink straight for the top left.
FrameState free_ball_position() {
    FrameState s = rig::position("straight_red");
    s.balls[std::size_t(place_of(s, Ball::red))].pos = {1.511, -0.0161};
    s.balls[std::size_t(place_of(s, Ball::pink))].pos = {1.3, 0.55};
    REQUIRE(rig::env().oracle.snookered(s.balls, s.cue_ball, rules::places_on(s), false));
    s.free_ball = true;
    return s;
}

}  // namespace

TEST_CASE("a free ball: the 147 pots the nominated ball for the value of the ball on, the maximum gone") {
    // pots.hpp: with a free ball each ball that may be declared is listed, worth the ball on.
    // evaluate.hpp: maximum_on is false with a free ball called.
    const FrameState s = free_ball_position();
    REQUIRE_FALSE(maximum_on(s));
    const Level& level = rig::levels().maximum;
    const std::vector<Pot> listed = pots(s, rig::env(), level.max_cut_rad);
    REQUIRE_FALSE(listed.empty());
    for (const Pot& pot : listed) {
        CHECK(pot.value == 1);
        if (s.balls[std::size_t(pot.target)].ball != Ball::red) CHECK(pot.input.has_nomination);
    }

    const Stroke st = choose(s, level, 5);
    check_accepted(s, st);
    CHECK(text(st.why, "kind") == "pot");
    CHECK(text(st.why, "maximum") == "gone");
    CHECK(st.strike.aim_rad == st.meant.aim_rad);
    const int target = place_labelled(s, text(st.why, "target"));
    REQUIRE(target >= 0);
    if (s.balls[std::size_t(target)].ball != Ball::red) {
        CHECK(st.input.has_nomination);
        CHECK(st.input.nominated == s.balls[std::size_t(target)].ball);
    }
    const rules::Judgement j = play(s, st, st.strike).judgement;
    CHECK(j.fouls.empty());
    CHECK(j.points == 1);
}

TEST_CASE("a free ball with every red still on: the 147 reads the maximum gone and still pots") {
    FrameState s = exhibition_frame(rig::levels().exhibitions.front(), 0, 3);
    REQUIRE(maximum_on(s));
    s.free_ball = true;
    REQUIRE_FALSE(maximum_on(s));
    const Stroke st = choose(s, rig::levels().maximum, 1);
    check_accepted(s, st);
    CHECK(text(st.why, "maximum") == "gone");
    REQUIRE(text(st.why, "kind") == "pot");
    const rules::Judgement j = play(s, st, st.strike).judgement;
    CHECK(j.fouls.empty());
    CHECK(j.points == 1);
}

TEST_CASE("a free ball on the colours: every pot is worth the yellow, and the 147's scores two") {
    FrameState s = rig::position("colours");
    REQUIRE(s.on.colour == Ball::yellow);
    s.free_ball = true;
    const std::vector<Pot> listed = pots(s, rig::env(), rig::levels().maximum.max_cut_rad);
    REQUIRE_FALSE(listed.empty());
    for (const Pot& pot : listed) {
        CHECK(pot.value == 2);
        if (s.balls[std::size_t(pot.target)].ball != Ball::yellow) CHECK(pot.input.has_nomination);
    }
    const Stroke st = choose(s, rig::levels().maximum, 1);
    check_accepted(s, st);
    CHECK(text(st.why, "maximum") == "gone");
    REQUIRE(text(st.why, "kind") == "pot");
    const rules::Judgement j = play(s, st, st.strike).judgement;
    CHECK(j.fouls.empty());
    CHECK(j.points == 2);
}

TEST_CASE("a free ball: Beginner's stroke is legal and, when a pot, its meant stroke scores the ball on's value") {
    const FrameState s = free_ball_position();
    const Level& level = rig::levels().beginner;
    int pots_seen = 0;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        CAPTURE(seed);
        const Stroke st = choose(s, level, seed);
        check_accepted(s, st);
        CHECK(text(st.why, "maximum") == "off");
        if (text(st.why, "kind") != "pot") continue;
        ++pots_seen;
        const rules::Judgement j = play(s, st, st.meant).judgement;
        CHECK(j.fouls.empty());
        CHECK(j.points == 1);
    }
    CHECK(pots_seen > 0);
}

namespace {

// pots.hpp: with the cue ball in hand, pots from each of cue_placements, each keeping its place.
void check_pots_keep_their_place(const FrameState& s) {
    REQUIRE(s.cue_in_hand);
    const std::vector<rules::Vec2> places = cue_placements(s, rig::env(), kPlacements);
    for (const Level* level : both()) {
        for (std::uint64_t seed = 1; seed <= (level->maximum ? 1u : 6u); ++seed) {
            CAPTURE(level->id);
            CAPTURE(seed);
            const Stroke st = choose(s, *level, seed);
            check_accepted(s, st);
            REQUIRE(st.places_cue);
            CHECK(rig::env().oracle.cue_placement_legal(s.balls, s.cue_ball, st.cue_pos));
            if (text(st.why, "kind") != "pot") continue;
            CHECK(std::any_of(places.begin(), places.end(),
                              [&](rules::Vec2 p) { return p.x == st.cue_pos.x && p.y == st.cue_pos.y; }));
            const int target = place_labelled(s, text(st.why, "target"));
            REQUIRE(target >= 0);
            const int pocket = st.why.value("pocket", -1);
            const std::vector<Pot> from_there = pots_from(s, st.cue_pos, rig::env(), level->max_cut_rad);
            CHECK(std::any_of(from_there.begin(), from_there.end(),
                              [&](const Pot& p) { return p.target == target && p.pocket == pocket; }));
            const rules::scenario::Played meant = play(s, st, st.meant);
            CHECK(meant.judgement.fouls.empty());
            CHECK(meant.record.final_state.balls[std::size_t(target)].pocket == pocket);
        }
    }
}

}  // namespace

TEST_CASE("in hand, a pot keeps the place it was listed from: a legal place in the D that pots the red") {
    check_pots_keep_their_place(rig::position("in_hand"));
}

TEST_CASE("in hand on the colours after an in-off, a pot keeps its place in the D too") {
    FrameState s = rig::position("colours");
    s.cue_in_hand = true;
    s.balls[std::size_t(s.cue_ball)].pos = {-1.2, 0.1};
    check_pots_keep_their_place(s);
}

TEST_CASE("a touching ball: neither level's stroke pushes it, and the 147 pots another red") {
    // The cue ball touches red 0 (2 microns apart, inside the referee's touching gap), which lies
    // dead straight for the top-left corner from it; red 1, played away from red 0, is a pot into
    // the middle right.
    std::vector<BallOnTable> balls{ball(Ball::cue, 0.5, 0.0)};
    const rules::Vec2 pocket{1.78, 0.89};
    const double d = std::hypot(pocket.x - 0.5, pocket.y);
    const double apart = 2.0 * R + 2.0e-6;
    balls.push_back(ball(Ball::red, 0.5 + apart * (pocket.x - 0.5) / d, apart * pocket.y / d, 0));
    balls.push_back(ball(Ball::red, 0.0, -0.5, 1));
    for (const Ball c : {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        balls.push_back(spotted(c));
    }
    FrameState s = rules::scenario::frame(balls);
    s.touching = rig::env().oracle.touching(s.balls, s.cue_ball);
    REQUIRE(s.touching == std::vector<int>{1});
    const std::vector<Pot> listed = pots(s, rig::env(), rig::levels().maximum.max_cut_rad);
    REQUIRE(std::any_of(listed.begin(), listed.end(), [](const Pot& p) { return p.target == 2; }));

    for (const Level* level : both()) {
        for (std::uint64_t seed = 1; seed <= (level->maximum ? 1u : 6u); ++seed) {
            CAPTURE(level->id);
            CAPTURE(seed);
            const Stroke st = choose(s, *level, seed);
            check_accepted(s, st);
            const rules::Judgement j = play(s, st, st.meant).judgement;
            CHECK(j.fouls.empty());
            if (text(st.why, "kind") == "pot") {
                CHECK(text(st.why, "target") != "red 0");
                CHECK(j.points == 1);
            }
            if (level->maximum) CHECK(text(st.why, "kind") == "pot");
        }
    }
}

TEST_CASE("the re-spotted black: each level places the cue ball, plays a stroke, and claims nothing") {
    FrameState s = rules::scenario::frame({ball(Ball::cue, -1.1475, 0.0), spotted(Ball::black)});
    s.respotted_black = true;
    s.cue_in_hand = true;
    s.scores = {60, 60};
    s.offender = 1;
    for (const Level* level : both()) {
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            CAPTURE(level->id);
            CAPTURE(seed);
            const Stroke st = choose(s, *level, seed);
            check_accepted(s, st);
            CHECK(st.why.value("level", std::string()) == level->id);
            CHECK(text(st.why, "maximum") == (level->maximum ? "gone" : "off"));
            const rules::Judgement j = play(s, st, st.meant).judgement;
            CHECK(j.fouls.empty());
        }
    }
}

TEST_CASE("needing snookers on the last colours, a level that never concedes plays on") {
    // README "The score": never with concede_snookers 0, as every level played in stage 1
    // (test_plan_concede.cpp has a copy that does concede).
    Level never = rig::levels().beginner;
    never.concede_snookers = 0;
    FrameState colours = rig::position("colours");
    colours.scores = {0, 60};
    FrameState black = rig::position("black_only");
    black.scores = {0, 20};
    for (const FrameState& s : {colours, black}) {
        const std::vector<rules::DecisionKind> open = rules::options(s, s.striker);
        REQUIRE(std::find(open.begin(), open.end(), rules::DecisionKind::concede_frame) != open.end());
        for (const Level* level : {static_cast<const Level*>(&never), &rig::levels().maximum}) {
            CAPTURE(level->id);
            const Stroke st = choose(s, *level, 2);
            CHECK_FALSE(st.decides);
            check_accepted(s, st);
        }
    }
}

TEST_CASE("the last red with the colours off their spots: the 147 pots the red and plans the black next") {
    FrameState s = rig::position("last_red");
    REQUIRE(maximum_on(s));
    const std::pair<Ball, rules::Vec2> moved[] = {{Ball::yellow, {-0.5, -0.5}}, {Ball::green, {-0.5, 0.5}},
                                                  {Ball::brown, {0.3, 0.6}},    {Ball::blue, {0.2, -0.2}},
                                                  {Ball::pink, {0.5, -0.65}}};
    for (const auto& [colour, at] : moved) s.balls[std::size_t(place_of(s, colour))].pos = at;
    const Stroke st = choose(s, rig::levels().maximum, 1);
    REQUIRE_FALSE(st.decides);
    CHECK(text(st.why, "maximum") == "on");
    CHECK(text(st.why, "kind") == "pot");
    CHECK(text(st.why, "target") == "red 0");
    const rules::Judgement j = play(s, st, st.strike).judgement;
    CHECK(j.fouls.empty());
    CHECK(j.points == 1);
    CHECK(maximum_on(j.post));
    const nlohmann::json& plan = st.why["plan"];
    if (plan.is_array() && !plan.empty()) CHECK(text(plan[0], "target") == "black");
}

TEST_CASE("the pink and black with the scores level: each level plays a legal stroke, any pot at the pink") {
    FrameState s = rules::scenario::frame({ball(Ball::cue, 0.6, 0.3), spotted(Ball::pink), spotted(Ball::black)});
    s.scores = {40, 40};
    REQUIRE(s.on.colour == Ball::pink);
    for (const Level* level : both()) {
        CAPTURE(level->id);
        const Stroke st = choose(s, *level, 4);
        check_accepted(s, st);
        if (text(st.why, "kind") == "pot") CHECK(text(st.why, "target") == "pink");
    }
}
