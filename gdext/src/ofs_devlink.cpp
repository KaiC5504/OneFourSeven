#include "ofs_devlink.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "bonjour_browser.h"
#include "ofs_log.h"

using namespace godot;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr const char *kServiceType = "_ofs-devlink._tcp";
constexpr const char *kLocalHost = "127.0.0.1";
constexpr int64_t kLocalPort = 47147;

constexpr double kUploadInterval = 1.0;
constexpr double kParamsInterval = 3.0;
constexpr double kHealthInterval = 5.0;
constexpr double kRequestTimeout = 5.0;
// devlink.py on this PC answers a connect at once; waiting longer only delays noticing
// it has stopped (Windows retries a refused localhost connect for about 2 s).
constexpr double kLocalConnectTimeout = 1.0;
constexpr double kBackoffCap = 30.0;
constexpr double kFailureLogInterval = 30.0;
constexpr double kSinkCheckInterval = 1.0;
constexpr double kBrowseRetry = 30.0;
// Long enough to read on screen before "searching" replaces it.
constexpr double kLostShownFor = 3.0;
constexpr int kFailuresBeforeLost = 3;
constexpr int64_t kBatchLines = 512;
constexpr uint64_t kBatchBytes = 1024 * 1024;
constexpr int64_t kBodyExcerptChars = 500;
// A large response is read over several frames rather than in one long one.
constexpr uint64_t kFrameBudgetUsec = 1000;

enum class State { Off, Searching, Connected, Lost };
enum class Kind { Health, Logs, Params };

String state_name(State state) {
    switch (state) {
        case State::Searching: return "searching";
        case State::Connected: return "connected";
        case State::Lost: return "lost";
        case State::Off: break;
    }
    return "off";
}

double now_seconds() { return double(Time::get_singleton()->get_ticks_usec()) / 1e6; }

String to_godot(const std::string &text) { return String::utf8(text.c_str(), int64_t(text.size())); }

std::string to_std(const String &text) {
    const CharString utf8 = text.utf8();
    return std::string(utf8.get_data(), size_t(utf8.length()));
}

json parse_json(const std::string &text) { return json::parse(text, nullptr, false); }

std::string dump_json(const json &value) { return value.dump(-1, ' ', false, json::error_handler_t::replace); }

// The server's own words: devlink.py answers errors as {"error": "..."}.
String excerpt(const std::string &body) {
    const json reply = parse_json(body);
    String text = (reply.is_object() && reply.contains("error") && reply["error"].is_string())
                      ? to_godot(reply["error"].get<std::string>())
                      : to_godot(body);
    return text.strip_edges().substr(0, kBodyExcerptChars);
}

void emit_log(const char *subsystem, const char *level, const char *event, const Dictionary &data) {
    OfsLog::emit(subsystem, level, event, data);
}

// First of each distinct error at once, then at most one per interval, so an unreachable
// PC can't fill the log (and the upload) with copies of one line.
struct Throttle {
    std::string last;
    double last_at = 0.0;
    bool any = false;

    bool allow(const std::string &key, double now, double interval) {
        if (any && key == last && now - last_at < interval) {
            return false;
        }
        note(key, now);
        return true;
    }

    void note(const std::string &key, double now) {
        last = key;
        last_at = now;
        any = true;
    }

    void reset() { any = false; }
};

// Stands in for NWBrowser in tests; OfsDevlink::push_test_bonjour queues its events.
class ScriptedBrowser : public BonjourBrowser {
public:
    struct Event {
        std::string kind;
        std::string name;
        std::string host;
        int port = 0;
        std::string error;
        bool denied = false;
    };

    bool supported() const override { return true; }

    void start(FoundCallback found, FailedCallback failed, ResolveFailedCallback resolve_failed) override {
        found_ = std::move(found);
        failed_ = std::move(failed);
        resolve_failed_ = std::move(resolve_failed);
        running_ = true;
        ++starts;
    }

    void stop() override { running_ = false; }

    void poll() override {
        if (!running_) {
            return;
        }
        std::vector<Event> events;
        events.swap(queued);
        for (const Event &event : events) {
            if (event.kind == "found") {
                found_(Found{event.name, event.host, event.port});
            } else if (event.kind == "browse_failed") {
                failed_(event.error, event.denied);
            } else if (event.kind == "resolve_failed") {
                resolve_failed_(event.name, event.error, event.denied);
            }
        }
    }

    std::vector<Event> queued;
    int64_t starts = 0;

private:
    bool running_ = false;
    FoundCallback found_;
    FailedCallback failed_;
    ResolveFailedCallback resolve_failed_;
};

