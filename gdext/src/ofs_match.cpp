#include "ofs_match.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <set>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/opponent/env.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/params.hpp"
#include "ofs/physics/serialize.hpp"
#include "ofs/rng.hpp"
#include "ofs/rules/engine.hpp"
#include "ofs/rules/facts.hpp"
#include "ofs/rules/rules_log.hpp"
#include "ofs/rules/serialize.hpp"

using namespace godot;

namespace {

using nlohmann::json;
namespace rules = ofs::rules;
namespace opponent = ofs::opponent;

// Bounds on a refused call's arguments as logged, so a runaway Dictionary can't flood the session.
constexpr int64_t kMaxLoggedItems = 32;
constexpr int kMaxLoggedDepth = 6;
// nearest_cue_placement is asked on every drag: a refusal held there is logged once, and only so
// many distinct ones in a session.
constexpr std::size_t kMaxRefusalsLogged = 64;
// facts.hpp: how far from its place in the frame a ball may start in the record.
constexpr double kStartTolerance_m = 1e-9;
// Whole numbers a double holds exactly.
constexpr double kMaxWholeDouble = 9007199254740992.0;

std::string to_std(const String &text) {
    const CharString utf8 = text.utf8();
    return std::string(utf8.get_data(), size_t(utf8.length()));
}

String from_std(std::string_view text) {
    return String::utf8(text.data(), int64_t(text.size()));
}

// Integer JSON stays int: scores, places and players index Arrays in the game.
Variant json_to_variant(const json &j) {
    switch (j.type()) {
        case json::value_t::boolean: return j.get<bool>();
        case json::value_t::number_integer: return j.get<int64_t>();
        case json::value_t::number_unsigned: return int64_t(j.get<uint64_t>());
        case json::value_t::number_float: return j.get<double>();
        case json::value_t::string: return from_std(j.get<std::string>());
        case json::value_t::array: {
            Array out;
            for (const json &item : j) out.push_back(json_to_variant(item));
            return out;
        }
        case json::value_t::object: {
            Dictionary out;
            for (const auto &[key, value] : j.items()) out[from_std(key)] = json_to_variant(value);
            return out;
        }
        default: return Variant();
    }
}

std::string variant_text(const Variant &v) {
    return to_std(v.get_type() == Variant::STRING || v.get_type() == Variant::STRING_NAME ? String(v)
                                                                                           : v.stringify());
}

// Any Variant as JSON, non-finite numbers and other types as their text.
json variant_json(const Variant &v, int depth) {
    switch (v.get_type()) {
        case Variant::NIL: return nullptr;
        case Variant::BOOL: return bool(v);
        case Variant::INT: return int64_t(v);
        case Variant::FLOAT: {
            const double d = v;
            return std::isfinite(d) ? json(d) : json(variant_text(v));
        }
        case Variant::ARRAY: {
            const Array a = v;
            if (depth >= kMaxLoggedDepth) return "Array of " + std::to_string(a.size()) + ", nested too deep";
            json out = json::array();
            for (int64_t i = 0; i < a.size() && i < kMaxLoggedItems; ++i) out.push_back(variant_json(a[i], depth + 1));
            if (a.size() > kMaxLoggedItems) {
                out.push_back("and " + std::to_string(a.size() - kMaxLoggedItems) + " more");
            }
            return out;
        }
        case Variant::DICTIONARY: {
            const Dictionary d = v;
            if (depth >= kMaxLoggedDepth) return "Dictionary of " + std::to_string(d.size()) + ", nested too deep";
            const Array keys = d.keys();
            json out = json::object();
            for (int64_t i = 0; i < keys.size() && i < kMaxLoggedItems; ++i) {
                out[variant_text(keys[i])] = variant_json(d[keys[i]], depth + 1);
            }
            if (keys.size() > kMaxLoggedItems) {
                out["..."] = "and " + std::to_string(keys.size() - kMaxLoggedItems) + " more";
            }
            return out;
        }
        default: return variant_text(v);
    }
}

std::string json_text(const json &j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

// No exception may unwind into Godot: `failed` gets its message and returns in body's place.
template <typename Body, typename Failed>
auto guarded(Body &&body, Failed &&failed) -> decltype(body()) {
    try {
        return body();
    } catch (const std::exception &e) {
        return failed(std::string(e.what()));
    } catch (...) {
        return failed(std::string("an exception that isn't a std::exception"));
    }
}

void invalid_once(const std::string &what, const std::string &detail) {
    static std::mutex mutex;
    static std::set<std::pair<std::string, std::string>> logged;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (logged.size() >= kMaxRefusalsLogged || !logged.emplace(what, detail).second) return;
    }
    rules::log_invalid(what, detail);
}

String context_text(const Dictionary &context, const char *key) {
    return context.has(key) ? from_std(variant_text(context[key])) : String();
}

Dictionary answer() {
    Dictionary out;
    out["ok"] = true;
    out["error"] = String();
    return out;
}

Dictionary failure(const String &error) {
    Dictionary out;
    out["ok"] = false;
    out["error"] = error;
    return out;
}

// A whole number from an int or, as a Dictionary read from JSON has them, a whole float.
bool whole(const Variant &v, int64_t &out) {
    if (v.get_type() == Variant::INT) {
        out = int64_t(v);
        return true;
    }
    if (v.get_type() != Variant::FLOAT) return false;
    const double d = v;
    if (!(d == std::floor(d)) || std::fabs(d) > kMaxWholeDouble) return false;
    out = int64_t(d);
    return true;
}

// An int or whole float from 0, or a decimal String, the form match_state().config.seed has, which
// carries seeds an int64 can't.
bool read_seed(const Dictionary &d, std::uint64_t &out, std::string &why) {
    if (!d.has("seed")) {
        why = "config: missing \"seed\"";
        return false;
    }
    const Variant v = d["seed"];
    if (v.get_type() == Variant::STRING || v.get_type() == Variant::STRING_NAME) {
        // The core's own reading of a config's seed.
        const json probe = {{"best_of", 1},
                            {"first_breaker", 0},
                            {"seed", variant_text(v)},
                            {"concede_any_time", json::array({false, false})}};
        rules::MatchConfig parsed;
        std::string error;
        if (!rules::config_from_json(probe, parsed, error)) {
            why = "config: " + error;
            return false;
        }
        out = parsed.seed;
        return true;
    }
    int64_t seed = 0;
    if (!whole(v, seed)) {
        why = "config: \"seed\" is not a whole number or a decimal String (" + variant_text(v) + ")";
        return false;
    }
    if (seed < 0) {
        why = "config: \"seed\" " + std::to_string(seed) + " is negative (a seed past an int64 goes as a decimal String)";
        return false;
    }
    out = static_cast<std::uint64_t>(seed);
    return true;
}

bool read_config(const Dictionary &d, rules::MatchConfig &out, std::string &why) {
    const auto number = [&](const char *key, int64_t &value) {
        if (!d.has(key)) {
            why = std::string("config: missing \"") + key + "\"";
            return false;
        }
        if (!whole(d[key], value)) {
            why = std::string("config: \"") + key + "\" is not a whole number (" + variant_text(d[key]) + ")";
            return false;
        }
        return true;
    };
    int64_t best_of = 0;
    int64_t first_breaker = 0;
    std::uint64_t seed = 0;
    if (!number("best_of", best_of) || !number("first_breaker", first_breaker) || !read_seed(d, seed, why)) {
        return false;
    }
    // valid_config says what is wrong with a number an int holds; one past an int would not reach it whole.
    const auto fits = [&why](const char *key, int64_t value) {
        if (value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max()) return true;
        why = std::string("config: \"") + key + "\" " + std::to_string(value) + " is out of range";
        return false;
    };
    if (!fits("best_of", best_of) || !fits("first_breaker", first_breaker)) return false;
    if (!d.has("concede_any_time")) {
        why = "config: missing \"concede_any_time\"";
        return false;
    }
    const Variant allowed = d["concede_any_time"];
    const Array pair = allowed.get_type() == Variant::ARRAY ? Array(allowed) : Array();
    if (pair.size() != 2 || pair[0].get_type() != Variant::BOOL || pair[1].get_type() != Variant::BOOL) {
        why = "config: \"concede_any_time\" is not [bool, bool] (" + variant_text(allowed) + ")";
        return false;
    }
    out.best_of = int(best_of);
    out.first_breaker = int(first_breaker);
    out.seed = seed;
    out.concede_any_time = {bool(pair[0]), bool(pair[1])};
    return true;
}

bool read_input(const Dictionary &d, rules::ShotInput &out, std::string &why) {
    out = rules::ShotInput{};
    if (!d.has("nominated")) return true;
    const Variant name = d["nominated"];
    if (name.get_type() != Variant::STRING && name.get_type() != Variant::STRING_NAME) {
        why = "input: \"nominated\" is not a ball's name or \"\" (" + variant_text(name) + ")";
        return false;
    }
    const std::string text = variant_text(name);
    if (text.empty()) return true;
    if (!ofs::table::parse_ball(text, out.nominated)) {
        why = "input: \"nominated\" is not a ball's name or \"\" (" + text + ")";
        return false;
    }
    out.has_nomination = true;
    return true;
}

std::string point_text(double x, double y) {
    return "(" + json_text(ofs::phys::number_to_json(x)) + ", " + json_text(ofs::phys::number_to_json(y)) + ")";
}

// Why `record` is not a stroke played from the frame's balls; "" when it is. observe() finds the
// same and answers with facts in which nothing was hit, which would be judged a foul.
std::string not_played_from(const rules::Position &pre, const ofs::phys::ShotRecord &record) {
    if (!record.has_strike) return "the shot record has no strike";
    const std::size_t n = pre.balls.size();
    if (record.segments.size() != n || record.final_state.balls.size() != n) {
        return "the frame has " + std::to_string(n) + " balls, the shot record " +
               std::to_string(record.segments.size());
    }
    for (std::size_t i = 0; i < n; ++i) {
        const std::string ball = "ball " + std::to_string(i);
        if (record.segments[i].empty()) return ball + " has no path in the shot record";
        const ofs::phys::BallState &start = record.segments[i].front().start;
        const rules::BallOnTable &b = pre.balls[i];
        const bool potted = start.motion == ofs::phys::Motion::pocketed;
        if (!potted && start.ball == b.ball &&
            std::hypot(start.r.x - b.pos.x, start.r.y - b.pos.y) <= kStartTolerance_m) {
            continue;
        }
        return ball + " is a " + std::string(ofs::table::ball_name(b.ball)) + " at " + point_text(b.pos.x, b.pos.y) +
               " in the frame and " +
               (potted ? "potted"
                       : "a " + std::string(ofs::table::ball_name(start.ball)) + " at " +
                             point_text(start.r.x, start.r.y)) +
               " in the shot record";
    }
    return {};
}

// The simulator's own guards: the facts still describe what was simulated (facts.hpp).
bool stopped_by_guard(const ofs::phys::ShotRecord &record) {
    return std::any_of(record.anomalies.begin(), record.anomalies.end(), [](const ofs::phys::Anomaly &a) {
        return a.kind == "event_guard" || a.kind == "zero_time_chain" || a.kind == "time_cap";
    });
}

Dictionary frame_view(const rules::FrameState &frame) {
    return json_to_variant(rules::frame_view_json(frame));
}

Dictionary match_view(const rules::MatchState &m) {
    json j = rules::match_json(m);
    j["frame"] = rules::frame_view_json(m.frame);
    return json_to_variant(j);
}

// The frame and match lines `event` brings, from the match as it stands after it. frame.started
// takes `racked`, which names the physics values the rack was laid with.
void log_event(const rules::MatchState &m, rules::MatchEvent event, const json &context, const json &racked) {
    switch (event) {
        case rules::MatchEvent::none: break;
        case rules::MatchEvent::reracked: rules::log_frame_started(m, racked); break;
        case rules::MatchEvent::frame_ended: rules::log_frame_ended(m, context); break;
        case rules::MatchEvent::match_ended: {
            rules::log_frame_ended(m, context);
            const bool conceded =
                !m.history.empty() && m.history.back().reason == rules::EndReason::match_conceded;
            rules::log_match_ended(m, conceded ? "conceded" : "frames", context);
            break;
        }
    }
}

json names(const std::vector<rules::DecisionKind> &kinds) {
    json out = json::array();
    for (const rules::DecisionKind k : kinds) out.push_back(std::string(rules::decision_name(k)));
    return out;
}

bool offered(const std::vector<rules::DecisionKind> &open, rules::DecisionKind kind) {
    return std::find(open.begin(), open.end(), kind) != open.end();
}

json standin_values(const opponent::StandinParams &p) {
    return {{"aim_error_rad", p.aim_error_rad},
            {"speed_error", p.speed_error},
            {"max_cut_rad", p.max_cut_rad},
            {"pot_speed_m_s", p.pot_speed_m_s},
            {"safety_speed_m_s", p.safety_speed_m_s},
            {"max_sims", p.max_sims},
            {"concede_required_points", p.concede_required_points}};
}

void log_standin(bool ok, json fields, const json &context) {
    for (auto it = context.begin(); it != context.end(); ++it) fields[it.key()] = it.value();
    ofs::log::Logger::instance().emit("opponent", ok ? ofs::log::Level::info : ofs::log::Level::warn,
                                      "opponent.decision", std::move(fields));
}

// Why the game could not play `who`'s stroke as given (a StandinShot or an opponent::Stroke):
// place_cue or OfsPhysics.simulate would refuse it. "" when it can.
template <typename Shot>
std::string unplayable(const Shot &shot, const rules::FrameState &frame, const rules::TableOracle &oracle,
                       const std::string &who) {
    const ofs::phys::CueStrike &c = shot.strike;
    for (const double v : {c.aim_rad, c.tip_x, c.tip_y, c.elevation_rad, c.speed_m_s}) {
        if (!std::isfinite(v)) return who + "'s strike has a number that is not finite";
    }
    if (!(c.speed_m_s > 0.0)) return who + "'s strike has no speed";
    if (!shot.places_cue) return {};
    const std::string place = who + " places the cue ball at " + point_text(shot.cue_pos.x, shot.cue_pos.y);
    if (!frame.cue_in_hand) return place + ", and it is not in hand";
    if (!std::isfinite(shot.cue_pos.x) || !std::isfinite(shot.cue_pos.y) ||
        !oracle.cue_placement_legal(frame.balls, frame.cue_ball, shot.cue_pos)) {
        return place + ", which is not in the D and clear of every ball";
    }
    return {};
}

double us_since(std::chrono::steady_clock::time_point began) {
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - began).count();
}

