#include <doctest/doctest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/rng.hpp"
#include "ofs/rules/match.hpp"
#include "ofs/rules/serialize.hpp"

#include "scenario.hpp"

using namespace ofs::rules;
using nlohmann::json;

namespace {

const ofs::phys::PhysicsParams kParams{};

MatchConfig config(int best_of, int first_breaker = 0, std::uint64_t seed = 147) {
    MatchConfig c;
    c.best_of = best_of;
    c.first_breaker = first_breaker;
    c.seed = seed;
    return c;
}

// Ends the frame in play the way the engine leaves it, without playing it.
MatchEvent finish(MatchState& m, int winner, EndReason reason = EndReason::black) {
    m.frame.over = true;
    m.frame.winner = winner;
    m.frame.end_reason = reason;
    m.frame.scores = {winner == 0 ? 74 : 31, winner == 0 ? 31 : 74};
    m.frame.strokes = 50 + m.frame_number;
    return settle(m, kParams);
}

struct Capture : ofs::log::Sink {
    std::vector<ofs::log::Event> events;
    void write(const ofs::log::Event& event, const std::string&) override { events.push_back(event); }
};

std::shared_ptr<Capture> capture_log() {
    auto sink = std::make_shared<Capture>();
    ofs::log::Logger::instance().clear_sinks();
    ofs::log::Logger::instance().add_sink(sink);
    return sink;
}

// The details of the rules.invalid lines about `what`, in order.
std::vector<std::string> invalid_details(const Capture& sink, const std::string& what) {
    std::vector<std::string> details;
    for (const ofs::log::Event& e : sink.events) {
        if (e.event != "rules.invalid" || e.data.value("what", "") != what) continue;
        CHECK(e.subsystem == "rules");
        CHECK(e.level == ofs::log::Level::warn);
        details.push_back(e.data.value("detail", ""));
    }
    return details;
}

}  // namespace

TEST_CASE("valid_config wants an odd best_of from 1 to 35 and a breaker of -1, 0 or 1") {
    std::string error;
    for (const int best_of : {1, 3, 5, 7, 9, 11, 33, 35}) {
        for (const int first_breaker : {-1, 0, 1}) {
            CAPTURE(best_of);
            CAPTURE(first_breaker);
            CHECK(valid_config(config(best_of, first_breaker), error));
        }
    }
    CHECK(error.empty());
    for (const int best_of : {-3, -1, 0, 2, 4, 34, 36, 37, 1000001}) {
        CAPTURE(best_of);
        error.clear();
        CHECK_FALSE(valid_config(config(best_of), error));
        CHECK(error == "best_of: " + std::to_string(best_of) + " is not an odd number from 1 to 35");
    }
    for (const int first_breaker : {-2, 2, 7}) {
        CAPTURE(first_breaker);
        error.clear();
        CHECK_FALSE(valid_config(config(3, first_breaker), error));
        CHECK(error.rfind("first_breaker: " + std::to_string(first_breaker), 0) == 0);
    }
    CHECK(valid_config(MatchConfig{}, error));
}

TEST_CASE("frame_seed differs for every frame and re-rack, and with the match seed") {
    std::set<std::uint64_t> seen;
    for (int number = 1; number <= 35; ++number) {
        for (int reracks = 0; reracks < 64; ++reracks) {
            const std::uint64_t seed = frame_seed(config(35, 0, 147), number, reracks);
            CHECK(seed == frame_seed(config(35, 0, 147), number, reracks));
            seen.insert(seed);
        }
    }
    CHECK(seen.size() == 35 * 64);

    CHECK(frame_seed(config(3, 0, 147), 1, 0) != frame_seed(config(3, 0, 148), 1, 0));
    CHECK(frame_seed(config(3, 0, 147), 1, 0) != 147);
    // Nothing but the seed, the frame and the re-racks goes in.
    MatchConfig other = config(35, 1, 147);
    other.concede_any_time = {true, true};
    CHECK(frame_seed(other, 4, 2) == frame_seed(config(3, 0, 147), 4, 2));
}

