#include "ofs_devlink.h"

#include <godot_cpp/core/class_db.hpp>

// Contract stub: agent D replaces every body in this file except the bindings' names.

using namespace godot;

struct OfsDevlink::Impl {
    bool enabled = true;
    String applied_hash;
};

void OfsDevlink::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_status"), &OfsDevlink::get_status);
    ClassDB::bind_method(D_METHOD("get_host"), &OfsDevlink::get_host);
    ClassDB::bind_method(D_METHOD("get_port"), &OfsDevlink::get_port);
    ClassDB::bind_method(D_METHOD("set_link_enabled", "enabled"), &OfsDevlink::set_link_enabled);
    ClassDB::bind_method(D_METHOD("is_link_enabled"), &OfsDevlink::is_link_enabled);
    ClassDB::bind_method(D_METHOD("set_applied_params_hash", "hash"), &OfsDevlink::set_applied_params_hash);
    ClassDB::bind_method(D_METHOD("get_applied_params_hash"), &OfsDevlink::get_applied_params_hash);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "link_enabled"), "set_link_enabled", "is_link_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "applied_params_hash"), "set_applied_params_hash", "get_applied_params_hash");
    ADD_SIGNAL(MethodInfo("status_changed", PropertyInfo(Variant::STRING, "status")));
    ADD_SIGNAL(MethodInfo("params_received", PropertyInfo(Variant::STRING, "json_text"), PropertyInfo(Variant::STRING, "hash")));
}

OfsDevlink::OfsDevlink() : impl_(std::make_unique<Impl>()) {}
OfsDevlink::~OfsDevlink() = default;

String OfsDevlink::get_status() const { return "off"; }
String OfsDevlink::get_host() const { return String(); }
int64_t OfsDevlink::get_port() const { return 0; }
void OfsDevlink::set_link_enabled(bool enabled) { impl_->enabled = enabled; }
bool OfsDevlink::is_link_enabled() const { return impl_->enabled; }
void OfsDevlink::set_applied_params_hash(const String &hash) { impl_->applied_hash = hash; }
String OfsDevlink::get_applied_params_hash() const { return impl_->applied_hash; }
void OfsDevlink::_ready() {}
void OfsDevlink::_process(double) {}
