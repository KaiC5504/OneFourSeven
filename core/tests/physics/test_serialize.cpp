#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
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

TEST_CASE("non-finite doubles are written as strings and read back") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    std::string error;

    BallState b;
    b.ball = ofs::table::Ball::pink;
    b.r = {nan, 0.25, 0.0};
    b.v = {inf, -inf, 0.0};
    b.w = {-0.0, nan, -inf};
    const nlohmann::json j = to_json(b);
    CHECK(j["r"] == nlohmann::json::array({"nan", 0.25}));
    CHECK(j["v"] == nlohmann::json::array({"inf", "-inf"}));
    CHECK(j["w"][1] == "nan");
    CHECK(j["w"][2] == "-inf");
    CHECK(j.dump().find("null") == std::string::npos);
    BallState back;
    REQUIRE_MESSAGE(from_json(nlohmann::json::parse(j.dump()), back, error), error);
    CHECK(std::isnan(back.r.x));
    CHECK(back.r.y == 0.25);
    CHECK(back.v.x == inf);
    CHECK(back.v.y == -inf);
    CHECK(back.w.x == 0.0);
    CHECK(std::signbit(back.w.x));
    CHECK(std::isnan(back.w.y));
    CHECK(back.w.z == -inf);

    TableState s;
    s.balls = {BallState{}, b};
    s.balls[0].r = {-1.0 / 3.0, 0.1 + 0.2, 0.0};
    TableState back_s;
    REQUIRE_MESSAGE(from_json(nlohmann::json::parse(to_json(s).dump()), back_s, error), error);
    REQUIRE(back_s.balls.size() == 2);
    CHECK(back_s.balls[0].r == s.balls[0].r);
    CHECK(std::isnan(back_s.balls[1].r.x));
    CHECK(back_s.balls[1].v.y == -inf);
    CHECK(to_json(back_s).dump() == to_json(s).dump());

    const CueStrike c{nan, inf, -inf, 0.1, nan};
    const nlohmann::json cj = to_json(c);
    CHECK(cj["aim_rad"] == "nan");
    CHECK(cj["tip_x"] == "inf");
    CHECK(cj["tip_y"] == "-inf");
    CHECK(cj["elevation_rad"] == 0.1);
    CueStrike back_c;
    REQUIRE_MESSAGE(from_json(nlohmann::json::parse(cj.dump()), back_c, error), error);
    CHECK(std::isnan(back_c.aim_rad));
    CHECK(back_c.tip_x == inf);
    CHECK(back_c.tip_y == -inf);
    CHECK(back_c.elevation_rad == 0.1);
    CHECK(std::isnan(back_c.speed_m_s));
}

TEST_CASE("only the three non-finite strings stand in for a number") {
    for (const nlohmann::json& value : {nlohmann::json("NaN"), nlohmann::json("Infinity"), nlohmann::json("+inf"),
                                        nlohmann::json("1.5"), nlohmann::json(""), nlohmann::json(nullptr),
                                        nlohmann::json(std::numeric_limits<double>::quiet_NaN())}) {
        CAPTURE(value.dump());
        BallState b;
        std::string error;
        CHECK_FALSE(from_json(nlohmann::json{{"ball", "red"}, {"r", {0.0, value}}}, b, error));
        CHECK_MESSAGE(error.find("\"nan\"") != std::string::npos, error);
        nlohmann::json strike = to_json(CueStrike{0.25, -0.3, 0.5, 0.1, 3.75});
        strike["tip_y"] = value;
        CueStrike c;
        CHECK_FALSE(from_json(strike, c, error));
        CHECK_MESSAGE(error.find("tip_y") != std::string::npos, error);
    }
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
