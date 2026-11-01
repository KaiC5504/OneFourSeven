#include <doctest/doctest.h>

#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/log_schema.hpp"

using namespace ofs::log;

namespace {

const nlohmann::json& schema() {
    static const nlohmann::json s = load_schema(OFS_CORE_DATA_DIR "/log_schema.json");
    return s;
}

std::vector<std::string> read_lines(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

Event fixture_event(const std::string& name) {
    for (const auto& line : read_lines(OFS_TEST_FIXTURES_DIR "/log/valid_events.jsonl")) {
        Event e;
        if (from_jsonl(line, e) && e.event == name) return e;
    }
    FAIL("no fixture line for " << name);
    return {};
}

bool mentions(const std::optional<std::string>& problem, const std::string& text) {
    return problem && problem->find(text) != std::string::npos;
}

}  // namespace

TEST_CASE("schema loads") {
    REQUIRE(schema().contains("events"));
    CHECK(schema()["events"].size() >= 15);
}

TEST_CASE("valid_events.jsonl has one canonical, valid line per schema event") {
    const auto lines = read_lines(OFS_TEST_FIXTURES_DIR "/log/valid_events.jsonl");
    std::set<std::string> seen;
    for (const auto& line : lines) {
        CAPTURE(line);
        Event e;
        REQUIRE(from_jsonl(line, e));
        CHECK(to_jsonl(e) == line);
        const auto problem = check(e, schema());
        CHECK_MESSAGE(!problem, problem.value_or(""));
        CHECK(seen.insert(e.event).second);
    }
    std::set<std::string> expected;
    for (const auto& [name, entry] : schema()["events"].items()) expected.insert(name);
    CHECK(seen == expected);
}

TEST_CASE("check rejects an unknown event") {
    Event e = fixture_event("app.lifecycle");
    e.event = "app.teleport";
    const auto problem = check(e, schema());
    CHECK(mentions(problem, "unknown event 'app.teleport'"));
}

TEST_CASE("check rejects the wrong subsystem") {
    Event e = fixture_event("perf.hitch");
    e.subsystem = "app";
    const auto problem = check(e, schema());
    CHECK(mentions(problem, "event 'perf.hitch'"));
    CHECK(mentions(problem, "subsystem 'app' should be 'perf'"));
}

TEST_CASE("check rejects a level the event doesn't allow") {
    Event e = fixture_event("perf.hitch");
    e.level = Level::info;
    CHECK(mentions(check(e, schema()), "level 'info' not allowed"));
}

TEST_CASE("check rejects a missing required field") {
    Event e = fixture_event("devlink.upload_failed");
    e.data.erase("attempt");
    const auto problem = check(e, schema());
    CHECK(mentions(problem, "event 'devlink.upload_failed'"));
    CHECK(mentions(problem, "missing required field 'attempt'"));
}

TEST_CASE("check allows a missing optional field and unlisted keys") {
    Event e = fixture_event("app.error");
    e.data.erase("stack");
    e.data.erase("line");
    e.data["extra"] = "kept";
    CHECK_FALSE(check(e, schema()));
}

TEST_CASE("check rejects a wrong field type") {
    Event e = fixture_event("table.loaded");
    e.data["layout"] = 3;
    CHECK(mentions(check(e, schema()), "field 'layout' should be string, got integer"));

    e = fixture_event("perf.second");
    e.data["fps"] = "60";
    CHECK(mentions(check(e, schema()), "field 'fps' should be number, got string"));

    e = fixture_event("perf.second");
    e.data["idle"] = 1;
    CHECK(mentions(check(e, schema()), "field 'idle' should be boolean, got integer"));

    e = fixture_event("app.start");
    e.data["settings"] = nlohmann::json::array();
    CHECK(mentions(check(e, schema()), "field 'settings' should be object, got array"));

    e = fixture_event("app.start");
    e.data["screen_size"] = nlohmann::json::object();
    CHECK(mentions(check(e, schema()), "field 'screen_size' should be array, got object"));
}

TEST_CASE("integer fields reject booleans and floats; number fields accept integers") {
    Event e = fixture_event("devlink.connected");
    e.data["port"] = true;
    CHECK(mentions(check(e, schema()), "field 'port' should be integer, got boolean"));
    e.data["port"] = 47147.0;
    CHECK(mentions(check(e, schema()), "field 'port' should be integer, got number"));

    e = fixture_event("perf.second");
    e.data["fps"] = 60;
    CHECK_FALSE(check(e, schema()));
}

TEST_CASE("any accepts every type") {
    Event e = fixture_event("harness.command");
    for (const auto& value : {nlohmann::json(nullptr), nlohmann::json(1), nlohmann::json("x"),
                              nlohmann::json::array(), nlohmann::json(false)}) {
        e.data["result"] = value;
        CHECK_FALSE(check(e, schema()));
    }
}

TEST_CASE("check reports every problem in one message") {
    Event e = fixture_event("devlink.lost");
    e.data.erase("reason");
    e.data["queued_lines"] = "41";
    const auto problem = check(e, schema());
    CHECK(mentions(problem, "missing required field 'reason'"));
    CHECK(mentions(problem, "field 'queued_lines' should be integer, got string"));
}

TEST_CASE("an unloadable schema is reported, not thrown") {
    const auto missing = load_schema(OFS_TEST_FIXTURES_DIR "/log/does_not_exist.json");
    CHECK(missing == nlohmann::json::object());
    CHECK(check(fixture_event("app.lifecycle"), missing) == std::optional<std::string>("schema not loaded"));

    const auto not_json = load_schema(OFS_TEST_FIXTURES_DIR "/log/valid_events.jsonl");
    CHECK(not_json == nlohmann::json::object());
}
