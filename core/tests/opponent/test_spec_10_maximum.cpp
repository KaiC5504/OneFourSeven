// The 147 (spec 10.1's sixth tier; docs/opponent/README.md "The 147" and "The search"): no error,
// the same stroke whatever the seed, a black after every red while the maximum is on, and the
// highest break still possible once it is gone.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using nlohmann::json;
using rules::Ball;
using rules::BallOnTable;
using rules::FrameState;
using rules::OnKind;
using rules::scenario::ball;
using rules::scenario::spotted;

namespace {

const char* const kPositions[] = {"straight_red", "cut_red",  "long_red",  "colours",   "black_only",
                                  "last_red",     "after_red", "in_hand", "snookered", "racked"};

FrameState named(const std::string& name) { return name == "racked" ? rig::racked(1) : rig::position(name.c_str()); }

const Level& maximum() { return rig::levels().maximum; }

Think budget(int sims = 1500) {
    Think t;
    t.max_sims = sims;
    return t;
}

Stroke choose(const FrameState& s, const Level& level, std::uint64_t seed, const Think& think = budget()) {
    Rng rng(seed);
    return choose_stroke(s, level, rig::env(), rng, think);
}

// The 147's stroke from each named position with seed 1, chosen once.
const Stroke& chosen(const std::string& name) {
    static std::map<std::string, Stroke> cache;
    auto it = cache.find(name);
    if (it == cache.end()) it = cache.emplace(name, choose(named(name), maximum(), 1)).first;
    return it->second;
}

std::string text(const json& j, const char* key) {
    if (!j.is_object()) return "";
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

bool same_strike(const phys::CueStrike& a, const phys::CueStrike& b) {
    return a.aim_rad == b.aim_rad && a.speed_m_s == b.speed_m_s && a.tip_x == b.tip_x && a.tip_y == b.tip_y &&
           a.elevation_rad == b.elevation_rad;
}

struct Result {
    bool placed = false;
    rules::Judgement judgement;
};

Result play_stroke(const FrameState& s, const Stroke& stroke) {
    Result out;
    FrameState f = s;
    std::string error;
    if (stroke.places_cue && !rules::place_cue_ball(f, stroke.cue_pos, rig::env().oracle, error)) return out;
    out.placed = true;
    out.judgement = rules::scenario::play(f, stroke.strike, stroke.input).judgement;
    return out;
}

bool scored_clean(const Result& r) { return r.placed && r.judgement.points > 0 && r.judgement.fouls.empty(); }

// The level plays on from `s` until its break ends: the frame over, the turn passed, or a
// decision pending. At most 60 strokes.
struct Break {
    FrameState end;
    int strokes = 0;
    bool refused = false;  // a placement or a decision the engine would not take
};

Break play_break(FrameState s, const Level& level, std::uint64_t seed, const Think& think) {
    Break b;
    Rng rng(seed);
    const int player = s.striker;
    while (b.strokes < 60 && !s.over && s.pending == rules::Pending::none && s.striker == player) {
        const Stroke st = choose_stroke(s, level, rig::env(), rng, think);
        ++b.strokes;
        if (st.decides) {
            std::vector<rules::Call> calls;
            std::string error;
            b.refused = !rules::decide(s, {st.decision, s.striker}, rig::env().oracle, rig::env().ref, calls, error);
            break;
        }
        const Result r = play_stroke(s, st);
        if (!r.placed) {
            b.refused = true;
            break;
        }
        s = r.judgement.post;
    }
    b.end = s;
    return b;
}

int place_of(const FrameState& s, Ball b, int index = 0) { return rules::scenario::place_of(s, b, index); }

constexpr double kGap = 2.0 * table::kBallRadius + 0.0005;

// The black on its spot with the pink beside it towards baulk, both ringed by eight reds with
// half a millimetre between neighbours: neither has a clear run to any pocket.
void add_boxed_black_and_pink(std::vector<BallOnTable>& balls, int first_red) {
    const double bx = table::spot(Ball::black).x;
    const double px = bx - kGap;
    const double h = kGap * std::sqrt(3.0) / 2.0;
    balls.push_back(ball(Ball::black, bx, 0.0));
    balls.push_back(ball(Ball::pink, px, 0.0));
    const double ring[8][2] = {{bx + kGap, 0.0},       {bx + kGap / 2, h},  {bx + kGap / 2, -h},
                               {bx - kGap / 2, h},     {bx - kGap / 2, -h}, {px - kGap / 2, h},
                               {px - kGap / 2, -h},    {px - kGap, 0.0}};
    for (int i = 0; i < 8; ++i) balls.push_back(ball(Ball::red, ring[i][0], ring[i][1], first_red + i));
}

// Reds out of the way along y = -0.6, 0.1 m apart.
void add_row_of_reds(std::vector<BallOnTable>& balls, int first_red, int count) {
    for (int i = 0; i < count; ++i) balls.push_back(ball(Ball::red, -0.65 + 0.1 * i, -0.6, first_red + i));
}

void add_spotted(std::vector<BallOnTable>& balls, std::initializer_list<Ball> colours) {
    for (Ball c : colours) balls.push_back(spotted(c));
}

// The cue ball and a ball dead straight into the top-left corner, as straight_red has them.
constexpr rules::Vec2 kStraightCue{0.8084, 0.206};
constexpr rules::Vec2 kStraightBall{1.3, 0.55};
// The cue ball and the blue dead straight into the left middle pocket.
constexpr rules::Vec2 kBlueCue{0.0, 0.1};
constexpr rules::Vec2 kBlue{0.0, 0.5};

FrameState with_colour_of_choice(std::vector<BallOnTable> balls, int break_points) {
    FrameState s = rules::scenario::frame(std::move(balls));
    s.on = {OnKind::any_colour, Ball::yellow};
    s.break_points = break_points;
    s.scores = {break_points, 0};
    return s;
}

// Fourteen reds, one potted in this break: the maximum on and the black needed, but the black is
// boxed in and the blue sits over the middle pocket.
FrameState maximum_on_black_boxed() {
    std::vector<BallOnTable> balls{ball(Ball::cue, kBlueCue.x, kBlueCue.y), ball(Ball::blue, kBlue.x, kBlue.y)};
    add_spotted(balls, {Ball::yellow, Ball::green, Ball::brown});
    add_boxed_black_and_pink(balls, 0);
    add_row_of_reds(balls, 8, 6);
    return with_colour_of_choice(balls, 1);
}

// Red on, fourteen reds, no break under way: the maximum is gone. One red is dead straight.
FrameState gone_red_on() {
    std::vector<BallOnTable> balls{ball(Ball::cue, kStraightCue.x, kStraightCue.y),
                                   ball(Ball::red, kStraightBall.x, kStraightBall.y, 0)};
    add_row_of_reds(balls, 1, 13);
    add_spotted(balls, {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black});
    return rules::scenario::frame(balls);
}

// Thirteen reds and a colour of choice after a break of one: the maximum is gone. The black is
// dead straight into the top-left corner.
FrameState gone_black_easy() {
    std::vector<BallOnTable> balls{ball(Ball::cue, kStraightCue.x, kStraightCue.y),
                                   ball(Ball::black, kStraightBall.x, kStraightBall.y)};
    add_row_of_reds(balls, 0, 13);
    add_spotted(balls, {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink});
    return with_colour_of_choice(balls, 1);
}

// As above with the black and the pink boxed in and the blue over the middle pocket.
FrameState gone_black_boxed() {
    std::vector<BallOnTable> balls{ball(Ball::cue, kBlueCue.x, kBlueCue.y), ball(Ball::blue, kBlue.x, kBlue.y)};
    add_spotted(balls, {Ball::yellow, Ball::green, Ball::brown});
    add_boxed_black_and_pink(balls, 0);
    add_row_of_reds(balls, 8, 5);
    return with_colour_of_choice(balls, 1);
}

// All fifteen reds, no break yet: the maximum on, one red dead straight.
FrameState fifteen_reds() {
    std::vector<BallOnTable> balls{ball(Ball::cue, kStraightCue.x, kStraightCue.y),
                                   ball(Ball::red, kStraightBall.x, kStraightBall.y, 0)};
    add_row_of_reds(balls, 1, 14);
    add_spotted(balls, {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black});
    return rules::scenario::frame(balls);
}

bool has_pot_of(const FrameState& s, Ball b) {
    for (const Pot& p : pots(s, rig::env(), maximum().max_cut_rad)) {
        if (s.balls[std::size_t(p.target)].ball == b) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("README, the 147: the stroke played is the stroke planned, from every named position and the break-off") {
    for (const std::string name : kPositions) {
        CAPTURE(name);
        const Stroke& st = chosen(name);
        if (st.decides) continue;
        CHECK(same_strike(st.strike, st.meant));
    }
}

TEST_CASE("README, the search: the 147's stroke is the same whatever the seed") {
    for (const std::string name : {"straight_red", "after_red", "last_red"}) {
        const FrameState s = named(name);
        const Stroke& first = chosen(name);
        for (std::uint64_t seed : {std::uint64_t(2), std::uint64_t(0xFFFFFFFFull)}) {
            CAPTURE(name);
            CAPTURE(seed);
            const Stroke other = choose(s, maximum(), seed);
            CHECK(other.decides == first.decides);
            CHECK(same_strike(other.strike, first.strike));
            CHECK(other.input.has_nomination == first.input.has_nomination);
            CHECK(other.input.nominated == first.input.nominated);
            CHECK(other.places_cue == first.places_cue);
            CHECK(other.cue_pos.x == first.cue_pos.x);
            CHECK(other.cue_pos.y == first.cue_pos.y);
        }
    }
}

TEST_CASE("README, the 147: it pots what it plans, a pot that scores and is no foul") {
    for (const std::string name : {"straight_red", "cut_red", "long_red", "colours", "black_only", "last_red", "after_red"}) {
        CAPTURE(name);
        const FrameState s = named(name);
        const Stroke& st = chosen(name);
        REQUIRE_FALSE(st.decides);
        CHECK(text(st.why, "kind") == "pot");
        const Result r = play_stroke(s, st);
        CHECK(scored_clean(r));
    }
}

TEST_CASE("spec 10.1, the 147: after a red with the maximum on it declares the black and pots it") {
    const FrameState s = rig::position("after_red");
    REQUIRE(maximum_on(s));
    const Stroke& st = chosen("after_red");
    REQUIRE_FALSE(st.decides);
    CHECK(st.input.has_nomination);
    CHECK(st.input.nominated == Ball::black);
    CHECK(text(st.why, "target") == "black");
    const Result r = play_stroke(s, st);
    REQUIRE(r.placed);
    CHECK(r.judgement.fouls.empty());
    CHECK(r.judgement.points == 7);
}

TEST_CASE("spec 10.1, the 147: from the last red with the maximum on it clears the table for 147") {
    const FrameState s = rig::position("last_red");
    REQUIRE(maximum_on(s));
    const Break b = play_break(s, maximum(), 1, budget(5000));
    CHECK_FALSE(b.refused);
    CHECK(b.strokes <= 8);
    CHECK(b.end.over);
    CHECK(b.end.scores[0] == 147);
    CHECK(b.end.winner == 0);
}

TEST_CASE("README, the 147: after a red, with the black boxed in and the blue easy, it does not pot another colour") {
    const FrameState s = maximum_on_black_boxed();
    REQUIRE(maximum_on(s));
    REQUIRE(maximum_next(s) == Ball::black);
    REQUIRE(pockets_open_to(s, place_of(s, Ball::black), rig::env()).empty());
    REQUIRE(pockets_open_to(s, place_of(s, Ball::pink), rig::env()).empty());
    REQUIRE(has_pot_of(s, Ball::blue));

    const Stroke st = choose(s, maximum(), 1);
    REQUIRE_FALSE(st.decides);
    const std::string kind = text(st.why, "kind");
    CAPTURE(kind);
    CHECK((kind != "pot" || text(st.why, "target") == "black"));
    CHECK(same_strike(st.strike, st.meant));
    CHECK(play_stroke(s, st).placed);
}

TEST_CASE("README, the 147: with no pot at all it plays an escape or a safety, without error") {
    const Stroke& st = chosen("snookered");
    REQUIRE_FALSE(st.decides);
    const std::string kind = text(st.why, "kind");
    CAPTURE(kind);
    CHECK((kind == "escape" || kind == "safety"));
    CHECK(same_strike(st.strike, st.meant));
}

TEST_CASE("README, the 147: the maximum gone with a red off the table, it says so and pots a red") {
    const FrameState s = gone_red_on();
    REQUIRE_FALSE(maximum_on(s));
    REQUIRE(has_pot_of(s, Ball::red));
    const Stroke st = choose(s, maximum(), 1);
    REQUIRE_FALSE(st.decides);
    CHECK(text(st.why, "maximum") == "gone");
    CHECK(text(st.why, "maximum_why") == "a red is off the table");
    CHECK(text(st.why, "kind") == "pot");
    const Result r = play_stroke(s, st);
    CHECK(scored_clean(r));
    CHECK(r.judgement.points == 1);
}

TEST_CASE("README, the 147: the maximum gone, a colour of choice with the black easy, it takes the black") {
    const FrameState s = gone_black_easy();
    REQUIRE_FALSE(maximum_on(s));
    REQUIRE(has_pot_of(s, Ball::black));
    const Stroke st = choose(s, maximum(), 1);
    REQUIRE_FALSE(st.decides);
    CHECK(text(st.why, "maximum") == "gone");
    CHECK(text(st.why, "kind") == "pot");
    CHECK(text(st.why, "target") == "black");
    CHECK(st.input.has_nomination);
    CHECK(st.input.nominated == Ball::black);
    const Result r = play_stroke(s, st);
    CHECK(scored_clean(r));
    CHECK(r.judgement.points == 7);
}

TEST_CASE("README, the 147: the maximum gone, the black boxed in and the blue easy, it takes the blue") {
    const FrameState s = gone_black_boxed();
    REQUIRE_FALSE(maximum_on(s));
    REQUIRE(pockets_open_to(s, place_of(s, Ball::black), rig::env()).empty());
    REQUIRE(pockets_open_to(s, place_of(s, Ball::pink), rig::env()).empty());
    REQUIRE(has_pot_of(s, Ball::blue));
    const Stroke st = choose(s, maximum(), 1);
    REQUIRE_FALSE(st.decides);
    CHECK(text(st.why, "maximum") == "gone");
    CHECK(text(st.why, "kind") == "pot");
    CHECK(text(st.why, "target") == "blue");
    CHECK(st.input.has_nomination);
    CHECK(st.input.nominated == Ball::blue);
    const Result r = play_stroke(s, st);
    CHECK(scored_clean(r));
    CHECK(r.judgement.points == 5);
}

TEST_CASE("README, the search: why.maximum is on for the 147 at the break-off, the last red and after a red") {
    for (const std::string name : {"racked", "last_red", "after_red"}) {
        CAPTURE(name);
        REQUIRE(maximum_on(named(name)));
        const Stroke& st = chosen(name);
        REQUIRE(st.why.is_object());
        CHECK(text(st.why, "maximum") == "on");
    }
}

TEST_CASE("README, the search: why.maximum is gone for the 147 where a red is already off and no break is on") {
    for (const std::string name : {"straight_red", "cut_red", "long_red", "colours", "black_only", "snookered"}) {
        CAPTURE(name);
        REQUIRE_FALSE(maximum_on(named(name)));
        const Stroke& st = chosen(name);
        CHECK(text(st.why, "maximum") == "gone");
        CHECK(text(st.why, "maximum_why") == "a red is off the table");
    }
}

TEST_CASE("README, the search: why.maximum is off for Beginner everywhere") {
    for (const std::string name : kPositions) {
        CAPTURE(name);
        const Stroke st = choose(named(name), rig::levels().beginner, 1, Think{});
        if (st.decides) continue;
        CHECK(text(st.why, "maximum") == "off");
    }
}

TEST_CASE("README, the 147: from the break-off rack it plays a break-off from the D that is no foul") {
    const FrameState s = named("racked");
    const Stroke& st = chosen("racked");
    REQUIRE_FALSE(st.decides);
    CHECK(text(st.why, "kind") == "break");
    CHECK(st.places_cue);
    const Result r = play_stroke(s, st);
    CHECK(r.placed);
    CHECK(r.judgement.fouls.empty());
}

TEST_CASE("README, why: a pot's why has every field, and its band is short, medium or long") {
    std::vector<std::pair<std::string, Stroke>> strokes;
    for (const std::string name : {"straight_red", "after_red", "long_red"}) strokes.emplace_back(name, chosen(name));
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        const Stroke st = choose(named("straight_red"), rig::levels().beginner, seed, Think{});
        if (text(st.why, "kind") == "pot") {
            strokes.emplace_back("straight_red, Beginner", st);
            break;
        }
    }
    REQUIRE(strokes.size() == 4);
    for (const auto& named_stroke : strokes) {
        const std::string& name = named_stroke.first;
        const Stroke& st = named_stroke.second;
        CAPTURE(name);
        REQUIRE(text(st.why, "kind") == "pot");
        for (const char* key : {"target", "pocket", "band", "cut_rad", "chance", "worth", "spec", "leave", "plan",
                                "others", "drawn", "maximum"}) {
            CAPTURE(key);
            REQUIRE(st.why.contains(key));
        }
        const std::string band = text(st.why, "band");
        CHECK((band == "short" || band == "medium" || band == "long"));
        CHECK(st.why["pocket"].is_number_integer());
        CHECK(st.why["plan"].is_array());
        CHECK(st.why["others"].is_array());
        CHECK(st.why["others"].size() <= 3);
        const json& spec = st.why["spec"];
        REQUIRE(spec.is_object());
        for (const char* key : {"speed_m_s", "tip_x", "tip_y", "elevation_rad"}) {
            CAPTURE(key);
            REQUIRE(spec.contains(key));
            REQUIRE(spec[key].is_number());
        }
        CHECK(spec["speed_m_s"].get<double>() == doctest::Approx(st.meant.speed_m_s).epsilon(1e-9));
        CHECK(spec["tip_x"].get<double>() == doctest::Approx(st.meant.tip_x).epsilon(1e-9));
        CHECK(spec["tip_y"].get<double>() == doctest::Approx(st.meant.tip_y).epsilon(1e-9));
        CHECK(spec["elevation_rad"].get<double>() == doctest::Approx(st.meant.elevation_rad).epsilon(1e-9));
    }
}

TEST_CASE("README, the search: the 147's stroke, played, leaves the cue ball where its why says") {
    for (const std::string name : {"straight_red", "after_red", "last_red"}) {
        CAPTURE(name);
        const FrameState s = named(name);
        const Stroke& st = chosen(name);
        REQUIRE_FALSE(st.decides);
        REQUIRE(st.why.contains("leave"));
        REQUIRE(st.why["leave"].contains("cue"));
        const json& cue = st.why["leave"]["cue"];
        REQUIRE(cue.is_array());
        REQUIRE(cue.size() == 2);
        const Result r = play_stroke(s, st);
        REQUIRE(r.placed);
        const FrameState& post = r.judgement.post;
        REQUIRE(post.cue_ball >= 0);
        REQUIRE(std::size_t(post.cue_ball) < post.balls.size());
        const rules::Vec2 at = post.balls[std::size_t(post.cue_ball)].pos;
        CHECK(at.x == doctest::Approx(cue[0].get<double>()).epsilon(1e-9));
        CHECK(at.y == doctest::Approx(cue[1].get<double>()).epsilon(1e-9));
    }
}

TEST_CASE("README, the search: the 147 plays no tip offset beyond its level's max_tip") {
    for (const std::string name : kPositions) {
        CAPTURE(name);
        const Stroke& st = chosen(name);
        if (st.decides || text(st.why, "kind") != "pot") continue;
        CHECK(std::hypot(st.meant.tip_x, st.meant.tip_y) <= maximum().max_tip + 1e-12);
    }
}

TEST_CASE("README, the search: with fifteen reds and the maximum on it pots one red, keeping the maximum") {
    const FrameState s = fifteen_reds();
    REQUIRE(maximum_on(s));
    REQUIRE(has_pot_of(s, Ball::red));
    const Stroke st = choose(s, maximum(), 1);
    REQUIRE_FALSE(st.decides);
    CHECK(text(st.why, "kind") == "pot");
    CHECK(text(st.why, "maximum") == "on");
    CHECK(text(st.why, "target").rfind("red ", 0) == 0);
    const Result r = play_stroke(s, st);
    CHECK(scored_clean(r));
    CHECK(r.judgement.points == 1);
    CHECK(maximum_on(r.judgement.post));
}
