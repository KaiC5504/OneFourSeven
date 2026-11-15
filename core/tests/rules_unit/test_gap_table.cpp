#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "ofs/rules/rules_log.hpp"
#include "test_standin_support.hpp"

using namespace ofs;
using namespace ofs::opponent;
using namespace ofs::opponent::standin_test;
using rules::DecisionKind;
using rules::Judgement;
using rules::Pending;
using rules::ShotFacts;
using rules::scenario::ball;
using rules::scenario::spotted;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Moves the cue ball against `target` at a free angle on the cloth, within the touching gap.
bool cue_against(FrameState& s, int target, Rng& rng) {
    const Vec2 at = s.balls[std::size_t(target)].pos;
    for (int tries = 0; tries < 64; ++tries) {
        const double a = rng.uniform(0.0, 2.0 * kPi);
        const Vec2 p{at.x + (kD + 2.0e-6) * std::cos(a), at.y + (kD + 2.0e-6) * std::sin(a)};
        if (std::fabs(p.x) > 1.7 || std::fabs(p.y) > 0.8) continue;
        bool clear = true;
        for (std::size_t i = 0; i < s.balls.size(); ++i) {
            if (int(i) == s.cue_ball || int(i) == target) continue;
            if (std::hypot(p.x - s.balls[i].pos.x, p.y - s.balls[i].pos.y) <= kD + 0.002) clear = false;
        }
        if (!clear) continue;
        s.balls[std::size_t(s.cue_ball)].pos = p;
        return true;
    }
    return false;
}

// What judge() would have called TOUCHING BALL on for this frame (S3.8(a)).
void call_touching(FrameState& s) {
    s.touching.clear();
    for (int place : rig().oracle->touching(s.balls, s.cue_ball)) {
        const Ball b = s.balls[std::size_t(place)].ball;
        const bool could = s.free_ball || (s.on.kind == OnKind::red && b == Ball::red) ||
                           (s.on.kind == OnKind::any_colour && b != Ball::red) ||
                           (s.on.kind == OnKind::colour && b == s.on.colour);
        if (could) s.touching.push_back(place);
    }
}

const char* const kKinds[] = {"touching a red", "colour of choice touching a colour", "free ball touching a colour",
                              "free ball in hand", "re-spotted black in hand"};

// Position `i` of kind `kind`; false when the layout left no room to set it up.
bool awkward(int kind, int i, Rng& rng, FrameState& s) {
    switch (kind) {
        case 0:
        case 1:
        case 2: {
            s = scattered(rng, 1 + i % 10);
            if (kind == 1) s.on = {OnKind::any_colour, Ball::yellow};
            if (kind == 2) s.free_ball = true;
            int target = -1;
            for (std::size_t p = 0; p < s.balls.size(); ++p) {
                const bool red = s.balls[p].ball == Ball::red;
                if (int(p) != s.cue_ball && (kind == 0 ? red : !red && s.balls[p].ball != Ball::cue)) {
                    target = int(p);
                    if (rng.uniform(0.0, 1.0) < 0.5) break;
                }
            }
            if (target < 0 || !cue_against(s, target, rng)) return false;
            call_touching(s);
            return !s.touching.empty();
        }
        case 3:
            s = in_hand(snookered(rng));
            s.free_ball = true;
            return true;
        default: {
            s = colours_only(rng, Ball::black);
            const int black = rules::scenario::place_of(s, Ball::black);
            s.balls[std::size_t(black)].pos = table::spot(Ball::black);
            s.respotted_black = true;
            s.scores = {50 + i % 20, 50 + i % 20};
            s = in_hand(s);
            return true;
        }
    }
}

}  // namespace

TEST_CASE("gap stand-in: touching, free-ball-in-hand and re-spotted-black positions get strokes the engine takes") {
    Rng layouts(20261116);
    int positions = 0;
    std::map<int, int> pushes;
    for (int kind = 0; kind < 5; ++kind) {
        for (int i = 0; i < 30; ++i) {
            CAPTURE(kKinds[kind]);
            CAPTURE(i);
            FrameState s;
            if (!awkward(kind, i, layouts, s)) continue;
            s.striker = i % 2;
            s.seed = 7000 + std::uint64_t(i);
            ++positions;
            const auto sink = capture_log();
            Rng rng(977 * std::uint64_t(i) + std::uint64_t(kind));
            const StandinShot shot = choose(s, standin_params(), rng);
            FrameState placed;
            const std::string why_not = refusal(s, shot, placed);
            CHECK_MESSAGE(why_not.empty(), why_not);
            if (shot.decides || !why_not.empty()) {
                log::Logger::instance().clear_sinks();
                continue;
            }
            const auto played = play(placed, shot, std::uint64_t(i) + 1);
            CHECK(played.judgement.post.strokes == s.strokes + 1);
            CHECK_FALSE(played.facts.anomaly);
            CHECK(sink->count("rules.invalid") == 0);
            for (const rules::Foul& f : played.judgement.fouls) {
                if (f.kind == rules::FoulKind::push_stroke) ++pushes[kind];
            }
            log::Logger::instance().clear_sinks();
        }
    }
    log::Logger::instance().clear_sinks();
    CHECK(positions >= 120);
    // S3.8(b): it plays away from a touching ball rather than through it.
    for (const auto& [kind, n] : pushes) CHECK_MESSAGE(n == 0, kKinds[kind], ": ", n, " push strokes of 30");
}

