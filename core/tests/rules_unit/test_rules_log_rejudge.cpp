#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/rules/rules_log.hpp"
#include "ofs/rules/serialize.hpp"

#include "scenario.hpp"

using namespace ofs::rules;
using nlohmann::json;

namespace {

struct Capture : ofs::log::Sink {
    std::vector<std::string> decisions;
    void write(const ofs::log::Event& event, const std::string& line) override {
        if (event.event == "rules.decision") decisions.push_back(line);
    }
};

// The opening table with the cue ball placed.
FrameState full_table() {
    FrameState s = new_frame(0, ofs::phys::opening_rack(20261115, scenario::rig().params), 20261115);
    s.cue_in_hand = false;
    return s;
}

// The stroke judged and logged as the game does it; the line's data as a tool reads it back from
// the log's text.
json decision_data(const FrameState& pre, const ShotInput& input, const ShotFacts& facts) {
    const scenario::Rig& rig = scenario::rig();
    const Judgement j = judge(pre, input, facts, *rig.oracle, rig.ref);
    auto sink = std::make_shared<Capture>();
    ofs::log::Logger::instance().clear_sinks();
    ofs::log::Logger::instance().add_sink(sink);
    log_decision(pre, input, facts, j, rig.ref, {{"match_id", "m1"}, {"frame", 1}, {"shot_id", "s1"}});
    ofs::log::Logger::instance().clear_sinks();
    REQUIRE(sink->decisions.size() == 1);
    ofs::log::Event event;
    REQUIRE(ofs::log::from_jsonl(sink->decisions[0], event));
    return event.data;
}

// A stroke at the opening table that hit nothing.
json missed_everything() {
    const FrameState pre = full_table();
    return decision_data(pre, {}, scenario::quiet_facts(pre));
}

std::string rejudged(const json& data) { return rejudge(data, scenario::rig().table); }

bool starts_with(const std::string& text, const std::string& start) { return text.rfind(start, 0) == 0; }

}  // namespace

TEST_CASE("a logged decision re-judges to itself") {
    CHECK(rejudged(missed_everything()) == "");

    // A red hit first, nothing potted.
    const FrameState pre = full_table();
    ShotFacts facts = scenario::quiet_facts(pre);
    facts.first_hit = {scenario::place_of(pre, Ball::red, 0)};
    facts.first_hit_t = 0.8;
    facts.closest_gap_m[std::size_t(facts.first_hit[0])] = 0.0;
    facts.cue_travel_m = 2.4;
    facts.object_travel_m = 0.9;
    CHECK(rejudged(decision_data(pre, {}, facts)) == "");

    // A declared colour that is not open to declare.
    CHECK(rejudged(decision_data(pre, {true, Ball::blue}, facts)) == "");
}

TEST_CASE("rejudge names the first part of the judgement that differs") {
    json data = missed_everything();
    data["judgement"]["penalty"] = 99;
    CHECK(starts_with(rejudged(data), "judgement.penalty: logged 99, re-judged "));

    data = missed_everything();
    data["judgement"]["fouls"].push_back(foul_json({FoulKind::push_stroke, 4, 1}));
    CHECK(starts_with(rejudged(data), "judgement.fouls: logged "));
    CHECK(rejudged(data).find(" entries, re-judged ") != std::string::npos);

    data = missed_everything();
    data["judgement"]["miss_reason"] = "the referee felt like it";
    CHECK(starts_with(rejudged(data), "judgement.miss_reason: logged \"the referee felt like it\", re-judged \""));

    // The parts are compared in the order a person reads them, not the JSON's.
    data = missed_everything();
    data["judgement"]["calls"].push_back({{"kind", "warning"}, {"value", 0}, {"detail", ""}});
    data["judgement"]["points"] = 147;
    CHECK(starts_with(rejudged(data), "judgement.points: logged 147, re-judged "));

    data = missed_everything();
    data["judgement"].erase("miss");
    CHECK(rejudged(data) == "judgement.miss: missing from the line");

    data = missed_everything();
    data["judgement"]["verdict"] = "guilty";
    CHECK(rejudged(data) == "judgement.verdict: logged \"guilty\", absent when re-judged");
}