double ms_since(std::chrono::steady_clock::time_point began) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
}

Array xy(const rules::Vec2 &v) {
    Array out;
    out.push_back(v.x);
    out.push_back(v.y);
    return out;
}

Dictionary strike_view(const ofs::phys::CueStrike &c) {
    Dictionary out;
    out["aim_rad"] = c.aim_rad;
    out["tip_x"] = c.tip_x;
    out["tip_y"] = c.tip_y;
    out["elevation_rad"] = c.elevation_rad;
    out["speed_m_s"] = c.speed_m_s;
    return out;
}

// The answer standin_shot and opponent_take give for `who`'s stroke or decision on frame `f`, and
// its opponent.decision line in `line`. `problem` gets why the game could not apply it, "" when it
// can.
template <typename Shot>
Dictionary shot_answer(const Shot &shot, const rules::FrameState &f, const rules::TableOracle &oracle,
                       const std::string &who, std::uint64_t seed, double us, json &line, std::string &problem) {
    const json reasons = shot.why.is_object() ? shot.why : json::object();
    line = {{"kind", shot.decides ? "choice" : "shot"},
            {"player", f.striker},
            {"seed", std::to_string(seed)},
            {"why", reasons},
            {"us", us}};
    Dictionary out = answer();
    out["why"] = json_to_variant(reasons);
    out["us"] = us;
    problem.clear();
    if (shot.decides) {
        const std::vector<rules::DecisionKind> open = rules::options(f, f.striker);
        const std::string name(rules::decision_name(shot.decision));
        line["choice"] = name;
        line["options"] = names(open);
        out["decision"] = from_std(name);
        if (!offered(open, shot.decision)) {
            problem = who + " chose " + name + ", which is not open to player " + std::to_string(f.striker);
        }
        return out;
    }
    const ofs::phys::CueStrike &c = shot.strike;
    const std::string nominated =
        shot.input.has_nomination ? std::string(ofs::table::ball_name(shot.input.nominated)) : "";
    line["strike"] = ofs::phys::to_json(c);
    line["nominated"] = nominated;
    if (shot.places_cue) {
        line["cue"] =
            json::array({ofs::phys::number_to_json(shot.cue_pos.x), ofs::phys::number_to_json(shot.cue_pos.y)});
    }
    out["places_cue"] = shot.places_cue;
    out["cue"] = xy(shot.places_cue ? shot.cue_pos : f.balls.at(std::size_t(f.cue_ball)).pos);
    out["nominated"] = from_std(nominated);
    out["strike"] = strike_view(c);
    problem = unplayable(shot, f, oracle, who);
    return out;
}

