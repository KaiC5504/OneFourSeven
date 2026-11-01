#include "ofs_device.h"

#include <godot_cpp/core/class_db.hpp>

// Contract stub: agent D owns this file. On iOS it calls ofs_ios_thermal_state() from
// gdext/ios/device_info.mm.

using namespace godot;

void OfsDevice::_bind_methods() {
    ClassDB::bind_static_method("OfsDevice", D_METHOD("thermal_state"), &OfsDevice::thermal_state);
}

String OfsDevice::thermal_state() { return "unknown"; }
