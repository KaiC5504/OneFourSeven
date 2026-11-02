// sim_cli bench: fixed shots timed against the 2 ms budget of spec 7.13 and a recorded baseline.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#endif

#include "ofs/physics/rack.hpp"
#include "ofs/physics/repro.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using ofs::table::Ball;

constexpr double kBudgetUs = 2000.0;
// A shot regresses when its time at the baseline machine's speed is over kRegression times its
// baseline plus kRegressionSlackUs. The slack keeps scheduler noise on the 10-30 us shots from
// failing the check; a shot that takes twice as long still fails once it is over 200 us.
constexpr double kRegression = 1.25;
constexpr double kRegressionSlackUs = 50.0;
constexpr int kBaselineVersion = 2;

struct BenchShot {
    std::string name;
    phys::SimInput in;
    phys::PhysicsParams params;
};

#ifdef NDEBUG
const char* const kBuild = "release";
#else
const char* const kBuild = "debug";
#endif

std::string cpu_name() {
    unsigned regs[12] = {};
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int info[4] = {};
    __cpuid(info, static_cast<int>(0x80000000u));
    if (static_cast<unsigned>(info[0]) < 0x80000004u) return "unknown CPU";
    for (int k = 0; k < 3; ++k) {
        __cpuid(info, static_cast<int>(0x80000002u + static_cast<unsigned>(k)));
        std::memcpy(regs + 4 * k, info, sizeof info);
    }
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    if (__get_cpuid_max(0x80000000u, nullptr) < 0x80000004u) return "unknown CPU";
    for (unsigned k = 0; k < 3; ++k) {
        __get_cpuid(0x80000002u + k, &regs[4 * k], &regs[4 * k + 1], &regs[4 * k + 2], &regs[4 * k + 3]);
    }
#else
    return "unknown CPU";
#endif
    char text[sizeof regs + 1] = {};
    std::memcpy(text, regs, sizeof regs);
    std::string name;
    for (const char* c = text; *c != '\0'; ++c) {
        if (*c == ' ' && (name.empty() || name.back() == ' ')) continue;
        name += *c;
    }
    while (!name.empty() && name.back() == ' ') name.pop_back();
    return name.empty() ? "unknown CPU" : name;
}

// Timings only compare on the PC that made them: a baseline from another machine is only shown.
std::string machine_name() {
    return cpu_name() + format(", %u logical cores", std::max(1u, std::thread::hardware_concurrency()));
}

volatile double g_probe_seed = 1.0;
volatile double g_probe_sink = 0.0;

// The PC's speed right now: fixed floating-point work in the simulator's style (contact times of
// 22 points in pairs, from a quadratic each) that shares no code with the physics, so a change to
// the physics can't move it, while load or a slower CPU moves it as it moves the shots.
void probe_once() {
    constexpr int kPoints = 22;
    const double seed = g_probe_seed;
    double x[kPoints], y[kPoints], vx[kPoints], vy[kPoints];
    for (int i = 0; i < kPoints; ++i) {
        x[i] = seed * (0.15 * i - 1.6);
        y[i] = seed * (0.07 * ((i * 7) % 23) - 0.8);
        vx[i] = std::cos(0.7 * i) * seed;
        vy[i] = std::sin(1.3 * i) * seed;
    }
    double sum = 0.0;
    for (int step = 0; step < 200; ++step) {
        for (int i = 0; i < kPoints; ++i) {
            for (int j = i + 1; j < kPoints; ++j) {
                const double dx = x[j] - x[i], dy = y[j] - y[i];
                const double dvx = vx[j] - vx[i], dvy = vy[j] - vy[i];
                const double a = dvx * dvx + dvy * dvy;
                const double b = dx * dvx + dy * dvy;
                const double disc = b * b - a * (dx * dx + dy * dy - 4.0 * kR * kR);
                if (b < 0.0 && disc > 0.0) sum += (-b - std::sqrt(disc)) / a;
            }
        }
        for (int i = 0; i < kPoints; ++i) {
            x[i] += vx[i] * 0.01;
            y[i] += vy[i] * 0.01;
            if (std::fabs(x[i]) > 1.75) vx[i] = -vx[i];
            if (std::fabs(y[i]) > 0.87) vy[i] = -vy[i];
        }
    }
    g_probe_sink = sum;
}

