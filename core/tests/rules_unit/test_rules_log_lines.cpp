#include <doctest/doctest.h>

#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/log_schema.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/rules/rules_log.hpp"
#include "ofs/rules/serialize.hpp"

#include "scenario.hpp"

using namespace ofs::rules;
using nlohmann::json;
using ofs::log::Level;

namespace {

struct Line {
    ofs::log::Event event;
    std::string text;
};

struct Capture : ofs::log::Sink {
    std::vector<Line> lines;
    void write(const ofs::log::Event& event, const std::string& line) override { lines.push_back({event, line}); }
};

std::vector<Line> logged(const std::function<void()>& emit) {
    auto sink = std::make_shared<Capture>();
    ofs::log::Logger::instance().clear_sinks();
    ofs::log::Logger::instance().add_sink(sink);
    emit();
    ofs::log::Logger::instance().clear_sinks();
    return sink->lines;
}

// The one line of that event among them: the engine may log lines of its own.
Line only(const std::vector<Line>& lines, const std::string& event) {
    std::vector<Line> found;
    for (const Line& line : lines) {
        if (line.event.event == event) found.push_back(line);
    }
    REQUIRE_MESSAGE(found.size() == 1, event);
    return found[0];
}

const json& schema() {
    static const json s = ofs::log::load_schema(OFS_CORE_DATA_DIR "/log_schema.json");
    return s;
}

json context() { return {{"match_id", "20261115-101500-ab12cd-m1"}, {"frame", 2}, {"shot_id", "s17"}}; }

// The line is what the schema says, carries the context beside its fields, and its text reads
// back to the same data.
void check_line(const Line& line, const char* subsystem, Level level, const json& with = context()) {
    const auto problem = ofs::log::check(line.event, schema());
    CHECK_MESSAGE(!problem, problem.value_or(""));
    CHECK(line.event.subsystem == subsystem);
    CHECK(line.event.level == level);
    for (auto it = with.begin(); it != with.end(); ++it) {
        CAPTURE(it.key());
        REQUIRE(line.event.data.contains(it.key()));
        CHECK(line.event.data[it.key()] == it.value());
    }
    ofs::log::Event back;
    REQUIRE(ofs::log::from_jsonl(line.text, back));
    CHECK(back.data == line.event.data);
}

FrameState example_frame() {
    std::ifstream in(OFS_TEST_FIXTURES_DIR "/rules/example_frame.json", std::ios::binary);
    REQUIRE(in.good());
    FrameState s;
    std::string error;
    REQUIRE_MESSAGE(frame_from_json(json::parse(in), s, error), error);
    return s;
}

// All 22 balls on, the cue ball placed, every coordinate with all its digits in use.
FrameState full_table() {
    FrameState s = new_frame(0, ofs::phys::opening_rack(20261115, scenario::rig().params), 20261115);
    s.cue_in_hand = false;
    for (BallOnTable& b : s.balls) {
        b.pos.x += 1.0e-7 / 3.0;
        b.pos.y -= 1.0e-7 / 7.0;
    }
    return s;
}

MatchState example_match() {
    std::ifstream in(OFS_TEST_FIXTURES_DIR "/rules/example_match.json", std::ios::binary);
    REQUIRE(in.good());
    MatchState m;
    std::string error;
    REQUIRE_MESSAGE(match_from_json(json::parse(in), m, error), error);
    return m;
}

}  // namespace

TEST_CASE("rules.decision carries what the decision was made from") {
    const scenario::Rig& rig = scenario::rig();
    const FrameState pre = full_table();
    const ShotInput input{true, Ball::blue};
    ShotFacts facts = scenario::quiet_facts(pre);
    facts.first_hit = {3};
    facts.first_hit_t = 0.4;

    Judgement j;
    j.post = example_frame();
    j.fouls = {{FoulKind::first_hit_not_on, 5, 3}};
    j.penalty = 5;
    j.miss = true;
    j.miss_reason = "S3.14(c)";
    j.nominated = input;
    j.calls = {{CallKind::foul, 5, "first_hit_not_on"}, {CallKind::miss, 0, "S3.14(c)"}};

    const Line line = only(logged([&] { log_decision(pre, input, facts, j, rig.ref, context()); }), "rules.decision");
    check_line(line, "rules", Level::info);
    const json& data = line.event.data;
    CHECK(data.size() == 10);
    CHECK(data["pre"] == frame_json(pre));
    CHECK(data["input"] == input_json(input));
    CHECK(data["facts"] == facts_json(facts));
    json judgement = judgement_json(j);
    judgement.erase("post");
    CHECK(data["judgement"] == judgement);
    CHECK_FALSE(data["judgement"].contains("post"));
    CHECK(data["judgement"]["fouls"][0]["clause"] == "S3.11(b)(iv)");
    CHECK(data["post_hash"] == frame_hash(j.post));
    CHECK(data["ref"] == ref_params_values(rig.ref));

    const json post = frame_json(j.post);
    const json& after = data["after"];
    CHECK(after.size() == 10);
    for (const char* key : {"striker", "scores", "on", "free_ball", "cue_in_hand", "pending", "decider", "over",
                            "winner", "end_reason"}) {
        CAPTURE(key);
        REQUIRE(after.contains(key));
        CHECK(after[key] == post[key]);
    }
    CHECK(after["pending"] == "after_foul");
    CHECK(after["scores"] == json::array({24, 31}));
}

