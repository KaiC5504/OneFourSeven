// How the levels play after the statistics check of M4 stage 1: the 147 pots the last black rather
// than claim, does not pot reds two at a time once the maximum is gone, keeps the pace and the
// side for leaves worth them, and Beginner rolls a short pot in. The frames are from that check's
// 147 v Beginner log, the stroke before which each was logged.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/rules/serialize.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::FrameState;
using rules::scenario::ball;

namespace {

// mb.jsonl line 188
const char* const kShortBlack = R"json({"balls":[{"ball":"cue","index":0,"x":1.3939593101084011,"y":0.15916564876076528},{"ball":"red","index":0,"x":0.7800472649551616,"y":0.30576406901489633},{"ball":"red","index":2,"x":0.2050023400429679,"y":0.16063177779879836},{"ball":"red","index":4,"x":0.5013219761452973,"y":0.09921023053092885},{"ball":"red","index":9,"x":1.681296252421753,"y":-0.2251428546229015},{"ball":"red","index":13,"x":0.21909488053238435,"y":0.7454222728730681},{"ball":"yellow","index":0,"x":-1.0474999999999999,"y":-0.292},{"ball":"green","index":0,"x":-1.0474999999999999,"y":0.292},{"ball":"brown","index":0,"x":-1.0474999999999999,"y":0.0},{"ball":"blue","index":0,"x":0.0,"y":0.0},{"ball":"pink","index":0,"x":-0.8842119577740103,"y":0.14780317868772713},{"ball":"black","index":0,"x":1.4605,"y":0.0}],"break_points":73,"concede_any_time":[false,false],"cue_ball":0,"cue_in_hand":false,"decider":0,"end_reason":"none","free_ball":false,"last_miss":false,"last_penalty":0,"miss_run":0,"miss_warned":false,"offender":0,"on":{"kind":"any_colour"},"original":{"balls":[],"cue_ball":0,"cue_in_hand":false,"free_ball":false,"on":{"kind":"red"},"touching":[]},"over":false,"pending":"none","quiet_strokes":0,"rerack_accepted":-1,"rerack_deadline":-1,"respotted_black":false,"restart":false,"scores":[73,0],"seed":"9391409690812996836","stats":[{"fouls":0,"highest_break":73,"misses":0,"points_from_fouls":0,"pot_attempts":19,"pots":19,"shots":19},{"fouls":0,"highest_break":0,"misses":0,"points_from_fouls":0,"pot_attempts":0,"pots":0,"shots":1}],"striker":0,"strokes":20,"touching":[],"winner":-1})json";

// mb.jsonl line 828
const char* const kSlammedBlack = R"json({"balls":[{"ball":"cue","index":0,"x":1.308139487338506,"y":-0.39671911339547383},{"ball":"red","index":0,"x":0.9447344264920339,"y":3.0140989299078003e-06},{"ball":"red","index":1,"x":0.7664542485985798,"y":0.4231246874235995},{"ball":"red","index":2,"x":0.9902696812955063,"y":-0.02612724747616548},{"ball":"red","index":3,"x":1.0339709377043302,"y":0.05755291653087185},{"ball":"red","index":4,"x":1.0360346382792387,"y":0.0030729463821969334},{"ball":"red","index":5,"x":1.0526663623468921,"y":-0.05075688006113864},{"ball":"red","index":6,"x":1.103437716426268,"y":0.1376695258743091},{"ball":"red","index":7,"x":1.0878912234689446,"y":0.034349216311231494},{"ball":"red","index":8,"x":1.0965140926301284,"y":-0.019044463046898514},{"ball":"red","index":9,"x":1.0921354140459965,"y":-0.08538829962348633},{"ball":"red","index":10,"x":1.15913224107554,"y":0.1919699715629901},{"ball":"red","index":11,"x":1.1600824605342563,"y":0.0557611913156819},{"ball":"red","index":12,"x":1.5828425185437884,"y":0.08666894740450938},{"ball":"red","index":13,"x":1.1575072753317441,"y":-0.061585843329580524},{"ball":"yellow","index":0,"x":-1.0474999999999999,"y":-0.292},{"ball":"green","index":0,"x":-1.0474999999999999,"y":0.292},{"ball":"brown","index":0,"x":-1.3353330651505908,"y":0.3753012056501206},{"ball":"blue","index":0,"x":0.0,"y":0.0},{"ball":"pink","index":0,"x":0.8922078689597048,"y":3.527370940591663e-06},{"ball":"black","index":0,"x":1.4605,"y":0.0}],"break_points":1,"concede_any_time":[false,false],"cue_ball":0,"cue_in_hand":false,"decider":0,"end_reason":"none","free_ball":false,"last_miss":false,"last_penalty":0,"miss_run":0,"miss_warned":false,"offender":0,"on":{"kind":"any_colour"},"original":{"balls":[],"cue_ball":0,"cue_in_hand":false,"free_ball":false,"on":{"kind":"red"},"touching":[]},"over":false,"pending":"none","quiet_strokes":0,"rerack_accepted":-1,"rerack_deadline":-1,"respotted_black":false,"restart":false,"scores":[1,0],"seed":"13923813345766992319","stats":[{"fouls":0,"highest_break":1,"misses":0,"points_from_fouls":0,"pot_attempts":1,"pots":1,"shots":1},{"fouls":0,"highest_break":0,"misses":0,"points_from_fouls":0,"pot_attempts":0,"pots":0,"shots":1}],"striker":0,"strokes":2,"touching":[],"winner":-1})json";

// mb.jsonl line 625
const char* const kTwoRedsCut = R"json({"balls":[{"ball":"cue","index":0,"x":1.24936136282127,"y":-0.24292807785802917},{"ball":"red","index":0,"x":0.9086112721630974,"y":0.068423808080096},{"ball":"red","index":1,"x":0.9674405122754356,"y":0.012597720644740516},{"ball":"red","index":2,"x":0.9446306207548202,"y":-0.05695439135786785},{"ball":"red","index":3,"x":1.039104949894839,"y":0.05115229233486836},{"ball":"red","index":4,"x":1.042115086203968,"y":-0.0017389075889664898},{"ball":"red","index":5,"x":1.0354422229630114,"y":-0.06157046712489259},{"ball":"red","index":7,"x":1.0910350357066663,"y":0.02538291407206446},{"ball":"red","index":8,"x":1.0978973157358276,"y":-0.02820615585484946},{"ball":"red","index":9,"x":1.0834276389140995,"y":-0.0912992560102294},{"ball":"red","index":10,"x":0.9875806169715565,"y":0.07662120198359787},{"ball":"red","index":13,"x":1.1683526544374014,"y":-0.08876133872082165},{"ball":"red","index":14,"x":1.2811523316093834,"y":-0.3015352307564219},{"ball":"yellow","index":0,"x":-1.0474999999999999,"y":-0.292},{"ball":"green","index":0,"x":-1.0474999999999999,"y":0.292},{"ball":"brown","index":0,"x":-1.0474999999999999,"y":0.0},{"ball":"blue","index":0,"x":0.0,"y":0.0},{"ball":"pink","index":0,"x":0.8530717016195273,"y":0.004109283577644176},{"ball":"black","index":0,"x":1.4605,"y":0.0}],"break_points":9,"concede_any_time":[false,false],"cue_ball":0,"cue_in_hand":false,"decider":0,"end_reason":"none","free_ball":false,"last_miss":false,"last_penalty":0,"miss_run":0,"miss_warned":false,"offender":1,"on":{"kind":"red"},"original":{"balls":[],"cue_ball":0,"cue_in_hand":false,"free_ball":false,"on":{"kind":"red"},"touching":[]},"over":false,"pending":"none","quiet_strokes":0,"rerack_accepted":-1,"rerack_deadline":-1,"respotted_black":false,"restart":false,"scores":[13,1],"seed":"7959962799974569576","stats":[{"fouls":0,"highest_break":9,"misses":0,"points_from_fouls":4,"pot_attempts":2,"pots":2,"shots":3},{"fouls":1,"highest_break":1,"misses":0,"points_from_fouls":0,"pot_attempts":2,"pots":1,"shots":3}],"striker":0,"strokes":6,"touching":[],"winner":-1})json";

// mb.jsonl line 631
const char* const kTwoRedsFirm = R"json({"balls":[{"ball":"cue","index":0,"x":0.7442456342705989,"y":0.10861517053299635},{"ball":"red","index":1,"x":0.9567325379853789,"y":0.18578865054918473},{"ball":"red","index":3,"x":1.0237046388266626,"y":0.061465470286201584},{"ball":"red","index":4,"x":1.061048444391076,"y":0.011175152604740338},{"ball":"red","index":5,"x":0.9772266969271556,"y":0.0306847954026528},{"ball":"red","index":7,"x":1.1566798297013554,"y":0.43100741655416974},{"ball":"red","index":8,"x":1.1358159559993972,"y":-0.018732815643074698},{"ball":"red","index":9,"x":1.0640278642474041,"y":-0.04298424441395203},{"ball":"red","index":10,"x":1.154825053869442,"y":-0.11916989366274315},{"ball":"red","index":14,"x":1.2811523316093834,"y":-0.3015352307564219},{"ball":"yellow","index":0,"x":-1.0474999999999999,"y":-0.292},{"ball":"green","index":0,"x":0.7716910442969567,"y":-0.8339680916746438},{"ball":"brown","index":0,"x":-1.0474999999999999,"y":0.0},{"ball":"blue","index":0,"x":0.0,"y":0.0},{"ball":"pink","index":0,"x":0.89225,"y":0.0},{"ball":"black","index":0,"x":1.4916973724386824,"y":-0.12637859147709074}],"break_points":18,"concede_any_time":[false,false],"cue_ball":0,"cue_in_hand":false,"decider":0,"end_reason":"none","free_ball":false,"last_miss":false,"last_penalty":0,"miss_run":0,"miss_warned":false,"offender":1,"on":{"kind":"red"},"original":{"balls":[],"cue_ball":0,"cue_in_hand":false,"free_ball":false,"on":{"kind":"red"},"touching":[]},"over":false,"pending":"none","quiet_strokes":0,"rerack_accepted":-1,"rerack_deadline":-1,"respotted_black":false,"restart":false,"scores":[22,1],"seed":"7959962799974569576","stats":[{"fouls":0,"highest_break":18,"misses":0,"points_from_fouls":4,"pot_attempts":4,"pots":4,"shots":5},{"fouls":1,"highest_break":1,"misses":0,"points_from_fouls":0,"pot_attempts":2,"pots":1,"shots":3}],"striker":0,"strokes":8,"touching":[],"winner":-1})json";


FrameState frame_of(const char* text) {
    FrameState s;
    std::string error;
    REQUIRE(rules::frame_from_json(nlohmann::json::parse(text), s, error));
    return s;
}

Stroke choose(const FrameState& s, const Level& level, int sims, std::uint64_t seed = 1) {
    Rng rng(seed);
    Think t;
    t.max_sims = sims;
    return choose_stroke(s, level, rig::env(), rng, t);
}

int reds(const std::vector<rules::BallOnTable>& balls) {
    return int(std::count_if(balls.begin(), balls.end(), [](const rules::BallOnTable& b) { return b.ball == Ball::red; }));
}

// The black alone on its spot, the striker 60 ahead with the maximum long gone.
FrameState black_alone(rules::Vec2 cue) {
    FrameState s = rules::scenario::frame({ball(Ball::cue, cue.x, cue.y), ball(Ball::black, 1.4605, 0.0)});
    s.on = {rules::OnKind::colour, Ball::black};
    s.scores = {70, 10};
    s.break_points = 12;
    return s;
}

bool claim_open(const FrameState& s) {
    const std::vector<rules::DecisionKind> open = rules::options(s, s.striker);
    return std::find(open.begin(), open.end(), rules::DecisionKind::claim_frame) != open.end();
}

}  // namespace

