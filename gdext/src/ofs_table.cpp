#include "ofs_table.h"

// Contract stub: agent C replaces every body in this file.

using namespace godot;

void OfsTable::_bind_methods() {
    ClassDB::bind_static_method("OfsTable", D_METHOD("dimensions"), &OfsTable::dimensions);
    ClassDB::bind_static_method("OfsTable", D_METHOD("opening_layout", "gap"), &OfsTable::opening_layout);
    ClassDB::bind_static_method("OfsTable", D_METHOD("spot", "colour"), &OfsTable::spot);
}

Dictionary OfsTable::dimensions() { return Dictionary(); }
Array OfsTable::opening_layout(double) { return Array(); }
Dictionary OfsTable::spot(const String &) { return Dictionary(); }
