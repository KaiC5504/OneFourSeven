// sim_cli opponent: the opponent's logged decisions made again (spec 16.10). An opponent.decision
// line carries its level, seed and the steps it took; the frame it was made from is the "pre" of
// the rules.decision line with the same shot_id.

#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <sstream>

#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/params.hpp"
#include "ofs/physics/repro.hpp"
#include "ofs/physics/serialize.hpp"
#include "ofs/rules/rules_log.hpp"
#include "ofs/rules/serialize.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

namespace rules = ofs::rules;
namespace opponent = ofs::opponent;
using nlohmann::json;

struct Line {
    int number = 0;
    std::string event;
    json data;
};

struct Tally {
    int decisions = 0;
    int remade = 0;
    int differences = 0;
    int unreadable = 0;
    int unreplayed = 0;
    int skipped = 0;  // the stand-in's, which carry no level
};

std::string text_of(const json& holder, const char* key) {
    const auto it = holder.find(key);
    return it != holder.end() && it->is_string() ? it->get<std::string>() : std::string();
}

void differs(Tally& tally, int line, const char* what, const std::string& text) {
    std::printf("line %-6d %-10s DIFFERS  %s\n", line, what, text.c_str());
    ++tally.differences;
}

void unreadable(Tally& tally, int line, const std::string& text) {
    std::printf("line %-6d ERROR    %s\n", line, text.c_str());
    ++tally.unreadable;
}

void unreplayed(Tally& tally, int line, const std::string& why) {
    std::printf("NOT REPLAYED line %d: %s\n", line, why.c_str());
    ++tally.unreplayed;
}

// What a decision is made from, besides its frame.
struct Made {
    phys::PhysicsParams params;
    rules::RefParams ref;
};

class Remaker {
public:
    Remaker(std::vector<Line> lines, phys::TableShape table, std::string table_hash, opponent::StandinParams standin)
        : lines_(std::move(lines)),
          table_(std::move(table)),
          table_hash_(std::move(table_hash)),
          standin_(standin) {
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            const Line& l = lines_[i];
            if (l.event == "rules.decision") {
                const std::string id = text_of(l.data, "shot_id");
                if (!id.empty() && decisions_.count(id) == 0) decisions_[id] = i;
            } else if (l.event == "shot.simulated") {
                const auto context = l.data.find("context");
                const std::string id = context != l.data.end() && context->is_object() ? text_of(*context, "shot_id")
                                                                                        : std::string();
                if (!id.empty() && shots_.count(id) == 0) shots_[id] = i;
            }
        }
    }

    const std::vector<Line>& lines() const { return lines_; }

    void remake(std::size_t index, Tally& tally) {
        const Line& line = lines_[index];
        const json& d = line.data;
        if (!d.contains("level")) {
            ++tally.skipped;
            return;
        }
        ++tally.decisions;
        const std::string level_id = text_of(d, "level");
        const opponent::Level* level = level_for(level_id);
        if (level == nullptr) return unreadable(tally, line.number, "level '" + level_id + "': " + level_errors_[level_id]);
        std::uint64_t seed = 0;
        if (!parse_u64(text_of(d, "seed"), seed)) return unreadable(tally, line.number, "seed: not a decimal number");
        const std::string kind = text_of(d, "kind");
        const auto player = d.find("player");
        if (player == d.end() || !player->is_number_integer() || (*player != 0 && *player != 1)) {
            return unreadable(tally, line.number, "player: not 0 or 1");
        }
        opponent::Think think;
        if (const auto t = d.find("think"); t != d.end() && t->is_object()) {
            const std::string stopped = text_of(*t, "stopped");
            const auto steps = t->find("steps");
            if (!stopped.empty() && stopped != "done") {
                if (steps == t->end() || !steps->is_number_integer() || steps->get<int>() < 1) {
                    return unreadable(tally, line.number, "think.steps: not a count of steps");
                }
                think.max_steps = steps->get<int>();
            }
        }
        if (kind == "shot") return remake_shot(line, *level, seed, think, tally);
        if (kind == "choice") return remake_choice(index, *level, seed, player->get<int>(), think, tally);
        unreadable(tally, line.number, "kind: '" + kind + "' is neither shot nor choice");
    }

