#include "ofs/physics/repro.hpp"

// Stub from the M1 contracts commit; the simulator agent replaces it.

namespace ofs::phys {

nlohmann::json make_bundle(const SimInput&, const PhysicsParams&) { return nlohmann::json::object(); }
nlohmann::json make_result(const ShotRecord&) { return nlohmann::json::object(); }

bool parse_bundle(const nlohmann::json&, SimInput&, PhysicsParams&, std::string& error) {
    error = "not implemented";
    return false;
}

Divergence compare_results(const nlohmann::json&, const nlohmann::json&, double) { return {}; }

void log_shot(const SimInput&, const PhysicsParams&, const ShotRecord&) {}

}  // namespace ofs::phys
