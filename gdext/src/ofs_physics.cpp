#include "ofs_physics.h"

#include <cmath>
#include <exception>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/log.hpp"
#include "ofs/params.hpp"
#include "ofs/physics/cue.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/physics/record_query.hpp"
#include "ofs/physics/repro.hpp"
#include "ofs/physics/simulator.hpp"

using namespace godot;

namespace {

constexpr std::size_t kMaxPathPoints = 2048;
// Bounds on a refused shot's inputs as logged, so a runaway Array can't flood the session.
constexpr int64_t kMaxLoggedItems = 32;
constexpr int kMaxLoggedDepth = 6;
// The guides query every frame; a bad input held there logs its refusal once, and only so many
// distinct refusals are logged in a session.
constexpr std::size_t kMaxRefusalsLogged = 64;

std::string to_std(const String &text) {
    const CharString utf8 = text.utf8();
    return std::string(utf8.get_data(), size_t(utf8.length()));
}

String from_std(std::string_view text) {
    return String::utf8(text.data(), int64_t(text.size()));
}

Array xy(double x, double y) {
    Array out;
    out.push_back(x);
    out.push_back(y);
    return out;
}

Array xy(const ofs::table::Vec2 &v) { return xy(v.x, v.y); }

// Integer JSON stays int, so a parameter like max_events reads back as one.
Variant json_to_variant(const nlohmann::json &j) {
    switch (j.type()) {
        case nlohmann::json::value_t::boolean: return j.get<bool>();
        case nlohmann::json::value_t::number_integer: return j.get<int64_t>();
        case nlohmann::json::value_t::number_unsigned: return int64_t(j.get<uint64_t>());
        case nlohmann::json::value_t::number_float: return j.get<double>();
        case nlohmann::json::value_t::string: return from_std(j.get<std::string>());
        case nlohmann::json::value_t::array: {
            Array out;
            for (const nlohmann::json &item : j) out.push_back(json_to_variant(item));
            return out;
        }
        case nlohmann::json::value_t::object: {
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
nlohmann::json variant_json(const Variant &v, int depth) {
    switch (v.get_type()) {
        case Variant::NIL: return nullptr;
        case Variant::BOOL: return bool(v);
        case Variant::INT: return int64_t(v);
        case Variant::FLOAT: {
            const double d = v;
            return std::isfinite(d) ? nlohmann::json(d) : nlohmann::json(variant_text(v));
        }
        case Variant::ARRAY: {
            const Array a = v;
            if (depth >= kMaxLoggedDepth) return "Array of " + std::to_string(a.size()) + ", nested too deep";
            nlohmann::json out = nlohmann::json::array();
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
            nlohmann::json out = nlohmann::json::object();
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

void query_refused(const char *what, const std::string &reason) {
    static std::mutex mutex;
    static std::set<std::pair<std::string, std::string>> logged;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (logged.size() >= kMaxRefusalsLogged || !logged.emplace(what, reason).second) return;
    }
    ofs::log::warn("physics", "physics.query_refused", {{"what", what}, {"reason", reason}});
}

void caught(const char *function, const std::string &message) {
    ofs::log::error("app", "app.error", {{"kind", "exception"}, {"message", message}, {"function", function}});
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

bool read_number(const Dictionary &d, const char *key, const std::string &where, double &out, std::string &why) {
    if (!d.has(key)) {
        why = where + ": missing \"" + key + "\"";
        return false;
    }
    const Variant v = d[key];
    if (v.get_type() != Variant::FLOAT && v.get_type() != Variant::INT) {
        why = where + ": \"" + key + "\" is not a number (" + variant_text(v) + ")";
        return false;
    }
    out = double(v);
    if (!std::isfinite(out)) {
        why = where + ": \"" + key + "\" is not finite (" + variant_text(v) + ")";
        return false;
    }
    return true;
}

// JSON-loaded positions carry integers as floats, so an integral float counts.
bool read_int(const Dictionary &d, const char *key, const std::string &where, int64_t &out, std::string &why) {
    double value = 0.0;
    if (!read_number(d, key, where, value, why)) return false;
    if (value != std::floor(value) || std::fabs(value) > 1e9) {
        why = where + ": \"" + key + "\" is not a whole number (" + variant_text(d[key]) + ")";
        return false;
    }
    out = int64_t(value);
    return true;
}

bool parse_balls(const Array &balls, const ofs::phys::TableShape &table, ofs::phys::TableState &out,
                 std::string &why) {
    out.balls.clear();
    out.balls.reserve(size_t(balls.size()));
    // Each (ball, index) once, which also caps the list at the 22 balls of a full table.
    std::set<std::pair<int, int>> seen;
    for (int64_t i = 0; i < balls.size(); ++i) {
        const std::string where = "balls[" + std::to_string(i) + "]";
        if (balls[i].get_type() != Variant::DICTIONARY) {
            why = where + ": not a Dictionary (" + variant_text(balls[i]) + ")";
            return false;
        }
        const Dictionary d = balls[i];
        ofs::phys::BallState b;
        if (!d.has("ball")) {
            why = where + ": missing \"ball\"";
            return false;
        }
        const Variant name = d["ball"];
        if ((name.get_type() != Variant::STRING && name.get_type() != Variant::STRING_NAME) ||
            !ofs::table::parse_ball(variant_text(name), b.ball)) {
            why = where + ": unknown ball \"" + variant_text(name) + "\"";
            return false;
        }
        int64_t index = 0;
        if (!read_int(d, "index", where, index, why)) return false;
        if (index < 0 || index > (b.ball == ofs::table::Ball::red ? 14 : 0)) {
            why = where + ": index " + std::to_string(index) + " is out of range for a " + variant_text(name) +
                  " ball";
            return false;
        }
        b.index = int(index);
        if (!seen.emplace(int(b.ball), b.index).second) {
            why = where + ": " + variant_text(name) + " " + std::to_string(index) + " is already in the list";
            return false;
        }
        if (!read_number(d, "x", where, b.r.x, why) || !read_number(d, "y", where, b.r.y, why)) return false;
        if (d.has("pocket")) {
            int64_t pocket = -1;
            if (!read_int(d, "pocket", where, pocket, why)) return false;
            if (pocket < -1 || pocket >= int64_t(table.pockets.size())) {
                why = where + ": pocket " + std::to_string(pocket) + " is not -1 or a pocket index";
                return false;
            }
            if (pocket >= 0) {
                b.motion = ofs::phys::Motion::pocketed;
                b.pocket = int(pocket);
            }
        }
        out.balls.push_back(b);
    }
    return true;
}

bool parse_strike(const Dictionary &d, bool aim_only, ofs::phys::CueStrike &out, std::string &why) {
    const std::string where = "strike";
    if (!read_number(d, "aim_rad", where, out.aim_rad, why) || !read_number(d, "tip_x", where, out.tip_x, why) ||
        !read_number(d, "tip_y", where, out.tip_y, why)) {
        return false;
    }
    if (aim_only) return true;
    return read_number(d, "elevation_rad", where, out.elevation_rad, why) &&
           read_number(d, "speed_m_s", where, out.speed_m_s, why);
}

bool check_cue_ball(const ofs::phys::TableState &s, int64_t cue_ball, std::string &why) {
    if (cue_ball < 0 || uint64_t(cue_ball) >= s.balls.size()) {
        why = "cue_ball " + std::to_string(cue_ball) + " is out of range (" + std::to_string(s.balls.size()) +
              " balls)";
        return false;
    }
    const ofs::phys::BallState &b = s.balls[size_t(cue_ball)];
    if (b.ball != ofs::table::Ball::cue) {
        why = "cue_ball " + std::to_string(cue_ball) + " is a " + std::string(ofs::table::ball_name(b.ball)) +
              ", not the cue ball";
        return false;
    }
    if (b.motion == ofs::phys::Motion::pocketed) {
        why = "cue_ball " + std::to_string(cue_ball) + " is potted";
        return false;
    }
    return true;
}

Dictionary ball_entry(const ofs::phys::BallState &b) {
    Dictionary d;
    d["ball"] = from_std(ofs::table::ball_name(b.ball));
    d["index"] = int64_t(b.index);
    d["x"] = b.r.x;
    d["y"] = b.r.y;
    return d;
}

// The outline as the game draws it. Lines and arcs come out in TableShape's order, which is the
// file's: load_table_shape refuses a file with any bad entry, so none are skipped. Checked anyway,
// since events' body indices depend on it.
Dictionary outline(const nlohmann::json &file, const ofs::phys::TableShape &t, std::vector<std::string> &problems) {
    const nlohmann::json &file_lines = file.at("lines");
    const nlohmann::json &file_arcs = file.at("arcs");
    if (file_lines.size() != t.lines.size() || file_arcs.size() != t.arcs.size()) {
        problems.push_back("table: the loaded outline has " + std::to_string(t.lines.size()) + " lines and " +
                           std::to_string(t.arcs.size()) + " arcs, the file " + std::to_string(file_lines.size()) +
                           " and " + std::to_string(file_arcs.size()));
        return {};
    }
    const auto name_of = [](const nlohmann::json &item) {
        const auto it = item.find("name");
        return it != item.end() && it->is_string() ? from_std(it->get<std::string>()) : String();
    };
    Array lines;
    for (size_t i = 0; i < t.lines.size(); ++i) {
        const ofs::phys::LineCushion &l = t.lines[i];
        const nlohmann::json &item = file_lines[i];
        if (item.at("p1").at(0).get<double>() != l.p1.x || item.at("p1").at(1).get<double>() != l.p1.y ||
            item.at("p2").at(0).get<double>() != l.p2.x || item.at("p2").at(1).get<double>() != l.p2.y) {
            problems.push_back("table: lines[" + std::to_string(i) + "] doesn't match the loaded outline's order");
            return {};
        }
        Dictionary d;
        d["name"] = name_of(item);
        d["kind"] = from_std(item.at("kind").get<std::string>());
        d["p1"] = xy(l.p1);
        d["p2"] = xy(l.p2);
        d["normal"] = xy(l.normal);
        lines.push_back(d);
    }
    Array arcs;
    for (size_t i = 0; i < t.arcs.size(); ++i) {
        const ofs::phys::ArcCushion &a = t.arcs[i];
        const nlohmann::json &item = file_arcs[i];
        if (item.at("centre").at(0).get<double>() != a.centre.x || item.at("centre").at(1).get<double>() != a.centre.y) {
            problems.push_back("table: arcs[" + std::to_string(i) + "] doesn't match the loaded outline's order");
            return {};
        }
        Dictionary d;
        d["name"] = name_of(item);
        d["centre"] = xy(a.centre);
        d["radius"] = a.radius;
        d["angle_start"] = a.angle_start;
        d["angle_end"] = a.angle_end;
        d["pocket"] = int64_t(a.pocket);
        arcs.push_back(d);
    }
    // TableShape orders pockets by name whatever the file's order; pocket events and arcs use it.
    Array pockets;
    for (const ofs::phys::Pocket &p : t.pockets) {
        Dictionary d;
        d["name"] = from_std(p.name);
        d["corner"] = p.corner;
        d["drop_centre"] = xy(p.drop_centre);
        d["drop_radius"] = p.drop_radius;
        pockets.push_back(d);
    }
    Dictionary out;
    out["lines"] = lines;
    out["arcs"] = arcs;
    out["pockets"] = pockets;
    out["rail_height"] = t.rail_height_m;
    out["rail_width"] = t.rail_width_m;
    return out;
}

Array vec_array(const ofs::Vec3 &v, bool with_z) {
    Array out;
    out.push_back(v.x);
    out.push_back(v.y);
    if (with_z) out.push_back(v.z);
    return out;
}

String context_text(const Dictionary &context, const char *key) {
    return context.has(key) ? from_std(variant_text(context[key])) : String();
}

}  // namespace

void OfsShotRecord::_bind_methods() {
    ClassDB::bind_method(D_METHOD("t_end"), &OfsShotRecord::t_end);
    ClassDB::bind_method(D_METHOD("ball_count"), &OfsShotRecord::ball_count);
    ClassDB::bind_method(D_METHOD("shot_id"), &OfsShotRecord::shot_id);
    ClassDB::bind_method(D_METHOD("state_at", "t"), &OfsShotRecord::state_at);
    ClassDB::bind_method(D_METHOD("events"), &OfsShotRecord::events);
    ClassDB::bind_method(D_METHOD("path", "ball", "max_step_s"), &OfsShotRecord::path);
    ClassDB::bind_method(D_METHOD("strike_outcome"), &OfsShotRecord::strike_outcome);
    ClassDB::bind_method(D_METHOD("final_balls"), &OfsShotRecord::final_balls);
    ClassDB::bind_method(D_METHOD("anomalies"), &OfsShotRecord::anomalies);
    ClassDB::bind_method(D_METHOD("sim_us"), &OfsShotRecord::sim_us);
    BIND_CONSTANT(kStride);
}

double OfsShotRecord::t_end() const { return record_.t_end; }
int OfsShotRecord::ball_count() const { return int(record_.final_state.balls.size()); }
String OfsShotRecord::shot_id() const { return shot_id_; }

PackedFloat64Array OfsShotRecord::state_at(double t) const {
    return guarded(
        [&] {
            // !(t > 0) also catches NaN.
            const double at = !(t > 0.0) ? 0.0 : std::min(t, record_.t_end);
            const size_t n = record_.final_state.balls.size();
            PackedFloat64Array out;
            out.resize(int64_t(n * kStride));
            double *w = out.ptrw();
            for (size_t i = 0; i < n; ++i) {
                const ofs::phys::BallState b = i < record_.segments.size() && !record_.segments[i].empty()
                                                   ? record_.ball_at(int(i), at)
                                                   : record_.final_state.balls[i];
                double *s = w + i * kStride;
                s[0] = b.r.x;
                s[1] = b.r.y;
                s[2] = b.v.x;
                s[3] = b.v.y;
                s[4] = b.w.x;
                s[5] = b.w.y;
                s[6] = b.w.z;
                s[7] = double(b.motion);
                s[8] = double(b.pocket);
            }
            return out;
        },
        [&](const std::string &message) {
            caught("OfsShotRecord.state_at",
                   "shot " + to_std(shot_id_) + " at t=" + std::to_string(t) + ": " + message);
            return PackedFloat64Array();
        });
}

Array OfsShotRecord::events() const {
    if (events_built_) return events_;
    return guarded(
        [&] {
            Array out;
            const ofs::phys::TableShape empty;
            const ofs::phys::TableShape &table = table_ ? *table_ : empty;
            for (size_t i = 0; i < record_.events.size(); ++i) {
                const ofs::phys::ShotEvent &e = record_.events[i];
                Dictionary d;
                d["t"] = e.t;
                d["kind"] = from_std(ofs::phys::event_kind_name(e.kind));
                d["a"] = int64_t(e.a);
                d["b"] = int64_t(e.b);
                d["body"] = int64_t(e.body);
                d["from"] = from_std(ofs::phys::motion_name(e.from));
                d["to"] = from_std(ofs::phys::motion_name(e.to));
                d["speed"] = ofs::phys::event_impact_speed(record_, i, table);
                d.make_read_only();
                out.push_back(d);
            }
            out.make_read_only();
            events_ = out;
            events_built_ = true;
            return events_;
        },
        [&](const std::string &message) {
            caught("OfsShotRecord.events", "shot " + to_std(shot_id_) + ": " + message);
            return Array();
        });
}

PackedVector2Array OfsShotRecord::path(int64_t ball, double max_step_s) const {
    if (ball < 0 || uint64_t(ball) >= record_.segments.size()) {
        query_refused("path", "ball " + std::to_string(ball) + " is out of range (" +
                                  std::to_string(record_.segments.size()) + " balls)");
        return {};
    }
    return guarded(
        [&] {
            const std::vector<ofs::table::Vec2> points =
                ofs::phys::sample_path(record_, int(ball), max_step_s, kMaxPathPoints);
            PackedVector2Array out;
            out.resize(int64_t(points.size()));
            Vector2 *w = out.ptrw();
            for (size_t i = 0; i < points.size(); ++i) w[i] = Vector2(real_t(points[i].x), real_t(points[i].y));
            return out;
        },
        [&](const std::string &message) {
            caught("OfsShotRecord.path", "shot " + to_std(shot_id_) + " ball " + std::to_string(ball) + ": " + message);
            return PackedVector2Array();
        });
}

Dictionary OfsShotRecord::strike_outcome() const {
    Dictionary out;
    if (!record_.has_strike) return out;
    const ofs::phys::StrikeOutcome &s = record_.strike;
    out["v"] = vec_array(s.v, false);
    out["w"] = vec_array(s.w, true);
    out["elevation_used_rad"] = s.elevation_used_rad;
    out["elevation_raised"] = s.elevation_raised;
    out["squirt_rad"] = s.squirt_rad;
    out["miscue"] = s.miscue;
    return out;
}

Array OfsShotRecord::final_balls() const {
    Array out;
    for (const ofs::phys::BallState &b : record_.final_state.balls) {
        Dictionary d = ball_entry(b);
        d["pocket"] = int64_t(b.motion == ofs::phys::Motion::pocketed ? b.pocket : -1);
        out.push_back(d);
    }
    return out;
}

Array OfsShotRecord::anomalies() const {
    Array out;
    for (const ofs::phys::Anomaly &a : record_.anomalies) {
        Array balls;
        for (const int b : a.balls) balls.push_back(int64_t(b));
        Dictionary d;
        d["kind"] = from_std(a.kind);
        d["t"] = a.t;
        d["balls"] = balls;
        d["detail"] = from_std(a.detail);
        out.push_back(d);
    }
    return out;
}

double OfsShotRecord::sim_us() const { return record_.sim_us; }

void OfsShotRecord::set_record(ofs::phys::ShotRecord record, std::shared_ptr<const ofs::phys::TableShape> table,
                               String shot_id) {
    record_ = std::move(record);
    table_ = std::move(table);
    shot_id_ = shot_id;
    events_ = Array();
    events_built_ = false;
}

void OfsPhysics::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "params_json", "params_hash", "table_json"), &OfsPhysics::configure);
    ClassDB::bind_method(D_METHOD("is_ready"), &OfsPhysics::is_ready);
    ClassDB::bind_method(D_METHOD("params_hash"), &OfsPhysics::params_hash);
    ClassDB::bind_method(D_METHOD("table_hash"), &OfsPhysics::table_hash);
    ClassDB::bind_method(D_METHOD("simulate", "balls", "cue_ball", "strike", "seed", "context"), &OfsPhysics::simulate);
    ClassDB::bind_method(D_METHOD("predict", "balls", "cue_ball", "strike"), &OfsPhysics::predict);
    ClassDB::bind_method(D_METHOD("min_cue_elevation", "balls", "cue_ball", "strike"), &OfsPhysics::min_cue_elevation);
    ClassDB::bind_method(D_METHOD("last_error"), &OfsPhysics::last_error);
    ClassDB::bind_method(D_METHOD("table_shape"), &OfsPhysics::table_shape);
    ClassDB::bind_method(D_METHOD("params_values"), &OfsPhysics::params_values);
    ClassDB::bind_method(D_METHOD("opening_rack", "seed"), &OfsPhysics::opening_rack);
    ClassDB::bind_static_method("OfsPhysics", D_METHOD("member_json", "json_text", "key"), &OfsPhysics::member_json);
}

bool OfsPhysics::configure(const String &params_json, const String &params_hash, const String &table_json) {
    std::vector<std::string> problems;
    std::string table_hash;
    ofs::phys::PhysicsParams params;
    auto table = std::make_shared<ofs::phys::TableShape>();
    Dictionary shape;
    const auto prefix_from = [&problems](size_t first, const char *what) {
        for (size_t i = first; i < problems.size(); ++i) problems[i] = std::string(what) + ": " + problems[i];
    };
    try {
        const nlohmann::json params_file = nlohmann::json::parse(to_std(params_json), nullptr, false);
        if (params_file.is_discarded()) {
            problems.push_back("params: not valid JSON");
        } else {
            const size_t first = problems.size();
            ofs::phys::load_physics_params(params_file, params, problems);
            prefix_from(first, "params");
        }
        const nlohmann::json table_file = nlohmann::json::parse(to_std(table_json), nullptr, false);
        if (table_file.is_discarded()) {
            problems.push_back("table: not valid JSON");
        } else {
            // The same hash sim_cli's load_table takes, so its replay recognises the game's table.
            try {
                table_hash = ofs::params::content_hash(table_file);
            } catch (const std::exception &e) {
                problems.push_back(std::string("table: can't be hashed: ") + e.what());
            }
            const size_t first = problems.size();
            if (ofs::phys::load_table_shape(table_file, *table, problems)) {
                prefix_from(first, "table");
                shape = outline(table_file, *table, problems);
            } else {
                prefix_from(first, "table");
            }
        }
    } catch (const std::exception &e) {
        problems.push_back(std::string("configure: ") + e.what());
    }

    const bool ok = problems.empty();
    nlohmann::json data = {{"ok", ok},
                           {"params_hash", to_std(params_hash)},
                           {"table_hash", table_hash},
                           {"problems", problems}};
    if (!ok) {
        ofs::log::error("physics", "physics.configured", std::move(data));
        return false;
    }
    params_ = params;
    table_ = std::move(table);
    params_hash_ = params_hash;
    table_hash_ = from_std(table_hash);
    table_shape_ = shape;
    ready_ = true;
    ofs::log::info("physics", "physics.configured", std::move(data));
    return true;
}

bool OfsPhysics::is_ready() const { return ready_; }
String OfsPhysics::params_hash() const { return params_hash_; }
String OfsPhysics::table_hash() const { return table_hash_; }

Ref<OfsShotRecord> OfsPhysics::run(const Array &balls, int64_t cue_ball, const Dictionary &strike, int64_t seed,
                                    const Dictionary *context) {
    const String shot_id = context ? context_text(*context, "shot_id") : String();
    const auto refuse = [&](const std::string &why) {
        last_error_ = from_std(why);
        if (context) {
            ofs::log::warn("shot", "shot.rejected",
                           {{"shot_id", to_std(shot_id)},
                            {"reason", why},
                            {"balls", variant_json(balls, 0)},
                            {"cue_ball", cue_ball},
                            {"strike", variant_json(strike, 0)}});
        } else {
            query_refused("predict", why);
        }
        return Ref<OfsShotRecord>();
    };
    return guarded(
        [&] {
            std::string why;
            ofs::phys::SimInput in;
            if (!ready_) {
                why = "physics is not configured";
            } else if (parse_balls(balls, *table_, in.start, why) && check_cue_ball(in.start, cue_ball, why)) {
                parse_strike(strike, false, in.strike, why);
            }
            if (!why.empty()) return refuse(why);
            in.cue_ball = int(cue_ball);
            in.has_strike = true;
            in.seed = static_cast<uint64_t>(seed);
            in.params_hash = to_std(params_hash_);
            in.table_hash = to_std(table_hash_);

            ofs::phys::ShotRecord record = ofs::phys::simulate(in, *table_, params_);
            if (context) {
                const nlohmann::json logged = {{"shot_id", to_std(shot_id)},
                                               {"mode", to_std(context_text(*context, "mode"))},
                                               {"strike_mode", to_std(context_text(*context, "strike_mode"))}};
                ofs::phys::log_shot(in, params_, record, logged);
            }
            Ref<OfsShotRecord> out;
            out.instantiate();
            out->set_record(std::move(record), table_, shot_id);
            last_error_ = String();
            return out;
        },
        [&](const std::string &message) {
            if (context) return refuse("exception: " + message);
            last_error_ = from_std("exception: " + message);
            caught("OfsPhysics.predict", message);
            return Ref<OfsShotRecord>();
        });
}

Ref<OfsShotRecord> OfsPhysics::simulate(const Array &balls, int64_t cue_ball, const Dictionary &strike, int64_t seed,
                                        const Dictionary &context) {
    return run(balls, cue_ball, strike, seed, &context);
}

Ref<OfsShotRecord> OfsPhysics::predict(const Array &balls, int64_t cue_ball, const Dictionary &strike) {
    return run(balls, cue_ball, strike, 0, nullptr);
}

double OfsPhysics::min_cue_elevation(const Array &balls, int64_t cue_ball, const Dictionary &strike) const {
    return guarded(
        [&] {
            std::string why;
            ofs::phys::TableState s;
            ofs::phys::CueStrike c;
            if (!ready_) {
                why = "physics is not configured";
            } else if (parse_balls(balls, *table_, s, why) && check_cue_ball(s, cue_ball, why)) {
                parse_strike(strike, true, c, why);
            }
            if (!why.empty()) {
                last_error_ = from_std(why);
                query_refused("min_cue_elevation", why);
                return 0.0;
            }
            last_error_ = String();
            return ofs::phys::min_cue_elevation(s, int(cue_ball), c, *table_, params_);
        },
        [&](const std::string &message) {
            last_error_ = from_std("exception: " + message);
            caught("OfsPhysics.min_cue_elevation", message);
            return 0.0;
        });
}

String OfsPhysics::last_error() const { return last_error_; }

Dictionary OfsPhysics::table_shape() const {
    return ready_ ? table_shape_.duplicate(true) : Dictionary();
}

Dictionary OfsPhysics::params_values() const {
    if (!ready_) return {};
    return json_to_variant(ofs::phys::physics_params_values(params_));
}

Array OfsPhysics::opening_rack(int64_t seed) const {
    Array out;
    if (!ready_) return out;
    for (const ofs::phys::BallState &b : ofs::phys::opening_rack(static_cast<uint64_t>(seed), params_).balls) {
        out.push_back(ball_entry(b));
    }
    return out;
}

String OfsPhysics::member_json(const String &json_text, const String &key) {
    const nlohmann::json j = nlohmann::json::parse(to_std(json_text), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return String();
    const auto it = j.find(to_std(key));
    if (it == j.end()) return String();
    return from_std(it->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}
