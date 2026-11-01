#include "ofs_device.h"

#include <godot_cpp/core/class_db.hpp>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
// gdext/ios/device_info.mm
extern "C" const char *ofs_ios_thermal_state();
#endif

using namespace godot;

void OfsDevice::_bind_methods() {
    ClassDB::bind_static_method("OfsDevice", D_METHOD("thermal_state"), &OfsDevice::thermal_state);
}

String OfsDevice::thermal_state() {
#if defined(__APPLE__) && TARGET_OS_IOS
    return String(ofs_ios_thermal_state());
#else
    return "unknown";
#endif
}