TEST_CASE("start_match racks frame 1 for the breaker from the frame's seed") {
    MatchConfig c = config(5, 1, 20261115);
    c.concede_any_time = {true, false};
    const MatchState m = start_match(c, kParams);

    CHECK(match_json(m)["config"] == config_json(c));
    CHECK(m.frames == std::array<int, 2>{0, 0});
    CHECK(m.frame_number == 1);
    CHECK(m.breaker == 1);
    CHECK(m.reracks == 0);
    CHECK(m.history.empty());
    CHECK_FALSE(m.over);
    CHECK(m.winner == -1);

    const std::uint64_t seed = frame_seed(c, 1, 0);
    const ofs::phys::TableState rack = ofs::phys::opening_rack(seed, kParams);
    CHECK(m.frame.seed == seed);
    CHECK(m.frame.striker == 1);
    CHECK(m.frame.concede_any_time == c.concede_any_time);
    CHECK(m.frame.scores == std::array<int, 2>{0, 0});
    CHECK_FALSE(m.frame.over);
    REQUIRE(m.frame.balls.size() == 22);
    for (std::size_t i = 0; i < 22; ++i) {
        CAPTURE(i);
        CHECK(m.frame.balls[i].ball == rack.balls[i].ball);
        CHECK(m.frame.balls[i].index == rack.balls[i].index);
    }
    // The reds' seeded gaps are the frame seed's, not the match seed's.
    CHECK(m.frame.balls[5].pos.x == rack.balls[5].r.x);
    CHECK(m.frame.balls[5].pos.y == rack.balls[5].r.y);
    CHECK(m.frame.balls[5].pos.x != ofs::phys::opening_rack(c.seed, kParams).balls[5].r.x);

    CHECK(frame_hash(start_match(c, kParams).frame) == frame_hash(m.frame));
    CHECK(start_match(config(5, 0), kParams).frame.striker == 0);
}

TEST_CASE("lots for the first break: both players over a range of seeds, the same for the same seed") {
    int breaks[2] = {0, 0};
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        const MatchState m = start_match(config(3, -1, seed), kParams);
        REQUIRE((m.breaker == 0 || m.breaker == 1));
        CHECK(m.breaker == int(ofs::Rng(seed).next_u64() & 1));
        CHECK(m.frame.striker == m.breaker);
        CHECK(start_match(config(3, -1, seed), kParams).breaker == m.breaker);
        ++breaks[m.breaker];
    }
    CHECK(breaks[0] > 60);
    CHECK(breaks[1] > 60);
}

TEST_CASE("settle leaves a frame in play alone") {
    MatchState m = start_match(config(3), kParams);
    const json before = match_json(m);
    CHECK(settle(m, kParams) == MatchEvent::none);
    m.frame.scores = {12, 5};
    m.frame.strokes = 9;
    CHECK(settle(m, kParams) == MatchEvent::none);
    CHECK(m.history.empty());
    CHECK(m.frames == std::array<int, 2>{0, 0});
    CHECK(match_json(m)["frame_number"] == before["frame_number"]);
}

TEST_CASE("a finished frame is summarised and counted once") {
    MatchState m = start_match(config(3, 1), kParams);
    m.frame.stats[0] = {20, 9, 12, 1, 0, 4, 33};
    m.frame.stats[1] = {19, 6, 10, 2, 1, 4, 18};
    CHECK(finish(m, 0, EndReason::conceded) == MatchEvent::frame_ended);

    CHECK(m.frames == std::array<int, 2>{1, 0});
    CHECK_FALSE(m.over);
    CHECK(m.winner == -1);
    REQUIRE(m.history.size() == 1);
    const FrameSummary& s = m.history[0];
    CHECK(s.number == 1);
    CHECK(s.breaker == 1);
    CHECK(s.winner == 0);
    CHECK(s.reason == EndReason::conceded);
    CHECK(s.scores == std::array<int, 2>{74, 31});
    CHECK(s.stats[0].highest_break == 33);
    CHECK(s.stats[1].fouls == 2);
    CHECK(s.strokes == 51);
    CHECK(s.reracks == 0);

    const json after = match_json(m);
    CHECK(settle(m, kParams) == MatchEvent::none);
    CHECK(settle(m, kParams) == MatchEvent::none);
    CHECK(match_json(m) == after);
}

TEST_CASE("best of 1: the first frame is the match") {
    for (int winner = 0; winner < 2; ++winner) {
        MatchState m = start_match(config(1), kParams);
        CHECK(finish(m, winner) == MatchEvent::match_ended);
        CHECK(m.over);
        CHECK(m.winner == winner);
        CHECK(m.frames[winner] == 1);
        CHECK(m.history.size() == 1);
        const json after = match_json(m);
        CHECK(settle(m, kParams) == MatchEvent::none);
        CHECK_FALSE(next_frame(m, kParams));
        CHECK(match_json(m) == after);
    }
}

