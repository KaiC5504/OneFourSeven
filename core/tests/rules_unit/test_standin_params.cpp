#include <doctest/doctest.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/log_schema.hpp"
#include "ofs/opponent/standin.hpp"
#include "scenario.hpp"

using namespace ofs;
using namespace ofs::opponent;
using nlohmann::json;

namespace {

json read_params_file() { return rules::scenario::data_file("standin_params.json"); }

struct Capture : log::Sink {
    std::vector<log::Event> events;
    void write(const log::Event& event, const std::string&) override { events.push_back(event); }
};

std::vector<log::Event> load_capturing(const json& file, StandinParams& out, std::vector<std::string>& problems,
                                       bool& ok) {
    auto capture = std::make_shared<Capture>();
    log::Logger::instance().clear_sinks();
    log::Logger::instance().add_sink(capture);
    ok = load_standin_params(file, out, problems);
    log::Logger::instance().clear_sinks();
    return capture->events;
}

// Every field as the file would hold it.
json values_of(const StandinParams& p) {
    return {{"aim_error_rad", p.aim_error_rad},
            {"speed_error", p.speed_error},
            {"max_cut_rad", p.max_cut_rad},
            {"pot_speed_m_s", p.pot_speed_m_s},
            {"safety_speed_m_s", p.safety_speed_m_s},
            {"max_sims", p.max_sims},
            {"concede_required_points", p.concede_required_points}};
}

}  // namespace

TEST_CASE("standin_params.json loads with no problems and documents every value") {
    const json file = read_params_file();
    StandinParams p;
    std::vector<std::string> problems;
    CHECK(load_standin_params(file, p, problems));
    CHECK(problems.empty());
    for (const std::string& name : standin_param_names()) {
        CAPTURE(name);
        CHECK(file["values"].contains(name));
        REQUIRE(file["docs"].contains(name));
        CHECK(file["docs"][name].contains("unit"));
        CHECK_FALSE(file["docs"][name].value("note", std::string()).empty());
        CHECK_FALSE(file["docs"][name].value("source", std::string()).empty());
    }
    CHECK(file["values"].size() == standin_param_names().size());
    CHECK(file["docs"].size() == standin_param_names().size());
}

TEST_CASE("the stand-in's name list covers the struct and the file is its defaults") {
    // StandinParams is five doubles and two ints; a field added to it changes its size, and must
    // then be added to standin.cpp's list, the file's values and docs, and values_of here.
    CHECK(sizeof(StandinParams) == 48);
    CHECK(standin_param_names().size() == 7);
    CHECK(values_of(StandinParams{}).size() == standin_param_names().size());
    for (const std::string& name : standin_param_names()) CHECK(values_of(StandinParams{}).contains(name));
    // The defaults are what a build falls back to field by field; they must not drift from the file.
    CHECK(values_of(StandinParams{}) == read_params_file()["values"]);
}

TEST_CASE("every stand-in value is read from the file") {
    StandinParams changed;
    changed.aim_error_rad = 0.0125;
    changed.speed_error = 0.11;
    changed.max_cut_rad = 0.8;
    changed.pot_speed_m_s = 3.1;
    changed.safety_speed_m_s = 1.1;
    changed.max_sims = 5;
    changed.concede_required_points = 31;
    const json values = values_of(changed);
    const json defaults = values_of(StandinParams{});
    for (const std::string& name : standin_param_names()) {
        CAPTURE(name);
        CHECK(values[name] != defaults[name]);
    }
    StandinParams loaded;
    std::vector<std::string> problems;
    REQUIRE(load_standin_params(json{{"values", values}}, loaded, problems));
    CHECK(values_of(loaded) == values);
}

TEST_CASE("a value missing from the stand-in file is caught by name") {
    for (const std::string& name : standin_param_names()) {
        CAPTURE(name);
        json file = read_params_file();
        file["values"].erase(name);
        StandinParams p;
        std::vector<std::string> problems;
        bool ok = true;
        const auto events = load_capturing(file, p, problems, ok);
        CHECK_FALSE(ok);
        REQUIRE(problems.size() == 1);
        CHECK(problems[0] == name + ": missing");
        CHECK(events.size() == 1);
    }
}