struct Batch {
    std::string name;
    uint64_t from = 0;
    uint64_t to = 0;
    int64_t lines = 0;
    // Bytes after `to` were already in the file when it was read.
    bool more = false;
    std::string bytes;
};

struct LineCount {
    uint64_t from = 0;
    uint64_t to = 0;
    int64_t lines = 0;
};

}  // namespace

struct OfsDevlink::Impl {
    explicit Impl(OfsDevlink *owner) : owner(owner) {}

    OfsDevlink *owner;
    bool enabled = true;
    bool ready = false;
    State state = State::Off;
    String applied_hash;
    // Last hash handed to params_received, so a set Params rejected isn't offered every 3 s.
    String offered_hash;

    double time_scale = 1.0;
    bool test_target = false;
    String test_host;
    int64_t test_port = 0;
    bool test_bonjour = false;
    ScriptedBrowser *scripted = nullptr;  // owned by browser

    String host;
    int64_t port = 0;
    bool have_target = false;
    bool announced = false;
    double lost_until = 0.0;

    std::unique_ptr<BonjourBrowser> browser;
    bool browsing = false;
    double browse_retry_at = -1.0;
    std::string last_browse_error;
    std::map<std::string, Throttle> resolve_failures;

    Ref<HTTPClient> http;
    bool in_flight = false;
    Kind kind = Kind::Health;
    HTTPClient::Method method = HTTPClient::METHOD_GET;
    String path;
    PackedStringArray headers;
    PackedByteArray body;
    bool sent = false;
    int64_t response_code = 0;
    int64_t expected_length = -1;  // -1: chunked or until the server closes
    std::string response;
    double deadline = 0.0;

    // What gets uploaded (docs/devlink-protocol.md): the session files in log_dir.
    bool test_logs = false;
    std::string test_log_dir;
    std::string test_current;
    bool sources_known = false;
    std::string log_dir;
    std::string current;
    std::vector<ofs_upload::SessionFile> earlier;  // sizes as listed; nothing writes them
    ofs_upload::Offsets acked;
    // Earlier files whose unsent tail holds no complete line (a crash mid-write), by size.
    std::map<std::string, uint64_t> no_complete_line;
    std::map<std::string, LineCount> line_counts;
    Batch batch;

    double next_health = 0.0;
    double next_upload = 0.0;
    double next_params = 0.0;
    double next_sink_check = 0.0;
    int upload_attempt = 0;
    int health_attempt = 0;
    int params_attempt = 0;
    int unanswered = 0;
    Throttle upload_failures;
    Throttle health_failures;
    Throttle params_failures;
    Throttle rejections;
    int64_t rejections_suppressed = 0;
    Throttle problems;

    double scaled(double seconds) const { return seconds * time_scale; }

    double backoff() const {
        const int exponent = std::clamp(upload_attempt - 1, 0, 16);
        return scaled(std::min(kBackoffCap, std::ldexp(1.0, exponent)));
    }

    String via() const {
        return (browser && browser->supported() && !test_target) ? "bonjour" : "localhost";
    }

    void set_state(State next) {
        if (state == next) {
            return;
        }
        state = next;
        owner->emit_signal("status_changed", state_name(next));
    }

    void start(double now) {
        announced = false;
        unanswered = 0;
        health_attempt = 0;
        offered_hash = String();
        if (test_target) {
            host = test_host;
            port = test_port;
            have_target = true;
        } else {
            if (!browser) {
                if (test_bonjour) {
                    auto fake = std::make_unique<ScriptedBrowser>();
                    scripted = fake.get();
                    browser = std::move(fake);
                } else {
                    browser = BonjourBrowser::create(kServiceType);
                }
            }
            if (browser->supported()) {
                have_target = false;
                start_browsing();
            } else {
                host = kLocalHost;
                port = kLocalPort;
                have_target = true;
            }
        }
        next_health = now;
        set_state(State::Searching);
    }

    void stop() {
        abort_request();
        if (browsing) {
            browser->stop();
            browsing = false;
        }
        have_target = false;
        set_state(State::Off);
    }

    void start_browsing() {
        browsing = true;
        browse_retry_at = -1.0;
        browser->start([this](const BonjourBrowser::Found &found) { on_found(found); },
                       [this](const std::string &error, bool denied) { on_browse_failed(error, denied); },
                       [this](const std::string &name, const std::string &error, bool denied) {
                           on_resolve_failed(name, error, denied);
                       });
    }

