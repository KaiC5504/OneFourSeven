#include "ofs_device.h"

#include <godot_cpp/core/class_db.hpp>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
// gdext/ios/device_info.mm
extern "C" const char *ofs_ios_thermal_state();
extern "C" double ofs_ios_memory_footprint_mb();
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

using namespace godot;

void OfsDevice::_bind_methods() {
    ClassDB::bind_static_method("OfsDevice", D_METHOD("thermal_state"), &OfsDevice::thermal_state);
    ClassDB::bind_static_method("OfsDevice", D_METHOD("memory_footprint_mb"), &OfsDevice::memory_footprint_mb);
}

String OfsDevice::thermal_state() {
#if defined(__APPLE__) && TARGET_OS_IOS
    return String(ofs_ios_thermal_state());
#else
    return "unknown";
#endif
}

// -1 when the OS call fails, 0 on platforms without one.
double OfsDevice::memory_footprint_mb() {
#if defined(__APPLE__) && TARGET_OS_IOS
    return ofs_ios_memory_footprint_mb();
#elif defined(_WIN32)
    // PrivateUsage (private bytes), the closest Windows figure to iOS's phys_footprint.
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                              sizeof(counters))) {
        return -1.0;
    }
    return double(counters.PrivateUsage) / 1e6;
#else
    return 0.0;
#endif
}
