#include "ofs/physics/serialize.hpp"

#include <array>
#include <cmath>

namespace ofs::phys {

namespace {

constexpr std::array<std::string_view, 5> kMotionNames = {"stationary", "spinning", "sliding", "rolling",
                                                          "pocketed"};

bool read_numbers(const nlohmann::json& j, std::size_t n, double* out, const char* what, std::string& error) {
    if (!j.is_array() || j.size() != n) {
        error = std::string(what) + ": want an array of " + std::to_string(n) + " numbers";
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!j[i].is_number() || !std::isfinite(j[i].get<double>())) {
            error = std::string(what) + ": element " + std::to_string(i) + " is not a finite number";
            return false;
        }
        out[i] = j[i].get<double>();
    }
    return true;
}

bool read_number(const nlohmann::json& j, const char* key, double& out, std::string& error) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number() || !std::isfinite(it->get<double>())) {
        error = std::string(key) + ": want a finite number";
        return false;
    }
    out = it->get<double>();
    return true;
}

}  // namespace

std::string_view motion_name(Motion m) { return kMotionNames[static_cast<std::size_t>(m)]; }

bool parse_motion(std::string_view name, Motion& out) {
    for (std::size_t i = 0; i < kMotionNames.size(); ++i) {
        if (kMotionNames[i] == name) {
            out = static_cast<Motion>(i);
            return true;
        }
    }
    return false;
}

nlohmann::json to_json(const BallState& b) {
    return {{"ball", table::ball_name(b.ball)},
            {"index", b.index},
            {"r", {b.r.x, b.r.y}},
            {"v", {b.v.x, b.v.y}},
            {"w", {b.w.x, b.w.y, b.w.z}},
            {"motion", motion_name(b.motion)},
            {"pocket", b.pocket}};
}

nlohmann::json to_json(const TableState& s) {
    nlohmann::json balls = nlohmann::json::array();
    for (const BallState& b : s.balls) balls.push_back(to_json(b));
    return {{"balls", balls}};
}

nlohmann::json to_json(const CueStrike& c) {
    return {{"aim_rad", c.aim_rad},
            {"tip_x", c.tip_x},
            {"tip_y", c.tip_y},
            {"elevation_rad", c.elevation_rad},
            {"speed_m_s", c.speed_m_s}};
}

bool from_json(const nlohmann::json& j, BallState& out, std::string& error) {
    if (!j.is_object()) {
        error = "ball: want an object";
        return false;
    }
    BallState b;
    const auto name = j.find("ball");
    if (name == j.end() || !name->is_string() || !table::parse_ball(name->get<std::string>(), b.ball)) {
        error = "ball: unknown or missing ball name";
        return false;
    }
    if (const auto index = j.find("index"); index != j.end()) {
        if (!index->is_number_integer()) {
            error = "index: want an integer";
            return false;
        }
        b.index = index->get<int>();
    }
    if (b.index < 0 || b.index > (b.ball == table::Ball::red ? 14 : 0)) {
        error = "index: " + std::to_string(b.index) + " is out of range for " + std::string(table::ball_name(b.ball));
        return false;
    }
    double r[2];
    if (!read_numbers(j.value("r", nlohmann::json()), 2, r, "r", error)) return false;
    b.r = {r[0], r[1], 0.0};
    if (const auto v = j.find("v"); v != j.end()) {
        double vv[2];
        if (!read_numbers(*v, 2, vv, "v", error)) return false;
        b.v = {vv[0], vv[1], 0.0};
    }
    if (const auto w = j.find("w"); w != j.end()) {
        double ww[3];
        if (!read_numbers(*w, 3, ww, "w", error)) return false;
        b.w = {ww[0], ww[1], ww[2]};
    }
    if (const auto motion = j.find("motion"); motion != j.end()) {
        if (!motion->is_string() || !parse_motion(motion->get<std::string>(), b.motion)) {
            error = "motion: unknown state";
            return false;
        }
    }
    if (const auto pocket = j.find("pocket"); pocket != j.end()) {
        if (!pocket->is_number_integer() || pocket->get<int>() < -1) {
            error = "pocket: want an integer >= -1";
            return false;
        }
        b.pocket = pocket->get<int>();
    }
    out = b;
    return true;
}

bool from_json(const nlohmann::json& j, TableState& out, std::string& error) {
    if (!j.is_object() || !j.contains("balls") || !j["balls"].is_array()) {
        error = "table: want {\"balls\": [...]}";
        return false;
    }
    TableState s;
    for (std::size_t i = 0; i < j["balls"].size(); ++i) {
        BallState b;
        if (!from_json(j["balls"][i], b, error)) {
            error = "balls[" + std::to_string(i) + "]: " + error;
            return false;
        }
        s.balls.push_back(b);
    }
    out = std::move(s);
    return true;
}

bool from_json(const nlohmann::json& j, CueStrike& out, std::string& error) {
    if (!j.is_object()) {
        error = "strike: want an object";
        return false;
    }
    CueStrike c;
    if (!read_number(j, "aim_rad", c.aim_rad, error) || !read_number(j, "tip_x", c.tip_x, error) ||
        !read_number(j, "tip_y", c.tip_y, error) || !read_number(j, "elevation_rad", c.elevation_rad, error) ||
        !read_number(j, "speed_m_s", c.speed_m_s, error)) {
        error = "strike: " + error;
        return false;
    }
    out = c;
    return true;
}

}  // namespace ofs::phys