TEST_CASE("gap spec 16.10: strokes and choices played on from awkward positions re-judge from their log lines") {
    Rng layouts(20261117);
    int judged = 0;
    for (int kind = 0; kind < 5; ++kind) {
        for (int i = 0; i < 12; ++i) {
            FrameState s;
            if (!awkward(kind, i, layouts, s)) continue;
            s.striker = i % 2;
            s.seed = 5000 + std::uint64_t(i);
            Rng rng(31 * std::uint64_t(i) + std::uint64_t(kind));
            for (int stroke = 0; stroke < 4 && !s.over && !s.restart; ++stroke) {
                CAPTURE(kKinds[kind]);
                CAPTURE(i);
                CAPTURE(stroke);
                while (s.pending != Pending::none && !s.over) {
                    const std::vector<DecisionKind> open = rules::options(s, s.decider);
                    REQUIRE_FALSE(open.empty());
                    const DecisionKind k = choose_decision(s, s.decider, open, standin_params(), rng);
                    std::vector<rules::Call> calls;
                    std::string error;
                    REQUIRE_MESSAGE(rules::decide(s, {k, s.decider}, *rig().oracle, rig().ref, calls, error), error);
                }
                if (s.over || s.restart) break;
                const StandinShot shot = choose(s, standin_params(), rng);
                if (shot.decides) break;
                FrameState placed;
                REQUIRE_MESSAGE(refusal(s, shot, placed).empty(), refusal(s, shot, placed));
                const auto played = play(placed, shot, rng.next_u64());

                const auto sink = capture_log();
                rules::log_decision(placed, shot.input, played.facts, played.judgement, rig().ref,
                                    {{"shot_id", "gap"}});
                log::Logger::instance().clear_sinks();
                REQUIRE(sink->events.size() == 1);
                log::Event back;
                REQUIRE(log::from_jsonl(log::to_jsonl(sink->events[0]), back));
                CHECK_MESSAGE(rules::rejudge(back.data, rig().table).empty(), rules::rejudge(back.data, rig().table));
                ++judged;
                s = played.judgement.post;
            }
        }
    }
    CHECK(judged >= 150);
}

TEST_CASE("gap stand-in: every choice it makes at a pending decision is one offered, over 300 seeds") {
    Rng layouts(4711);
    int asked = 0;
    for (int i = 0; i < 300; ++i) {
        CAPTURE(i);
        FrameState s = scattered(layouts, 1 + i % 15);
        s.scores = {i % 90, (i * 7) % 90};
        s.striker = i % 2;
        switch (i % 4) {
            case 0: {
                // A real foul from the position: nothing hit, judged on the real table.
                ShotFacts quiet = rules::scenario::quiet_facts(s);
                s = rules::judge(s, {}, quiet, *rig().oracle, rig().ref).post;
                break;
            }
            case 1:
                s.pending = Pending::rerack;
                s.decider = i % 2;
                s.rerack_accepted = i % 3 == 0 ? 1 - s.decider : -1;
                break;
            case 2:
                s = colours_only(layouts, Ball::black);
                s.respotted_black = true;
                s.pending = Pending::toss;
                s.decider = i % 2;
                s = in_hand(s);
                break;
            default:
                s.pending = Pending::after_foul;
                s.decider = 1 - s.striker;
                s.offender = s.striker;
                s.last_penalty = 4;
                s.last_miss = i % 3 != 0;
                s.original = static_cast<const rules::Position&>(s);
                break;
        }
        REQUIRE(s.pending != Pending::none);
        for (int player = 0; player < 2; ++player) {
            const std::vector<DecisionKind> open = rules::options(s, player);
            if (open.empty()) continue;
            ++asked;
            Rng rng(std::uint64_t(i) * 2 + std::uint64_t(player));
            const DecisionKind k = choose_decision(s, player, open, standin_params(), rng);
            CHECK_MESSAGE(std::find(open.begin(), open.end(), k) != open.end(), rules::decision_name(k));
            FrameState applied = s;
            std::vector<rules::Call> calls;
            std::string error;
            CHECK_MESSAGE(rules::decide(applied, {k, player}, *rig().oracle, rig().ref, calls, error), error);
        }
    }
    CHECK(asked >= 300);
}

