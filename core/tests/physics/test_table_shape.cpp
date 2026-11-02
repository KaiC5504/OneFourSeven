#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/log_schema.hpp"
#include "ofs/params.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/rng.hpp"

using namespace ofs;
using namespace ofs::phys;
using table::Vec2;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double R = table::kBallRadius;
constexpr double kHalfL = table::kPlayingLength / 2.0;
constexpr double kHalfW = table::kPlayingWidth / 2.0;

nlohmann::json read_json(const std::string& path) {
    std::ifstream in(path);
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

nlohmann::json shape_file() { return read_json(std::string(OFS_CORE_DATA_DIR) + "/table_shape.json"); }

TableShape load_good() {
    TableShape s;
    std::vector<std::string> problems;
    const bool ok = load_table_shape(shape_file(), s, problems);
    for (const auto& p : problems) MESSAGE(p);
    REQUIRE(ok);
    return s;
}

bool has_problem(const std::vector<std::string>& problems, const std::string& needle) {
    return std::any_of(problems.begin(), problems.end(),
                       [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
Vec2 sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 add(Vec2 a, Vec2 b, double t) { return {a.x + b.x * t, a.y + b.y * t}; }
double len(Vec2 a) { return std::hypot(a.x, a.y); }

// Anticlockwise from angle_start, so arcs that pass +-pi work.
bool in_arc(double angle, const ArcCushion& a) {
    double rel = std::fmod(angle - a.angle_start, 2.0 * kPi);
    if (rel < 0.0) rel += 2.0 * kPi;
    return rel <= a.angle_end - a.angle_start + 1e-12;
}

double outside_rectangle_by(Vec2 p) {
    return std::hypot(std::max(0.0, std::abs(p.x) - kHalfL), std::max(0.0, std::abs(p.y) - kHalfW));
}

enum class Hit { none, line, arc, pocket };

struct RayHit {
    double t = std::numeric_limits<double>::infinity();
    Hit kind = Hit::none;
    int index = -1;
    Vec2 normal;
};

// Entering root of |o + t d - c| = radius; infinity when the ray misses or starts inside.
double enter_circle(Vec2 o, Vec2 d, Vec2 c, double radius) {
    const Vec2 oc = sub(o, c);
    const double b = dot(oc, d);
    const double disc = b * b - (dot(oc, oc) - radius * radius);
    if (disc < 0.0) return std::numeric_limits<double>::infinity();
    const double t = -b - std::sqrt(disc);
    return t >= 0.0 ? t : std::numeric_limits<double>::infinity();
}

// The first surface a ball centre at o moving along unit d meets: cushions offset by R, drop
// circles as they are.
RayHit first_hit(const TableShape& s, Vec2 o, Vec2 d) {
    RayHit best;
    for (std::size_t i = 0; i < s.lines.size(); ++i) {
        const LineCushion& l = s.lines[i];
        const double dn = dot(d, l.normal);
        if (dn >= 0.0) continue;
        const Vec2 a = add(l.p1, l.normal, R);
        const Vec2 b = add(l.p2, l.normal, R);
        const double t = dot(sub(a, o), l.normal) / dn;
        if (t < 0.0 || t >= best.t) continue;
        const Vec2 q = add(o, d, t);
        const Vec2 ab = sub(b, a);
        const double u = dot(sub(q, a), ab) / dot(ab, ab);
        if (u < 0.0 || u > 1.0) continue;
        best = {t, Hit::line, static_cast<int>(i), l.normal};
    }
    for (std::size_t i = 0; i < s.arcs.size(); ++i) {
        const ArcCushion& a = s.arcs[i];
        const double t = enter_circle(o, d, a.centre, a.radius + R);
        if (t >= best.t) continue;
        const Vec2 q = add(o, d, t);
        const Vec2 n = sub(q, a.centre);
        if (!in_arc(std::atan2(n.y, n.x), a)) continue;
        best = {t, Hit::arc, static_cast<int>(i), {n.x / len(n), n.y / len(n)}};
    }
    for (std::size_t i = 0; i < s.pockets.size(); ++i) {
        const double t = enter_circle(o, d, s.pockets[i].drop_centre, s.pockets[i].drop_radius);
        if (t < best.t) best = {t, Hit::pocket, static_cast<int>(i), {}};
    }
    return best;
}

const ArcCushion* jaw_on_side(const TableShape& s, int pocket, Vec2 towards) {
    const ArcCushion* best = nullptr;
    for (const ArcCushion& a : s.arcs) {
        if (a.pocket == pocket && (!best || dot(a.centre, towards) > dot(best->centre, towards))) best = &a;
    }
    return best;
}

}  // namespace

TEST_CASE("table shape: the outline loads with no problems") {
    const TableShape s = load_good();
    REQUIRE(s.pockets.size() == 6);
    const std::array<const char*, 6> names = {"top_left",    "top_right",   "middle_left",
                                              "middle_right", "bottom_left", "bottom_right"};
    for (std::size_t i = 0; i < names.size(); ++i) CHECK(s.pockets[i].name == names[i]);
    CHECK(s.lines.size() == 18);
    CHECK(s.arcs.size() == 12);
    CHECK(s.rail_height_m > 0.0);
    CHECK(s.rail_width_m > 0.0);
}

TEST_CASE("table shape: every dimension has a source and the outline matches the dimensions") {
    // Spec 6.3: the geometry is recorded in one data file with its sources.
    const nlohmann::json file = shape_file();
    const nlohmann::json& dims = file.at("dimensions");
    for (auto it = dims.begin(); it != dims.end(); ++it) {
        INFO(it.key());
        REQUIRE(file.at("docs").contains(it.key()));
        const nlohmann::json& doc = file.at("docs").at(it.key());
        CHECK(!doc.at("source").get<std::string>().empty());
        CHECK(!doc.at("unit").get<std::string>().empty());
        CHECK(!doc.at("note").get<std::string>().empty());
    }
    const TableShape s = load_good();
    CHECK(dims.at("playing_length_m").get<double>() == table::kPlayingLength);
    CHECK(dims.at("playing_width_m").get<double>() == table::kPlayingWidth);
    CHECK(s.rail_width_m == dims.at("rail_width_m").get<double>());
    CHECK(s.rail_height_m == dims.at("rail_height_m").get<double>());

    const nlohmann::json& lines = file.at("lines");
    int noses = 0;
    for (std::size_t i = 0; i < s.lines.size(); ++i) {
        if (lines[i].at("kind") != "nose") continue;
        ++noses;
        const LineCushion& l = s.lines[i];
        // On the playing area's edge, facing in.
        const double edge = dot(l.p1, {-l.normal.x, -l.normal.y});
        CHECK(edge == doctest::Approx(std::abs(l.normal.x) > 0.5 ? kHalfL : kHalfW).epsilon(1e-12));
        CHECK(dot(l.p2, {-l.normal.x, -l.normal.y}) == doctest::Approx(edge).epsilon(1e-12));
    }
    CHECK(noses == 6);

    for (std::size_t pi = 0; pi < s.pockets.size(); ++pi) {
        const Pocket& p = s.pockets[pi];
        INFO(p.name);
        const double gap = dims.at(p.corner ? "corner_gap_m" : "middle_gap_m").get<double>();
        const double jaw = dims.at(p.corner ? "corner_jaw_radius_m" : "middle_jaw_radius_m").get<double>();
        std::vector<const LineCushion*> facings;
        for (std::size_t i = 0; i < s.lines.size(); ++i) {
            const std::string name = lines[i].at("name").get<std::string>();
            if (name.rfind(p.name + "_facing", 0) == 0) facings.push_back(&s.lines[i]);
        }
        REQUIRE(facings.size() == 2);
        // Parallel, facing each other, gap apart.
        CHECK(dot(facings[0]->normal, facings[1]->normal) == doctest::Approx(-1.0).epsilon(1e-12));
        CHECK(dot(sub(facings[1]->p1, facings[0]->p1), facings[0]->normal) == doctest::Approx(gap).epsilon(1e-9));
        for (const ArcCushion& a : s.arcs) {
            if (a.pocket == static_cast<int>(pi)) CHECK(a.radius == jaw);
        }
        CHECK(p.drop_radius == doctest::Approx(gap / 2.0).epsilon(1e-12));
    }
}

TEST_CASE("table shape: the outline is closed and continuous") {
    // Each jaw meets its lines tangentially (within 1e-9 m and 1e-9 rad), with no gap or kink a
    // ball could slip through (spec 6.3, 7.5).
    const TableShape s = load_good();
    const nlohmann::json lines = shape_file().at("lines");
    std::vector<int> joins(s.lines.size(), 0);
    for (const ArcCushion& a : s.arcs) {
        for (double angle : {a.angle_start, a.angle_end}) {
            const Vec2 radial{std::cos(angle), std::sin(angle)};
            const Vec2 end = add(a.centre, radial, a.radius);
            int found = 0;
            for (std::size_t i = 0; i < s.lines.size(); ++i) {
                const LineCushion& l = s.lines[i];
                for (Vec2 q : {l.p1, l.p2}) {
                    if (len(sub(q, end)) > 1e-9) continue;
                    ++found;
                    ++joins[i];
                    const double cross = l.normal.x * radial.y - l.normal.y * radial.x;
                    const double kink = std::atan2(cross, dot(l.normal, radial));
                    CHECK(std::abs(kink) <= 1e-9);
                }
            }
            CHECK(found == 1);
        }
    }
    for (std::size_t i = 0; i < s.lines.size(); ++i) {
        INFO(lines[i].at("name").get<std::string>());
        CHECK(joins[i] == (lines[i].at("kind") == "nose" ? 2 : 1));
    }
    // Going round the playing area, the nose lines and jaws cover every edge but the pockets.
    double nose_length = 0.0;
    for (std::size_t i = 0; i < s.lines.size(); ++i) {
        if (lines[i].at("kind") == "nose") nose_length += len(sub(s.lines[i].p2, s.lines[i].p1));
    }
    CHECK(nose_length < 2.0 * (table::kPlayingLength + table::kPlayingWidth));
}

TEST_CASE("table shape: every pocket has two jaws and a drop circle behind its mouth") {
    // Spec 7.6: potted when the centre passes the drop boundary, a region behind the jaws.
    const TableShape s = load_good();
    for (std::size_t pi = 0; pi < s.pockets.size(); ++pi) {
        const Pocket& p = s.pockets[pi];
        INFO(p.name);
        int jaws = 0;
        for (const ArcCushion& a : s.arcs) jaws += a.pocket == static_cast<int>(pi) ? 1 : 0;
        CHECK(jaws == 2);
        // Outside the playing rectangle, so no ball on the cloth is potted.
        CHECK(outside_rectangle_by(p.drop_centre) > p.drop_radius);
        // Behind the mouth: further out than both jaws' centres along the pocket's axis.
        const double sy = std::copysign(1.0, p.drop_centre.y);
        const Vec2 axis = p.corner ? Vec2{std::copysign(1.0, p.drop_centre.x), sy} : Vec2{0.0, sy};
        for (const ArcCushion& a : s.arcs) {
            if (a.pocket == static_cast<int>(pi)) CHECK(dot(p.drop_centre, axis) > dot(a.centre, axis));
        }
    }
}

TEST_CASE("table shape: corner and middle pockets differ, and the middle pockets are set back") {
    // Spec 6.3.
    const TableShape s = load_good();
    const nlohmann::json dims = shape_file().at("dimensions");
    CHECK(dims.at("middle_gap_m").get<double>() > dims.at("corner_gap_m").get<double>());
    CHECK(dims.at("middle_jaw_radius_m").get<double>() != dims.at("corner_jaw_radius_m").get<double>());
    const Pocket& corner = s.pockets[0];
    const Pocket& middle = s.pockets[2];
    REQUIRE(corner.corner);
    REQUIRE(!middle.corner);
    // How far behind the playing area's edge (the corner point for a corner) the ball centre must
    // get, on the pocket's axis, before it drops.
    const double corner_depth = len(sub(corner.drop_centre, {kHalfL, kHalfW})) - corner.drop_radius;
    const double middle_depth = std::abs(middle.drop_centre.y) - middle.drop_radius - kHalfW;
    MESSAGE("drop depth behind the edge: corner " << corner_depth * 1000.0 << " mm, middle "
                                                  << middle_depth * 1000.0 << " mm");
    CHECK(middle_depth > corner_depth);
}

TEST_CASE("table shape: drop circles are reachable straight down each pocket's axis") {
    const TableShape s = load_good();
    for (std::size_t pi = 0; pi < s.pockets.size(); ++pi) {
        const Pocket& p = s.pockets[pi];
        INFO(p.name);
        const Vec2 from = p.corner ? Vec2{std::copysign(kHalfL - 0.5, p.drop_centre.x),
                                          std::copysign(kHalfW - 0.5, p.drop_centre.y)}
                                   : Vec2{0.0, 0.0};
        const Vec2 d = sub(p.drop_centre, from);
        const RayHit h = first_hit(s, from, {d.x / len(d), d.y / len(d)});
        CHECK(h.kind == Hit::pocket);
        CHECK(h.index == static_cast<int>(pi));
    }
}

TEST_CASE("table shape: the named jaw fixtures meet what their notes say") {
    // core/tests/fixtures/positions/README.md.
    const TableShape s = load_good();
    const std::string dir = std::string(OFS_TEST_FIXTURES_DIR) + "/positions/";
    {
        const nlohmann::json f = read_json(dir + "slow_to_middle_left.json");
        const auto& r = f.at("start").at("balls")[0].at("r");
        const RayHit h = first_hit(s, {r[0].get<double>(), r[1].get<double>()}, {0.0, 1.0});
        CHECK(h.kind == Hit::pocket);
        CHECK(s.pockets[static_cast<std::size_t>(h.index)].name == "middle_left");
    }
    {
        const nlohmann::json f = read_json(dir + "towards_top_left_jaw.json");
        const auto& r = f.at("start").at("balls")[0].at("r");
        const RayHit h = first_hit(s, {r[0].get<double>(), r[1].get<double>()}, {1.0, 0.0});
        REQUIRE(h.kind == Hit::arc);
        CHECK(s.pockets[static_cast<std::size_t>(s.arcs[static_cast<std::size_t>(h.index)].pocket)].name ==
              "top_left");
        CHECK(&s.arcs[static_cast<std::size_t>(h.index)] == jaw_on_side(s, 0, {1.0, 0.0}));
    }
}

TEST_CASE("table shape: no straight path escapes the outline") {
    // >= 100,000 random rays from inside the playing area, reflected naively off the cushions
    // (offset by R) up to 40 times: each segment must meet a line, an arc or a drop circle.
    const TableShape s = load_good();
    Rng rng(6031769);
    const int rays = 100000;
    int escaped = 0;
    int still_bouncing = 0;
    std::array<int, 6> potted{};
    long line_hits = 0;
    long arc_hits = 0;
    const double box_x = kHalfL + s.rail_width_m + R;
    const double box_y = kHalfW + s.rail_width_m + R;
    for (int i = 0; i < rays; ++i) {
        Vec2 o{rng.uniform(-kHalfL + R, kHalfL - R), rng.uniform(-kHalfW + R, kHalfW - R)};
        const double heading = rng.uniform(0.0, 2.0 * kPi);
        Vec2 d{std::cos(heading), std::sin(heading)};
        bool done = false;
        for (int bounce = 0; bounce < 40 && !done; ++bounce) {
            const RayHit h = first_hit(s, o, d);
            const Vec2 q = h.kind == Hit::none ? o : add(o, d, h.t);
            if (h.kind == Hit::none || std::abs(q.x) > box_x || std::abs(q.y) > box_y) {
                ++escaped;
                done = true;
            } else if (h.kind == Hit::pocket) {
                ++potted[static_cast<std::size_t>(h.index)];
                done = true;
            } else {
                (h.kind == Hit::line ? line_hits : arc_hits) += 1;
                const double dn = dot(d, h.normal);
                d = {d.x - 2.0 * dn * h.normal.x, d.y - 2.0 * dn * h.normal.y};
                o = q;
            }
        }
        if (!done) ++still_bouncing;
    }
    int total_potted = 0;
    for (int n : potted) total_potted += n;
    MESSAGE(rays << " rays: " << total_potted << " potted (top_left " << potted[0] << ", top_right " << potted[1]
                 << ", middle_left " << potted[2] << ", middle_right " << potted[3] << ", bottom_left "
                 << potted[4] << ", bottom_right " << potted[5] << "), " << still_bouncing
                 << " still bouncing after 40, " << escaped << " escaped; " << line_hits << " line and " << arc_hits
                 << " jaw reflections");
    CHECK(escaped == 0);
    for (int n : potted) CHECK(n > 0);
}

TEST_CASE("table shape: malformed files are refused with every problem listed") {
    const nlohmann::json good = shape_file();
    SUBCASE("missing pocket, non-unit normal, arc without pocket") {
        nlohmann::json bad = good;
        auto& pockets = bad.at("pockets");
        pockets.erase(std::remove_if(pockets.begin(), pockets.end(),
                                     [](const nlohmann::json& p) { return p.at("name") == "middle_left"; }),
                      pockets.end());
        bad.at("lines")[2]["normal"] = {0.0, -1.2};
        bad.at("arcs")[0].erase("pocket");
        bad.at("arcs")[1]["pocket"] = "nowhere";
        TableShape out;
        out.rail_width_m = 123.0;
        std::vector<std::string> problems;
        CHECK_FALSE(load_table_shape(bad, out, problems));
        for (const auto& p : problems) MESSAGE(p);
        CHECK(has_problem(problems, "\"middle_left\" is missing"));
        CHECK(has_problem(problems, "not a unit vector"));
        CHECK(has_problem(problems, "has no pocket"));
        CHECK(has_problem(problems, "names no known pocket"));
        CHECK(out.rail_width_m == 123.0);
        CHECK(out.lines.empty());
    }
    SUBCASE("a gap at a joint") {
        nlohmann::json bad = good;
        bad.at("lines")[0]["p1"][1] = bad.at("lines")[0]["p1"][1].get<double>() + 0.001;
        TableShape out;
        std::vector<std::string> problems;
        CHECK_FALSE(load_table_shape(bad, out, problems));
        CHECK(has_problem(problems, "not joined to a line end"));
    }
    SUBCASE("a kink at a joint") {
        // Turn a facing about its joint with the jaw: still joined, no longer tangent.
        nlohmann::json bad = good;
        nlohmann::json& facing = bad.at("lines")[6];
        const Vec2 p1{facing["p1"][0].get<double>(), facing["p1"][1].get<double>()};
        const Vec2 p2{facing["p2"][0].get<double>(), facing["p2"][1].get<double>()};
        const Vec2 n{facing["normal"][0].get<double>(), facing["normal"][1].get<double>()};
        const double c = std::cos(0.2);
        const double sn = std::sin(0.2);
        const Vec2 d = sub(p2, p1);
        facing["p2"] = {p1.x + c * d.x - sn * d.y, p1.y + sn * d.x + c * d.y};
        facing["normal"] = {c * n.x - sn * n.y, sn * n.x + c * n.y};
        TableShape out;
        std::vector<std::string> problems;
        CHECK_FALSE(load_table_shape(bad, out, problems));
        CHECK(has_problem(problems, "not tangent"));
    }
    SUBCASE("wrong types and values") {
        nlohmann::json bad = good;
        bad["rail_width_m"] = -1.0;
        bad.at("pockets")[0]["corner"] = false;
        bad.at("pockets")[1]["drop_radius"] = "big";
        bad.at("arcs")[2]["angle_end"] = bad.at("arcs")[2]["angle_start"];
        bad.at("lines")[3]["kind"] = "rail";
        TableShape out;
        std::vector<std::string> problems;
        CHECK_FALSE(load_table_shape(bad, out, problems));
        CHECK(problems.size() >= 5);
        CHECK(has_problem(problems, "rail_width_m"));
        CHECK(has_problem(problems, "\"corner\" must be true"));
        CHECK(has_problem(problems, "drop_radius"));
        CHECK(has_problem(problems, "angle_end must exceed"));
        CHECK(has_problem(problems, "\"kind\" must be"));
    }
    SUBCASE("not an object") {
        TableShape out;
        std::vector<std::string> problems;
        CHECK_FALSE(load_table_shape(nlohmann::json::array(), out, problems));
        CHECK(problems.size() == 1);
    }
}

namespace {

struct Capture : log::Sink {
    std::vector<log::Event> events;
    void write(const log::Event& event, const std::string&) override { events.push_back(event); }
};

std::vector<log::Event> load_capturing(const nlohmann::json& file, std::vector<std::string>& problems, bool& ok) {
    auto capture = std::make_shared<Capture>();
    log::Logger::instance().clear_sinks();
    log::Logger::instance().add_sink(capture);
    TableShape out;
    ok = load_table_shape(file, out, problems);
    log::Logger::instance().clear_sinks();
    return capture->events;
}

}  // namespace

TEST_CASE("table shape: a refused file is logged once as table.invalid with its hash and every problem") {
    // Spec 16.10: a refused table file must be diagnosable from the log alone.
    const nlohmann::json schema = log::load_schema(std::string(OFS_CORE_DATA_DIR) + "/log_schema.json");
    nlohmann::json bad = shape_file();
    bad["rail_width_m"] = -1.0;
    bad.at("lines")[3]["kind"] = "rail";
    std::vector<std::string> problems{"left by an earlier load"};
    bool ok = true;
    auto events = load_capturing(bad, problems, ok);
    CHECK_FALSE(ok);
    REQUIRE(problems.size() >= 3);
    REQUIRE(events.size() == 1);
    const log::Event e = events[0];
    CHECK(e.event == "table.invalid");
    CHECK(e.subsystem == "table");
    CHECK(e.level == log::Level::error);
    const auto problem = log::check(e, schema);
    CHECK_MESSAGE(!problem.has_value(), problem.value_or(""));
    CHECK(e.data["hash"] == params::content_hash(bad));
    CHECK(e.data["hash"] != params::content_hash(shape_file()));
    const std::vector<std::string> found(problems.begin() + 1, problems.end());
    CHECK(e.data["problems"] == nlohmann::json(found));

    problems.clear();
    events = load_capturing(nlohmann::json::array(), problems, ok);
    CHECK_FALSE(ok);
    REQUIRE(events.size() == 1);
    CHECK(events[0].event == "table.invalid");
    CHECK(events[0].data["hash"] == params::content_hash(nlohmann::json::array()));
    CHECK(events[0].data["problems"] == nlohmann::json(problems));

    problems.clear();
    events = load_capturing(shape_file(), problems, ok);
    CHECK(ok);
    CHECK(events.empty());
}