TEST_CASE("with the maximum gone the 147 pots the last black rather than claim; Beginner claims") {
    const FrameState s = black_alone({1.0, 0.25});
    REQUIRE(claim_open(s));
    REQUIRE_FALSE(pots(s, rig::env(), rig::levels().maximum.max_cut_rad).empty());
    const Stroke maximum = choose(s, rig::levels().maximum, 1500);
    CHECK_FALSE(maximum.decides);
    CHECK(maximum.why.value("kind", std::string()) == "pot");
    const Stroke beginner = choose(s, rig::levels().beginner, 150);
    CHECK(beginner.decides);
    CHECK(beginner.decision == rules::DecisionKind::claim_frame);
}

TEST_CASE("with no pot of the last black the 147 claims the frame it has won") {
    // The cue ball touching the black: it may only be played away from, so there is no pot.
    FrameState s = black_alone({1.4605 - 2.0 * table::kBallRadius, 0.0});
    s.touching = rig::env().oracle.touching(s.balls, s.cue_ball);
    REQUIRE(claim_open(s));
    REQUIRE(pots(s, rig::env(), rig::levels().maximum.max_cut_rad).empty());
    const Stroke st = choose(s, rig::levels().maximum, 1500);
    CHECK(st.decides);
    CHECK(st.decision == rules::DecisionKind::claim_frame);
}

