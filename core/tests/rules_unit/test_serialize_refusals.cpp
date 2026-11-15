#include <doctest/doctest.h>

#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "ofs/rules/serialize.hpp"

using namespace ofs::rules;
using nlohmann::json;

namespace {

json fixture(const char* name) {
    std::ifstream in(std::string(OFS_TEST_FIXTURES_DIR) + "/rules/" + name, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), name);
    return json::parse(in);
}

json example_frame() { return fixture("example_frame.json"); }

json example_facts() {
    ShotFacts f;
    f.first_hit = {2, 5};
    f.first_hit_t = 0.25;
    f.pots = {{2, 4, 1.5}, {7, 0, 2.5}};
    f.cue_potted = true;
    f.cue_pocket = 3;
    f.touching_moved = {1};
    for (int i = 0; i < 8; ++i) {
        f.rest.push_back({0.1 * i, -0.05 * i});
        f.closest_gap_m.push_back(0.01 * i);
    }
    f.cue_travel_m = 2.0;
    f.object_travel_m = 1.25;
    return facts_json(f);
}

// A position with a colour on and a touching ball, so every optional part is present.
json example_position() {
    json p = example_frame()["original"];
    p["on"] = {{"kind", "colour"}, {"colour", "blue"}};
    p["touching"] = {6};
    return p;
}

struct Node {
    json::json_pointer pointer;
    std::string path;  // as the readers name it: original.balls[2].x
    bool member;       // an object's key, not an array's entry
};

void collect(const json& node, const json::json_pointer& pointer, const std::string& path, std::vector<Node>& out) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            const std::string here = path.empty() ? it.key() : path + "." + it.key();
            out.push_back({pointer / it.key(), here, true});
            collect(it.value(), pointer / it.key(), here, out);
        }
    } else if (node.is_array()) {
        for (std::size_t i = 0; i < node.size(); ++i) {
            const std::string here = path + "[" + std::to_string(i) + "]";
            out.push_back({pointer / i, here, false});
            collect(node[i], pointer / i, here, out);
        }
    }
}

json wrong_type_for(const json& v) { return v.is_string() ? json(12) : json("wrong"); }

// Every key of `good`, at any depth, is needed, and every value must be of its type: the reader
// refuses the document without it or with another type there, and its reason starts with the path.
template <typename T>
void check_every_value_is_needed(const json& good, bool (*from)(const json&, T&, std::string&)) {
    T out;
    std::string error;
    REQUIRE_MESSAGE(from(good, out, error), error);
    std::vector<Node> nodes;
    collect(good, json::json_pointer(), "", nodes);
    REQUIRE_FALSE(nodes.empty());
    for (const Node& n : nodes) {
        CAPTURE(n.path);
        if (n.member) {
            json doc = good;
            doc.at(n.pointer.parent_pointer()).erase(n.pointer.back());
            error.clear();
            CHECK_FALSE(from(doc, out, error));
            CHECK(error == n.path + ": missing");
        }
        json doc = good;
        doc.at(n.pointer) = wrong_type_for(good.at(n.pointer));
        error.clear();
        CHECK_FALSE(from(doc, out, error));
        CHECK_MESSAGE(error.rfind(n.path + ": ", 0) == 0, error);
    }
}

struct Change {
    const char* pointer;
    json value;
    const char* reason;  // what the reader's reason must start with
};

template <typename T>
void check_refused(const json& good, bool (*from)(const json&, T&, std::string&), const std::vector<Change>& changes) {
    T out;
    std::string error;
    REQUIRE_MESSAGE(from(good, out, error), error);
    for (const Change& c : changes) {
        CAPTURE(c.pointer);
        CAPTURE(c.value.dump());
        json doc = good;
        doc[json::json_pointer(c.pointer)] = c.value;
        error.clear();
        CHECK_FALSE(from(doc, out, error));
        CHECK_MESSAGE(error.rfind(c.reason, 0) == 0, error);
    }
}

const double kNan = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

}  // namespace

TEST_CASE("a frame is refused without any one key, or with another type anywhere") {
    check_every_value_is_needed<FrameState>(example_frame(), &frame_from_json);
}

TEST_CASE("a position is refused without any one key, or with another type anywhere") {
    check_every_value_is_needed<Position>(example_position(), &position_from_json);
}

