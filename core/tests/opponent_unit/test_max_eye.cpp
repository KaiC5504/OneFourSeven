// The 147 aims perfectly, but among plans worth the same it takes the pot a good player would: each
// pot it plans is charged for the chance the person reader (core/data/opponent_person.json) would
// not give it, never so much that it gives up a maximum for an easier ball.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"

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

const Level& person() {
    static const Level l = [] {
        Level out;
        std::vector<std::string> problems;
        load_level(rules::scenario::data_file("opponent_person.json"), out, problems);
        REQUIRE(problems.empty());
        return out;
    }();
    return l;
}

Stroke choose(const FrameState& s, int sims, std::uint64_t seed = 1) {
    Think think;
    think.max_sims = sims;
    Rng rng(seed);
    return choose_stroke(s, maximum(), rig::env(), rng, think);
}

std::string label(const FrameState& s, int place) {
    const BallOnTable& b = s.balls[std::size_t(place)];
    std::string out(table::ball_name(b.ball));
    if (b.ball == Ball::red) out += " " + std::to_string(b.index);
    return out;
}

// The person's chance of the listed pot of `target` into `pocket`; -1 when none is listed.
double eye(const FrameState& s, const std::string& target, int pocket) {
    for (const Pot& p : pots(s, rig::env(), maximum().max_cut_rad)) {
        if (label(s, p.target) == target && p.pocket == pocket) return pot_chance(person(), p, Spec{}, p.margin_rad);
    }
    return -1.0;
}

bool baulk_corner(int pocket) {
    const phys::Pocket& p = rig::env().table.pockets[std::size_t(pocket)];
    return p.corner && p.drop_centre.x < 0.0;
}

// A 96 break with three reds left, from the 147's own frames against Beginner: red 7 lies by the
// left middle pocket and along the left cushion towards the bottom left corner. In stage 1 it sent
// it the length of the table into a baulk corner, a pot the person gives one chance in a hundred.
FrameState red_by_the_middle() {
    std::vector<BallOnTable> balls{ball(Ball::cue, 1.461963811130569, -0.03685822547530867),
                                   ball(Ball::red, 0.08045413387162412, 0.8110549566227563, 7),
                                   ball(Ball::red, 1.0338195523820624, 0.6930895394057981, 10),
                                   ball(Ball::red, 0.39668174016692564, 0.7514223643800774, 11),
                                   spotted(Ball::yellow),
                                   spotted(Ball::green),
                                   spotted(Ball::brown),
                                   spotted(Ball::blue),
                                   ball(Ball::pink, -0.5238626304158587, -0.16964950699369494),
                                   ball(Ball::black, 0.89225, 0.0)};
    FrameState s = rules::scenario::frame(std::move(balls));
    s.break_points = 96;
    s.scores = {96, 0};
    return s;
}

}  // namespace

TEST_CASE("between pots worth the same otherwise the 147 takes the one a person would") {
    const FrameState s = red_by_the_middle();
    REQUIRE(maximum_on(s));
    const Stroke st = choose(s, 1500);
    REQUIRE_FALSE(st.decides);
    REQUIRE(st.why.value("kind", std::string()) == "pot");
    const std::string target = st.why["target"].get<std::string>();
    const int pocket = st.why["pocket"].get<int>();
    CHECK_FALSE(baulk_corner(pocket));
    const double chosen = eye(s, target, pocket);
    CHECK(chosen > 0.9);

    // A baulk corner was among its plans, worth less only by about what the person's eye charges
    // it: a point for a pot the person would never make.
    const double worth = st.why["worth"].get<double>();
    bool compared = false;
    for (const nlohmann::json& other : st.why["others"]) {
        const int its = other["pocket"].get<int>();
        if (!baulk_corner(its)) continue;
        const double other_eye = eye(s, other["target"].get<std::string>(), its);
        CHECK(other_eye < chosen);
        CHECK(other["worth"].get<double>() < worth);
        CHECK(other["worth"].get<double>() + (1.0 - other_eye) > worth - 0.25);
        compared = true;
    }
    CHECK(compared);

    const rules::Judgement j = rules::scenario::play(s, st.strike, st.input).judgement;
    CHECK(j.fouls.empty());
    CHECK(j.points == 1);
    CHECK(maximum_on(j.post));
}

TEST_CASE("the person's eye never buys an easier colour at the cost of the maximum") {
    // After a red, the maximum on: the black a cut to a top corner, the pink straight into the
    // right middle pocket from close by.
    std::vector<BallOnTable> balls{ball(Ball::cue, 0.0, -0.35),  spotted(Ball::yellow), spotted(Ball::green),
                                   spotted(Ball::brown),         spotted(Ball::blue),   ball(Ball::pink, 0.0, -0.7),
                                   spotted(Ball::black)};
    for (int i = 0; i < 14; ++i) balls.push_back(ball(Ball::red, -1.5, -0.65 + 0.1 * i, i));
    FrameState s = rules::scenario::frame(std::move(balls));
    s.on = {OnKind::any_colour, Ball::yellow};
    s.break_points = 1;
    s.scores = {1, 0};
    REQUIRE(maximum_on(s));

    double best_black = -1.0;
    double best_pink = -1.0;
    for (const Pot& p : pots(s, rig::env(), maximum().max_cut_rad)) {
        const double c = pot_chance(person(), p, Spec{}, p.margin_rad);
        const Ball b = s.balls[std::size_t(p.target)].ball;
        if (b == Ball::black) best_black = std::max(best_black, c);
        if (b == Ball::pink) best_pink = std::max(best_pink, c);
    }
    REQUIRE(best_black >= 0.0);
    REQUIRE(best_pink > best_black);

    const Stroke st = choose(s, 1500);
    REQUIRE_FALSE(st.decides);
    CHECK(st.why.value("kind", std::string()) == "pot");
    CHECK(st.why.value("target", std::string()) == "black");
    CHECK(st.input.nominated == Ball::black);
    const rules::Judgement j = rules::scenario::play(s, st.strike, st.input).judgement;
    CHECK(j.fouls.empty());
    CHECK(j.points == 7);
    CHECK(maximum_on(j.post));
}