TEST_CASE("best of 3: two frames win it, in two or in three") {
    MatchState m = start_match(config(3), kParams);
    CHECK(finish(m, 1) == MatchEvent::frame_ended);
    REQUIRE(next_frame(m, kParams));
    CHECK(finish(m, 1) == MatchEvent::match_ended);
    CHECK(m.over);
    CHECK(m.winner == 1);
    CHECK(m.frames == std::array<int, 2>{0, 2});
    CHECK(m.history.size() == 2);

    m = start_match(config(3), kParams);
    CHECK(finish(m, 0) == MatchEvent::frame_ended);
    REQUIRE(next_frame(m, kParams));
    CHECK(finish(m, 1) == MatchEvent::frame_ended);
    CHECK_FALSE(m.over);
    REQUIRE(next_frame(m, kParams));
    CHECK(m.frame_number == 3);
    CHECK(finish(m, 0) == MatchEvent::match_ended);
    CHECK(m.winner == 0);
    CHECK(m.frames == std::array<int, 2>{2, 1});
    CHECK(m.history.size() == 3);
}

TEST_CASE("best of 35: eighteen frames win it, the breaker alternating all the way") {
    MatchState m = start_match(config(35, 1), kParams);
    std::set<std::uint64_t> seeds;
    // Frame about: 17 all after 34 frames, then the decider.
    for (int number = 1; number <= 35; ++number) {
        CAPTURE(number);
        CHECK(m.frame_number == number);
        CHECK(m.breaker == (number % 2 == 1 ? 1 : 0));
        CHECK(m.frame.striker == m.breaker);
        CHECK(m.reracks == 0);
        CHECK(m.frame.seed == frame_seed(m.config, number, 0));
        CHECK_FALSE(m.frame.over);
        CHECK(m.frame.scores == std::array<int, 2>{0, 0});
        seeds.insert(m.frame.seed);

        const MatchEvent event = finish(m, number == 35 ? 1 : number % 2);
        if (number < 35) {
            CHECK(event == MatchEvent::frame_ended);
            CHECK_FALSE(m.over);
            REQUIRE(next_frame(m, kParams));
        } else {
            CHECK(event == MatchEvent::match_ended);
        }
    }
    CHECK(seeds.size() == 35);
    CHECK(m.over);
    CHECK(m.winner == 1);
    CHECK(m.frames == std::array<int, 2>{17, 18});
    CHECK(m.history.size() == 35);
    CHECK(m.history[34].number == 35);
    CHECK(m.history[34].breaker == 1);
    CHECK(m.history[33].breaker == 0);

    // A run of eighteen ends it early.
    m = start_match(config(35), kParams);
    for (int number = 1; number <= 17; ++number) {
        CHECK(finish(m, 0) == MatchEvent::frame_ended);
        REQUIRE(next_frame(m, kParams));
    }
    CHECK(finish(m, 0) == MatchEvent::match_ended);
    CHECK(m.frames == std::array<int, 2>{18, 0});
    CHECK(m.winner == 0);
}

TEST_CASE("a re-rack keeps the breaker and the frame number and changes the rack") {
    MatchConfig c = config(3, 1);
    c.concede_any_time = {true, false};
    MatchState m = start_match(c, kParams);
    const FrameState first = m.frame;
    m.frame.scores = {22, 30};
    m.frame.strokes = 40;
    m.frame.striker = 0;
    m.frame.stats[0].shots = 20;
    // The frame's own setting is what is kept, should it ever differ from the config's.
    m.frame.concede_any_time = {false, true};
    m.frame.restart = true;

    CHECK(settle(m, kParams) == MatchEvent::reracked);
    CHECK(m.frame_number == 1);
    CHECK(m.breaker == 1);
    CHECK(m.reracks == 1);
    CHECK(m.frames == std::array<int, 2>{0, 0});
    CHECK(m.history.empty());
    CHECK_FALSE(m.over);
    CHECK_FALSE(m.frame.restart);
    CHECK(m.frame.striker == 1);
    CHECK(m.frame.scores == std::array<int, 2>{0, 0});
    CHECK(m.frame.strokes == 0);
    CHECK(m.frame.stats[0].shots == 0);
    CHECK(m.frame.concede_any_time == std::array<bool, 2>{false, true});
    CHECK(m.frame.seed == frame_seed(c, 1, 1));
    CHECK(m.frame.seed != first.seed);
    REQUIRE(m.frame.balls.size() == 22);
    CHECK(m.frame.balls[5].pos.x != first.balls[5].pos.x);
    CHECK(m.frame.balls[5].pos.x == ofs::phys::opening_rack(frame_seed(c, 1, 1), kParams).balls[5].r.x);
    CHECK(settle(m, kParams) == MatchEvent::none);

    m.frame.restart = true;
    CHECK(settle(m, kParams) == MatchEvent::reracked);
    CHECK(m.reracks == 2);
    CHECK(m.frame.seed == frame_seed(c, 1, 2));

    // The re-racks go into the summary and the next frame starts without any.
    CHECK(finish(m, 0) == MatchEvent::frame_ended);
    CHECK(m.history[0].reracks == 2);
    CHECK(m.history[0].breaker == 1);
    REQUIRE(next_frame(m, kParams));
    CHECK(m.reracks == 0);
    CHECK(m.breaker == 0);
    CHECK(m.frame.seed == frame_seed(c, 2, 0));
    CHECK(m.frame.concede_any_time == c.concede_any_time);
}

