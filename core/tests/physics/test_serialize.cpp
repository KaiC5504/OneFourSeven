#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "ofs/physics/serialize.hpp"

using namespace ofs::phys;

TEST_CASE("a ball round-trips exactly") {
    BallState b;
    b.ball = ofs::table::Ball::red;
    b.index = 14;
    b.r = {0.1 + 0.2, -1.0 / 3.0, 0.0};
    b.v = {1e-300, -2.5, 0.0};
    b.w = {3.0000000000000004, -0.0, 123.456789012345678};
    b.motion = Motion::sliding;
    b.pocket = 2;
    BallState back;
    std::string error;
    REQUIRE(from_json(nlohmann::json::parse(to_json(b).dump()), back, error));
    CHECK(back.ball == b.ball);
    CHECK(back.index == b.index);
    CHECK(back.r == b.r);
    CHECK(back.v == b.v);
    CHECK(back.w == b.w);
    CHECK(back.motion == b.motion);
    CHECK(back.pocket == b.pocket);
}

TEST_CASE("optional fields default") {
    BallState b;
    std::string error;
    REQUIRE(from_json(nlohmann::json{{"ball", "black"}, {"r", {0.5, 0.25}}}, b, error));
    CHECK(b.v == ofs::Vec3{});
    CHECK(b.w == ofs::Vec3{});
    CHECK(b.motion == Motion::stationary);
    CHECK(b.pocket == -1);
}

TEST_CASE("malformed balls are refused with a reason") {
    const nlohmann::json bad[] = {
        {{"ball", "purple"}, {"r", {0.0, 0.0}}},
        {{"ball", "red"}, {"index", 15}, {"r", {0.0, 0.0}}},
        {{"ball", "pink"}, {"index", 1}, {"r", {0.0, 0.0}}},
        {{"ball", "red"}, {"r", {0.0}}},
        {{"ball", "red"}, {"r", {0.0, "x"}}},
        {{"ball", "red"}, {"r", {0.0, 0.0}}, {"w", {0.0, 0.0}}},
        {{"ball", "red"}, {"r", {0.0, 0.0}}, {"motion", "flying"}},
        {{"ball", "red"}, {"r", {0.0, 0.0}}, {"pocket", -2}},
    };
    for (const auto& j : bad) {
        CAPTURE(j.dump());
        BallState b;
        std::string error;
        CHECK_FALSE(from_json(j, b, error));
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("strikes round-trip and need every field") {
    CueStrike c{0.25, -0.3, 0.5, 0.1, 3.75};
    CueStrike back;
    std::string error;
    REQUIRE(from_json(to_json(c), back, error));
    CHECK(back.aim_rad == c.aim_rad);
    CHECK(back.tip_x == c.tip_x);
    CHECK(back.tip_y == c.tip_y);
    CHECK(back.elevation_rad == c.elevation_rad);
    CHECK(back.speed_m_s == c.speed_m_s);
    nlohmann::json missing = to_json(c);
    missing.erase("speed_m_s");
    CHECK_FALSE(from_json(missing, back, error));
}

TEST_CASE("every named position fixture parses") {
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(std::string(OFS_TEST_FIXTURES_DIR) + "/positions")) {
        if (entry.path().extension() != ".json") continue;
        CAPTURE(entry.path().string());
        std::ifstream in(entry.path());
        const nlohmann::json j = nlohmann::json::parse(in);
        TableState s;
        CueStrike c;
        std::string error;
        CHECK_MESSAGE(from_json(j["start"], s, error), error);
        CHECK_MESSAGE(from_json(j["strike"], c, error), error);
        CHECK(j["cue_ball"].get<int>() < static_cast<int>(s.balls.size()));
        ++count;
    }
    CHECK(count >= 13);
}
