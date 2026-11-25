// sim_cli tournament: two levels of the computer opponent play frames against each other under the
// full rules, and what each did is counted against spec 10.1 (spec 16.4). docs/opponent/README.md
// says what is counted and how.
//
// Frame i of seed S is always the same frame and frames are counted in frame order, so the report
// and the log are the same whatever the thread count, the thinking times aside.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "ofs/log.hpp"
#include "ofs/opponent/exhibition.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/params.hpp"
#include "ofs/physics/cue.hpp"
#include "ofs/physics/repro.hpp"
#include "ofs/physics/serialize.hpp"
#include "ofs/rules/engine.hpp"
#include "ofs/rules/match.hpp"
#include "ofs/rules/rules_log.hpp"
#include "ofs/rules/serialize.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

namespace fs = std::filesystem;
namespace rules = ofs::rules;
namespace opponent = ofs::opponent;
using nlohmann::json;
using ofs::table::Ball;
using rules::DecisionKind;
using rules::FrameState;
using Clock = std::chrono::steady_clock;

constexpr double kD = ofs::table::kBallDiameter;
// Each its own stream, so the decisions, the shots and the exhibitions never share numbers.
constexpr std::uint64_t kDecisionStream = 0x746f75725f646563ULL;
constexpr std::uint64_t kShotStream = 0x746f75725f73686fULL;
constexpr std::uint64_t kExhibitionStream = 0x746f75725f657868ULL;
constexpr int kMaxRepros = 20;
constexpr int kMaxPrinted = 100;
constexpr const char* kBands[] = {"short", "medium", "long", "unknown"};
constexpr int kBandCount = 4;
constexpr const char* kStopped[] = {"done", "sims", "steps", "time"};
// An escape's `why.cushions`: 0 to 4, "5+", or "unknown" when its why has none (the stand-in's).
constexpr const char* kCushionNames[] = {"0", "1", "2", "3", "4", "5+", "unknown"};
constexpr int kCushionBuckets = 7;
// The game's position reader (docs/opponent/README.md, "The game, stage 2"), never an opponent.
constexpr const char* kReaderId = "person";

