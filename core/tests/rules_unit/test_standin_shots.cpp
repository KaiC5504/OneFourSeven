#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <string>

#include "test_standin_support.hpp"

using namespace ofs;
using namespace ofs::opponent;
using namespace ofs::opponent::standin_test;
using rules::DecisionKind;
using rules::scenario::ball;
using rules::scenario::spotted;

namespace {

const char* const kLayouts[] = {"opening rack", "mid-frame", "colours only", "in hand",
                                "snookered",    "colour of choice", "free ball", "final black"};
constexpr int kLayoutCount = 8;

// Position `i` of a kind, from the layouts' own stream.
FrameState position(int layout, int i, Rng& rng) {
    FrameState s;
    switch (layout) {
        case 0: {
            const std::uint64_t seed = 100 + std::uint64_t(i);
            s = rules::new_frame(i % 2, phys::opening_rack(seed, rig().params), seed);
            break;
        }
        case 1: s = scattered(rng, 1 + i % 15); break;
        case 2: s = colours_only(rng, kColours[i % 6]); break;
        case 3: s = in_hand(i % 3 == 0 ? colours_only(rng, kColours[i % 6]) : scattered(rng, 1 + i % 15)); break;
        case 4: s = snookered(rng); break;
        case 5:
            s = scattered(rng, 1 + i % 14);
            s.on = {OnKind::any_colour, Ball::yellow};
            s.break_points = 1;
            break;
        case 6:
            s = snookered(rng);
            s.free_ball = true;
            break;
        default:
            s = colours_only(rng, Ball::black);
            // Within seven either way, or the striker far enough ahead to claim (S2.1(b)).
            s.scores = i % 3 == 0 ? std::array<int, 2>{70, 40} : std::array<int, 2>{50 + i % 8, 50};
            break;
    }
    if (layout != 0) s.striker = layout == 7 ? 0 : i % 2;
    s.seed = 9000 + std::uint64_t(i);
    return s;
}

double microseconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - since).count();
}

bool same(const StandinShot& a, const StandinShot& b) {
    return a.decides == b.decides && a.decision == b.decision && a.places_cue == b.places_cue &&
           a.cue_pos.x == b.cue_pos.x && a.cue_pos.y == b.cue_pos.y &&
           a.input.has_nomination == b.input.has_nomination && a.input.nominated == b.input.nominated &&
           a.strike.aim_rad == b.strike.aim_rad && a.strike.tip_x == b.strike.tip_x &&
           a.strike.tip_y == b.strike.tip_y && a.strike.elevation_rad == b.strike.elevation_rad &&
           a.strike.speed_m_s == b.strike.speed_m_s && a.why == b.why;
}

// The cue ball, one red a ball and a half from the pocket's mouth on its axis, the cue ball half a
// metre behind it on the same line, and the colours on their spots.
FrameState straight_pot(std::size_t pocket) {
    const phys::Pocket& p = rig().table.pockets[pocket];
    const double sx = p.drop_centre.x > 0.0 ? 1.0 : -1.0;
    const double sy = p.drop_centre.y > 0.0 ? 1.0 : -1.0;
    const double k = p.corner ? std::sqrt(0.5) : 1.0;
    const Vec2 axis{p.corner ? sx * k : 0.0, sy * k};
    const Vec2 mouth{p.drop_centre.x - axis.x * p.drop_radius, p.drop_centre.y - axis.y * p.drop_radius};
    const Vec2 red{mouth.x - axis.x * 1.5 * kD, mouth.y - axis.y * 1.5 * kD};
    std::vector<BallOnTable> balls{ball(Ball::cue, red.x - axis.x * 0.5, red.y - axis.y * 0.5),
                                   ball(Ball::red, red.x, red.y)};
    for (Ball colour : kColours) balls.push_back(spotted(colour));
    return rules::scenario::frame(std::move(balls));
}

// The blue straight between the cue ball and the only red, the left cushion clear for a way round.
FrameState blue_in_the_way() {
    std::vector<BallOnTable> balls{ball(Ball::cue, 0.3, 0.3), ball(Ball::red, 1.3, 0.3), ball(Ball::blue, 0.8, 0.3)};
    for (Ball colour : kColours) {
        if (colour != Ball::blue) balls.push_back(spotted(colour));
    }
    return rules::scenario::frame(std::move(balls));
}

}  // namespace

