#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>

#include "ofs/log.hpp"
#include "ofs/params.hpp"
#include "ofs/physics/motion.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/physics/serialize.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace fs = std::filesystem;

bool Args::flag(const std::string& name) const {
    return std::find(flags.begin(), flags.end(), name) != flags.end();
}

const std::string* Args::value(const std::string& name) const {
    const auto it = values.find(name);
    return it == values.end() ? nullptr : &it->second;
}

bool parse_args(const std::vector<std::string>& argv, const ArgSpec& spec, Args& out, std::string& error) {
    Args args;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        const std::string& arg = argv[i];
        if (arg.size() < 3 || arg.compare(0, 2, "--") != 0) {
            args.positional.push_back(arg);
            continue;
        }
        const std::size_t eq = arg.find('=');
        const std::string name = arg.substr(2, eq == std::string::npos ? std::string::npos : eq - 2);
        const bool valued = std::find(spec.valued.begin(), spec.valued.end(), name) != spec.valued.end();
        const bool is_flag = std::find(spec.flags.begin(), spec.flags.end(), name) != spec.flags.end();
        if (!valued && !is_flag) {
            error = "unknown option --" + name;
            return false;
        }
        if (args.values.count(name) != 0 || args.flag(name)) {
            error = "--" + name + " given twice";
            return false;
        }
        if (is_flag) {
            if (eq != std::string::npos) {
                error = "--" + name + " takes no value";
                return false;
            }
            args.flags.push_back(name);
            continue;
        }
        if (eq != std::string::npos) {
            args.values[name] = arg.substr(eq + 1);
        } else if (i + 1 < argv.size()) {
            args.values[name] = argv[++i];
        } else {
            error = "--" + name + " needs a value";
            return false;
        }
    }
    if (args.positional.size() < spec.min_positional || args.positional.size() > spec.max_positional) {
        if (args.positional.size() < spec.min_positional) {
            error = "missing argument";
        } else {
            error = "unexpected argument '" + args.positional[spec.max_positional] + "'";
        }
        return false;
    }
    out = std::move(args);
    return true;
}

bool parse_u64(const std::string& text, std::uint64_t& out) {
    std::uint64_t value = 0;
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (text.empty() || result.ec != std::errc() || result.ptr != end) return false;
    out = value;
    return true;
}

bool parse_int(const std::string& text, long long& out) {
    long long value = 0;
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (text.empty() || result.ec != std::errc() || result.ptr != end) return false;
    out = value;
    return true;
}

bool parse_double(const std::string& text, double& out) {
    if (text.empty() || std::isspace(static_cast<unsigned char>(text[0]))) return false;
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || errno == ERANGE || !std::isfinite(value)) return false;
    out = value;
    return true;
}

bool option_u64(const Args& args, const std::string& name, std::uint64_t& out, std::string& error) {
    const std::string* text = args.value(name);
    if (text == nullptr) return true;
    if (!parse_u64(*text, out)) {
        error = "--" + name + ": '" + *text + "' is not a whole number >= 0";
        return false;
    }
    return true;
}

bool option_int(const Args& args, const std::string& name, long long min, long long max, long long& out,
                std::string& error) {
    const std::string* text = args.value(name);
    if (text == nullptr) return true;
    long long value = 0;
    if (!parse_int(*text, value) || value < min || value > max) {
        error = "--" + name + ": '" + *text + "' is not a whole number in [" + std::to_string(min) + ", " +
                std::to_string(max) + "]";
        return false;
    }
    out = value;
    return true;
}

bool option_double(const Args& args, const std::string& name, double min, double max, double& out,
                   std::string& error) {
    const std::string* text = args.value(name);
    if (text == nullptr) return true;
    double value = 0.0;
    if (!parse_double(*text, value) || value < min || value > max) {
        error = "--" + name + ": '" + *text + "' is not a number in [" + format("%g", min) + ", " +
                format("%g", max) + "]";
        return false;
    }
    out = value;
    return true;
}

bool read_text_file(const std::string& path, std::string& out, std::string& error) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    if (in.bad()) {
        error = "cannot read " + path;
        return false;
    }
    out = text.str();
    return true;
}

