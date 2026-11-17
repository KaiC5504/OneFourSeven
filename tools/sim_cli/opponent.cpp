// sim_cli opponent: the opponent's logged decisions made again (spec 16.10). An opponent.decision
// line carries its level, seed and the steps it took; the frame it was made from is the "pre" of
// the rules.decision line with the same shot_id, and the level's and the stand-in's values are the
// ones the log's rules.configured gave.

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
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

// As configure_logger names this build. A log from another platform was made with another libm,
// whose last bits differ: a solved aim, and with it a pot near a jaw, can come out otherwise.
#if defined(_WIN32)
constexpr const char* kPlatform = "windows";
#elif defined(__APPLE__)
constexpr const char* kPlatform = "macos";
#else
constexpr const char* kPlatform = "linux";
#endif
constexpr double kAcross = 1e-9;

struct Line {
    int number = 0;
    std::string event;
    std::string level;     // the log level: "info", "warn"
    std::string platform;  // "" when the line names none
    json data;
};

struct Tally {
    int decisions = 0;
    int remade = 0;
    int differences = 0;
    int unreadable = 0;
    int unreplayed = 0;
    int not_rebuilt = 0;
    int not_rebuilt_across = 0;  // of not_rebuilt, logged on another platform
    int struck_otherwise = 0;
    int across = 0;   // decisions that differ, logged on another platform
    int refused = 0;  // refused in the game: nothing to make again
    int skipped = 0;  // the stand-in's, which carry no level
};

struct Finding {
    std::string what;
    json logged;
    json remade;
};

std::string text_of(const json& holder, const char* key) {
    const auto it = holder.find(key);
    return it != holder.end() && it->is_string() ? it->get<std::string>() : std::string();
}

json at(const json& holder, const char* key) {
    return holder.is_object() && holder.contains(key) ? holder[key] : json();
}

bool from_elsewhere(const Line& line) { return !line.platform.empty() && line.platform != kPlatform; }

std::string across_tag(const Line& line) {
    return "DIFFERS ACROSS PLATFORMS (" + line.platform + " -> " + kPlatform + ")";
}

std::string params_note(const json& decision) {
    const auto it = decision.find("params_changed");
    return it != decision.end() && it->is_boolean() && it->get<bool>()
               ? " (params_changed: the physics values changed while it thought)"
               : "";
}

// Numbers within `tolerance`, everything else equal.
bool near(const json& a, const json& b, double tolerance) {
    if (a.is_number() && b.is_number()) return std::fabs(a.get<double>() - b.get<double>()) <= tolerance;
    if (a.is_object() && b.is_object()) {
        if (a.size() != b.size()) return false;
        for (auto it = a.begin(); it != a.end(); ++it) {
            const auto other = b.find(it.key());
            if (other == b.end() || !near(it.value(), *other, tolerance)) return false;
        }
        return true;
    }
    if (a.is_array() && b.is_array()) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!near(a[i], b[i], tolerance)) return false;
        }
        return true;
    }
    return a == b;
}

// What the remade stroke weighed, to set beside the logged think when they part across platforms.
json think_of(const opponent::Stroke& st) {
    return {{"candidates", st.candidates}, {"sims", st.sims}, {"steps", st.steps}};
}

// A remade stroke against its line: every field exactly for a log of this platform; across
// platforms what it chose exactly and its numbers within kAcross.
std::vector<Finding> stroke_findings(const json& d, const opponent::Stroke& st, bool across) {
    std::vector<Finding> out;
    if (st.decides) {
        out.push_back({"kind", "a shot", std::string(rules::decision_name(st.decision))});
        return out;
    }
    const auto add = [&](const std::string& what, const json& logged, const json& remade, bool numbers) {
        // Read back from its text, as the logged value was: doubles compare exactly.
        const json back = json::parse(remade.dump());
        if (across && numbers ? !near(logged, back, kAcross) : logged != back) out.push_back({what, logged, back});
    };
    if (across) {
        const json why = at(d, "why");
        for (const char* key : {"kind", "target", "pocket"}) {
            add(std::string("why.") + key, at(why, key), at(st.why, key), false);
        }
    }
    add("strike", at(d, "strike"), phys::to_json(st.strike), true);
    add("meant", at(d, "meant"), phys::to_json(st.meant), true);
    add("nominated", at(d, "nominated"),
        st.input.has_nomination ? json(ofs::table::ball_name(st.input.nominated)) : json(""), false);
    add("cue", at(d, "cue"), st.places_cue ? json::array({st.cue_pos.x, st.cue_pos.y}) : json(), true);
    return out;
}

