// Contract stub (M3): replaced by the stand-in opponent's builder.

#include "ofs/opponent/standin.hpp"

namespace ofs::opponent {

const std::vector<std::string>& standin_param_names() {
    static const std::vector<std::string> names;
    return names;
}

bool load_standin_params(const nlohmann::json&, StandinParams& out, std::vector<std::string>&) {
    out = StandinParams{};
    return true;
}

StandinShot choose_shot(const rules::FrameState&, const phys::TableShape&, const phys::PhysicsParams&,
                        const rules::TableOracle&, const rules::RefParams&, const StandinParams&, Rng&) {
    StandinShot shot;
    shot.strike.speed_m_s = 1.0;
    shot.why = nlohmann::json::object();
    return shot;
}

rules::DecisionKind choose_decision(const rules::FrameState&, int, const std::vector<rules::DecisionKind>& options,
                                    const StandinParams&, Rng&) {
    return options.empty() ? rules::DecisionKind::play : options.front();
}

}  // namespace ofs::opponent