TEST_CASE("a match is refused without any one key, or with another type anywhere") {
    check_every_value_is_needed<MatchState>(fixture("example_match.json"), &match_from_json);
}

TEST_CASE("facts are refused without any one key, or with another type anywhere") {
    check_every_value_is_needed<ShotFacts>(example_facts(), &facts_from_json);
}

TEST_CASE("an input, a decision and a config are refused without any one key, or with another type") {
    check_every_value_is_needed<ShotInput>(json{{"nominated", "blue"}}, &input_from_json);
    check_every_value_is_needed<Decision>(json{{"kind", "replace"}, {"player", 1}}, &decision_from_json);
    check_every_value_is_needed<MatchConfig>(fixture("example_match.json")["config"], &config_from_json);
}

TEST_CASE("anything but an object is refused") {
    std::string error;
    for (const json& j : {json(), json::array(), json("frame"), json(3)}) {
        CAPTURE(j.dump());
        Position p;
        FrameState s;
        ShotInput in;
        ShotFacts f;
        Decision d;
        MatchConfig c;
        MatchState m;
        error.clear();
        CHECK_FALSE(position_from_json(j, p, error));
        CHECK(error == "position: want an object");
        CHECK_FALSE(frame_from_json(j, s, error));
        CHECK(error == "frame: want an object");
        CHECK_FALSE(input_from_json(j, in, error));
        CHECK(error == "input: want an object");
        CHECK_FALSE(facts_from_json(j, f, error));
        CHECK(error == "facts: want an object");
        CHECK_FALSE(decision_from_json(j, d, error));
        CHECK(error == "decision: want an object");
        CHECK_FALSE(config_from_json(j, c, error));
        CHECK(error == "config: want an object");
        CHECK_FALSE(match_from_json(j, m, error));
        CHECK(error == "match: want an object");
    }
}

TEST_CASE("an unknown name is refused") {
    check_refused<FrameState>(example_frame(), &frame_from_json,
                              {{"/balls/1/ball", "purple", "balls[1].ball: unknown name \"purple\""},
                               {"/balls/1/ball", "", "balls[1].ball: unknown name"},
                               {"/on/kind", "blue", "on.kind: unknown name \"blue\""},
                               {"/on", {{"kind", "colour"}, {"colour", "mauve"}}, "on.colour: unknown name \"mauve\""},
                               {"/on", {{"kind", "colour"}, {"colour", "red"}}, "on.colour: \"red\" is not a colour"},
                               {"/on", {{"kind", "colour"}, {"colour", "cue"}}, "on.colour: \"cue\" is not a colour"},
                               {"/on", {{"kind", "colour"}}, "on.colour: missing"},
                               {"/pending", "soon", "pending: unknown name \"soon\""},
                               {"/end_reason", "boredom", "end_reason: unknown name \"boredom\""},
                               {"/original/on/kind", "Red", "original.on.kind: unknown name \"Red\""},
                               {"/original/balls/8/ball", "white", "original.balls[8].ball: unknown name \"white\""}});
    check_refused<Position>(example_position(), &position_from_json,
                            {{"/balls/0/ball", "white", "balls[0].ball: unknown name \"white\""},
                             {"/on/kind", "colours", "on.kind: unknown name \"colours\""},
                             {"/on/colour", "orange", "on.colour: unknown name \"orange\""}});
    check_refused<ShotInput>(json{{"nominated", "blue"}}, &input_from_json,
                             {{"/nominated", "purple", "nominated: unknown name \"purple\""}});
    check_refused<Decision>(json{{"kind", "play"}, {"player", 0}}, &decision_from_json,
                            {{"/kind", "resign", "kind: unknown name \"resign\""}});
    check_refused<MatchState>(fixture("example_match.json"), &match_from_json,
                              {{"/history/0/reason", "walkover", "history[0].reason: unknown name \"walkover\""},
                               {"/frame/pending", "later", "frame.pending: unknown name \"later\""}});
}

