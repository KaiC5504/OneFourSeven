// sim_cli bench: fixed shots timed against the 2 ms budget of spec 7.13 and a recorded baseline.

#include <cmath>
#include <cstdio>
#include <filesystem>

#include "ofs/physics/rack.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

using Clock = std::chrono::steady_clock;
using ofs::table::Ball;

constexpr double kBudgetUs = 2000.0;
constexpr double kRegression = 1.25;

struct BenchShot {
    std::string name;
    phys::SimInput in;
};

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

struct Measured {
    std::string name;
    std::size_t events = 0;
    std::size_t anomalies = 0;
    int runs = 0;
    double median_us = 0.0;
    double p99_us = 0.0;
};

Measured measure(const BenchShot& shot, const Setup& setup, long long fixed_runs) {
    Measured m;
    m.name = shot.name;
    for (int i = 0; i < 2; ++i) {
        const phys::ShotRecord r = phys::simulate(shot.in, setup.table, setup.params);
        m.events = r.events.size();
        m.anomalies = r.anomalies.size();
    }
    std::vector<double> times;
    const auto begin = Clock::now();
    // Enough runs for a stable median: at least 30 and a quarter of a second, at most 2,000.
    for (;;) {
        const auto start = Clock::now();
        const phys::ShotRecord r = phys::simulate(shot.in, setup.table, setup.params);
        times.push_back(microseconds_since(start));
        const auto n = static_cast<long long>(times.size());
        const bool enough = (n >= 30 && microseconds_since(begin) >= 250000.0) || n >= 2000;
        if (fixed_runs > 0 ? n >= fixed_runs : enough) break;
    }
    const Stats s = stats_of(times);
    m.runs = static_cast<int>(times.size());
    m.median_us = s.p50;
    m.p99_us = s.p99;
    return m;
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
    const std::string baseline_path =
        args.value("baseline") ? *args.value("baseline") : repo_path("tools/sim_cli/bench_baseline.json");

    nlohmann::json baseline;
    bool have_baseline = false;
    if (std::filesystem::exists(std::filesystem::u8path(baseline_path))) {
        if (!read_json_file(baseline_path, baseline, error)) {
            return usage_error("bench", "--baseline: " + error);
        }
        if (!baseline.is_object() || !baseline.contains("shots") || !baseline["shots"].is_object()) {
            return usage_error("bench", baseline_path + ": want {\"shots\": {name: {\"median_us\": ...}}}");
        }
        have_baseline = true;
    }

#ifdef NDEBUG
    const char* build = "release";
#else
    const char* build = "debug";
#endif
    std::printf("bench: %s build, params %s, budget %.0f us per shot, baseline %s\n", build,
                setup.params_hash.c_str(), budget_us, have_baseline ? baseline_path.c_str() : "(none)");
    std::printf("%-24s %7s %6s %11s %11s %12s  %s\n", "shot", "events", "runs", "median_us", "p99_us",
                "baseline_us", "status");

    const bool check = args.flag("check");
    int over_budget = 0;
    int regressed = 0;
    nlohmann::json written = {{"version", 1},
                              {"note", "Written by ofs_sim_cli bench --write-baseline; microseconds."},
                              {"params_hash", setup.params_hash},
                              {"shots", nlohmann::json::object()}};
    for (const BenchShot& shot : bench_shots(setup)) {
        const Measured m = measure(shot, setup, runs);
        std::string status = "ok";
        std::string base = "-";
        if (m.median_us > budget_us) {
            status = format("OVER BUDGET (%.0f us)", budget_us);
            ++over_budget;
        }
        if (have_baseline) {
            const nlohmann::json& entry = baseline["shots"].value(shot.name, nlohmann::json());
            if (entry.is_object() && entry.contains("median_us") && entry["median_us"].is_number()) {
                const double before = entry["median_us"].get<double>();
                base = format("%.1f", before);
                if (m.median_us > before * kRegression) {
                    status = (status == "ok" ? std::string() : status + ", ") +
                             format("REGRESSED (+%.0f%%)", (m.median_us / before - 1.0) * 100.0);
                    ++regressed;
                }
            } else {
                base = "(new)";
            }
        }
        if (m.anomalies > 0) status += format(", %zu anomalies", m.anomalies);
        std::printf("%-24s %7zu %6d %11.1f %11.1f %12s  %s\n", m.name.c_str(), m.events, m.runs, m.median_us,
                    m.p99_us, base.c_str(), status.c_str());
        written["shots"][m.name] = {{"median_us", m.median_us}, {"p99_us", m.p99_us}, {"events", m.events}};
    }

    int code = kExitOk;
    if (args.flag("write-baseline")) {
        if (!write_text_file(baseline_path, written.dump(2) + "\n", error)) {
            std::fprintf(stderr, "bench: --write-baseline: %s\n", error.c_str());
            code = kExitUsage;
        } else {
            std::printf("baseline written to %s\n", baseline_path.c_str());
        }
    }
    if (check) {
        std::printf("check: %d over the %.0f us budget, %d regressed more than %.0f%% against the baseline\n",
                    over_budget, budget_us, regressed, (kRegression - 1.0) * 100.0);
        if (over_budget + regressed > 0 && code == kExitOk) code = kExitFailed;
    }
    return code;
}

}  // namespace sim_cli
