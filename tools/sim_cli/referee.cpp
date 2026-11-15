// sim_cli referee: the referee's logged decisions judged again (spec 16.10). A rules.decision
// line carries all it was judged from; where the log also holds the stroke's shot.simulated line,
// the shot is replayed and read again, so what the referee saw is checked as well as the ruling.

#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>

#include "ofs/physics/repro.hpp"
#include "ofs/rules/facts.hpp"
#include "ofs/rules/rules_log.hpp"
#include "ofs/rules/serialize.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

namespace rules = ofs::rules;
using nlohmann::json;

constexpr double kRestTolerance_m = 1e-6;
// Event times differ across platforms by libm rounding, far below this.
constexpr double kPotTimeTolerance_s = 1e-6;

struct Line {
    int number = 0;
    json data;
};

struct Tally {
    int judged = 0;
    int replayed = 0;
    int differences = 0;
    int unreadable = 0;
};

std::string shot_id_of(const json& holder) {
    const auto it = holder.find("shot_id");
    return it != holder.end() && it->is_string() ? it->get<std::string>() : std::string();
}

void differs(Tally& tally, int line, const char* what, const std::string& text) {
    std::printf("line %-6d %-15s DIFFERS  %s\n", line, what, text.c_str());
    ++tally.differences;
}

void unreadable(Tally& tally, int line, const char* what, const std::string& text) {
    std::printf("line %-6d %-15s ERROR    %s\n", line, what, text.c_str());
    ++tally.unreadable;
}

std::string places(const std::vector<int>& list) { return json(list).dump(); }

std::string pots_text(const std::vector<rules::Pot>& pots) {
    json out = json::array();
    for (const rules::Pot& p : pots) out.push_back({p.ball, p.pocket});
    return out.dump();
}

// The stroke simulated again from its bundle and read again from the decision's own frame: every
// fact the ruling rests on must come out as logged.
void replay(const Line& decision, const Line& shot, const phys::TableShape& table, const std::string& table_hash,
            Tally& tally) {
    const std::string from = format("(shot.simulated at line %d) ", shot.number);
    const auto bundle = shot.data.find("bundle");
    phys::SimInput in;
    phys::PhysicsParams params;
    std::string error;
    if (bundle == shot.data.end() || !bundle->is_object() || !phys::parse_bundle(*bundle, in, params, error)) {
        return unreadable(tally, shot.number, "shot.simulated",
                          "bundle unreadable: " + (error.empty() ? "no bundle" : error));
    }
    if (!in.table_hash.empty() && in.table_hash != table_hash) {
        return unreadable(tally, shot.number, "shot.simulated",
                          "the shot ran on table " + in.table_hash + ", this one is " + table_hash +
                              "; pass that table's file with --table");
    }
    rules::FrameState pre;
    rules::ShotFacts logged;
    rules::RefParams ref;
    std::vector<std::string> problems;
    // rejudge has already reported a line these cannot be read from.
    if (!rules::frame_from_json(decision.data["pre"], pre, error) ||
        !rules::facts_from_json(decision.data["facts"], logged, error) ||
        !rules::ref_params_from_values(decision.data["ref"], ref, problems)) {
        return;
    }
    const phys::ShotRecord record = phys::simulate(in, table, params);
    const rules::ShotFacts now = rules::observe(pre, record, table, ref);
    ++tally.replayed;

    if (now.first_hit != logged.first_hit) {
        differs(tally, decision.number, "shot.simulated",
                from + "facts.first_hit: logged " + places(logged.first_hit) + ", replayed " + places(now.first_hit));
    }
    bool same_pots = now.pots.size() == logged.pots.size();
    for (std::size_t i = 0; same_pots && i < now.pots.size(); ++i) {
        same_pots = now.pots[i].ball == logged.pots[i].ball && now.pots[i].pocket == logged.pots[i].pocket &&
                    std::fabs(now.pots[i].t - logged.pots[i].t) <= kPotTimeTolerance_s;
    }
    if (!same_pots) {
        differs(tally, decision.number, "shot.simulated",
                from + "facts.pots ([ball, pocket]): logged " + pots_text(logged.pots) + ", replayed " +
                    pots_text(now.pots));
    }
    if (now.cue_potted != logged.cue_potted || now.cue_pocket != logged.cue_pocket) {
        differs(tally, decision.number, "shot.simulated",
                from + format("facts.cue_potted: logged %s in pocket %d, replayed %s in pocket %d",
                              logged.cue_potted ? "true" : "false", logged.cue_pocket,
                              now.cue_potted ? "true" : "false", now.cue_pocket));
    }
    if (now.push != logged.push) {
        differs(tally, decision.number, "shot.simulated",
                from + format("facts.push: logged %s, replayed %s", logged.push ? "true" : "false",
                              now.push ? "true" : "false"));
    }
    if (now.touching_moved != logged.touching_moved) {
        differs(tally, decision.number, "shot.simulated",
                from + "facts.touching_moved: logged " + places(logged.touching_moved) + ", replayed " +
                    places(now.touching_moved));
    }
    if (now.anomaly != logged.anomaly) {
        differs(tally, decision.number, "shot.simulated",
                from + format("facts.anomaly: logged %s, replayed %s", logged.anomaly ? "true" : "false",
                              now.anomaly ? "true" : "false"));
    }
    if (now.rest.size() != logged.rest.size()) {
        return differs(tally, decision.number, "shot.simulated",
                       from + format("facts.rest: logged %zu balls, replayed %zu", logged.rest.size(),
                                     now.rest.size()));
    }
    for (std::size_t i = 0; i < now.rest.size(); ++i) {
        const double off = std::hypot(now.rest[i].x - logged.rest[i].x, now.rest[i].y - logged.rest[i].y);
        if (off <= kRestTolerance_m) continue;
        return differs(tally, decision.number, "shot.simulated",
                       from + format("facts.rest[%zu]: logged (%.9g, %.9g), replayed (%.9g, %.9g), %.3g m apart", i,
                                     logged.rest[i].x, logged.rest[i].y, now.rest[i].x, now.rest[i].y, off));
    }
}

}  // namespace

