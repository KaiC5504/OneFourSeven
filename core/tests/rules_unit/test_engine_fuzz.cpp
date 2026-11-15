#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ofs/rng.hpp"
#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;
namespace table = ofs::table;

namespace {

bool chance(ofs::Rng& rng, double p) { return rng.uniform() < p; }

int pick(ofs::Rng& rng, int count) { return int(rng.next_u64() % std::uint64_t(count)); }

// Whatever a stroke could do, likely or not: the rules must make sense of all of it.
ShotFacts random_facts(const FrameState& s, const FakeOracle& oracle, double pot_rate, ofs::Rng& rng) {
    ShotFacts f = scenario::quiet_facts(s);
    const int n = int(s.balls.size());
    std::vector<int> objects;
    for (int i = 0; i < n; ++i) {
        if (i != s.cue_ball) objects.push_back(i);
    }
    const std::vector<int> on = places_on(s);
    if (!objects.empty() && !chance(rng, 0.12)) {
        const int first = !on.empty() && chance(rng, 0.7) ? on[std::size_t(pick(rng, int(on.size())))]
                                                          : objects[std::size_t(pick(rng, int(objects.size())))];
        f.first_hit = {first};
        f.first_hit_t = 0.3;
        if (chance(rng, 0.05)) {
            const int second = objects[std::size_t(pick(rng, int(objects.size())))];
            if (second != first) f.first_hit.push_back(second);
        }
        if (chance(rng, pot_rate * 8.0)) f.pots.push_back({first, pick(rng, 6), 1.0});
    }
    for (int place : objects) {
        if (chance(rng, pot_rate) && (f.pots.empty() || f.pots.front().ball != place)) {
            f.pots.push_back({place, pick(rng, 6), 2.0});
        }
        if (chance(rng, 0.2)) f.rest[std::size_t(place)] = {rng.uniform(-1.7, 1.7), rng.uniform(-0.8, 0.8)};
        f.closest_gap_m[std::size_t(place)] = rng.uniform(0.0, 0.5);
    }
    f.rest[std::size_t(s.cue_ball)] = {rng.uniform(-1.7, 1.7), rng.uniform(-0.8, 0.8)};
    if (chance(rng, 0.03)) {
        // Now and then the cue ball stops against a ball.
        const Vec2 pos = f.rest[std::size_t(objects.empty() ? s.cue_ball : objects.front())];
        f.rest[std::size_t(s.cue_ball)] = {pos.x, pos.y - table::kBallDiameter};
    }
    for (int place : oracle.touching(s.balls, s.cue_ball)) {
        if (chance(rng, 0.5)) f.touching_moved.push_back(place);
    }
    f.cue_potted = chance(rng, 0.05);
    f.push = !f.first_hit.empty() && chance(rng, 0.03);
    f.pot_attempt = chance(rng, 0.4);
    f.object_travel_m = chance(rng, 0.8) ? 0.1 : 1.2;
    f.cue_travel_m = rng.uniform(0.0, 3.0);
    return f;
}

ShotInput random_input(const FrameState& s, ofs::Rng& rng) {
    const std::vector<Ball> open = nominations(s);
    if (!open.empty() && chance(rng, 0.5)) return {true, open[std::size_t(pick(rng, int(open.size())))]};
    if (chance(rng, 0.05)) return {true, Ball(pick(rng, 8))};
    return {};
}

void check_table(const FrameState& s) {
    const int n = int(s.balls.size());
    REQUIRE(s.cue_ball >= 0);
    REQUIRE(s.cue_ball < n);
    CHECK(s.balls[std::size_t(s.cue_ball)].ball == Ball::cue);
    CHECK(balls_count(s, Ball::cue) == 1);
    for (Ball colour : kColours) CHECK(balls_count(s, colour) <= 1);
    for (int place : s.touching) {
        CHECK(place >= 0);
        CHECK(place < n);
        CHECK(place != s.cue_ball);
    }
    CHECK((s.striker == 0 || s.striker == 1));
    CHECK((s.decider == 0 || s.decider == 1));
    CHECK((s.offender == 0 || s.offender == 1));
    if (s.over) {
        CHECK((s.winner == 0 || s.winner == 1));
        CHECK(s.pending == Pending::none);
        CHECK(options(s, 0).empty());
        CHECK(options(s, 1).empty());
        return;
    }
    CHECK(s.winner == -1);
    if (balls_count(s, Ball::red) > 0) {
        for (Ball colour : kColours) CHECK(balls_count(s, colour) == 1);
        CHECK(s.on.kind != OnKind::colour);
    } else if (s.on.kind == OnKind::colour) {
        for (Ball colour : kColours) {
            if (table::ball_value(colour) < table::ball_value(s.on.colour)) CHECK(balls_count(s, colour) == 0);
        }
        CHECK(balls_count(s, s.on.colour) == 1);
    }
    if (s.pending != Pending::none) {
        CHECK_FALSE(options(s, s.decider).empty());
        CHECK(options(s, 1 - s.decider).empty());
    }
    if (s.pending == Pending::after_foul) {
        CHECK(s.striker == s.decider);
        CHECK(s.offender == 1 - s.decider);
    }
    if (s.free_ball) CHECK_FALSE(nominations(s).empty());
}

void check_judgement(const FrameState& pre, const Judgement& j) {
    const FrameState& post = j.post;
    CHECK(post.strokes == pre.strokes + 1);
    CHECK(post.balls.size() <= pre.balls.size() + 1);
    CHECK(post.scores[0] >= pre.scores[0]);
    CHECK(post.scores[1] >= pre.scores[1]);
    CHECK(post.scores[0] + post.scores[1] == pre.scores[0] + pre.scores[1] + j.points + j.penalty);
    CHECK((j.points == 0 || j.penalty == 0));
    CHECK(j.fouls.empty() == (j.penalty == 0));
    for (std::size_t i = 0; i < j.fouls.size(); ++i) {
        CHECK(j.fouls[i].value >= 4);
        CHECK(j.fouls[i].value <= 7);
        if (i > 0) CHECK(j.fouls[i - 1].value >= j.fouls[i].value);
    }
    if (!j.fouls.empty()) CHECK(j.penalty == j.fouls.front().value);
    if (j.miss) {
        CHECK_FALSE(j.fouls.empty());
        CHECK_FALSE(j.miss_reason.empty());
    }
    for (std::size_t i = 1; i < j.calls.size(); ++i) CHECK(j.calls[i - 1].kind < j.calls[i].kind);
    CHECK(has_call(j.calls, CallKind::foul) == !j.fouls.empty());
    CHECK(has_call(j.calls, CallKind::frame_over) == post.over);
    if (post.pending == Pending::after_foul) {
        CHECK(post.last_penalty == j.penalty);
        CHECK(post.last_miss == j.miss);
        CHECK(same(post.original, pre));
        CHECK(post.offender == pre.striker);
    }
    if (post.free_ball) CHECK(post.pending == Pending::after_foul);
}

}  // namespace

