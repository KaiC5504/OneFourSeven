#include "ofs/opponent/opponent.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <string>

#include "ofs/log.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/rules/engine.hpp"
#include "ofs/rules/rules_log.hpp"
#include "ofs/rules/serialize.hpp"

namespace ofs::opponent {

namespace {

// After the other player's foul: the position the 147 would play from has a pot of the ball its
// maximum needs or, with the maximum gone, any pot.
bool has_its_pot(const rules::FrameState& s, int player, const Level& level, const Env& env) {
    rules::FrameState left = s;
    std::vector<rules::Call> calls;
    std::string error;
    if (!rules::decide(left, {rules::DecisionKind::play, player}, env.oracle, env.ref, calls, error)) return false;
    if (left.over || left.pending != rules::Pending::none || left.striker != player) return false;
    const bool on = maximum_on(left);
    const rules::Ball needed = maximum_next(left);
    for (const Pot& pot : pots(left, env, level.max_cut_rad)) {
        if (!on || left.balls[std::size_t(pot.target)].ball == needed) return true;
    }
    return false;
}

}  // namespace

rules::DecisionKind choose_option(const rules::FrameState& s, int player,
                                  const std::vector<rules::DecisionKind>& options, const Level& level,
                                  const Env& env, Rng& rng, nlohmann::json* why) {
    rules::DecisionKind chosen = choose_decision(s, player, options, env.standin, rng, why);
    bool its_pot = false;
    const bool may_play = std::find(options.begin(), options.end(), rules::DecisionKind::play) != options.end();
    if (level.maximum && may_play && s.pending == rules::Pending::after_foul && s.offender != player) {
        try {
            its_pot = has_its_pot(s, player, level, env);
        } catch (const std::exception& e) {
            // The stand-in's choice stands; the frame travels in the message, as in solve.cpp.
            nlohmann::json data = {{"kind", "exception"},
                                   {"function", "opponent::choose_option"},
                                   {"message", std::string(e.what()) + "; level " + level.id + "; player " +
                                                   std::to_string(player) + "; frame " + rules::frame_json(s).dump()}};
            const nlohmann::json& context = rules::scoped_log_context();
            for (const char* key : {"match_id", "frame"}) {
                if (context.contains(key)) data[key] = context[key];
            }
            log::error("app", "app.error", std::move(data));
        }
        if (its_pot) chosen = rules::DecisionKind::play;
    }
    if (why != nullptr) {
        if (!why->is_object()) *why = nlohmann::json::object();
        (*why)["level"] = level.id;
        if (its_pot) {
            (*why)["rule"] = "play";
            (*why)["pot"] = true;
        }
    }
    return chosen;
}

}  // namespace ofs::opponent