TEST_CASE("rules.choice is info when the choice was taken and warn when it was refused") {
    const FrameState before = example_frame();
    FrameState after = before;
    after.pending = Pending::none;
    after.striker = 1;
    const std::vector<Call> calls = {{CallKind::free_ball, 0, ""}};
    json offered = json::array();
    for (const DecisionKind k : options(before, 0)) offered.push_back(std::string(decision_name(k)));

    Line line = only(
        logged([&] { log_choice(before, {DecisionKind::play_again, 0}, true, "", after, calls, context()); }),
        "rules.choice");
    check_line(line, "rules", Level::info);
    CHECK(line.event.data.size() == 12);
    CHECK(line.event.data["before"] == frame_json(before));
    CHECK(line.event.data["player"] == 0);
    CHECK(line.event.data["kind"] == "play_again");
    CHECK(line.event.data["ok"] == true);
    CHECK(line.event.data["error"] == "");
    CHECK(line.event.data["options"] == offered);
    CHECK(line.event.data["calls"] == calls_json(calls));
    CHECK(line.event.data["after"]["striker"] == 1);
    CHECK(line.event.data["after"]["pending"] == "none");
    CHECK(line.event.data["after"].size() == 10);
    CHECK(line.event.data["post_hash"] == frame_hash(after));

    line = only(logged([&] {
                    log_choice(before, {DecisionKind::accept_rerack, 1}, false, "no re-rack was offered", before, {},
                               context());
                }),
                "rules.choice");
    check_line(line, "rules", Level::warn);
    CHECK(line.event.data["player"] == 1);
    CHECK(line.event.data["kind"] == "accept_rerack");
    CHECK(line.event.data["ok"] == false);
    CHECK(line.event.data["error"] == "no re-rack was offered");
    CHECK(line.event.data["calls"] == json::array());
    CHECK(line.event.data["post_hash"] == frame_hash(before));

    // A player who does not exist was offered nothing, and the line still says who was named.
    line = only(logged([&] {
                    log_choice(before, {DecisionKind::play, 7}, false, "player 7", before, {}, context());
                }),
                "rules.choice");
    check_line(line, "rules", Level::warn);
    CHECK(line.event.data["player"] == 7);
    CHECK(line.event.data["options"] == json::array());
}

TEST_CASE("rules.cue_placed names the striker and the place") {
    FrameState s = full_table();
    s.striker = 1;
    Line line = only(logged([&] { log_cue_placed(s, {-1.0475, 0.1 + 0.2}, true, "", context()); }), "rules.cue_placed");
    check_line(line, "rules", Level::info);
    CHECK(line.event.data.size() == 8);
    CHECK(line.event.data["player"] == 1);
    CHECK(line.event.data["x"] == -1.0475);
    CHECK(line.event.data["y"] == 0.1 + 0.2);
    CHECK(line.event.data["ok"] == true);
    CHECK(line.event.data["error"] == "");

    line = only(logged([&] { log_cue_placed(s, {0.5, 0.0}, false, "outside the D", context()); }), "rules.cue_placed");
    check_line(line, "rules", Level::warn);
    CHECK(line.event.data["ok"] == false);
    CHECK(line.event.data["error"] == "outside the D");
}

