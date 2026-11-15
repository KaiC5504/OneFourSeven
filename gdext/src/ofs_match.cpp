#include "ofs_match.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "ofs/log.hpp"
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
    int64_t seed = 0;
    if (!number("best_of", best_of) || !number("first_breaker", first_breaker) || !number("seed", seed)) return false;
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
    out.seed = static_cast<std::uint64_t>(seed);
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

// The frame and match lines `event` brings, from the match as it stands after it.
void log_event(const rules::MatchState &m, rules::MatchEvent event, const json &context) {
    switch (event) {
        case rules::MatchEvent::none: break;
        case rules::MatchEvent::reracked: rules::log_frame_started(m, context); break;
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

// Why the game could not play the stand-in's stroke as given: place_cue or OfsPhysics.simulate
// would refuse it. "" when it can.
std::string unplayable(const opponent::StandinShot &shot, const rules::FrameState &frame,
                       const rules::TableOracle &oracle) {
    const ofs::phys::CueStrike &c = shot.strike;
    for (const double v : {c.aim_rad, c.tip_x, c.tip_y, c.elevation_rad, c.speed_m_s}) {
        if (!std::isfinite(v)) return "the stand-in's strike has a number that is not finite";
    }
    if (!(c.speed_m_s > 0.0)) return "the stand-in's strike has no speed";
    if (!shot.places_cue) return {};
    const std::string place = "the stand-in places the cue ball at " + point_text(shot.cue_pos.x, shot.cue_pos.y);
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

Array xy(const rules::Vec2 &v) {
    Array out;
    out.push_back(v.x);
    out.push_back(v.y);
    return out;
}

}  // namespace

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
    std::string logged = message;
    if (started_) logged += " (match " + to_std(match_id_) + ", frame " + std::to_string(match_.frame_number) + ")";
    ofs::log::error("app", "app.error",
                    {{"kind", "exception"}, {"message", logged}, {"function", std::string("OfsMatch.") + call}});
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
                         {"problems", problems}};
            if (!ok) {
                std::string all;
                for (const std::string &problem : problems) all += (all.empty() ? "" : "; ") + problem;
                last_error_ = from_std(all);
                ofs::log::error("rules", "rules.configured", std::move(data));
                return false;
            }
            params_ = physics->physics_params();
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
            rules::MatchState next = rules::start_match(c, params_);
            Dictionary out = answer();
            out["match"] = match_view(next);

            // One match at a time: the one still on is left as Quit leaves it.
            abandon();
            json context = {{"match_id", to_std(match_id)}, {"frame", next.frame_number}};
            json started = context;
            if (config.has("person")) {
                int64_t person = -1;
                if (whole(config["person"], person) && (person == 0 || person == 1)) {
                    started["person"] = person;
                } else {
                    rules::log_invalid("OfsMatch.start", "config: \"person\" is not 0 or 1 (" +
                                                             variant_text(config["person"]) +
                                                             "); match.started goes without it; match " +
                                                             to_std(match_id));
                }
            }
            rules::log_match_started(next, started);
            rules::log_frame_started(next, context);
            match_ = std::move(next);
            match_id_ = match_id;
            started_ = true;
            last_error_ = String();
            return out;
        },
        [&](const std::string &message) { return thrown("start", message); });
}

bool OfsMatch::in_progress() const { return started_ && !match_.over; }

void OfsMatch::abandon() {
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

            const String shot_id = context_text(context, "shot_id");
            rules::log_decision(pre, in, facts, j, ref_, log_context(match_, shot_id));
            log_event(next, event, log_context(next, shot_id));
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
            log_event(next, event, log_context(next));
            match_ = std::move(next);
            last_error_ = String();
            return out;
        },
        [&](const std::string &message) { return thrown("decide", message); });
}