TEST_CASE("bad stand-in values are refused, logged and leave the default") {
    json file = read_params_file();
    file["values"]["aim_error_rad"] = -0.001;
    file["values"]["pot_speed_m_s"] = "brisk";
    file["values"]["max_sims"] = 7.5;
    file["values"]["speed_error"] = std::numeric_limits<double>::infinity();
    file["values"].erase("max_cut_rad");
    file["values"]["position_play"] = true;
    StandinParams p;
    p.aim_error_rad = 0.0125;
    const StandinParams before = p;
    std::vector<std::string> problems{"an earlier problem"};
    bool ok = true;
    const auto events = load_capturing(file, p, problems, ok);
    CHECK_FALSE(ok);
    CHECK(problems.size() == 7);
    CHECK(problems[0] == "an earlier problem");
    CHECK(events.size() == 6);
    const json schema = log::load_schema(std::string(OFS_CORE_DATA_DIR) + "/log_schema.json");
    for (const auto& e : events) {
        CHECK(e.event == "params.invalid");
        CHECK(e.subsystem == "params");
        CHECK(e.level == log::Level::error);
        const auto problem = log::check(e, schema);
        CHECK_MESSAGE(!problem.has_value(), problem.value_or(""));
    }
    CHECK(p.aim_error_rad == before.aim_error_rad);
    CHECK(p.pot_speed_m_s == before.pot_speed_m_s);
    CHECK(p.max_sims == before.max_sims);
    CHECK(p.speed_error == before.speed_error);
    CHECK(p.max_cut_rad == before.max_cut_rad);
    // The fields beside them still load.
    CHECK(p.safety_speed_m_s == file["values"]["safety_speed_m_s"].get<double>());
    CHECK(p.concede_required_points == file["values"]["concede_required_points"].get<int>());
}

TEST_CASE("values outside each stand-in field's range are refused and the bounds accepted") {
    // standin.cpp gives each bound's reason.
    struct Case {
        const char* key;
        double refused;
        double accepted;
    };
    const Case cases[] = {
        {"aim_error_rad", -1e-6, 0.0},
        {"aim_error_rad", 0.11, 0.1},
        {"speed_error", -0.01, 0.0},
        {"speed_error", 0.51, 0.5},
        {"max_cut_rad", -0.1, 0.0},
        {"max_cut_rad", 1.58, 1.5},
        {"pot_speed_m_s", 0.1, 0.2},
        {"pot_speed_m_s", 10.5, 10.0},
        {"safety_speed_m_s", 0.0, 0.2},
        {"safety_speed_m_s", 11.0, 10.0},
        {"max_sims", -1, 0},
        {"max_sims", 65, 64},
        {"concede_required_points", 0, 1},
        {"concede_required_points", 148, 147},
    };
    for (const Case& c : cases) {
        CAPTURE(c.key);
        CAPTURE(c.refused);
        json values = values_of(StandinParams{});
        const bool integer = values[c.key].is_number_integer();
        const auto put = [&](double v) { values[c.key] = integer ? json(static_cast<int>(v)) : json(v); };
        StandinParams p;
        std::vector<std::string> problems;
        bool ok = true;
        put(c.refused);
        load_capturing(json{{"values", values}}, p, problems, ok);
        CHECK_FALSE(ok);
        REQUIRE(problems.size() == 1);
        CHECK(problems[0].rfind(std::string(c.key) + ": outside", 0) == 0);
        problems.clear();
        put(c.accepted);
        load_capturing(json{{"values", values}}, p, problems, ok);
        CHECK(ok);
        CHECK(problems.empty());
    }
}

TEST_CASE("a stand-in file without values is refused") {
    StandinParams p;
    std::vector<std::string> problems;
    bool ok = true;
    auto events = load_capturing(json{{"schema_version", 1}}, p, problems, ok);
    CHECK_FALSE(ok);
    CHECK(problems.size() == 1);
    CHECK(events.size() == 1);

    problems.clear();
    events = load_capturing(json{{"values", 3}}, p, problems, ok);
    CHECK_FALSE(ok);
    CHECK(problems.size() == 1);
    CHECK(events.size() == 1);

    problems.clear();
    events = load_capturing(json::array(), p, problems, ok);
    CHECK_FALSE(ok);
    CHECK(events.size() == 1);
}
