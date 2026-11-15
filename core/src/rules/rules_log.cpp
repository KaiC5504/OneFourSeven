#include "ofs/rules/rules_log.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <string_view>
#include <utility>

#include "ofs/log.hpp"
#include "ofs/physics/serialize.hpp"
#include "ofs/rules/oracle.hpp"
#include "ofs/rules/serialize.hpp"

namespace ofs::rules {

namespace {

using nlohmann::json;

constexpr std::array<const char*, 9> kJudgementKeys = {
    "fouls", "penalty", "miss", "miss_reason", "points", "nominated", "free_ball_used", "spotted", "calls"};
constexpr std::array<const char*, 10> kAfterKeys = {
    "striker", "scores", "on", "free_ball", "cue_in_hand", "pending", "decider", "over", "winner", "end_reason"};

thread_local json t_log_context = json::object();

// The caller's context beside the line's own fields, which win over a context key of the same name.
json with_context(const json& context, json fields) {
    if (context.is_object()) {
        for (auto it = context.begin(); it != context.end(); ++it) {
            if (!fields.contains(it.key())) fields[it.key()] = it.value();
        }
    }
    return fields;
}

// What a reader of the log wants to know about the frame after a decision without the whole state.
json after_json(const FrameState& s) {
    const json frame = frame_json(s);
    json after = json::object();
    for (const char* key : kAfterKeys) after[key] = frame.at(key);
    return after;
}

json judgement_without_post(const Judgement& j) {
    json out = judgement_json(j);
    out.erase("post");
    return out;
}

std::string shown(const json& v) {
    std::string text = v.dump(-1, ' ', false, json::error_handler_t::replace);
    if (text.size() > 80) text = text.substr(0, 77) + "...";
    return text;
}

// "" when the two are the same value, else where they first differ and how.
std::string first_difference(const json& logged, const json& now, const std::string& where) {
    if (logged.is_object() && now.is_object()) {
        for (auto it = logged.begin(); it != logged.end(); ++it) {
            const std::string here = where + "." + it.key();
            const auto other = now.find(it.key());
            if (other == now.end()) return here + ": logged " + shown(it.value()) + ", absent when re-judged";
            const std::string difference = first_difference(it.value(), *other, here);
            if (!difference.empty()) return difference;
        }
        for (auto it = now.begin(); it != now.end(); ++it) {
            if (!logged.contains(it.key())) {
                return where + "." + it.key() + ": not logged, re-judged " + shown(it.value());
            }
        }
        return "";
    }
    if (logged.is_array() && now.is_array()) {
        for (std::size_t i = 0; i < std::min(logged.size(), now.size()); ++i) {
            const std::string difference = first_difference(logged[i], now[i], where + "[" + std::to_string(i) + "]");
            if (!difference.empty()) return difference;
        }
        if (logged.size() != now.size()) {
            return where + ": logged " + std::to_string(logged.size()) + " entries, re-judged " +
                   std::to_string(now.size());
        }
        return "";
    }
    if (logged == now) return "";
    return where + ": logged " + shown(logged) + ", re-judged " + shown(now);
}

// The keys a person reads first, in that order, then anything else that differs.
template <std::size_t N>
std::string first_difference_by(const std::array<const char*, N>& keys, const json& logged, const json& now,
                                const std::string& where) {
    for (const char* key : keys) {
        const std::string here = where + "." + key;
        const auto it = logged.find(key);
        if (it == logged.end()) return here + ": missing from the line";
        const std::string difference = first_difference(*it, now.at(key), here);
        if (!difference.empty()) return difference;
    }
    return first_difference(logged, now, where);
}

}  // namespace

void log_decision(const FrameState& pre, const ShotInput& input, const ShotFacts& facts, const Judgement& j,
                  const RefParams& ref, const nlohmann::json& context) {
    log::info("rules", "rules.decision",
              with_context(context, {{"pre", frame_json(pre)},
                                     {"input", input_json(input)},
                                     {"facts", facts_json(facts)},
                                     {"judgement", judgement_without_post(j)},
                                     {"after", after_json(j.post)},
                                     {"post_hash", frame_hash(j.post)},
                                     {"ref", ref_params_values(ref)}}));
}

void log_choice(const FrameState& before, Decision d, bool ok, const std::string& error, const FrameState& after,
                const std::vector<Call>& calls, const nlohmann::json& context) {
    json offered = json::array();
    // A refused choice may name a player who does not exist: nothing was offered to them.
    if (d.player == 0 || d.player == 1) {
        for (const DecisionKind k : options(before, d.player)) offered.push_back(json(decision_name(k)));
    }
    log::Logger::instance().emit("rules", ok ? log::Level::info : log::Level::warn, "rules.choice",
                                 with_context(context, {{"before", frame_json(before)},
                                                        {"player", d.player},
                                                        {"kind", decision_name(d.kind)},
                                                        {"ok", ok},
                                                        {"error", error},
                                                        {"options", std::move(offered)},
                                                        {"calls", calls_json(calls)},
                                                        {"after", after_json(after)},
                                                        {"post_hash", frame_hash(after)}}));
}

void log_cue_placed(const FrameState& s, Vec2 pos, bool ok, const std::string& error, const nlohmann::json& context) {
    log::Logger::instance().emit("rules", ok ? log::Level::info : log::Level::warn, "rules.cue_placed",
                                 with_context(context, {{"player", s.striker},
                                                        {"x", phys::number_to_json(pos.x)},
                                                        {"y", phys::number_to_json(pos.y)},
                                                        {"ok", ok},
                                                        {"error", error}}));
}

void log_match_started(const MatchState& m, const nlohmann::json& context) {
    log::info("match", "match.started",
              with_context(context, {{"config", config_json(m.config)}, {"breaker", m.breaker}}));
}

void log_frame_started(const MatchState& m, const nlohmann::json& context) {
    log::info("match", "frame.started",
              with_context(context, {{"number", m.frame_number},
                                     {"breaker", m.breaker},
                                     {"seed", std::to_string(m.frame.seed)},
                                     {"reracks", m.reracks}}));
}

void log_frame_ended(const MatchState& m, const nlohmann::json& context) {
    if (m.history.empty()) {
        log_invalid("frame.ended", "asked to log the end of frame " + std::to_string(m.frame_number) +
                                       " of a match with no finished frame");
        return;
    }
    log::info("match", "frame.ended", with_context(context, {{"summary", summary_json(m.history.back())}}));
}

void log_match_ended(const MatchState& m, const std::string& reason, const nlohmann::json& context) {
    log::info("match", "match.ended",
              with_context(context, {{"winner", m.winner},
                                     {"frames", json::array({m.frames[0], m.frames[1]})},
                                     {"frames_played", m.history.size()},
                                     {"reason", reason}}));
}

void log_invalid(const std::string& what, const std::string& detail) {
    log::warn("rules", "rules.invalid", with_context(t_log_context, {{"what", what}, {"detail", detail}}));
}

ScopedLogContext::ScopedLogContext(const nlohmann::json& context) : previous_(t_log_context) {
    if (!context.is_object()) return;
    for (auto it = context.begin(); it != context.end(); ++it) t_log_context[it.key()] = it.value();
}

ScopedLogContext::~ScopedLogContext() { t_log_context = std::move(previous_); }

const nlohmann::json& scoped_log_context() { return t_log_context; }

bool logged_ref_params(const nlohmann::json& values, RefParams& out, std::vector<std::string>& problems) {
    if (values.is_object() && !values.contains("d_fine_step_m") && values.contains("d_sample_step_m")) {
        nlohmann::json filled = values;
        filled["d_fine_step_m"] = values["d_sample_step_m"];
        return ref_params_from_values(filled, out, problems);
    }
    return ref_params_from_values(values, out, problems);
}

std::string rejudge(const nlohmann::json& decision_data, const phys::TableShape& table) {
    if (!decision_data.is_object()) return "data: want an object";
    for (const char* key : {"pre", "input", "facts", "judgement", "post_hash", "ref"}) {
        const auto it = decision_data.find(key);
        if (it == decision_data.end()) return std::string(key) + ": missing from the line";
        const bool hash = std::string_view(key) == "post_hash";
        if (hash ? !it->is_string() : !it->is_object()) {
            return std::string(key) + (hash ? ": want a string" : ": want an object");
        }
    }
    const json& logged = decision_data["judgement"];

    FrameState pre;
    ShotInput input;
    ShotFacts facts;
    RefParams ref;
    std::string error;
    if (!frame_from_json(decision_data["pre"], pre, error)) return "pre." + error;
    if (!input_from_json(decision_data["input"], input, error)) return "input." + error;
    if (!facts_from_json(decision_data["facts"], facts, error)) return "facts." + error;
    std::vector<std::string> problems;
    if (!logged_ref_params(decision_data["ref"], ref, problems)) {
        return "ref: " + (problems.empty() ? std::string("refused") : problems.front());
    }
    if (facts.rest.size() != pre.balls.size()) {
        return "facts.rest: " + std::to_string(facts.rest.size()) + " entries for the " +
               std::to_string(pre.balls.size()) + " balls of pre";
    }

    const std::unique_ptr<TableOracle> oracle = make_geometry_oracle(table, ref);
    const Judgement now = judge(pre, input, facts, *oracle, ref);

    std::string difference = first_difference_by(kJudgementKeys, logged, judgement_without_post(now), "judgement");
    if (!difference.empty()) return difference;

    const std::string hash = frame_hash(now.post);
    const std::string logged_hash = decision_data["post_hash"].get<std::string>();
    if (hash == logged_hash) return "";
    // The hash says the frames differ, not where: `after`, when the line has it, usually does.
    const std::string hashes = "post_hash: logged " + logged_hash + ", re-judged " + hash;
    const auto after = decision_data.find("after");
    if (after == decision_data.end() || !after->is_object()) return hashes;
    difference = first_difference_by(kAfterKeys, *after, after_json(now.post), "after");
    if (!difference.empty()) return difference + " (" + hashes + ")";
    return hashes + "; the frame after differs outside \"after\"";
}

}  // namespace ofs::rules
