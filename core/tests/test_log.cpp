#include <doctest/doctest.h>

#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "ofs/log.hpp"

using namespace ofs::log;
namespace fs = std::filesystem;

namespace {

Event sample_event() {
    Event e;
    e.ts = 1793633412.123;
    e.session = "20261102-153012-a1b2c3";
    e.build = "dev";
    e.platform = "windows";
    e.subsystem = "devlink";
    e.level = Level::warn;
    e.event = "devlink.lost";
    e.data = {{"reason", "timeout"}, {"queued_lines", 41}};
    return e;
}

struct CaptureSink : Sink {
    std::vector<std::string> lines;
    void write(const Event&, const std::string& line) override { lines.push_back(line); }
};

struct ThrowingSink : Sink {
    void write(const Event& event, const std::string&) override {
        if (event.event != "app.error") throw std::runtime_error("disk on fire");
    }
};

// The Logger is process-wide; every test that uses it starts and ends with no sinks.
struct LoggerScope {
    LoggerScope() {
        Logger::instance().clear_sinks();
        Logger::instance().configure({"20261102-153012-a1b2c3", "dev", "windows"});
    }
    ~LoggerScope() { Logger::instance().clear_sinks(); }
};

struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("ofs_test_log_" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::vector<std::string> read_lines(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
}

void write_file(const fs::path& path, std::size_t bytes) {
    std::ofstream out(path, std::ios::binary);
    out << std::string(bytes, 'x');
}

}  // namespace

TEST_CASE("level names round trip") {
    for (Level level : {Level::debug, Level::info, Level::warn, Level::error}) {
        Level parsed = Level::debug;
        CHECK(parse_level(level_name(level), parsed));
        CHECK(parsed == level);
    }
    Level unchanged = Level::warn;
    CHECK_FALSE(parse_level("fatal", unchanged));
    CHECK_FALSE(parse_level("INFO", unchanged));
    CHECK(unchanged == Level::warn);
}

TEST_CASE("to_jsonl writes the fixed key order") {
    const std::string line = to_jsonl(sample_event());
    CHECK(line ==
          R"({"ts":1793633412.123,"session":"20261102-153012-a1b2c3","build":"dev","platform":"windows",)"
          R"("subsystem":"devlink","level":"warn","event":"devlink.lost","data":{"queued_lines":41,"reason":"timeout"}})");
    CHECK(line.find('\n') == std::string::npos);
}

TEST_CASE("to_jsonl and from_jsonl round trip") {
    const Event e = sample_event();
    Event back;
    REQUIRE(from_jsonl(to_jsonl(e), back));
    CHECK(back.ts == e.ts);
    CHECK(back.session == e.session);
    CHECK(back.build == e.build);
    CHECK(back.platform == e.platform);
    CHECK(back.subsystem == e.subsystem);
    CHECK(back.level == e.level);
    CHECK(back.event == e.event);
    CHECK(back.data == e.data);
    CHECK(to_jsonl(back) == to_jsonl(e));
}

TEST_CASE("to_jsonl writes non-finite numbers as null and replaces invalid UTF-8") {
    Event e = sample_event();
    e.data = {{"nan", std::nan("")}, {"inf", std::numeric_limits<double>::infinity()},
              {"bad", std::string("a\xff" "b")}};
    const std::string line = to_jsonl(e);
    CHECK(line.find(R"("inf":null)") != std::string::npos);
    CHECK(line.find(R"("nan":null)") != std::string::npos);
    CHECK(line.find("\"bad\":\"a\xef\xbf\xbd" "b\"") != std::string::npos);
    Event back;
    CHECK(from_jsonl(line, back));
}

TEST_CASE("from_jsonl rejects anything that isn't a complete log line") {
    const nlohmann::json good = nlohmann::json::parse(to_jsonl(sample_event()));
    Event out;
    REQUIRE(from_jsonl(good.dump(), out));

    CHECK_FALSE(from_jsonl("", out));
    CHECK_FALSE(from_jsonl("not json", out));
    CHECK_FALSE(from_jsonl("[1,2]", out));
    CHECK_FALSE(from_jsonl(good.dump() + "x", out));
    CHECK_FALSE(from_jsonl(R"({"ts":1e999,"session":"s","build":"b","platform":"p","subsystem":"app",)"
                           R"("level":"info","event":"app.lifecycle","data":{}})",
                           out));

    for (const char* key : {"ts", "session", "build", "platform", "subsystem", "level", "event", "data"}) {
        CAPTURE(key);
        auto missing = good;
        missing.erase(key);
        CHECK_FALSE(from_jsonl(missing.dump(), out));
    }

    auto extra = good;
    extra["extra"] = 1;
    CHECK_FALSE(from_jsonl(extra.dump(), out));

    const std::pair<const char*, nlohmann::json> wrong_types[] = {
        {"ts", "1793633412.123"}, {"ts", true},        {"ts", nullptr},  {"session", 5},
        {"build", nullptr},       {"platform", false}, {"subsystem", 1}, {"level", "fatal"},
        {"level", 1},             {"event", nlohmann::json::array()},    {"data", nlohmann::json::array()},
        {"data", nullptr},        {"data", "x"},
    };
    for (const auto& [key, value] : wrong_types) {
        CAPTURE(key);
        CAPTURE(value.dump());
        auto bad = good;
        bad[key] = value;
        CHECK_FALSE(from_jsonl(bad.dump(), out));
    }

    auto integer_ts = good;
    integer_ts["ts"] = 1793633412;
    CHECK(from_jsonl(integer_ts.dump(), out));
    CHECK(out.ts == 1793633412.0);
}

TEST_CASE("new_session_id is UTC time plus six hex digits") {
    const std::regex pattern("^20[0-9]{6}-[0-9]{6}-[0-9a-f]{6}$");
    const std::string a = new_session_id();
    const std::string b = new_session_id();
    CHECK(std::regex_match(a, pattern));
    CHECK(std::regex_match(b, pattern));
    CHECK(a != b);
}

TEST_CASE("Logger fills context and a timestamp") {
    LoggerScope scope;
    auto capture = std::make_shared<CaptureSink>();
    Logger::instance().add_sink(capture);
    const double before = static_cast<double>(std::time(nullptr));
    info("app", "app.lifecycle", {{"state", "foreground"}});
    REQUIRE(capture->lines.size() == 1);
    Event e;
    REQUIRE(from_jsonl(capture->lines[0], e));
    CHECK(e.session == "20261102-153012-a1b2c3");
    CHECK(e.build == "dev");
    CHECK(e.platform == "windows");
    CHECK(e.subsystem == "app");
    CHECK(e.level == Level::info);
    CHECK(e.event == "app.lifecycle");
    CHECK(e.data == nlohmann::json{{"state", "foreground"}});
    CHECK(e.ts >= before - 1.0);
    CHECK(e.ts <= before + 60.0);
}

TEST_CASE("Logger treats null data as no fields and rejects non-object data") {
    LoggerScope scope;
    auto capture = std::make_shared<CaptureSink>();
    Logger::instance().add_sink(capture);

    Logger::instance().emit("app", Level::info, "app.lifecycle", {});
    REQUIRE(capture->lines.size() == 1);
    Event e;
    REQUIRE(from_jsonl(capture->lines[0], e));
    CHECK(e.data == nlohmann::json::object());

    Logger::instance().emit("app", Level::info, "app.lifecycle", nlohmann::json::array({1, 2}));
    REQUIRE(capture->lines.size() == 2);
    REQUIRE(from_jsonl(capture->lines[1], e));
    CHECK(e.event == "app.error");
    CHECK(e.subsystem == "app");
    CHECK(e.level == Level::error);
    CHECK(e.data["kind"] == "log");
    CHECK(e.data["message"].get<std::string>().find("app.lifecycle") != std::string::npos);
}

TEST_CASE("a throwing sink turns into an app.error line and emit doesn't throw") {
    LoggerScope scope;
    auto capture = std::make_shared<CaptureSink>();
    Logger::instance().add_sink(std::make_shared<ThrowingSink>());
    Logger::instance().add_sink(capture);

    CHECK_NOTHROW(warn("devlink", "devlink.lost", {{"reason", "timeout"}, {"queued_lines", 3}}));
    REQUIRE(capture->lines.size() == 2);
    Event original, fallback;
    REQUIRE(from_jsonl(capture->lines[0], original));
    REQUIRE(from_jsonl(capture->lines[1], fallback));
    CHECK(original.event == "devlink.lost");
    CHECK(fallback.event == "app.error");
    CHECK(fallback.data["kind"] == "log");
    const std::string message = fallback.data["message"];
    CHECK(message.find("devlink.lost") != std::string::npos);
    CHECK(message.find("disk on fire") != std::string::npos);
}

TEST_CASE("emit_line rewrites valid lines canonically and refuses invalid ones") {
    LoggerScope scope;
    auto capture = std::make_shared<CaptureSink>();
    Logger::instance().add_sink(capture);

    const std::string messy =
        R"({ "data": {"state": "background"}, "event": "app.lifecycle", "level": "info", "subsystem": "app",)"
        R"( "platform": "ios", "build": "12", "session": "20261102-153012-a1b2c3", "ts": 1793633612.659 })";
    CHECK(Logger::instance().emit_line(messy));
    REQUIRE(capture->lines.size() == 1);
    CHECK(capture->lines[0] ==
          R"({"ts":1793633612.659,"session":"20261102-153012-a1b2c3","build":"12","platform":"ios",)"
          R"("subsystem":"app","level":"info","event":"app.lifecycle","data":{"state":"background"}})");

    CHECK_FALSE(Logger::instance().emit_line("{}"));
    CHECK_FALSE(Logger::instance().emit_line("garbage"));
    CHECK(capture->lines.size() == 1);
}

TEST_CASE("Logger from 8 threads writes every line whole") {
    LoggerScope scope;
    TempDir dir;
    auto sink = std::make_shared<FileSink>(dir.path.u8string(), "20261102-153012-a1b2c3", 64u << 20);
    Logger::instance().add_sink(sink);

    static constexpr int threads = 8;
    static constexpr int per_thread = 1000;
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([t] {
            for (int i = 0; i < per_thread; ++i) {
                info("harness", "harness.command",
                     {{"command", "stress"}, {"args", {t, i}}, {"ok", true},
                      {"result", std::string(static_cast<std::size_t>(i % 97), 'z')}});
            }
        });
    }
    for (auto& w : workers) w.join();
    Logger::instance().flush();