TEST_CASE("match.started, frame.started, frame.ended and match.ended") {
    MatchState m = example_match();
    json with = context();
    with["person"] = 0;

    Line line = only(logged([&] { log_match_started(m, with); }), "match.started");
    check_line(line, "match", Level::info, with);
    CHECK(line.event.data.size() == 6);
    CHECK(line.event.data["config"] == config_json(m.config));
    CHECK(line.event.data["config"]["seed"] == "147");
    CHECK(line.event.data["breaker"] == 1);

    m.reracks = 3;
    m.frame.seed = std::numeric_limits<std::uint64_t>::max();
    line = only(logged([&] { log_frame_started(m, context()); }), "frame.started");
    check_line(line, "match", Level::info);
    CHECK(line.event.data.size() == 7);
    CHECK(line.event.data["number"] == 2);
    CHECK(line.event.data["breaker"] == 1);
    CHECK(line.event.data["seed"] == "18446744073709551615");
    CHECK(line.event.data["reracks"] == 3);

    line = only(logged([&] { log_frame_ended(m, context()); }), "frame.ended");
    check_line(line, "match", Level::info);
    CHECK(line.event.data.size() == 4);
    CHECK(line.event.data["summary"] == summary_json(m.history.back()));
    CHECK(line.event.data["summary"]["reason"] == "black");

    m.over = true;
    m.winner = 1;
    m.frames = {0, 2};
    m.history.push_back(m.history.back());
    line = only(logged([&] { log_match_ended(m, "frames", context()); }), "match.ended");
    check_line(line, "match", Level::info);
    CHECK(line.event.data.size() == 7);
    CHECK(line.event.data["winner"] == 1);
    CHECK(line.event.data["frames"] == json::array({0, 2}));
    CHECK(line.event.data["frames_played"] == 2);
    CHECK(line.event.data["reason"] == "frames");

    // Abandoned: nobody won, and only finished frames count as played.
    MatchState quit = example_match();
    line = only(logged([&] { log_match_ended(quit, "abandoned", context()); }), "match.ended");
    check_line(line, "match", Level::info);
    CHECK(line.event.data["winner"] == -1);
    CHECK(line.event.data["frames"] == json::array({0, 1}));
    CHECK(line.event.data["frames_played"] == 1);
    CHECK(line.event.data["reason"] == "abandoned");
}

TEST_CASE("frame.ended with no finished frame logs rules.invalid and no frame.ended") {
    MatchState m = example_match();
    m.history.clear();
    const std::vector<Line> lines = logged([&] { log_frame_ended(m, context()); });
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].event.event == "rules.invalid");
    check_line(lines[0], "rules", Level::warn, json::object());
    CHECK(lines[0].event.data["what"] == "frame.ended");
    CHECK(lines[0].event.data["detail"] == "asked to log the end of frame 2 of a match with no finished frame");
}

TEST_CASE("rules.invalid says what was refused and why") {
    const Line line = only(logged([] { log_invalid("nomination", "\"cue\" cannot be declared"); }), "rules.invalid");
    check_line(line, "rules", Level::warn, json::object());
    CHECK(line.event.data == json{{"what", "nomination"}, {"detail", "\"cue\" cannot be declared"}});
}

TEST_CASE("rules.invalid carries the scoped context of its thread while the scope lives") {
    const auto invalid = [] {
        return only(logged([] { log_invalid("judge", "a frame it can't make sense of"); }), "rules.invalid");
    };
    CHECK(scoped_log_context() == json::object());
    {
        const ScopedLogContext match({{"match_id", "m1"}, {"frame", 2}});
        Line line = invalid();
        check_line(line, "rules", Level::warn, json{{"match_id", "m1"}, {"frame", 2}});
        CHECK_FALSE(line.event.data.contains("shot_id"));
        {
            // A stroke within the match: its keys join and win, and the line's own fields stay.
            const ScopedLogContext stroke({{"frame", 3}, {"shot_id", "s9"}, {"what", "not this"}});
            line = invalid();
            check_line(line, "rules", Level::warn, json{{"match_id", "m1"}, {"frame", 3}, {"shot_id", "s9"}});
            CHECK(line.event.data["what"] == "judge");
            CHECK(line.event.data["detail"] == "a frame it can't make sense of");
        }
        line = invalid();
        check_line(line, "rules", Level::warn, json{{"match_id", "m1"}, {"frame", 2}});
        CHECK_FALSE(line.event.data.contains("shot_id"));

        // Another thread has its own, here none.
        json elsewhere;
        std::thread([&] { elsewhere = scoped_log_context(); }).join();
        CHECK(elsewhere == json::object());
        const ScopedLogContext ignored(json("not an object"));
        CHECK(scoped_log_context() == json{{"match_id", "m1"}, {"frame", 2}});
    }
    CHECK(scoped_log_context() == json::object());
    CHECK(invalid().event.data.size() == 2);
    // Only rules.invalid: the other lines take their context from the caller.
    const ScopedLogContext match({{"match_id", "m1"}});
    const Line line = only(logged([] { log_frame_started(example_match(), json::object()); }), "frame.started");
    CHECK_FALSE(line.event.data.contains("match_id"));
}