    void restart_browsing() {
        browser->stop();
        start_browsing();
    }

    void on_found(const BonjourBrowser::Found &found) {
        last_browse_error.clear();
        resolve_failures.erase(found.name);
        // Stay on a PC that answers; a lost link restarts browsing and picks up the new one.
        if (state == State::Connected) {
            return;
        }
        host = to_godot(found.host);
        port = found.port;
        have_target = true;
        Dictionary data;
        data["host"] = host;
        data["port"] = port;
        data["via"] = "bonjour";
        emit_log("devlink", "info", "devlink.discovered", data);
        announced = true;
        next_health = now_seconds();
    }

    void on_browse_failed(const std::string &error, bool denied) {
        if (error != last_browse_error) {
            Dictionary data;
            data["error"] = to_godot(error);
            data["local_network_denied"] = denied;
            emit_log("devlink", "warn", "devlink.browse_failed", data);
            last_browse_error = error;
        }
        browse_retry_at = now_seconds() + scaled(kBrowseRetry);
    }

    void on_resolve_failed(const std::string &name, const std::string &error, bool denied) {
        if (!resolve_failures[name].allow(error, now_seconds(), scaled(kFailureLogInterval))) {
            return;
        }
        Dictionary data;
        data["name"] = to_godot(name);
        data["error"] = to_godot(error);
        data["local_network_denied"] = denied;
        emit_log("devlink", "warn", "devlink.resolve_failed", data);
    }

    void tick(double now) {
        if (state == State::Off) {
            return;
        }
        if (now >= next_sink_check) {
            next_sink_check = now + scaled(kSinkCheckInterval);
            OfsLog::check_sink();
        }
        if (browsing) {
            browser->poll();
        }
        if (state == State::Lost && now >= lost_until) {
            set_state(State::Searching);
        }
        if (browsing && browse_retry_at >= 0.0 && now >= browse_retry_at) {
            if (state == State::Connected) {
                browse_retry_at = -1.0;
            } else {
                restart_browsing();
            }
        }
        const uint64_t frame_start = Time::get_singleton()->get_ticks_usec();
        if (!in_flight) {
            begin_next(now);
        }
        if (in_flight) {
            advance(frame_start);
        }
    }

    void begin_next(double now) {
        // "lost" keeps trying /health; it shows a while before "searching" takes over.
        if (state == State::Searching || state == State::Lost) {
            if (have_target && now >= next_health) {
                begin(Kind::Health, HTTPClient::METHOD_GET, "/health", PackedStringArray(), PackedByteArray(), now);
            }
            return;
        }
        if (state != State::Connected) {
            return;
        }
        if (now >= next_params) {
            PackedStringArray params_headers;
            if (!applied_hash.is_empty()) {
                params_headers.push_back("If-None-Match: \"" + applied_hash + "\"");
            }
            begin(Kind::Params, HTTPClient::METHOD_GET, "/params", params_headers, PackedByteArray(), now);
            return;
        }
        if (now >= next_upload) {
            begin_upload(now);
        }
    }

    void refresh_sources() {
        const std::string dir = test_logs ? test_log_dir : OfsLog::sink_directory();
        const std::string live = test_logs ? test_current : OfsLog::sink_file_name();
        if (sources_known && dir == log_dir && live == current) {
            return;
        }
        const bool new_dir = !sources_known || dir != log_dir;
        log_dir = dir;
        current = live;
        sources_known = true;
        rescan(new_dir);
    }

    void rescan(bool reload_state) {
        earlier.clear();
        no_complete_line.clear();
        line_counts.clear();
        if (log_dir.empty()) {
            acked.clear();
            return;
        }
        if (reload_state) {
            std::string error;
            acked = ofs_upload::load(log_dir, error);
            if (!error.empty()) {
                report_problem("devlink: " + error + "; those sessions upload again from the start");
            }
        }
        for (const ofs_upload::SessionFile &file : ofs_upload::list(log_dir)) {
            if (file.name != current) {
                earlier.push_back(file);
            }
        }
    }

    uint64_t offset_of(const std::string &name) const {
        const auto it = acked.find(name);
        return it == acked.end() ? 0 : it->second;
    }

    uint64_t size_of(const std::string &name) const {
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(fs::u8path(ofs_upload::join(log_dir, name)), ec);
        return ec ? 0 : uint64_t(size);
    }