    const auto lines = read_lines(fs::u8path(sink->path()));
    REQUIRE(lines.size() == threads * per_thread);
    std::set<std::pair<int, int>> seen;
    for (const auto& line : lines) {
        Event e;
        REQUIRE(from_jsonl(line, e));
        seen.insert({e.data["args"][0].get<int>(), e.data["args"][1].get<int>()});
    }
    CHECK(seen.size() == static_cast<std::size_t>(threads * per_thread));
}

TEST_CASE("FileSink line is on disk before the sink closes") {
    TempDir dir;
    FileSink sink(dir.path.u8string(), "20261102-153012-a1b2c3", 1u << 20);
    CHECK(fs::u8path(sink.path()) == dir.path / "20261102-153012-a1b2c3.jsonl");
    const Event e = sample_event();
    const std::string line = to_jsonl(e);
    sink.write(e, line);
    const auto lines = read_lines(fs::u8path(sink.path()));
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == line);
}

TEST_CASE("FileSink appends to an existing session file") {
    TempDir dir;
    const Event e = sample_event();
    { FileSink(dir.path.u8string(), "s1", 1u << 20).write(e, "first"); }
    { FileSink(dir.path.u8string(), "s1", 1u << 20).write(e, "second"); }
    CHECK(read_lines(dir.path / "s1.jsonl") == std::vector<std::string>{"first", "second"});
}

