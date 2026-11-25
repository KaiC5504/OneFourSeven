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
#include "ofs/table_geometry.hpp"

#include "choice.hpp"

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

// The frame travels in the message, as in solve.cpp.
void option_failure_logged(const std::exception& e, const rules::FrameState& s, int player, const Level& level) {
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

// The 147 with a pot of any ball it may play, from the position it would play: never conceded.
bool has_a_pot(const rules::FrameState& s, int player, const Level& level, const Env& env) {
    if (s.pending == rules::Pending::after_foul && s.offender != player) return has_its_pot(s, player, level, env);
    return s.pending == rules::Pending::none && s.striker == player && !pots(s, env, level.max_cut_rad).empty();
}

}  // namespace

int snookers_needed(const rules::FrameState& s, int player) {
    const int required = rules::penalty_points_required(s, player);
    if (required <= 0) return 0;
    // A foul brings at least 4, the ball on's value when that is more (S3.10). With red or a
    // colour of choice on, the least a snooker brings is 4.
    const int penalty = s.on.kind == rules::OnKind::colour ? std::max(4, table::ball_value(s.on.colour)) : 4;
    return (required + penalty - 1) / penalty;
}

bool wants_to_concede(const rules::FrameState& s, int player, const Level& level) {
    return level.concede_snookers > 0 && snookers_needed(s, player) >= level.concede_snookers;
}

rules::DecisionKind choose_option(const rules::FrameState& s, int player,
                                  const std::vector<rules::DecisionKind>& options, const Level& level,
                                  const Env& env, Rng& rng, nlohmann::json* why) {
    if (std::find(options.begin(), options.end(), rules::DecisionKind::concede_frame) != options.end() &&
        wants_to_concede(s, player, level)) {
        bool keeps_on = false;
        try {
            keeps_on = level.maximum && has_a_pot(s, player, level, env);
        } catch (const std::exception& e) {
            // It plays on rather than concede on a reading that failed.
            option_failure_logged(e, s, player, level);
            keeps_on = true;
        }
        if (!keeps_on) {
            if (why != nullptr) {
                *why = {{"level", level.id}, {"kind", "concede"}, {"snookers", snookers_needed(s, player)}};
            }
            return rules::DecisionKind::concede_frame;
        }
    }
    rules::DecisionKind chosen = choose_decision(s, player, options, env.standin, rng, why);
    bool its_pot = false;
    const bool may_play = std::find(options.begin(), options.end(), rules::DecisionKind::play) != options.end();
    if (level.maximum && may_play && s.pending == rules::Pending::after_foul && s.offender != player) {
        try {
            its_pot = has_its_pot(s, player, level, env);
        } catch (const std::exception& e) {
            // The stand-in's choice stands.
            option_failure_logged(e, s, player, level);
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
