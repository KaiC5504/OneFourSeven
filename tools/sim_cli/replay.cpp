#include <cmath>
#include <cstdio>
#include <sstream>

#include "ofs/physics/repro.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

// What one replayable piece of the input holds: a logged shot.simulated or physics.anomaly, a
// {bundle, result} object (sim_cli fuzz writes those) or a bare bundle.
struct Item {
    int line = 0;  // 1-based in a JSONL file, 0 for a whole-file JSON object
    std::string kind;  // "shot.simulated", "physics.anomaly", "bundle"
    nlohmann::json data;
};

// The table every bundle is replayed on, and whether a bundle from another table may use it.
struct Table {
    phys::TableShape shape;
    std::string hash;
    std::string name;  // shareable_path of its file, for messages
    bool force = false;
};

// --update: a single-object file whose stored result is out of date (a recorded bench shot after a
// deliberate physics change) gets the replayed result and its bundle written back.
struct Update {
    bool enabled = false;
    std::string path;
};

struct Tally {
    int ok = 0;
    int diverged = 0;
    int reproduced = 0;
    int not_reproduced = 0;
    int replayed = 0;
    int errors = 0;
};

std::string where(const Item& item) {
    return item.line > 0 ? format("line %d", item.line) : std::string("file");
}

std::string event_entry(const nlohmann::json& result, int index) {
    const auto it = result.find("event_list");
    if (index < 0 || it == result.end() || !it->is_array() || static_cast<std::size_t>(index) >= it->size()) {
        return "(none)";
    }
    return (*it)[static_cast<std::size_t>(index)].dump();
}

// A bundle's events depend on the cushions it ran against, which it names only by hash: replayed on
// another outline it reports differences that are not divergences.
bool table_matches(const Item& item, const nlohmann::json& bundle, const Table& table, Tally& tally) {
    const auto recorded = bundle.find("table_hash");
    if (recorded == bundle.end() || (recorded->is_string() && recorded->get<std::string>().empty())) {
        std::printf("%-8s %-16s warning: the bundle has no table_hash, so its table can't be checked; "
                    "replaying on %s (%s)\n",
                    where(item).c_str(), item.kind.c_str(), table.name.c_str(), table.hash.c_str());
        return true;
    }
    if (!recorded->is_string()) {
        std::printf("%-8s %-16s ERROR        bundle unreadable: table_hash: want a string\n", where(item).c_str(),
                    item.kind.c_str());
        ++tally.errors;
        return false;
    }
    const std::string hash = recorded->get<std::string>();
    if (hash == table.hash) return true;
    if (table.force) {
        std::printf("%-8s %-16s warning: the bundle ran on table %s; replaying on %s (%s) as --force-table "
                    "asks\n",
                    where(item).c_str(), item.kind.c_str(), hash.c_str(), table.name.c_str(), table.hash.c_str());
        return true;
    }
    std::printf("%-8s %-16s ERROR        table mismatch: the bundle ran on table %s, but %s is %s; pass that "
                "table's file with --table, or --force-table to replay on this one anyway\n",
                where(item).c_str(), item.kind.c_str(), hash.c_str(), table.name.c_str(), table.hash.c_str());
    ++tally.errors;
    return false;
}

bool replay_bundle(const Item& item, const nlohmann::json& bundle, const Table& table, phys::SimInput& in,
                   phys::PhysicsParams& p, phys::ShotRecord& r, Tally& tally) {
    std::string error;
    if (!bundle.is_object() || !phys::parse_bundle(bundle, in, p, error)) {
        std::printf("%-8s %-16s ERROR        bundle unreadable: %s\n", where(item).c_str(), item.kind.c_str(),
                    bundle.is_object() ? error.c_str() : "not an object");
        ++tally.errors;
        return false;
    }
    if (!table_matches(item, bundle, table, tally)) return false;
    r = phys::simulate(in, table.shape, p);
    return true;
}

// The bundle is written again from what was parsed, so an old one comes out at the current version
// with params_values_hash, naming the table it was just replayed on.
bool write_update(const Update& update, const Item& item, const Table& table, phys::SimInput in,
                  const phys::PhysicsParams& p, const nlohmann::json& replayed) {
    nlohmann::json file = item.data;
    in.table_hash = table.hash;
    file["bundle"] = phys::make_bundle(in, p);
    file["result"] = replayed;
    std::string error;
    if (!write_text_file(update.path, file.dump(1) + "\n", error)) {
        std::printf("%-8s %-16s ERROR        not updated: %s\n", where(item).c_str(), item.kind.c_str(),
                    error.c_str());
        return false;
    }
    std::printf("%-8s %-16s UPDATED      %s\n", where(item).c_str(), item.kind.c_str(), update.path.c_str());
    return true;
}

