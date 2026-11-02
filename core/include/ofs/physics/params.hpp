#pragma once

// The tunable physics constants (spec 7.10). core/data/table_params.json holds one value per
// field under "values", keyed by the field name, and a unit, source and note per key under
// "docs". SI units throughout; angles in radians; "fraction of R" means a ball radius.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ofs::phys {

struct PhysicsParams {
    // Ball and cloth.
    double ball_mass_kg = 0.141;
    double gravity_m_s2 = 9.81;
    double slide_friction = 0.2;        // mu_s, ball-cloth while the contact point slips
    double roll_friction = 0.008;       // mu_r, rolling resistance
    double spin_friction_per_r = 0.4444444444444444;  // mu_sp / R (pooltool u_sp_proportionality)

    // Ball-ball (spec 7.4): normal restitution, and contact friction
    // mu = a + b * exp(-c * v_rel) with v_rel the sliding speed at the contact in m/s.
    double ball_restitution = 0.94;
    double ball_friction_a = 9.951e-3;
    double ball_friction_b = 0.108;
    double ball_friction_c = 1.088;

    // Cushions and jaws (spec 7.5, Mathavan et al. 2010).
    double cushion_restitution = 0.98;
    double cushion_friction = 0.14;        // mu_w, ball-cushion
    double cushion_table_friction = 0.212;  // mu_s, ball-cloth during the impact
    double cushion_contact_height_m = 0.0333;  // height of the cushion nose's contact above the cloth
    double jaw_restitution = 0.8;
    double jaw_friction = 0.14;

    // Cue (spec 7.3).
    double cue_mass_kg = 0.54;
    double cue_tip_restitution = 0.75;
    double cue_end_mass_kg = 0.0075;  // drives squirt
    double miscue_limit = 0.55;        // tip offset, fraction of R
    double miscue_speed_factor = 0.35;  // fraction of the cue speed a miscued ball keeps, at most
    double miscue_max_angle_rad = 0.05;
    double cue_tip_diameter_m = 0.0095;
    double cue_butt_diameter_m = 0.029;
    double cue_length_m = 1.45;
    double cue_clearance_m = 0.002;  // margin the cue keeps over a rail or ball
    double max_elevation_rad = 1.3962634015954636;  // 80 degrees
    double max_cue_speed_m_s = 10.0;

    // Nap (spec 7.8). A rolling ball's nap acceleration is held constant until its speed falls
    // by nap_tick_ratio, then re-evaluated (nap.hpp).
    double nap_tick_ratio = 0.5;
    double nap_lateral_coeff = 1.2e-4;  // times g, sideways drift at nap_speed_ref_m_s
    double nap_spin_coeff = 2.0e-4;     // times g, side-spin interaction
    double nap_speed_ref_m_s = 0.3;
    double nap_speed_max_m_s = 1.5;     // no nap effect above this speed
    double nap_speed_min_m_s = 0.02;    // no further re-evaluation below this speed

    // Break-off and the simulator (spec 7.7).
    double rack_gap_min_m = 1.0e-5;
    double rack_gap_max_m = 1.0e-4;
    double time_cap_s = 120.0;
    int max_events = 50000;
    double zero_time_s = 1.0e-9;    // events closer than this count as simultaneous
    int max_zero_time_chain = 100;  // simultaneous events in a row before the guard trips
};

// Every field of PhysicsParams by name, in declaration order. Loading, serialising and the
// docs check all walk this list, so a field missing from it is a test failure.
const std::vector<std::string>& physics_param_names();

// Reads the "values" object of table_params.json. Unknown keys, missing keys, non-numbers,
// non-finite numbers, non-integral values for the integer fields and values outside each
// field's valid range are problems: each is logged as params.invalid (error) and appended to
// `problems`, and that field keeps its default. Returns true when there were none.
bool load_physics_params(const nlohmann::json& file, PhysicsParams& out, std::vector<std::string>& problems);

// {"name": value, ...} for every field: the "params" of a repro bundle.
nlohmann::json physics_params_values(const PhysicsParams& p);
// The inverse, strict: any problem makes it false and is appended to `problems`.
bool physics_params_from_values(const nlohmann::json& values, PhysicsParams& out,
                                std::vector<std::string>& problems);

}  // namespace ofs::phys
