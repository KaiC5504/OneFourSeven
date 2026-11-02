#include "ofs/physics/params.hpp"

#include <cmath>
#include <limits>

#include "ofs/log.hpp"

namespace ofs::phys {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

struct Field {
    const char* name;
    double PhysicsParams::* real = nullptr;
    int PhysicsParams::* integer = nullptr;
    // Inclusive bounds; a value outside them is rejected rather than clamped.
    double min = 0.0;
    double max = kInf;
};

// Bounds reject values that would break the model (a negative friction, a restitution above
// one) or, with the other values at their defaults, keep ordinary shots from coming to rest (the
// shot counts below are ofs_sim_cli fuzz runs with that one value changed), not values that
// merely feel wrong: those are the tuner's call. The guards' lower bounds stay low enough to cut
// ordinary shots short (under about 45 s, 3,000 events or a chain of 130): tests trip them on
// purpose, and a tripped guard is logged.
const std::vector<Field>& fields() {
    static const std::vector<Field> all = {
        {"ball_mass_kg", &PhysicsParams::ball_mass_kg, nullptr, 0.05, 0.5},
        {"gravity_m_s2", &PhysicsParams::gravity_m_s2, nullptr, 9.0, 10.0},
        // A slide lasts 2 |u| / (7 mu_s g): at 1e-3 a hard screw slides for ten minutes, and 87 of
        // 5,000 shots ran into time_cap_s. 0.05 is a quarter of the lowest measured (0.178).
        {"slide_friction", &PhysicsParams::slide_friction, nullptr, 0.05, 2.0},
        // At 1e-4, 220 of 5,000 shots were still rolling at time_cap_s. A fifth of the fastest
        // published cloth (0.005).
        {"roll_friction", &PhysicsParams::roll_friction, nullptr, 1e-3, 0.1},
        // At 0 side spin never stops and every side-spin shot runs into time_cap_s; 0.2 is
        // Alciatore's slowest spin decay (5 rad/s^2), at which the hardest side dies in 93 s.
        {"spin_friction_per_r", &PhysicsParams::spin_friction_per_r, nullptr, 0.2, 10.0},
        // Packs and clusters collapse inelastically below about 0.75 (a zero-time chain or the
        // event guard in 1 of 5,000 shots at 0.7, 7 of 2,000 at 0); published balls are 0.92-0.98.
        {"ball_restitution", &PhysicsParams::ball_restitution, nullptr, 0.8, 1.0},
        {"ball_friction_a", &PhysicsParams::ball_friction_a, nullptr, 0.0, 1.0},
        {"ball_friction_b", &PhysicsParams::ball_friction_b, nullptr, 0.0, 1.0},
        {"ball_friction_c", &PhysicsParams::ball_friction_c, nullptr, 0.0, 100.0},
        // 0 turns the push off and inelastic collapse comes back (52 of 2,000 shots tripped a
        // guard).
        {"ball_rest_speed_m_s", &PhysicsParams::ball_rest_speed_m_s, nullptr, 1e-4, 0.05},
        {"cushion_restitution", &PhysicsParams::cushion_restitution, nullptr, 0.0, 1.0},
        // Both cushion contacts can stick and wedge the ball once the contact height reaches
        // 2R / (1 + mu^2), mu the lower of the two frictions; with every mu <= 0.25 that stays above
        // 0.0494 m, clear of the 0.045 cap below (0 fallbacks in 140k impacts per friction pair). At
        // 0.3 fallbacks appear, and at mu >= 1 every height wedges.
        {"cushion_friction", &PhysicsParams::cushion_friction, nullptr, 0.0, 0.25},
        {"cushion_table_friction", &PhysicsParams::cushion_table_friction, nullptr, 0.0, 0.25},
        // The impact solver starts giving up as the contact nears the top of the ball (2R): on 1
        // of 20,000 random impacts at 0.049, on 8% at 0.052. 0.045 is sin(theta) 0.71.
        {"cushion_contact_height_m", &PhysicsParams::cushion_contact_height_m, nullptr, 0.02625, 0.045},
        {"cushion_rigid_limit_m_s", &PhysicsParams::cushion_rigid_limit_m_s, nullptr, 0.1, 20.0},
        {"cushion_restitution_slope_per_m_s", &PhysicsParams::cushion_restitution_slope_per_m_s, nullptr, 0.0, 1.0},
        {"cushion_restitution_min", &PhysicsParams::cushion_restitution_min, nullptr, 0.0, 1.0},
        {"jaw_restitution", &PhysicsParams::jaw_restitution, nullptr, 0.0, 1.0},
        // The jaw uses the same cushion solve, so the same wedging bound.
        {"jaw_friction", &PhysicsParams::jaw_friction, nullptr, 0.0, 0.25},
        {"cue_mass_kg", &PhysicsParams::cue_mass_kg, nullptr, 0.05, 2.0},
        {"cue_tip_restitution", &PhysicsParams::cue_tip_restitution, nullptr, 0.0, 1.0},
        // A tip and ferrule weigh grams; at 0.1 g full side squirts 0.05 degrees.
        {"cue_end_mass_kg", &PhysicsParams::cue_end_mass_kg, nullptr, 1e-4, 0.5},
        {"miscue_limit", &PhysicsParams::miscue_limit, nullptr, 0.0, 1.0},
        {"miscue_speed_factor", &PhysicsParams::miscue_speed_factor, nullptr, 0.0, 1.0},
        {"miscue_max_angle_rad", &PhysicsParams::miscue_max_angle_rad, nullptr, 0.0, 0.5},
        {"cue_tip_diameter_m", &PhysicsParams::cue_tip_diameter_m, nullptr, 0.005, 0.02},
        {"cue_butt_diameter_m", &PhysicsParams::cue_butt_diameter_m, nullptr, 0.01, 0.05},
        {"cue_length_m", &PhysicsParams::cue_length_m, nullptr, 0.5, 2.5},
        {"cue_clearance_m", &PhysicsParams::cue_clearance_m, nullptr, 0.0, 0.02},
        {"max_elevation_rad", &PhysicsParams::max_elevation_rad, nullptr, 0.0, 1.5707963267948966},
        {"max_cue_speed_m_s", &PhysicsParams::max_cue_speed_m_s, nullptr, 0.5, 20.0},
        {"nap_tick_ratio", &PhysicsParams::nap_tick_ratio, nullptr, 0.05, 0.95},
        {"nap_lateral_coeff", &PhysicsParams::nap_lateral_coeff, nullptr, 0.0, 0.01},
        {"nap_spin_coeff", &PhysicsParams::nap_spin_coeff, nullptr, 0.0, 0.01},
        {"nap_speed_ref_m_s", &PhysicsParams::nap_speed_ref_m_s, nullptr, 1e-3, 5.0},
        {"nap_speed_max_m_s", &PhysicsParams::nap_speed_max_m_s, nullptr, 0.0, 20.0},
        {"nap_speed_min_m_s", &PhysicsParams::nap_speed_min_m_s, nullptr, 1e-4, 1.0},
        {"rack_gap_min_m", &PhysicsParams::rack_gap_min_m, nullptr, 0.0, 1e-3},
        {"rack_gap_max_m", &PhysicsParams::rack_gap_max_m, nullptr, 0.0, 1e-3},
        {"time_cap_s", &PhysicsParams::time_cap_s, nullptr, 1.0, 3600.0},
        // An event costs a few microseconds and a few hundred bytes of record: 1e6 holds a
        // runaway shot to seconds and some hundreds of megabytes, where 1e7 could run a phone out
        // of memory.
        {"max_events", nullptr, &PhysicsParams::max_events, 100.0, 1e6},
        // Events this close are resolved in kind order, so a later one can go first and leave its
        // closing speed times zero_time_s of overlap, which must stay under the simulator's 1e-7 m:
        // 2e-9 overlapped in 1 of 40,000 shots at max_cue_speed_m_s 20, 5e-9 in 3 at the default.
        {"zero_time_s", &PhysicsParams::zero_time_s, nullptr, 0.0, 1e-9},
        {"max_zero_time_chain", nullptr, &PhysicsParams::max_zero_time_chain, 2.0, 1e6},
    };
    return all;
}

// Appends a problem for `key` when `value` can't go into `field`; otherwise stores it.
bool apply(const Field& field, const nlohmann::json& value, PhysicsParams& out, std::string& problem) {
    if (!value.is_number()) {
        problem = "not a number";
        return false;
    }
    const double v = value.get<double>();
    if (!std::isfinite(v)) {
        problem = "not finite";
        return false;
    }
    if (field.integer != nullptr && (!value.is_number_integer() || v != std::floor(v))) {
        problem = "not an integer";
        return false;
    }
    if (v < field.min || v > field.max) {
        problem = "outside [" + nlohmann::json(field.min).dump() + ", " + nlohmann::json(field.max).dump() + "]";
        return false;
    }
    if (field.integer != nullptr) {
        out.*field.integer = value.get<int>();
    } else {
        out.*field.real = v;
    }
    return true;
}

bool read_values(const nlohmann::json& values, PhysicsParams& out, std::vector<std::string>& problems,
                 bool log_problems) {
    const std::size_t before = problems.size();
    auto report = [&](const std::string& key, const nlohmann::json& value, const std::string& reason) {
        problems.push_back(key + ": " + reason);
        if (log_problems) {
            log::error("params", "params.invalid", {{"key", key}, {"value", value}, {"reason", reason}});
        }
    };
    if (!values.is_object()) {
        report("values", values, "not an object");
        return false;
    }
    for (const Field& field : fields()) {
        const auto it = values.find(field.name);
        if (it == values.end()) {
            report(field.name, nullptr, "missing");
            continue;
        }
        std::string problem;
        if (!apply(field, *it, out, problem)) report(field.name, *it, problem);
    }
    for (const auto& [key, value] : values.items()) {
        bool known = false;
        for (const Field& field : fields()) known = known || key == field.name;
        if (!known) report(key, value, "unknown key");
    }
    if (out.rack_gap_min_m > out.rack_gap_max_m) {
        report("rack_gap_min_m", out.rack_gap_min_m, "greater than rack_gap_max_m");
    }
    // The obstruction takes the butt as the cue's widest point.
    if (out.cue_tip_diameter_m > out.cue_butt_diameter_m) {
        report("cue_tip_diameter_m", out.cue_tip_diameter_m, "greater than cue_butt_diameter_m");
    }
    return problems.size() == before;
}

}  // namespace

const std::vector<std::string>& physics_param_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const Field& field : fields()) out.emplace_back(field.name);
        return out;
    }();
    return names;
}

bool load_physics_params(const nlohmann::json& file, PhysicsParams& out, std::vector<std::string>& problems) {
    if (!file.is_object() || !file.contains("values")) {
        problems.emplace_back("values: missing");
        log::error("params", "params.invalid", {{"key", "values"}, {"value", nullptr}, {"reason", "missing"}});
        return false;
    }
    return read_values(file["values"], out, problems, true);
}

nlohmann::json physics_params_values(const PhysicsParams& p) {
    nlohmann::json values = nlohmann::json::object();
    for (const Field& field : fields()) {
        if (field.integer != nullptr) {
            values[field.name] = p.*field.integer;
        } else {
            values[field.name] = p.*field.real;
        }
    }
    return values;
}

bool physics_params_from_values(const nlohmann::json& values, PhysicsParams& out,
                                std::vector<std::string>& problems) {
    return read_values(values, out, problems, false);
}

}  // namespace ofs::phys
