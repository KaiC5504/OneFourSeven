#include "ofs/opponent/opponent.hpp"

#include "ofs/opponent/standin.hpp"

namespace ofs::opponent {

rules::DecisionKind choose_option(const rules::FrameState& s, int player,
                                  const std::vector<rules::DecisionKind>& options, const Level& level,
                                  const Env& env, Rng& rng, nlohmann::json* why) {
    const rules::DecisionKind chosen = choose_decision(s, player, options, env.standin, rng, why);
    if (why != nullptr) {
        if (!why->is_object()) *why = nlohmann::json::object();
        (*why)["level"] = level.id;
    }
    return chosen;
}

}  // namespace ofs::opponent
