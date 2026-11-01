#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

// Device facts Godot doesn't expose.
class OfsDevice : public godot::RefCounted {
    GDCLASS(OfsDevice, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    // "nominal" | "fair" | "serious" | "critical" on iOS (ProcessInfo.thermalState);
    // "unknown" elsewhere.
    static godot::String thermal_state();
};
