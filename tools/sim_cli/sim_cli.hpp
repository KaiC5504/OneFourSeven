#pragma once

// Pieces the ofs_sim_cli commands share: arguments, data files, positions, record lookups and the
// geometry checks of the playing area.

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/physics/params.hpp"
#include "ofs/physics/shot_record.hpp"
#include "ofs/physics/simulator.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/rng.hpp"

namespace sim_cli {

namespace phys = ofs::phys;
using ofs::Vec3;

inline constexpr int kExitOk = 0;
inline constexpr int kExitFailed = 1;  // a check failed: a divergence, a fuzz failure, a budget
inline constexpr int kExitUsage = 2;   // bad arguments or unreadable input

inline constexpr double kR = ofs::table::kBallRadius;

struct ArgSpec {
    std::vector<std::string> valued;  // --name value or --name=value
    std::vector<std::string> flags;
    std::size_t min_positional = 0;
    std::size_t max_positional = 0;
};

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> values;
    std::vector<std::string> flags;

    bool flag(const std::string& name) const;
    const std::string* value(const std::string& name) const;
};

bool parse_args(const std::vector<std::string>& argv, const ArgSpec& spec, Args& out, std::string& error);

// Whole-string parses; false on trailing junk, overflow or a non-finite double.
bool parse_u64(const std::string& text, std::uint64_t& out);
bool parse_int(const std::string& text, long long& out);
bool parse_double(const std::string& text, double& out);

// Each leaves `out` alone when the option is absent and is false, with `error` set, when it is
// present but malformed or outside [min, max].
bool option_u64(const Args& args, const std::string& name, std::uint64_t& out, std::string& error);
bool option_int(const Args& args, const std::string& name, long long min, long long max, long long& out,
                std::string& error);
bool option_double(const Args& args, const std::string& name, double min, double max, double& out,
                   std::string& error);

bool read_text_file(const std::string& path, std::string& out, std::string& error);
bool read_json_file(const std::string& path, nlohmann::json& out, std::string& error);
bool write_text_file(const std::string& path, const std::string& text, std::string& error);

// OFS_CORE_DATA_DIR is <repo>/core/data, so the repository root is two levels up.
std::string data_path(const std::string& file);
std::string repo_path(const std::string& relative);
// A path fit for a log or a written file: relative to the repository when it is inside it, else
// the bare file name, so no home folder (and with it a username) leaves this PC.
std::string shareable_path(const std::string& path);

struct Setup {
    phys::PhysicsParams params;
    phys::TableShape table;
    std::string params_hash;  // content_hash({"table_params": file}), the game's and devlink's hash
    std::string table_hash;
    std::string params_file;
    std::string table_file;
};

// From --params and --table, else the files in core/data. Prints every problem to stderr.
bool load_setup(const Args& args, Setup& out);
// One table_shape.json and its content hash; every problem goes to stderr.
bool load_table(const std::string& file, phys::TableShape& table, std::string& hash);

struct Position {
    std::string name;
    phys::TableState start;
    int cue_ball = 0;
    bool has_strike = false;
    nlohmann::json strike;  // serialize.hpp's strike form
};

// A fixture-style file ({start, cue_ball, strike}), a bare table ({balls}) or "rack:SEED", the
// opening rack with a default break-off.
bool load_position(const std::string& spec, const phys::PhysicsParams& p, Position& out, std::string& error);

// A thin hit on the end red of the back row from the cue ball's side, with running side.
phys::CueStrike break_off_strike(const phys::TableState& rack, double speed_m_s);

// A ball's state once every event at t is resolved, or just before them, from its segments.
phys::BallState state_after(const phys::ShotRecord& r, int ball, double t);
phys::BallState state_before(const phys::ShotRecord& r, int ball, double t);

bool finite(const Vec3& v);
bool finite(const phys::BallState& b);
double planar_distance(const Vec3& a, const Vec3& b);

std::string format(const char* fmt, ...);
std::string ball_label(const phys::BallState& b);
std::string pocket_label(const phys::TableShape& t, int pocket);
// "14 events, t_end 4.213000 s, potted 1, anomalies 0, sim 103 us".
std::string record_summary(const phys::ShotRecord& r);
std::string anomaly_text(const phys::Anomaly& a);

// Prints "<command>: <error>" and a pointer to the usage, returns kExitUsage.
int usage_error(const std::string& command, const std::string& error);
// The log lines' build field: "sim_cli-<core version>", plus "+<git sha>" when the build defines
// OFS_GIT_SHA.
std::string build_name();
void configure_logger();

double microseconds_since(std::chrono::steady_clock::time_point start);

struct Stats {
    double mean = 0.0;
    double p50 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};
Stats stats_of(std::vector<double> values);

// Where a ball centre may be (spec 6.3, 7.5, 7.6): inside the cushion outline, clear of the noses
// and jaws, not in a drop circle. The rectangle comes from the cushion lines when the table has
// them, else from table_geometry.hpp.
class PlayArea {
public:
    explicit PlayArea(const phys::TableShape& t);

    // A resting ball may be put here: strictly clear of every cushion, jaw circle, drop circle.
    bool placement_ok(const Vec3& c) const;
    // Empty when an unpotted ball's centre at c is possible, else why not. `tol` is how far a
    // ball may overlap a cushion before it counts.
    std::string violation(const Vec3& c, double tol) const;

    double half_x() const { return half_x_; }
    double half_y() const { return half_y_; }
    double centre_x() const { return centre_x_; }
    double centre_y() const { return centre_y_; }
    const phys::TableShape& table() const { return table_; }

    // Signed distance of c from line k along its normal, and whether its foot lies on the segment.
    double line_distance(std::size_t k, const Vec3& c, bool& on_segment) const;

private:
    phys::TableShape table_;
    double centre_x_ = 0.0;
    double centre_y_ = 0.0;
    double half_x_ = 0.0;  // of the rectangle a centre may occupy
    double half_y_ = 0.0;
    std::vector<double> zone_radius_;  // per pocket: how far from its drop centre a ball may roam
};

enum class FuzzKind { break_off, scattered, cluster, cushion };
inline constexpr int kFuzzKinds = 4;
const char* fuzz_kind_name(FuzzKind k);

struct FuzzShot {
    FuzzKind kind = FuzzKind::scattered;
    phys::SimInput in;
};

// Shot `index` of the set seeded by `seed`: the same pair always gives the same shot.
FuzzShot make_fuzz_shot(std::uint64_t seed, std::uint64_t index, const Setup& setup, const PlayArea& area);

struct ShotFailure {
    std::string kind;
    std::string detail;
};

// C12 and C14 on one record, every failure found: anomalies other than solver fallbacks, NaN,
// balls overlapping or outside the playing area at any event time, a ball into a cushion, jaw or
// drop circle at any moment between events, a rise in total kinetic energy from one event time to
// the next, balls not at rest at the end.
std::vector<ShotFailure> check_record(const phys::SimInput& in, const phys::ShotRecord& r,
                                      const PlayArea& area);

// Command entry points; argv excludes the program and command names.
int cmd_simulate(const std::vector<std::string>& argv);
int cmd_replay(const std::vector<std::string>& argv);
int cmd_fuzz(const std::vector<std::string>& argv);
int cmd_bench(const std::vector<std::string>& argv);
int cmd_calibrate(const std::vector<std::string>& argv);

}  // namespace sim_cli
