#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>

#include "ofs/log.hpp"
#include "ofs/physics/repro.hpp"
#include "ofs/physics/serialize.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

// One JSONL file at a path of the caller's choosing (FileSink names its own files by session).
class LineFileSink : public ofs::log::Sink {
public:
    explicit LineFileSink(const std::string& path) {
        const std::filesystem::path file = std::filesystem::u8path(path);
        std::error_code ec;
        if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
        out_.open(file, std::ios::binary | std::ios::trunc);
        if (!out_) error_ = "cannot open " + path + " for writing";
    }

    void write(const ofs::log::Event&, const std::string& line) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!error_.empty()) return;
        out_ << line << '\n';
        out_.flush();
        if (!out_) error_ = "write failed";
    }

    std::string error() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_;
    }

private:
    mutable std::mutex mutex_;
    std::ofstream out_;
    std::string error_;
};

const char* const kStrikeKeys[] = {"aim_rad", "tip_x", "tip_y", "elevation_rad", "speed_m_s"};

// --strike overrides the position's strike key by key, so '{"speed_m_s": 5}' is enough.
bool merge_strike(const std::string& text, nlohmann::json& strike, std::string& error) {
    nlohmann::json patch;
    try {
        patch = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        error = std::string("--strike is not JSON: ") + e.what();
        return false;
    }
    if (!patch.is_object()) {
        error = "--strike: want a JSON object";
        return false;
    }
    for (const auto& [key, value] : patch.items()) {
        bool known = false;
        for (const char* name : kStrikeKeys) known = known || key == name;
        if (!known) {
            error = "--strike: unknown key '" + key + "'";
            return false;
        }
        strike[key] = value;
    }
    return true;
}

void print_summary(const Position& pos, const phys::SimInput& in, const Setup& setup,
                   const phys::ShotRecord& r) {
    std::printf("position   %s: %zu balls, cue ball %d\n", pos.name.c_str(), in.start.balls.size(),
                in.cue_ball);
    if (in.has_strike) {
        std::printf("strike     aim %.6f rad, tip (%.3f, %.3f) R, elevation %.4f rad, speed %.4f m/s, "
                    "seed %llu\n",
                    in.strike.aim_rad, in.strike.tip_x, in.strike.tip_y, in.strike.elevation_rad,
                    in.strike.speed_m_s, static_cast<unsigned long long>(in.seed));
    } else {
        std::printf("strike     none (the balls move as given), seed %llu\n",
                    static_cast<unsigned long long>(in.seed));
    }
    std::printf("params     %s, table %s\n", setup.params_hash.c_str(), setup.table_hash.c_str());
    std::map<std::string, int> kinds;
    for (const phys::ShotEvent& e : r.events) ++kinds[std::string(phys::event_kind_name(e.kind))];
    std::string by_kind;
    for (const auto& [kind, count] : kinds) {
        by_kind += (by_kind.empty() ? ": " : ", ") + kind + " " + std::to_string(count);
    }
    std::printf("events     %zu%s\n", r.events.size(), by_kind.c_str());
    std::printf("t_end      %.6f s\n", r.t_end);
    std::printf("sim        %.0f us\n", r.sim_us);
    std::string potted;
    for (const phys::ShotEvent& e : r.events) {
        if (e.kind != phys::EventKind::pocket) continue;
        const std::string ball = e.a >= 0 && static_cast<std::size_t>(e.a) < in.start.balls.size()
                                     ? ball_label(in.start.balls[static_cast<std::size_t>(e.a)])
                                     : "ball " + std::to_string(e.a);
        const std::string pocket = pocket_label(setup.table, e.body);
        potted += (potted.empty() ? "" : "; ") +
                  format("%s (%d) in %s at %.6f s", ball.c_str(), e.a, pocket.c_str(), e.t);
    }
    std::printf("potted     %s\n", potted.empty() ? "none" : potted.c_str());
    if (in.has_strike) {
        const double squirt_deg = r.strike.squirt_rad * 180.0 / 3.14159265358979323846;
        std::printf("cue        miscue %s, elevation used %.4f rad (raised %s), squirt %.3f deg\n",
                    r.strike.miscue ? "yes" : "no", r.strike.elevation_used_rad,
                    r.strike.elevation_raised ? "yes" : "no", squirt_deg);
    }
    if (r.anomalies.empty()) {
        std::printf("anomalies  none\n");
    } else {
        std::printf("anomalies  %zu\n", r.anomalies.size());
        for (const phys::Anomaly& a : r.anomalies) std::printf("           %s\n", anomaly_text(a).c_str());
    }
}

}  // namespace