int cmd_referee(const std::vector<std::string>& argv) {
    const ArgSpec spec{{"line", "table"}, {}, 1, 1};
    Args args;
    std::string error;
    if (!parse_args(argv, spec, args, error)) return usage_error("referee", error);
    long long only_line = 0;
    if (!option_int(args, "line", 1, 1LL << 40, only_line, error)) return usage_error("referee", error);
    phys::TableShape table;
    std::string table_hash;
    const std::string table_file = args.value("table") ? *args.value("table") : data_path("table_shape.json");
    if (!load_table(table_file, table, table_hash)) return usage_error("referee", table_file + " was refused");

    const std::string& path = args.positional[0];
    std::string text;
    if (!read_text_file(path, text, error)) return usage_error("referee", error);

    Tally tally;
    std::vector<Line> decisions;
    std::map<std::string, Line> shots;  // by shot_id; the first line with it
    std::istringstream lines(text);
    std::string line;
    int number = 0;
    bool found_line = false;
    while (std::getline(lines, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const bool wanted = only_line == 0 || number == only_line;
        found_line = found_line || number == only_line;
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            if (wanted) unreadable(tally, number, "", "not a JSON object");
            continue;
        }
        const auto event = j.find("event");
        const auto data = j.find("data");
        if (event == j.end() || !event->is_string() || data == j.end() || !data->is_object()) {
            if (wanted) unreadable(tally, number, "", "not a log line");
            continue;
        }
        if (*event == "rules.decision" && wanted) {
            decisions.push_back({number, std::move(*data)});
        } else if (*event == "shot.simulated") {
            const auto context = data->find("context");
            const bool has_context = context != data->end() && context->is_object();
            const std::string id = has_context ? shot_id_of(*context) : std::string();
            if (!id.empty() && shots.count(id) == 0) shots[id] = {number, std::move(*data)};
        }
    }
    if (only_line > 0 && !found_line) {
        return usage_error("referee", format("--line %lld: %s has %d lines", only_line, path.c_str(), number));
    }
    if (only_line > 0 && decisions.empty() && tally.unreadable == 0) {
        return usage_error("referee", format("line %lld is not a rules.decision event", only_line));
    }
    if (decisions.empty() && tally.unreadable == 0) {
        return usage_error("referee", path + " has no rules.decision lines to re-judge");
    }

    for (const Line& decision : decisions) {
        ++tally.judged;
        const std::string difference = rules::rejudge(decision.data, table);
        if (!difference.empty()) differs(tally, decision.number, "rules.decision", difference);
        const auto shot = shots.find(shot_id_of(decision.data));
        if (shot != shots.end() && decision.data.contains("pre") && decision.data.contains("facts") &&
            decision.data.contains("ref")) {
            replay(decision, shot->second, table, table_hash, tally);
        }
    }
    std::printf("re-judged %d decisions, replayed %d of their shots: %d differences, %d unreadable\n", tally.judged,
                tally.replayed, tally.differences, tally.unreadable);
    if (tally.differences > 0) return kExitFailed;
    return tally.unreadable > 0 ? kExitUsage : kExitOk;
}

}  // namespace sim_cli
