#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "ofs/rules/serialize.hpp"

using namespace ofs::rules;
using nlohmann::json;

namespace {

json fixture(const char* name) {
    std::ifstream in(std::string(OFS_TEST_FIXTURES_DIR) + "/rules/" + name, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), name);
    return json::parse(in);
}

FrameState example_frame() {
    FrameState s;
    std::string error;
    REQUIRE_MESSAGE(frame_from_json(fixture("example_frame.json"), s, error), error);
    return s;
}

}  // namespace

TEST_CASE("example_frame.json reads into the frame it describes and writes back unchanged") {
    const json file = fixture("example_frame.json");
    const FrameState s = example_frame();
    CHECK(frame_json(s) == file);
    CHECK(json::parse(frame_json(s).dump()) == file);

    REQUIRE(s.balls.size() == 9);
    CHECK(s.balls[2].ball == Ball::red);
    CHECK(s.balls[2].index == 7);
    CHECK(s.balls[2].pos.x == 1.1);
    CHECK(s.balls[2].pos.y == -0.15);
    CHECK(s.cue_ball == 0);
    CHECK(s.on.kind == OnKind::red);
    CHECK(s.free_ball);
    CHECK(s.seed == 147);
    CHECK(s.striker == 0);
    CHECK(s.scores == std::array<int, 2>{24, 31});
    CHECK(s.stats[1].highest_break == 23);
    CHECK(s.concede_any_time == std::array<bool, 2>{true, false});
    CHECK(s.pending == Pending::after_foul);
    CHECK(s.offender == 1);
    CHECK(s.last_miss);
    REQUIRE(s.original.balls.size() == 9);
    CHECK(s.original.balls[0].pos.x == -1.2);
    CHECK_FALSE(s.original.free_ball);
    CHECK(s.winner == -1);
    CHECK(s.end_reason == EndReason::none);
}

TEST_CASE("the frame view reads as the same frame: its view is ignored") {
    FrameState s;
    std::string error;
    REQUIRE_MESSAGE(frame_from_json(fixture("example_frame_view.json"), s, error), error);
    CHECK(frame_json(s) == fixture("example_frame.json"));
}

TEST_CASE("example_judgement.json is judgement_json's form and its post is the example frame") {
    const json file = fixture("example_judgement.json");
    Judgement j;
    std::string error;
    REQUIRE_MESSAGE(frame_from_json(file["post"], j.post, error), error);
    CHECK(frame_json(j.post) == file["post"]);
    CHECK(frame_json(j.post) == fixture("example_frame.json"));

    j.fouls = {{FoulKind::no_hit, 4, -1}};
    j.penalty = 4;
    j.miss = true;
    j.miss_reason = "S3.14(c)";
    j.calls = {{CallKind::foul, 4, "no_hit"}, {CallKind::miss, 0, "S3.14(c)"}, {CallKind::free_ball, 0, ""}};
    CHECK(judgement_json(j) == file);
}

TEST_CASE("example_match.json reads and writes back unchanged") {
    const json file = fixture("example_match.json");
    MatchState m;
    std::string error;
    REQUIRE_MESSAGE(match_from_json(file, m, error), error);
    CHECK(match_json(m) == file);
    CHECK(json::parse(match_json(m).dump()) == file);

    CHECK(m.config.best_of == 3);
    CHECK(m.config.first_breaker == -1);
    CHECK(m.config.seed == 147);
    CHECK(m.frames == std::array<int, 2>{0, 1});
    CHECK(m.frame_number == 2);
    CHECK(m.breaker == 1);
    REQUIRE(m.history.size() == 1);
    CHECK(m.history[0].reason == EndReason::black);
    CHECK(m.history[0].scores == std::array<int, 2>{41, 63});
    CHECK(m.history[0].stats[1].highest_break == 31);
    CHECK(frame_json(m.frame) == fixture("example_frame.json"));
}

// The view comes from the engine's helpers, so this holds once the real engine is in.
TEST_CASE("frame_view_json gives example_frame_view.json" * doctest::test_suite("with the engine")) {
    const json view = frame_view_json(example_frame());
    const json file = fixture("example_frame_view.json");
    CHECK(view["view"] == file["view"]);
    CHECK((view == file));
}

TEST_CASE("frame_view_json is the frame plus a view of the documented shape") {
    const FrameState s = example_frame();
    json view = frame_view_json(s);
    REQUIRE(view.contains("view"));
    const json v = view["view"];
    view.erase("view");
    CHECK(view == frame_json(s));
    CHECK(v.size() == 6);
    CHECK(v.at("points_remaining").is_number_integer());
    REQUIRE(v.at("required").is_array());
    CHECK(v["required"].size() == 2);
    CHECK(v.at("on_places").is_array());
    CHECK(v.at("nominations").is_array());
    REQUIRE(v.at("options").is_array());
    REQUIRE(v["options"].size() == 2);
    CHECK(v["options"][0].is_array());
    CHECK(v["options"][1].is_array());
    CHECK(v.at("leader") == 1);

    FrameState level = s;
    level.scores = {31, 31};
    CHECK(frame_view_json(level)["view"]["leader"] == -1);
    level.scores = {32, 31};
    CHECK(frame_view_json(level)["view"]["leader"] == 0);
}