void differs(Tally& tally, int line, const std::string& what, const std::string& text) {
    std::printf("line %-6d %-10s DIFFERS  %s\n", line, what.c_str(), text.c_str());
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

// Same platform: a line per field that differs, each a difference. Another platform: the same
// lines marked as such with what each search weighed, one count for the decision.
void report(Tally& tally, const Line& line, const std::vector<Finding>& findings, const json& remade_think) {
    if (findings.empty()) return;
    const std::string note = params_note(line.data);
    if (!from_elsewhere(line)) {
        for (const Finding& f : findings) {
            differs(tally, line.number, f.what, "logged " + f.logged.dump() + ", remade " + f.remade.dump() + note);
        }
        return;
    }
    ++tally.across;
    const std::string tag = across_tag(line);
    for (const Finding& f : findings) {
        std::printf("line %-6d %-10s %s  logged %s, remade %s%s\n", line.number, f.what.c_str(), tag.c_str(),
                    f.logged.dump().c_str(), f.remade.dump().c_str(), note.c_str());
    }
    if (remade_think.is_null()) return;
    const json logged_think = at(line.data, "think");
    json logged = json::object();
    for (auto it = remade_think.begin(); it != remade_think.end(); ++it) {
        logged[it.key()] = at(logged_think, it.key().c_str());
    }
    std::printf("line %-6d %-10s %s  logged %s, remade %s\n", line.number, "think", tag.c_str(), logged.dump().c_str(),
                remade_think.dump().c_str());
}

// What a decision is made from, besides its frame.
struct Made {
    phys::PhysicsParams params;
    rules::RefParams ref;
    json struck;  // the strike the shot.simulated bundle played
    int struck_line = 0;
};

class Remaker {
public:
    Remaker(std::vector<Line> lines, phys::TableShape table, std::string table_hash, opponent::StandinParams standin,
            json standin_values)
        : lines_(std::move(lines)),
          table_(std::move(table)),
          table_hash_(std::move(table_hash)),
          file_standin_(standin),
          file_standin_values_(std::move(standin_values)) {
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
            } else if (l.event == "rules.configured") {
                const auto ok = l.data.find("ok");
                if (ok != l.data.end() && ok->is_boolean() && ok->get<bool>()) configured_.push_back(i);
            }
        }
    }

    void remake(std::size_t index, Tally& tally) {
        const Line& line = lines_[index];
        const json& d = line.data;
        if (!d.contains("level")) {
            ++tally.skipped;
            return;
        }
        const std::string kind = text_of(d, "kind");
        if (line.level == "warn" || d.contains("error")) return refused(line, kind, tally);
        ++tally.decisions;
        if (from_elsewhere(line) && said_.insert("platform " + line.platform).second) {
            std::printf("line %d was logged on %s, this build is %s: its numbers are compared within %g\n", line.number,
                        line.platform.c_str(), kPlatform, kAcross);
        }
        const std::string level_id = text_of(d, "level");
        std::string error;
        std::vector<const opponent::Level*> levels = logged_levels(index, level_id, error);
        if (!error.empty()) return unreadable(tally, line.number, error);
        if (levels.empty()) {
            const opponent::Level* level = file_level(level_id);
            if (level == nullptr) {
                return unreadable(tally, line.number, "level '" + level_id + "': " + level_errors_[level_id]);
            }
            levels.push_back(level);
        }
        const opponent::StandinParams* standin = standin_for(index, error);
        if (standin == nullptr) return unreadable(tally, line.number, error);
        std::uint64_t seed = 0;
        if (!parse_u64(text_of(d, "seed"), seed)) return unreadable(tally, line.number, "seed: not a decimal number");
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
        if (kind == "shot") return remake_shot(line, levels, *standin, seed, think, tally);
        if (kind == "choice") return remake_choice(index, levels, *standin, seed, player->get<int>(), think, tally);
        unreadable(tally, line.number, "kind: '" + kind + "' is neither shot nor choice");
    }