TEST_CASE("a place or a player out of range is refused") {
    check_refused<FrameState>(
        example_frame(), &frame_from_json,
        {{"/cue_ball", 9, "cue_ball: place 9 is out of range (9 balls)"},
         {"/cue_ball", -1, "cue_ball: place -1 is out of range"},
         {"/touching", {9}, "touching[0]: place 9 is out of range (9 balls)"},
         {"/touching", {1, -1}, "touching[1]: place -1 is out of range"},
         {"/original/cue_ball", 40, "original.cue_ball: place 40 is out of range"},
         {"/original/touching", {2, 3, 42}, "original.touching[2]: place 42 is out of range"},
         {"/balls/1/index", 15, "balls[1].index: 15 is out of range for red"},
         {"/balls/1/index", -1, "balls[1].index: -1 is out of range for red"},
         {"/balls/8/index", 1, "balls[8].index: 1 is out of range for black"},
         {"/striker", 2, "striker: 2 is not a player (0 or 1)"},
         {"/striker", -1, "striker: -1 is not a player (0 or 1)"},
         {"/decider", 2, "decider: 2 is not a player (0 or 1)"},
         {"/offender", -1, "offender: -1 is not a player (0 or 1)"},
         {"/winner", 2, "winner: 2 is not -1, 0 or 1"},
         {"/winner", -2, "winner: -2 is not -1, 0 or 1"},
         {"/rerack_accepted", 2, "rerack_accepted: 2 is not -1, 0 or 1"},
         {"/strokes", 1099511627776, "strokes: too large for an integer"},
         {"/strokes", -1.0e300, "strokes: too large for an integer"},
         {"/strokes", 41.5, "strokes: want an integer"},
         {"/strokes", kNan, "strokes: want an integer"},
         {"/cue_ball", 0.5, "cue_ball: want an integer"},
         {"/scores", {24}, "scores: want two entries"},
         {"/scores", {24, 31, 0}, "scores: want two entries"},
         {"/concede_any_time", {true}, "concede_any_time: want two entries"},
         {"/stats", json::array({example_frame()["stats"][0]}), "stats: want two entries"}});

    json no_balls = example_frame();
    no_balls["balls"] = json::array();
    no_balls["cue_ball"] = 1;
    FrameState s;
    std::string error;
    CHECK_FALSE(frame_from_json(no_balls, s, error));
    CHECK(error == "cue_ball: place 1 is out of range (0 balls)");

    check_refused<Position>(example_position(), &position_from_json,
                            {{"/cue_ball", 9, "cue_ball: place 9 is out of range (9 balls)"},
                             {"/touching", {6, 9}, "touching[1]: place 9 is out of range (9 balls)"},
                             {"/balls/2/index", 15, "balls[2].index: 15 is out of range for red"}});
    check_refused<ShotFacts>(example_facts(), &facts_from_json,
                             {{"/first_hit", {8}, "first_hit[0]: place 8 is out of range (8 balls)"},
                              {"/first_hit", {0, -1}, "first_hit[1]: place -1 is out of range"},
                              {"/pots/1/ball", 8, "pots[1].ball: place 8 is out of range"},
                              {"/pots/0/ball", -1, "pots[0].ball: place -1 is out of range"},
                              {"/touching_moved", {99}, "touching_moved[0]: place 99 is out of range"},
                              {"/closest_gap_m", {0.0, 0.1}, "closest_gap_m: 2 entries for 8 balls"},
                              {"/rest/3", {0.5}, "rest[3]: want [x, y]"},
                              {"/rest/3", {0.5, 0.5, 0.5}, "rest[3]: want [x, y]"}});
    check_refused<Decision>(json{{"kind", "play"}, {"player", 0}}, &decision_from_json,
                            {{"/player", 2, "player: 2 is not a player (0 or 1)"},
                             {"/player", -1, "player: -1 is not a player (0 or 1)"}});
    check_refused<MatchState>(fixture("example_match.json"), &match_from_json,
                              {{"/breaker", 2, "breaker: 2 is not a player (0 or 1)"},
                               {"/winner", 2, "winner: 2 is not -1, 0 or 1"},
                               {"/frames", {1}, "frames: want two entries"},
                               {"/history/0/winner", 5, "history[0].winner: 5 is not -1, 0 or 1"},
                               {"/history/0/breaker", -1, "history[0].breaker: -1 is not a player (0 or 1)"},
                               {"/frame/cue_ball", 9, "frame.cue_ball: place 9 is out of range (9 balls)"}});
}

