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
    // The process's memory footprint in MB: phys_footprint on iOS, PrivateUsage (private
    // committed bytes) on Windows, -1 if the OS call fails; works in release builds, unlike OS.get_static_memory_usage().
    static double memory_footprint_mb();
};