// The probe's time per call over `calls` calls in a row. Each run of a shot is paired with a probe
// run as long as it, so the pair sees the same load: a long run is preempted under load as often
// as the shot beside it, which a lone 50 us call would not be.
double probe_run(int calls) {
    const auto start = Clock::now();
    for (int k = 0; k < calls; ++k) probe_once();
    return microseconds_since(start) / calls;
}

phys::BallState ball_at(Ball ball, int index, double x, double y) {
    phys::BallState b;
    b.ball = ball;
    b.index = index;
    b.r = {x, y, 0.0};
    return b;
}

phys::BallState ball_on_spot(Ball colour) {
    const ofs::table::Vec2 s = ofs::table::spot(colour);
    return ball_at(colour, 0, s.x, s.y);
}

// The cue ball `distance` behind the ghost ball for potting `object` towards `target` with a cut
// of `cut_rad`, and the aim that sends it there.
void line_up_pot(const phys::BallState& object, ofs::table::Vec2 target, double cut_rad, double distance,
                 phys::BallState& cue, double& aim) {
    const Vec3 to_pocket = ofs::unit_or_zero(Vec3{target.x, target.y, 0.0} - object.r);
    const Vec3 ghost = object.r - to_pocket * (2.0 * kR);
    const double line = std::atan2(to_pocket.y, to_pocket.x) + cut_rad;
    const Vec3 approach{std::cos(line), std::sin(line), 0.0};
    cue.r = ghost - approach * distance;
    aim = line;
}

std::vector<BenchShot> bench_shots(const Setup& setup) {
    std::vector<BenchShot> shots;
    const auto add = [&shots, &setup](std::string name, phys::TableState start, phys::CueStrike strike) {
        BenchShot shot;
        shot.name = std::move(name);
        shot.in.start = std::move(start);
        shot.in.cue_ball = 0;
        shot.in.has_strike = true;
        shot.in.strike = strike;
        shot.in.seed = 147;
        shot.in.params_hash = setup.params_hash;
        shot.in.table_hash = setup.table_hash;
        shot.params = setup.params;
        shots.push_back(std::move(shot));
    };
    const double half_length = ofs::table::kPlayingLength / 2.0;
    const double half_width = ofs::table::kPlayingWidth / 2.0;

    for (double speed : {4.5, 7.0, 10.0}) {
        for (std::uint64_t seed : {1, 2, 3}) {
            const phys::TableState rack = phys::opening_rack(seed, setup.params);
            add(format("break_%.1fms_rack%llu", speed, static_cast<unsigned long long>(seed)), rack,
                break_off_strike(rack, speed));
        }
    }

    {
        phys::CueStrike c;
        c.aim_rad = 0.35;
        c.tip_x = 0.3;
        c.speed_m_s = 8.0;
        add("long_multi_cushion", {{ball_at(Ball::cue, 0, -1.2, -0.5)}}, c);
    }

    {
        // Six reds frozen in a triangle, struck full on the apex.
        phys::TableState s{{ball_at(Ball::cue, 0, -0.5, 0.0)}};
        const double row = 2.0 * kR * std::sqrt(3.0) / 2.0;
        int index = 0;
        for (int r = 0; r < 3; ++r) {
            for (int k = 0; k <= r; ++k) {
                s.balls.push_back(ball_at(Ball::red, index++, 0.8 + r * row, (k - r / 2.0) * 2.0 * kR));
            }
        }
        phys::CueStrike c;
        c.speed_m_s = 5.0;
        add("cluster_split", s, c);
    }

    // Typical pots with the colours on their spots.
    const auto colours = [](bool with_blue) {
        phys::TableState s;
        for (Ball colour : {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink}) {
            if (colour != Ball::blue || with_blue) s.balls.push_back(ball_on_spot(colour));
        }
        return s;
    };
    {
        phys::TableState s{{ball_at(Ball::cue, 0, 0.0, 0.0), ball_on_spot(Ball::black)}};
        for (const phys::BallState& b : colours(true).balls) s.balls.push_back(b);
        phys::CueStrike c;
        line_up_pot(s.balls[1], {half_length, half_width}, 0.25, 0.6, s.balls[0], c.aim_rad);
        c.speed_m_s = 2.0;
        c.tip_y = -0.2;
        add("pot_black_corner", s, c);
    }
    {
        phys::BallState red = ball_at(Ball::red, 0, -0.35, -0.55);
        phys::TableState s{{ball_at(Ball::cue, 0, 0.0, 0.0), red}};
        for (const phys::BallState& b : colours(true).balls) s.balls.push_back(b);
        phys::CueStrike c;
        line_up_pot(s.balls[1], {0.0, -half_width}, -0.2, 0.45, s.balls[0], c.aim_rad);
        c.speed_m_s = 2.5;
        c.tip_y = 0.2;
        add("pot_red_middle", s, c);
    }
    {
        // Without the blue, which sits on this shot's line.
        phys::TableState s{{ball_at(Ball::cue, 0, 0.0, 0.0), ball_at(Ball::red, 0, 1.0, -0.45)}};
        for (const phys::BallState& b : colours(false).balls) s.balls.push_back(b);
        s.balls.push_back(ball_on_spot(Ball::black));
        phys::CueStrike c;
        line_up_pot(s.balls[1], {half_length, -half_width}, 0.12, 1.4, s.balls[0], c.aim_rad);
        c.speed_m_s = 3.5;
        c.tip_y = -0.4;
        add("pot_long_red_screw", s, c);
    }

    {
        phys::CueStrike c;
        c.aim_rad = 0.02;
        c.tip_x = 0.35;
        c.tip_y = 0.2;
        c.speed_m_s = 0.9;
        add("slow_roll_nap", {{ball_at(Ball::cue, 0, -1.2, 0.3)}}, c);
    }
    return shots;
}