TEST_CASE("frames of random strokes and choices keep every invariant and judge the same twice") {
    ofs::Rng rng(20261115);
    const RefParams ref;
    int finished = 0;
    int reracked = 0;
    int respotted = 0;
    for (int frame = 0; frame < 300; ++frame) {
        // Some frames start on the black a pot or a foul from level, for the re-spotted black; some
        // are all safeties, for the stalemates.
        const bool on_black = chance(rng, 0.15);
        FrameState s = on_black ? colours_left({Ball::black}) : open_table(pick(rng, 16));
        if (on_black) s.scores = {40, 47};
        s.striker = pick(rng, 2);
        s.seed = rng.next_u64();
        s.concede_any_time = {chance(rng, 0.5), false};
        s.cue_in_hand = true;
        const double pot_rate = chance(rng, 0.3) ? 0.0 : 0.03;
        for (int step = 0; step < 500 && !s.over && !s.restart; ++step) {
            FakeOracle oracle;
            oracle.is_snookered = chance(rng, 0.25);
            oracle.snookerers = {pick(rng, 12)};
            oracle.has_clear_path = chance(rng, 0.6);
            oracle.has_full_ball = chance(rng, 0.6);
            oracle.reachable = chance(rng, 0.9);
            if (s.pending != Pending::none) {
                std::vector<DecisionKind> open = options(s, s.decider);
                REQUIRE_FALSE(open.empty());
                std::vector<Call> calls;
                std::string error;
                const DecisionKind kind = open[std::size_t(pick(rng, int(open.size())))];
                REQUIRE(decide(s, {kind, s.decider}, oracle, ref, calls, error));
                CHECK(has_call(calls, CallKind::frame_over) == s.over);
            } else if (chance(rng, 0.002) && !options(s, s.striker).empty()) {
                const std::vector<DecisionKind> open = options(s, s.striker);
                std::vector<Call> calls;
                std::string error;
                REQUIRE(decide(s, {open.front(), s.striker}, oracle, ref, calls, error));
            } else {
                if (s.cue_in_hand) {
                    std::string error;
                    place_cue_ball(s, {table::baulk_line_x() - rng.uniform(0.0, 0.3), rng.uniform(-0.3, 0.3)}, oracle,
                                   error);
                }
                const ShotFacts facts = random_facts(s, oracle, pot_rate, rng);
                const ShotInput input = random_input(s, rng);
                const Judgement j = judge(s, input, facts, oracle, ref);
                const Judgement again = judge(s, input, facts, oracle, ref);
                CHECK(same(j.post, again.post));
                check_judgement(s, j);
                respotted += has_call(j.calls, CallKind::respotted_black) ? 1 : 0;
                s = j.post;
            }
            check_table(s);
        }
        finished += s.over ? 1 : 0;
        reracked += s.restart ? 1 : 0;
    }
    CHECK(finished > 100);
    CHECK(reracked > 5);
    CHECK(respotted > 5);
}