private:
    void say_once(const std::string& key, const std::string& text) {
        if (said_.insert(key).second) std::printf("%s\n", text.c_str());
    }

    void refused(const Line& line, const std::string& kind, Tally& tally) {
        ++tally.refused;
        const json& d = line.data;
        const std::string shot_id = text_of(d, "shot_id");
        std::string text =
            "refused in the game: " + (d.contains("error") ? text_of(d, "error") : std::string("(no error)"));
        text += shot_id.empty() ? " (no shot_id)" : " (shot_id " + shot_id + ")";
        if (d.contains("pre")) {
            rules::FrameState frame;
            std::string error;
            text += rules::frame_from_json(d["pre"], frame, error)
                        ? "; the frame it was decided from is in its \"pre\", to rebuild from"
                        : "; its \"pre\" does not read: " + error;
        } else {
            text += "; no frame logged with it";
        }
        if (d.contains("now_hash")) text += "; the frame had moved on when it answered";
        std::printf("line %-6d %-10s %s%s\n", line.number, kind.c_str(), text.c_str(), params_note(d).c_str());
    }

    void not_rebuilt(Tally& tally, const Line& line, const std::string& text) {
        ++tally.not_rebuilt;
        const bool across = from_elsewhere(line);
        if (across) ++tally.not_rebuilt_across;
        std::printf("line %-6d %-10s NOT REBUILT  the frame could not be rebuilt: %s%s%s\n", line.number, "frame",
                    text.c_str(), across ? (" (logged on " + line.platform + ")").c_str() : "",
                    params_note(line.data).c_str());
    }

    // The game must have struck what was decided.
    void check_struck(Tally& tally, const Line& line, const Made& made) {
        if (made.struck.is_null()) return;
        const json decided = at(line.data, "strike");
        if (decided == made.struck) return;
        ++tally.struck_otherwise;
        std::printf("line %-6d %-10s DIFFERS  decided %s, struck %s (shot.simulated at line %d)%s\n", line.number,
                    "struck", decided.dump().c_str(), made.struck.dump().c_str(), made.struck_line,
                    params_note(line.data).c_str());
    }

    // Checked against the logged pre_hash when there is one; false when it is not that frame.
    bool rebuilt(Tally& tally, const Line& line, const rules::FrameState& frame, const std::string& how) {
        const auto logged = line.data.find("pre_hash");
        if (logged == line.data.end() || !logged->is_string()) return true;
        const std::string hash = rules::frame_hash(frame);
        if (hash == logged->get<std::string>()) return true;
        not_rebuilt(tally, line, "pre_hash " + logged->get<std::string>() + ", rebuilt " + hash + how);
        return false;
    }

    const opponent::Level* file_level(const std::string& id) {
        const auto found = file_levels_.find(id);
        if (found != file_levels_.end()) return found->second ? &*found->second : nullptr;
        std::optional<opponent::Level>& slot = file_levels_[id];
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
        file_hashes_[id] = ofs::params::content_hash(j);
        slot = level;
        return &*slot;
    }

    // The level `id` as the last rules.configured before `index` that lists it gave it: more than
    // one when it lists two levels of that id (a tournament of a level against a retuned copy).
    // Empty when the log lists none, or with `error` set when one does not load.
    std::vector<const opponent::Level*> logged_levels(std::size_t index, const std::string& id, std::string& error) {
        std::vector<const opponent::Level*> out;
        for (auto it = configured_.rbegin(); it != configured_.rend() && out.empty(); ++it) {
            if (*it >= index) continue;
            const Line& configured = lines_[*it];
            const auto levels = configured.data.find("levels");
            if (levels == configured.data.end() || !levels->is_array()) continue;
            for (const json& entry : *levels) {
                if (!entry.is_object() || text_of(entry, "id") != id) continue;
                const opponent::Level* level = logged_level(entry, configured.number, error);
                if (level == nullptr) return {};
                out.push_back(level);
            }
        }
        if (out.empty() && error.empty()) {
            const opponent::Level* level = file_level(id);
            if (level != nullptr) {
                say_once("file " + id, "level " + id + ": the log has no rules.configured with its values; using " +
                                           shareable_path(data_path("opponent_" + id + ".json")) + ", hash " +
                                           file_hashes_[id]);
            }
        }
        return out;
    }

    const opponent::Level* logged_level(const json& entry, int number, std::string& error) {
        const std::string key = entry.dump();
        const std::string id = text_of(entry, "id");
        const auto found = logged_levels_.find(key);
        if (found != logged_levels_.end()) return &found->second;
        json values = entry;
        values.erase("id");
        values.erase("hash");
        opponent::Level level;
        std::vector<std::string> problems;
        if (!opponent::load_level(json{{"id", id}, {"values", values}}, level, problems)) {
            std::string all;
            for (const std::string& p : problems) all += (all.empty() ? "" : "; ") + p;
            error = format("level '%s': the values rules.configured gave at line %d are not a level: %s", id.c_str(),
                           number, all.c_str());
            return nullptr;
        }
        const std::string hash = entry.contains("hash") && entry["hash"].is_string()
                                     ? ", hash " + entry["hash"].get<std::string>()
                                     : std::string();
        const opponent::Level* today = file_level(id);
        const std::string where = format(" (rules.configured at line %d%s)", number, hash.c_str());
        if (today != nullptr && opponent::level_json(*today) == opponent::level_json(level)) {
            say_once(key, "level " + id + ": the log's values, the same as " +
                              shareable_path(data_path("opponent_" + id + ".json")) + where);
        } else {
            say_once(key, "level " + id + ": the log's values differ from core/data's; using the log's" + where);
        }
        return &logged_levels_.emplace(key, level).first->second;
    }

    // The stand-in's values from the last rules.configured before `index`, else core/data's.
    const opponent::StandinParams* standin_for(std::size_t index, std::string& error) {
        for (auto it = configured_.rbegin(); it != configured_.rend(); ++it) {
            if (*it >= index) continue;
            const Line& configured = lines_[*it];
            const auto values = configured.data.find("standin");
            if (values == configured.data.end() || !values->is_object()) continue;
            const std::string key = values->dump();
            const auto found = logged_standins_.find(key);
            if (found != logged_standins_.end()) return &found->second;
            opponent::StandinParams standin;
            std::vector<std::string> problems;
            if (!opponent::load_standin_params(json{{"values", *values}}, standin, problems)) {
                std::string all;
                for (const std::string& p : problems) all += (all.empty() ? "" : "; ") + p;
                error = format("standin: the values rules.configured gave at line %d do not load: %s",
                               configured.number, all.c_str());
                return nullptr;
            }
            if (*values != file_standin_values_) {
                say_once("standin " + key, format("stand-in: the log's values differ from core/data's; using the "
                                                  "log's (rules.configured at line %d)",
                                                  configured.number));
            }
            return &logged_standins_.emplace(key, standin).first->second;
        }
        say_once("standin file", "stand-in: the log has no rules.configured with its values; using "
                                 "core/data/standin_params.json");
        return &file_standin_;
    }

    // The physics values the shot `shot_id` was simulated with, and the strike it played; "" when
    // found, else why not.
    std::string params_of(const std::string& shot_id, Made& made, bool& unreadable_bundle) {
        unreadable_bundle = false;
        const auto shot = shots_.find(shot_id);
        if (shot == shots_.end()) return "no shot.simulated line with shot_id " + shot_id;
        const json& data = lines_[shot->second].data;
        const auto bundle = data.find("bundle");
        phys::SimInput in;
        std::string error;
        if (bundle == data.end() || !bundle->is_object() || !phys::parse_bundle(*bundle, in, made.params, error)) {
            unreadable_bundle = true;
            return format("the bundle at line %d is unreadable: %s", lines_[shot->second].number,
                          error.empty() ? "no bundle" : error.c_str());
        }
        if (!in.table_hash.empty() && in.table_hash != table_hash_) {
            unreadable_bundle = true;
            return "the shot ran on table " + in.table_hash + ", this one is " + table_hash_ +
                   "; pass that table's file with --table";
        }
        made.struck = json::parse(phys::to_json(in.strike).dump());
        made.struck_line = lines_[shot->second].number;
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

    void remake_shot(const Line& line, const std::vector<const opponent::Level*>& levels,
                     const opponent::StandinParams& standin, std::uint64_t seed, const opponent::Think& think,
                     Tally& tally) {
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
        error = params_of(shot_id, made, bad);
        if (!error.empty()) return bad ? unreadable(tally, line.number, error) : unreplayed(tally, line.number, error);
        check_struck(tally, line, made);
        const rules::TableOracle& oracle = oracle_for(made.ref);
        // "pre" has the cue ball where it was placed; it was decided from where the engine had put
        // it in hand (take_cue_in_hand, new_frame): the place nearest the usual one.
        std::string how;
        if (d.contains("cue") && frame.cue_in_hand && frame.cue_ball >= 0 &&
            frame.cue_ball < static_cast<int>(frame.balls.size())) {
            rules::Vec2& cue = frame.balls[static_cast<std::size_t>(frame.cue_ball)].pos;
            cue = oracle.nearest_cue_placement(frame.balls, frame.cue_ball, ofs::table::default_cue_ball_position());
            how = format(" (the cue ball in hand put back at (%.17g, %.17g))", cue.x, cue.y);
        }
        if (!rebuilt(tally, line, frame, how)) return;
        const opponent::Env env{table_, made.params, oracle, made.ref, standin};
        const bool across = from_elsewhere(line);
        std::vector<Finding> findings;
        opponent::Stroke kept;
        for (std::size_t k = 0; k < levels.size(); ++k) {
            ofs::Rng rng(seed);
            opponent::Stroke st = opponent::choose_stroke(frame, *levels[k], env, rng, think);
            std::vector<Finding> found = stroke_findings(d, st, across);
            if (k == 0 || found.empty()) {
                findings = std::move(found);
                kept = std::move(st);
            }
            if (findings.empty()) break;
        }
        ++tally.remade;
        report(tally, line, findings, think_of(kept));
    }

    // The frame a choice was made from is the "before" of the rules.choice that applied it: the
    // next one of the same match and player. Its physics and referee values are those of the
    // nearest stroke of the match, before it or else after it.
    void remake_choice(std::size_t index, const std::vector<const opponent::Level*>& levels,
                       const opponent::StandinParams& standin, std::uint64_t seed, int player,
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
        if (!rebuilt(tally, line, frame, format(" (the before of rules.choice at line %d)", applied->number))) return;
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
        error = params_of(text_of(stroke->data, "shot_id"), made, bad);
        if (!error.empty()) return bad ? unreadable(tally, line.number, error) : unreplayed(tally, line.number, error);
        const rules::TableOracle& oracle = oracle_for(made.ref);
        const opponent::Env env{table_, made.params, oracle, made.ref, standin};
        const json logged = at(line.data, "choice");
        std::vector<Finding> findings;
        json remade_think;
        for (std::size_t k = 0; k < levels.size(); ++k) {
            ofs::Rng rng(seed);
            json remade;
            json st_think;
            if (frame.pending == rules::Pending::none) {
                // A claim or concession in place of a stroke.
                const opponent::Stroke st = opponent::choose_stroke(frame, *levels[k], env, rng, think);
                remade = st.decides ? json(rules::decision_name(st.decision)) : json("(a stroke)");
                st_think = think_of(st);
            } else {
                const std::vector<rules::DecisionKind> open = rules::options(frame, player);
                if (open.empty()) return unreplayed(tally, line.number, "its frame offers the player nothing");
                remade = rules::decision_name(opponent::choose_option(frame, player, open, *levels[k], env, rng));
            }
            std::vector<Finding> found;
            if (logged != remade) found.push_back({"choice", logged, remade});
            if (k == 0 || found.empty()) {
                findings = std::move(found);
                remade_think = std::move(st_think);
            }
            if (findings.empty()) break;
        }
        ++tally.remade;
        report(tally, line, findings, remade_think);
    }

    std::vector<Line> lines_;
    phys::TableShape table_;
    std::string table_hash_;
    opponent::StandinParams file_standin_;
    json file_standin_values_;
    std::map<std::string, std::size_t> decisions_;  // rules.decision by shot_id, the first
    std::map<std::string, std::size_t> shots_;      // shot.simulated by shot_id, the first
    std::vector<std::size_t> configured_;           // rules.configured lines that were applied
    std::map<std::string, std::optional<opponent::Level>> file_levels_;
    std::map<std::string, std::string> file_hashes_;
    std::map<std::string, std::string> level_errors_;
    std::map<std::string, opponent::Level> logged_levels_;  // by the logged entry's text
    std::map<std::string, opponent::StandinParams> logged_standins_;
    std::map<std::string, std::unique_ptr<rules::TableOracle>> oracles_;
    std::set<std::string> said_;
};

}  // namespace