// A whole number from 0 for a budget key; absent is 0.
bool budget_count(const Dictionary &budget, const char *key, int &out, std::string &why) {
    out = 0;
    if (!budget.has(key)) return true;
    int64_t value = 0;
    if (!whole(budget[key], value) || value < 0 || value > std::numeric_limits<int>::max()) {
        why = std::string("budget: \"") + key + "\" is not a whole number from 0 (" + variant_text(budget[key]) + ")";
        return false;
    }
    out = int(value);
    return true;
}

// Spec 10.6: the opponent answers within 1.5 s unless the caller says otherwise.
constexpr double kDefaultThinkMs = 1500.0;

bool read_budget(const Dictionary &budget, opponent::Think &out, std::string &why) {
    out = opponent::Think{};
    if (!budget_count(budget, "max_sims", out.max_sims, why) || !budget_count(budget, "max_steps", out.max_steps, why)) {
        return false;
    }
    out.max_ms = kDefaultThinkMs;
    if (budget.has("max_ms")) {
        const Variant v = budget["max_ms"];
        const bool number = v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT;
        const double ms = number ? double(v) : 0.0;
        if (!number || std::isnan(ms)) {
            why = "budget: \"max_ms\" is not a number (" + variant_text(v) + ")";
            return false;
        }
        out.max_ms = ms > 0.0 && std::isfinite(ms) ? ms : 0.0;
    }
    return true;
}

json think_json(const opponent::Stroke &s, double ms) {
    json out = {{"candidates", s.candidates}, {"sims", s.sims},       {"steps", s.steps},
                {"depth", s.depth},           {"stopped", s.stopped}, {"ms", ms}};
    if (s.errors > 0) out["errors"] = s.errors;
    return out;
}

json without_shot(json context) {
    context.erase("shot_id");
    return context;
}

int64_t reds_on(const rules::FrameState &f) {
    return int64_t(std::count_if(f.balls.begin(), f.balls.end(),
                                 [](const rules::BallOnTable &b) { return b.ball == rules::Ball::red; }));
}

void log_maximum(const std::string &state, const std::string &why, const std::string &level, int player,
                 int break_points, int64_t reds, const json &context) {
    json line = {{"state", state}, {"level", level}, {"player", player}, {"break_points", break_points}, {"reds", reds}};
    if (!why.empty()) line["why"] = why;
    for (auto it = context.begin(); it != context.end(); ++it) line[it.key()] = it.value();
    ofs::log::info("opponent", "opponent.maximum", std::move(line));
}

}  // namespace

struct OfsMatch::OpponentJob {
    // Copied in on the main thread before the worker starts; the worker only reads them.
    rules::FrameState frame;
    ofs::phys::PhysicsParams params;
    std::shared_ptr<const ofs::phys::TableShape> table;
    rules::RefParams ref;
    opponent::StandinParams standin;
    opponent::Level level;
    std::uint64_t seed = 0;
    opponent::Think think;
    std::string hash;         // rules::frame_hash(frame): the answer is for this frame only
    std::string params_hash;  // of `params`: a change before the take is logged with the stroke
    json context;             // log_context with shot_id
    std::chrono::steady_clock::time_point began;

    std::atomic<bool> cancel{false};
    // Set by the worker last, after it has written the answer below.
    std::atomic<bool> done{false};
    opponent::Stroke stroke;
    std::string error;  // what it threw; "" when it didn't
    double ms = 0.0;    // its own time

    std::thread worker;

    OpponentJob() = default;
    OpponentJob(const OpponentJob &) = delete;
    OpponentJob &operator=(const OpponentJob &) = delete;
    ~OpponentJob() {
        cancel.store(true);
        if (worker.joinable()) worker.join();
    }

    // The worker's body: plain C++ only, nothing of Godot's.
    void run() {
        const auto start = std::chrono::steady_clock::now();
        try {
            const rules::ScopedLogContext scope(context);
            const std::unique_ptr<rules::TableOracle> oracle = rules::make_geometry_oracle(*table, ref);
            const opponent::Env env{*table, params, *oracle, ref, standin};
            ofs::Rng rng(seed);
            opponent::Think budget = think;
            budget.cancel = &cancel;
            stroke = opponent::choose_stroke(frame, level, env, rng, budget);
        } catch (const std::exception &e) {
            error = std::string("exception: ") + e.what();
        } catch (...) {
            error = "an exception that isn't a std::exception";
        }
        ms = ms_since(start);
        done.store(true, std::memory_order_release);
    }
};

