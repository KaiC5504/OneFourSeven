// Contract stub (M3): replaced by the builder of the referee's eyes.

#include "ofs/rules/referee_params.hpp"

namespace ofs::rules {

const std::vector<std::string>& ref_param_names() {
    static const std::vector<std::string> names;
    return names;
}

bool load_ref_params(const nlohmann::json&, RefParams& out, std::vector<std::string>&) {
    out = RefParams{};
    return true;
}

nlohmann::json ref_params_values(const RefParams&) { return nlohmann::json::object(); }

bool ref_params_from_values(const nlohmann::json&, RefParams& out, std::vector<std::string>&) {
    out = RefParams{};
    return true;
}

}  // namespace ofs::rules