TEST_CASE("a number that is not finite is refused") {
    check_refused<FrameState>(example_frame(), &frame_from_json,
                              {{"/balls/1/x", kNan, "balls[1].x: want a finite number"},
                               {"/balls/1/y", kInf, "balls[1].y: want a finite number"},
                               {"/balls/0/x", "nan", "balls[0].x: want a finite number"},
                               {"/balls/0/y", "-inf", "balls[0].y: want a finite number"},
                               {"/original/balls/4/y", -kInf, "original.balls[4].y: want a finite number"}});
    check_refused<Position>(example_position(), &position_from_json,
                            {{"/balls/8/x", kNan, "balls[8].x: want a finite number"},
                             {"/balls/8/y", -kInf, "balls[8].y: want a finite number"}});
    check_refused<MatchState>(fixture("example_match.json"), &match_from_json,
                              {{"/frame/balls/0/x", kNan, "frame.balls[0].x: want a finite number"},
                               {"/frame/original/balls/3/y", kInf, "frame.original.balls[3].y: want a finite"}});
    check_refused<ShotFacts>(example_facts(), &facts_from_json,
                             {{"/first_hit_t", kInf, "first_hit_t: want a finite number"},
                              {"/pots/0/t", kNan, "pots[0].t: want a finite number"},
                              {"/rest/0/1", kNan, "rest[0][1]: want a finite number"},
                              {"/rest/7/0", "inf", "rest[7][0]: want a finite number"},
                              {"/closest_gap_m/2", kInf, "closest_gap_m[2]: want a finite number"},
                              {"/cue_travel_m", kNan, "cue_travel_m: want a finite number"},
                              {"/object_travel_m", -kInf, "object_travel_m: want a finite number"}});
}

TEST_CASE("what the writers make of a number that is not finite is refused on the way back") {
    FrameState s;
    std::string error;
    REQUIRE(frame_from_json(example_frame(), s, error));
    s.balls[3].pos.y = kNan;
    const json j = frame_json(s);
    CHECK(j["balls"][3]["y"] == "nan");
    CHECK_FALSE(frame_from_json(json::parse(j.dump()), s, error));
    CHECK(error == "balls[3].y: want a finite number");

    ShotFacts f;
    REQUIRE(facts_from_json(example_facts(), f, error));
    f.closest_gap_m[5] = kInf;
    CHECK_FALSE(facts_from_json(json::parse(facts_json(f).dump()), f, error));
    CHECK(error == "closest_gap_m[5]: want a finite number");
}

TEST_CASE("a seed must be a decimal string that fits") {
    check_refused<FrameState>(example_frame(), &frame_from_json,
                              {{"/seed", 147, "seed: want a decimal string"},
                               {"/seed", "", "seed: \"\" is not a decimal number"},
                               {"/seed", "12a", "seed: \"12a\" is not a decimal number"},
                               {"/seed", "-1", "seed: \"-1\" is not a decimal number"},
                               {"/seed", " 1", "seed: \" 1\" is not a decimal number"},
                               {"/seed", "1.0", "seed: \"1.0\" is not a decimal number"},
                               {"/seed", "0x10", "seed: \"0x10\" is not a decimal number"},
                               {"/seed", "18446744073709551616", "seed: \"18446744073709551616\" is not a decimal"},
                               {"/seed", "99999999999999999999999", "seed: \"99999999999999999999999\" is not"}});
    check_refused<MatchConfig>(fixture("example_match.json")["config"], &config_from_json,
                               {{"/seed", 147, "seed: want a decimal string"},
                                {"/seed", "18446744073709551616", "seed: \"18446744073709551616\" is not"}});
    check_refused<MatchState>(fixture("example_match.json"), &match_from_json,
                              {{"/config/seed", "seven", "config.seed: \"seven\" is not a decimal number"},
                               {"/frame/seed", 7, "frame.seed: want a decimal string"}});

    json frame = example_frame();
    FrameState s;
    std::string error;
    frame["seed"] = "18446744073709551615";
    REQUIRE_MESSAGE(frame_from_json(frame, s, error), error);
    CHECK(s.seed == std::numeric_limits<std::uint64_t>::max());
    frame["seed"] = "00147";
    REQUIRE_MESSAGE(frame_from_json(frame, s, error), error);
    CHECK(s.seed == 147);
}

