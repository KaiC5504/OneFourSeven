#pragma once

#include <memory>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

// The game's half of the phone link (docs/devlink-protocol.md, spec 16.10). Lives in the
// Link autoload (game/scripts/autoload/link.gd extends OfsDevlink). Does its HTTP from
// _process with non-blocking HTTPClient calls, never blocking a frame, and uploads the
// session files OfsLog writes, resuming from the offsets in upload_state.json.
class OfsDevlink : public godot::Node {
    GDCLASS(OfsDevlink, godot::Node)

protected:
    static void _bind_methods();

public:
    OfsDevlink();
    ~OfsDevlink() override;

    // "off" | "searching" | "connected" | "lost"
    godot::String get_status() const;
    godot::String get_host() const;
    int64_t get_port() const;
    // When false the node does nothing and status is "off". Default true.
    void set_link_enabled(bool enabled);
    bool is_link_enabled() const;
    // The hash of the parameters last applied; params_received fires only for new ones.
    void set_applied_params_hash(const godot::String &hash);
    godot::String get_applied_params_hash() const;
    // Test-only: skip discovery and talk to host:port, so tests can use a local fake server.
    void set_test_target(const godot::String &host, int64_t port);
    // Test-only: multiplies every interval, backoff and timeout, so tests run in seconds.
    void set_test_time_scale(double scale);
    // Test-only: upload the session files in log_dir (an absolute OS path) instead of
    // OfsLog's, treating current_file as the one still being written.
    void set_test_log_dir(const godot::String &log_dir, const godot::String &current_file);
    // Test-only, before the node enters the tree: discovery through a scripted browser fed
    // by push_test_bonjour({"kind": "found" | "browse_failed" | "resolve_failed", "name",
    // "host", "port", "error", "denied"}).
    void set_test_bonjour(bool enabled);
    void push_test_bonjour(const godot::Dictionary &event);
    int64_t get_test_bonjour_starts() const;

    void _ready() override;
    void _process(double delta) override;

    // Signals:
    //   status_changed(status: String)
    //   params_received(json_text: String, hash: String)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
