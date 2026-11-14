// ofs_sim_cli pots: how wide each pocket plays (spec 6.3's owner check, as numbers). A lone ball
// rolls at a pocket and its line is swept sideways; the width of the band of lines that drop is
// the pocket's size from that angle at that pace. README.md has the details.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sim_cli.hpp"

namespace sim_cli {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Degrees from the pocket's axis: the diagonal for a corner (43 runs almost along a cushion),
// square to the cushion for a middle pocket.
constexpr double kCornerAngles[] = {0.0, 15.0, 30.0, 43.0};
constexpr double kMiddleAngles[] = {0.0, 20.0, 40.0, 60.0};
constexpr double kSpeeds[] = {0.5, 1.5, 4.0};
// The sweep looks this far to either side of the line through the pocket's point.
constexpr double kSweepHalf_m = 0.15;
// A ball that leaves the pocket and drops after a trip round the table is a fluke, not a pot.
constexpr double kStayWithin_m = 0.75;

// The pocket a sweep aims at: the corner point or, for a middle pocket, the middle of its mouth
// on the nose line; and its axis out of the table. Neither depends on the outline's jaws, so the
// offsets of two outlines compare.
struct Target {
    const char* kind;
    int pocket;
    Vec3 point;
    Vec3 axis;
};

struct Band {
    int lines = 0;   // lines the ball could start on
    int potted = 0;
    double lo = 0.0;  // first and last line that dropped, m across the heading
    double hi = 0.0;
    int out_inside = 0;  // lines between lo and hi that stayed out
    int anomalies = 0;
};

struct Sweep {
    const phys::TableShape* table;
    const phys::PhysicsParams* params;
    double distance;
    double step;
};

int pocket_named(const phys::TableShape& t, const char* name) {
    for (std::size_t k = 0; k < t.pockets.size(); ++k) {
        if (t.pockets[k].name == name) return static_cast<int>(k);
    }
    return -1;
}

bool can_start(const Vec3& r) {
    // A picometre of slack, so a ball placed touching a cushion counts.
    const double tol = 1e-12;
    return std::abs(r.x) <= ofs::table::kPlayingLength / 2.0 - kR + tol &&
           std::abs(r.y) <= ofs::table::kPlayingWidth / 2.0 - kR + tol;
}

// A ball rolling from `start` along unit `heading`, paced to pass `distance` on at `arrival`.
bool drops(const Sweep& s, const Target& target, const Vec3& start, const Vec3& heading, double arrival,
           int& anomalies) {
    const phys::PhysicsParams& p = *s.params;
    const double v0 = std::sqrt(arrival * arrival + 2.0 * p.roll_friction * p.gravity_m_s2 * s.distance);
    phys::BallState b;
    b.ball = ofs::table::Ball::red;
    b.r = start;
    b.v = heading * v0;
    b.w = {-b.v.y / kR, b.v.x / kR, 0.0};
    b.motion = phys::Motion::rolling;
    phys::SimInput in;
    in.start.balls = {b};
    in.has_strike = false;
    const phys::ShotRecord r = phys::simulate(in, *s.table, p);
    anomalies += r.anomalies.empty() ? 0 : 1;
    const phys::BallState& end = r.final_state.balls[0];
    if (end.motion != phys::Motion::pocketed || end.pocket != target.pocket) return false;
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind == phys::EventKind::pocket) break;
        if (planar_distance(state_before(r, 0, e.t).r, target.point) > kStayWithin_m) return false;
    }
    return true;
}

// One line of a sweep, in the order swept.
struct Line {
    double offset;
    bool potted;
};

Band band_of(const std::vector<Line>& lines, int anomalies) {
    Band band;
    band.lines = static_cast<int>(lines.size());
    band.anomalies = anomalies;
    int out = 0;
    for (const Line& line : lines) {
        if (!line.potted) {
            out += band.potted > 0 ? 1 : 0;
            continue;
        }
        if (band.potted++ == 0) band.lo = line.offset;
        band.hi = line.offset;
        band.out_inside = out;
    }
    return band;
}

