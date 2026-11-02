#include "ofs/physics/table_shape.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

#include "ofs/log.hpp"
#include "ofs/params.hpp"

namespace ofs::phys {

namespace {

using nlohmann::json;
using table::Vec2;

constexpr double kTwoPi = 6.283185307179586;
// Joints must meet this closely, in metres and radians, for the outline to have no gap or kink.
constexpr double kJoinTol = 1e-9;
constexpr double kAngleTol = 1e-9;

// The order of TableShape::pockets, whatever the file's order.
constexpr std::array<const char*, 6> kPocketNames = {"top_left",    "top_right",   "middle_left",
                                                     "middle_right", "bottom_left", "bottom_right"};

int pocket_index(const std::string& name) {
    for (std::size_t i = 0; i < kPocketNames.size(); ++i) {
        if (name == kPocketNames[i]) return static_cast<int>(i);
    }
    return -1;
}

class Reader {
public:
    explicit Reader(std::vector<std::string>& problems) : problems_(problems) {}

    void problem(const std::string& where, const std::string& what) { problems_.push_back(where + ": " + what); }

    bool number(const json& obj, const char* key, const std::string& where, double& out) {
        const auto it = obj.find(key);
        if (it == obj.end()) {
            problem(where, std::string("missing \"") + key + "\"");
            return false;
        }
        if (!it->is_number() || !std::isfinite(it->get<double>())) {
            problem(where, std::string("\"") + key + "\" is not a finite number");
            return false;
        }
        out = it->get<double>();
        return true;
    }

    bool positive(const json& obj, const char* key, const std::string& where, double& out) {
        if (!number(obj, key, where, out)) return false;
        if (out > 0.0) return true;
        problem(where, std::string("\"") + key + "\" must be positive");
        return false;
    }

    bool vec2(const json& obj, const char* key, const std::string& where, Vec2& out) {
        const auto it = obj.find(key);
        if (it == obj.end()) {
            problem(where, std::string("missing \"") + key + "\"");
            return false;
        }
        if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number() ||
            !std::isfinite((*it)[0].get<double>()) || !std::isfinite((*it)[1].get<double>())) {
            problem(where, std::string("\"") + key + "\" is not [x, y] with finite numbers");
            return false;
        }
        out = {(*it)[0].get<double>(), (*it)[1].get<double>()};
        return true;
    }

