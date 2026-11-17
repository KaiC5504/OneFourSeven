#include <doctest/doctest.h>

#include "ofs/opponent/exhibition.hpp"
#include "ofs/opponent/level.hpp"
#include "ofs/opponent/opponent.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;

TEST_CASE("the level files load and say who they are") {
    const rig::Levels& l = rig::levels();
    CHECK(l.beginner.id == "beginner");
    CHECK(l.beginner.name == "Beginner");
    CHECK(l.beginner.tier == 1);
    CHECK_FALSE(l.beginner.maximum);
    CHECK(l.maximum.id == "maximum");
    CHECK(l.maximum.tier == 0);
    CHECK(l.maximum.maximum);
    CHECK(l.maximum.aim_sigma_rad == 0.0);
    CHECK(l.maximum.speed_sigma == 0.0);
    CHECK(l.maximum.tip_sigma == 0.0);
}

TEST_CASE("a level file with a value missing, unknown or out of range is refused whole") {
    const nlohmann::json good = rules::scenario::data_file("opponent_beginner.json");
    Level untouched;
    untouched.id = "untouched";

    nlohmann::json missing = good;
    missing["values"].erase("speed_sigma");
    std::vector<std::string> problems;
    CHECK_FALSE(load_level(missing, untouched, problems));
    CHECK(problems.size() == 1);

    nlohmann::json unknown = good;
    unknown["values"]["luck"] = 1.0;
    problems.clear();
    CHECK_FALSE(load_level(unknown, untouched, problems));

    nlohmann::json wild = good;
    wild["values"]["aim_sigma_rad"] = 2.0;
    problems.clear();
    CHECK_FALSE(load_level(wild, untouched, problems));

    nlohmann::json fractional = good;
    fractional["values"]["max_sims"] = 12.5;
    problems.clear();
    CHECK_FALSE(load_level(fractional, untouched, problems));

    CHECK(untouched.id == "untouched");
}

TEST_CASE("level_json carries every value by its file name") {
    const nlohmann::json j = level_json(rig::levels().beginner);
    for (const std::string& name : level_value_names()) CHECK(j.contains(name));
    CHECK(j["id"] == "beginner");
}

TEST_CASE("the exhibition tables are full tables with the striker on a red") {
    const rig::Levels& l = rig::levels();
    REQUIRE_FALSE(l.exhibitions.empty());
    for (const Exhibition& e : l.exhibitions) {
        CHECK(e.balls.size() == 22);
        const rules::FrameState s = exhibition_frame(e, 1, 7);
        CHECK(s.striker == 1);
        CHECK(s.on.kind == rules::OnKind::red);
        CHECK_FALSE(s.cue_in_hand);
        CHECK(s.pending == rules::Pending::none);
        CHECK(rules::places_on(s).size() == 15);
    }
}

TEST_CASE("a layout with a ball missing or two balls overlapping is refused") {
    nlohmann::json file = rules::scenario::data_file("exhibitions.json");
    nlohmann::json short_one = file;
    short_one["layouts"][0]["balls"].erase(1);
    std::vector<Exhibition> out;
    std::vector<std::string> problems;
    CHECK_FALSE(load_exhibitions(short_one, out, problems));
    CHECK(out.empty());

    nlohmann::json stacked = file;
    stacked["layouts"][0]["balls"][1]["x"] = stacked["layouts"][0]["balls"][2]["x"];
    stacked["layouts"][0]["balls"][1]["y"] = stacked["layouts"][0]["balls"][2]["y"];
    problems.clear();
    CHECK_FALSE(load_exhibitions(stacked, out, problems));
}

TEST_CASE("every named position loads, and a stroke is always chosen from it") {
    for (const char* name : {"straight_red", "cut_red", "long_red", "snookered", "in_hand", "colours",
                             "black_only", "last_red", "after_red"}) {
        CAPTURE(name);
        const rules::FrameState s = rig::position(name);
        CHECK_FALSE(s.over);
        for (const Level* level : {&rig::levels().beginner, &rig::levels().maximum}) {
            Rng rng(11);
            const Stroke stroke = choose_stroke(s, *level, rig::env(), rng);
            CHECK(stroke.why.is_object());
            CHECK(stroke.why.value("level", std::string()) == level->id);
            if (!stroke.decides) {
                CHECK(stroke.strike.speed_m_s > 0.0);
                CHECK(stroke.places_cue == s.cue_in_hand);
            }
        }
    }
    const rules::FrameState rack = rig::racked();
    Rng rng(3);
    const Stroke off = choose_stroke(rack, rig::levels().beginner, rig::env(), rng);
    CHECK_FALSE(off.decides);
    CHECK(off.places_cue);
}