    bool backlog_waiting() const {
        for (const ofs_upload::SessionFile &file : earlier) {
            const auto stuck = no_complete_line.find(file.name);
            if (file.size > offset_of(file.name) && (stuck == no_complete_line.end() || stuck->second != file.size)) {
                return true;
            }
        }
        return false;
    }

    void report_backlog() {
        refresh_sources();
        rescan(false);
        int64_t sessions = 0;
        uint64_t bytes = 0;
        for (const ofs_upload::SessionFile &file : earlier) {
            const uint64_t from = offset_of(file.name);
            if (file.size > from) {
                ++sessions;
                bytes += file.size - from;
            }
        }
        if (sessions == 0) {
            return;
        }
        Dictionary data;
        data["sessions"] = sessions;
        data["bytes"] = int64_t(bytes);
        emit_log("devlink", "info", "devlink.backlog", data);
    }

    // The live session first, so the PC's tail stays current, then the backlog oldest first.
    bool next_batch(Batch &out) {
        if (!current.empty() && read_batch(current, size_of(current), true, out)) {
            return true;
        }
        for (const ofs_upload::SessionFile &file : earlier) {
            if (read_batch(file.name, file.size, false, out)) {
                return true;
            }
        }
        return false;
    }

    // The complete lines after the acknowledged offset, within the server's limits. A line
    // over 1 MiB can never be sent, so it is skipped and reported.
    bool read_batch(const std::string &name, uint64_t size, bool live, Batch &out) {
        uint64_t from = offset_of(name);
        if (size < from) {
            report_problem("devlink: " + name + " is shorter than its uploaded offset; sending it again from the start");
            from = 0;
            acked[name] = 0;
        }
        if (size == from) {
            return false;
        }
        if (!live) {
            const auto stuck = no_complete_line.find(name);
            if (stuck != no_complete_line.end() && stuck->second == size) {
                return false;
            }
        }
        std::ifstream in(fs::u8path(ofs_upload::join(log_dir, name)), std::ios::binary);
        if (!in) {
            report_problem("devlink: cannot open " + name + " to upload it");
            if (!live) {
                no_complete_line[name] = size;
            }
            return false;
        }
        while (from < size) {
            in.clear();
            in.seekg(std::streamoff(from));
            std::string bytes(size_t(std::min<uint64_t>(size - from, kBatchBytes)), '\0');
            in.read(bytes.data(), std::streamsize(bytes.size()));
            bytes.resize(size_t(std::max<std::streamsize>(in.gcount(), 0)));
            size_t end = 0;
            int64_t lines = 0;
            while (lines < kBatchLines) {
                const size_t newline = bytes.find('\n', end);
                if (newline == std::string::npos) {
                    break;
                }
                end = newline + 1;
                ++lines;
            }
            if (lines > 0) {
                bytes.resize(end);
                out.name = name;
                out.from = from;
                out.to = from + end;
                out.lines = lines;
                out.more = out.to < size;
                out.bytes = std::move(bytes);
                return true;
            }
            if (bytes.size() < kBatchBytes) {
                break;  // an unfinished last line
            }
            const uint64_t line_end = find_line_end(in, from + kBatchBytes);
            if (line_end == 0) {
                break;
            }
            acked[name] = line_end;
            line_counts.erase(name);
            save_state();
            log_rejected(413, 1,
                         "a " + std::to_string(line_end - from) + "-byte line in " + name +
                             " is over the 1 MiB request limit; skipped without sending",
                         now_seconds());
            from = line_end;
        }
        if (!live) {
            no_complete_line[name] = size;
        }
        return false;
    }

    // Offset just past the next '\n' at or after start, 0 when the file ends first.
    static uint64_t find_line_end(std::ifstream &in, uint64_t start) {
        in.clear();
        in.seekg(std::streamoff(start));
        std::vector<char> chunk(64 * 1024);
        uint64_t at = start;
        while (in) {
            in.read(chunk.data(), std::streamsize(chunk.size()));
            const std::streamsize got = in.gcount();
            if (got <= 0) {
                break;
            }
            const char *newline = static_cast<const char *>(std::memchr(chunk.data(), '\n', size_t(got)));
            if (newline != nullptr) {
                return at + uint64_t(newline - chunk.data()) + 1;
            }
            at += uint64_t(got);
        }
        return 0;
    }

    int64_t pending_lines(const std::string &name, uint64_t size) {
        const uint64_t from = offset_of(name);
        if (size <= from) {
            return 0;
        }
        LineCount &count = line_counts[name];
        if (count.from != from || count.to < from || count.to > size) {
            count = LineCount{from, from, 0};
        }
        if (count.to < size) {
            const int64_t lines = ofs_upload::count_lines(ofs_upload::join(log_dir, name), count.to, size);
            if (lines >= 0) {
                count.lines += lines;
                count.to = size;
            }
        }
        return count.lines;
    }