TEST_CASE("a whole float is read as the integer it is, as a state that went through Godot's JSON has them") {
    json frame = example_frame();
    frame["cue_ball"] = 0.0;
    frame["striker"] = 1.0;
    frame["scores"] = {24.0, 31.0};
    frame["strokes"] = 41.0;
    frame["winner"] = -1.0;
    frame["balls"][1]["index"] = 3.0;
    frame["touching"] = {2.0};
    FrameState s;
    std::string error;
    REQUIRE_MESSAGE(frame_from_json(json::parse(frame.dump()), s, error), error);
    CHECK(s.striker == 1);
    CHECK(s.strokes == 41);
    CHECK(s.winner == -1);
    CHECK(s.touching == std::vector<int>{2});
    json expected = example_frame();
    expected["striker"] = 1;
    expected["touching"] = {2};
    CHECK(frame_json(s) == expected);
    CHECK(frame_json(s)["strokes"].is_number_integer());

    MatchConfig c;
    REQUIRE_MESSAGE(config_from_json(json::parse(R"({"best_of": 3.0, "first_breaker": -1.0, "seed": "147",
                                                     "concede_any_time": [true, false]})"),
                                     c, error),
                    error);
    CHECK(c.best_of == 3);
    CHECK(c.first_breaker == -1);
}

TEST_CASE("keys the readers do not know are ignored, and an integral number is a fine double") {
    json frame = example_frame();
    frame["view"] = "anything";
    frame["added_later"] = {1, 2, 3};
    frame["balls"][0]["colour"] = "white";
    frame["balls"][6]["x"] = 0;
    frame["balls"][1]["x"] = 1;
    FrameState s;
    std::string error;
    REQUIRE_MESSAGE(frame_from_json(frame, s, error), error);
    CHECK(s.balls[1].pos.x == 1.0);
    json expected = example_frame();
    expected["balls"][1]["x"] = 1.0;
    CHECK(frame_json(s) == expected);
}

TEST_CASE("scores, counts and points outside 0 to 1,000,000 are refused, so no sum of the engine's can overflow") {
    struct Case {
        const char* pointer;
        const char* path;
        int lowest;
    };
    const Case cases[] = {
        {"/scores/0", "scores[0]", 0},
        {"/scores/1", "scores[1]", 0},
        {"/break_points", "break_points", 0},
        {"/strokes", "strokes", 0},
        {"/last_penalty", "last_penalty", 0},
        {"/miss_run", "miss_run", 0},
        {"/quiet_strokes", "quiet_strokes", 0},
        {"/rerack_deadline", "rerack_deadline", -1},
        {"/stats/0/shots", "stats[0].shots", 0},
        {"/stats/1/pots", "stats[1].pots", 0},
        {"/stats/0/pot_attempts", "stats[0].pot_attempts", 0},
        {"/stats/1/fouls", "stats[1].fouls", 0},
        {"/stats/0/misses", "stats[0].misses", 0},
        {"/stats/1/points_from_fouls", "stats[1].points_from_fouls", 0},
        {"/stats/0/highest_break", "stats[0].highest_break", 0},
    };
    for (const Case& c : cases) {
        CAPTURE(c.path);
        const json::json_pointer at(c.pointer);
        for (int bad : {c.lowest - 1, 1000001, std::numeric_limits<int>::max(), std::numeric_limits<int>::min()}) {
            CAPTURE(bad);
            json doc = example_frame();
            doc[at] = bad;
            FrameState s;
            std::string error;
            CHECK_FALSE(frame_from_json(doc, s, error));
            CHECK(error.rfind(std::string(c.path) + ": " + std::to_string(bad) + " is outside [", 0) == 0);
        }
        for (int good : {c.lowest, 1000000}) {
            json doc = example_frame();
            doc[at] = good;
            FrameState s;
            std::string error;
            CHECK_MESSAGE(frame_from_json(doc, s, error), error);
        }
    }
}

TEST_CASE("a match's frame counts and its history's scores are bounded the same way") {
    const json good = fixture("example_match.json");
    for (const char* pointer : {"/frames/0", "/frame_number", "/reracks", "/frame/scores/1"}) {
        CAPTURE(pointer);
        json doc = good;
        doc[json::json_pointer(pointer)] = -1;
        MatchState m;
        std::string error;
        CHECK_FALSE(match_from_json(doc, m, error));
        CHECK(error.find("is outside [0, 1000000]") != std::string::npos);
    }
    REQUIRE_FALSE(good["history"].empty());
    json doc = good;
    doc["history"][0]["scores"][0] = 2000000;
    MatchState m;
    std::string error;
    CHECK_FALSE(match_from_json(doc, m, error));
    CHECK(error.rfind("history[0].scores[0]: 2000000 is outside", 0) == 0);
}
