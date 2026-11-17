#include "ofs/opponent/level.hpp"

#include <cmath>

#include "ofs/log.hpp"

namespace ofs::opponent {

namespace {

struct Field {
    const char* name;
    double Level::* real = nullptr;
    int Level::* integer = nullptr;
    // Inclusive bounds; a value outside them is rejected rather than clamped.
    double min = 0.0;
    double max = 0.0;
};

const std::vector<Field>& fields() {
    static const std::vector<Field> all = {
        {"tier", nullptr, &Level::tier, 0.0, 5.0},
        // 0.1 rad is six degrees: past it no ball goes where it was meant to.
        {"aim_sigma_rad", &Level::aim_sigma_rad, nullptr, 0.0, 0.1},
        {"aim_sigma_per_cut", &Level::aim_sigma_per_cut, nullptr, 0.0, 20.0},
        {"aim_sigma_per_m", &Level::aim_sigma_per_m, nullptr, 0.0, 5.0},
        {"awkward_factor", &Level::awkward_factor, nullptr, 1.0, 10.0},
        {"speed_sigma", &Level::speed_sigma, nullptr, 0.0, 0.5},
        {"tip_sigma", &Level::tip_sigma, nullptr, 0.0, 0.3},
        {"max_cut_rad", &Level::max_cut_rad, nullptr, 0.0, 1.5707963267948966},
        // The game's spin ball stops at 0.98 of the miscue limit (0.55): nothing is played past it.
        {"max_tip", &Level::max_tip, nullptr, 0.0, 0.53},
        {"speeds", nullptr, &Level::speeds, 1.0, 16.0},
        {"spins", nullptr, &Level::spins, 1.0, 25.0},
        {"depth", nullptr, &Level::depth, 0.0, 3.0},
        {"position_weight", &Level::position_weight, nullptr, 0.0, 1.0},
        {"judgement_sigma", &Level::judgement_sigma, nullptr, 0.0, 2.0},
        {"safety_below", &Level::safety_below, nullptr, 0.0, 1.0},
        // A simulation is tens of microseconds: a million is a minute of thinking.
        {"max_sims", nullptr, &Level::max_sims, 1.0, 1000000.0},
        {"tactics", nullptr, &Level::tactics, 0.0, 3.0},
        {"safety_tries", nullptr, &Level::safety_tries, 0.0, 1000.0},
        {"escape_cushions", nullptr, &Level::escape_cushions, 0.0, 2.0},
        {"miss_weight", &Level::miss_weight, nullptr, 0.0, 1.0},
        {"noisy_sims", nullptr, &Level::noisy_sims, 0.0, 64.0},
        {"score_aware", &Level::score_aware, nullptr, 0.0, 1.0},
        {"concede_snookers", nullptr, &Level::concede_snookers, 0.0, 10.0},
    };
    return all;
}

bool apply(const Field& field, const nlohmann::json& value, Level& out, std::string& problem) {
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

}  // namespace

const std::vector<std::string>& level_value_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out = {"name"};
        for (const Field& field : fields()) out.emplace_back(field.name);
        out.emplace_back("maximum");
        return out;
    }();
    return names;
}

bool load_level(const nlohmann::json& file, Level& out, std::vector<std::string>& problems) {
    const std::size_t before = problems.size();
    const auto report = [&](const std::string& key, const nlohmann::json& value, const std::string& reason) {
        problems.push_back(key + ": " + reason);
        log::error("params", "params.invalid", {{"key", key}, {"value", value}, {"reason", reason}});
    };
    if (!file.is_object() || !file.contains("values") || !file["values"].is_object()) {
        report("values", nullptr, "missing or not an object");
        return false;
    }
    Level level;
    const auto id = file.find("id");
    if (id == file.end() || !id->is_string() || id->get<std::string>().empty()) {
        report("id", id == file.end() ? nlohmann::json() : *id, "missing or not a name");
    } else {
        level.id = id->get<std::string>();
    }
    const nlohmann::json& values = file["values"];
    const auto name = values.find("name");
    if (name == values.end() || !name->is_string() || name->get<std::string>().empty()) {
        report("name", name == values.end() ? nlohmann::json() : *name, "missing or not a name");
    } else {
        level.name = name->get<std::string>();
    }
    const auto maximum = values.find("maximum");
    if (maximum == values.end() || !maximum->is_boolean()) {
        report("maximum", maximum == values.end() ? nlohmann::json() : *maximum, "missing or not true or false");
    } else {
        level.maximum = maximum->get<bool>();
    }
    for (const Field& field : fields()) {
        const auto it = values.find(field.name);
        if (it == values.end()) {
            report(field.name, nullptr, "missing");
            continue;
        }
        std::string problem;
        if (!apply(field, *it, level, problem)) report(field.name, *it, problem);
    }
    for (auto it = values.begin(); it != values.end(); ++it) {
        bool known = false;
        for (const std::string& known_name : level_value_names()) known = known || known_name == it.key();
        if (!known) report(it.key(), it.value(), "unknown");
    }
    if (problems.size() != before) return false;
    out = level;
    return true;
}

nlohmann::json level_json(const Level& level) {
    nlohmann::json out = {{"id", level.id}, {"name", level.name}, {"maximum", level.maximum}};
    for (const Field& field : fields()) {
        if (field.integer != nullptr) {
            out[field.name] = level.*field.integer;
        } else {
            out[field.name] = level.*field.real;
        }
    }
    return out;
}

}  // namespace ofs::opponent