bool read_json_file(const std::string& path, nlohmann::json& out, std::string& error) {
    std::string text;
    if (!read_text_file(path, text, error)) return false;
    try {
        out = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        error = path + " is not JSON: " + e.what();
        return false;
    }
    return true;
}

bool write_text_file(const std::string& path, const std::string& text, std::string& error) {
    const fs::path file = fs::u8path(path);
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    if (ec) {
        error = "cannot create the folder for " + path + ": " + ec.message();
        return false;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

std::string data_path(const std::string& file) {
    return (fs::u8path(OFS_CORE_DATA_DIR) / fs::u8path(file)).lexically_normal().u8string();
}

std::string repo_path(const std::string& relative) {
    const fs::path root = fs::u8path(OFS_CORE_DATA_DIR).lexically_normal().parent_path().parent_path();
    return (root / fs::u8path(relative)).lexically_normal().u8string();
}

bool load_setup(const Args& args, Setup& out) {
    const std::string* params_arg = args.value("params");
    const std::string* table_arg = args.value("table");
    const std::string params_file = params_arg ? *params_arg : data_path("table_params.json");
    const std::string table_file = table_arg ? *table_arg : data_path("table_shape.json");
    bool ok = true;
    std::string error;

    nlohmann::json params_json;
    if (!read_json_file(params_file, params_json, error)) {
        std::fprintf(stderr, "params: %s\n", error.c_str());
        ok = false;
    } else {
        std::vector<std::string> problems;
        if (!phys::load_physics_params(params_json, out.params, problems)) {
            for (const std::string& problem : problems) {
                std::fprintf(stderr, "params: %s: %s\n", params_file.c_str(), problem.c_str());
            }
            ok = false;
        }
        out.params_hash = ofs::params::content_hash(nlohmann::json{{"table_params", params_json}});
    }

    nlohmann::json table_json;
    if (!read_json_file(table_file, table_json, error)) {
        std::fprintf(stderr, "table: %s\n", error.c_str());
        ok = false;
    } else {
        std::vector<std::string> problems;
        if (!phys::load_table_shape(table_json, out.table, problems)) {
            for (const std::string& problem : problems) {
                std::fprintf(stderr, "table: %s: %s\n", table_file.c_str(), problem.c_str());
            }
            if (problems.empty()) std::fprintf(stderr, "table: %s was refused\n", table_file.c_str());
            ok = false;
        }
        out.table_hash = ofs::params::content_hash(table_json);
    }
    return ok;
}

namespace {

int default_cue_ball(const phys::TableState& s) {
    for (std::size_t i = 0; i < s.balls.size(); ++i) {
        if (s.balls[i].ball == ofs::table::Ball::cue) return static_cast<int>(i);
    }
    return 0;
}

}  // namespace

bool load_position(const std::string& spec, const phys::PhysicsParams& p, Position& out, std::string& error) {
    Position pos;
    if (spec.compare(0, 5, "rack:") == 0) {
        std::uint64_t seed = 0;
        if (!parse_u64(spec.substr(5), seed)) {
            error = "'" + spec + "': want rack:SEED with a whole number seed";
            return false;
        }
        pos.name = spec;
        pos.start = phys::opening_rack(seed, p);
        pos.cue_ball = default_cue_ball(pos.start);
        pos.has_strike = true;
        pos.strike = phys::to_json(break_off_strike(pos.start, 8.0));
        out = std::move(pos);
        return true;
    }
    nlohmann::json j;
    if (!read_json_file(spec, j, error)) return false;
    if (!j.is_object()) {
        error = spec + ": want a JSON object";
        return false;
    }
    const nlohmann::json& table = j.contains("start") ? j["start"] : j;
    if (!phys::from_json(table, pos.start, error)) {
        error = spec + ": " + error;
        return false;
    }
    pos.name = j.contains("name") && j["name"].is_string() ? j["name"].get<std::string>()
                                                           : fs::u8path(spec).stem().u8string();
    pos.cue_ball = default_cue_ball(pos.start);
    if (const auto it = j.find("cue_ball"); it != j.end()) {
        if (!it->is_number_integer()) {
            error = spec + ": cue_ball: want an integer";
            return false;
        }
        pos.cue_ball = it->get<int>();
    }
    if (const auto it = j.find("strike"); it != j.end()) {
        if (!it->is_object()) {
            error = spec + ": strike: want an object";
            return false;
        }
        pos.has_strike = true;
        pos.strike = *it;
    }
    out = std::move(pos);
    return true;
}

phys::CueStrike break_off_strike(const phys::TableState& rack, double speed_m_s) {
    phys::CueStrike c;
    c.speed_m_s = speed_m_s;
    c.tip_y = 0.1;
    c.elevation_rad = 0.05;
    const int cue = default_cue_ball(rack);
    if (rack.balls.empty()) return c;
    const Vec3 from = rack.balls[static_cast<std::size_t>(cue)].r;
    // The back row is the reds furthest from baulk; take its end on the cue ball's side.
    const phys::BallState* target = nullptr;
    double back = -std::numeric_limits<double>::infinity();
    for (const phys::BallState& b : rack.balls) {
        if (b.ball == ofs::table::Ball::red) back = std::max(back, b.r.x);
    }
    const double side = from.y > 0.0 ? 1.0 : -1.0;
    for (const phys::BallState& b : rack.balls) {
        if (b.ball != ofs::table::Ball::red || b.r.x < back - 1e-6) continue;
        if (target == nullptr || b.r.y * side > target->r.y * side) target = &b;
    }
    if (target == nullptr) return c;
    const Vec3 dir = ofs::unit_or_zero(target->r - from);
    Vec3 perp{-dir.y, dir.x, 0.0};
    if (perp.y * side < 0.0) perp = -perp;
    // Passing 0.92 of a contact width outside the red's centre: a thin hit on its outer side.
    const Vec3 aim_point = target->r + perp * (2.0 * kR * 0.92);
    c.aim_rad = std::atan2(aim_point.y - from.y, aim_point.x - from.x);
    c.tip_x = 0.3 * -side;
    return c;
}

namespace {

const std::vector<phys::Segment>* segments_of(const phys::ShotRecord& r, int ball) {
    if (ball < 0 || static_cast<std::size_t>(ball) >= r.segments.size()) return nullptr;
    const auto& segments = r.segments[static_cast<std::size_t>(ball)];
    return segments.empty() ? nullptr : &segments;
}

phys::BallState fallback_state(const phys::ShotRecord& r, int ball) {
    if (ball >= 0 && static_cast<std::size_t>(ball) < r.final_state.balls.size()) {
        return r.final_state.balls[static_cast<std::size_t>(ball)];
    }
    return {};
}

phys::BallState evolve_segment(const phys::ShotRecord& r, const phys::Segment& s, double t) {
    if (t <= s.t0) return s.start;
    return phys::evolve(s.start, t - s.t0, r.params, s.nap);
}

}  // namespace

phys::BallState state_after(const phys::ShotRecord& r, int ball, double t) {
    const auto* segments = segments_of(r, ball);
    if (segments == nullptr) return fallback_state(r, ball);
    auto it = std::upper_bound(segments->begin(), segments->end(), t,
                               [](double time, const phys::Segment& s) { return time < s.t0; });
    if (it == segments->begin()) return segments->front().start;
    return evolve_segment(r, *(it - 1), t);
}

phys::BallState state_before(const phys::ShotRecord& r, int ball, double t) {
    const auto* segments = segments_of(r, ball);
    if (segments == nullptr) return fallback_state(r, ball);
    auto it = std::lower_bound(segments->begin(), segments->end(), t,
                               [](const phys::Segment& s, double time) { return s.t0 < time; });
    if (it == segments->begin()) return segments->front().start;
    return evolve_segment(r, *(it - 1), t);
}

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool finite(const phys::BallState& b) { return finite(b.r) && finite(b.v) && finite(b.w); }

double planar_distance(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::string format(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int size = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out;
    if (size > 0) {
        out.resize(static_cast<std::size_t>(size) + 1);
        std::vsnprintf(out.data(), out.size(), fmt, args);
        out.resize(static_cast<std::size_t>(size));
    }
    va_end(args);
    return out;
}

std::string ball_label(const phys::BallState& b) {
    std::string label(ofs::table::ball_name(b.ball));
    if (b.ball == ofs::table::Ball::red) label += " " + std::to_string(b.index);
    return label;
}

std::string pocket_label(const phys::TableShape& t, int pocket) {
    if (pocket >= 0 && static_cast<std::size_t>(pocket) < t.pockets.size()) {
        return t.pockets[static_cast<std::size_t>(pocket)].name;
    }
    return "pocket " + std::to_string(pocket);
}

std::string record_summary(const phys::ShotRecord& r) {
    int potted = 0;
    for (const phys::ShotEvent& e : r.events) potted += e.kind == phys::EventKind::pocket ? 1 : 0;
    return format("%zu events, t_end %.6f s, potted %d, anomalies %zu, sim %.0f us", r.events.size(), r.t_end,
                  potted, r.anomalies.size(), r.sim_us);
}

std::string anomaly_text(const phys::Anomaly& a) {
    std::string balls;
    for (int b : a.balls) balls += (balls.empty() ? "" : ",") + std::to_string(b);
    return format("%s at t=%.9g s, balls [%s]: %s", a.kind.c_str(), a.t, balls.c_str(), a.detail.c_str());
}

int usage_error(const std::string& command, const std::string& error) {
    std::fprintf(stderr, "%s: %s\n(ofs_sim_cli help lists every command and option)\n", command.c_str(),
                 error.c_str());
    return kExitUsage;
}

void configure_logger() {
#if defined(_WIN32)
    const char* platform = "windows";
#elif defined(__APPLE__)
    const char* platform = "macos";
#else
    const char* platform = "linux";
#endif
    ofs::log::Logger::instance().configure({ofs::log::new_session_id(), "sim_cli", platform});
}

double microseconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
}

Stats stats_of(std::vector<double> values) {
    Stats s;
    if (values.empty()) return s;
    std::sort(values.begin(), values.end());
    double sum = 0.0;
    for (double v : values) sum += v;
    s.mean = sum / static_cast<double>(values.size());
    const auto quantile = [&values](double q) {
        const double pos = q * static_cast<double>(values.size() - 1);
        const std::size_t lo = static_cast<std::size_t>(pos);
        const std::size_t hi = std::min(lo + 1, values.size() - 1);
        return values[lo] + (values[hi] - values[lo]) * (pos - static_cast<double>(lo));
    };
    s.p50 = quantile(0.5);
    s.p99 = quantile(0.99);
    s.max = values.back();
    return s;
}

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

double planar_distance(const ofs::table::Vec2& a, const Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Whether `angle` lies in [start, end] going anticlockwise, as table_shape.hpp defines arcs.
bool angle_in_arc(double angle, double start, double end) {
    double span = std::fmod(end - start, kTwoPi);
    if (span < 0.0) span += kTwoPi;
    if (span == 0.0 && end != start) span = kTwoPi;
    double offset = std::fmod(angle - start, kTwoPi);
    if (offset < 0.0) offset += kTwoPi;
    return offset <= span + 1e-12;
}

}  // namespace

PlayArea::PlayArea(const phys::TableShape& t) : table_(t) {
    double x0 = -ofs::table::kPlayingLength / 2.0;
    double x1 = ofs::table::kPlayingLength / 2.0;
    double y0 = -ofs::table::kPlayingWidth / 2.0;
    double y1 = ofs::table::kPlayingWidth / 2.0;
    if (!t.lines.empty()) {
        x0 = y0 = std::numeric_limits<double>::infinity();
        x1 = y1 = -std::numeric_limits<double>::infinity();
        for (const phys::LineCushion& line : t.lines) {
            for (const ofs::table::Vec2& p : {line.p1, line.p2}) {
                x0 = std::min(x0, p.x);
                x1 = std::max(x1, p.x);
                y0 = std::min(y0, p.y);
                y1 = std::max(y1, p.y);
            }
        }
    }
    centre_x_ = (x0 + x1) / 2.0;
    centre_y_ = (y0 + y1) / 2.0;
    half_x_ = (x1 - x0) / 2.0 - kR;
    half_y_ = (y1 - y0) / 2.0 - kR;
    for (std::size_t k = 0; k < t.pockets.size(); ++k) {
        const phys::Pocket& pocket = t.pockets[k];
        double reach = pocket.drop_radius;
        for (const phys::ArcCushion& arc : t.arcs) {
            if (arc.pocket != static_cast<int>(k)) continue;
            const Vec3 centre{arc.centre.x, arc.centre.y, 0.0};
            reach = std::max(reach, planar_distance(pocket.drop_centre, centre) + arc.radius);
        }
        // A ball in the mouth sits between the jaws, within a ball's width of their outer edge.
        zone_radius_.push_back(reach + 2.0 * kR);
    }
}

double PlayArea::line_distance(std::size_t k, const Vec3& c, bool& on_segment) const {
    const phys::LineCushion& line = table_.lines[k];
    const double dx = line.p2.x - line.p1.x;
    const double dy = line.p2.y - line.p1.y;
    const double length = std::sqrt(dx * dx + dy * dy);
    const double rx = c.x - line.p1.x;
    const double ry = c.y - line.p1.y;
    const double along = length > 0.0 ? (rx * dx + ry * dy) / length : 0.0;
    on_segment = along >= 0.0 && along <= length;
    return rx * line.normal.x + ry * line.normal.y;
}

bool PlayArea::placement_ok(const Vec3& c) const {
    if (std::fabs(c.x - centre_x_) > half_x_ || std::fabs(c.y - centre_y_) > half_y_) return false;
    for (std::size_t k = 0; k < table_.lines.size(); ++k) {
        bool on_segment = false;
        const double d = line_distance(k, c, on_segment);
        if (on_segment && d < kR) return false;
        const phys::LineCushion& line = table_.lines[k];
        if (planar_distance(line.p1, c) < kR || planar_distance(line.p2, c) < kR) return false;
    }
    for (const phys::ArcCushion& arc : table_.arcs) {
        if (planar_distance(arc.centre, c) < arc.radius + kR) return false;
    }
    for (const phys::Pocket& pocket : table_.pockets) {
        if (planar_distance(pocket.drop_centre, c) < pocket.drop_radius + kR) return false;
    }
    return true;
}

std::string PlayArea::violation(const Vec3& c, double tol) const {
    for (std::size_t k = 0; k < table_.lines.size(); ++k) {
        bool on_segment = false;
        const double d = line_distance(k, c, on_segment);
        if (!on_segment || d >= kR - tol) continue;
        if (d < 0.0) return format("behind cushion line %zu (%.3g m past its nose)", k, -d);
        return format("into cushion line %zu by %.3g m", k, kR - d);
    }
    for (std::size_t k = 0; k < table_.arcs.size(); ++k) {
        const phys::ArcCushion& arc = table_.arcs[k];
        const double d = planar_distance(arc.centre, c);
        if (d < arc.radius - tol) {
            return format("inside jaw %zu of %s", k, pocket_label(table_, arc.pocket).c_str());
        }
        const double angle = std::atan2(c.y - arc.centre.y, c.x - arc.centre.x);
        if (d < arc.radius + kR - tol && angle_in_arc(angle, arc.angle_start, arc.angle_end)) {
            return format("into jaw %zu of %s by %.3g m", k, pocket_label(table_, arc.pocket).c_str(),
                          arc.radius + kR - d);
        }
    }
    for (std::size_t k = 0; k < table_.pockets.size(); ++k) {
        const phys::Pocket& pocket = table_.pockets[k];
        if (planar_distance(pocket.drop_centre, c) < pocket.drop_radius - tol) {
            return "in the drop circle of " + pocket.name + " but not potted";
        }
    }
    if (std::fabs(c.x - centre_x_) > half_x_ + tol || std::fabs(c.y - centre_y_) > half_y_ + tol) {
        for (std::size_t k = 0; k < table_.pockets.size(); ++k) {
            if (planar_distance(table_.pockets[k].drop_centre, c) <= zone_radius_[k]) return {};
        }
        return format("outside the playing area at (%.9g, %.9g)", c.x, c.y);
    }
    return {};
}

}  // namespace sim_cli