void OfsMatch::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "physics", "referee_json", "standin_json"), &OfsMatch::configure);
    ClassDB::bind_method(D_METHOD("is_ready"), &OfsMatch::is_ready);
    ClassDB::bind_method(D_METHOD("start", "config", "match_id"), &OfsMatch::start);
    ClassDB::bind_method(D_METHOD("in_progress"), &OfsMatch::in_progress);
    ClassDB::bind_method(D_METHOD("abandon"), &OfsMatch::abandon);
    ClassDB::bind_method(D_METHOD("match_state"), &OfsMatch::match_state);
    ClassDB::bind_method(D_METHOD("frame"), &OfsMatch::frame);
    ClassDB::bind_method(D_METHOD("balls"), &OfsMatch::balls);
    ClassDB::bind_method(D_METHOD("cue_ball"), &OfsMatch::cue_ball);
    ClassDB::bind_method(D_METHOD("place_cue", "x", "y"), &OfsMatch::place_cue);
    ClassDB::bind_method(D_METHOD("nearest_cue_placement", "x", "y"), &OfsMatch::nearest_cue_placement);
    ClassDB::bind_method(D_METHOD("judge", "record", "input", "context"), &OfsMatch::judge);
    ClassDB::bind_method(D_METHOD("decide", "kind", "player"), &OfsMatch::decide);
    ClassDB::bind_method(D_METHOD("next_frame"), &OfsMatch::next_frame);
    ClassDB::bind_method(D_METHOD("standin_shot", "seed", "context"), &OfsMatch::standin_shot);
    ClassDB::bind_method(D_METHOD("standin_choice", "player", "seed"), &OfsMatch::standin_choice);
    ClassDB::bind_method(D_METHOD("configure_opponent", "levels", "exhibitions_json"),
                         &OfsMatch::configure_opponent);
    ClassDB::bind_method(D_METHOD("levels"), &OfsMatch::levels);
    ClassDB::bind_method(D_METHOD("exhibitions"), &OfsMatch::exhibitions);
    ClassDB::bind_method(D_METHOD("opponent_think", "level", "seed", "context", "budget"),
                         &OfsMatch::opponent_think);
    ClassDB::bind_method(D_METHOD("opponent_thinking"), &OfsMatch::opponent_thinking);
    ClassDB::bind_method(D_METHOD("opponent_take"), &OfsMatch::opponent_take);
    ClassDB::bind_method(D_METHOD("opponent_cancel", "why"), &OfsMatch::opponent_cancel);
    ClassDB::bind_method(D_METHOD("opponent_choice", "level", "player", "seed"), &OfsMatch::opponent_choice);
    ClassDB::bind_method(D_METHOD("last_error"), &OfsMatch::last_error);
}

std::string OfsMatch::not_in_progress() const {
    if (!ready_) return "the match engine is not configured";
    if (!started_) return "no match has been started";
    if (match_.over) return match_.winner < 0 ? "the match was abandoned" : "the match is over";
    return {};
}

std::string OfsMatch::no_stroke_now() const {
    const std::string why = not_in_progress();
    if (!why.empty()) return why;
    const rules::FrameState &f = match_.frame;
    if (f.over) return "frame " + std::to_string(match_.frame_number) + " is over: next_frame racks the next";
    if (f.pending != rules::Pending::none) {
        return "player " + std::to_string(f.decider) + " has a decision to make first (" +
               std::string(rules::pending_name(f.pending)) + ")";
    }
    return {};
}

json OfsMatch::log_context(const rules::MatchState &m, const String &shot_id) const {
    json out = {{"match_id", to_std(match_id_)}, {"frame", m.frame_number}};
    if (!shot_id.is_empty()) out["shot_id"] = to_std(shot_id);
    return out;
}

json OfsMatch::rack_context(const rules::MatchState &m, const String &shot_id) const {
    json out = log_context(m, shot_id);
    out["params_hash"] = params_hash_;
    return out;
}

json OfsMatch::scope_context(const String &shot_id) const {
    return started_ ? log_context(match_, shot_id) : json::object();
}

void OfsMatch::refresh_params() {
    if (physics_.is_null() || !physics_->is_ready()) return;
    params_ = physics_->physics_params();
    params_hash_ = to_std(physics_->params_hash());
}

void OfsMatch::refusal(const char *call, const std::string &why, const json &given) const {
    last_error_ = from_std(why);
    std::string detail = why;
    if (!given.empty()) detail += "; given " + json_text(given);
    if (started_) detail += "; match " + to_std(match_id_) + ", frame " + std::to_string(match_.frame_number);
    rules::log_invalid(std::string("OfsMatch.") + call, detail);
}

Dictionary OfsMatch::refused(const char *call, const std::string &why, const json &given) const {
    refusal(call, why, given);
    return failure(last_error_);
}

Dictionary OfsMatch::thrown(const char *call, const std::string &message) const {
    last_error_ = from_std("exception: " + message);
    json data = {{"kind", "exception"}, {"message", message}, {"function", std::string("OfsMatch.") + call}};
    if (started_) {
        data["match_id"] = to_std(match_id_);
        data["frame"] = match_.frame_number;
    }
    ofs::log::error("app", "app.error", std::move(data));
    return failure(last_error_);
}

bool OfsMatch::configure(const Ref<OfsPhysics> &physics, const String &referee_json, const String &standin_json) {
    return guarded(
        [&] {
            std::vector<std::string> problems;
            rules::RefParams ref;
            opponent::StandinParams standin;
            std::unique_ptr<rules::TableOracle> oracle;
            if (in_progress()) problems.push_back("a match is in progress: it must end or be abandoned first");
            const bool have_physics = physics.is_valid() && physics->is_ready() && physics->table();
            if (!have_physics) {
                problems.push_back(physics.is_null() ? "physics: null" : "physics: not configured");
            }
            const auto load = [&problems](const String &text, const char *what, const auto &read) {
                const json file = json::parse(to_std(text), nullptr, false);
                if (file.is_discarded()) {
                    problems.push_back(std::string(what) + ": not valid JSON");
                    return;
                }
                const std::size_t first = problems.size();
                read(file);
                for (std::size_t i = first; i < problems.size(); ++i) {
                    problems[i] = std::string(what) + ": " + problems[i];
                }
            };
            load(referee_json, "referee", [&](const json &file) { rules::load_ref_params(file, ref, problems); });
            load(standin_json, "standin",
                 [&](const json &file) { opponent::load_standin_params(file, standin, problems); });
            if (problems.empty()) oracle = rules::make_geometry_oracle(*physics->table(), ref);

            const bool ok = problems.empty();
            json data = {{"ok", ok},
                         {"values", rules::ref_params_values(ref)},
                         {"standin", standin_values(standin)},
                         {"params_hash", have_physics ? to_std(physics->params_hash()) : std::string()},
                         {"problems", problems}};
            if (!ok) {
                std::string all;
                for (const std::string &problem : problems) all += (all.empty() ? "" : "; ") + problem;
                last_error_ = from_std(all);
                ofs::log::error("rules", "rules.configured", std::move(data));
                return false;
            }
            physics_ = physics;
            params_ = physics->physics_params();
            params_hash_ = to_std(physics->params_hash());
            table_ = physics->table();
            ref_ = ref;
            standin_ = standin;
            oracle_ = std::move(oracle);
            ready_ = true;
            last_error_ = String();
            ofs::log::info("rules", "rules.configured", std::move(data));
            return true;
        },
        [&](const std::string &message) {
            thrown("configure", message);
            return false;
        });
}

bool OfsMatch::is_ready() const { return ready_; }