private:
    const opponent::Level* level_for(const std::string& id) {
        const auto found = levels_.find(id);
        if (found != levels_.end()) return found->second ? &*found->second : nullptr;
        std::optional<opponent::Level>& slot = levels_[id];
        bool safe = !id.empty() && id.size() <= 64;
        for (char c : id) safe = safe && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-');
        if (!safe) {
            level_errors_[id] = "not a level id";
            return nullptr;
        }
        const std::string file = data_path("opponent_" + id + ".json");
        json j;
        std::string error;
        if (!read_json_file(file, j, error)) {
            level_errors_[id] = "no " + shareable_path(file);
            return nullptr;
        }
        opponent::Level level;
        std::vector<std::string> problems;
        if (!opponent::load_level(j, level, problems)) {
            std::string all;
            for (const std::string& p : problems) all += (all.empty() ? "" : "; ") + p;
            level_errors_[id] = shareable_path(file) + " is not a level: " + all;
            return nullptr;
        }
        std::printf("level %s: %s, hash %s\n", id.c_str(), shareable_path(file).c_str(),
                    ofs::params::content_hash(j).c_str());
        slot = level;
        return &*slot;
    }

    // The physics values the shot `shot_id` was simulated with; "" when found, else why not.
    std::string params_of(const std::string& shot_id, phys::PhysicsParams& out, bool& unreadable_bundle) {
        unreadable_bundle = false;
        const auto shot = shots_.find(shot_id);
        if (shot == shots_.end()) return "no shot.simulated line with shot_id " + shot_id;
        const json& data = lines_[shot->second].data;
        const auto bundle = data.find("bundle");
        phys::SimInput in;
        std::string error;
        if (bundle == data.end() || !bundle->is_object() || !phys::parse_bundle(*bundle, in, out, error)) {
            unreadable_bundle = true;
            return format("the bundle at line %d is unreadable: %s", lines_[shot->second].number,
                          error.empty() ? "no bundle" : error.c_str());
        }
        if (!in.table_hash.empty() && in.table_hash != table_hash_) {
            unreadable_bundle = true;
            return "the shot ran on table " + in.table_hash + ", this one is " + table_hash_ +
                   "; pass that table's file with --table";
        }
        return "";
    }

    std::string ref_of(const json& decision, rules::RefParams& out, bool& bad) {
        std::vector<std::string> problems;
        bad = !decision.contains("ref") || !rules::logged_ref_params(decision["ref"], out, problems);
        if (!bad) return "";
        std::string all;
        for (const std::string& p : problems) all += (all.empty() ? "" : "; ") + p;
        return "ref: " + (all.empty() ? std::string("missing") : all);
    }

    const rules::TableOracle& oracle_for(const rules::RefParams& ref) {
        const std::string key = rules::ref_params_values(ref).dump();
        std::unique_ptr<rules::TableOracle>& slot = oracles_[key];
        if (!slot) slot = rules::make_geometry_oracle(table_, ref);
        return *slot;
    }

    void compare(Tally& tally, int line, const char* what, const json& logged, const json& remade) {
        // Read back from its text, as the logged value was: doubles compare exactly.
        const json back = json::parse(remade.dump());
        if (logged == back) return;
        differs(tally, line, what, "logged " + logged.dump() + ", remade " + back.dump());
    }

    void remake_shot(const Line& line, const opponent::Level& level, std::uint64_t seed,
                     const opponent::Think& think, Tally& tally) {
        const json& d = line.data;
        const std::string shot_id = text_of(d, "shot_id");
        if (shot_id.empty()) return unreadable(tally, line.number, "a shot with no shot_id");
        const auto decision = decisions_.find(shot_id);
        if (decision == decisions_.end()) {
            return unreplayed(tally, line.number, "no rules.decision line with shot_id " + shot_id);
        }
        const json& logged_decision = lines_[decision->second].data;
        rules::FrameState frame;
        std::string error;
        if (!logged_decision.contains("pre") || !rules::frame_from_json(logged_decision["pre"], frame, error)) {
            return unreadable(tally, lines_[decision->second].number, "pre: " + (error.empty() ? "missing" : error));
        }
        Made made;
        bool bad = false;
        error = ref_of(logged_decision, made.ref, bad);
        if (bad) return unreadable(tally, lines_[decision->second].number, error);
        error = params_of(shot_id, made.params, bad);
        if (!error.empty()) return bad ? unreadable(tally, line.number, error) : unreplayed(tally, line.number, error);
        const rules::TableOracle& oracle = oracle_for(made.ref);
        // "pre" has the cue ball where it was placed; it was decided from where the engine had put
        // it in hand (take_cue_in_hand, new_frame): the place nearest the usual one.
        if (d.contains("cue") && frame.cue_in_hand && frame.cue_ball >= 0 &&
            frame.cue_ball < static_cast<int>(frame.balls.size())) {
            frame.balls[static_cast<std::size_t>(frame.cue_ball)].pos =
                oracle.nearest_cue_placement(frame.balls, frame.cue_ball, ofs::table::default_cue_ball_position());
        }
        const opponent::Env env{table_, made.params, oracle, made.ref, standin_};
        ofs::Rng rng(seed);
        const opponent::Stroke st = opponent::choose_stroke(frame, level, env, rng, think);
        ++tally.remade;
        if (st.decides) {
            return differs(tally, line.number, "kind",
                           "logged a shot, remade " + std::string(rules::decision_name(st.decision)));
        }
        const auto field = [&d](const char* key) { return d.contains(key) ? d[key] : json(); };
        compare(tally, line.number, "strike", field("strike"), phys::to_json(st.strike));
        compare(tally, line.number, "meant", field("meant"), phys::to_json(st.meant));
        compare(tally, line.number, "nominated", field("nominated"),
                st.input.has_nomination ? ofs::table::ball_name(st.input.nominated) : "");
        compare(tally, line.number, "cue", field("cue"),
                st.places_cue ? json::array({st.cue_pos.x, st.cue_pos.y}) : json());
    }

    // The frame a choice was made from is the "before" of the rules.choice that applied it: the
    // next one of the same match and player. Its physics and referee values are those of the
    // nearest stroke of the match, before it or else after it.
    void remake_choice(std::size_t index, const opponent::Level& level, std::uint64_t seed, int player,
                       const opponent::Think& think, Tally& tally) {
        const Line& line = lines_[index];
        const std::string match = text_of(line.data, "match_id");
        const Line* applied = nullptr;
        for (std::size_t i = index + 1; i < lines_.size() && applied == nullptr; ++i) {
            const Line& l = lines_[i];
            if (l.event != "rules.choice" || text_of(l.data, "match_id") != match) continue;
            const auto p = l.data.find("player");
            if (p != l.data.end() && p->is_number_integer() && *p == player) applied = &l;
        }
        if (applied == nullptr) return unreplayed(tally, line.number, "no rules.choice of player " +
                                                                          std::to_string(player) + " follows it");
        rules::FrameState frame;
        std::string error;
        if (!applied->data.contains("before") || !rules::frame_from_json(applied->data["before"], frame, error)) {
            return unreadable(tally, applied->number, "before: " + (error.empty() ? "missing" : error));
        }
        const Line* stroke = nullptr;
        for (std::size_t i = index; i-- > 0 && stroke == nullptr;) {
            const Line& l = lines_[i];
            if (l.event == "rules.decision" && text_of(l.data, "match_id") == match) stroke = &l;
        }
        for (std::size_t i = index + 1; i < lines_.size() && stroke == nullptr; ++i) {
            const Line& l = lines_[i];
            if (l.event == "rules.decision" && text_of(l.data, "match_id") == match) stroke = &l;
        }
        if (stroke == nullptr) return unreplayed(tally, line.number, "no stroke of match " + match + " to take its values from");
        Made made;
        bool bad = false;
        error = ref_of(stroke->data, made.ref, bad);
        if (bad) return unreadable(tally, stroke->number, error);
        error = params_of(text_of(stroke->data, "shot_id"), made.params, bad);
        if (!error.empty()) return bad ? unreadable(tally, line.number, error) : unreplayed(tally, line.number, error);
        const rules::TableOracle& oracle = oracle_for(made.ref);
        const opponent::Env env{table_, made.params, oracle, made.ref, standin_};
        ofs::Rng rng(seed);
        json remade;
        if (frame.pending == rules::Pending::none) {
            // A claim or concession in place of a stroke.
            const opponent::Stroke st = opponent::choose_stroke(frame, level, env, rng, think);
            remade = st.decides ? json(rules::decision_name(st.decision)) : json("(a stroke)");
        } else {
            const std::vector<rules::DecisionKind> open = rules::options(frame, player);
            if (open.empty()) return unreplayed(tally, line.number, "its frame offers the player nothing");
            remade = rules::decision_name(opponent::choose_option(frame, player, open, level, env, rng));
        }
        ++tally.remade;
        compare(tally, line.number, "choice", line.data.contains("choice") ? line.data["choice"] : json(), remade);
    }

    std::vector<Line> lines_;
    phys::TableShape table_;
    std::string table_hash_;
    opponent::StandinParams standin_;
    std::map<std::string, std::size_t> decisions_;  // rules.decision by shot_id, the first
    std::map<std::string, std::size_t> shots_;      // shot.simulated by shot_id, the first
    std::map<std::string, std::optional<opponent::Level>> levels_;
    std::map<std::string, std::string> level_errors_;
    std::map<std::string, std::unique_ptr<rules::TableOracle>> oracles_;
};

}  // namespace