    int64_t queued_lines() {
        refresh_sources();
        int64_t total = current.empty() ? 0 : pending_lines(current, size_of(current));
        for (const ofs_upload::SessionFile &file : earlier) {
            total += pending_lines(file.name, file.size);
        }
        return total;
    }

    void save_state() {
        if (log_dir.empty()) {
            return;
        }
        ofs_upload::Offsets keep;
        const auto add = [&](const std::string &name) {
            const auto it = acked.find(name);
            if (it != acked.end()) {
                keep[name] = it->second;
            }
        };
        if (!current.empty()) {
            add(current);
        }
        for (const ofs_upload::SessionFile &file : earlier) {
            add(file.name);
        }
        std::string error;
        if (!ofs_upload::save(log_dir, keep, error)) {
            report_problem("devlink: " + error + "; lines may be sent twice after a restart");
        }
    }

    void acknowledge() {
        acked[batch.name] = batch.to;
        line_counts.erase(batch.name);
        save_state();
    }

    void begin_upload(double now) {
        refresh_sources();
        Batch next;
        if (!next_batch(next)) {
            next_upload = now + scaled(kUploadInterval);
            return;
        }
        PackedByteArray bytes;
        bytes.resize(int64_t(next.bytes.size()));
        std::memcpy(bytes.ptrw(), next.bytes.data(), next.bytes.size());
        next.bytes.clear();
        batch = std::move(next);
        PackedStringArray upload_headers;
        upload_headers.push_back("Content-Type: application/x-ndjson");
        begin(Kind::Logs, HTTPClient::METHOD_POST, "/logs", upload_headers, bytes, now);
    }

    void begin(Kind request_kind, HTTPClient::Method request_method, const String &request_path,
               const PackedStringArray &request_headers, const PackedByteArray &request_body, double now) {
        kind = request_kind;
        method = request_method;
        path = request_path;
        headers = request_headers;
        // One connection per request: no stale keep-alive state to recover from, and the
        // request rate (a few per second) makes the reconnect cost irrelevant.
        headers.push_back("Connection: close");
        body = request_body;
        sent = false;
        response_code = 0;
        expected_length = -1;
        response.clear();
        deadline = now + scaled(host == kLocalHost ? kLocalConnectTimeout : kRequestTimeout);
        in_flight = true;
        http.instantiate();
        http->set_blocking_mode(false);
        const Error err = http->connect_to_host(host, int(port));
        if (err != OK) {
            fail("connect_to_host failed with error " + std::to_string(int(err)));
        }
    }

    void capture_code() {
        if (response_code == 0 && http->has_response()) {
            response_code = http->get_response_code();
            expected_length = http->get_response_body_length();
        }
    }

    void advance(uint64_t frame_start) {
        while (in_flight) {
            http->poll();
            const HTTPClient::Status status = http->get_status();
            bool progressed = false;
            switch (status) {
                case HTTPClient::STATUS_RESOLVING:
                case HTTPClient::STATUS_CONNECTING:
                case HTTPClient::STATUS_REQUESTING:
                    break;
                case HTTPClient::STATUS_CANT_RESOLVE:
                    fail("cannot resolve host");
                    return;
                case HTTPClient::STATUS_CANT_CONNECT:
                    fail("cannot connect (refused or unreachable)");
                    return;
                case HTTPClient::STATUS_TLS_HANDSHAKE_ERROR:
                    fail("connection error");
                    return;
                case HTTPClient::STATUS_BODY: {
                    capture_code();
                    const PackedByteArray chunk = http->read_response_body_chunk();
                    if (!chunk.is_empty()) {
                        response.append(reinterpret_cast<const char *>(chunk.ptr()), size_t(chunk.size()));
                        progressed = true;
                    }
                    if (expected_length >= 0 && int64_t(response.size()) >= expected_length) {
                        finish();
                        return;
                    }
                    break;
                }
                case HTTPClient::STATUS_CONNECTED:
                    if (!sent) {
                        const Error err = http->request_raw(method, path, headers, body);
                        if (err != OK) {
                            fail("request failed with error " + std::to_string(int(err)));
                            return;
                        }
                        sent = true;
                        deadline = now_seconds() + scaled(kRequestTimeout);
                        progressed = true;
                        break;
                    }
                    [[fallthrough]];
                case HTTPClient::STATUS_DISCONNECTED:
                // Godot reports a connection error when the server closes right after a
                // complete answer (we ask it to, with Connection: close).
                case HTTPClient::STATUS_CONNECTION_ERROR:
                    capture_code();
                    if (response_code != 0 &&
                        (expected_length < 0 || int64_t(response.size()) >= expected_length)) {
                        finish();
                        return;
                    }
                    if (status != HTTPClient::STATUS_CONNECTED) {
                        fail(response_code != 0 ? "connection closed mid-response"
                                                : "connection closed without a response");
                        return;
                    }
                    break;
            }
            if (now_seconds() >= deadline) {
                fail(sent ? "timeout waiting for the answer" : "timeout connecting");
                return;
            }
            if (!progressed || Time::get_singleton()->get_ticks_usec() - frame_start > kFrameBudgetUsec) {
                return;
            }
        }
    }

