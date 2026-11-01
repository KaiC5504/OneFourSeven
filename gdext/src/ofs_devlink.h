#pragma once

#include <memory>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

// The game's half of the phone link (docs/devlink-protocol.md, spec 16.10). Lives in the
// Link autoload (game/scripts/autoload/link.gd extends OfsDevlink). Does all network work
// from _process with non-blocking HTTPClient calls, never blocking a frame.
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

    void _ready() override;
    void _process(double delta) override;

    // Signals:
    //   status_changed(status: String)
    //   params_received(json_text: String, hash: String)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
