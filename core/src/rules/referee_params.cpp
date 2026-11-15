#include "ofs/rules/referee_params.hpp"

#include <cmath>

#include "ofs/log.hpp"

namespace ofs::rules {

namespace {

struct Field {
    const char* name;
    double RefParams::* real = nullptr;
    int RefParams::* integer = nullptr;
    // Inclusive bounds; a value outside them is rejected rather than clamped.
    double min = 0.0;
    double max = 0.0;
};

// Bounds keep each threshold where the rule it stands for still means something, and the oracle's
// searches affordable; inside them the value is the tuner's call.
const std::vector<Field>& fields() {
    static const std::vector<Field> all = {
        // Under the simulator's overlap tolerance (1e-9 m) rounding would decide which balls touch;
        // a gap of a millimetre is plain to see and nobody would call it touching.
        {"touching_gap_m", &RefParams::touching_gap_m, nullptr, 1e-9, 1e-3},
        // 0 takes only contacts at the very same instant as one impact. A millisecond apart the
        // cue ball has already left the first ball by millimetres.
        {"simultaneous_hit_s", &RefParams::simultaneous_hit_s, nullptr, 0.0, 1e-3},
        // 0 turns the test off. A tip is on the ball for about a millisecond; ten is a ball some
        // centimetres away, an ordinary stroke.
        {"push_contact_time_s", &RefParams::push_contact_time_s, nullptr, 0.0, 0.01},
        // A cut is never past 90 degrees: at pi/2 no contact counts as a fine edge.
        {"push_fine_edge_rad", &RefParams::push_fine_edge_rad, nullptr, 0.0, 1.5707963267948966},
        // Two ball widths: further off than that the attempt was not close by any standard.
        {"miss_lenient_gap_m", &RefParams::miss_lenient_gap_m, nullptr, 0.0, 0.105},
        // Each further cushion multiplies the routes the oracle tries, and no referee expects an
        // escape off more than three.
        {"escape_max_cushions", nullptr, &RefParams::escape_max_cushions, 0.0, 3.0},
        // 0 counts only pots. At a metre every ball on the table is near some pocket.
        {"pot_attempt_radius_m", &RefParams::pot_attempt_radius_m, nullptr, 0.0, 1.0},
        // 0 calls no stroke quiet, which turns the re-rack offer off.
        {"stalemate_quiet_travel_m", &RefParams::stalemate_quiet_travel_m, nullptr, 0.0, 10.0},
        // Both players must have played a quiet stroke before anyone is asked.
        {"stalemate_offer_strokes", nullptr, &RefParams::stalemate_offer_strokes, 2.0, 1000.0},
        // At 0 a refused offer would re-rack the frame at once.
        {"stalemate_grace_strokes", nullptr, &RefParams::stalemate_grace_strokes, 1.0, 100.0},
        // The D holds about 0.134 / step^2 points: 33,000 at 2 mm, each tested against every ball
        // on. Past 50 mm, a ball's width, a place the cue ball could hit from slips between points.
        {"d_sample_step_m", &RefParams::d_sample_step_m, nullptr, 0.002, 0.05},
    };
    return all;
}

// Appends a problem for `key` when `value` can't go into `field`; otherwise stores it.
bool apply(const Field& field, const nlohmann::json& value, RefParams& out, std::string& problem) {
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

bool read_values(const nlohmann::json& values, RefParams& out, std::vector<std::string>& problems,
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
    return problems.size() == before;
}

}  // namespace

const std::vector<std::string>& ref_param_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const Field& field : fields()) out.emplace_back(field.name);
        return out;
    }();
    return names;
}

bool load_ref_params(const nlohmann::json& file, RefParams& out, std::vector<std::string>& problems) {
    if (!file.is_object() || !file.contains("values")) {
        problems.emplace_back("values: missing");
        log::error("params", "params.invalid", {{"key", "values"}, {"value", nullptr}, {"reason", "missing"}});
        return false;
    }
    return read_values(file["values"], out, problems, true);
}

nlohmann::json ref_params_values(const RefParams& p) {
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

bool ref_params_from_values(const nlohmann::json& values, RefParams& out, std::vector<std::string>& problems) {
    return read_values(values, out, problems, false);
}

}  // namespace ofs::rules
