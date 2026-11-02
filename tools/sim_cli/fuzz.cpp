// sim_cli fuzz: C13 and C14 over a reproducible set of random shots. Every shot is seeded by
// (seed, index), so the set and every result are the same whatever the thread count.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <thread>

#include "ofs/params.hpp"
#include "ofs/physics/repro.hpp"
#include "ofs/physics/serialize.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kDeterminismEvery = 100;

struct Outcome {
    FuzzKind kind = FuzzKind::scattered;
    std::size_t balls = 0;
    std::size_t events = 0;
    double sim_us = 0.0;
    int fallbacks = 0;
    bool miscue = false;
    bool determinism_checked = false;
    std::string input_hash;
    std::vector<ShotFailure> failures;
    std::string bundle_path;
};

struct Worker {
    std::atomic<long long> shot{-1};
    std::atomic<long long> started_ns{0};
    std::atomic<bool> finished{false};
    long long flagged_shot = -1;  // main thread only
};

struct Run {
    std::uint64_t seed = 1;
    std::uint64_t shots = 0;
    std::string out_dir;
    std::string inject_anomaly;  // --inject-anomaly: proves the failure path without a physics fault
    const Setup* setup = nullptr;
    const PlayArea* area = nullptr;
    std::vector<Outcome> outcomes;
    std::unique_ptr<std::atomic<bool>[]> done;
    std::atomic<std::uint64_t> next{0};
};

long long now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

nlohmann::json input_json(const phys::SimInput& in) {
    return {{"seed", std::to_string(in.seed)},
            {"cue_ball", in.cue_ball},
            {"has_strike", in.has_strike},
            {"strike", phys::to_json(in.strike)},
            {"start", phys::to_json(in.start)}};
}

std::string bundle_file(const Run& run, std::uint64_t index) {
    return (fs::u8path(run.out_dir) / format("fuzz-%llu-%06llu.json", static_cast<unsigned long long>(run.seed),
                                            static_cast<unsigned long long>(index)))
        .u8string();
}

// {fuzz, bundle, result}: `sim_cli replay` reads it as a bundle with its logged result.
std::string write_bundle(const Run& run, std::uint64_t index, const FuzzShot& shot, const phys::ShotRecord* r,
                         const std::vector<ShotFailure>& failures) {
    nlohmann::json listed = nlohmann::json::array();
    for (const ShotFailure& f : failures) listed.push_back({{"kind", f.kind}, {"detail", f.detail}});
    nlohmann::json file = {{"fuzz",
                            {{"seed", std::to_string(run.seed)},
                             {"shot", index},
                             {"kind", fuzz_kind_name(shot.kind)},
                             {"failures", listed}}},
                           {"bundle", phys::make_bundle(shot.in, run.setup->params)}};
    if (r != nullptr) file["result"] = phys::make_result(*r);
    const std::string path = bundle_file(run, index);
    std::string error;
    if (!write_text_file(path, file.dump(1) + "\n", error)) return "(not written: " + error + ")";
    return path;
}

void run_shot(Run& run, std::uint64_t index) {
    Outcome& o = run.outcomes[index];
    const FuzzShot shot = make_fuzz_shot(run.seed, index, *run.setup, *run.area);
    o.kind = shot.kind;
    o.balls = shot.in.start.balls.size();
    o.input_hash = ofs::params::fnv1a64_hex(input_json(shot.in).dump());
    phys::ShotRecord r;
    const auto start = Clock::now();
    try {
        r = phys::simulate(shot.in, run.setup->table, run.setup->params);
    } catch (const std::exception& e) {
        o.failures.push_back({"exception", e.what()});
        o.bundle_path = write_bundle(run, index, shot, nullptr, o.failures);
        return;
    }
    o.sim_us = microseconds_since(start);
    o.events = r.events.size();
    o.miscue = r.strike.miscue;
    std::vector<ShotFailure> nondeterminism;
    if (index % kDeterminismEvery == 0) {
        o.determinism_checked = true;
        const std::string first = phys::record_json(r).dump();
        const phys::ShotRecord again = phys::simulate(shot.in, run.setup->table, run.setup->params);
        const std::string second = phys::record_json(again).dump();
        if (first != second) {
            const auto diff = std::mismatch(first.begin(), first.end(), second.begin(), second.end());
            const std::size_t at = static_cast<std::size_t>(diff.first - first.begin());
            nondeterminism.push_back(
                {"nondeterministic", format("a second run's record differs from byte %zu: ...%s... vs ...%s...",
                                            at, first.substr(at, 40).c_str(), second.substr(at, 40).c_str())});
        }
    }
    if (!run.inject_anomaly.empty()) {
        r.anomalies.push_back({run.inject_anomaly, 0.0, {shot.in.cue_ball}, "injected by --inject-anomaly"});
    }
    for (const phys::Anomaly& a : r.anomalies) o.fallbacks += a.kind == "solver_fallback" ? 1 : 0;
    o.failures = check_record(shot.in, r, *run.area);
    o.failures.insert(o.failures.end(), nondeterminism.begin(), nondeterminism.end());
    if (!o.failures.empty()) o.bundle_path = write_bundle(run, index, shot, &r, o.failures);
}