Dictionary OfsMatch::next_frame() {
    return guarded(
        [&]() -> Dictionary {
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
            rules::log_frame_started(next, log_context(next));
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
            const std::string why = no_stroke_now();
            if (!why.empty()) return refuse(why);
            const rules::FrameState &f = match_.frame;
            ofs::Rng rng(static_cast<std::uint64_t>(seed));
            const auto began = std::chrono::steady_clock::now();
            const opponent::StandinShot shot =
                opponent::choose_shot(f, *table_, params_, *oracle_, ref_, standin_, rng);
            const double us = us_since(began);

            const json reasons = shot.why.is_object() ? shot.why : json::object();
            json line = {{"kind", shot.decides ? "choice" : "shot"},
                         {"player", f.striker},
                         {"why", reasons},
                         {"us", us}};
            Dictionary out = answer();
            out["why"] = json_to_variant(reasons);
            out["us"] = us;
            std::string problem;
            if (shot.decides) {
                const std::vector<rules::DecisionKind> open = rules::options(f, f.striker);
                const std::string name(rules::decision_name(shot.decision));
                line["choice"] = name;
                line["options"] = names(open);
                out["decision"] = from_std(name);
                if (!offered(open, shot.decision)) {
                    problem = "the stand-in chose " + name + ", which is not open to player " +
                              std::to_string(f.striker);
                }
            } else {
                const ofs::phys::CueStrike &c = shot.strike;
                const std::string nominated =
                    shot.input.has_nomination ? std::string(ofs::table::ball_name(shot.input.nominated)) : "";
                line["strike"] = ofs::phys::to_json(c);
                line["nominated"] = nominated;
                if (shot.places_cue) {
                    line["cue"] = json::array(
                        {ofs::phys::number_to_json(shot.cue_pos.x), ofs::phys::number_to_json(shot.cue_pos.y)});
                }
                Dictionary strike;
                strike["aim_rad"] = c.aim_rad;
                strike["tip_x"] = c.tip_x;
                strike["tip_y"] = c.tip_y;
                strike["elevation_rad"] = c.elevation_rad;
                strike["speed_m_s"] = c.speed_m_s;
                out["places_cue"] = shot.places_cue;
                out["cue"] = xy(shot.places_cue ? shot.cue_pos : f.balls.at(std::size_t(f.cue_ball)).pos);
                out["nominated"] = from_std(nominated);
                out["strike"] = strike;
                problem = unplayable(shot, f, *oracle_);
            }
            const json logged = log_context(match_, context_text(context, "shot_id"));
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
    return guarded(
        [&]() -> String {
            const json given = {{"player", player}, {"seed", seed}};
            std::string why = not_in_progress();
            if (why.empty() && player != 0 && player != 1) why = "player " + std::to_string(player) + " is not 0 or 1";
            if (!why.empty()) {
                refusal("standin_choice", why, given);
                return String();
            }
            const rules::FrameState &f = match_.frame;
            const std::vector<rules::DecisionKind> open = rules::options(f, int(player));
            const json context = log_context(match_);
            json line = {{"kind", "choice"}, {"player", player}, {"choice", ""}, {"options", names(open)}};
            if (open.empty()) {
                why = "nothing is open to player " + std::to_string(player) + " now";
                line["error"] = why;
                log_standin(false, std::move(line), context);
                refusal("standin_choice", why, given);
                return String();
            }
            ofs::Rng rng(static_cast<std::uint64_t>(seed));
            const auto began = std::chrono::steady_clock::now();
            rules::DecisionKind chosen = opponent::choose_decision(f, int(player), open, standin_, rng);
            line["us"] = us_since(began);
            const bool ok = offered(open, chosen);
            if (!ok) {
                // The game could not apply it. The first option is the plain one (play, accept,
                // play first), so the match goes on.
                line["error"] = "the stand-in chose " + std::string(rules::decision_name(chosen)) +
                                ", which is not open; " + std::string(rules::decision_name(open.front())) +
                                " is taken";
                chosen = open.front();
            }
            const std::string name(rules::decision_name(chosen));
            line["choice"] = name;
            log_standin(ok, std::move(line), context);
            last_error_ = String();
            return from_std(name);
        },
        [&](const std::string &message) {
            thrown("standin_choice", message);
            return String();
        });
}

String OfsMatch::last_error() const { return last_error_; }