// Lines parallel to `heading`, `offset` to its left of the line through the pocket's point.
Band sweep_across(const Sweep& s, const Target& target, const Vec3& heading, double arrival) {
    const Vec3 left{-heading.y, heading.x, 0.0};
    const int n = static_cast<int>(std::lround(kSweepHalf_m / s.step));
    std::vector<Line> lines;
    int anomalies = 0;
    for (int k = -n; k <= n; ++k) {
        const double offset = k * s.step;
        const Vec3 start = target.point - heading * s.distance + left * offset;
        if (can_start(start)) lines.push_back({offset, drops(s, target, start, heading, arrival, anomalies)});
    }
    return band_of(lines, anomalies);
}

// Lines along the side cushion into a corner, `gap` between the ball and the cushion's nose; the
// first is the ball touching it.
Band sweep_along_cushion(const Sweep& s, const Target& target, double arrival, bool& frozen_drops) {
    const Vec3 heading{target.axis.x > 0.0 ? 1.0 : -1.0, 0.0, 0.0};
    const double side = target.axis.y > 0.0 ? 1.0 : -1.0;
    const int n = static_cast<int>(std::lround(kSweepHalf_m / s.step));
    std::vector<Line> lines;
    int anomalies = 0;
    for (int k = 0; k <= n; ++k) {
        const double gap = k * s.step;
        const Vec3 start{target.point.x - heading.x * s.distance, target.point.y - side * (kR + gap), 0.0};
        lines.push_back({gap, drops(s, target, start, heading, arrival, anomalies)});
    }
    frozen_drops = lines.front().potted;
    return band_of(lines, anomalies);
}

double width_mm(const Band& b, double step) { return b.potted * step * 1000.0; }

nlohmann::json band_json(const Band& b, double step) {
    nlohmann::json j = {{"width_mm", width_mm(b, step)}, {"lines", b.lines}, {"out_inside", b.out_inside}};
    if (b.potted > 0) {
        j["lo_mm"] = b.lo * 1000.0;
        j["hi_mm"] = b.hi * 1000.0;
    }
    return j;
}

std::string band_text(const Band& b, double step) {
    if (b.potted == 0) return format("%7.1f mm  %-19s", 0.0, "(none drop)");
    const std::string holes = b.out_inside > 0 ? format(" (%d lines inside it stay out)", b.out_inside) : "";
    return format("%7.1f mm  %+7.1f .. %+7.1f mm", width_mm(b, step), b.lo * 1000.0, b.hi * 1000.0) + holes;
}

std::string ratio_text(const Band& b, const Band& other) {
    if (other.potted == 0) return b.potted == 0 ? "  -  " : " new ";
    return format("%5.2f", static_cast<double>(b.potted) / other.potted);
}

}  // namespace