TEST_CASE("every stroke the stand-in gives over 200 seeded positions is one the engine accepts") {
    const char* const why_keys[] = {"kind", "target", "pocket", "cut_rad", "candidates", "sims", "aim_error_rad",
                                    "speed_error"};
    Rng layouts(20261115);
    std::map<std::string, int> kinds;
    int strokes = 0;
    int decided = 0;
    double worst_us = 0.0;
    for (int i = 0; i < 25; ++i) {
        for (int layout = 0; layout < kLayoutCount; ++layout) {
            const FrameState s = position(layout, i, layouts);
            const std::uint64_t seed = 31 * std::uint64_t(i) + std::uint64_t(layout);
            CAPTURE(kLayouts[layout]);
            CAPTURE(i);
            const auto sink = capture_log();
            Rng rng(seed);
            const auto t0 = std::chrono::steady_clock::now();
            const StandinShot shot = choose(s, standin_params(), rng);
            double us = microseconds(t0);
            // Another program's turn on the processor is not the stand-in's time: the quickest of a few.
            for (int again = 0; again < 3 && us > 2500.0; ++again) {
                Rng same_rng(seed);
                const auto t1 = std::chrono::steady_clock::now();
                choose(s, standin_params(), same_rng);
                us = std::min(us, microseconds(t1));
            }
            worst_us = std::max(worst_us, us);

            FrameState placed;
            const std::string why_not = refusal(s, shot, placed);
            CHECK_MESSAGE(why_not.empty(), why_not);
            if (shot.decides) {
                ++decided;
                std::vector<rules::Call> calls;
                std::string error;
                CHECK(rules::decide(placed, {shot.decision, s.striker}, *rig().oracle, rig().ref, calls, error));
                CHECK(placed.over);
                continue;
            }
            ++strokes;
            for (const char* key : why_keys) CHECK_MESSAGE(shot.why.contains(key), key);
            ++kinds[shot.why.value("kind", "")];
            CHECK(shot.why["sims"].get<int>() <= standin_params().max_sims);
            const auto played = play(placed, shot, seed);
            CHECK(played.judgement.post.strokes == s.strokes + 1);
            CHECK_FALSE(played.facts.anomaly);
            // Nothing it declared or played was refused, in its own trials or in the stroke.
            CHECK(sink->count("rules.invalid") == 0);
            CHECK(sink->count("opponent.decision") == 0);
            log::Logger::instance().clear_sinks();
        }
    }
    log::Logger::instance().clear_sinks();
    CHECK(strokes + decided == 200);
    // Every third final black is the striker's to claim.
    CHECK(decided == 9);
    for (const char* kind : {"pot", "safety", "escape", "break"}) CHECK_MESSAGE(kinds[kind] > 0, kind);
    CHECK(kinds["break"] == 25);

#ifdef NDEBUG
    // Spec 21 M3: under 5 ms a stroke on this PC, break-off included.
    CHECK(worst_us < 5000.0);
#else
    // An unoptimised build is many times slower; this only catches a search that has run away.
    CHECK(worst_us < 500000.0);
#endif
}

TEST_CASE("a straight pot one ball from a pocket is taken and, played as aimed, potted") {
    for (std::size_t pocket = 0; pocket < rig().table.pockets.size(); ++pocket) {
        CAPTURE(rig().table.pockets[pocket].name);
        const FrameState s = straight_pot(pocket);
        Rng rng(5);
        const StandinShot shot = choose(s, exact(), rng);
        FrameState placed;
        REQUIRE(refusal(s, shot, placed).empty());
        CHECK(shot.why["kind"] == "pot");
        CHECK(shot.why["target"] == "red 0");
        CHECK(shot.why["pocket"] == int(pocket));
        CHECK(shot.why["cut_rad"].get<double>() < 0.01);
        CHECK(shot.why["penalty"] == 0);
        CHECK(shot.why["aim_error_rad"] == 0.0);
        const auto played = play(placed, shot);
        CHECK(played.judgement.fouls.empty());
        CHECK(played.judgement.points == 1);
        REQUIRE(played.facts.pots.size() == 1);
        CHECK(played.facts.pots[0].ball == 1);
        CHECK(played.facts.pots[0].pocket == int(pocket));
    }
}

TEST_CASE("with a colour of choice on it declares the colour it plays at") {
    // The black over the top-left pocket, the easiest pot on the table.
    FrameState s = straight_pot(0);
    s.balls[1] = ball(Ball::red, -0.3, -0.6);
    const int black = rules::scenario::place_of(s, Ball::black);
    const Vec2 over{s.balls[0].pos.x + 0.5 * std::sqrt(0.5), s.balls[0].pos.y + 0.5 * std::sqrt(0.5)};
    s.balls[std::size_t(black)].pos = over;
    s.on = {OnKind::any_colour, Ball::yellow};
    Rng rng(5);
    const StandinShot shot = choose(s, exact(), rng);
    CHECK(shot.why["kind"] == "pot");
    CHECK(shot.why["target"] == "black");
    CHECK(shot.input.has_nomination);
    CHECK(shot.input.nominated == Ball::black);
    FrameState placed;
    REQUIRE(refusal(s, shot, placed).empty());
    const auto played = play(placed, shot);
    CHECK(played.judgement.points == 7);
    CHECK(played.judgement.nominated.nominated == Ball::black);
}