void replay_shot(const Item& item, const Table& table, double tolerance, const Update& update, Tally& tally) {
    const auto bundle = item.data.find("bundle");
    const auto logged = item.data.find("result");
    if (bundle == item.data.end()) {
        std::printf("%-8s %-16s ERROR        no bundle\n", where(item).c_str(), item.kind.c_str());
        ++tally.errors;
        return;
    }
    phys::SimInput in;
    phys::PhysicsParams p;
    phys::ShotRecord r;
    if (!replay_bundle(item, *bundle, table, in, p, r, tally)) return;
    const nlohmann::json replayed = phys::make_result(r);
    if (update.enabled) {
        if (write_update(update, item, table, in, p, replayed)) {
            ++tally.ok;
        } else {
            ++tally.errors;
        }
        return;
    }
    if (logged == item.data.end() || !logged->is_object()) {
        std::printf("%-8s %-16s REPLAYED     %s (no logged result to compare)\n", where(item).c_str(),
                    item.kind.c_str(), record_summary(r).c_str());
        ++tally.replayed;
        return;
    }
    const phys::Divergence d = phys::compare_results(*logged, replayed, tolerance);
    if (!d.diverged) {
        std::printf("%-8s %-16s OK           %s\n", where(item).c_str(), item.kind.c_str(),
                    record_summary(r).c_str());
        ++tally.ok;
        return;
    }
    std::printf("%-8s %-16s DIVERGED     %s\n", where(item).c_str(), item.kind.c_str(), d.summary.c_str());
    std::printf("         max final position error %.3g m\n", d.max_position_error_m);
    if (d.first_event >= 0) {
        std::printf("         first diverging event %d: logged %s, replayed %s\n", d.first_event,
                    event_entry(*logged, d.first_event).c_str(), event_entry(replayed, d.first_event).c_str());
    } else {
        std::printf("         the event lists agree\n");
    }
    ++tally.diverged;
}

void replay_anomaly(const Item& item, const Table& table, Tally& tally) {
    const auto bundle = item.data.find("bundle");
    if (bundle == item.data.end()) {
        std::printf("%-8s %-16s ERROR        no bundle\n", where(item).c_str(), item.kind.c_str());
        ++tally.errors;
        return;
    }
    phys::Anomaly logged;
    logged.kind = item.data.value("kind", std::string());
    const auto t = item.data.find("t");
    logged.t = t != item.data.end() && t->is_number() ? t->get<double>() : std::nan("");
    if (const auto balls = item.data.find("balls"); balls != item.data.end() && balls->is_array()) {
        for (const auto& b : *balls) {
            if (b.is_number_integer()) logged.balls.push_back(b.get<int>());
        }
    }
    logged.detail = item.data.value("detail", std::string());
    phys::SimInput in;
    phys::PhysicsParams p;
    phys::ShotRecord r;
    if (!replay_bundle(item, *bundle, table, in, p, r, tally)) return;
    const phys::Anomaly* same_kind = nullptr;
    for (const phys::Anomaly& a : r.anomalies) {
        if (a.kind != logged.kind) continue;
        if (same_kind == nullptr) same_kind = &a;
        // Event times agree to 1e-9 s when a replay matches (repro.hpp's compare_results).
        if (a.balls == logged.balls && std::fabs(a.t - logged.t) <= 1e-9) {
            std::printf("%-8s %-16s REPRODUCED   %s\n", where(item).c_str(), item.kind.c_str(),
                        anomaly_text(a).c_str());
            ++tally.reproduced;
            return;
        }
    }
    std::printf("%-8s %-16s NOT REPRODUCED  logged %s\n", where(item).c_str(), item.kind.c_str(),
                anomaly_text(logged).c_str());
    if (same_kind != nullptr) {
        std::printf("         the replay had %s\n", anomaly_text(*same_kind).c_str());
    } else {
        std::string kinds;
        for (const phys::Anomaly& a : r.anomalies) kinds += (kinds.empty() ? "" : ", ") + a.kind;
        std::printf("         the replay had no %s anomaly (anomalies: %s)\n", logged.kind.c_str(),
                    kinds.empty() ? "none" : kinds.c_str());
    }
    ++tally.not_reproduced;
}