void work(Run& run, Worker& w) {
    for (;;) {
        const std::uint64_t index = run.next.fetch_add(1);
        if (index >= run.shots) break;
        w.started_ns.store(now_ns());
        w.shot.store(static_cast<long long>(index));
        run_shot(run, index);
        run.done[index].store(true, std::memory_order_release);
        w.shot.store(-1);
    }
    w.finished.store(true);
}

// Removes the failure bundles an earlier run left, so the folder only ever lists this run's.
void clear_old_bundles(const std::string& dir) {
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().u8string();
        std::error_code entry_ec;
        const bool ours = name.compare(0, 5, "fuzz-") == 0 && it->path().extension() == ".json";
        if (ours && it->is_regular_file(entry_ec)) {
            fs::remove(it->path(), entry_ec);
        }
    }
}

}  // namespace

int cmd_fuzz(const std::vector<std::string>& argv) {
    const ArgSpec spec{
        {"shots", "seed", "threads", "out", "hang-seconds", "params", "table", "inject-anomaly"}, {}, 0, 0};
    Args args;
    std::string error;
    if (!parse_args(argv, spec, args, error)) return usage_error("fuzz", error);
    long long shots = 1000;
    std::uint64_t seed = 1;
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    long long threads = hardware;
    double hang_seconds = 30.0;
    if (!option_int(args, "shots", 1, 1LL << 40, shots, error) || !option_u64(args, "seed", seed, error) ||
        !option_int(args, "threads", 1, 1024, threads, error) ||
        !option_double(args, "hang-seconds", 0.1, 86400.0, hang_seconds, error)) {
        return usage_error("fuzz", error);
    }
    Setup setup;
    if (!load_setup(args, setup)) return kExitUsage;
    const PlayArea area(setup.table);

    Run run;
    run.seed = seed;
    run.shots = static_cast<std::uint64_t>(shots);
    run.out_dir = args.value("out") ? *args.value("out") : repo_path("build/fuzz-failures");
    if (const std::string* kind = args.value("inject-anomaly")) {
        if (kind->empty()) return usage_error("fuzz", "--inject-anomaly needs an anomaly kind");
        run.inject_anomaly = *kind;
    }
    run.setup = &setup;
    run.area = &area;
    run.outcomes.resize(run.shots);
    run.done.reset(new std::atomic<bool>[run.shots]);
    for (std::uint64_t i = 0; i < run.shots; ++i) run.done[i].store(false);
    clear_old_bundles(run.out_dir);

    const std::size_t thread_count = static_cast<std::size_t>(std::min<long long>(threads, shots));
    std::printf("fuzz: %lld shots, seed %llu, %zu threads, params %s, table %s\n", shots,
                static_cast<unsigned long long>(seed), thread_count, setup.params_hash.c_str(),
                setup.table_hash.c_str());
    std::fflush(stdout);

    const auto start = Clock::now();
    std::vector<std::unique_ptr<Worker>> workers;
    std::vector<std::thread> pool;
    for (std::size_t i = 0; i < thread_count; ++i) workers.push_back(std::make_unique<Worker>());
    for (std::size_t i = 0; i < thread_count; ++i) {
        pool.emplace_back(work, std::ref(run), std::ref(*workers[i]));
    }

    // A shot that runs past --hang-seconds is a hang (C14). Its thread can't be stopped, so the
    // run reports it, lets the other threads finish and exits without joining.
    std::map<std::uint64_t, std::string> hangs;
    const long long hang_ns = static_cast<long long>(hang_seconds * 1e9);
    for (;;) {
        bool settled = true;
        for (auto& w : workers) {
            if (w->finished.load()) continue;
            const long long shot = w->shot.load();
            const bool stuck = shot >= 0 && now_ns() - w->started_ns.load() > hang_ns;
            if (stuck && w->flagged_shot != shot) {
                w->flagged_shot = shot;
                const auto index = static_cast<std::uint64_t>(shot);
                const FuzzShot again = make_fuzz_shot(seed, index, setup, area);
                const std::vector<ShotFailure> failure{
                    {"hang", format("still simulating after %g s", hang_seconds)}};
                hangs[index] = write_bundle(run, index, again, nullptr, failure);
                std::printf("HANG shot %llu (%s, %zu balls): still simulating after %g s; bundle %s\n",
                            static_cast<unsigned long long>(index), fuzz_kind_name(again.kind),
                            again.in.start.balls.size(), hang_seconds, hangs[index].c_str());
                std::fflush(stdout);
            }
            if (!(stuck && w->flagged_shot == shot)) settled = false;
        }
        if (settled) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const double seconds = microseconds_since(start) / 1e6;
    const bool hung = !hangs.empty();
    if (hung) {
        for (std::thread& t : pool) t.detach();
    } else {
        for (std::thread& t : pool) t.join();
    }

    std::map<std::string, int> by_kind;
    int kind_counts[kFuzzKinds] = {};
    int failed_shots = 0;
    int determinism_checks = 0;
    int fallbacks = 0;
    int miscues = 0;
    std::size_t max_events = 0;
    double total_events = 0.0;
    std::vector<double> sim_us;
    std::string digests;
    std::uint64_t completed = 0;
    for (std::uint64_t i = 0; i < run.shots; ++i) {
        if (!run.done[i].load(std::memory_order_acquire)) continue;
        const Outcome& o = run.outcomes[i];
        ++completed;
        ++kind_counts[static_cast<int>(o.kind)];
        digests += o.input_hash;
        total_events += static_cast<double>(o.events);
        max_events = std::max(max_events, o.events);
        sim_us.push_back(o.sim_us);
        fallbacks += o.fallbacks;
        miscues += o.miscue ? 1 : 0;
        determinism_checks += o.determinism_checked ? 1 : 0;
        if (o.failures.empty() && hangs.count(i) == 0) continue;
        ++failed_shots;
        std::printf("FAIL shot %llu (%s, %zu balls)\n", static_cast<unsigned long long>(i),
                    fuzz_kind_name(o.kind), o.balls);
        for (const ShotFailure& f : o.failures) {
            ++by_kind[f.kind];
            std::printf("     %s: %s\n", f.kind.c_str(), f.detail.c_str());
        }
        if (!o.bundle_path.empty()) std::printf("     bundle: %s\n", o.bundle_path.c_str());
    }
    for (const auto& [index, path] : hangs) {
        ++by_kind["hang"];
        if (!run.done[index].load(std::memory_order_acquire)) ++failed_shots;
    }

    std::printf("shots %llu of %lld (", static_cast<unsigned long long>(completed), shots);
    for (int k = 0; k < kFuzzKinds; ++k) {
        std::printf("%s%s %d", k == 0 ? "" : ", ", fuzz_kind_name(static_cast<FuzzKind>(k)), kind_counts[k]);
    }
    std::printf("), %d failed\n", failed_shots);
    std::string kinds;
    for (const auto& [kind, count] : by_kind) {
        kinds += (kinds.empty() ? "" : ", ") + kind + " " + std::to_string(count);
    }
    std::printf("failures by kind: %s\n", kinds.empty() ? "none" : kinds.c_str());
    std::printf("determinism (C13): %d shots simulated twice, every %llu shots\n", determinism_checks,
                static_cast<unsigned long long>(kDeterminismEvery));
    const double mean_events = completed > 0 ? total_events / static_cast<double>(completed) : 0.0;
    std::printf("events: mean %.1f, max %zu, total %.0f; miscues %d; solver fallbacks %d\n", mean_events,
                max_events, total_events, miscues, fallbacks);
    const Stats t = stats_of(sim_us);
    std::printf("sim time: p50 %.1f us, p99 %.1f us, max %.1f us\n", t.p50, t.p99, t.max);
    const double rate = seconds > 0.0 ? static_cast<double>(completed) / seconds : 0.0;
    std::printf("speed: %.0f shots/s over %.2f s\n", rate, seconds);
    std::printf("inputs digest: %s\n", ofs::params::fnv1a64_hex(digests).c_str());
    if (failed_shots > 0) std::printf("failure bundles in %s\n", run.out_dir.c_str());
    std::fflush(stdout);
    if (hung) std::_Exit(kExitFailed);
    return failed_shots > 0 ? kExitFailed : kExitOk;
}

}  // namespace sim_cli
