#include "ofs/rules/serialize.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include "ofs/params.hpp"
#include "ofs/physics/serialize.hpp"

namespace ofs::rules {

namespace {

using nlohmann::json;

// Readers name the value they refuse by its path in the document: `at` is the object being read
// ("" for the document itself, "original", "balls[3]") and the reason starts "original.balls[3].x: ".
std::string join(const std::string& at, const std::string& key) { return at.empty() ? key : at + "." + key; }

std::string item(const std::string& where, std::size_t i) { return where + "[" + std::to_string(i) + "]"; }

bool refuse(std::string& error, const std::string& where, const std::string& why) {
    error = where + ": " + why;
    return false;
}

// Godot's JSON reads every number as a float, so a whole float is taken as the integer it is.
bool value(const json& v, const std::string& where, int& out, std::string& error) {
    const double d = v.is_number() ? v.get<double>() : std::numeric_limits<double>::quiet_NaN();
    if (!(d == std::floor(d))) return refuse(error, where, "want an integer");
    if (d < double(std::numeric_limits<int>::min()) || d > double(std::numeric_limits<int>::max())) {
        return refuse(error, where, "too large for an integer");
    }
    out = int(d);
    return true;
}

bool value(const json& v, const std::string& where, bool& out, std::string& error) {
    if (!v.is_boolean()) return refuse(error, where, "want true or false");
    out = v.get<bool>();
    return true;
}

bool value(const json& v, const std::string& where, double& out, std::string& error) {
    if (!v.is_number() || !std::isfinite(v.get<double>())) return refuse(error, where, "want a finite number");
    out = v.get<double>();
    return true;
}

bool value(const json& v, const std::string& where, std::string& out, std::string& error) {
    if (!v.is_string()) return refuse(error, where, "want a string");
    out = v.get<std::string>();
    return true;
}

const json* member(const json& j, const std::string& at, const char* key, std::string& error) {
    const auto it = j.find(key);
    if (it != j.end()) return &*it;
    refuse(error, join(at, key), "missing");
    return nullptr;
}

template <typename T>
bool read(const json& j, const std::string& at, const char* key, T& out, std::string& error) {
    const json* v = member(j, at, key, error);
    return v != nullptr && value(*v, join(at, key), out, error);
}

const json* read_array(const json& j, const std::string& at, const char* key, std::string& error) {
    const json* v = member(j, at, key, error);
    if (v != nullptr && !v->is_array()) {
        refuse(error, join(at, key), "want an array");
        return nullptr;
    }
    return v;
}

const json* read_object(const json& j, const std::string& at, const char* key, std::string& error) {
    const json* v = member(j, at, key, error);
    if (v != nullptr && !v->is_object()) {
        refuse(error, join(at, key), "want an object");
        return nullptr;
    }
    return v;
}

template <typename E>
bool read_name(const json& j, const std::string& at, const char* key, bool (*parse)(std::string_view, E&), E& out,
               std::string& error) {
    std::string name;
    if (!read(j, at, key, name, error)) return false;
    if (!parse(name, out)) return refuse(error, join(at, key), "unknown name \"" + name + "\"");
    return true;
}

// A player, or with `or_nobody` -1 as well. These index the per-player arrays, so anything else
// is refused here and never reaches the engine.
bool read_player(const json& j, const std::string& at, const char* key, bool or_nobody, int& out,
                 std::string& error) {
    if (!read(j, at, key, out, error)) return false;
    if (out < (or_nobody ? -1 : 0) || out > 1) {
        return refuse(error, join(at, key),
                      std::to_string(out) + (or_nobody ? " is not -1, 0 or 1" : " is not a player (0 or 1)"));
    }
    return true;
}

template <typename T>
bool read_pair(const json& j, const std::string& at, const char* key, std::array<T, 2>& out, std::string& error) {
    const json* v = read_array(j, at, key, error);
    if (v == nullptr) return false;
    const std::string where = join(at, key);
    if (v->size() != 2) return refuse(error, where, "want two entries, one per player");
    return value((*v)[0], item(where, 0), out[0], error) && value((*v)[1], item(where, 1), out[1], error);
}

bool in_range(int place, std::size_t count) { return place >= 0 && std::size_t(place) < count; }

std::string out_of_range(int place, std::size_t count) {
    return "place " + std::to_string(place) + " is out of range (" + std::to_string(count) + " balls)";
}

bool read_places(const json& j, const std::string& at, const char* key, std::size_t count, std::vector<int>& out,
                 std::string& error) {
    const json* v = read_array(j, at, key, error);
    if (v == nullptr) return false;
    const std::string where = join(at, key);
    out.clear();
    for (std::size_t i = 0; i < v->size(); ++i) {
        int place = 0;
        if (!value((*v)[i], item(where, i), place, error)) return false;
        if (!in_range(place, count)) return refuse(error, item(where, i), out_of_range(place, count));
        out.push_back(place);
    }
    return true;
}

bool parse_seed(const std::string& text, std::uint64_t& out) {
    if (text.empty()) return false;
    std::uint64_t seed = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        const std::uint64_t digit = std::uint64_t(c - '0');
        if (seed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        seed = seed * 10 + digit;
    }
    out = seed;
    return true;
}

bool read_seed(const json& j, const std::string& at, std::uint64_t& out, std::string& error) {
    const json* v = member(j, at, "seed", error);
    if (v == nullptr) return false;
    const std::string where = join(at, "seed");
    if (!v->is_string()) return refuse(error, where, "want a decimal string");
    const std::string& text = v->get_ref<const std::string&>();
    if (!parse_seed(text, out)) {
        return refuse(error, where, "\"" + text + "\" is not a decimal number from 0 to 18446744073709551615");
    }
    return true;
}

std::string nominated_name(const ShotInput& in) {
    return in.has_nomination ? std::string(table::ball_name(in.nominated)) : std::string();
}

template <typename T>
json pair_json(const std::array<T, 2>& p) {
    return json::array({p[0], p[1]});
}

json on_json(const BallOn& on) {
    json j = {{"kind", on_kind_name(on.kind)}};
    if (on.kind == OnKind::colour) j["colour"] = table::ball_name(on.colour);
    return j;
}

json stats_json(const PlayerStats& s) {
    return {{"shots", s.shots},
            {"pots", s.pots},
            {"pot_attempts", s.pot_attempts},
            {"fouls", s.fouls},
            {"misses", s.misses},
            {"points_from_fouls", s.points_from_fouls},
            {"highest_break", s.highest_break}};
}

json stats_pair_json(const std::array<PlayerStats, 2>& stats) {
    return json::array({stats_json(stats[0]), stats_json(stats[1])});
}

bool read_ball(const json& j, const std::string& at, BallOnTable& out, std::string& error) {
    if (!j.is_object()) return refuse(error, at, "want an object");
    BallOnTable b;
    if (!read_name(j, at, "ball", &table::parse_ball, b.ball, error) || !read(j, at, "index", b.index, error)) {
        return false;
    }
    if (b.index < 0 || b.index > (b.ball == Ball::red ? 14 : 0)) {
        return refuse(error, join(at, "index"),
                      std::to_string(b.index) + " is out of range for " + std::string(table::ball_name(b.ball)));
    }
    if (!read(j, at, "x", b.pos.x, error) || !read(j, at, "y", b.pos.y, error)) return false;
    out = b;
    return true;
}

bool read_on(const json& j, const std::string& at, BallOn& out, std::string& error) {
    const json* on = read_object(j, at, "on", error);
    if (on == nullptr) return false;
    const std::string where = join(at, "on");
    BallOn b;
    if (!read_name(*on, where, "kind", &parse_on_kind, b.kind, error)) return false;
    if (b.kind == OnKind::colour) {
        if (!read_name(*on, where, "colour", &table::parse_ball, b.colour, error)) return false;
        if (b.colour == Ball::cue || b.colour == Ball::red) {
            return refuse(error, join(where, "colour"),
                          "\"" + std::string(table::ball_name(b.colour)) + "\" is not a colour");
        }
    }
    out = b;
    return true;
}

bool read_position(const json& j, const std::string& at, Position& out, std::string& error) {
    const json* balls = read_array(j, at, "balls", error);
    if (balls == nullptr) return false;
    out.balls.clear();
    for (std::size_t i = 0; i < balls->size(); ++i) {
        BallOnTable b;
        if (!read_ball((*balls)[i], item(join(at, "balls"), i), b, error)) return false;
        out.balls.push_back(b);
    }
    const std::size_t count = out.balls.size();
    if (!read(j, at, "cue_ball", out.cue_ball, error)) return false;
    // A position nobody has set (FrameState::original before any foul) lists no balls and has
    // cue_ball 0.
    if (!in_range(out.cue_ball, std::max<std::size_t>(count, 1))) {
        return refuse(error, join(at, "cue_ball"), out_of_range(out.cue_ball, count));
    }
    return read(j, at, "cue_in_hand", out.cue_in_hand, error) && read_on(j, at, out.on, error) &&
           read(j, at, "free_ball", out.free_ball, error) && read_places(j, at, "touching", count, out.touching, error);
}

bool read_stats(const json& j, const std::string& at, std::array<PlayerStats, 2>& out, std::string& error) {
    const json* v = read_array(j, at, "stats", error);
    if (v == nullptr) return false;
    const std::string where = join(at, "stats");
    if (v->size() != 2) return refuse(error, where, "want two entries, one per player");
    for (std::size_t i = 0; i < 2; ++i) {
        const json& s = (*v)[i];
        const std::string here = item(where, i);
        if (!s.is_object()) return refuse(error, here, "want an object");
        PlayerStats& p = out[i];
        if (!read(s, here, "shots", p.shots, error) || !read(s, here, "pots", p.pots, error) ||
            !read(s, here, "pot_attempts", p.pot_attempts, error) || !read(s, here, "fouls", p.fouls, error) ||
            !read(s, here, "misses", p.misses, error) ||
            !read(s, here, "points_from_fouls", p.points_from_fouls, error) ||
            !read(s, here, "highest_break", p.highest_break, error)) {
            return false;
        }
    }
    return true;
}

bool read_frame(const json& j, const std::string& at, FrameState& out, std::string& error) {
    FrameState s;
    if (!read_position(j, at, s, error) || !read_seed(j, at, s.seed, error) ||
        !read_player(j, at, "striker", false, s.striker, error) || !read_pair(j, at, "scores", s.scores, error) ||
        !read(j, at, "break_points", s.break_points, error) || !read_stats(j, at, s.stats, error) ||
        !read(j, at, "strokes", s.strokes, error) || !read(j, at, "respotted_black", s.respotted_black, error) ||
        !read_pair(j, at, "concede_any_time", s.concede_any_time, error) ||
        !read_name(j, at, "pending", &parse_pending, s.pending, error) ||
        !read_player(j, at, "decider", false, s.decider, error) ||
        !read_player(j, at, "offender", false, s.offender, error) ||
        !read(j, at, "last_penalty", s.last_penalty, error) || !read(j, at, "last_miss", s.last_miss, error)) {
        return false;
    }
    const json* original = read_object(j, at, "original", error);
    if (original == nullptr || !read_position(*original, join(at, "original"), s.original, error)) return false;
    if (!read(j, at, "miss_run", s.miss_run, error) || !read(j, at, "miss_warned", s.miss_warned, error) ||
        !read(j, at, "quiet_strokes", s.quiet_strokes, error) ||
        !read_player(j, at, "rerack_accepted", true, s.rerack_accepted, error) ||
        !read(j, at, "rerack_deadline", s.rerack_deadline, error) || !read(j, at, "over", s.over, error) ||
        !read_player(j, at, "winner", true, s.winner, error) ||
        !read_name(j, at, "end_reason", &parse_end_reason, s.end_reason, error) ||
        !read(j, at, "restart", s.restart, error)) {
        return false;
    }
    out = std::move(s);
    return true;
}

bool read_config(const json& j, const std::string& at, MatchConfig& out, std::string& error) {
    MatchConfig c;
    if (!read(j, at, "best_of", c.best_of, error) || !read(j, at, "first_breaker", c.first_breaker, error) ||
        !read_seed(j, at, c.seed, error) || !read_pair(j, at, "concede_any_time", c.concede_any_time, error)) {
        return false;
    }
    out = c;
    return true;
}

bool read_summary(const json& j, const std::string& at, FrameSummary& out, std::string& error) {
    if (!j.is_object()) return refuse(error, at, "want an object");
    FrameSummary s;
    if (!read(j, at, "number", s.number, error) || !read_player(j, at, "breaker", false, s.breaker, error) ||
        !read_player(j, at, "winner", true, s.winner, error) ||
        !read_name(j, at, "reason", &parse_end_reason, s.reason, error) ||
        !read_pair(j, at, "scores", s.scores, error) || !read_stats(j, at, s.stats, error) ||
        !read(j, at, "strokes", s.strokes, error) || !read(j, at, "reracks", s.reracks, error)) {
        return false;
    }
    out = s;
    return true;
}

}  // namespace

nlohmann::json ball_json(const BallOnTable& b) {
    return {{"ball", table::ball_name(b.ball)},
            {"index", b.index},
            {"x", phys::number_to_json(b.pos.x)},
            {"y", phys::number_to_json(b.pos.y)}};
}

nlohmann::json position_json(const Position& p) {
    json balls = json::array();
    for (const BallOnTable& b : p.balls) balls.push_back(ball_json(b));
    return {{"balls", std::move(balls)},
            {"cue_ball", p.cue_ball},
            {"cue_in_hand", p.cue_in_hand},
            {"on", on_json(p.on)},
            {"free_ball", p.free_ball},
            {"touching", p.touching}};
}

nlohmann::json frame_json(const FrameState& s) {
    json j = position_json(s);
    j["seed"] = std::to_string(s.seed);
    j["striker"] = s.striker;
    j["scores"] = pair_json(s.scores);
    j["break_points"] = s.break_points;
    j["stats"] = stats_pair_json(s.stats);
    j["strokes"] = s.strokes;
    j["respotted_black"] = s.respotted_black;
    j["concede_any_time"] = pair_json(s.concede_any_time);
    j["pending"] = pending_name(s.pending);
    j["decider"] = s.decider;
    j["offender"] = s.offender;
    j["last_penalty"] = s.last_penalty;
    j["last_miss"] = s.last_miss;
    j["original"] = position_json(s.original);
    j["miss_run"] = s.miss_run;
    j["miss_warned"] = s.miss_warned;
    j["quiet_strokes"] = s.quiet_strokes;
    j["rerack_accepted"] = s.rerack_accepted;
    j["rerack_deadline"] = s.rerack_deadline;
    j["over"] = s.over;
    j["winner"] = s.winner;
    j["end_reason"] = end_reason_name(s.end_reason);
    j["restart"] = s.restart;
    return j;
}

nlohmann::json input_json(const ShotInput& in) { return {{"nominated", nominated_name(in)}}; }

nlohmann::json facts_json(const ShotFacts& f) {
    json pots = json::array();
    for (const Pot& p : f.pots) {
        json pot = {{"ball", p.ball}, {"pocket", p.pocket}, {"t", phys::number_to_json(p.t)}};
        pots.push_back(std::move(pot));
    }
    json rest = json::array();
    for (const Vec2& r : f.rest) rest.push_back(json::array({phys::number_to_json(r.x), phys::number_to_json(r.y)}));
    json gaps = json::array();
    for (const double gap : f.closest_gap_m) gaps.push_back(phys::number_to_json(gap));
    return {{"first_hit", f.first_hit},
            {"first_hit_t", phys::number_to_json(f.first_hit_t)},
            {"pots", std::move(pots)},
            {"cue_potted", f.cue_potted},
            {"cue_pocket", f.cue_pocket},
            {"push", f.push},
            {"touching_moved", f.touching_moved},
            {"rest", std::move(rest)},
            {"closest_gap_m", std::move(gaps)},
            {"cue_travel_m", phys::number_to_json(f.cue_travel_m)},
            {"object_travel_m", phys::number_to_json(f.object_travel_m)},
            {"pot_attempt", f.pot_attempt},
            {"anomaly", f.anomaly}};
}

nlohmann::json foul_json(const Foul& f) {
    return {{"kind", foul_name(f.kind)}, {"clause", foul_clause(f.kind)}, {"value", f.value}, {"ball", f.ball}};
}

nlohmann::json calls_json(const std::vector<Call>& calls) {
    json out = json::array();
    for (const Call& c : calls) {
        json call = {{"kind", call_name(c.kind)}, {"value", c.value}, {"detail", c.detail}};
        out.push_back(std::move(call));
    }
    return out;
}

nlohmann::json judgement_json(const Judgement& j) {
    json fouls = json::array();
    for (const Foul& f : j.fouls) fouls.push_back(foul_json(f));
    json spotted = json::array();
    for (const BallOnTable& b : j.spotted) spotted.push_back(ball_json(b));
    return {{"post", frame_json(j.post)},
            {"fouls", std::move(fouls)},
            {"penalty", j.penalty},
            {"miss", j.miss},
            {"miss_reason", j.miss_reason},
            {"points", j.points},
            {"nominated", nominated_name(j.nominated)},
            {"free_ball_used", j.free_ball_used},
            {"spotted", std::move(spotted)},
            {"calls", calls_json(j.calls)}};
}

nlohmann::json decision_json(const Decision& d) { return {{"kind", decision_name(d.kind)}, {"player", d.player}}; }

nlohmann::json config_json(const MatchConfig& c) {
    return {{"best_of", c.best_of},
            {"first_breaker", c.first_breaker},
            {"seed", std::to_string(c.seed)},
            {"concede_any_time", pair_json(c.concede_any_time)}};
}

nlohmann::json summary_json(const FrameSummary& s) {
    return {{"number", s.number},
            {"breaker", s.breaker},
            {"winner", s.winner},
            {"reason", end_reason_name(s.reason)},
            {"scores", pair_json(s.scores)},
            {"stats", stats_pair_json(s.stats)},
            {"strokes", s.strokes},
            {"reracks", s.reracks}};
}

nlohmann::json match_json(const MatchState& m) {
    json history = json::array();
    for (const FrameSummary& s : m.history) history.push_back(summary_json(s));
    return {{"config", config_json(m.config)},
            {"frames", pair_json(m.frames)},
            {"frame_number", m.frame_number},
            {"breaker", m.breaker},
            {"reracks", m.reracks},
            {"frame", frame_json(m.frame)},
            {"history", std::move(history)},
            {"over", m.over},
            {"winner", m.winner}};
}

nlohmann::json frame_view_json(const FrameState& s) {
    json names = json::array();
    for (const Ball b : nominations(s)) names.push_back(json(table::ball_name(b)));
    json offered = json::array();
    for (int player = 0; player < 2; ++player) {
        json kinds = json::array();
        for (const DecisionKind k : options(s, player)) kinds.push_back(json(decision_name(k)));
        offered.push_back(std::move(kinds));
    }
    const int leader = s.scores[0] == s.scores[1] ? -1 : (s.scores[0] > s.scores[1] ? 0 : 1);
    json j = frame_json(s);
    j["view"] = {{"points_remaining", points_remaining(s)},
                 {"required", json::array({penalty_points_required(s, 0), penalty_points_required(s, 1)})},
                 {"on_places", places_on(s)},
                 {"nominations", std::move(names)},
                 {"options", std::move(offered)},
                 {"leader", leader}};
    return j;
}

bool position_from_json(const nlohmann::json& j, Position& out, std::string& error) {
    if (!j.is_object()) return refuse(error, "position", "want an object");
    Position p;
    if (!read_position(j, "", p, error)) return false;
    out = std::move(p);
    return true;
}

bool frame_from_json(const nlohmann::json& j, FrameState& out, std::string& error) {
    if (!j.is_object()) return refuse(error, "frame", "want an object");
    return read_frame(j, "", out, error);
}

bool input_from_json(const nlohmann::json& j, ShotInput& out, std::string& error) {
    if (!j.is_object()) return refuse(error, "input", "want an object");
    std::string name;
    if (!read(j, "", "nominated", name, error)) return false;
    ShotInput in;
    if (!name.empty()) {
        if (!table::parse_ball(name, in.nominated)) return refuse(error, "nominated", "unknown name \"" + name + "\"");
        in.has_nomination = true;
    }
    out = in;
    return true;
}

bool facts_from_json(const nlohmann::json& j, ShotFacts& out, std::string& error) {
    if (!j.is_object()) return refuse(error, "facts", "want an object");
    ShotFacts f;
    // `rest` first: it has one entry per ball, which is the range every place is checked against.
    const json* rest = read_array(j, "", "rest", error);
    if (rest == nullptr) return false;
    for (std::size_t i = 0; i < rest->size(); ++i) {
        const json& r = (*rest)[i];
        const std::string here = item("rest", i);
        Vec2 pos;
        if (!r.is_array() || r.size() != 2) return refuse(error, here, "want [x, y]");
        if (!value(r[0], item(here, 0), pos.x, error) || !value(r[1], item(here, 1), pos.y, error)) return false;
        f.rest.push_back(pos);
    }
    const std::size_t count = f.rest.size();

    const json* gaps = read_array(j, "", "closest_gap_m", error);
    if (gaps == nullptr) return false;
    if (gaps->size() != count) {
        return refuse(error, "closest_gap_m",
                      std::to_string(gaps->size()) + " entries for " + std::to_string(count) + " balls");
    }
    for (std::size_t i = 0; i < count; ++i) {
        double gap = 0.0;
        if (!value((*gaps)[i], item("closest_gap_m", i), gap, error)) return false;
        f.closest_gap_m.push_back(gap);
    }

    if (!read_places(j, "", "first_hit", count, f.first_hit, error) ||
        !read(j, "", "first_hit_t", f.first_hit_t, error)) {
        return false;
    }
    const json* pots = read_array(j, "", "pots", error);
    if (pots == nullptr) return false;
    for (std::size_t i = 0; i < pots->size(); ++i) {
        const json& p = (*pots)[i];
        const std::string here = item("pots", i);
        if (!p.is_object()) return refuse(error, here, "want an object");
        Pot pot;
        if (!read(p, here, "ball", pot.ball, error)) return false;
        if (!in_range(pot.ball, count)) return refuse(error, join(here, "ball"), out_of_range(pot.ball, count));
        if (!read(p, here, "pocket", pot.pocket, error) || !read(p, here, "t", pot.t, error)) return false;
        f.pots.push_back(pot);
    }
    if (!read(j, "", "cue_potted", f.cue_potted, error) || !read(j, "", "cue_pocket", f.cue_pocket, error) ||
        !read(j, "", "push", f.push, error) || !read_places(j, "", "touching_moved", count, f.touching_moved, error) ||
        !read(j, "", "cue_travel_m", f.cue_travel_m, error) ||
        !read(j, "", "object_travel_m", f.object_travel_m, error) ||
        !read(j, "", "pot_attempt", f.pot_attempt, error) || !read(j, "", "anomaly", f.anomaly, error)) {
        return false;
    }
    out = std::move(f);
    return true;
}

bool decision_from_json(const nlohmann::json& j, Decision& out, std::string& error) {
    if (!j.is_object()) return refuse(error, "decision", "want an object");
    Decision d;
    if (!read_name(j, "", "kind", &parse_decision, d.kind, error) ||
        !read_player(j, "", "player", false, d.player, error)) {
        return false;
    }
    out = d;
    return true;
}

bool config_from_json(const nlohmann::json& j, MatchConfig& out, std::string& error) {
    if (!j.is_object()) return refuse(error, "config", "want an object");
    return read_config(j, "", out, error);
}

bool match_from_json(const nlohmann::json& j, MatchState& out, std::string& error) {
    if (!j.is_object()) return refuse(error, "match", "want an object");
    MatchState m;
    const json* config = read_object(j, "", "config", error);
    if (config == nullptr || !read_config(*config, "config", m.config, error)) return false;
    if (!read_pair(j, "", "frames", m.frames, error) || !read(j, "", "frame_number", m.frame_number, error) ||
        !read_player(j, "", "breaker", false, m.breaker, error) || !read(j, "", "reracks", m.reracks, error)) {
        return false;
    }
    const json* frame = read_object(j, "", "frame", error);
    if (frame == nullptr || !read_frame(*frame, "frame", m.frame, error)) return false;
    const json* history = read_array(j, "", "history", error);
    if (history == nullptr) return false;
    for (std::size_t i = 0; i < history->size(); ++i) {
        FrameSummary s;
        if (!read_summary((*history)[i], item("history", i), s, error)) return false;
        m.history.push_back(s);
    }
    if (!read(j, "", "over", m.over, error) || !read_player(j, "", "winner", true, m.winner, error)) return false;
    out = std::move(m);
    return true;
}

std::string frame_hash(const FrameState& s) { return params::content_hash(frame_json(s)); }

}  // namespace ofs::rules