int cmd_opponent(const std::vector<std::string>& argv) {
    const ArgSpec spec{{"line", "table"}, {"strict"}, 1, 1};
    Args args;
    std::string error;
    if (!parse_args(argv, spec, args, error)) return usage_error("opponent", error);
    long long only_line = 0;
    if (!option_int(args, "line", 1, 1LL << 40, only_line, error)) return usage_error("opponent", error);
    const bool strict = args.flag("strict");
    phys::TableShape table;
    std::string table_hash;
    const std::string table_file = args.value("table") ? *args.value("table") : data_path("table_shape.json");
    if (!load_table(table_file, table, table_hash)) return usage_error("opponent", table_file + " was refused");
    opponent::StandinParams standin;
    json standin_values;
    {
        json j;
        std::vector<std::string> problems;
        if (!read_json_file(data_path("standin_params.json"), j, error)) return usage_error("opponent", error);
        if (!opponent::load_standin_params(j, standin, problems)) {
            return usage_error("opponent", "core/data/standin_params.json was refused");
        }
        standin_values = j["values"];
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
        lines.push_back({number, event->get<std::string>(), text_of(j, "level"), text_of(j, "platform"),
                         std::move(*data)});
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

    Remaker remaker(std::move(lines), std::move(table), table_hash, standin, std::move(standin_values));
    for (std::size_t index : wanted) remaker.remake(index, tally);
    std::printf("made again %d of %d decisions: %d differences, %d unreadable, %d not replayed, %d frames not "
                "rebuilt, %d struck otherwise; %d differ across platforms; %d refused in the game; "
                "%d without a level skipped\n",
                tally.remade, tally.decisions, tally.differences, tally.unreadable, tally.unreplayed, tally.not_rebuilt,
                tally.struck_otherwise, tally.across, tally.refused, tally.skipped);
    // Across platforms libm alone can part a decision, so only --strict holds those to the exit code.
    const int failed = tally.differences + tally.unreadable + tally.struck_otherwise +
                       (tally.not_rebuilt - tally.not_rebuilt_across) +
                       (strict ? tally.across + tally.not_rebuilt_across : 0);
    return failed > 0 ? kExitFailed : kExitOk;
}

}  // namespace sim_cli