TEST_CASE("with FREE BALL called it nominates the ball it pots") {
    // Snookered on the red by the blue, which itself hangs over the middle pocket.
    std::vector<BallOnTable> balls{ball(Ball::cue, 0.0, 0.2), ball(Ball::red, 0.0, 0.84), ball(Ball::blue, 0.0, 0.76)};
    for (Ball colour : kColours) {
        if (colour != Ball::blue) balls.push_back(spotted(colour));
    }
    FrameState s = rules::scenario::frame(std::move(balls));
    REQUIRE(rig().oracle->snookered(s.balls, s.cue_ball, rules::places_on(s), false));
    s.free_ball = true;
    Rng rng(5);
    const StandinShot shot = choose(s, exact(), rng);
    FrameState placed;
    REQUIRE(refusal(s, shot, placed).empty());
    CHECK(shot.input.has_nomination);
    CHECK(shot.input.nominated != Ball::red);
    const auto played = play(placed, shot);
    CHECK(played.judgement.fouls.empty());
    CHECK(played.judgement.free_ball_used);
}

TEST_CASE("in a snooker it escapes off a cushion when a one-cushion route exists") {
    const FrameState s = blue_in_the_way();
    const int red = rules::scenario::place_of(s, Ball::red);
    REQUIRE(rig().oracle->snookered(s.balls, s.cue_ball, {red}, false));
    REQUIRE_FALSE(rig().oracle->clear_path(s.balls, s.cue_ball, {red}));
    Rng rng(5);
    const StandinShot shot = choose(s, exact(), rng);
    FrameState placed;
    REQUIRE(refusal(s, shot, placed).empty());
    CHECK(shot.why["kind"] == "escape");
    CHECK(shot.why["target"] == "red 0");
    CHECK(shot.why["penalty"] == 0);
    const auto played = play(placed, shot);
    CHECK(played.judgement.fouls.empty());
    REQUIRE(played.facts.first_hit.size() == 1);
    CHECK(played.facts.first_hit[0] == red);
    // It went round the blue, not through a gap beside it.
    bool cushion_first = false;
    for (const phys::ShotEvent& e : played.record.events) {
        if (e.kind == phys::EventKind::ball_ball) break;
        cushion_first = cushion_first || e.kind == phys::EventKind::ball_line;
    }
    CHECK(cushion_first);
}

TEST_CASE("most seeded snookers are escaped when played as aimed") {
    Rng layouts(31);
    int clean = 0;
    for (int i = 0; i < 40; ++i) {
        const FrameState s = snookered(layouts);
        Rng rng(1);
        const StandinShot shot = choose(s, exact(), rng);
        FrameState placed;
        REQUIRE(refusal(s, shot, placed).empty());
        clean += play(placed, shot).judgement.fouls.empty() ? 1 : 0;
    }
    CHECK(clean >= 36);
}

TEST_CASE("the break-off is clean as aimed and brings the cue ball back to baulk") {
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
        CAPTURE(seed);
        const FrameState s = rules::new_frame(int(seed % 2), phys::opening_rack(seed, rig().params), seed);
        Rng rng(seed);
        const StandinShot shot = choose(s, exact(), rng);
        FrameState placed;
        REQUIRE(refusal(s, shot, placed).empty());
        CHECK(shot.why["kind"] == "break");
        CHECK(shot.places_cue);
        CHECK(table::in_d(shot.cue_pos));
        const auto played = play(placed, shot);
        CHECK(played.judgement.fouls.empty());
        REQUIRE(played.facts.first_hit.size() == 1);
        CHECK(placed.balls[std::size_t(played.facts.first_hit[0])].ball == Ball::red);
        CHECK(played.facts.rest[std::size_t(placed.cue_ball)].x < table::baulk_line_x() + 0.3);
    }
}

TEST_CASE("the same Rng state gives the same stroke, another its own error") {
    Rng layouts(8);
    for (int i = 0; i < 12; ++i) {
        CAPTURE(i);
        const FrameState s = position(i % kLayoutCount, i, layouts);
        Rng a(77);
        Rng b(77);
        Rng c(78);
        const StandinShot first = choose(s, standin_params(), a);
        const StandinShot second = choose(s, standin_params(), b);
        const StandinShot other = choose(s, standin_params(), c);
        CHECK(same(first, second));
        CHECK(a.next_u64() == b.next_u64());
        if (first.decides) continue;
        CHECK(first.strike.aim_rad != other.strike.aim_rad);
        // Only the error differs: what it chose to play is the same.
        CHECK(first.why["kind"] == other.why["kind"]);
        CHECK(first.why["target"] == other.why["target"]);
    }
}