int cmd_simulate(const std::vector<std::string>& argv) {
    const ArgSpec spec{{"strike", "seed", "params", "table", "out", "log"}, {"no-strike"}, 1, 1};
    Args args;
    std::string error;
    if (!parse_args(argv, spec, args, error)) return usage_error("simulate", error);
    std::uint64_t seed = 0;
    if (!option_u64(args, "seed", seed, error)) return usage_error("simulate", error);

    configure_logger();
    std::shared_ptr<LineFileSink> sink;
    if (const std::string* log_path = args.value("log")) {
        sink = std::make_shared<LineFileSink>(*log_path);
        if (!sink->error().empty()) return usage_error("simulate", "--log: " + sink->error());
        // Before the parameters load, so a refused value's params.invalid lands in the log too.
        ofs::log::Logger::instance().add_sink(sink);
    }
    struct DropSinks {
        ~DropSinks() { ofs::log::Logger::instance().clear_sinks(); }
    } drop_sinks;

    Setup setup;
    if (!load_setup(args, setup)) return kExitUsage;
    ofs::log::info("params", "params.loaded",
                   {{"source", args.value("params") ? *args.value("params") : "core/data/table_params.json"},
                    {"hash", setup.params_hash}});

    Position pos;
    if (!load_position(args.positional[0], setup.params, pos, error)) return usage_error("simulate", error);
    nlohmann::json strike = pos.has_strike ? pos.strike : phys::to_json(phys::CueStrike{});
    bool has_strike = pos.has_strike;
    if (const std::string* text = args.value("strike")) {
        if (!merge_strike(*text, strike, error)) return usage_error("simulate", error);
        has_strike = true;
    }
    if (args.flag("no-strike")) has_strike = false;

    phys::SimInput in;
    in.start = pos.start;
    in.cue_ball = pos.cue_ball;
    in.has_strike = has_strike;
    in.seed = seed;
    in.params_hash = setup.params_hash;
    if (!phys::from_json(strike, in.strike, error)) return usage_error("simulate", error);
    if (has_strike && (in.cue_ball < 0 || static_cast<std::size_t>(in.cue_ball) >= in.start.balls.size())) {
        return usage_error("simulate", format("cue_ball %d is not one of the %zu balls", in.cue_ball,
                                              in.start.balls.size()));
    }

    const phys::ShotRecord r = phys::simulate(in, setup.table, setup.params);
    print_summary(pos, in, setup, r);

    int code = kExitOk;
    if (const std::string* out = args.value("out")) {
        if (!write_text_file(*out, phys::record_json(r).dump(2) + "\n", error)) {
            std::fprintf(stderr, "simulate: --out: %s\n", error.c_str());
            code = kExitUsage;
        } else {
            std::printf("record     %s\n", out->c_str());
        }
    }
    if (sink) {
        phys::log_shot(in, setup.params, r);
        ofs::log::Logger::instance().flush();
        if (!sink->error().empty()) {
            std::fprintf(stderr, "simulate: --log: %s\n", sink->error().c_str());
            code = kExitUsage;
        } else {
            std::printf("log        %s\n", args.value("log")->c_str());
        }
    }
    return code;
}

}  // namespace sim_cli
