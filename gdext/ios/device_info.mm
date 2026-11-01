#import <Foundation/Foundation.h>
#include <mach/mach.h>

extern "C" const char *ofs_ios_thermal_state() {
    switch (NSProcessInfo.processInfo.thermalState) {
        case NSProcessInfoThermalStateNominal: return "nominal";
        case NSProcessInfoThermalStateFair: return "fair";
        case NSProcessInfoThermalStateSerious: return "serious";
        case NSProcessInfoThermalStateCritical: return "critical";
    }
    return "unknown";
}

// phys_footprint is the figure iOS's memory limit (jetsam) and Xcode's gauge use.
extern "C" double ofs_ios_memory_footprint_mb() {
    task_vm_info_data_t info = {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return -1.0;
    }
    return double(info.phys_footprint) / (1024.0 * 1024.0);
}
