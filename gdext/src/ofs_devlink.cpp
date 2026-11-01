#include "ofs_devlink.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "bonjour_browser.h"
#include "ofs_log.h"

using namespace godot;
using nlohmann::json;

namespace {

constexpr const char *kServiceType = "_ofs-devlink._tcp";
constexpr const char *kLocalHost = "127.0.0.1";
constexpr int64_t kLocalPort = 47147;

constexpr double kUploadInterval = 1.0;
constexpr double kParamsInterval = 3.0;
constexpr double kHealthInterval = 5.0;
constexpr double kRequestTimeout = 5.0;
constexpr double kBackoffCap = 30.0;
constexpr double kFailureLogInterval = 30.0;
constexpr double kDroppedLogInterval = 60.0;
constexpr double kDroppedCheckInterval = 1.0;
constexpr double kBrowseRetry = 30.0;
constexpr int kFailuresBeforeLost = 3;
constexpr int64_t kBatchLines = 512;
constexpr size_t kBatchBytes = 1024 * 1024;
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

json parse_json(const std::string &text) { return json::parse(text, nullptr, false); }

// Exceptions may be compiled out (godot-cpp disables them), so nothing here may throw.
std::string dump_json(const json &value) { return value.dump(-1, ' ', false, json::error_handler_t::replace); }

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

    String host;
    int64_t port = 0;
    bool have_target = false;
    bool announced = false;

    std::unique_ptr<BonjourBrowser> browser;
    bool browsing = false;
    double browse_retry_at = -1.0;
    std::string last_browse_error;

    Ref<HTTPClient> http;
    bool in_flight = false;
    Kind kind = Kind::Health;
    HTTPClient::Method method = HTTPClient::METHOD_GET;
    String path;
    PackedStringArray headers;
    PackedByteArray body;
    PackedStringArray batch;
    bool sent = false;
    int64_t response_code = 0;
    int64_t expected_length = -1;  // -1: chunked or until the server closes
    std::string response;
    double deadline = 0.0;

    double next_health = 0.0;
    double next_upload = 0.0;
    double next_params = 0.0;
    int upload_attempt = 0;
    int unanswered = 0;
    bool failure_logged = false;
    double last_failure_log = 0.0;
    bool rejected_logged = false;
    double last_rejected_log = 0.0;
    bool bad_params_logged = false;
    int64_t dropped_reported = 0;
    bool dropped_logged = false;
    double last_dropped_log = 0.0;
    double next_dropped_check = 0.0;

    double scaled(double seconds) const { return seconds * time_scale; }

    double backoff() const {
        const int exponent = std::clamp(upload_attempt - 1, 0, 16);
        return scaled(std::min(kBackoffCap, std::ldexp(1.0, exponent)));
    }

    String via() const {
        return (browser && browser->supported() && !test_target) ? "bonjour" : "localhost";
    }