TEST_CASE("a conceded match ends at once, for the frame's winner") {
    MatchState m = start_match(config(35), kParams);
    CHECK(finish(m, 0) == MatchEvent::frame_ended);
    REQUIRE(next_frame(m, kParams));
    CHECK(finish(m, 0) == MatchEvent::frame_ended);
    REQUIRE(next_frame(m, kParams));
    // Player 0 leads 2-0 and concedes the match.
    CHECK(finish(m, 1, EndReason::match_conceded) == MatchEvent::match_ended);
    CHECK(m.over);
    CHECK(m.winner == 1);
    CHECK(m.frames == std::array<int, 2>{2, 1});
    REQUIRE(m.history.size() == 3);
    CHECK(m.history[2].reason == EndReason::match_conceded);
    CHECK(m.history[2].winner == 1);
    CHECK_FALSE(next_frame(m, kParams));
    CHECK(settle(m, kParams) == MatchEvent::none);
}

TEST_CASE("next_frame is refused unless a finished frame of an unfinished match was settled") {
    const auto sink = capture_log();
    MatchState m = start_match(config(3), kParams);
    json before = match_json(m);
    CHECK_FALSE(next_frame(m, kParams));
    CHECK(match_json(m) == before);

    // Over, but settle has not counted it: racking the next frame would lose its result.
    m.frame.over = true;
    m.frame.winner = 0;
    m.frame.end_reason = EndReason::black;
    before = match_json(m);
    CHECK_FALSE(next_frame(m, kParams));
    CHECK(match_json(m) == before);

    CHECK(settle(m, kParams) == MatchEvent::frame_ended);
    REQUIRE(next_frame(m, kParams));
    CHECK(m.frame_number == 2);
    CHECK(m.breaker == 1);
    CHECK_FALSE(m.frame.over);
    CHECK(m.frame.winner == -1);
    before = match_json(m);
    CHECK_FALSE(next_frame(m, kParams));
    CHECK(match_json(m) == before);

    CHECK(finish(m, 0) == MatchEvent::match_ended);
    CHECK_FALSE(next_frame(m, kParams));
    ofs::log::Logger::instance().clear_sinks();

    // Each refusal says why in the log.
    CHECK(invalid_details(*sink, "next_frame") ==
          std::vector<std::string>{"frame 1: the frame in play is not over",
                                   "frame 1: the finished frame has not been settled",
                                   "frame 2: the frame in play is not over", "frame 2: the match is over"});
}

TEST_CASE("a finished match changes no more") {
    MatchState m = start_match(config(1), kParams);
    CHECK(finish(m, 1) == MatchEvent::match_ended);
    const json after = match_json(m);
    m.frame.restart = true;
    CHECK(settle(m, kParams) == MatchEvent::none);
    m.frame.restart = false;
    CHECK(match_json(m) == after);
}

TEST_CASE("a frame that ends with no winner is recorded, counts for nobody and is logged") {
    const auto sink = capture_log();
    MatchState m = start_match(config(3), kParams);
    m.frame.over = true;
    m.frame.end_reason = EndReason::black;
    CHECK(settle(m, kParams) == MatchEvent::frame_ended);
    ofs::log::Logger::instance().clear_sinks();

    CHECK(m.frames == std::array<int, 2>{0, 0});
    CHECK_FALSE(m.over);
    REQUIRE(m.history.size() == 1);
    CHECK(m.history[0].winner == -1);
    CHECK(invalid_details(*sink, "settle") == std::vector<std::string>{"frame 1 is over with winner -1 (black)"});
    CHECK(next_frame(m, kParams));
}