TEST_CASE("rejudge names what differs in the frame after the stroke") {
    json data = missed_everything();
    const std::string hash = data["post_hash"].get<std::string>();
    data["pre"]["scores"][1] = data["pre"]["scores"][1].get<int>() + 1;
    const std::string scores = rejudged(data);
    CHECK(starts_with(scores, "after.scores[1]: logged "));
    CHECK(scores.find("(post_hash: logged " + hash + ", re-judged ") != std::string::npos);

    // The seed is in the frame but not in `after`.
    data = missed_everything();
    data["pre"]["seed"] = "7";
    const std::string seed = rejudged(data);
    CHECK(starts_with(seed, "post_hash: logged " + hash + ", re-judged "));
    CHECK(seed.find("differs outside \"after\"") != std::string::npos);

    data = missed_everything();
    data["post_hash"] = "0000000000000000";
    CHECK(rejudged(data) ==
          "post_hash: logged 0000000000000000, re-judged " + hash + "; the frame after differs outside \"after\"");

    // Without `after` the hash alone still tells.
    data = missed_everything();
    data.erase("after");
    CHECK(rejudged(data) == "");
    data["pre"]["scores"][1] = data["pre"]["scores"][1].get<int>() + 1;
    CHECK(starts_with(rejudged(data), "post_hash: logged " + hash));
}

TEST_CASE("rejudge says what is wrong with a line it cannot read") {
    CHECK(rejudged(json::array()) == "data: want an object");
    CHECK(rejudged(json()) == "data: want an object");

    for (const char* key : {"pre", "input", "facts", "judgement", "post_hash", "ref"}) {
        json data = missed_everything();
        data.erase(key);
        CHECK(rejudged(data) == std::string(key) + ": missing from the line");
    }

    json data = missed_everything();
    data["pre"]["striker"] = 7;
    CHECK(rejudged(data) == "pre.striker: 7 is not a player (0 or 1)");

    data = missed_everything();
    data["pre"]["balls"][4].erase("x");
    CHECK(rejudged(data) == "pre.balls[4].x: missing");

    data = missed_everything();
    data["input"]["nominated"] = "purple";
    CHECK(rejudged(data) == "input.nominated: unknown name \"purple\"");

    data = missed_everything();
    data["facts"]["first_hit"] = {22};
    CHECK(rejudged(data) == "facts.first_hit[0]: place 22 is out of range (22 balls)");

    data = missed_everything();
    data["facts"]["rest"].erase(std::size_t(21));
    data["facts"]["closest_gap_m"].erase(std::size_t(21));
    CHECK(rejudged(data) == "facts.rest: 21 entries for the 22 balls of pre");

    for (const char* key : {"pre", "input", "facts", "judgement", "ref"}) {
        data = missed_everything();
        data[key] = "foul";
        CHECK(rejudged(data) == std::string(key) + ": want an object");
    }

    data = missed_everything();
    data["post_hash"] = 12;
    CHECK(rejudged(data) == "post_hash: want a string");
}

TEST_CASE("rejudge's answer stays one line a person can read") {
    json data = missed_everything();
    data["judgement"]["spotted"] = std::string(5000, 'x');
    const std::string answer = rejudged(data);
    CHECK(starts_with(answer, "judgement.spotted: logged \"xxx"));
    CHECK(answer.size() < 200);
    CHECK(answer.find('\n') == std::string::npos);
}

// The referee's values are read back by the real ref_params_from_values.
TEST_CASE("rejudge refuses a line whose referee values are not whole" * doctest::test_suite("with the engine")) {
    json data = missed_everything();
    REQUIRE(data["ref"].contains("touching_gap_m"));
    data["ref"].erase("touching_gap_m");
    CHECK(starts_with(rejudged(data), "ref: "));
    CHECK(rejudged(data).find("touching_gap_m") != std::string::npos);
}
