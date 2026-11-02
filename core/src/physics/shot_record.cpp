#include "ofs/physics/shot_record.hpp"

// Stub from the M1 contracts commit; the simulator agent replaces it.

namespace ofs::phys {

std::string_view event_kind_name(EventKind) { return "strike"; }

BallState ShotRecord::ball_at(int ball, double) const { return final_state.balls.at(static_cast<std::size_t>(ball)); }

TableState ShotRecord::state_at(double) const { return final_state; }

nlohmann::json record_json(const ShotRecord&) { return nlohmann::json::object(); }

}  // namespace ofs::phys