Dictionary OfsMatch::start(const Dictionary &config, const String &match_id) {
    const auto refuse = [&](const std::string &why) {
        return refused("start", why, {{"config", variant_json(config, 0)}, {"match_id", to_std(match_id)}});
    };
    return guarded(
        [&]() -> Dictionary {
            if (!ready_) return refuse("the match engine is not configured");
            rules::MatchConfig c;
            std::string why;
            if (!read_config(config, c, why) || !rules::valid_config(c, why)) return refuse(why);
            std::string level;
            if (config.has("level")) {
                const Variant v = config["level"];
                if (v.get_type() != Variant::STRING && v.get_type() != Variant::STRING_NAME) {
                    return refuse("config: \"level\" is not a level's id or \"\" (" + variant_text(v) + ")");
                }
                level = variant_text(v);
                if (!level.empty() && find_level(level) == nullptr) {
                    return refuse("config: \"level\" " + level + " is not a configured level");
                }
            }
            const opponent::Exhibition *exhibition = nullptr;
            if (config.has("exhibition")) {
                int64_t index = -1;
                if (!whole(config["exhibition"], index) || index < 0 ||
                    index >= int64_t(exhibitions_.size())) {
                    return refuse("config: \"exhibition\" " + variant_text(config["exhibition"]) +
                                  " is not an index into the " + std::to_string(exhibitions_.size()) +
                                  " exhibition tables");
                }
                if (c.best_of != 1) {
                    return refuse("config: an exhibition is best of 1, not " + std::to_string(c.best_of));
                }
                exhibition = &exhibitions_[std::size_t(index)];
            }
            int64_t person = -1;
            const bool has_person =
                config.has("person") && whole(config["person"], person) && (person == 0 || person == 1);
            refresh_params();
            rules::MatchState next;
            {
                const rules::ScopedLogContext scope({{"match_id", to_std(match_id)}});
                next = rules::start_match(c, params_);
            }
            if (exhibition != nullptr) {
                // The opponent plays the table out; the person, when there is one, watches.
                const int striker = has_person ? int(1 - person) : 1;
                next.breaker = striker;
                next.frame = opponent::exhibition_frame(*exhibition, striker, next.frame.seed);
                next.frame.concede_any_time = c.concede_any_time;
            }
            Dictionary out = answer();
            out["match"] = match_view(next);

            // One match at a time: the one still on is left as Quit leaves it.
            cancel_thinking("a new match");
            abandon();
            const json context = {{"match_id", to_std(match_id)}, {"frame", next.frame_number}};
            json started = context;
            if (has_person) {
                started["person"] = person;
            } else if (config.has("person")) {
                rules::log_invalid("OfsMatch.start", "config: \"person\" is not 0 or 1 (" +
                                                         variant_text(config["person"]) +
                                                         "); match.started goes without it; match " +
                                                         to_std(match_id));
            }
            if (!level.empty()) started["level"] = level;
            if (exhibition != nullptr) started["exhibition"] = exhibition->name;
            rules::log_match_started(next, started);
            json racked = context;
            racked["params_hash"] = params_hash_;
            rules::log_frame_started(next, racked);
            if (exhibition != nullptr) {
                ofs::log::info("match", "exhibition.started",
                               {{"layout", exhibition->name}, {"level", level}, {"match_id", to_std(match_id)}});
            }
            match_ = std::move(next);
            maximum_state_.clear();
            maximum_frame_.clear();
            maximum_armed_ = false;
            match_id_ = match_id;
            started_ = true;
            last_error_ = String();
            return out;
        },
        [&](const std::string &message) { return thrown("start", message); });
}

bool OfsMatch::in_progress() const { return started_ && !match_.over; }

OfsMatch::OfsMatch() = default;

OfsMatch::~OfsMatch() {
    guarded(
        [&] {
            cancel_thinking("the match engine was freed");
            return true;
        },
        [&](const std::string &message) {
            thrown("~OfsMatch", message);
            return false;
        });
}

void OfsMatch::abandon() {
    if (job_) {
        guarded(
            [&] {
                cancel_thinking("the match was abandoned");
                return true;
            },
            [&](const std::string &message) {
                thrown("abandon", message);
                return false;
            });
    }
    if (!in_progress()) return;
    guarded(
        [&] {
            match_.over = true;
            match_.winner = -1;
            rules::log_match_ended(match_, "abandoned", log_context(match_));
            return true;
        },
        [&](const std::string &message) {
            thrown("abandon", message);
            return false;
        });
}

Dictionary OfsMatch::match_state() const {
    if (!started_) return {};
    return guarded([&] { return match_view(match_); }, [&](const std::string &message) {
        thrown("match_state", message);
        return Dictionary();
    });
}

Dictionary OfsMatch::frame() const {
    if (!started_) return {};
    return guarded([&] { return frame_view(match_.frame); }, [&](const std::string &message) {
        thrown("frame", message);
        return Dictionary();
    });
}

Array OfsMatch::balls() const {
    Array out;
    if (!started_) return out;
    for (const rules::BallOnTable &b : match_.frame.balls) {
        Dictionary d;
        d["ball"] = from_std(ofs::table::ball_name(b.ball));
        d["index"] = int64_t(b.index);
        d["x"] = b.pos.x;
        d["y"] = b.pos.y;
        out.push_back(d);
    }
    return out;
}

int64_t OfsMatch::cue_ball() const { return started_ ? int64_t(match_.frame.cue_ball) : 0; }

Dictionary OfsMatch::place_cue(double x, double y) {
    const auto at = [&](Dictionary out) {
        out["x"] = x;
        out["y"] = y;
        return out;
    };
    return guarded(
        [&]() -> Dictionary {
            const rules::ScopedLogContext scope(scope_context());
            const json given = {{"x", ofs::phys::number_to_json(x)}, {"y", ofs::phys::number_to_json(y)}};
            std::string why = not_in_progress();
            // Before rules.cue_placed, whose x and y are numbers.
            if (why.empty() && (!std::isfinite(x) || !std::isfinite(y))) {
                why = point_text(x, y) + " is not a point on the table";
            }
            if (!why.empty()) return at(refused("place_cue", why, given));
            rules::FrameState next = match_.frame;
            const rules::Vec2 pos{x, y};
            const bool ok = rules::place_cue_ball(next, pos, *oracle_, why);
            rules::log_cue_placed(match_.frame, pos, ok, why, log_context(match_));
            if (!ok) return at(refused("place_cue", why, given));
            match_.frame = std::move(next);
            last_error_ = String();
            return at(answer());
        },
        [&](const std::string &message) { return at(thrown("place_cue", message)); });
}

Dictionary OfsMatch::nearest_cue_placement(double x, double y) const {
    return guarded(
        [&]() -> Dictionary {
            std::string why = not_in_progress();
            if (why.empty() && (!std::isfinite(x) || !std::isfinite(y))) {
                why = point_text(x, y) + " is not a point on the table";
            }
            if (!why.empty()) {
                last_error_ = from_std(why);
                invalid_once("OfsMatch.nearest_cue_placement", why);
                return {};
            }
            const rules::FrameState &f = match_.frame;
            const rules::Vec2 place = oracle_->nearest_cue_placement(f.balls, f.cue_ball, {x, y});
            Dictionary out;
            out["x"] = place.x;
            out["y"] = place.y;
            last_error_ = String();
            return out;
        },
        [&](const std::string &message) {
            thrown("nearest_cue_placement", message);
            return Dictionary();
        });
}