    void end_request() {
        in_flight = false;
        body = PackedByteArray();
        if (http.is_valid()) {
            http->close();
        }
    }

    void abort_request() {
        if (in_flight) {
            end_request();
        }
    }

    void fail(const std::string &error) { failed(kind, 0, error, std::string(), true); }

    void finish() {
        const Kind finished = kind;
        const int64_t code = response_code;
        const std::string text = std::move(response);
        end_request();
        if (code >= 500) {
            failed(finished, code, "HTTP " + std::to_string(code), text, true);
            return;
        }
        unanswered = 0;
        switch (finished) {
            case Kind::Health: on_health(code, text); break;
            case Kind::Logs: on_logs(code, text); break;
            case Kind::Params: on_params(code, text); break;
        }
    }

    // status is the HTTP status, 0 when there was no response. Only unanswered requests and
    // 5xx count towards losing the link: any other answer proves the PC is there.
    void failed(Kind failed_kind, int64_t status, const std::string &error, const std::string &reply,
                bool unanswered_request) {
        end_request();
        const double now = now_seconds();
        switch (failed_kind) {
            case Kind::Health:
                next_health = now + scaled(kHealthInterval);
                log_request_failed(health_failures, ++health_attempt, "/health", status, error, reply, now);
                return;
            case Kind::Logs:
                // Nothing to put back: the offset only moves on a 2xx.
                ++upload_attempt;
                next_upload = now + backoff();
                log_upload_failed(status, error, now);
                break;
            case Kind::Params:
                next_params = now + scaled(kParamsInterval);
                log_request_failed(params_failures, ++params_attempt, "/params", status, error, reply, now);
                break;
        }
        if (unanswered_request && state == State::Connected && ++unanswered >= kFailuresBeforeLost) {
            lose(error, now);
        }
    }

    void log_request_failed(Throttle &throttle, int attempt, const char *endpoint, int64_t status,
                            const std::string &error, const std::string &reply, double now) {
        if (!throttle.allow(std::to_string(status) + " " + error, now, scaled(kFailureLogInterval))) {
            return;
        }
        Dictionary data;
        data["endpoint"] = endpoint;
        data["status"] = status;
        data["error"] = to_godot(error);
        data["attempt"] = attempt;
        const String text = excerpt(reply);
        if (!text.is_empty()) {
            data["body"] = text;
        }
        emit_log("devlink", "warn", "devlink.request_failed", data);
    }

    // These lines join the files they report on, so an unreachable PC must not fill them.
    void log_upload_failed(int64_t status, const std::string &error, double now) {
        const std::string key = std::to_string(status) + " " + error;
        if (upload_attempt == 1) {
            upload_failures.note(key, now);
        } else if (!upload_failures.allow(key, now, scaled(kFailureLogInterval))) {
            return;
        }
        Dictionary data;
        data["status"] = status;
        data["error"] = to_godot(error);
        data["queued_lines"] = queued_lines();
        data["attempt"] = upload_attempt;
        emit_log("devlink", "warn", "devlink.upload_failed", data);
    }

    void log_rejected(int64_t status, int64_t dropped, const std::string &reply, double now) {
        if (!rejections.allow(std::to_string(status), now, scaled(kFailureLogInterval))) {
            ++rejections_suppressed;
            return;
        }
        Dictionary data;
        data["status"] = status;
        data["dropped_lines"] = dropped;
        data["suppressed_since_last"] = rejections_suppressed;
        const String text = excerpt(reply);
        if (!text.is_empty()) {
            data["body"] = text;
        }
        emit_log("devlink", "warn", "devlink.upload_rejected", data);
        rejections_suppressed = 0;
    }

