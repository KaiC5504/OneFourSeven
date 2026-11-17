// The level and exhibition loaders (level.hpp, exhibition.hpp): what each refuses, and that a
// refused level leaves `out` alone.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ofs/opponent/exhibition.hpp"
#include "ofs/opponent/level.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;

namespace {

nlohmann::json beginner_file() { return rules::scenario::data_file("opponent_beginner.json"); }

// Refused, `out` untouched, and a problem naming `key`.
void check_refused(const nlohmann::json& file, const std::string& key) {
    Level out;
    out.id = "untouched";
    out.max_sims = 7;
    std::vector<std::string> problems;
    CHECK_FALSE(load_level(file, out, problems));
    CHECK(out.id == "untouched");
    CHECK(out.max_sims == 7);
    REQUIRE_FALSE(problems.empty());
    bool named = false;
    for (const std::string& p : problems) named = named || p.rfind(key, 0) == 0;
    CHECK(named);
}

bool exhibitions_load(const nlohmann::json& file, std::vector<std::string>& problems) {
    std::vector<Exhibition> out;
    return load_exhibitions(file, out, problems);
}

nlohmann::json& first_layout(nlohmann::json& file) { return file["layouts"][0]; }

int place_of(const nlohmann::json& layout, const char* ball, int index = 0) {
    const nlohmann::json& balls = layout["balls"];
    for (std::size_t i = 0; i < balls.size(); ++i) {
        if (balls[i]["ball"] == ball && balls[i]["index"] == index) return int(i);
    }
    return -1;
}

}  // namespace

TEST_CASE("a level value of the wrong type is refused, out unchanged") {
    {
        nlohmann::json f = beginner_file();
        f["values"]["speeds"] = "2";
        check_refused(f, "speeds");
    }
    {
        nlohmann::json f = beginner_file();
        f["values"]["aim_sigma_rad"] = true;
        check_refused(f, "aim_sigma_rad");
    }
    {
        nlohmann::json f = beginner_file();
        f["values"]["maximum"] = 0;
        check_refused(f, "maximum");
    }
    {
        nlohmann::json f = beginner_file();
        f["values"]["name"] = 1;
        check_refused(f, "name");
    }
    {
        nlohmann::json f = beginner_file();
        f["values"]["name"] = "";
        check_refused(f, "name");
    }
    {
        nlohmann::json f = beginner_file();
        f["values"]["depth"] = nlohmann::json::array({1});
        check_refused(f, "depth");
    }
}

TEST_CASE("a level file without its id, or without values, is refused") {
    {
        nlohmann::json f = beginner_file();
        f.erase("id");
        check_refused(f, "id");
    }
    {
        nlohmann::json f = beginner_file();
        f["id"] = 3;
        check_refused(f, "id");
    }
    {
        nlohmann::json f = beginner_file();
        f.erase("values");
        check_refused(f, "values");
    }
    {
        nlohmann::json f = beginner_file();
        f["values"] = nlohmann::json::array();
        check_refused(f, "values");
    }
    check_refused(nlohmann::json::array(), "values");
}

TEST_CASE("each value at either end of its range loads; just past either end is refused") {
    struct Edge {
        const char* key;
        double low;
        double high;
    };
    // level.cpp's bounds, inclusive.
    const Edge edges[] = {{"tier", 0, 5},           {"speeds", 1, 16},     {"spins", 1, 25},
                          {"depth", 0, 3},          {"max_sims", 1, 1000000}, {"position_weight", 0.0, 1.0},
                          {"safety_below", 0.0, 1.0}, {"awkward_factor", 1.0, 10.0}};
    for (const Edge& e : edges) {
        CAPTURE(e.key);
        const bool integer = std::string(e.key) == "tier" || std::string(e.key) == "speeds" ||
                             std::string(e.key) == "spins" || std::string(e.key) == "depth" ||
                             std::string(e.key) == "max_sims";
        for (const double v : {e.low, e.high}) {
            nlohmann::json f = beginner_file();
            f["values"][e.key] = integer ? nlohmann::json(int(v)) : nlohmann::json(v);
            Level out;
            std::vector<std::string> problems;
            CHECK(load_level(f, out, problems));
        }
        nlohmann::json below = beginner_file();
        below["values"][e.key] = integer ? nlohmann::json(int(e.low) - 1) : nlohmann::json(e.low - 1e-9);
        check_refused(below, e.key);
        nlohmann::json above = beginner_file();
        above["values"][e.key] = integer ? nlohmann::json(int(e.high) + 1) : nlohmann::json(e.high + 1e-9);
        check_refused(above, e.key);
    }
}

TEST_CASE("every problem in a level file is reported, not only the first") {
    nlohmann::json f = beginner_file();
    f["values"].erase("speeds");
    f["values"]["spins"] = "one";
    f["values"]["luck"] = 1;
    Level out;
    std::vector<std::string> problems;
    CHECK_FALSE(load_level(f, out, problems));
    CHECK(problems.size() == 3);
}

TEST_CASE("an exhibition layout with two cue balls, a colour twice or the cue_ball on another ball is refused") {
    const nlohmann::json file = rules::scenario::data_file("exhibitions.json");
    {
        nlohmann::json f = file;
        nlohmann::json extra = first_layout(f)["balls"][std::size_t(place_of(first_layout(f), "cue"))];
        extra["x"] = 0.0;
        extra["y"] = -0.7;
        first_layout(f)["balls"].push_back(extra);
        std::vector<std::string> problems;
        CHECK_FALSE(exhibitions_load(f, problems));
    }
    {
        nlohmann::json f = file;
        nlohmann::json extra = first_layout(f)["balls"][std::size_t(place_of(first_layout(f), "pink"))];
        extra["x"] = 0.0;
        extra["y"] = -0.7;
        first_layout(f)["balls"].push_back(extra);
        std::vector<std::string> problems;
        CHECK_FALSE(exhibitions_load(f, problems));
    }
    {
        nlohmann::json f = file;
        first_layout(f)["cue_ball"] = place_of(first_layout(f), "red", 0);
        std::vector<std::string> problems;
        CHECK_FALSE(exhibitions_load(f, problems));
    }
    {
        nlohmann::json f = file;
        first_layout(f)["cue_ball"] = int(first_layout(f)["balls"].size());
        std::vector<std::string> problems;
        CHECK_FALSE(exhibitions_load(f, problems));
    }
}

TEST_CASE("an exhibition layout with fewer than fifteen reds is refused: a maximum needs them all") {
    // exhibition.hpp: all fifteen reds and the six colours on the table.
    nlohmann::json f = rules::scenario::data_file("exhibitions.json");
    const int red = place_of(first_layout(f), "red", 14);
    REQUIRE(red >= 0);
    first_layout(f)["balls"].erase(std::size_t(red));
    std::vector<Exhibition> out;
    std::vector<std::string> problems;
    CHECK_FALSE(load_exhibitions(f, out, problems));
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("red 14") != std::string::npos);
    CHECK(out.size() == f["layouts"].size() - 1);
}

TEST_CASE("an exhibitions file that is not a list of layouts is refused") {
    std::vector<std::string> problems;
    CHECK_FALSE(exhibitions_load(nlohmann::json::object(), problems));
    problems.clear();
    CHECK_FALSE(exhibitions_load({{"layouts", 3}}, problems));
    problems.clear();
    CHECK_FALSE(exhibitions_load({{"layouts", nlohmann::json::array({nlohmann::json::object()})}}, problems));
}
