#pragma once

// What every part of the opponent is handed: the table, the physics values, the rules' geometry
// and the referee's values. Nothing here is owned; the caller keeps them alive for the call.

#include "ofs/opponent/standin.hpp"
#include "ofs/physics/params.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/rules/oracle.hpp"
#include "ofs/rules/referee_params.hpp"

namespace ofs::opponent {

struct Env {
    const phys::TableShape& table;
    const phys::PhysicsParams& params;
    const rules::TableOracle& oracle;
    const rules::RefParams& ref;
    // The stand-in's values: it still plays the safeties, escapes and break-offs (stage 1).
    const StandinParams& standin;
};

}  // namespace ofs::opponent