    void report_problem(const std::string &message) {
        if (!problems.allow(message, now_seconds(), scaled(kFailureLogInterval))) {
            return;
        }
        Dictionary data;
        data["kind"] = "log";
        data["message"] = to_godot(message);
        emit_log("app", "warn", "app.error", data);
    }

    void lose(const std::string &reason, double now) {
        Dictionary data;
        data["reason"] = to_godot(reason);
        data["queued_lines"] = queued_lines();
        emit_log("devlink", "warn", "devlink.lost", data);
        set_state(State::Lost);
        lost_until = now + scaled(kLostShownFor);
        announced = false;
        unanswered = 0;
        next_health = now + scaled(kHealthInterval);
        if (browsing) {
            restart_browsing();
        }
    }

    void on_health(int64_t code, const std::string &text) {
        const double now = now_seconds();
        const json reply = parse_json(text);
        const bool ok = code == 200 && reply.is_object() && reply.contains("ok") && reply["ok"].is_boolean() &&
                        reply["ok"].get<bool>();
        if (!ok) {
            failed(Kind::Health, code, code == 200 ? "answer is not {\"ok\": true}" : "HTTP " + std::to_string(code),
                   text, false);
            return;
        }
        health_attempt = 0;
        health_failures.reset();
        if (state != State::Searching && state != State::Lost) {
            next_health = now + scaled(kHealthInterval);
            return;
        }
        int64_t version = 0;
        if (reply.contains("v") && reply["v"].is_number_integer()) {
            version = reply["v"].get<int64_t>();
        }
        if (!announced) {
            Dictionary found;
            found["host"] = host;
            found["port"] = port;
            found["via"] = via();
            emit_log("devlink", "info", "devlink.discovered", found);
            announced = true;
        }
        Dictionary data;
        data["host"] = host;
        data["port"] = port;
        data["server_version"] = version;
        emit_log("devlink", "info", "devlink.connected", data);
        report_backlog();
        unanswered = 0;
        next_params = now;
        next_upload = upload_attempt > 0 ? now + backoff() : now;
        set_state(State::Connected);
    }

    // A 4xx other than these says the server will never take the batch.
    static bool rejected_for_good(int64_t code) { return code >= 400 && code < 500 && code != 408 && code != 429; }

    void on_logs(int64_t code, const std::string &text) {
        const double now = now_seconds();
        if (code >= 200 && code < 300) {
            upload_attempt = 0;
            acknowledge();
            next_upload = (batch.more || backlog_waiting()) ? now : now + scaled(kUploadInterval);
            return;
        }
        if (!rejected_for_good(code)) {
            failed(Kind::Logs, code, "HTTP " + std::to_string(code), text, false);
            return;
        }
        // A 400 means the server stored the valid lines; the bad ones would only fail again.
        int64_t dropped = batch.lines;
        if (code == 400) {
            const json reply = parse_json(text);
            if (reply.is_object() && reply.contains("bad_lines") && reply["bad_lines"].is_array()) {
                dropped = int64_t(reply["bad_lines"].size());
            }
        }
        upload_attempt = 0;
        acknowledge();
        log_rejected(code, dropped, text, now);
        next_upload = (batch.more || backlog_waiting()) ? now : now + scaled(kUploadInterval);
    }

    void on_params(int64_t code, const std::string &text) {
        const double now = now_seconds();
        next_params = now + scaled(kParamsInterval);
        if (code != 200 && code != 304) {
            failed(Kind::Params, code, "HTTP " + std::to_string(code), text, false);
            return;
        }
        if (code == 304) {
            params_attempt = 0;
            params_failures.reset();
            return;
        }
        const json reply = parse_json(text);
        if (!reply.is_object() || !reply.contains("hash") || !reply["hash"].is_string() ||
            !reply.contains("files") || !reply["files"].is_object()) {
            failed(Kind::Params, code, "answer has no hash and files object", text, false);
            return;
        }
        params_attempt = 0;
        params_failures.reset();
        const String hash = to_godot(reply["hash"].get<std::string>());
        if (hash == applied_hash || hash == offered_hash) {
            return;
        }
        offered_hash = hash;
        owner->emit_signal("params_received", to_godot(dump_json(reply["files"])), hash);
    }
};