TEST_CASE("gap S3.5: the cue ball may go on the yellow spot once the yellow is off the table, not while it is on") {
    const rules::scenario::Rig& r = rig();
    std::vector<BallOnTable> balls{ball(Ball::cue, -1.2, 0.1)};
    for (Ball colour : {Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        balls.push_back(spotted(colour));
    }
    FrameState gone = rules::scenario::frame(balls);
    gone.cue_in_hand = true;
    std::string error;
    CHECK_MESSAGE(rules::place_cue_ball(gone, table::spot(Ball::yellow), *r.oracle, error), error);
    CHECK(gone.balls[0].pos.x == table::spot(Ball::yellow).x);
    CHECK(gone.balls[0].pos.y == table::spot(Ball::yellow).y);

    balls.push_back(spotted(Ball::yellow));
    FrameState there = rules::scenario::frame(balls);
    there.cue_in_hand = true;
    CHECK_FALSE(rules::place_cue_ball(there, table::spot(Ball::yellow), *r.oracle, error));
    CHECK_FALSE(rules::place_cue_ball(there, table::spot(Ball::green), *r.oracle, error));
    CHECK(there.balls[0].pos.x == -1.2);
    const Vec2 near = r.oracle->nearest_cue_placement(there.balls, 0, table::spot(Ball::yellow));
    CHECK(r.oracle->cue_placement_legal(there.balls, 0, near));
    CHECK(std::hypot(near.x - table::spot(Ball::yellow).x, near.y - table::spot(Ball::yellow).y) >= kD);
}

TEST_CASE("gap S3.7(e) through the engine: a colour whose spot the cue ball covers goes to the highest spot free") {
    const rules::scenario::Rig& r = rig();
    std::vector<BallOnTable> balls{ball(Ball::cue, table::spot(Ball::brown).x, table::spot(Ball::brown).y),
                                   ball(Ball::red, 0.5, 0.5),
                                   spotted(Ball::yellow),
                                   spotted(Ball::green),
                                   ball(Ball::brown, -0.4, -0.5),
                                   spotted(Ball::blue),
                                   spotted(Ball::pink),
                                   ball(Ball::black, 1.0, 0.6)};
    const FrameState s = rules::scenario::frame(balls);
    ShotFacts f = rules::scenario::quiet_facts(s);
    f.first_hit = {4};
    f.first_hit_t = 0.4;
    f.closest_gap_m[4] = 0.0;
    f.pots.push_back({4, 0, 1.0});
    const Judgement j = rules::judge(s, {}, f, *r.oracle, r.ref);
    REQUIRE_FALSE(j.fouls.empty());
    const int brown = rules::scenario::place_of(j.post, Ball::brown);
    REQUIRE(brown >= 0);
    CHECK(j.post.balls[std::size_t(brown)].pos.x == table::spot(Ball::black).x);
    CHECK(j.post.balls[std::size_t(brown)].pos.y == table::spot(Ball::black).y);
    CHECK_FALSE(j.post.cue_in_hand);
}

TEST_CASE("gap S3.7, S3.5: after an in-off that pots the yellow, the cue ball in hand is clear of the spotted yellow") {
    const rules::scenario::Rig& r = rig();
    std::vector<BallOnTable> balls{ball(Ball::cue, 0.0, 0.5), ball(Ball::red, 0.6, 0.5),
                                   ball(Ball::yellow, 0.3, -0.5)};
    for (Ball colour : {Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black}) {
        balls.push_back(spotted(colour));
    }
    const FrameState s = rules::scenario::frame(balls);
    ShotFacts f = rules::scenario::quiet_facts(s);
    f.first_hit = {2};
    f.first_hit_t = 0.4;
    f.closest_gap_m[2] = 0.0;
    f.pots.push_back({2, 0, 1.0});
    f.cue_potted = true;
    f.cue_pocket = 1;
    const Judgement j = rules::judge(s, {}, f, *r.oracle, r.ref);
    REQUIRE(j.post.cue_in_hand);
    const int yellow = rules::scenario::place_of(j.post, Ball::yellow);
    REQUIRE(yellow >= 0);
    CHECK(j.post.balls[std::size_t(yellow)].pos.x == table::spot(Ball::yellow).x);
    const Vec2 cue = j.post.balls[std::size_t(j.post.cue_ball)].pos;
    CHECK(table::in_d(cue));
    CHECK(r.oracle->cue_placement_legal(j.post.balls, j.post.cue_ball, cue));
    for (std::size_t i = 0; i < j.post.balls.size(); ++i) {
        if (int(i) == j.post.cue_ball) continue;
        CHECK(std::hypot(j.post.balls[i].pos.x - cue.x, j.post.balls[i].pos.y - cue.y) > kD);
    }
}