    bool string(const json& obj, const char* key, const std::string& where, std::string& out) {
        const auto it = obj.find(key);
        if (it == obj.end() || !it->is_string()) {
            problem(where, std::string("missing or non-string \"") + key + "\"");
            return false;
        }
        out = it->get<std::string>();
        return true;
    }

private:
    std::vector<std::string>& problems_;
};

std::string label(const char* list, std::size_t i, const json& item) {
    std::string out = std::string(list) + "[" + std::to_string(i) + "]";
    const auto name = item.find("name");
    if (name != item.end() && name->is_string()) out += " (" + name->get<std::string>() + ")";
    return out;
}

double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

Vec2 arc_point(const ArcCushion& a, double angle) {
    return {a.centre.x + a.radius * std::cos(angle), a.centre.y + a.radius * std::sin(angle)};
}

enum class Kind { nose, facing };

// Checks that each arc end meets a line end with the line's normal equal to the arc's outward
// radius there (tangent, no kink) and the line running on away from the arc; that each nose line
// meets an arc at both ends and each facing at one.
void check_joints(const TableShape& s, const std::vector<std::string>& line_names, const std::vector<Kind>& kinds,
                  const std::vector<std::string>& arc_names, Reader& r) {
    std::vector<int> joined(s.lines.size(), 0);
    for (std::size_t ai = 0; ai < s.arcs.size(); ++ai) {
        const ArcCushion& a = s.arcs[ai];
        for (int end = 0; end < 2; ++end) {
            const double angle = end == 0 ? a.angle_start : a.angle_end;
            const Vec2 p = arc_point(a, angle);
            const Vec2 radial{std::cos(angle), std::sin(angle)};
            // Leaving the arc: clockwise from its start, anticlockwise from its end.
            const Vec2 away = end == 0 ? Vec2{radial.y, -radial.x} : Vec2{-radial.y, radial.x};
            const std::string where = arc_names[ai] + (end == 0 ? " start" : " end");
            int found = 0;
            for (std::size_t li = 0; li < s.lines.size(); ++li) {
                const LineCushion& l = s.lines[li];
                for (int le = 0; le < 2; ++le) {
                    const Vec2 q = le == 0 ? l.p1 : l.p2;
                    const Vec2 other = le == 0 ? l.p2 : l.p1;
                    if (dist(p, q) > kJoinTol) continue;
                    ++found;
                    ++joined[li];
                    const double cross = l.normal.x * radial.y - l.normal.y * radial.x;
                    const double dot = l.normal.x * radial.x + l.normal.y * radial.y;
                    if (std::abs(cross) > kAngleTol || dot <= 0.0) {
                        r.problem(where, "meets " + line_names[li] + " at an angle (not tangent)");
                    }
                    if ((other.x - q.x) * away.x + (other.y - q.y) * away.y <= 0.0) {
                        r.problem(where, "meets " + line_names[li] + ", which doubles back over the arc");
                    }
                }
            }
            if (found == 0) r.problem(where, "is not joined to a line end (a gap in the outline)");
            if (found > 1) r.problem(where, "is joined to more than one line end");
        }
    }
    for (std::size_t li = 0; li < s.lines.size(); ++li) {
        if (kinds[li] == Kind::nose && joined[li] != 2) {
            r.problem(line_names[li], "a nose line must end in a jaw at both ends");
        }
        if (kinds[li] == Kind::facing && joined[li] != 1) {
            r.problem(line_names[li], "a facing must start at exactly one jaw");
        }
    }
}

bool refuse(const json& file, const std::vector<std::string>& problems, std::size_t first) {
    const std::vector<std::string> found(problems.begin() + static_cast<std::ptrdiff_t>(first), problems.end());
    log::error("table", "table.invalid", {{"hash", params::content_hash(file)}, {"problems", found}});
    return false;
}

}  // namespace