Dictionary OfsMatch::judge(const Ref<OfsShotRecord> &record, const Dictionary &input, const Dictionary &context) {
    const auto refuse = [&](const std::string &why) {
        return refused("judge", why, {{"input", variant_json(input, 0)}, {"context", variant_json(context, 0)}});
    };
    return guarded(
        [&]() -> Dictionary {
            const String shot_id = context_text(context, "shot_id");
            const rules::ScopedLogContext scope(scope_context(shot_id));
            refresh_params();
            std::string why = no_stroke_now();
            if (why.empty() && record.is_null()) why = "no shot record: the stroke was not simulated";
            rules::ShotInput in;
            if (!why.empty() || !read_input(input, in, why)) return refuse(why);
            const rules::FrameState &pre = match_.frame;
            const ofs::phys::ShotRecord &shot = record->record();
            const char *not_ours = "the shot record was not simulated from balls() and cue_ball(): ";
            why = not_played_from(pre, shot);
            if (!why.empty()) return refuse(not_ours + why);
            const rules::ShotFacts facts = rules::observe(pre, shot, *table_, ref_);
            if (facts.anomaly && !stopped_by_guard(shot)) {
                return refuse(std::string(not_ours) + "rules.invalid (observe), logged before this, says how");
            }
            const rules::Judgement j = rules::judge(pre, in, facts, *oracle_, ref_);
            // Every judged stroke counts; judge() hands a frame it can't make sense of back as it was.
            if (j.post.strokes == pre.strokes) {
                return refuse("the referee could not judge the stroke: rules.invalid (judge), logged before this, "
                              "says why");
            }
            rules::MatchState next = match_;
            next.frame = j.post;
            const rules::MatchEvent event = rules::settle(next, params_);

            json judgement = rules::judgement_json(j);
            judgement.erase("post");
            Dictionary out = answer();
            out["judgement"] = json_to_variant(judgement);
            out["event"] = from_std(rules::match_event_name(event));
            out["frame"] = frame_view(next.frame);

            rules::log_decision(pre, in, facts, j, ref_, log_context(match_, shot_id));
            maximum_judged(pre, j.post, j.fouls.empty() && j.points > 0, shot_id);
            log_event(next, event, log_context(next, shot_id), rack_context(next, shot_id));
            match_ = std::move(next);
            last_error_ = String();
            return out;
        },
        [&](const std::string &message) { return thrown("judge", message); });
}

Dictionary OfsMatch::decide(const String &kind, int64_t player) {
    const auto refuse = [&](const std::string &why) {
        return refused("decide", why, {{"kind", to_std(kind)}, {"player", player}});
    };
    return guarded(
        [&]() -> Dictionary {
            const rules::ScopedLogContext scope(scope_context());
            // An accepted re-rack lays a new rack.
            refresh_params();
            std::string why = not_in_progress();
            rules::Decision d;
            if (why.empty() && !rules::parse_decision(to_std(kind), d.kind)) {
                why = "\"" + to_std(kind) + "\" is not a decision";
            }
            // Only a player past an int: the engine refuses any other itself, on the rules.choice line.
            if (why.empty() &&
                (player < std::numeric_limits<int>::min() || player > std::numeric_limits<int>::max())) {
                why = "player " + std::to_string(player) + " is not 0 or 1";
            }
            if (!why.empty()) return refuse(why);
            d.player = int(player);

            const rules::FrameState &before = match_.frame;
            const json context = log_context(match_);
            rules::MatchState next = match_;
            std::vector<rules::Call> calls;
            rules::MatchEvent event = rules::MatchEvent::none;
            if (!rules::match_decide(next, d, *oracle_, ref_, params_, calls, event, why)) {
                rules::log_choice(before, d, false, why, before, {}, context);
                const json open = names(rules::options(before, d.player));
                return refuse(why + (open.empty() ? "; nothing is open to them now"
                                                  : "; open to them: " + json_text(open)));
            }
            // rules.choice carries the frame as the choice left it, as rules.decision does; a frame
            // racked again is frame.started's to tell.
            rules::FrameState after = next.frame;
            if (event == rules::MatchEvent::reracked) {
                after = before;
                std::vector<rules::Call> again;
                std::string unused;
                rules::decide(after, d, *oracle_, ref_, again, unused);
            }

            Dictionary out = answer();
            out["calls"] = json_to_variant(rules::calls_json(calls));
            out["event"] = from_std(rules::match_event_name(event));
            out["frame"] = frame_view(next.frame);

            rules::log_choice(before, d, true, "", after, calls, context);
            log_event(next, event, log_context(next), rack_context(next));
            match_ = std::move(next);
            last_error_ = String();
            return out;
        },
        [&](const std::string &message) { return thrown("decide", message); });
}

Dictionary OfsMatch::next_frame() {
    return guarded(
        [&]() -> Dictionary {
            const rules::ScopedLogContext scope(scope_context());
            refresh_params();
            std::string why = not_in_progress();
            const std::string number = "frame " + std::to_string(match_.frame_number);
            if (why.empty() && !match_.frame.over) why = number + " is not over";
            if (!why.empty()) return refused("next_frame", why, json::array());
            rules::MatchState next = match_;
            if (!rules::next_frame(next, params_)) {
                return refused("next_frame",
                               number + " can't be followed: rules.invalid (next_frame), logged before this, says why",
                               json::array());
            }
            Dictionary out = answer();
            out["frame"] = frame_view(next.frame);
            rules::log_frame_started(next, rack_context(next));
            match_ = std::move(next);
            last_error_ = String();
            return out;
        },
        [&](const std::string &message) { return thrown("next_frame", message); });
}

Dictionary OfsMatch::standin_shot(int64_t seed, const Dictionary &context) {
    const auto refuse = [&](const std::string &why) {
        return refused("standin_shot", why, {{"seed", seed}, {"context", variant_json(context, 0)}});
    };
    return guarded(
        [&]() -> Dictionary {
            const String shot_id = context_text(context, "shot_id");
            const rules::ScopedLogContext scope(scope_context(shot_id));
            refresh_params();
            const std::string why = no_stroke_now();
            if (!why.empty()) return refuse(why);
            const rules::FrameState &f = match_.frame;
            const std::uint64_t drawn = static_cast<std::uint64_t>(seed);
            ofs::Rng rng(drawn);
            const auto began = std::chrono::steady_clock::now();
            const opponent::StandinShot shot =
                opponent::choose_shot(f, *table_, params_, *oracle_, ref_, standin_, rng);
            const double us = us_since(began);

            json line;
            std::string problem;
            Dictionary out = shot_answer(shot, f, *oracle_, "the stand-in", drawn, us, line, problem);
            // A claim or concession strikes nothing, so no shot carries its id.
            const json logged = log_context(match_, shot.decides ? String() : shot_id);
            if (!problem.empty()) {
                line["error"] = problem;
                log_standin(false, std::move(line), logged);
                return refuse(problem);
            }
            log_standin(true, std::move(line), logged);
            last_error_ = String();
            return out;
        },
        [&](const std::string &message) { return thrown("standin_shot", message); });
}

String OfsMatch::standin_choice(int64_t player, int64_t seed) {
    return choose("standin_choice", nullptr, player, seed);
}