TEST_CASE("a match in play round-trips through its JSON at every stage") {
    MatchState m = start_match(config(5, -1, 99), kParams);
    const auto check = [](const MatchState& state) {
        MatchState back;
        std::string error;
        REQUIRE_MESSAGE(match_from_json(json::parse(match_json(state).dump()), back, error), error);
        CHECK(match_json(back) == match_json(state));
    };
    check(m);
    finish(m, 0);
    check(m);
    REQUIRE(next_frame(m, kParams));
    m.frame.restart = true;
    CHECK(settle(m, kParams) == MatchEvent::reracked);
    check(m);
    finish(m, 0);
    REQUIRE(next_frame(m, kParams));
    finish(m, 0);
    CHECK(m.over);
    check(m);
}

TEST_CASE("match_event_name names every event") {
    CHECK(match_event_name(MatchEvent::none) == "none");
    CHECK(match_event_name(MatchEvent::reracked) == "reracked");
    CHECK(match_event_name(MatchEvent::frame_ended) == "frame_ended");
    CHECK(match_event_name(MatchEvent::match_ended) == "match_ended");
}

namespace {

// match_decide with the real table's oracle and referee values.
bool choose(MatchState& m, Decision d, MatchEvent& event, std::string& error) {
    const scenario::Rig& rig = scenario::rig();
    std::vector<Call> calls;
    return match_decide(m, d, *rig.oracle, rig.ref, rig.params, calls, event, error);
}

}  // namespace

TEST_CASE("match_decide refuses what the engine refuses and leaves the match as it was") {
    MatchState m = start_match(config(3), kParams);
    const json before = match_json(m);
    MatchEvent event = MatchEvent::frame_ended;
    std::string error;
    // Nothing is pending at the break-off, so there is no foul to answer.
    CHECK_FALSE(choose(m, {DecisionKind::replace, 0}, event, error));
    CHECK(event == MatchEvent::none);
    CHECK_FALSE(error.empty());
    CHECK(match_json(m) == before);

    finish(m, 0);
    REQUIRE(next_frame(m, kParams));
    finish(m, 0);
    REQUIRE(m.over);
    error.clear();
    event = MatchEvent::frame_ended;
    CHECK_FALSE(choose(m, {DecisionKind::concede_match, 1}, event, error));
    CHECK(error == "the match is over");
    CHECK(event == MatchEvent::none);
    CHECK(m.winner == 0);
}

// These go through the engine's decide(), so they hold once the real engine is in.
TEST_CASE("conceding the match ends it for the other player whatever the frames stand at" *
          doctest::test_suite("with the engine")) {
    MatchConfig c = config(35);
    c.concede_any_time = {true, false};
    MatchState m = start_match(c, kParams);
    finish(m, 0);
    REQUIRE(next_frame(m, kParams));
    MatchEvent event = MatchEvent::none;
    std::string error;
    REQUIRE_MESSAGE(choose(m, {DecisionKind::concede_match, 0}, event, error), error);
    CHECK(event == MatchEvent::match_ended);
    CHECK(m.over);
    CHECK(m.winner == 1);
    CHECK(m.frames == std::array<int, 2>{1, 1});
    REQUIRE(m.history.size() == 2);
    CHECK(m.history[1].reason == EndReason::match_conceded);
    CHECK(m.history[1].winner == 1);
    CHECK_FALSE(next_frame(m, kParams));
}

TEST_CASE("conceding a frame gives it to the other player and the match goes on" *
          doctest::test_suite("with the engine")) {
    MatchConfig c = config(3);
    c.concede_any_time = {true, false};
    MatchState m = start_match(c, kParams);
    MatchEvent event = MatchEvent::none;
    std::string error;
    // Player 1 may concede only when penalty points are required: not at the break-off.
    CHECK_FALSE(choose(m, {DecisionKind::concede_frame, 1}, event, error));
    CHECK(event == MatchEvent::none);
    REQUIRE_MESSAGE(choose(m, {DecisionKind::concede_frame, 0}, event, error), error);
    CHECK(event == MatchEvent::frame_ended);
    CHECK_FALSE(m.over);
    CHECK(m.frames == std::array<int, 2>{0, 1});
    REQUIRE(m.history.size() == 1);
    CHECK(m.history[0].reason == EndReason::conceded);
    REQUIRE(next_frame(m, kParams));
    CHECK(m.breaker == 1);
    CHECK(m.frame.concede_any_time == c.concede_any_time);

    REQUIRE_MESSAGE(choose(m, {DecisionKind::concede_frame, 0}, event, error), error);
    CHECK(event == MatchEvent::match_ended);
    CHECK(m.winner == 1);
    CHECK(m.frames == std::array<int, 2>{0, 2});
}