TEST_CASE("FileSink prunes the oldest sessions down to the cap") {
    TempDir dir;
    write_file(dir.path / "20261030-090000-aaaaaa.jsonl", 400);
    write_file(dir.path / "20261031-090000-bbbbbb.jsonl", 400);
    write_file(dir.path / "20261101-090000-cccccc.jsonl", 400);
    write_file(dir.path / "20261102-153012-a1b2c3.jsonl", 300);
    write_file(dir.path / "notes.txt", 5000);

    FileSink sink(dir.path.u8string(), "20261102-153012-a1b2c3", 1000);
    CHECK_FALSE(fs::exists(dir.path / "20261030-090000-aaaaaa.jsonl"));
    CHECK_FALSE(fs::exists(dir.path / "20261031-090000-bbbbbb.jsonl"));
    CHECK(fs::exists(dir.path / "20261101-090000-cccccc.jsonl"));
    CHECK(fs::exists(dir.path / "20261102-153012-a1b2c3.jsonl"));
    CHECK(fs::exists(dir.path / "notes.txt"));
}

TEST_CASE("FileSink never prunes the current session, even alone over the cap") {
    TempDir dir;
    write_file(dir.path / "20261101-090000-cccccc.jsonl", 400);
    write_file(dir.path / "20261102-153012-a1b2c3.jsonl", 2000);
    FileSink sink(dir.path.u8string(), "20261102-153012-a1b2c3", 1000);
    CHECK_FALSE(fs::exists(dir.path / "20261101-090000-cccccc.jsonl"));
    CHECK(fs::file_size(dir.path / "20261102-153012-a1b2c3.jsonl") == 2000);
}