bool is_replayable(const nlohmann::json& line) {
    if (!line.is_object() || !line.contains("data") || !line["data"].is_object()) return false;
    const auto event = line.find("event");
    return event != line.end() && event->is_string() &&
           (*event == "shot.simulated" || *event == "physics.anomaly");
}

// A whole-file JSON object is one log line, a {bundle, result} pair or a bare bundle; anything else
// is read as JSONL.
bool collect_items(const std::string& path, const std::string& text, long long only_line,
                   std::vector<Item>& items, std::string& error) {
    const nlohmann::json whole = nlohmann::json::parse(text, nullptr, false);
    if (whole.is_object() && !(whole.contains("event") && whole.contains("data"))) {
        if (only_line > 0) {
            error = "--line applies to JSONL logs; " + path + " is a single JSON object";
            return false;
        }
        Item item;
        item.kind = "bundle";
        item.data = whole.contains("bundle") ? whole : nlohmann::json{{"bundle", whole}};
        items.push_back(std::move(item));
        return true;
    }
    std::istringstream lines(text);
    std::string line;
    int number = 0;
    int unreadable = 0;
    bool found_line = false;
    while (std::getline(lines, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (only_line > 0 && number != only_line) continue;
        found_line = true;
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        const nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded()) {
            ++unreadable;
            std::printf("line %d: not JSON, skipped\n", number);
            continue;
        }
        if (!is_replayable(j)) {
            if (only_line > 0) {
                error = format("line %d is not a shot.simulated or physics.anomaly event", number);
                return false;
            }
            continue;
        }
        items.push_back({number, j["event"].get<std::string>(), j["data"]});
    }
    if (only_line > 0 && !found_line) {
        error = format("--line %lld: %s has %d lines", only_line, path.c_str(), number);
        return false;
    }
    if (items.empty()) {
        error = path + " has no shot.simulated or physics.anomaly lines to replay" +
                (unreadable > 0 ? format(" (%d lines were not JSON)", unreadable) : std::string());
        return false;
    }
    return true;
}

}  // namespace

int cmd_replay(const std::vector<std::string>& argv) {
    const ArgSpec spec{{"line", "tolerance", "table"}, {"force-table", "update"}, 1, 1};
    Args args;
    std::string error;
    if (!parse_args(argv, spec, args, error)) return usage_error("replay", error);
    long long only_line = 0;
    double tolerance = 1e-6;
    if (!option_int(args, "line", 1, 1LL << 40, only_line, error) ||
        !option_double(args, "tolerance", 0.0, 1.0, tolerance, error)) {
        return usage_error("replay", error);
    }
    // A bundle carries its parameters but not the table outline, which comes from the local file.
    Table table;
    const std::string table_file = args.value("table") ? *args.value("table") : data_path("table_shape.json");
    if (!load_table(table_file, table.shape, table.hash)) return usage_error("replay", table_file + " was refused");
    table.name = shareable_path(table_file);
    table.force = args.flag("force-table");

    const std::string& path = args.positional[0];
    std::string text;
    if (!read_text_file(path, text, error)) return usage_error("replay", error);
    std::vector<Item> items;
    if (!collect_items(path, text, only_line, items, error)) return usage_error("replay", error);
    const Update update{args.flag("update"), path};
    if (update.enabled && (items.size() != 1 || items[0].line != 0 || items[0].kind != "bundle")) {
        return usage_error("replay", "--update rewrites a single {bundle, result} file, not a log");
    }

    Tally tally;
    for (const Item& item : items) {
        if (item.kind == "physics.anomaly") {
            replay_anomaly(item, table, tally);
        } else {
            replay_shot(item, table, tolerance, update, tally);
        }
    }
    std::printf("replayed %zu: %d ok, %d diverged, %d anomalies reproduced, %d not reproduced, "
                "%d without a result, %d errors (tolerance %g m)\n",
                items.size(), tally.ok, tally.diverged, tally.reproduced, tally.not_reproduced, tally.replayed,
                tally.errors, tolerance);
    return tally.diverged + tally.not_reproduced + tally.errors > 0 ? kExitFailed : kExitOk;
}

}  // namespace sim_cli