// `out` is only written when the file has no problems.
bool load_table_shape(const json& file, TableShape& out, std::vector<std::string>& problems) {
    const std::size_t problems_before = problems.size();
    Reader r(problems);
    if (!file.is_object()) {
        r.problem("table_shape", "not a JSON object");
        return refuse(file, problems, problems_before);
    }
    TableShape s;
    double version = 0.0;
    if (r.number(file, "schema_version", "table_shape", version) && version != 1.0) {
        r.problem("table_shape", "schema_version must be 1");
    }
    r.positive(file, "rail_height_m", "table_shape", s.rail_height_m);
    r.positive(file, "rail_width_m", "table_shape", s.rail_width_m);

    s.pockets.resize(kPocketNames.size());
    std::array<bool, kPocketNames.size()> seen{};
    const auto pockets = file.find("pockets");
    if (pockets == file.end() || !pockets->is_array()) {
        r.problem("table_shape", "missing \"pockets\" array");
    } else {
        for (std::size_t i = 0; i < pockets->size(); ++i) {
            const json& item = (*pockets)[i];
            const std::string where = label("pockets", i, item);
            if (!item.is_object()) {
                r.problem(where, "not an object");
                continue;
            }
            std::string name;
            if (!r.string(item, "name", where, name)) continue;
            const int index = pocket_index(name);
            if (index < 0) {
                r.problem(where, "unknown pocket name \"" + name + "\"");
                continue;
            }
            if (seen[static_cast<std::size_t>(index)]) {
                r.problem(where, "pocket \"" + name + "\" appears twice");
                continue;
            }
            seen[static_cast<std::size_t>(index)] = true;
            Pocket& p = s.pockets[static_cast<std::size_t>(index)];
            p.name = name;
            const bool corner = name.rfind("middle", 0) != 0;
            const auto c = item.find("corner");
            if (c == item.end() || !c->is_boolean()) {
                r.problem(where, "missing or non-boolean \"corner\"");
            } else if (c->get<bool>() != corner) {
                r.problem(where,
                          std::string("\"corner\" must be ") + (corner ? "true" : "false") + " for " + name);
            }
            p.corner = corner;
            r.vec2(item, "drop_centre", where, p.drop_centre);
            r.positive(item, "drop_radius", where, p.drop_radius);
        }
    }
    for (std::size_t i = 0; i < kPocketNames.size(); ++i) {
        if (!seen[i]) r.problem("pockets", std::string("pocket \"") + kPocketNames[i] + "\" is missing");
    }

    std::vector<std::string> line_names;
    std::vector<Kind> kinds;
    const auto lines = file.find("lines");
    if (lines == file.end() || !lines->is_array()) {
        r.problem("table_shape", "missing \"lines\" array");
    } else {
        for (std::size_t i = 0; i < lines->size(); ++i) {
            const json& item = (*lines)[i];
            const std::string where = label("lines", i, item);
            if (!item.is_object()) {
                r.problem(where, "not an object");
                continue;
            }
            LineCushion l;
            bool ok = r.vec2(item, "p1", where, l.p1);
            ok = r.vec2(item, "p2", where, l.p2) && ok;
            ok = r.vec2(item, "normal", where, l.normal) && ok;
            std::string kind;
            if (r.string(item, "kind", where, kind) && kind != "nose" && kind != "facing") {
                r.problem(where, "\"kind\" must be \"nose\" or \"facing\"");
                ok = false;
            }
            if (!ok) continue;
            const double n = std::hypot(l.normal.x, l.normal.y);
            const double len = dist(l.p1, l.p2);
            if (std::abs(n - 1.0) > kAngleTol) {
                r.problem(where, "normal is not a unit vector (length " + std::to_string(n) + ")");
                continue;
            }
            if (len <= kJoinTol) {
                r.problem(where, "p1 and p2 coincide");
                continue;
            }
            const double along = (l.normal.x * (l.p2.x - l.p1.x) + l.normal.y * (l.p2.y - l.p1.y)) / len;
            if (std::abs(along) > kAngleTol) r.problem(where, "normal is not perpendicular to the segment");
            s.lines.push_back(l);
            line_names.push_back(where);
            kinds.push_back(kind == "nose" ? Kind::nose : Kind::facing);
        }
    }

    std::vector<std::string> arc_names;
    std::array<int, kPocketNames.size()> jaws{};
    const auto arcs = file.find("arcs");
    if (arcs == file.end() || !arcs->is_array()) {
        r.problem("table_shape", "missing \"arcs\" array");
    } else {
        for (std::size_t i = 0; i < arcs->size(); ++i) {
            const json& item = (*arcs)[i];
            const std::string where = label("arcs", i, item);
            if (!item.is_object()) {
                r.problem(where, "not an object");
                continue;
            }
            ArcCushion a;
            bool ok = r.vec2(item, "centre", where, a.centre);
            ok = r.positive(item, "radius", where, a.radius) && ok;
            ok = r.number(item, "angle_start", where, a.angle_start) && ok;
            ok = r.number(item, "angle_end", where, a.angle_end) && ok;
            const auto pocket = item.find("pocket");
            if (pocket == item.end()) {
                r.problem(where, "has no pocket");
                ok = false;
            } else if (!pocket->is_string() || pocket_index(pocket->get<std::string>()) < 0) {
                r.problem(where, "names no known pocket (" + pocket->dump() + ")");
                ok = false;
            } else {
                a.pocket = pocket_index(pocket->get<std::string>());
            }
            if (!ok) continue;
            const double sweep = a.angle_end - a.angle_start;
            if (!(sweep > 0.0 && sweep < kTwoPi)) {
                r.problem(where, "angle_end must exceed angle_start by less than a full turn");
                continue;
            }
            ++jaws[static_cast<std::size_t>(a.pocket)];
            s.arcs.push_back(a);
            arc_names.push_back(where);
        }
    }
    for (std::size_t i = 0; i < kPocketNames.size(); ++i) {
        if (seen[i] && jaws[i] != 2) {
            r.problem("arcs", std::string("pocket \"") + kPocketNames[i] + "\" has " + std::to_string(jaws[i]) +
                                  " jaws, not 2");
        }
    }

    if (problems.size() == problems_before) check_joints(s, line_names, kinds, arc_names, r);
    if (problems.size() != problems_before) return refuse(file, problems, problems_before);
    out = std::move(s);
    return true;
}

}  // namespace ofs::phys