int cmd_pots(const std::vector<std::string>& argv) {
    const ArgSpec spec{{"params", "table", "against", "step-mm", "distance", "out"}, {}, 0, 0};
    Args args;
    std::string error;
    if (!parse_args(argv, spec, args, error)) return usage_error("pots", error);
    double step_mm = 0.1;
    double distance = 0.6;
    if (!option_double(args, "step-mm", 0.01, 10.0, step_mm, error)) return usage_error("pots", error);
    if (!option_double(args, "distance", 0.2, 1.5, distance, error)) return usage_error("pots", error);

    configure_logger();
    Setup setup;
    if (!load_setup(args, setup)) return kExitUsage;
    phys::TableShape other;
    std::string other_hash;
    const std::string* against = args.value("against");
    if (against != nullptr && !load_table(*against, other, other_hash)) return kExitUsage;

    const double hx = ofs::table::kPlayingLength / 2.0;
    const double hy = ofs::table::kPlayingWidth / 2.0;
    const Target corner{"corner", pocket_named(setup.table, "top_left"), {hx, hy, 0.0}, {std::sqrt(0.5), std::sqrt(0.5), 0.0}};
    const Target middle{"middle", pocket_named(setup.table, "middle_left"), {0.0, hy, 0.0}, {0.0, 1.0, 0.0}};
    if (corner.pocket < 0 || middle.pocket < 0) return usage_error("pots", "the table has no top_left or middle_left pocket");

    const Sweep here{&setup.table, &setup.params, distance, step_mm / 1000.0};
    const Sweep there{&other, &setup.params, distance, step_mm / 1000.0};

    std::printf("pots       a lone ball rolling into a pocket from %.3f m, its line swept sideways in %.3g mm steps\n",
                distance, step_mm);
    std::printf("params     %s, table %s (%s)\n", setup.params_hash.c_str(), setup.table_hash.c_str(),
                shareable_path(setup.table_file).c_str());
    if (against != nullptr) {
        std::printf("against    table %s (%s), the same parameters\n", other_hash.c_str(),
                    shareable_path(*against).c_str());
    }
    std::printf("\nwidth: how much of the sweep drops; band: its first and last line, across the heading from the line\n"
                "through the corner point (the middle of the mouth for a middle pocket), positive to the left.\n\n");
    std::printf("%-7s %-9s %-8s %-33s %s\n", "pocket", "off axis", "arrival", "this table: width, band",
                against != nullptr ? "against: width, band                ratio" : "");

    nlohmann::json rows = nlohmann::json::array();
    int anomalies = 0;
    for (const Target* target : {&corner, &middle}) {
        const bool is_corner = target == &corner;
        const double* angles = is_corner ? kCornerAngles : kMiddleAngles;
        for (int a = 0; a < 4; ++a) {
            // Turned towards +x: for the corner, towards running along the side cushion.
            const double turn = -angles[a] * kPi / 180.0;
            const Vec3 heading{std::cos(turn) * target->axis.x - std::sin(turn) * target->axis.y,
                               std::sin(turn) * target->axis.x + std::cos(turn) * target->axis.y, 0.0};
            for (const double speed : kSpeeds) {
                const Band b = sweep_across(here, *target, heading, speed);
                anomalies += b.anomalies;
                nlohmann::json row = {{"pocket", target->kind}, {"angle_deg", angles[a]}, {"speed_m_s", speed}};
                row.update(band_json(b, here.step));
                std::string line = format("%-7s %5.0f deg %5.1f m/s %s", target->kind, angles[a], speed,
                                          band_text(b, here.step).c_str());
                if (against != nullptr) {
                    const Band o = sweep_across(there, *target, heading, speed);
                    anomalies += o.anomalies;
                    row["against"] = band_json(o, there.step);
                    line += "   " + band_text(o, there.step) + "   " + ratio_text(b, o);
                }
                std::printf("%s\n", line.c_str());
                rows.push_back(std::move(row));
            }
        }
    }

    std::printf("\nalong the side cushion into the corner (gap: between the ball and the cushion):\n");
    nlohmann::json along = nlohmann::json::array();
    for (const double speed : kSpeeds) {
        bool frozen = false;
        const Band b = sweep_along_cushion(here, corner, speed, frozen);
        anomalies += b.anomalies;
        nlohmann::json row = {{"speed_m_s", speed}, {"frozen_drops", frozen}};
        row.update(band_json(b, here.step));
        std::string line = format("%5.1f m/s  touching the cushion: %-9s gaps that drop: %s", speed,
                                  frozen ? "drops" : "stays out", band_text(b, here.step).c_str());
        if (against != nullptr) {
            bool other_frozen = false;
            const Band o = sweep_along_cushion(there, corner, speed, other_frozen);
            anomalies += o.anomalies;
            row["against"] = band_json(o, there.step);
            row["against"]["frozen_drops"] = other_frozen;
            line += format("   against: %-9s %s   %s", other_frozen ? "drops" : "stays out",
                           band_text(o, there.step).c_str(), ratio_text(b, o).c_str());
        }
        std::printf("%s\n", line.c_str());
        along.push_back(std::move(row));
    }
    if (anomalies > 0) std::printf("\n%d shots had a physics anomaly\n", anomalies);

    int code = anomalies > 0 ? kExitFailed : kExitOk;
    if (const std::string* out = args.value("out")) {
        nlohmann::json j = {{"distance_m", distance},
                            {"step_mm", step_mm},
                            {"params_hash", setup.params_hash},
                            {"table_hash", setup.table_hash},
                            {"rows", rows},
                            {"along_cushion", along},
                            {"anomalies", anomalies}};
        if (against != nullptr) j["against_table_hash"] = other_hash;
        if (!write_text_file(*out, j.dump(2) + "\n", error)) {
            std::fprintf(stderr, "pots: --out: %s\n", error.c_str());
            code = kExitUsage;
        }
    }
    return code;
}

}  // namespace sim_cli