// The pack-heavy shots the generated ones miss: recorded bundles (a fuzz failure file's
// {bundle, result} or a bare bundle) in tools/sim_cli/bench_shots, each named after its file and
// run with the parameters it carries, so it stays the shot that was recorded.
bool recorded_shots(std::vector<BenchShot>& shots, std::string& error) {
    const std::string dir = repo_path("tools/sim_cli/bench_shots");
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".json") files.push_back(it->path());
    }
    if (ec) {
        error = "cannot list " + dir + ": " + ec.message();
        return false;
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        nlohmann::json j;
        if (!read_json_file(file.u8string(), j, error)) return false;
        const nlohmann::json& bundle = j.is_object() && j.contains("bundle") ? j["bundle"] : j;
        BenchShot shot;
        shot.name = file.stem().u8string();
        if (!phys::parse_bundle(bundle, shot.in, shot.params, error)) {
            error = file.u8string() + ": " + error;
            return false;
        }
        shots.push_back(std::move(shot));
    }
    return true;
}

struct Measured {
    std::string name;
    std::size_t events = 0;
    std::size_t anomalies = 0;
    int runs = 0;
    double median_us = 0.0;
    double p99_us = 0.0;
    double probe_us = 0.0;     // the median time of one probe call
    double probe_units = 0.0;  // the median of each run's time over its paired probe's
};

