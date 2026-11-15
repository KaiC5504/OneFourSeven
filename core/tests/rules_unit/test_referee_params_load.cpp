#include <doctest/doctest.h>

#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/log_schema.hpp"
#include "ofs/rules/referee_params.hpp"

using namespace ofs::rules;

namespace {

nlohmann::json read_params_file() {
    std::ifstream in(std::string(OFS_CORE_DATA_DIR) + "/referee_params.json");
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

struct Capture : ofs::log::Sink {
    std::vector<ofs::log::Event> events;
    void write(const ofs::log::Event& event, const std::string&) override { events.push_back(event); }
};

// Loads `file` with a capture sink attached; returns the params.invalid events.
std::vector<ofs::log::Event> load_capturing(const nlohmann::json& file, RefParams& out,
                                            std::vector<std::string>& problems, bool& ok) {
    auto capture = std::make_shared<Capture>();
    ofs::log::Logger::instance().clear_sinks();
    ofs::log::Logger::instance().add_sink(capture);
    ok = load_ref_params(file, out, problems);
    ofs::log::Logger::instance().clear_sinks();
    return capture->events;
}

// A RefParams with every field moved off its default, so a field the loader or the writer skips
// shows up as a default left behind.
RefParams all_changed() {
    RefParams p;
    p.touching_gap_m = 2.5e-5;
    p.simultaneous_hit_s = 3.0e-7;
    p.push_contact_time_s = 1.75e-3;
    p.push_fine_edge_rad = 1.25;
    p.miss_lenient_gap_m = 0.0123;
    p.escape_max_cushions = 1;
    p.pot_attempt_radius_m = 0.15;
    p.stalemate_quiet_travel_m = 0.456;
    p.stalemate_offer_strokes = 9;
    p.stalemate_grace_strokes = 4;
    p.d_sample_step_m = 0.0075;
    p.d_fine_step_m = 0.0015;
    return p;
}

}  // namespace

TEST_CASE("referee_params.json loads with no problems and documents every value") {
    const nlohmann::json file = read_params_file();
    RefParams p;
    std::vector<std::string> problems;
    CHECK(load_ref_params(file, p, problems));
    CHECK(problems.empty());
    for (const std::string& name : ref_param_names()) {
        CAPTURE(name);
        CHECK(file["values"].contains(name));
        REQUIRE(file["docs"].contains(name));
        CHECK(file["docs"][name].contains("unit"));
        CHECK(file["docs"][name].contains("note"));
        CHECK_FALSE(file["docs"][name].value("source", std::string()).empty());
    }
    CHECK(file["values"].size() == ref_param_names().size());
    CHECK(file["docs"].size() == ref_param_names().size());
}

TEST_CASE("a field missing from the file's values or docs is caught") {
    // What the test above rests on: the name list covers the struct, and a file short of a name
    // fails. RefParams is nine doubles and three ints; a field added to it changes its size, and
    // must then be added to referee_params.cpp's list, the file's values and docs, and here.
    CHECK(sizeof(RefParams) == 88);
    CHECK(ref_param_names().size() == 12);
    CHECK(ref_params_values(RefParams{}).size() == ref_param_names().size());
    for (const std::string& name : ref_param_names()) {
        CAPTURE(name);
        nlohmann::json file = read_params_file();
        file["values"].erase(name);
        RefParams p;
        std::vector<std::string> problems;
        bool ok = true;
        const auto events = load_capturing(file, p, problems, ok);
        CHECK_FALSE(ok);
        REQUIRE(problems.size() == 1);
        CHECK(problems[0] == name + ": missing");
        CHECK(events.size() == 1);
    }
}

TEST_CASE("the referee file's values are the struct's defaults") {
    // The defaults are what a build falls back to field by field; they must not drift from the
    // committed file.
    const nlohmann::json values = read_params_file()["values"];
    CHECK(ref_params_values(RefParams{}) == values);
}

TEST_CASE("the owner's ruling stands: every failed escape is a miss") {
    RefParams p;
    std::vector<std::string> problems;
    REQUIRE(load_ref_params(read_params_file(), p, problems));
    CHECK(p.miss_lenient_gap_m == 0.0);
}

TEST_CASE("every referee value round-trips through its logged form") {
    const RefParams p = all_changed();
    const nlohmann::json values = ref_params_values(p);
    const nlohmann::json defaults = ref_params_values(RefParams{});
    for (const std::string& name : ref_param_names()) {
        CAPTURE(name);
        REQUIRE(values.contains(name));
        CHECK(values[name] != defaults[name]);
    }
    CHECK(values["escape_max_cushions"].is_number_integer());
    CHECK(values["stalemate_offer_strokes"].is_number_integer());
    CHECK(values["stalemate_grace_strokes"].is_number_integer());

    RefParams back;
    std::vector<std::string> problems;
    REQUIRE(ref_params_from_values(values, back, problems));
    CHECK(problems.empty());
    CHECK(ref_params_values(back) == values);
    CHECK(back.push_fine_edge_rad == p.push_fine_edge_rad);
    CHECK(back.stalemate_offer_strokes == 9);

    // The file form loads the same values.
    RefParams from_file;
    REQUIRE(load_ref_params(nlohmann::json{{"values", values}}, from_file, problems));
    CHECK(ref_params_values(from_file) == values);
}

TEST_CASE("bad referee values are refused, logged and leave the default") {
    nlohmann::json file = read_params_file();
    file["values"]["touching_gap_m"] = -1e-5;
    file["values"]["push_fine_edge_rad"] = "fine";
    file["values"]["stalemate_offer_strokes"] = 12.5;
    file["values"].erase("pot_attempt_radius_m");
    file["values"]["surprise"] = 1;
    RefParams p;
    std::vector<std::string> problems;
    bool ok = true;
    const auto events = load_capturing(file, p, problems, ok);
    CHECK_FALSE(ok);
    CHECK(problems.size() == 5);
    CHECK(events.size() == 5);
    const nlohmann::json schema = ofs::log::load_schema(std::string(OFS_CORE_DATA_DIR) + "/log_schema.json");
    for (const auto& e : events) {
        CHECK(e.event == "params.invalid");
        CHECK(e.subsystem == "params");
        CHECK(e.level == ofs::log::Level::error);
        const auto problem = ofs::log::check(e, schema);
        CHECK_MESSAGE(!problem.has_value(), problem.value_or(""));
    }
    const RefParams defaults;
    CHECK(p.touching_gap_m == defaults.touching_gap_m);
    CHECK(p.push_fine_edge_rad == defaults.push_fine_edge_rad);
    CHECK(p.stalemate_offer_strokes == defaults.stalemate_offer_strokes);
    CHECK(p.pot_attempt_radius_m == defaults.pot_attempt_radius_m);
    // The fields beside them still load.
    CHECK(p.d_sample_step_m == file["values"]["d_sample_step_m"].get<double>());
}

TEST_CASE("the strict form refuses the same things without logging") {
    auto capture = std::make_shared<Capture>();
    ofs::log::Logger::instance().clear_sinks();
    ofs::log::Logger::instance().add_sink(capture);

    RefParams p;
    std::vector<std::string> problems;
    nlohmann::json values = ref_params_values(RefParams{});
    values["simultaneous_hit_s"] = std::numeric_limits<double>::infinity();
    values["escape_max_cushions"] = 1.5;
    values["stalemate_grace_strokes"] = 3.0;  // integral, but not an integer in the file
    values.erase("miss_lenient_gap_m");
    values["extra"] = true;
    CHECK_FALSE(ref_params_from_values(values, p, problems));
    CHECK(problems.size() == 5);

    problems.clear();
    CHECK_FALSE(ref_params_from_values(nlohmann::json::array({1, 2}), p, problems));
    CHECK(problems.size() == 1);
    problems.clear();
    CHECK_FALSE(ref_params_from_values(nullptr, p, problems));
    CHECK(problems.size() == 1);

    ofs::log::Logger::instance().clear_sinks();
    CHECK(capture->events.empty());
}

TEST_CASE("values outside each referee field's range are refused and the bounds accepted") {
    // referee_params.cpp gives each bound's reason.
    struct Case {
        const char* key;
        double refused;
        double accepted;
    };
    const Case cases[] = {
        {"touching_gap_m", 0.0, 1e-9},
        {"touching_gap_m", 2e-3, 1e-3},
        {"simultaneous_hit_s", -1e-9, 0.0},
        {"simultaneous_hit_s", 2e-3, 1e-3},
        {"push_contact_time_s", -1e-4, 0.0},
        {"push_contact_time_s", 0.02, 0.01},
        {"push_fine_edge_rad", -0.1, 0.0},
        {"push_fine_edge_rad", 1.6, 1.5707963267948966},
        {"miss_lenient_gap_m", -0.001, 0.0},
        {"miss_lenient_gap_m", 0.2, 0.105},
        {"escape_max_cushions", -1, 0},
        {"escape_max_cushions", 4, 3},
        {"pot_attempt_radius_m", -0.1, 0.0},
        {"pot_attempt_radius_m", 1.5, 1.0},
        {"stalemate_quiet_travel_m", -0.1, 0.0},
        {"stalemate_quiet_travel_m", 11.0, 10.0},
        {"stalemate_offer_strokes", 1, 2},
        {"stalemate_offer_strokes", 1001, 1000},
        {"stalemate_grace_strokes", 0, 1},
        {"stalemate_grace_strokes", 101, 100},
        {"d_sample_step_m", 0.001, 0.002},
        {"d_sample_step_m", 0.06, 0.05},
        {"d_fine_step_m", 0.0005, 0.001},
        {"d_fine_step_m", 0.011, 0.01},
    };
    for (const Case& c : cases) {
        CAPTURE(c.key);
        CAPTURE(c.refused);
        nlohmann::json values = ref_params_values(RefParams{});
        const bool integer = values[c.key].is_number_integer();
        const auto put = [&](double v) {
            values[c.key] = integer ? nlohmann::json(static_cast<int>(v)) : nlohmann::json(v);
        };
        RefParams p;
        std::vector<std::string> problems;
        put(c.refused);
        CHECK_FALSE(ref_params_from_values(values, p, problems));
        REQUIRE(problems.size() == 1);
        CHECK(problems[0].rfind(std::string(c.key) + ": outside", 0) == 0);
        problems.clear();
        put(c.accepted);
        CHECK(ref_params_from_values(values, p, problems));
        CHECK(problems.empty());
    }
}

TEST_CASE("a referee file without values is refused") {
    RefParams p;
    std::vector<std::string> problems;
    bool ok = true;
    auto events = load_capturing(nlohmann::json{{"schema_version", 1}}, p, problems, ok);
    CHECK_FALSE(ok);
    CHECK(problems.size() == 1);
    CHECK(events.size() == 1);

    problems.clear();
    events = load_capturing(nlohmann::json{{"values", 3}}, p, problems, ok);
    CHECK_FALSE(ok);
    CHECK(problems.size() == 1);
    CHECK(events.size() == 1);

    problems.clear();
    events = load_capturing(nlohmann::json::array(), p, problems, ok);
    CHECK_FALSE(ok);
    CHECK(events.size() == 1);
}
