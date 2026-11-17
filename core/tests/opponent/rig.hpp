#pragma once

// Shared by the opponent's tests (tests/opponent, written from the spec; tests/opponent_unit, the
// builders' own): the real table, physics, referee and level files, and the named positions of
// tests/fixtures/opponent. Builds on the rules' scenario rig.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/opponent/env.hpp"
#include "ofs/opponent/exhibition.hpp"
#include "ofs/opponent/level.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/rules/serialize.hpp"

#include "scenario.hpp"

namespace ofs::opponent::rig {

struct Levels {
    StandinParams standin;
    Level beginner;
    Level club;
    Level amateur;
    Level pro;
    Level world;
    Level maximum;
    std::vector<Exhibition> exhibitions;
};

// core/data's standin_params.json, the six level files and exhibitions.json, loaded once. A file that is refused aborts the test run with its problems.
inline const Levels& levels() {
    static const Levels instance = [] {
        Levels l;
        std::vector<std::string> problems;
        load_standin_params(rules::scenario::data_file("standin_params.json"), l.standin, problems);
        load_level(rules::scenario::data_file("opponent_beginner.json"), l.beginner, problems);
        load_level(rules::scenario::data_file("opponent_club.json"), l.club, problems);
        load_level(rules::scenario::data_file("opponent_amateur.json"), l.amateur, problems);
        load_level(rules::scenario::data_file("opponent_pro.json"), l.pro, problems);
        load_level(rules::scenario::data_file("opponent_world.json"), l.world, problems);
        load_level(rules::scenario::data_file("opponent_maximum.json"), l.maximum, problems);
        load_exhibitions(rules::scenario::data_file("exhibitions.json"), l.exhibitions, problems);
        if (!problems.empty()) {
            for (const std::string& p : problems) std::fprintf(stderr, "opponent rig: %s\n", p.c_str());
            std::abort();
        }
        return l;
    }();
    return instance;
}

inline Env env() {
    const rules::scenario::Rig& r = rules::scenario::rig();
    return Env{r.table, r.params, *r.oracle, r.ref, levels().standin};
}

// tests/fixtures/opponent/<name>.json: {"note", "cue_ball", "balls": [ball]} and, each optional,
// "cue_in_hand", "on" (the rules' form), "striker", "scores": [a, b], "break_points". Without
// "on", red is on when a red is listed, else the lowest colour. A file that cannot be read aborts.
inline rules::FrameState position(const char* name) {
    const std::string path = std::string(OFS_TEST_FIXTURES_DIR) + "/opponent/" + name + ".json";
    std::ifstream in(path, std::ios::binary);
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    const auto fail = [&](const std::string& why) {
        std::fprintf(stderr, "opponent rig: %s: %s\n", path.c_str(), why.c_str());
        std::abort();
    };
    if (!in || j.is_discarded() || !j.is_object()) fail("cannot be read");
    nlohmann::json whole;
    whole["balls"] = j.value("balls", nlohmann::json::array());
    whole["cue_ball"] = j.value("cue_ball", 0);
    whole["cue_in_hand"] = j.value("cue_in_hand", false);
    whole["on"] = j.contains("on") ? j["on"] : nlohmann::json{{"kind", "red"}};
    whole["free_ball"] = false;
    whole["touching"] = nlohmann::json::array();
    rules::Position parsed;
    std::string error;
    if (!rules::position_from_json(whole, parsed, error)) fail(error);
    rules::FrameState s = rules::scenario::frame(parsed.balls, parsed.cue_ball);
    if (j.contains("on")) s.on = parsed.on;
    s.cue_in_hand = parsed.cue_in_hand;
    s.striker = j.value("striker", 0);
    s.break_points = j.value("break_points", 0);
    if (j.contains("scores")) s.scores = {j["scores"][0].get<int>(), j["scores"][1].get<int>()};
    return s;
}

// A frame about to be broken off by player 0, as a match racks it.
inline rules::FrameState racked(std::uint64_t seed = 1) {
    return rules::new_frame(0, phys::opening_rack(seed, rules::scenario::rig().params), seed);
}

}  // namespace ofs::opponent::rig