std::uint64_t mix(std::uint64_t a, std::uint64_t b) {
    std::uint64_t z = a + 0x9E3779B97F4A7C15ULL * (b + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// What the current thread's frame logged: its lines when a log is kept, and what the checks read
// back. The logger calls a sink on the thread that logged, so each frame gets its own lines.
struct Capture {
    bool keep = false;
    std::vector<std::string> lines;
    std::string decision_line;         // the last rules.decision
    std::vector<std::string> invalid;  // rules.invalid since the last step began
    std::vector<std::string> errors;   // app.error since the last step began
};

thread_local Capture* t_capture = nullptr;

class CaptureSink : public ofs::log::Sink {
public:
    void write(const ofs::log::Event& event, const std::string& line) override {
        Capture* capture = t_capture;
        if (capture == nullptr) return;
        if (capture->keep) capture->lines.push_back(line);
        if (event.event == "rules.decision") {
            capture->decision_line = line;
        } else if (event.event == "rules.invalid") {
            capture->invalid.push_back(event.data.value("what", std::string("?")) + ": " +
                                       event.data.value("detail", std::string()));
        } else if (event.event == "app.error") {
            capture->errors.push_back(event.data.value("function", std::string("?")) + ": " +
                                      event.data.value("message", std::string()));
        }
    }
};

struct Side {
    opponent::Level level;
    json file;
    std::string source;  // shareable_path of its file
    std::string hash;    // content hash of its file
    bool exact = false;  // every sigma of its execution 0: a planned pot must go in
};

// One level's counts over the frames merged so far.
struct Tally {
    int frames_won = 0;
    long long strokes = 0;
    long long attempts = 0;
    long long made = 0;
    std::array<long long, kBandCount> band_attempts{};
    std::array<long long, kBandCount> band_made{};
    std::vector<int> breaks;
    long long safeties = 0;
    long long escapes = 0;
    long long safeties_with_pot = 0;
    long long no_pot = 0;
    long long fouls = 0;
    long long points = 0;
    long long visits_on = 0;
    long long completed = 0;
    long long thinks = 0;
    long long sims = 0;
    int sims_max = 0;
    long long steps = 0;
    std::vector<double> us;
    std::array<long long, 4> stopped{};
    long long stopped_other = 0;
    long long fallbacks = 0;
    // The ladder's counts (docs/opponent/README.md, "The ladder"), in frames only; what followed a
    // stroke is read at the next stroke of the frame, when it is the other player's.
    long long safety_played = 0;
    long long safety_then_attempt = 0;
    long long safety_then_potted = 0;
    long long snooker_kind = 0;
    long long snookers_laid = 0;
    long long snookers_called = 0;
    long long snooker_then_fouled = 0;
    long long snooker_then_points = 0;
    std::array<long long, kCushionBuckets> escape_played{};
    std::array<long long, kCushionBuckets> escape_hit{};
    long long break_offs = 0;
    long long break_off_then_pot = 0;
    std::vector<int> frame_highs;  // the side's highest break in each frame, 0 for none
    long long concessions = 0;
    long long claims = 0;

    void add(const Tally& o) {
        frames_won += o.frames_won;
        strokes += o.strokes;
        attempts += o.attempts;
        made += o.made;
        for (int i = 0; i < kBandCount; ++i) {
            band_attempts[std::size_t(i)] += o.band_attempts[std::size_t(i)];
            band_made[std::size_t(i)] += o.band_made[std::size_t(i)];
        }
        breaks.insert(breaks.end(), o.breaks.begin(), o.breaks.end());
        safeties += o.safeties;
        escapes += o.escapes;
        safeties_with_pot += o.safeties_with_pot;
        no_pot += o.no_pot;
        fouls += o.fouls;
        points += o.points;
        visits_on += o.visits_on;
        completed += o.completed;
        thinks += o.thinks;
        sims += o.sims;
        sims_max = std::max(sims_max, o.sims_max);
        steps += o.steps;
        us.insert(us.end(), o.us.begin(), o.us.end());
        for (std::size_t i = 0; i < stopped.size(); ++i) stopped[i] += o.stopped[i];
        stopped_other += o.stopped_other;
        fallbacks += o.fallbacks;
        safety_played += o.safety_played;
        safety_then_attempt += o.safety_then_attempt;
        safety_then_potted += o.safety_then_potted;
        snooker_kind += o.snooker_kind;
        snookers_laid += o.snookers_laid;
        snookers_called += o.snookers_called;
        snooker_then_fouled += o.snooker_then_fouled;
        snooker_then_points += o.snooker_then_points;
        for (std::size_t i = 0; i < escape_played.size(); ++i) {
            escape_played[i] += o.escape_played[i];
            escape_hit[i] += o.escape_hit[i];
        }
        break_offs += o.break_offs;
        break_off_then_pot += o.break_off_then_pot;
        frame_highs.insert(frame_highs.end(), o.frame_highs.begin(), o.frame_highs.end());
        concessions += o.concessions;
        claims += o.claims;
    }
};

struct Failure {
    std::string kind;
    std::string detail;
    std::string where;  // "frame 12" or "exhibition Open table 1"
    std::string name;   // the repro file's name
    int stroke = 0;
    int player = 0;
    std::string level;
    json repro;
    std::string written;  // the path written, or why not; empty when not one of the first
};

struct JobResult {
    std::array<Tally, 2> sides;
    std::vector<Failure> failures;
    std::vector<std::string> lines;
    bool exhibition = false;
    json entry;  // an exhibition's report entry
    int frame_best = -1;  // the frame's highest break by either player; -1 for an exhibition
};

struct Run {
    std::uint64_t seed = 1;
    int frames = 100;
    int max_strokes = 600;
    double max_ms = 0.0;
    std::string out_dir;
    bool keep_log = false;
    Setup setup;
    rules::RefParams ref;
    opponent::StandinParams standin;
    std::unique_ptr<rules::TableOracle> oracle;
    std::unique_ptr<PlayArea> area;
    std::array<Side, 2> sides;
    bool two_sides = true;
    std::vector<opponent::Exhibition> exhibitions;

    std::atomic<int> next{0};
    int jobs = 0;

    // Finished jobs wait here until every job before them is in, then join the totals.
    std::mutex mutex;
    std::map<int, JobResult> waiting;
    int merged = 0;
    std::array<Tally, 2> totals;
    std::vector<int> frame_best;
    std::vector<json> exhibition_entries;
    std::vector<Failure> failures;
    int repros = 0;
    std::ofstream log;
    std::string log_error;

    void write_lines(const std::vector<std::string>& lines) {
        if (!keep_log || !log_error.empty()) return;
        for (const std::string& line : lines) log << line << '\n';
        if (!log) log_error = "write failed";
    }

    void finish(int index, JobResult result) {
        std::lock_guard<std::mutex> lock(mutex);
        waiting.emplace(index, std::move(result));
        for (auto it = waiting.find(merged); it != waiting.end(); it = waiting.find(merged)) {
            JobResult& r = it->second;
            totals[0].add(r.sides[0]);
            totals[1].add(r.sides[1]);
            if (r.frame_best >= 0) frame_best.push_back(r.frame_best);
            if (r.exhibition) exhibition_entries.push_back(r.entry);
            for (Failure& f : r.failures) {
                if (repros < kMaxRepros) {
                    ++repros;
                    const std::string path = (fs::u8path(out_dir) / f.name).u8string();
                    std::string error;
                    f.written = write_text_file(path, f.repro.dump(1) + "\n", error) ? shareable_path(path)
                                                                                   : "(not written: " + error + ")";
                }
                f.repro = json();
                failures.push_back(std::move(f));
            }
            write_lines(r.lines);
            waiting.erase(it);
            ++merged;
        }
    }
};

std::string ball_text(const rules::BallOnTable& b) {
    std::string label(ofs::table::ball_name(b.ball));
    if (b.ball == Ball::red) label += " " + std::to_string(b.index);
    return label;
}

bool listed(const std::vector<DecisionKind>& options, DecisionKind kind) {
    return std::find(options.begin(), options.end(), kind) != options.end();
}

int count_of(const rules::Position& p, Ball ball) {
    int n = 0;
    for (const rules::BallOnTable& b : p.balls) n += b.ball == ball ? 1 : 0;
    return n;
}

// The checks below are match.cpp's, which the stand-in's matches are held to.

// What a frame must keep true after every stroke and choice; "" when it does.
std::string frame_problem(const Run& run, const FrameState& s) {
    const int n = static_cast<int>(s.balls.size());
    if (s.cue_ball < 0 || s.cue_ball >= n || s.balls[static_cast<std::size_t>(s.cue_ball)].ball != Ball::cue) {
        return format("the cue ball is not at place %d of %d", s.cue_ball, n);
    }
    if (count_of(s, Ball::cue) != 1) return format("%d cue balls are listed", count_of(s, Ball::cue));
    std::array<int, 15> red_seen{};
    for (const rules::BallOnTable& b : s.balls) {
        if (!std::isfinite(b.pos.x) || !std::isfinite(b.pos.y)) return ball_text(b) + " is nowhere";
        if (b.ball != Ball::red) continue;
        if (b.index < 0 || b.index > 14) return format("a red has index %d", b.index);
        if (++red_seen[static_cast<std::size_t>(b.index)] > 1) return ball_text(b) + " is listed twice";
    }
    const Ball colours[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};
    for (Ball colour : colours) {
        if (count_of(s, colour) > 1) return std::string(ofs::table::ball_name(colour)) + " is listed twice";
    }
    for (int i = 0; i < n; ++i) {
        const rules::BallOnTable& a = s.balls[static_cast<std::size_t>(i)];
        for (int k = i + 1; k < n; ++k) {
            const rules::BallOnTable& b = s.balls[static_cast<std::size_t>(k)];
            const double d = std::hypot(a.pos.x - b.pos.x, a.pos.y - b.pos.y);
            if (d < kD - phys::kOverlapTolerance_m) {
                return format("%s and %s overlap by %.3g m", ball_text(a).c_str(), ball_text(b).c_str(), kD - d);
            }
        }
        const std::string where = run.area->violation(Vec3{a.pos.x, a.pos.y, 0.0}, 1e-9);
        if (!where.empty()) return ball_text(a) + " is off the cloth: " + where;
    }
    for (int place : s.touching) {
        if (place < 0 || place >= n || place == s.cue_ball) return format("touching lists place %d", place);
    }
    if (s.scores[0] < 0 || s.scores[1] < 0) return format("a score is negative: %d-%d", s.scores[0], s.scores[1]);
    if (s.restart) return "the frame is still marked for a re-rack";

    const bool none_open = rules::options(s, 0).empty() && rules::options(s, 1).empty();
    if (s.over) {
        if (s.winner != 0 && s.winner != 1) return format("the frame is over with winner %d", s.winner);
        if (s.end_reason == rules::EndReason::none) return "the frame is over for no reason";
        if (s.pending != rules::Pending::none) return "the frame is over with a decision pending";
        if (!none_open) return "the frame is over and a player is still offered a choice";
        return "";
    }
    if (s.winner != -1 || s.end_reason != rules::EndReason::none) return "the frame has a winner but is not over";
    if (s.striker != 0 && s.striker != 1) return format("the striker is player %d", s.striker);

    const int reds = count_of(s, Ball::red);
    const std::string on = std::string(rules::on_kind_name(s.on.kind)) +
                           (s.on.kind == rules::OnKind::colour ? " " + std::string(ofs::table::ball_name(s.on.colour))
                                                               : std::string());
    const bool sequence = s.on.kind == rules::OnKind::colour;
    if (reds > 0 && sequence) return format("%d reds remain and the ball on is %s", reds, on.c_str());
    if (reds == 0 && s.on.kind == rules::OnKind::red) return "red is on with no red left";
    if (sequence && (s.on.colour == Ball::cue || s.on.colour == Ball::red)) return "the ball on is " + on;
    for (Ball colour : colours) {
        const bool wanted = !sequence || ofs::table::ball_value(colour) >= ofs::table::ball_value(s.on.colour);
        if (count_of(s, colour) != (wanted ? 1 : 0)) {
            return format("with %s on and %d reds left, %s is %s the table", on.c_str(), reds,
                          std::string(ofs::table::ball_name(colour)).c_str(), wanted ? "off" : "on");
        }
    }
    if (s.respotted_black && (n != 2 || !sequence || s.on.colour != Ball::black)) {
        return format("a re-spotted black with %d balls and %s on", n, on.c_str());
    }
    if (s.free_ball && rules::nominations(s).empty()) return "FREE BALL with no ball to nominate";
    if (s.cue_in_hand &&
        !run.oracle->cue_placement_legal(s.balls, s.cue_ball, s.balls[static_cast<std::size_t>(s.cue_ball)].pos)) {
        return "the cue ball in hand lies where it may not be placed";
    }

    if (s.pending == rules::Pending::none) return "";
    if (s.decider != 0 && s.decider != 1) return format("the decider is player %d", s.decider);
    const std::string pending = "pending " + std::string(rules::pending_name(s.pending));
    if (rules::options(s, s.decider).empty()) return pending + " offers its decider nothing";
    if (!rules::options(s, 1 - s.decider).empty()) return pending + " offers the other player a choice";
    switch (s.pending) {
        case rules::Pending::after_foul:
            if (s.striker != s.decider || s.offender != 1 - s.decider) {
                return format("after a foul the striker is %d, the decider %d and the offender %d", s.striker,
                              s.decider, s.offender);
            }
            if (s.last_penalty < 4 || s.last_penalty > 7) return format("the last penalty is %d", s.last_penalty);
            break;
        case rules::Pending::rerack:
            if (s.decider != (s.rerack_accepted < 0 ? s.striker : 1 - s.rerack_accepted)) {
                return format("a re-rack offer to player %d with the striker %d and %d having accepted", s.decider,
                              s.striker, s.rerack_accepted);
            }
            break;
        case rules::Pending::toss:
            if (!s.respotted_black || !s.cue_in_hand || s.scores[0] != s.scores[1]) {
                return "a toss without a re-spotted black, the cue ball in hand and level scores";
            }
            break;
        case rules::Pending::none: break;
    }
    return "";
}

// What one judged stroke must keep true of the frame before it; "" when it does.
std::string stroke_problem(const FrameState& pre, const rules::Judgement& j) {
    const FrameState& post = j.post;
    const int striker = pre.striker;
    const int other = 1 - striker;
    if (post.strokes != pre.strokes + 1) return format("strokes went from %d to %d", pre.strokes, post.strokes);
    if (j.points < 0 || j.penalty < 0 || (j.points > 0 && j.penalty > 0)) {
        return format("the stroke is worth %d points and a penalty of %d", j.points, j.penalty);
    }
    if (j.fouls.empty() != (j.penalty == 0)) return format("%zu fouls and a penalty of %d", j.fouls.size(), j.penalty);
    for (std::size_t i = 0; i < j.fouls.size(); ++i) {
        const rules::Foul& f = j.fouls[i];
        if (f.value < 4 || f.value > 7) return format("a foul worth %d", f.value);
        if (i > 0 && j.fouls[i - 1].value < f.value) return "the fouls are not listed highest first";
    }
    if (!j.fouls.empty() && j.penalty != j.fouls.front().value) return "the penalty is not the highest foul's value";
    if (j.miss && j.fouls.empty()) return "a miss without a foul";
    for (int player : {striker, other}) {
        const int before = pre.scores[static_cast<std::size_t>(player)];
        const int after = post.scores[static_cast<std::size_t>(player)];
        const int due = player == striker ? j.points : j.penalty;
        if (after != before + due) {
            return format("player %d's score went from %d to %d on a stroke that gave them %d", player, before, after,
                          due);
        }
    }
    bool foul_called = false;
    bool over_called = false;
    bool respotted = false;
    for (const rules::Call& c : j.calls) {
        foul_called = foul_called || c.kind == rules::CallKind::foul;
        over_called = over_called || c.kind == rules::CallKind::frame_over;
        respotted = respotted || c.kind == rules::CallKind::respotted_black;
    }
    // A black potted to level the scores ends the break with the frame it would have won (S3.4(b)).
    if (post.break_points != (j.points > 0 && !respotted ? pre.break_points + j.points : 0)) {
        return format("the break went from %d to %d on %d points", pre.break_points, post.break_points, j.points);
    }
    if (count_of(post, Ball::red) > count_of(pre, Ball::red)) return "a red came back onto the table";
    if (post.balls.size() > pre.balls.size() + 1) return "balls came back onto the table";
    if (foul_called != !j.fouls.empty()) return "FOUL was called without a foul, or not called with one";
    if (over_called != post.over) return "the frame's end and its call disagree";
    if (post.pending == rules::Pending::after_foul &&
        (post.last_penalty != j.penalty || post.last_miss != j.miss || post.offender != striker)) {
        return "the frame after a foul does not carry that foul";
    }
    if (post.free_ball && post.pending != rules::Pending::after_foul) return "FREE BALL without a foul";
    return "";
}

std::string round_trip_problem(const FrameState& s) {
    FrameState back;
    std::string error;
    if (!rules::frame_from_json(rules::frame_json(s), back, error)) return "its JSON does not read back: " + error;
    if (rules::frame_hash(back) != rules::frame_hash(s)) return "its JSON reads back as a different frame";
    return "";
}

// The place in `s` of the ball a why's "target" names ("red 3", "black"); -1 when none is there.
int target_place(const FrameState& s, const std::string& target) {
    for (std::size_t i = 0; i < s.balls.size(); ++i) {
        if (ball_text(s.balls[i]) == target) return static_cast<int>(i);
    }
    return -1;
}

int band_index(const json& why) {
    const auto it = why.find("band");
    if (it != why.end() && it->is_string()) {
        for (int i = 0; i < kBandCount - 1; ++i) {
            if (*it == kBands[i]) return i;
        }
    }
    return kBandCount - 1;
}

int cushion_bucket(const json& why) {
    const auto it = why.find("cushions");
    if (it == why.end() || !it->is_number_integer()) return kCushionBuckets - 1;
    return std::clamp(it->get<int>(), 0, kCushionBuckets - 2);
}

// `why.pots` above 0: its search found a stroke that pots. The stand-in's own lines have none.
bool had_pot(const json& why) {
    const auto pots = why.find("pots");
    return pots != why.end() && pots->is_number() && pots->get<double>() > 0.0;
}

// The stroke met a ball on first: none of the fouls for missing it, whatever else it did.
bool hit_ball_on(const rules::Judgement& j) {
    for (const rules::Foul& f : j.fouls) {
        if (f.kind == rules::FoulKind::no_hit || f.kind == rules::FoulKind::first_hit_not_on ||
            f.kind == rules::FoulKind::simultaneous_hit) {
            return false;
        }
    }
    return true;
}

json think_json(const opponent::Stroke& st, double us) {
    json think = {{"candidates", st.candidates}, {"sims", st.sims},     {"steps", st.steps},
                  {"depth", st.depth},           {"stopped", st.stopped}, {"ms", us / 1000.0}};
    if (st.errors > 0) think["errors"] = st.errors;
    return think;
}

// One stroke or choice, kept so a failure can be written out whole.
struct Step {
    FrameState before;
    bool is_stroke = false;
    std::uint64_t seed = 0;  // the opponent's Rng for its stroke or choice
    opponent::Stroke stroke;
    phys::SimInput in;
    bool simulated = false;
    rules::ShotFacts facts;
    rules::Judgement judgement;
    bool judged = false;
    rules::Decision decision;
    std::vector<rules::Call> calls;
};

class FramePlayer {
public:
    FramePlayer(Run& run, int job) : run_(run), job_(job) {
        capture_.keep = run.keep_log;
        exhibition_ = job >= run.frames;
        if (exhibition_) {
            const int e = job - run.frames;
            frame_seed_ = mix(mix(run.seed, kExhibitionStream), static_cast<std::uint64_t>(e));
            match_id_ = format("tournament-%llu-x%d", static_cast<unsigned long long>(run.seed), e);
            where_ = "exhibition " + run.exhibitions[static_cast<std::size_t>(e)].name;
        } else {
            frame_seed_ = mix(run.seed, static_cast<std::uint64_t>(job));
            match_id_ = format("tournament-%llu-%d", static_cast<unsigned long long>(run.seed), job);
            where_ = format("frame %d", job);
        }
    }

    JobResult play() {
        t_capture = &capture_;
        out_.exhibition = exhibition_;
        try {
            if (exhibition_) {
                play_exhibition();
            } else {
                play_frame();
                record_frame_highs();
            }
        } catch (const std::exception& e) {
            // The engine and the opponent promise never to throw.
            Step step;
            step.before = m_.frame;
            step.seed = current_seed_;
            step.decision.player = current_player_;
            ofs::log::error("app", "app.error",
                            {{"kind", "exception"},
                             {"message", e.what()},
                             {"function", "tournament"},
                             {"match_id", match_id_},
                             {"frame", m_.frame_number},
                             {"level", side(current_player_).level.id},
                             {"seed", std::to_string(current_seed_)}});
            fail("exception", e.what(), step);
            if (exhibition_ && !exhibition_ended_) end_exhibition(false);
            if (!exhibition_ && !highs_recorded_) record_frame_highs();
        }
        t_capture = nullptr;
        out_.lines = std::move(capture_.lines);
        return std::move(out_);
    }

private:
    const Side& side(int player) const { return run_.sides[static_cast<std::size_t>(exhibition_ ? 0 : player)]; }
    Tally& stats(int player) { return out_.sides[static_cast<std::size_t>(exhibition_ ? 0 : player)]; }

    opponent::Env env() const {
        return {run_.setup.table, run_.setup.params, *run_.oracle, run_.ref, run_.standin};
    }

    json context(bool with_shot) const {
        json c = {{"match_id", match_id_}, {"frame", m_.frame_number}};
        if (with_shot) c["shot_id"] = format("%s-f%d-s%d", match_id_.c_str(), m_.frame_number, frame_strokes_);
        return c;
    }

    void play_frame() {
        rules::MatchConfig config;
        config.best_of = 1;
        config.first_breaker = job_ % 2;
        config.seed = frame_seed_;
        m_ = rules::start_match(config, run_.setup.params);
        // The level at player 1's place, where the game's opponent sits; each decision names its own.
        rules::log_match_started(m_, {{"match_id", match_id_}, {"level", run_.sides[1].level.id}});
        rules::log_frame_started(m_, rack_context());
        bool ok = check_frame(Step{});
        while (ok && !m_.over) {
            capture_.invalid.clear();
            capture_.errors.clear();
            const rules::ScopedLogContext scope(context(false));
            ok = m_.frame.pending == rules::Pending::none ? stroke() : pending_choice();
        }
        if (m_.over && m_.winner >= 0) ++stats(m_.winner).frames_won;
    }

    void play_exhibition() {
        const opponent::Exhibition& layout = run_.exhibitions[static_cast<std::size_t>(job_ - run_.frames)];
        rules::MatchConfig config;
        config.best_of = 1;
        config.first_breaker = 0;
        config.seed = frame_seed_;
        m_ = rules::MatchState{};
        m_.config = config;
        m_.frame_number = 1;
        m_.frame = opponent::exhibition_frame(layout, 0, frame_seed_);
        ofs::log::info("match", "exhibition.started",
                       {{"layout", layout.name}, {"level", run_.sides[0].level.id}, {"match_id", match_id_}});
        bool ok = check_frame(Step{});
        while (ok && !break_over_) {
            capture_.invalid.clear();
            capture_.errors.clear();
            const rules::ScopedLogContext scope(context(false));
            ok = stroke();
        }
        end_exhibition(ok);
    }

    // A failure that stopped the break ends it as "failed", whatever ended the break before it.
    void end_exhibition(bool ok) {
        exhibition_ended_ = true;
        if (!ok) {
            if (!break_over_) exhibition_break_ = m_.frame.break_points;
            exhibition_why_ = "failed";
        }
        out_.entry = {{"name", run_.exhibitions[static_cast<std::size_t>(job_ - run_.frames)].name},
                      {"break_points", exhibition_break_},
                      {"maximum", exhibition_break_ >= 147},
                      {"strokes", frame_strokes_}};
        ofs::log::info("match", "exhibition.ended",
                       {{"break_points", exhibition_break_},
                        {"maximum", exhibition_break_ >= 147},
                        {"why", exhibition_why_},
                        {"strokes", frame_strokes_},
                        {"match_id", match_id_}});
    }

    json rack_context() const {
        json c = context(false);
        c["params_hash"] = run_.setup.params_hash;
        return c;
    }

    // The line logged, to log again with its error if the engine will not take it; null when no log is kept.
    json log_choice_made(int player, const std::vector<DecisionKind>& open, DecisionKind kind, std::uint64_t seed,
                         const json& why, double us, const opponent::Stroke* from_stroke) {
        if (!capture_.keep) return json();
        json names = json::array();
        for (DecisionKind k : open) names.push_back(json(rules::decision_name(k)));
        json line = {{"kind", "choice"},
                     {"player", player},
                     {"seed", std::to_string(seed)},
                     {"level", side(player).level.id},
                     {"choice", rules::decision_name(kind)},
                     {"options", names},
                     {"why", why.is_object() ? why : json::object()},
                     {"us", us},
                     {"pre_hash", rules::frame_hash(m_.frame)}};
        if (from_stroke != nullptr) line["think"] = think_json(*from_stroke, us);
        line.update(context(false));
        ofs::log::info("opponent", "opponent.decision", line);
        return line;
    }

    // As the game logs a decision it would not play: again, at warn, with why and the frame.
    void log_refused(json line, const std::string& error, const FrameState& before) {
        if (line.is_null()) return;
        line["error"] = error;
        line["pre"] = rules::frame_json(before);
        ofs::log::warn("opponent", "opponent.decision", std::move(line));
    }

    // Writes nothing yet: the first failures in frame order are written when the frame is merged.
    bool fail(const std::string& kind, const std::string& detail, const Step& step, bool stops = true) {
        Failure f;
        f.kind = kind;
        f.detail = detail;
        f.where = where_;
        f.stroke = frame_strokes_;
        f.player = step.is_stroke || step.judged ? step.before.striker : step.decision.player;
        f.level = side(f.player).level.id;
        f.name = exhibition_ ? format("tournament-%llu-x%02d-s%04d-%s.json", static_cast<unsigned long long>(run_.seed),
                                      job_ - run_.frames, frame_strokes_, kind.c_str())
                             : format("tournament-%llu-f%06d-s%04d-%s.json", static_cast<unsigned long long>(run_.seed),
                                      job_, frame_strokes_, kind.c_str());
        json file = {{"tournament",
                      {{"seed", std::to_string(run_.seed)},
                       {"where", where_},
                       {"frame_seed", std::to_string(frame_seed_)},
                       {"stroke", frame_strokes_},
                       {"player", f.player},
                       {"level", f.level}}},
                     {"failure", {{"kind", kind}, {"detail", detail}}},
                     {"seed", std::to_string(step.seed)},
                     {"frame", rules::frame_json(step.before)},
                     {"after", rules::frame_json(m_.frame)},
                     {"ref", rules::ref_params_values(run_.ref)},
                     {"level", opponent::level_json(side(f.player).level)}};
        if (step.is_stroke) {
            const opponent::Stroke& st = step.stroke;
            file["stroke"] = {{"cue", st.places_cue ? json::array({st.cue_pos.x, st.cue_pos.y}) : json()},
                              {"input", rules::input_json(st.input)},
                              {"strike", phys::to_json(st.strike)},
                              {"meant", phys::to_json(st.meant)},
                              {"sim_seed", std::to_string(step.in.seed)},
                              {"why", st.why}};
            if (step.simulated) file["bundle"] = phys::make_bundle(step.in, run_.setup.params);
        } else {
            file["decision"] = rules::decision_json(step.decision);
            file["calls"] = rules::calls_json(step.calls);
        }
        if (step.judged) {
            file["facts"] = rules::facts_json(step.facts);
            file["judgement"] = rules::judgement_json(step.judgement);
        }
        f.repro = std::move(file);
        out_.failures.push_back(std::move(f));
        return !stops;
    }

    bool check_frame(const Step& step) {
        if (!capture_.invalid.empty()) {
            return fail("invalid", "the engine refused an input: " + capture_.invalid.front(), step);
        }
        // The stand-in falls back on a plain stroke when it throws; the run must not pass over it.
        if (!capture_.errors.empty()) return fail("error", "app.error: " + capture_.errors.front(), step);
        std::string problem = frame_problem(run_, m_.frame);
        if (!problem.empty()) return fail("frame", problem, step);
        problem = round_trip_problem(m_.frame);
        if (!problem.empty()) return fail("json", problem, step);
        return true;
    }

    void record_frame_highs() {
        highs_recorded_ = true;
        stats(0).frame_highs.push_back(frame_high_[0]);
        stats(1).frame_highs.push_back(frame_high_[1]);
        out_.frame_best = std::max(frame_high_[0], frame_high_[1]);
    }

    // A stroke by `player` settles what the stroke before it left waiting on the next one; the
    // same player's stroke settles it as nothing followed.
    void reply(int player, const json& why, bool scored, int penalty) {
        if (follow_.player >= 0 && follow_.player != player) {
            Tally& t = stats(follow_.player);
            const bool attempt = why.value("kind", std::string()) == "pot";
            if (follow_.safety) {
                t.safety_then_attempt += attempt ? 1 : 0;
                t.safety_then_potted += attempt && scored ? 1 : 0;
            }
            if (follow_.snooker) {
                t.snooker_then_fouled += penalty > 0 ? 1 : 0;
                t.snooker_then_points += penalty;
            }
            if (follow_.break_off) t.break_off_then_pot += attempt || had_pot(why) ? 1 : 0;
        }
        follow_ = Followed{};
    }

    // The test the engine calls a free ball by (S2.17), on the table the other player now faces:
    // only after a stroke with no foul that passed the turn and left nothing pending.
    bool laid_snooker(const FrameState& pre, const rules::Judgement& j) const {
        const FrameState& post = j.post;
        if (!j.fouls.empty() || post.over || post.restart || post.striker == pre.striker ||
            post.pending != rules::Pending::none) {
            return false;
        }
        return run_.oracle->snookered(post.balls, post.cue_ball, rules::places_on(post), post.cue_in_hand);
    }

    void count_ladder(Tally& mine, int player, const json& why, const FrameState& pre, const rules::Judgement& j,
                      bool scored) {
        reply(player, why, scored, j.penalty);
        const std::string kind = why.value("kind", std::string());
        Followed next;
        next.player = player;
        if (kind == "safety" || kind == "snooker") {
            ++mine.safety_played;
            mine.snooker_kind += kind == "snooker" ? 1 : 0;
            next.safety = true;
        } else if (kind == "break") {
            ++mine.break_offs;
            next.break_off = true;
        } else if (kind == "escape") {
            const std::size_t bucket = static_cast<std::size_t>(cushion_bucket(why));
            ++mine.escape_played[bucket];
            mine.escape_hit[bucket] += hit_ball_on(j) ? 1 : 0;
        }
        if (laid_snooker(pre, j)) {
            ++mine.snookers_laid;
            mine.snookers_called += kind == "snooker" ? 1 : 0;
            next.snooker = true;
        }
        follow_ = next;
    }

    void end_break(int player, int points) {
        if (points >= 1) {
            stats(player).breaks.push_back(points);
            if (visit_on_ && points >= 147) ++stats(player).completed;
        }
        if (!exhibition_) {
            int& high = frame_high_[static_cast<std::size_t>(player)];
            high = std::max(high, points);
        }
        in_visit_ = false;
        visit_on_ = false;
        if (exhibition_) {
            break_over_ = true;
            exhibition_break_ = points;
        }
    }

    void begin_visit(int player) {
        if (in_visit_) return;
        in_visit_ = true;
        visit_on_ = opponent::maximum_on(m_.frame);
        if (visit_on_) ++stats(player).visits_on;
    }

    bool after(rules::MatchEvent event, const Step& step) {
        if (!check_frame(step)) return false;
        if (exhibition_) return true;
        switch (event) {
            case rules::MatchEvent::none: return true;
            case rules::MatchEvent::reracked:
                in_visit_ = false;
                visit_on_ = false;
                follow_ = Followed{};
                pot_points_ = 0;
                pot_allowance_ = 147;
                rules::log_frame_started(m_, rack_context());
                return true;
            case rules::MatchEvent::frame_ended:
                // Best of one: the frame's end is the match's.
                return fail("match", "a frame of a best of one ended without ending the match", step);
            case rules::MatchEvent::match_ended:
                rules::log_frame_ended(m_, context(false));
                rules::log_match_ended(m_, "frames", {{"match_id", match_id_}});
                return true;
        }
        return true;
    }

    bool choose(Step& step, int player, DecisionKind kind) {
        step.decision = {kind, player};
        rules::MatchEvent event = rules::MatchEvent::none;
        std::string error;
        const bool ok = rules::match_decide(m_, step.decision, *run_.oracle, run_.ref, run_.setup.params, step.calls,
                                            event, error);
        rules::log_choice(step.before, step.decision, ok, error, m_.frame, step.calls, context(false));
        if (!ok) {
            return fail("choice", "the engine refused " + std::string(rules::decision_name(kind)) + ": " + error, step);
        }
        if (kind == DecisionKind::concede_frame || kind == DecisionKind::concede_match) ++stats(player).concessions;
        if (kind == DecisionKind::claim_frame) ++stats(player).claims;
        if (event != rules::MatchEvent::reracked) {
            if (m_.frame.scores != step.before.scores) return fail("scores", "a choice changed the scores", step);
            if (m_.frame.strokes != step.before.strokes) return fail("strokes", "a choice counted as a stroke", step);
        }
        return after(event, step);
    }

    std::uint64_t next_seed() { return mix(mix(frame_seed_, kDecisionStream), decisions_++); }

    bool pending_choice() {
        Step step;
        step.before = m_.frame;
        const int player = m_.frame.decider;
        step.decision.player = player;
        const std::vector<DecisionKind> open = rules::options(m_.frame, player);
        if (open.empty()) return fail("options", "a decision is pending and its decider is offered nothing", step);
        step.seed = next_seed();
        current_seed_ = step.seed;
        current_player_ = player;
        ofs::Rng rng(step.seed);
        json why;
        const auto t0 = Clock::now();
        const DecisionKind kind =
            opponent::choose_option(m_.frame, player, open, side(player).level, env(), rng, &why);
        const json decided = log_choice_made(player, open, kind, step.seed, why, microseconds_since(t0), nullptr);
        in_visit_ = false;
        visit_on_ = false;
        if (!listed(open, kind)) {
            const std::string refused =
                "it chose " + std::string(rules::decision_name(kind)) + ", which was not offered";
            log_refused(decided, refused, step.before);
            return fail("opponent", refused, step);
        }
        return choose(step, player, kind);
    }

    // Why the engine would not take the stroke as given; "" when it would.
    std::string refusal(const opponent::Stroke& st) {
        FrameState& f = m_.frame;
        const phys::PhysicsParams& p = run_.setup.params;
        if (st.places_cue && !f.cue_in_hand) return "it placed a cue ball that was not in hand";
        if (!st.places_cue && f.cue_in_hand) return "it left the cue ball in hand unplaced";
        if (st.places_cue) {
            std::string error;
            const bool ok = rules::place_cue_ball(f, st.cue_pos, *run_.oracle, error);
            rules::log_cue_placed(f, st.cue_pos, ok, error, context(true));
            if (!ok) {
                return format("its cue ball place (%.6f, %.6f) was refused: %s", st.cue_pos.x, st.cue_pos.y,
                              error.c_str());
            }
        }
        const std::vector<Ball> open = rules::nominations(f);
        if (f.on.kind == rules::OnKind::any_colour && !st.input.has_nomination) return "it declared no colour";
        if (st.input.has_nomination && std::find(open.begin(), open.end(), st.input.nominated) == open.end()) {
            return "it declared " + std::string(ofs::table::ball_name(st.input.nominated)) + ", which is not open";
        }
        const phys::CueStrike& c = st.strike;
        if (!std::isfinite(c.aim_rad) || !(c.speed_m_s > 0.0) || c.speed_m_s > p.max_cue_speed_m_s) {
            return format("its strike has aim %g rad and speed %g m/s", c.aim_rad, c.speed_m_s);
        }
        if (!(std::hypot(c.tip_x, c.tip_y) <= p.miscue_limit)) return "its tip offset is past the miscue limit";
        return "";
    }

    void count_thinking(Tally& s, const opponent::Stroke& st, double us) {
        ++s.thinks;
        s.sims += st.sims;
        s.sims_max = std::max(s.sims_max, st.sims);
        s.steps += st.steps;
        s.us.push_back(us);
        bool known = false;
        for (std::size_t i = 0; i < 4; ++i) {
            if (st.stopped == kStopped[i]) {
                ++s.stopped[i];
                known = true;
            }
        }
        if (!known) ++s.stopped_other;
        if (!st.fallback.empty()) ++s.fallbacks;
    }

    bool stroke() {
        Step step;
        step.is_stroke = true;
        step.before = m_.frame;
        if (frame_strokes_ >= run_.max_strokes) {
            return fail("max_strokes", format("the frame is not over after %d strokes", run_.max_strokes), step);
        }
        const phys::PhysicsParams& params = run_.setup.params;
        const phys::TableShape& table = run_.setup.table;
        const int player = m_.frame.striker;
        const Side& me = side(player);
        Tally& mine = stats(player);

        step.seed = next_seed();
        current_seed_ = step.seed;
        current_player_ = player;
        ofs::Rng rng(step.seed);
        opponent::Think think;
        think.max_ms = run_.max_ms;
        think.seed = step.seed;
        const auto t0 = Clock::now();
        step.stroke = opponent::choose_stroke(m_.frame, me.level, env(), rng, think);
        const double us = microseconds_since(t0);
        const opponent::Stroke& st = step.stroke;
        count_thinking(mine, st, us);
        begin_visit(player);
        if (!st.fallback.empty()) {
            json line = {{"level", me.level.id}, {"why", st.fallback}, {"player", player},
                         {"seed", std::to_string(step.seed)}};
            line.update(context(true));
            ofs::log::warn("opponent", "opponent.fallback", std::move(line));
        }
        if (st.decides) {
            step.is_stroke = false;
            step.decision.player = player;
            const std::vector<DecisionKind> open = rules::options(m_.frame, player);
            const json decided = log_choice_made(player, open, st.decision, step.seed, st.why, us, &st);
            end_break(player, m_.frame.break_points);
            follow_ = Followed{};
            if (exhibition_) exhibition_why_ = st.decision == DecisionKind::claim_frame ? "claimed" : "conceded";
            if (!listed(open, st.decision)) {
                const std::string refused =
                    "it chose " + std::string(rules::decision_name(st.decision)) + ", which was not offered";
                log_refused(decided, refused, step.before);
                return fail("opponent", refused, step);
            }
            return choose(step, player, st.decision);
        }

        ++frame_strokes_;
        ++mine.strokes;
        const rules::ScopedLogContext scope(context(true));
        json decided;
        if (capture_.keep) {
            json line = {{"kind", "shot"},
                         {"player", player},
                         {"seed", std::to_string(step.seed)},
                         {"level", me.level.id},
                         {"strike", phys::to_json(st.strike)},
                         {"meant", phys::to_json(st.meant)},
                         {"nominated", st.input.has_nomination ? ofs::table::ball_name(st.input.nominated) : ""},
                         {"why", st.why},
                         {"us", us},
                         {"think", think_json(st, us)},
                         {"pre_hash", rules::frame_hash(m_.frame)}};
            if (st.places_cue) line["cue"] = {st.cue_pos.x, st.cue_pos.y};
            line.update(context(true));
            ofs::log::info("opponent", "opponent.decision", line);
            decided = std::move(line);
        }
        const std::string refused = refusal(st);
        if (!refused.empty()) {
            log_refused(decided, refused, step.before);
            return fail("opponent", refused, step);
        }

        const FrameState& pre = m_.frame;
        for (const rules::BallOnTable& b : pre.balls) {
            phys::BallState state;
            state.ball = b.ball;
            state.index = b.index;
            state.r = {b.pos.x, b.pos.y, 0.0};
            step.in.start.balls.push_back(state);
        }
        step.in.cue_ball = pre.cue_ball;
        step.in.strike = st.strike;
        step.in.seed = mix(mix(frame_seed_, kShotStream), static_cast<std::uint64_t>(frame_strokes_));
        step.in.params_hash = run_.setup.params_hash;
        step.in.table_hash = run_.setup.table_hash;
        const double least = phys::min_cue_elevation(step.in.start, pre.cue_ball, st.strike, table, params);
        if (st.strike.elevation_rad < least - 1e-12 || st.strike.elevation_rad > params.max_elevation_rad) {
            const std::string elevation = format("its elevation %g rad is outside [%g, %g], what the cue allows there",
                                                 st.strike.elevation_rad, least, params.max_elevation_rad);
            log_refused(decided, elevation, step.before);
            return fail("opponent", elevation, step);
        }

        const phys::ShotRecord record = phys::simulate(step.in, table, params);
        step.simulated = true;
        step.facts = rules::observe(pre, record, table, run_.ref);
        step.judgement = rules::judge(pre, st.input, step.facts, *run_.oracle, run_.ref);
        step.judged = true;
        const rules::Judgement& j = step.judgement;
        const json shot_context = context(true);
        if (capture_.keep) {
            phys::log_shot(step.in, params, record,
                           {{"shot_id", shot_context["shot_id"]}, {"mode", "match"}, {"strike_mode", "opponent"}});
        }
        capture_.decision_line.clear();
        rules::log_decision(pre, st.input, step.facts, j, run_.ref, shot_context);

        std::string problem = stroke_problem(pre, j);
        if (!problem.empty()) return fail("stroke", problem, step);
        const rules::Judgement again = rules::judge_shot(pre, st.input, record, table, *run_.oracle, run_.ref);
        if (rules::frame_hash(again.post) != rules::frame_hash(j.post)) {
            return fail("twice", "judging the same stroke twice gave two different frames", step);
        }
        const json logged = json::parse(capture_.decision_line, nullptr, false);
        if (!logged.is_object() || !logged.contains("data")) {
            return fail("log", "the stroke logged no rules.decision", step);
        }
        problem = rules::rejudge(logged["data"], table);
        if (!problem.empty()) return fail("log", "the logged decision re-judges differently: " + problem, step);

        // S3.12(c)-(e), S3.4(b): a free ball potted is worth up to a red and a colour more than the
        // balls on the table, a re-spotted black seven.
        pot_points_ += j.points;
        if (j.free_ball_used && j.points > 0) pot_allowance_ += 8;
        for (const rules::Call& c : j.calls) pot_allowance_ += c.kind == rules::CallKind::respotted_black ? 7 : 0;
        if (pot_points_ > pot_allowance_) {
            return fail("points", format("%d points potted in a frame that holds %d", pot_points_, pot_allowance_),
                        step);
        }

        const bool scored = j.points > 0 && j.fouls.empty();
        mine.points += j.points;
        mine.fouls += j.fouls.empty() ? 0 : 1;
        const std::string kind = st.why.value("kind", std::string());
        if (kind == "pot") {
            const int band = band_index(st.why);
            ++mine.attempts;
            ++mine.band_attempts[static_cast<std::size_t>(band)];
            mine.made += scored ? 1 : 0;
            mine.band_made[static_cast<std::size_t>(band)] += scored ? 1 : 0;
            if (me.exact) check_planned_pot(step);
        } else if (kind == "safety" || kind == "snooker" || kind == "escape") {
            ++(kind == "escape" ? mine.escapes : mine.safeties);
            ++(had_pot(st.why) ? mine.safeties_with_pot : mine.no_pot);
        }
        if (!exhibition_) count_ladder(mine, player, st.why, pre, j, scored);

        const FrameState& post = j.post;
        const bool goes_on = scored && !post.over && post.striker == pre.striker && post.pending == rules::Pending::none;
        if (!goes_on) {
            end_break(player, pre.break_points + (j.fouls.empty() ? j.points : 0));
            if (exhibition_) {
                exhibition_why_ = exhibition_break_ >= 147 ? "maximum" : !j.fouls.empty() ? "fouled" : "missed";
            }
        }

        m_.frame = j.post;
        if (exhibition_) return check_frame(step);
        return after(rules::settle(m_, params), step);
    }

    // A level with no error plays what it planned: a pot it meant must go in.
    void check_planned_pot(const Step& step) {
        const std::string target = step.stroke.why.value("target", std::string());
        const int place = target_place(step.before, target);
        if (place < 0) {
            fail("planned_pot", "a planned pot names '" + target + "', which is not on the table", step, false);
            return;
        }
        for (const rules::Pot& p : step.facts.pots) {
            if (p.ball == place) return;
        }
        const int pocket = step.stroke.why.value("pocket", -1);
        fail("planned_pot",
             format("a planned pot stayed out: %s into pocket %d", target.c_str(), pocket), step, false);
    }

    Run& run_;
    int job_;
    bool exhibition_ = false;
    std::uint64_t frame_seed_ = 0;
    std::string match_id_;
    std::string where_;
    rules::MatchState m_;
    Capture capture_;
    JobResult out_;
    std::uint64_t decisions_ = 0;  // the frame's strokes and choices so far: each one's seed
    int frame_strokes_ = 0;        // across its re-racks
    int pot_points_ = 0;
    int pot_allowance_ = 147;
    bool in_visit_ = false;
    bool visit_on_ = false;  // the visit began with a maximum still possible
    bool break_over_ = false;
    bool exhibition_ended_ = false;
    int exhibition_break_ = 0;
    std::string exhibition_why_ = "missed";
    // The decision being made, for an exception's app.error and repro.
    std::uint64_t current_seed_ = 0;
    int current_player_ = 0;
    // What the last stroke left to be counted at the other player's next one.
    struct Followed {
        int player = -1;
        bool safety = false;
        bool snooker = false;
        bool break_off = false;
    };
    Followed follow_;
    std::array<int, 2> frame_high_{};
    bool highs_recorded_ = false;
};

void work(Run& run) {
    while (true) {
        const int job = run.next.fetch_add(1);
        if (job >= run.jobs) break;
        FramePlayer player(run, job);
        run.finish(job, player.play());
    }
}

json highs_json(std::vector<int> highs) {
    std::sort(highs.begin(), highs.end());
    double median = 0.0;
    if (!highs.empty()) {
        const std::size_t n = highs.size();
        median = n % 2 == 1 ? highs[n / 2] : 0.5 * (highs[n / 2 - 1] + highs[n / 2]);
    }
    // [break, frames] for each highest break that occurred, so reports of one level can be merged.
    json counts = json::array();
    for (std::size_t i = 0; i < highs.size();) {
        std::size_t k = i;
        while (k < highs.size() && highs[k] == highs[i]) ++k;
        counts.push_back(json::array({highs[i], static_cast<long long>(k - i)}));
        i = k;
    }
    const auto at_least = [&](int points) {
        return static_cast<long long>(std::count_if(highs.begin(), highs.end(), [points](int h) { return h >= points; }));
    };
    return {{"frames", highs.size()},
            {"typical", median},
            {"fifty_plus", at_least(50)},
            {"century", at_least(100)},
            {"counts", counts}};
}

json stats_json(const Tally& s) {
    json bands = json::object();
    for (int i = 0; i < kBandCount; ++i) {
        bands[kBands[i]] = {{"attempts", s.band_attempts[std::size_t(i)]}, {"made", s.band_made[std::size_t(i)]}};
    }
    std::vector<int> sorted = s.breaks;
    std::sort(sorted.begin(), sorted.end());
    double mean = 0.0;
    double median = 0.0;
    long long fifty = 0;
    long long hundred = 0;
    long long maximums = 0;
    for (int b : sorted) {
        mean += b;
        fifty += b >= 50 ? 1 : 0;
        hundred += b >= 100 ? 1 : 0;
        maximums += b >= 147 ? 1 : 0;
    }
    if (!sorted.empty()) {
        mean /= static_cast<double>(sorted.size());
        const std::size_t n = sorted.size();
        median = n % 2 == 1 ? sorted[n / 2] : 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
    }
    const sim_cli::Stats us = stats_of(s.us);
    const double thinks = s.thinks > 0 ? static_cast<double>(s.thinks) : 1.0;
    json stopped = json::object();
    for (std::size_t i = 0; i < 4; ++i) stopped[kStopped[i]] = s.stopped[i];
    if (s.stopped_other > 0) stopped["cancelled"] = s.stopped_other;

    json by_cushions = json::object();
    long long escape_played = 0;
    long long escape_hit = 0;
    for (std::size_t i = 0; i < s.escape_played.size(); ++i) {
        escape_played += s.escape_played[i];
        escape_hit += s.escape_hit[i];
        if (s.escape_played[i] > 0) {
            by_cushions[kCushionNames[i]] = {{"played", s.escape_played[i]}, {"hit", s.escape_hit[i]}};
        }
    }
    return {{"frames_won", s.frames_won},
            {"strokes", s.strokes},
            {"pots", {{"attempts", s.attempts}, {"made", s.made}, {"bands", bands}}},
            {"breaks",
             {{"count", sorted.size()},
              {"mean", mean},
              {"median", median},
              {"highest", sorted.empty() ? 0 : sorted.back()},
              {"fifty_plus", fifty},
              {"hundred_plus", hundred},
              {"maximums", maximums}}},
            {"safeties", s.safeties},
            {"escapes", s.escapes},
            {"safeties_with_pot", s.safeties_with_pot},
            {"no_pot", s.no_pot},
            {"fouls", s.fouls},
            {"points", s.points},
            {"maximum", {{"visits_on", s.visits_on}, {"completed", s.completed}}},
            {"think",
             {{"sims_mean", static_cast<double>(s.sims) / thinks},
              {"sims_max", s.sims_max},
              {"steps_mean", static_cast<double>(s.steps) / thinks},
              {"us_mean", us.mean},
              {"us_p99", us.p99},
              {"us_max", us.max},
              {"stopped", stopped}}},
            {"fallbacks", s.fallbacks},
            {"tactics",
             {{"safeties",
               {{"played", s.safety_played},
                {"then_pot_attempt", s.safety_then_attempt},
                {"then_potted", s.safety_then_potted},
                {"then_no_attempt", s.safety_played - s.safety_then_attempt},
                {"snooker_kind", s.snooker_kind}}},
              {"snookers",
               {{"laid", s.snookers_laid},
                {"called", s.snookers_called},
                {"then_fouled", s.snooker_then_fouled},
                {"then_foul_points", s.snooker_then_points}}},
              {"escapes", {{"played", escape_played}, {"hit", escape_hit}, {"by_cushions", by_cushions}}},
              {"break_offs",
               {{"played", s.break_offs},
                {"then_pot", s.break_off_then_pot},
                {"then_no_pot", s.break_offs - s.break_off_then_pot}}}}},
            {"frame_highs", highs_json(s.frame_highs)},
            {"concessions", s.concessions},
            {"claims", s.claims}};
}

std::string percent(long long part, long long whole) {
    return whole > 0 ? format("%lld/%lld %.1f%%", part, whole, 100.0 * static_cast<double>(part) / whole)
                     : format("%lld/%lld", part, whole);
}

void print_table(const Run& run, const json& a, const json* b) {
    const auto row = [&](const std::string& name, const auto& cell) {
        std::printf("  %-22s %-28s", name.c_str(), cell(a).c_str());
        if (b != nullptr) std::printf(" %s", cell(*b).c_str());
        std::printf("\n");
    };
    std::printf("  %-22s %-28s", "", ("a: " + run.sides[0].level.id).c_str());
    if (b != nullptr) std::printf(" %s", ("b: " + run.sides[1].level.id).c_str());
    std::printf("\n");
    row("frames won", [](const json& s) { return std::to_string(s["frames_won"].get<long long>()); });
    row("strokes", [](const json& s) { return std::to_string(s["strokes"].get<long long>()); });
    row("pots", [](const json& s) {
        return percent(s["pots"]["made"].get<long long>(), s["pots"]["attempts"].get<long long>());
    });
    for (const char* band : kBands) {
        row(std::string("  ") + band, [band](const json& s) {
            const json& b = s["pots"]["bands"][band];
            return percent(b["made"].get<long long>(), b["attempts"].get<long long>());
        });
    }
    row("breaks (count)", [](const json& s) { return std::to_string(s["breaks"]["count"].get<long long>()); });
    row("  mean / median", [](const json& s) {
        return format("%.1f / %.1f", s["breaks"]["mean"].get<double>(), s["breaks"]["median"].get<double>());
    });
    row("  highest, 50+, 100+", [](const json& s) {
        const json& b = s["breaks"];
        return format("%d, %lld, %lld", b["highest"].get<int>(), b["fifty_plus"].get<long long>(),
                      b["hundred_plus"].get<long long>());
    });
    row("  147s", [](const json& s) { return std::to_string(s["breaks"]["maximums"].get<long long>()); });
    row("safeties", [](const json& s) {
        return percent(s["safeties"].get<long long>(), s["strokes"].get<long long>());
    });
    row("escapes", [](const json& s) { return std::to_string(s["escapes"].get<long long>()); });
    row("safe with a pot", [](const json& s) {
        const long long safe = s["safeties_with_pot"].get<long long>();
        return percent(safe, safe + s["pots"]["attempts"].get<long long>());
    });
    row("no pot at all", [](const json& s) {
        return percent(s["no_pot"].get<long long>(), s["strokes"].get<long long>());
    });
    row("fouls", [](const json& s) { return percent(s["fouls"].get<long long>(), s["strokes"].get<long long>()); });
    row("points", [](const json& s) { return std::to_string(s["points"].get<long long>()); });
    row("maximum on / made", [](const json& s) {
        return format("%lld / %lld", s["maximum"]["visits_on"].get<long long>(),
                      s["maximum"]["completed"].get<long long>());
    });
    row("own typical break", [](const json& s) {
        return format("%.1f", s["frame_highs"]["typical"].get<double>());
    });
    row("  frames with 100+", [](const json& s) {
        const json& h = s["frame_highs"];
        return percent(h["century"].get<long long>(), h["frames"].get<long long>());
    });
    row("safeties then a pot", [](const json& s) {
        const json& r = s["tactics"]["safeties"];
        return percent(r["then_pot_attempt"].get<long long>(), r["played"].get<long long>());
    });
    row("  then potted", [](const json& s) {
        const json& r = s["tactics"]["safeties"];
        return percent(r["then_potted"].get<long long>(), r["played"].get<long long>());
    });
    row("snookers laid / called", [](const json& s) {
        const json& n = s["tactics"]["snookers"];
        return format("%lld / %lld", n["laid"].get<long long>(), n["called"].get<long long>());
    });
    row("  then fouled, points", [](const json& s) {
        const json& n = s["tactics"]["snookers"];
        return format("%lld, %lld", n["then_fouled"].get<long long>(), n["then_foul_points"].get<long long>());
    });
    row("escapes hit a ball on", [](const json& s) {
        const json& e = s["tactics"]["escapes"];
        return percent(e["hit"].get<long long>(), e["played"].get<long long>());
    });
    row("break-offs then a pot", [](const json& s) {
        const json& b = s["tactics"]["break_offs"];
        return percent(b["then_pot"].get<long long>(), b["played"].get<long long>());
    });
    row("concessions / claims", [](const json& s) {
        return format("%lld / %lld", s["concessions"].get<long long>(), s["claims"].get<long long>());
    });
    row("sims mean / max", [](const json& s) {
        return format("%.1f / %d", s["think"]["sims_mean"].get<double>(), s["think"]["sims_max"].get<int>());
    });
    row("steps mean", [](const json& s) { return format("%.1f", s["think"]["steps_mean"].get<double>()); });
    row("think us mean/p99/max", [](const json& s) {
        const json& t = s["think"];
        return format("%.0f / %.0f / %.0f", t["us_mean"].get<double>(), t["us_p99"].get<double>(),
                      t["us_max"].get<double>());
    });
    row("stopped", [](const json& s) {
        std::string out;
        for (const auto& [key, value] : s["think"]["stopped"].items()) {
            if (value.get<long long>() == 0) continue;
            out += (out.empty() ? "" : ", ") + key + " " + std::to_string(value.get<long long>());
        }
        return out.empty() ? std::string("none") : out;
    });
    row("fallbacks", [](const json& s) { return std::to_string(s["fallbacks"].get<long long>()); });
}

void clear_old_repros(const std::string& dir) {
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().u8string();
        std::error_code entry_ec;
        const bool ours = name.compare(0, 11, "tournament-") == 0 && it->path().extension() == ".json";
        if (ours && it->is_regular_file(entry_ec)) fs::remove(it->path(), entry_ec);
    }
}

template <typename Params, typename Load>
bool load_values(const char* what, const std::string& file, Params& out, Load&& load) {
    json j;
    std::string error;
    if (!read_json_file(file, j, error)) {
        std::fprintf(stderr, "%s: %s\n", what, error.c_str());
        return false;
    }
    std::vector<std::string> problems;
    if (load(j, out, problems)) return true;
    for (const std::string& problem : problems) {
        std::fprintf(stderr, "%s: %s: %s\n", what, shareable_path(file).c_str(), problem.c_str());
    }
    return false;
}

bool level_id_ok(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// --a ID or --a-file PATH; "" when it loaded, else the error.
std::string load_side(const Args& args, const std::string& name, Side& out) {
    const std::string* id = args.value(name);
    const std::string* path = args.value(name + "-file");
    if (id != nullptr && path != nullptr) return "--" + name + " and --" + name + "-file both name a level";
    if (id == nullptr && path == nullptr) return "--" + name + " ID or --" + name + "-file PATH is needed";
    std::string file;
    if (id != nullptr) {
        if (!level_id_ok(*id)) return "--" + name + ": '" + *id + "' is not a level id";
        if (*id == kReaderId) return "--" + name + ": '" + *id + "' is the game's position reader, not an opponent";
        file = data_path("opponent_" + *id + ".json");
        std::error_code ec;
        if (!fs::is_regular_file(fs::u8path(file), ec)) {
            return "--" + name + ": unknown level '" + *id + "' (no core/data/opponent_" + *id + ".json)";
        }
    } else {
        file = *path;
    }
    std::string error;
    if (!read_json_file(file, out.file, error)) return "--" + name + ": " + error;
    std::vector<std::string> problems;
    if (!opponent::load_level(out.file, out.level, problems)) {
        std::string all;
        for (const std::string& p : problems) all += (all.empty() ? "" : "; ") + p;
        return "--" + name + ": " + shareable_path(file) + " is not a level: " + all;
    }
    if (id != nullptr && out.level.id != *id) {
        return "--" + name + ": " + shareable_path(file) + " has id '" + out.level.id + "'";
    }
    if (out.level.id == kReaderId) {
        return "--" + name + ": " + shareable_path(file) + " is the game's position reader, not an opponent";
    }
    out.source = shareable_path(file);
    out.hash = ofs::params::content_hash(out.file);
    const opponent::Level& l = out.level;
    out.exact = l.aim_sigma_rad == 0.0 && l.speed_sigma == 0.0 && l.tip_sigma == 0.0;
    return "";
}

}  // namespace

int cmd_tournament(const std::vector<std::string>& argv) {
    const ArgSpec spec{{"a", "b", "a-file", "b-file", "frames", "seed", "threads", "max-ms", "max-strokes", "report",
                        "log", "out", "params", "table", "exhibitions-file"},
                       {"exhibitions"},
                       0,
                       0};
    Args args;
    std::string error;
    if (!parse_args(argv, spec, args, error)) return usage_error("tournament", error);
    long long frames = 100;
    std::uint64_t seed = 1;
    long long threads = 2;
    long long max_strokes = 600;
    double max_ms = 0.0;
    if (!option_int(args, "frames", 0, 1000000, frames, error) || !option_u64(args, "seed", seed, error) ||
        !option_int(args, "threads", 1, 4, threads, error) ||
        !option_int(args, "max-strokes", 1, 100000, max_strokes, error) ||
        !option_double(args, "max-ms", 0.0, 3600000.0, max_ms, error)) {
        return usage_error("tournament", error);
    }
    const bool exhibitions = args.flag("exhibitions");
    if (frames == 0 && !exhibitions) return usage_error("tournament", "--frames 0 plays nothing without --exhibitions");
    if (args.value("exhibitions-file") && !exhibitions) {
        return usage_error("tournament", "--exhibitions-file needs --exhibitions");
    }

    Run run;
    run.seed = seed;
    run.frames = static_cast<int>(frames);
    run.max_strokes = static_cast<int>(max_strokes);
    run.max_ms = max_ms;
    run.out_dir = args.value("out") ? *args.value("out") : repo_path("build/tournament-failures");
    run.keep_log = args.value("log") != nullptr;
    run.two_sides = frames > 0;

    configure_logger();
    Capture head;
    head.keep = run.keep_log;
    t_capture = &head;
    ofs::log::Logger::instance().add_sink(std::make_shared<CaptureSink>());
    struct DropSinks {
        ~DropSinks() {
            ofs::log::Logger::instance().clear_sinks();
            t_capture = nullptr;
        }
    } drop_sinks;

    error = load_side(args, "a", run.sides[0]);
    if (error.empty() && run.two_sides) error = load_side(args, "b", run.sides[1]);
    if (error.empty() && !run.two_sides && (args.value("b") || args.value("b-file"))) {
        error = "--b plays frames, and --frames is 0";
    }
    if (!error.empty()) return usage_error("tournament", error);
    if (!run.two_sides) run.sides[1] = run.sides[0];

    if (run.keep_log) {
        const fs::path file = fs::u8path(*args.value("log"));
        std::error_code ec;
        if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
        run.log.open(file, std::ios::binary | std::ios::trunc);
        if (!run.log) return usage_error("tournament", "--log: cannot open " + *args.value("log") + " for writing");
    }

    bool loaded = load_setup(args, run.setup);
    if (loaded) {
        ofs::log::info("params", "params.loaded",
                       {{"source", shareable_path(run.setup.params_file)}, {"hash", run.setup.params_hash}});
        loaded = load_values("referee", data_path("referee_params.json"), run.ref, rules::load_ref_params) &&
                 load_values("stand-in", data_path("standin_params.json"), run.standin, opponent::load_standin_params);
    }
    if (loaded && exhibitions) {
        const std::string file =
            args.value("exhibitions-file") ? *args.value("exhibitions-file") : data_path("exhibitions.json");
        loaded = load_values("exhibitions", file, run.exhibitions, opponent::load_exhibitions);
        if (loaded && run.exhibitions.empty()) {
            std::fprintf(stderr, "exhibitions: %s has no layout\n", shareable_path(file).c_str());
            loaded = false;
        }
    }
    if (!loaded) {
        run.write_lines(head.lines);
        return kExitUsage;
    }
    json levels = json::array();
    for (int i = 0; i < (run.two_sides ? 2 : 1); ++i) {
        const Side& s = run.sides[static_cast<std::size_t>(i)];
        ofs::log::info("params", "params.loaded", {{"source", s.source}, {"hash", s.hash}});
        if (i == 1 && s.level.id == run.sides[0].level.id && s.hash == run.sides[0].hash) break;
        json entry = opponent::level_json(s.level);
        entry["hash"] = s.hash;
        levels.push_back(std::move(entry));
    }
    {
        // What the game's binding logs when it is configured, so `opponent` makes this log's
        // decisions with the values they were made with.
        json standin_file;
        read_json_file(data_path("standin_params.json"), standin_file, error);
        json configured = {{"ok", true},
                           {"values", rules::ref_params_values(run.ref)},
                           {"standin", standin_file.is_object() ? standin_file.value("values", json::object())
                                                                : json::object()},
                           {"params_hash", run.setup.params_hash},
                           {"levels", levels},
                           {"problems", json::array()}};
        if (exhibitions) {
            json names = json::array();
            for (const opponent::Exhibition& e : run.exhibitions) names.push_back(e.name);
            configured["exhibitions"] = std::move(names);
        }
        ofs::log::info("rules", "rules.configured", std::move(configured));
    }
    run.oracle = rules::make_geometry_oracle(run.setup.table, run.ref);
    run.area = std::make_unique<PlayArea>(run.setup.table);
    clear_old_repros(run.out_dir);
    run.write_lines(head.lines);
    t_capture = nullptr;

    run.jobs = run.frames + static_cast<int>(run.exhibitions.size());
    const std::size_t thread_count = static_cast<std::size_t>(std::min<long long>(threads, run.jobs));
    std::printf("tournament: %lld frames, seed %llu, %zu threads, params %s, table %s\n", frames,
                static_cast<unsigned long long>(seed), thread_count, run.setup.params_hash.c_str(),
                run.setup.table_hash.c_str());
    for (int i = 0; i < (run.two_sides ? 2 : 1); ++i) {
        const Side& s = run.sides[static_cast<std::size_t>(i)];
        std::printf("%s: %s (%s), %s, hash %s\n", i == 0 ? "a" : "b", s.level.id.c_str(), s.level.name.c_str(),
                    s.source.c_str(), s.hash.c_str());
    }
    std::fflush(stdout);

    const auto start = Clock::now();
    std::vector<std::thread> pool;
    for (std::size_t i = 0; i < thread_count; ++i) pool.emplace_back(work, std::ref(run));
    for (std::thread& t : pool) t.join();
    const double seconds = microseconds_since(start) / 1e6;
    if (run.keep_log) run.log.close();

    const json a = stats_json(run.totals[0]);
    const json b = stats_json(run.totals[1]);
    json failures = json::array();
    for (const Failure& f : run.failures) {
        failures.push_back({{"where", f.where},
                            {"stroke", f.stroke},
                            {"player", f.player},
                            {"level", f.level},
                            {"kind", f.kind},
                            {"detail", f.detail},
                            {"repro", f.written}});
    }
    json report = {{"frames", frames},
                   {"seed", std::to_string(seed)},
                   {"params_hash", run.setup.params_hash},
                   {"table_hash", run.setup.table_hash},
                   {"levels",
                    {{"a", opponent::level_json(run.sides[0].level)},
                     {"b", run.two_sides ? opponent::level_json(run.sides[1].level) : json()}}},
                   {"level_hashes", {{"a", run.sides[0].hash}, {"b", run.two_sides ? json(run.sides[1].hash) : json()}}},
                   {"a", a},
                   {"b", run.two_sides ? b : json()},
                   {"frame_best", highs_json(run.frame_best)},
                   {"exhibitions", run.exhibition_entries},
                   {"failures", failures},
                   {"timing",
                    {{"wall_s", seconds},
                     {"threads", thread_count},
                     {"frames_per_s", seconds > 0.0 ? static_cast<double>(frames) / seconds : 0.0}}}};

    int printed = 0;
    for (const Failure& f : run.failures) {
        if (printed++ >= kMaxPrinted) break;
        std::printf("FAIL %s, stroke %d, player %d (%s), %s: %s%s\n", f.where.c_str(), f.stroke, f.player,
                    f.level.c_str(), f.kind.c_str(), f.detail.c_str(),
                    f.written.empty() ? "" : ("\n     repro: " + f.written).c_str());
    }
    if (printed > kMaxPrinted) {
        std::printf("... and %zu more failures (all of them are in --report)\n", run.failures.size() - kMaxPrinted);
    }
    if (frames > 0) {
        print_table(run, a, &b);
        const json& best = report["frame_best"];
        std::printf("  %-22s %-28s\n", "frame's best break",
                    format("typical %.1f, 100+ in %s", best["typical"].get<double>(),
                           percent(best["century"].get<long long>(), best["frames"].get<long long>()).c_str())
                        .c_str());
    }
    for (const json& e : run.exhibition_entries) {
        std::printf("exhibition %-20s break %3d in %3d strokes%s\n", e["name"].get<std::string>().c_str(),
                    e["break_points"].get<int>(), e["strokes"].get<int>(), e["maximum"].get<bool>() ? ", 147" : "");
    }
    std::printf("failures: %zu\n", run.failures.size());
    std::printf("wall time: %.2f s (%.1f frames/s)\n", seconds, report["timing"]["frames_per_s"].get<double>());

    bool write_failed = false;
    if (args.value("report")) {
        const std::string path = *args.value("report");
        const fs::path file = fs::u8path(path);
        std::error_code ec;
        if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
        if (!write_text_file(path, report.dump(1) + "\n", error)) {
            std::fprintf(stderr, "tournament: --report: %s\n", error.c_str());
            write_failed = true;
        } else {
            std::printf("report: %s\n", shareable_path(path).c_str());
        }
    }
    if (run.keep_log) {
        if (run.log_error.empty()) {
            std::printf("log: %s\n", shareable_path(*args.value("log")).c_str());
        } else {
            std::fprintf(stderr, "tournament: --log: %s\n", run.log_error.c_str());
            write_failed = true;
        }
    }
    std::fflush(stdout);
    if (!run.failures.empty()) return kExitFailed;
    return write_failed ? kExitUsage : kExitOk;
}

}  // namespace sim_cli