int cmd_opponent(const std::vector<std::string>& argv) {
    const ArgSpec spec{{"line", "table"}, {}, 1, 1};
    Args args;
    std::string error;
    if (!parse_args(argv, spec, args, error)) return usage_error("opponent", error);
    long long only_line = 0;
    if (!option_int(args, "line", 1, 1LL << 40, only_line, error)) return usage_error("opponent", error);
    phys::TableShape table;
    std::string table_hash;
    const std::string table_file = args.value("table") ? *args.value("table") : data_path("table_shape.json");
    if (!load_table(table_file, table, table_hash)) return usage_error("opponent", table_file + " was refused");
    opponent::StandinParams standin;
    {
        json j;
        std::vector<std::string> problems;
        if (!read_json_file(data_path("standin_params.json"), j, error)) return usage_error("opponent", error);
        if (!opponent::load_standin_params(j, standin, problems)) {
            return usage_error("opponent", "core/data/standin_params.json was refused");
        }
    }

    const std::string& path = args.positional[0];
    std::string text;
    if (!read_text_file(path, text, error)) return usage_error("opponent", error);

    Tally tally;
    std::vector<Line> lines;
    std::vector<std::size_t> wanted;  // indices of the opponent.decision lines to make again
    std::istringstream input(text);
    std::string raw;
    int number = 0;
    bool found_line = false;
    while (std::getline(input, raw)) {
        ++number;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const bool asked = only_line == 0 || number == only_line;
        found_line = found_line || number == only_line;
        if (raw.find_first_not_of(" \t") == std::string::npos) continue;
        json j = json::parse(raw, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            if (asked) unreadable(tally, number, "not a JSON object");
            continue;
        }
        const auto event = j.find("event");
        const auto data = j.find("data");
        if (event == j.end() || !event->is_string() || data == j.end() || !data->is_object()) {
            if (asked) unreadable(tally, number, "not a log line");
            continue;
        }
        if (*event == "opponent.decision" && asked) wanted.push_back(lines.size());
        lines.push_back({number, event->get<std::string>(), std::move(*data)});
    }
    if (only_line > 0 && !found_line) {
        return usage_error("opponent", format("--line %lld: %s has %d lines", only_line, path.c_str(), number));
    }
    if (only_line > 0 && wanted.empty() && tally.unreadable == 0) {
        return usage_error("opponent", format("line %lld is not an opponent.decision event", only_line));
    }
    if (wanted.empty() && tally.unreadable == 0) {
        return usage_error("opponent", shareable_path(path) + " has no opponent.decision lines to make again");
    }

    Remaker remaker(std::move(lines), std::move(table), table_hash, standin);
    for (std::size_t index : wanted) remaker.remake(index, tally);
    std::printf("made again %d of %d decisions: %d differences, %d unreadable, %d not replayed; "
                "%d without a level skipped\n",
                tally.remade, tally.decisions, tally.differences, tally.unreadable, tally.unreplayed, tally.skipped);
    return tally.differences + tally.unreadable > 0 ? kExitFailed : kExitOk;
}

}  // namespace sim_cli