String OfsMatch::choose(const char *call, const std::string *level_id, int64_t player, int64_t seed) {
    return guarded(
        [&]() -> String {
            const rules::ScopedLogContext scope(scope_context());
            refresh_params();
            json given = {{"player", player}, {"seed", seed}};
            if (level_id != nullptr) given["level"] = *level_id;
            std::string why = not_in_progress();
            if (why.empty() && player != 0 && player != 1) why = "player " + std::to_string(player) + " is not 0 or 1";
            const opponent::Level *level = level_id == nullptr ? nullptr : find_level(*level_id);
            if (why.empty() && level_id != nullptr && level == nullptr) {
                why = "level \"" + *level_id + "\" is not configured";
            }
            if (!why.empty()) {
                refusal(call, why, given);
                return String();
            }
            const std::string who = level == nullptr ? "the stand-in" : "level " + level->id;
            const rules::FrameState &f = match_.frame;
            const std::vector<rules::DecisionKind> open = rules::options(f, int(player));
            const json context = log_context(match_);
            const std::uint64_t drawn = static_cast<std::uint64_t>(seed);
            json line = {{"kind", "choice"},
                         {"player", player},
                         {"seed", std::to_string(drawn)},
                         {"choice", ""},
                         {"options", names(open)}};
            if (level != nullptr) line["level"] = level->id;
            if (open.empty()) {
                why = "nothing is open to player " + std::to_string(player) + " now";
                line["error"] = why;
                log_standin(false, std::move(line), context);
                refusal(call, why, given);
                return String();
            }
            ofs::Rng rng(drawn);
            const auto began = std::chrono::steady_clock::now();
            json weighed;
            rules::DecisionKind chosen = rules::DecisionKind::play;
            if (level == nullptr) {
                chosen = opponent::choose_decision(f, int(player), open, standin_, rng, &weighed);
            } else {
                const opponent::Env env{*table_, params_, *oracle_, ref_, standin_};
                chosen = opponent::choose_option(f, int(player), open, *level, env, rng, &weighed);
            }
            line["us"] = us_since(began);
            line["why"] = weighed.is_object() ? weighed : json::object();
            const bool ok = offered(open, chosen);
            if (!ok) {
                // The game could not apply it. The first option is the plain one (play, accept,
                // play first), so the match goes on.
                line["error"] = who + " chose " + std::string(rules::decision_name(chosen)) + ", which is not open; " +
                                std::string(rules::decision_name(open.front())) + " is taken";
                chosen = open.front();
            }
            const std::string name(rules::decision_name(chosen));
            line["choice"] = name;
            log_standin(ok, std::move(line), context);
            last_error_ = String();
            return from_std(name);
        },
        [&](const std::string &message) {
            thrown(call, message);
            return String();
        });
}

const opponent::Level *OfsMatch::find_level(const std::string &id) const {
    for (const opponent::Level &level : levels_) {
        if (level.id == id) return &level;
    }
    return nullptr;
}

bool OfsMatch::configure_opponent(const Dictionary &level_files, const String &exhibitions_json) {
    return guarded(
        [&] {
            std::vector<std::string> problems;
            if (!ready_) problems.push_back("the match engine is not configured: configure() first");
            if (in_progress()) problems.push_back("a match is in progress: it must end or be abandoned first");

            std::vector<opponent::Level> loaded;
            json described = json::array();
            const Array keys = level_files.keys();
            if (keys.is_empty()) problems.push_back("levels: none given");
            for (int64_t i = 0; i < keys.size(); ++i) {
                const Variant key = keys[i];
                const std::string id = variant_text(key);
                if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
                    problems.push_back("levels: the key " + id + " is not a level's id");
                    continue;
                }
                const Variant text = level_files[key];
                if (text.get_type() != Variant::STRING) {
                    problems.push_back(id + ": not the text of a level file");
                    continue;
                }
                const json file = json::parse(variant_text(text), nullptr, false);
                if (file.is_discarded()) {
                    problems.push_back(id + ": not valid JSON");
                    continue;
                }
                opponent::Level level;
                std::vector<std::string> found;
                if (!opponent::load_level(file, level, found)) {
                    for (const std::string &problem : found) problems.push_back(id + ": " + problem);
                    continue;
                }
                if (level.id != id) {
                    problems.push_back(id + ": the file is level " + level.id);
                    continue;
                }
                json entry = opponent::level_json(level);
                // As ofs_sim_cli tournament reports the level's file.
                entry["hash"] = ofs::params::content_hash(file);
                described.push_back(std::move(entry));
                loaded.push_back(std::move(level));
            }
            std::stable_sort(loaded.begin(), loaded.end(), [](const opponent::Level &a, const opponent::Level &b) {
                // The 147 (tier 0) is outside the ladder: last.
                const int ta = a.tier == 0 ? std::numeric_limits<int>::max() : a.tier;
                const int tb = b.tier == 0 ? std::numeric_limits<int>::max() : b.tier;
                return ta != tb ? ta < tb : a.id < b.id;
            });

            std::vector<opponent::Exhibition> tables;
            json table_names = json::array();
            const json file = json::parse(to_std(exhibitions_json), nullptr, false);
            if (file.is_discarded()) {
                problems.push_back("exhibitions: not valid JSON");
            } else {
                std::vector<std::string> found;
                opponent::load_exhibitions(file, tables, found);
                for (const std::string &problem : found) problems.push_back("exhibitions: " + problem);
                for (const opponent::Exhibition &e : tables) table_names.push_back(e.name);
            }

            const bool ok = problems.empty();
            json data = {{"ok", ok},
                         {"values", rules::ref_params_values(ref_)},
                         {"standin", standin_values(standin_)},
                         {"params_hash", params_hash_},
                         {"levels", described},
                         {"exhibitions", table_names},
                         {"problems", problems}};
            if (!ok) {
                std::string all;
                for (const std::string &problem : problems) all += (all.empty() ? "" : "; ") + problem;
                last_error_ = from_std(all);
                ofs::log::error("rules", "rules.configured", std::move(data));
                return false;
            }
            levels_ = std::move(loaded);
            exhibitions_ = std::move(tables);
            last_error_ = String();
            ofs::log::info("rules", "rules.configured", std::move(data));
            return true;
        },
        [&](const std::string &message) {
            thrown("configure_opponent", message);
            return false;
        });
}

Array OfsMatch::levels() const {
    Array out;
    for (const opponent::Level &level : levels_) {
        Dictionary d;
        d["id"] = from_std(level.id);
        d["name"] = from_std(level.name);
        d["tier"] = int64_t(level.tier);
        d["maximum"] = level.maximum;
        out.push_back(d);
    }
    return out;
}

PackedStringArray OfsMatch::exhibitions() const {
    PackedStringArray out;
    for (const opponent::Exhibition &e : exhibitions_) out.push_back(from_std(e.name));
    return out;
}

bool OfsMatch::opponent_think(const String &level_id, int64_t seed, const Dictionary &context,
                              const Dictionary &budget) {
    const std::uint64_t drawn = static_cast<std::uint64_t>(seed);
    const json given = {{"level", to_std(level_id)},
                        {"seed", std::to_string(drawn)},
                        {"context", variant_json(context, 0)},
                        {"budget", variant_json(budget, 0)}};
    return guarded(
        [&] {
            const String shot_id = context_text(context, "shot_id");
            const rules::ScopedLogContext scope(scope_context(shot_id));
            const opponent::Level *level = find_level(to_std(level_id));
            std::string why = no_stroke_now();
            if (why.empty() && level == nullptr) why = "level " + to_std(level_id) + " is not configured";
            opponent::Think think;
            if (!why.empty() || !read_budget(budget, think, why)) {
                refusal("opponent_think", why, given);
                return false;
            }
            cancel_thinking("replaced");
            refresh_params();
            auto job = std::make_unique<OpponentJob>();
            job->frame = match_.frame;
            job->params = params_;
            job->table = table_;
            job->ref = ref_;
            job->standin = standin_;
            job->level = *level;
            job->seed = drawn;
            think.seed = drawn;
            job->think = think;
            job->hash = rules::frame_hash(match_.frame);
            job->params_hash = params_hash_;
            job->context = log_context(match_, shot_id);
            job->began = std::chrono::steady_clock::now();

            json line = {{"level", level->id},
                         {"player", match_.frame.striker},
                         {"seed", std::to_string(drawn)},
                         {"budget", {{"max_sims", think.max_sims},
                                     {"max_steps", think.max_steps},
                                     {"max_ms", think.max_ms}}},
                         {"params_hash", params_hash_}};
            for (auto it = job->context.begin(); it != job->context.end(); ++it) line[it.key()] = it.value();
            ofs::log::info("opponent", "opponent.think", std::move(line));

            OpponentJob &running = *job;
            job->worker = std::thread([&running] { running.run(); });
            job_ = std::move(job);
            last_error_ = String();
            return true;
        },
        [&](const std::string &message) {
            thrown("opponent_think", message);
            return false;
        });
}