TEST_CASE("with the maximum gone the 147 does not pot two reds in one stroke where it smashed them before") {
    for (const char* text : {kTwoRedsCut, kTwoRedsFirm}) {
        const FrameState s = frame_of(text);
        const Stroke st = choose(s, rig::levels().maximum, 1500);
        REQUIRE(st.why.value("kind", std::string()) == "pot");
        CHECK(st.why.value("maximum", std::string()) == "gone");
        FrameState from = s;
        std::string error;
        if (st.places_cue) REQUIRE(rules::place_cue_ball(from, st.cue_pos, rig::env().oracle, error));
        const rules::scenario::Played played = rules::scenario::play(from, st.strike, st.input);
        CAPTURE(st.strike.speed_m_s);
        CHECK(reds(s.balls) - reds(played.judgement.post.balls) <= 1);
    }
}

TEST_CASE("the 147 plays a short black at a pace and with side only a better leave pays for") {
    // Before: 5.5 m/s from 17 cm with full side, and the 6.2 m/s cap on a near-straight black.
    for (const char* text : {kShortBlack, kSlammedBlack}) {
        const FrameState s = frame_of(text);
        const Stroke st = choose(s, rig::levels().maximum, 1500);
        REQUIRE(st.why.value("kind", std::string()) == "pot");
        CAPTURE(st.why.dump());
        CHECK(st.meant.speed_m_s < 4.0);
    }
}

TEST_CASE("Beginner rolls a short straight pot in rather than hit it firm") {
    // A red 12 cm in front of the cue ball, straight into the top left corner.
    const double c = std::sqrt(0.5);
    const rules::Vec2 red{1.35, 0.45};
    FrameState s = rules::scenario::frame({ball(Ball::cue, red.x - 0.12 * c, red.y - 0.12 * c), ball(Ball::red, red.x, red.y, 0),
                                           ball(Ball::black, 1.4605, 0.0)});
    std::vector<double> speeds;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        const Stroke st = choose(s, rig::levels().beginner, 150, seed);
        if (st.why.value("kind", std::string()) == "pot" && st.why.value("target", std::string()) == "red 0") {
            speeds.push_back(st.meant.speed_m_s);
        }
    }
    REQUIRE(speeds.size() >= 10);
    std::sort(speeds.begin(), speeds.end());
    // Before, nothing under 2.6 m/s, the softer of its two firm speeds; now a roll at about 0.8
    // is one of its two, and its misreading of the two picks either.
    CHECK(speeds.front() < 1.0);
    CHECK(speeds[speeds.size() / 2] < 2.0);
}
