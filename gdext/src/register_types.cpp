#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "engine_logger.h"
#include "ofs_devlink.h"
#include "ofs_log.h"
#include "ofs_table.h"

using namespace godot;

void initialize_onefourseven_module(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(OfsLog);
    GDREGISTER_CLASS(OfsTable);
    GDREGISTER_CLASS(OfsDevlink);
    GDREGISTER_CLASS(OfsEngineLogger);
}

void uninitialize_onefourseven_module(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

extern "C" {
GDExtensionBool GDE_EXPORT ofs_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                            GDExtensionClassLibraryPtr library,
                                            GDExtensionInitialization *initialization) {
    GDExtensionBinding::InitObject init_obj(get_proc_address, library, initialization);
    init_obj.register_initializer(initialize_onefourseven_module);
    init_obj.register_terminator(uninitialize_onefourseven_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}