TEST_CASE("FileSink creates missing directories") {
    TempDir dir;
    const fs::path nested = dir.path / "logs" / "2026-11-02";
    FileSink sink(nested.u8string(), "s", 1u << 20);
    sink.write(sample_event(), "line");
    CHECK(read_lines(nested / "s.jsonl") == std::vector<std::string>{"line"});
}

TEST_CASE("FileSink that can't open its file does nothing but keeps its path") {
    TempDir dir;
    write_file(dir.path / "blocker", 1);
    const fs::path inside_a_file = dir.path / "blocker" / "logs";
    FileSink sink(inside_a_file.u8string(), "s", 1u << 20);
    CHECK(fs::u8path(sink.path()) == inside_a_file / "s.jsonl");
    CHECK_NOTHROW(sink.write(sample_event(), "line"));
    CHECK_NOTHROW(sink.flush());
    CHECK_FALSE(fs::exists(inside_a_file));
}

TEST_CASE("RingBuffer drops and counts the oldest lines over its cap") {
    RingBuffer ring(20);  // each line costs its length + 1
    const Event e = sample_event();
    ring.write(e, "aaaa");
    ring.write(e, "bbbb");
    ring.write(e, "cccc");
    ring.write(e, "dddd");
    CHECK(ring.size_lines() == 4);
    CHECK(ring.dropped() == 0);
    ring.write(e, "eeee");
    CHECK(ring.size_lines() == 4);
    CHECK(ring.dropped() == 1);
    CHECK(ring.take(10) == std::vector<std::string>{"bbbb", "cccc", "dddd", "eeee"});
    CHECK(ring.size_lines() == 0);

    ring.write(e, std::string(30, 'x'));
    CHECK(ring.size_lines() == 0);
    CHECK(ring.dropped() == 2);
}

TEST_CASE("RingBuffer take and restore keep order") {
    RingBuffer ring(1000);
    const Event e = sample_event();
    for (const char* line : {"1", "2", "3", "4", "5"}) ring.write(e, line);
    auto batch = ring.take(2);
    CHECK(batch == std::vector<std::string>{"1", "2"});
    ring.write(e, "6");
    ring.restore(batch);
    CHECK(ring.size_lines() == 6);
    CHECK(ring.take(100) == std::vector<std::string>{"1", "2", "3", "4", "5", "6"});
    CHECK(ring.take(5).empty());
    CHECK(ring.dropped() == 0);
}

TEST_CASE("RingBuffer restore over the cap drops from the oldest end") {
    RingBuffer ring(10);  // room for five one-character lines
    const Event e = sample_event();
    for (const char* line : {"1", "2", "3", "4", "5"}) ring.write(e, line);
    auto batch = ring.take(3);
    ring.write(e, "6");
    ring.write(e, "7");
    ring.write(e, "8");
    ring.restore(batch);
    CHECK(ring.dropped() == 3);
    CHECK(ring.take(100) == std::vector<std::string>{"4", "5", "6", "7", "8"});
}

TEST_CASE("RingBuffer is safe to take from while the Logger writes") {
    LoggerScope scope;
    auto ring = std::make_shared<RingBuffer>(1u << 20);
    Logger::instance().add_sink(ring);
    std::size_t taken = 0;
    std::thread writer([] {
        for (int i = 0; i < 2000; ++i) info("app", "app.lifecycle", {{"state", "foreground"}});
    });
    while (taken < 2000) {
        auto batch = ring->take(64);
        if (batch.size() > 4) {
            ring->restore({batch.back()});
            batch.pop_back();
        }
        taken += batch.size();
    }
    writer.join();
    CHECK(ring->size_lines() == 0);
    CHECK(ring->dropped() == 0);
}