TEST_CASE("the error it adds is the parameters' and no more") {
    const FrameState s = straight_pot(0);
    StandinParams sp = exact();
    sp.aim_error_rad = 0.01;
    sp.speed_error = 0.1;
    Rng none(3);
    const StandinShot aimed = choose(s, exact(), none);
    double aim_sq = 0.0;
    double speed_sq = 0.0;
    const int n = 400;
    for (int i = 0; i < n; ++i) {
        Rng rng(1000 + std::uint64_t(i));
        const StandinShot shot = choose(s, sp, rng);
        const double aim_error = shot.strike.aim_rad - aimed.strike.aim_rad;
        CHECK(shot.why["aim_error_rad"].get<double>() == doctest::Approx(aim_error).epsilon(1e-9));
        aim_sq += aim_error * aim_error;
        const double speed_error = shot.strike.speed_m_s / aimed.strike.speed_m_s - 1.0;
        speed_sq += speed_error * speed_error;
    }
    CHECK(std::sqrt(aim_sq / n) == doctest::Approx(0.01).epsilon(0.15));
    CHECK(std::sqrt(speed_sq / n) == doctest::Approx(0.1).epsilon(0.15));
}

TEST_CASE("with no simulations allowed it still gives a legal stroke") {
    StandinParams sp = standin_params();
    sp.max_sims = 0;
    Rng layouts(4);
    for (int i = 0; i < 16; ++i) {
        const FrameState s = position(i % kLayoutCount, i, layouts);
        Rng rng(9);
        const StandinShot shot = choose(s, sp, rng);
        FrameState placed;
        const std::string why_not = refusal(s, shot, placed);
        CHECK_MESSAGE(why_not.empty(), why_not);
        if (!shot.decides) {
            CHECK(shot.why["sims"] == 0);
            CHECK(shot.why["penalty"] == -1);
        }
    }
}

TEST_CASE("it claims a frame it has won and concedes one it cannot win") {
    Rng layouts(12);
    Rng rng(1);

    // Only the black left and more than seven ahead: S2.1(b).
    FrameState s = colours_only(layouts, Ball::black);
    s.scores = {60, 52};
    StandinShot shot = choose(s, standin_params(), rng);
    CHECK(shot.decides);
    CHECK(shot.decision == DecisionKind::claim_frame);
    s.scores = {60, 53};
    CHECK_FALSE(choose(s, standin_params(), rng).decides);

    // Pink and black left, 13 points: 33 behind needs 20 in penalties.
    s = colours_only(layouts, Ball::pink);
    s.scores = {10, 43};
    shot = choose(s, standin_params(), rng);
    CHECK(shot.decides);
    CHECK(shot.decision == DecisionKind::concede_frame);
    CHECK(shot.why["required"] == 20);
    s.scores = {10, 42};
    CHECK_FALSE(choose(s, standin_params(), rng).decides);
    // The other player, far ahead, plays on.
    s.scores = {43, 10};
    CHECK_FALSE(choose(s, standin_params(), rng).decides);

    // Its own threshold.
    StandinParams keen = standin_params();
    keen.concede_required_points = 5;
    s.scores = {10, 28};
    CHECK(choose(s, keen, rng).decides);
    CHECK_FALSE(choose(s, standin_params(), rng).decides);

    // While a red is left it plays on, however far behind.
    s = scattered(layouts, 1);
    s.scores = {0, 140};
    REQUIRE(rules::penalty_points_required(s, 0) >= 20);
    CHECK_FALSE(choose(s, standin_params(), rng).decides);
}

TEST_CASE("a frame it should not be asked about still gets a stroke and nothing thrown") {
    Rng rng(1);
    FrameState s;
    StandinShot shot;
    CHECK_NOTHROW(shot = choose(s, standin_params(), rng));
    CHECK_FALSE(shot.decides);
    CHECK(shot.strike.speed_m_s > 0.0);

    // The cue ball alone.
    s = rules::scenario::frame({ball(Ball::cue, 0.2, 0.1)});
    CHECK_NOTHROW(shot = choose(s, standin_params(), rng));
    CHECK(shot.strike.speed_m_s > 0.0);
    CHECK(shot.strike.speed_m_s <= rig().params.max_cue_speed_m_s);
}
