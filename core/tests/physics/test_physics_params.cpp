#include <doctest/doctest.h>

#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/log_schema.hpp"
#include "ofs/physics/params.hpp"

using namespace ofs::phys;

namespace {

nlohmann::json read_params_file() {
    std::ifstream in(std::string(OFS_CORE_DATA_DIR) + "/table_params.json");
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

struct Capture : ofs::log::Sink {
    std::vector<ofs::log::Event> events;
    void write(const ofs::log::Event& event, const std::string&) override { events.push_back(event); }
};

// Loads `file` with a capture sink attached; returns the params.invalid events.
std::vector<ofs::log::Event> load_capturing(const nlohmann::json& file, PhysicsParams& out,
                                            std::vector<std::string>& problems, bool& ok) {
    auto capture = std::make_shared<Capture>();
    ofs::log::Logger::instance().clear_sinks();
    ofs::log::Logger::instance().add_sink(capture);
    ok = load_physics_params(file, out, problems);
    ofs::log::Logger::instance().clear_sinks();
    return capture->events;
}

}  // namespace

TEST_CASE("table_params.json loads with no problems and documents every value") {
    const nlohmann::json file = read_params_file();
    PhysicsParams p;
    std::vector<std::string> problems;
    CHECK(load_physics_params(file, p, problems));
    CHECK(problems.empty());
    for (const std::string& name : physics_param_names()) {
        CAPTURE(name);
        REQUIRE(file["docs"].contains(name));
        CHECK(file["docs"][name].contains("unit"));
        CHECK(file["docs"][name].contains("note"));
        CHECK_FALSE(file["docs"][name].value("source", std::string()).empty());
    }
    CHECK(file["docs"].size() == physics_param_names().size());
}

TEST_CASE("the file's values are the struct's defaults") {
    // The defaults are what a release build falls back to field by field; they must not drift
    // from the committed file.
    const nlohmann::json values = read_params_file()["values"];
    CHECK(physics_params_values(PhysicsParams{}) == values);
}

TEST_CASE("values round-trip through the bundle form") {
    PhysicsParams p;
    p.roll_friction = 0.0061234567890123;
    p.max_events = 777;
    PhysicsParams back;
    std::vector<std::string> problems;
    REQUIRE(physics_params_from_values(physics_params_values(p), back, problems));
    CHECK(physics_params_values(back) == physics_params_values(p));
}

TEST_CASE("bad values are refused, logged and leave the default") {
    nlohmann::json file = read_params_file();
    file["values"]["slide_friction"] = -0.1;
    file["values"]["ball_restitution"] = "high";
    file["values"]["max_events"] = 12.5;
    file["values"].erase("roll_friction");
    file["values"]["surprise"] = 1;
    PhysicsParams p;
    std::vector<std::string> problems;
    bool ok = true;
    const auto events = load_capturing(file, p, problems, ok);
    CHECK_FALSE(ok);
    CHECK(problems.size() == 5);
    CHECK(events.size() == 5);
    const nlohmann::json schema = ofs::log::load_schema(std::string(OFS_CORE_DATA_DIR) + "/log_schema.json");
    for (const auto& e : events) {
        CHECK(e.event == "params.invalid");
        CHECK(e.level == ofs::log::Level::error);
        const auto problem = ofs::log::check(e, schema);
        CHECK_MESSAGE(!problem.has_value(), problem.value_or(""));
    }
    const PhysicsParams defaults;
    CHECK(p.slide_friction == defaults.slide_friction);
    CHECK(p.ball_restitution == defaults.ball_restitution);
    CHECK(p.max_events == defaults.max_events);
    CHECK(p.roll_friction == defaults.roll_friction);
}

TEST_CASE("non-finite numbers and inverted rack gaps are refused") {
    PhysicsParams p;
    std::vector<std::string> problems;
    nlohmann::json values = physics_params_values(PhysicsParams{});
    values["gravity_m_s2"] = std::numeric_limits<double>::infinity();
    values["rack_gap_min_m"] = 5e-4;
    values["rack_gap_max_m"] = 1e-4;
    CHECK_FALSE(physics_params_from_values(values, p, problems));
    CHECK(problems.size() == 2);
}

TEST_CASE("a file without values is refused") {
    PhysicsParams p;
    std::vector<std::string> problems;
    bool ok = true;
    const auto events = load_capturing(nlohmann::json{{"schema_version", 2}}, p, problems, ok);
    CHECK_FALSE(ok);
    CHECK(events.size() == 1);
}