void OfsDevlink::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_status"), &OfsDevlink::get_status);
    ClassDB::bind_method(D_METHOD("get_host"), &OfsDevlink::get_host);
    ClassDB::bind_method(D_METHOD("get_port"), &OfsDevlink::get_port);
    ClassDB::bind_method(D_METHOD("set_link_enabled", "enabled"), &OfsDevlink::set_link_enabled);
    ClassDB::bind_method(D_METHOD("is_link_enabled"), &OfsDevlink::is_link_enabled);
    ClassDB::bind_method(D_METHOD("set_applied_params_hash", "hash"), &OfsDevlink::set_applied_params_hash);
    ClassDB::bind_method(D_METHOD("get_applied_params_hash"), &OfsDevlink::get_applied_params_hash);
    ClassDB::bind_method(D_METHOD("set_test_target", "host", "port"), &OfsDevlink::set_test_target);
    ClassDB::bind_method(D_METHOD("set_test_time_scale", "scale"), &OfsDevlink::set_test_time_scale);
    ClassDB::bind_method(D_METHOD("set_test_log_dir", "log_dir", "current_file"), &OfsDevlink::set_test_log_dir);
    ClassDB::bind_method(D_METHOD("set_test_bonjour", "enabled"), &OfsDevlink::set_test_bonjour);
    ClassDB::bind_method(D_METHOD("push_test_bonjour", "event"), &OfsDevlink::push_test_bonjour);
    ClassDB::bind_method(D_METHOD("get_test_bonjour_starts"), &OfsDevlink::get_test_bonjour_starts);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "link_enabled"), "set_link_enabled", "is_link_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "applied_params_hash"), "set_applied_params_hash", "get_applied_params_hash");
    ADD_SIGNAL(MethodInfo("status_changed", PropertyInfo(Variant::STRING, "status")));
    ADD_SIGNAL(MethodInfo("params_received", PropertyInfo(Variant::STRING, "json_text"), PropertyInfo(Variant::STRING, "hash")));
}

OfsDevlink::OfsDevlink() : impl_(std::make_unique<Impl>(this)) {}

OfsDevlink::~OfsDevlink() { impl_->abort_request(); }

String OfsDevlink::get_status() const { return state_name(impl_->state); }

String OfsDevlink::get_host() const { return impl_->have_target ? impl_->host : String(); }

int64_t OfsDevlink::get_port() const { return impl_->have_target ? impl_->port : 0; }

void OfsDevlink::set_link_enabled(bool enabled) {
    if (impl_->enabled == enabled) {
        return;
    }
    impl_->enabled = enabled;
    if (!impl_->ready) {
        return;
    }
    if (enabled) {
        impl_->start(now_seconds());
    } else {
        impl_->stop();
    }
}

bool OfsDevlink::is_link_enabled() const { return impl_->enabled; }

void OfsDevlink::set_applied_params_hash(const String &hash) { impl_->applied_hash = hash; }

String OfsDevlink::get_applied_params_hash() const { return impl_->applied_hash; }

void OfsDevlink::set_test_target(const String &host, int64_t port) {
    impl_->test_target = true;
    impl_->test_host = host;
    impl_->test_port = port;
    if (impl_->state != State::Off) {
        impl_->abort_request();
        impl_->start(now_seconds());
    }
}

void OfsDevlink::set_test_time_scale(double scale) { impl_->time_scale = std::max(scale, 0.001); }

void OfsDevlink::set_test_log_dir(const String &log_dir, const String &current_file) {
    impl_->test_logs = true;
    impl_->test_log_dir = to_std(log_dir);
    impl_->test_current = to_std(current_file);
    impl_->sources_known = false;
}

void OfsDevlink::set_test_bonjour(bool enabled) { impl_->test_bonjour = enabled; }

void OfsDevlink::push_test_bonjour(const Dictionary &event) {
    if (impl_->scripted == nullptr) {
        return;
    }
    ScriptedBrowser::Event e;
    e.kind = to_std(String(event.get("kind", "")));
    e.name = to_std(String(event.get("name", "")));
    e.host = to_std(String(event.get("host", "")));
    e.port = int(int64_t(event.get("port", 0)));
    e.error = to_std(String(event.get("error", "")));
    e.denied = bool(event.get("denied", false));
    impl_->scripted->queued.push_back(e);
}

int64_t OfsDevlink::get_test_bonjour_starts() const {
    return impl_->scripted == nullptr ? 0 : impl_->scripted->starts;
}

void OfsDevlink::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    impl_->ready = true;
    set_process(true);
    if (impl_->enabled) {
        impl_->start(now_seconds());
    }
}

void OfsDevlink::_process(double) { impl_->tick(now_seconds()); }