    static void emit_log(const char *subsystem, const char *level, const char *event, const Dictionary &data) {
        OfsLog::emit(subsystem, level, event, data);
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
        offered_hash = String();
        if (test_target) {
            host = test_host;
            port = test_port;
            have_target = true;
        } else {
            if (!browser) {
                browser = BonjourBrowser::create(kServiceType);
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
                       [this](const std::string &error, bool denied) { on_browse_failed(error, denied); });
    }

    void restart_browsing() {
        browser->stop();
        start_browsing();
    }

    void on_found(const BonjourBrowser::Found &found) {
        last_browse_error.clear();
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

    void tick(double now) {
        if (state == State::Off) {
            return;
        }
        if (browsing) {
            browser->poll();
        }
        if (state == State::Lost) {
            set_state(State::Searching);
        }
        if (browsing && browse_retry_at >= 0.0 && now >= browse_retry_at) {
            if (state == State::Connected) {
                browse_retry_at = -1.0;
            } else {
                restart_browsing();
            }
        }
        check_dropped(now);
        const uint64_t frame_start = Time::get_singleton()->get_ticks_usec();
        if (!in_flight) {
            begin_next(now);
        }
        if (in_flight) {
            advance(frame_start);
        }
    }

    void check_dropped(double now) {
        if (now < next_dropped_check) {
            return;
        }
        next_dropped_check = now + scaled(kDroppedCheckInterval);
        const int64_t total = OfsLog::dropped_total();
        if (total < dropped_reported) {
            dropped_reported = total;  // OfsLog.start replaced the ring buffer
        }
        if (total == dropped_reported) {
            return;
        }
        if (dropped_logged && now - last_dropped_log < scaled(kDroppedLogInterval)) {
            return;
        }
        Dictionary data;
        data["count"] = total - dropped_reported;
        data["total_dropped"] = total;
        emit_log("log", "warn", "log.dropped", data);
        dropped_reported = total;
        dropped_logged = true;
        last_dropped_log = now;
    }

    void begin_next(double now) {
        if (state == State::Searching) {
            if (have_target && now >= next_health) {
                begin(Kind::Health, HTTPClient::METHOD_GET, "/health", PackedStringArray(), PackedByteArray(),
                      PackedStringArray(), now);
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
            begin(Kind::Params, HTTPClient::METHOD_GET, "/params", params_headers, PackedByteArray(),
                  PackedStringArray(), now);
            return;
        }
        if (now >= next_upload) {
            begin_upload(now);
        }
    }

    void begin_upload(double now) {
        PackedStringArray lines = OfsLog::take_batch(kBatchLines);
        if (lines.is_empty()) {
            next_upload = now + scaled(kUploadInterval);
            return;
        }
        std::string text;
        int64_t used = 0;
        for (; used < lines.size(); ++used) {
            const CharString line = lines[used].utf8();
            const size_t length = size_t(line.length());
            // A single oversized line still goes alone, so it can't block the queue forever.
            if (used > 0 && text.size() + length + 1 > kBatchBytes) {
                break;
            }
            text.append(line.get_data(), length);
            text.push_back('\n');
        }
        if (used < lines.size()) {
            OfsLog::restore_batch(lines.slice(used));
            lines = lines.slice(0, used);
        }
        PackedByteArray bytes;
        bytes.resize(int64_t(text.size()));
        std::copy(text.begin(), text.end(), bytes.ptrw());
        PackedStringArray upload_headers;
        upload_headers.push_back("Content-Type: application/x-ndjson");
        begin(Kind::Logs, HTTPClient::METHOD_POST, "/logs", upload_headers, bytes, lines, now);
    }

    void begin(Kind request_kind, HTTPClient::Method request_method, const String &request_path,
               const PackedStringArray &request_headers, const PackedByteArray &request_body,
               const PackedStringArray &lines, double now) {
        kind = request_kind;
        method = request_method;
        path = request_path;
        headers = request_headers;
        // One connection per request: no stale keep-alive state to recover from, and the
        // request rate (a few per second) makes the reconnect cost irrelevant.
        headers.push_back("Connection: close");
        body = request_body;
        batch = lines;
        sent = false;
        response_code = 0;
        expected_length = -1;
        response.clear();
        deadline = now + scaled(kRequestTimeout);
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
                fail("timeout");
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
        batch = PackedStringArray();
        if (http.is_valid()) {
            http->close();
        }
    }

    void abort_request() {
        if (!in_flight) {
            return;
        }
        if (kind == Kind::Logs) {
            OfsLog::restore_batch(batch);
        }
        end_request();
    }

    void fail(const std::string &error) { failed(kind, 0, error, batch, true); }

    void finish() {
        const Kind finished = kind;
        const int64_t code = response_code;
        const std::string text = std::move(response);
        const PackedStringArray lines = batch;
        end_request();
        if (code >= 500) {
            failed(finished, code, "HTTP " + std::to_string(code), lines, true);
            return;
        }
        unanswered = 0;
        switch (finished) {
            case Kind::Health: on_health(code, text); break;
            case Kind::Logs: on_logs(code, text, lines); break;
            case Kind::Params: on_params(code, text); break;
        }
    }

    // status is the HTTP status, 0 when there was no response. Only unanswered requests and
    // 5xx count towards losing the link: any other answer proves the PC is there.
    void failed(Kind failed_kind, int64_t status, const std::string &error, PackedStringArray lines,
                bool unanswered_request) {
        end_request();
        const double now = now_seconds();
        if (failed_kind == Kind::Health) {
            next_health = now + scaled(kHealthInterval);
            return;
        }
        if (failed_kind == Kind::Logs) {
            OfsLog::restore_batch(lines);
            ++upload_attempt;
            next_upload = now + backoff();
            log_upload_failed(status, error, now);
        } else {
            next_params = now + scaled(kParamsInterval);
        }
        if (unanswered_request && state == State::Connected && ++unanswered >= kFailuresBeforeLost) {
            lose(error, now);
        }
    }

    // These lines join the queue they report on, so an unreachable PC must not fill it with them.
    void log_upload_failed(int64_t status, const std::string &error, double now) {
        if (upload_attempt > 1 && failure_logged && now - last_failure_log < scaled(kFailureLogInterval)) {
            return;
        }
        Dictionary data;
        data["status"] = status;
        data["error"] = to_godot(error);
        data["queued_lines"] = OfsLog::queued_lines();
        data["attempt"] = upload_attempt;
        emit_log("devlink", "warn", "devlink.upload_failed", data);
        failure_logged = true;
        last_failure_log = now;
    }

    void lose(const std::string &reason, double now) {
        Dictionary data;
        data["reason"] = to_godot(reason);
        data["queued_lines"] = OfsLog::queued_lines();
        emit_log("devlink", "warn", "devlink.lost", data);
        set_state(State::Lost);
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
        if (!ok || state != State::Searching) {
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
        unanswered = 0;
        bad_params_logged = false;
        next_params = now;
        next_upload = upload_attempt > 0 ? now + backoff() : now;
        set_state(State::Connected);
    }

    void on_logs(int64_t code, const std::string &text, const PackedStringArray &lines) {
        const double now = now_seconds();
        if (code >= 200 && code < 300) {
            upload_attempt = 0;
            next_upload = now + scaled(kUploadInterval);
            return;
        }
        if (code != 400) {
            failed(Kind::Logs, code, "HTTP " + std::to_string(code), lines, false);
            return;
        }
        // The server stored the valid lines; the bad ones would only fail again.
        upload_attempt = 0;
        next_upload = now + scaled(kUploadInterval);
        const json reply = parse_json(text);
        std::string error = "server rejected lines";
        if (reply.is_object() && reply.contains("bad_lines") && reply["bad_lines"].is_array()) {
            error = "server rejected " + std::to_string(reply["bad_lines"].size()) + " of " +
                    std::to_string(lines.size()) + " lines: " + dump_json(reply["bad_lines"]);
        }
        if (rejected_logged && now - last_rejected_log < scaled(kFailureLogInterval)) {
            return;
        }
        Dictionary data;
        data["status"] = code;
        data["error"] = to_godot(error);
        data["queued_lines"] = OfsLog::queued_lines();
        data["attempt"] = 1;
        emit_log("devlink", "warn", "devlink.upload_failed", data);
        rejected_logged = true;
        last_rejected_log = now;
    }

    void on_params(int64_t code, const std::string &text) {
        next_params = now_seconds() + scaled(kParamsInterval);
        if (code != 200) {
            return;
        }
        const json reply = parse_json(text);
        if (!reply.is_object() || !reply.contains("hash") || !reply["hash"].is_string() ||
            !reply.contains("files") || !reply["files"].is_object()) {
            if (!bad_params_logged) {
                Dictionary data;
                data["kind"] = "message";
                data["message"] = "devlink: /params answered 200 without a hash and a files object";
                emit_log("app", "warn", "app.error", data);
                bad_params_logged = true;
            }
            return;
        }
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