bool OfsMatch::opponent_thinking() const { return job_ != nullptr; }

Dictionary OfsMatch::opponent_take() {
    if (!job_ || !job_->done.load(std::memory_order_acquire)) return {};
    return guarded(
        [&]() -> Dictionary {
            // Taken, refused or thrown, the answer is spent.
            const std::unique_ptr<OpponentJob> job = std::move(job_);
            job->worker.join();
            const String shot_id = from_std(job->context.value("shot_id", std::string()));
            const rules::ScopedLogContext scope(scope_context(shot_id));
            refresh_params();
            const opponent::Level &level = job->level;
            const opponent::Stroke &stroke = job->stroke;
            const std::string who = "level " + level.id;
            const json think = think_json(stroke, job->ms);
            const bool params_changed = job->params_hash != params_hash_;
            const json given = {{"level", level.id}, {"seed", std::to_string(job->seed)}};
            // The line carries the frame it was asked about, so the stroke can be made again.
            const auto refuse = [&](const std::string &why, json line) {
                line["level"] = level.id;
                line["think"] = think;
                line["error"] = why;
                line["pre"] = rules::frame_json(job->frame);
                line["pre_hash"] = job->hash;
                if (started_) {
                    const std::string now = rules::frame_hash(match_.frame);
                    if (now != job->hash) line["now_hash"] = now;
                }
                if (params_changed) line["params_changed"] = true;
                log_standin(false, std::move(line), job->context);
                return refused("opponent_take", why, given);
            };
            json plain = {{"kind", "shot"}, {"player", job->frame.striker}, {"seed", std::to_string(job->seed)}};
            if (!job->error.empty()) return refuse(who + " could not choose a stroke: " + job->error, plain);
            if (stroke.decides) {
                plain["kind"] = "choice";
                plain["choice"] = std::string(rules::decision_name(stroke.decision));
            } else {
                plain["strike"] = ofs::phys::to_json(stroke.strike);
                plain["meant"] = ofs::phys::to_json(stroke.meant);
            }
            std::string why = not_in_progress();
            if (why.empty() && rules::frame_hash(match_.frame) != job->hash) {
                why = "the frame changed while " + who + " was thinking about it";
            }
            if (!why.empty()) return refuse(why, plain);

            json line;
            std::string problem;
            Dictionary out = shot_answer(stroke, match_.frame, *oracle_, who, job->seed, job->ms * 1000.0, line, problem);
            if (!stroke.decides) line["meant"] = ofs::phys::to_json(stroke.meant);
            if (!problem.empty()) return refuse(problem, line);
            line["level"] = level.id;
            line["think"] = think;
            line["pre_hash"] = job->hash;
            out["level"] = from_std(level.id);
            out["think"] = json_to_variant(think);
            if (!stroke.decides) out["meant"] = strike_view(stroke.meant);
            if (params_changed) {
                line["params_changed"] = true;
                out["params_changed"] = true;
            }
            // A claim or concession strikes nothing, so no shot carries its id.
            const json logged = stroke.decides ? without_shot(job->context) : job->context;
            log_standin(true, std::move(line), logged);
            if (!stroke.fallback.empty()) {
                json fallback = {{"level", level.id},
                                 {"why", stroke.fallback},
                                 {"player", job->frame.striker},
                                 {"seed", std::to_string(job->seed)}};
                for (auto it = job->context.begin(); it != job->context.end(); ++it) fallback[it.key()] = it.value();
                ofs::log::warn("opponent", "opponent.fallback", std::move(fallback));
            }
            if (level.maximum) maximum_visit(level, !stroke.decides, job->context);
            last_error_ = String();
            return out;
        },
        [&](const std::string &message) { return thrown("opponent_take", message); });
}

std::string OfsMatch::maximum_key() const {
    return to_std(match_id_) + "/" + std::to_string(match_.frame_number) + "/" + std::to_string(match_.reracks);
}

void OfsMatch::maximum_visit(const opponent::Level &level, bool strikes, const json &context) {
    const std::string key = maximum_key();
    if (key != maximum_frame_) {
        maximum_frame_ = key;
        maximum_state_.clear();
    }
    const rules::FrameState &f = match_.frame;
    maximum_armed_ = strikes;
    maximum_player_ = f.striker;
    maximum_level_ = level.id;
    maximum_shot_ = context.value("shot_id", std::string());
    // Within a visit the stroke judged says when it goes; only a visit's start is read here.
    if (f.break_points != 0) return;
    const bool on = opponent::maximum_on(f);
    const std::string state = on ? "on" : "gone";
    if (state == maximum_state_) return;
    maximum_state_ = state;
    const int64_t reds = reds_on(f);
    // With all fifteen reds on, maximum_on fails at a visit's start only on a free ball.
    const std::string why = on ? "" : reds == 15 && f.free_ball ? "a free ball" : "a red is off the table";
    log_maximum(state, why, level.id, f.striker, f.break_points, reds, context);
}

void OfsMatch::maximum_judged(const rules::FrameState &pre, const rules::FrameState &post, bool scored,
                              const String &shot_id) {
    if (!maximum_armed_) return;
    maximum_armed_ = false;
    if (maximum_state_ != "on" || maximum_key() != maximum_frame_ || pre.striker != maximum_player_) return;
    if (!maximum_shot_.empty() && maximum_shot_ != to_std(shot_id)) return;
    const bool goes_on = scored && post.striker == pre.striker;
    std::string why;
    if (!goes_on) {
        why = "the break ended";
    } else if (post.over || opponent::maximum_on(post)) {
        return;
    } else {
        why = reds_on(pre) - reds_on(post) >= 2 ? "two reds in one stroke" : "a colour other than the black";
    }
    maximum_state_ = "gone";
    log_maximum("gone", why, maximum_level_, pre.striker, goes_on ? post.break_points : pre.break_points,
                reds_on(post), log_context(match_, shot_id));
}

void OfsMatch::opponent_cancel(const String &why) {
    guarded(
        [&] {
            cancel_thinking(to_std(why));
            return true;
        },
        [&](const std::string &message) {
            thrown("opponent_cancel", message);
            return false;
        });
}

void OfsMatch::cancel_thinking(const std::string &why) {
    if (!job_) return;
    const std::unique_ptr<OpponentJob> job = std::move(job_);
    job->cancel.store(true);
    job->worker.join();
    json line = job->context;
    line["level"] = job->level.id;
    line["why"] = why;
    line["ms"] = ms_since(job->began);
    line["player"] = job->frame.striker;
    line["seed"] = std::to_string(job->seed);
    ofs::log::info("opponent", "opponent.cancelled", std::move(line));
}

String OfsMatch::opponent_choice(const String &level, int64_t player, int64_t seed) {
    const std::string id = to_std(level);
    return choose("opponent_choice", &id, player, seed);
}

String OfsMatch::last_error() const { return last_error_; }