Measured measure(const BenchShot& shot, const Setup& setup, long long fixed_runs) {
    Measured m;
    m.name = shot.name;
    const auto warm_start = Clock::now();
    const phys::ShotRecord warm = phys::simulate(shot.in, setup.table, shot.params);
    const double warm_us = microseconds_since(warm_start);
    m.events = warm.events.size();
    m.anomalies = warm.anomalies.size();
    std::vector<double> single;
    for (int i = 0; i < 5; ++i) single.push_back(probe_run(1));
    const int probe_calls = static_cast<int>(std::clamp(std::round(warm_us / stats_of(single).p50), 1.0, 1e4));

    std::vector<double> times;
    std::vector<double> probes;
    std::vector<double> units;
    const auto begin = Clock::now();
    // Enough runs for a stable median: at least 30 and a quarter of a second, at most 2,000.
    for (;;) {
        const auto start = Clock::now();
        const phys::ShotRecord r = phys::simulate(shot.in, setup.table, shot.params);
        times.push_back(microseconds_since(start));
        probes.push_back(probe_run(probe_calls));
        units.push_back(times.back() / probes.back());
        const auto n = static_cast<long long>(times.size());
        const bool enough = (n >= 30 && microseconds_since(begin) >= 250000.0) || n >= 2000;
        if (fixed_runs > 0 ? n >= fixed_runs : enough) break;
    }
    const Stats s = stats_of(times);
    m.runs = static_cast<int>(times.size());
    m.median_us = s.p50;
    m.p99_us = s.p99;
    m.probe_us = stats_of(probes).p50;
    m.probe_units = stats_of(units).p50;
    return m;
}

// What --check compares against: the baseline's shots in its machine's probe timings, or why there
// is nothing to compare.
struct Baseline {
    bool comparable = false;
    std::string why_not;
    double probe_us = 0.0;
    nlohmann::json shots;
};

Baseline read_baseline(const nlohmann::json& file, const std::string& machine) {
    Baseline b;
    const nlohmann::json version = file.value("version", nlohmann::json());
    const nlohmann::json probe = file.value("probe_us", nlohmann::json());
    const std::string recorded = file.value("machine", std::string());
    const std::string build = file.value("build", std::string());
    if (!version.is_number_integer() || version.get<int>() < kBaselineVersion || recorded.empty() ||
        !probe.is_number() || !(probe.get<double>() > 0.0)) {
        b.why_not = "the baseline names no machine or speed probe (written before baseline version 2); "
                    "write it again with --write-baseline";
    } else if (recorded != machine) {
        b.why_not = "the baseline was recorded on " + recorded + ", not this machine (" + machine + ")";
    } else if (build != kBuild) {
        b.why_not = "the baseline was recorded with a " + build + " build, this is a " + kBuild + " build";
    } else {
        b.comparable = true;
        b.probe_us = probe.get<double>();
        b.shots = file["shots"];
    }
    return b;
}

}  // namespace