TEST_CASE("the context sits beside the fields and never replaces one") {
    FrameState s = full_table();
    s.striker = 1;
    const json clash = {{"player", 9}, {"x", "elsewhere"}, {"match_id", "m7"}, {"run", 12}};
    Line line = only(logged([&] { log_cue_placed(s, {-1.0, 0.0}, true, "", clash); }), "rules.cue_placed");
    check_line(line, "rules", Level::info, json{{"match_id", "m7"}, {"run", 12}});
    CHECK(line.event.data["player"] == 1);
    CHECK(line.event.data["x"] == -1.0);

    // No context at all is fine too.
    for (const json& none : {json(), json::object(), json("not an object")}) {
        line = only(logged([&] { log_frame_started(example_match(), none); }), "frame.started");
        check_line(line, "match", Level::info, json::object());
        CHECK(line.event.data.size() == 4);
    }
}

TEST_CASE("a rules.decision line for a full table stays under 16 KB") {
    const scenario::Rig& rig = scenario::rig();
    // The most a line can hold: 22 balls before and in the position kept for a replaced miss, every
    // list in the facts and the judgement full, the widest numbers.
    FrameState pre = full_table();
    pre.original = pre;
    pre.touching = {1, 2, 3};
    pre.original.touching = {1, 2, 3};
    pre.seed = std::numeric_limits<std::uint64_t>::max();
    pre.scores = {147, 147};
    pre.break_points = 147;
    pre.stats[0] = {999, 999, 999, 999, 999, 999, 147};
    pre.stats[1] = {999, 999, 999, 999, 999, 999, 147};
    pre.strokes = 1998;
    pre.on = {OnKind::any_colour, Ball::yellow};
    pre.original.on = {OnKind::colour, Ball::yellow};
    pre.concede_any_time = {false, false};
    pre.last_penalty = 7;
    pre.miss_run = 2;
    pre.quiet_strokes = 11;
    pre.rerack_deadline = 6;

    ShotFacts facts;
    facts.first_hit = {1, 2, 3};
    facts.first_hit_t = 1.0 / 3.0;
    for (int place = 16; place < 22; ++place) facts.pots.push_back({place, 5, 10.0 / 3.0 + place});
    facts.cue_potted = true;
    facts.cue_pocket = 5;
    facts.touching_moved = {1, 2, 3};
    for (const BallOnTable& b : pre.balls) {
        facts.rest.push_back({-b.pos.x, -b.pos.y});
        facts.closest_gap_m.push_back(std::abs(b.pos.x) / 3.0);
    }
    facts.cue_travel_m = 100.0 / 3.0;
    facts.object_travel_m = 200.0 / 3.0;

    Judgement j;
    j.post = pre;
    j.post.pending = Pending::after_foul;
    j.post.end_reason = EndReason::respotted_black;
    for (int i = 0; i <= int(FoulKind::before_nominating); ++i) j.fouls.push_back({FoulKind(i), 7, 21});
    j.penalty = 7;
    j.miss = true;
    j.miss_reason = "S3.14(a)(ii)";
    j.points = 147;
    j.nominated = {true, Ball::yellow};
    j.free_ball_used = true;
    for (int place = 16; place < 22; ++place) j.spotted.push_back(pre.balls[std::size_t(place)]);
    for (int i = 0; i <= int(CallKind::frame_over); ++i) j.calls.push_back({CallKind(i), 7, "first_hit_not_on"});

    const Line full = only(
        logged([&] { log_decision(pre, {true, Ball::yellow}, facts, j, rig.ref, context()); }), "rules.decision");
    check_line(full, "rules", Level::info);
    CHECK(full.event.data["pre"]["balls"].size() == 22);
    CHECK(full.event.data["pre"]["original"]["balls"].size() == 22);
    MESSAGE("rules.decision, 22 balls, every list full: " << full.text.size() << " bytes");
    CHECK(full.text.size() < 16 * 1024);

    // An ordinary stroke from the same table, as the engine judges it.
    const FrameState table = full_table();
    const ShotFacts quiet = scenario::quiet_facts(table);
    const Judgement judged = judge(table, {}, quiet, *rig.oracle, rig.ref);
    const Line plain =
        only(logged([&] { log_decision(table, {}, quiet, judged, rig.ref, context()); }), "rules.decision");
    MESSAGE("rules.decision, 22 balls, a stroke that hit nothing: " << plain.text.size() << " bytes");
    CHECK(plain.text.size() < full.text.size());
}