int cmd_bench(const std::vector<std::string>& argv) {
    const ArgSpec spec{{"baseline", "runs", "budget-us", "params", "table"}, {"check", "write-baseline"}, 0, 0};
    Args args;
    std::string error;
    if (!parse_args(argv, spec, args, error)) return usage_error("bench", error);
    long long runs = 0;
    double budget_us = kBudgetUs;
    if (!option_int(args, "runs", 1, 1000000, runs, error) ||
        !option_double(args, "budget-us", 1e-3, 1e12, budget_us, error)) {
        return usage_error("bench", error);
    }
    Setup setup;
    if (!load_setup(args, setup)) return kExitUsage;
    std::vector<BenchShot> shots = bench_shots(setup);
    if (!recorded_shots(shots, error)) return usage_error("bench", error);
    const std::string baseline_path =
        args.value("baseline") ? *args.value("baseline") : repo_path("tools/sim_cli/bench_baseline.json");
    const std::string machine = machine_name();

    Baseline baseline;
    baseline.why_not = "no baseline file";
    bool have_baseline = false;
    if (fs::exists(fs::u8path(baseline_path))) {
        nlohmann::json file;
        if (!read_json_file(baseline_path, file, error)) {
            return usage_error("bench", "--baseline: " + error);
        }
        if (!file.is_object() || !file.contains("shots") || !file["shots"].is_object()) {
            return usage_error("bench", baseline_path + ": want {\"shots\": {name: {\"median_us\": ...}}}");
        }
        have_baseline = true;
        baseline = read_baseline(file, machine);
    }

    std::printf("bench: %s build, params %s, budget %.0f us per shot, baseline %s\n", kBuild,
                setup.params_hash.c_str(), budget_us, have_baseline ? baseline_path.c_str() : "(none)");
    std::printf("machine: %s\n", machine.c_str());
    if (have_baseline && !baseline.comparable) {
        std::printf("note: no regression check: %s; the times below are only reported\n", baseline.why_not.c_str());
    }
    // scaled_us is this run's time at the speed the baseline's machine had when it was written.
    std::printf("%-24s %7s %6s %11s %11s %11s %12s  %s\n", "shot", "events", "runs", "median_us", "p99_us",
                "scaled_us", "baseline_us", "status");

    const bool check = args.flag("check");
    int over_budget = 0;
    int regressed = 0;
    std::vector<double> probes;
    nlohmann::json written_shots = nlohmann::json::object();
    for (const BenchShot& shot : shots) {
        const Measured m = measure(shot, setup, runs);
        probes.push_back(m.probe_us);
        std::string status = "ok";
        std::string scaled = "-";
        std::string base = "-";
        if (m.median_us > budget_us) {
            status = format("OVER BUDGET (%.0f us)", budget_us);
            ++over_budget;
        }
        if (baseline.comparable) {
            const nlohmann::json entry = baseline.shots.value(shot.name, nlohmann::json());
            const nlohmann::json units = entry.is_object() ? entry.value("probe_units", nlohmann::json())
                                                           : nlohmann::json();
            if (units.is_number()) {
                const double before_us = units.get<double>() * baseline.probe_us;
                const double now_us = m.probe_units * baseline.probe_us;
                scaled = format("%.1f", now_us);
                base = format("%.1f", before_us);
                if (now_us > before_us * kRegression + kRegressionSlackUs) {
                    status = (status == "ok" ? std::string() : status + ", ") +
                             format("REGRESSED (+%.0f%%)", (now_us / before_us - 1.0) * 100.0);
                    ++regressed;
                }
            } else {
                base = "(new)";
            }
        }
        if (m.anomalies > 0) status += format(", %zu anomalies", m.anomalies);
        std::printf("%-24s %7zu %6d %11.1f %11.1f %11s %12s  %s\n", m.name.c_str(), m.events, m.runs, m.median_us,
                    m.p99_us, scaled.c_str(), base.c_str(), status.c_str());
        std::fflush(stdout);
        written_shots[m.name] = {
            {"median_us", m.median_us}, {"p99_us", m.p99_us}, {"events", m.events}, {"probe_units", m.probe_units}};
    }
    const double run_probe_us = stats_of(probes).p50;
    std::printf("speed probe: %.2f us this run", run_probe_us);
    if (baseline.comparable) {
        std::printf(", %.2f us when the baseline was written (this run is %.2fx its speed)", baseline.probe_us,
                    baseline.probe_us / run_probe_us);
    }
    std::printf("\n");

    int code = kExitOk;
    if (args.flag("write-baseline")) {
        const nlohmann::json written = {
            {"version", kBaselineVersion},
            {"note", "Written by ofs_sim_cli bench --write-baseline: microseconds, and each shot's time in "
                     "timings of the speed probe, which --check compares on this machine and build only."},
            {"params_hash", setup.params_hash},
            {"machine", machine},
            {"build", kBuild},
            {"probe_us", run_probe_us},
            {"shots", written_shots}};
        if (!write_text_file(baseline_path, written.dump(2) + "\n", error)) {
            std::fprintf(stderr, "bench: --write-baseline: %s\n", error.c_str());
            code = kExitUsage;
        } else {
            std::printf("baseline written to %s\n", baseline_path.c_str());
        }
    }
    if (check) {
        std::printf("check: %d over the %.0f us budget, ", over_budget, budget_us);
        if (baseline.comparable) {
            std::printf("%d regressed more than %.0f%% + %.0f us against the baseline\n", regressed,
                        (kRegression - 1.0) * 100.0, kRegressionSlackUs);
        } else {
            std::printf("regressions not checked: %s\n", baseline.why_not.c_str());
        }
        if (over_budget + regressed > 0 && code == kExitOk) code = kExitFailed;
    }
    return code;
}

}  // namespace sim_cli
