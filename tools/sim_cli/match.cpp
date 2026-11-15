// sim_cli match: the stand-in against itself over whole matches, with every stroke and choice
// checked against what a frame must keep true (spec 16.2, 21 M3). Match k of seed S is always the
// same match and the frames are counted in match order, so a run and its summary are the same
// whatever the thread count.

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

constexpr int kStrokeCap = 3000;
// Each enum's last name; a name added after it is left out of the counts until this follows.
constexpr int kFoulKinds = static_cast<int>(rules::FoulKind::before_nominating) + 1;
constexpr int kCallKinds = static_cast<int>(rules::CallKind::frame_over) + 1;
constexpr int kDecisionKinds = static_cast<int>(DecisionKind::concede_match) + 1;
constexpr int kEndReasons = static_cast<int>(rules::EndReason::match_conceded) + 1;
constexpr const char* kStrokeKinds[] = {"pot", "safety", "escape", "break"};
constexpr int kStrokeKindCount = 4;
constexpr double kD = ofs::table::kBallDiameter;
// Its own stream, so the stand-in's errors don't repeat the numbers that racked the frames.
constexpr std::uint64_t kPlayStream = 0x6d617463685f706cULL;

std::uint64_t mix(std::uint64_t a, std::uint64_t b) {
    std::uint64_t z = a + 0x9E3779B97F4A7C15ULL * (b + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// What the current thread's match logged: its lines when a log is kept, and what the checks read
// back. The logger calls a sink on the thread that logged, so each match gets its own lines.
struct Capture {
    bool keep = false;
    std::vector<std::string> lines;
    std::string decision_line;         // the last rules.decision
    std::vector<std::string> invalid;  // rules.invalid since the last step began
    int anomalies = 0;                 // physics.anomaly lines
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
        } else if (event.event == "physics.anomaly") {
            ++capture->anomalies;
        }
    }
};

template <std::size_t N, typename Kind>
void count(std::array<int, N>& counts, Kind kind) {
    const std::size_t i = static_cast<std::size_t>(kind);
    if (i < N) ++counts[i];
}

struct FrameTally {
    int strokes = 0;  // every attempt at the frame, re-racked ones included
    int points = 0;   // both players' final scores
    int penalties = 0;
    int highest_break = 0;
    int foul_strokes = 0;
    int misses = 0;
    int reracks = 0;
    int anomalies = 0;
    std::array<int, kFoulKinds> fouls{};
    std::array<int, kCallKinds> calls{};
    std::array<int, kDecisionKinds> choices{};
    std::array<int, kStrokeKindCount> kinds{};
    std::array<int, kStrokeKindCount> kinds_scored{};
    std::array<int, kStrokeKindCount> kinds_fouled{};
    rules::EndReason reason = rules::EndReason::none;
    double choose_us = 0.0;
    double choose_worst_us = 0.0;
    int chosen = 0;
    std::string final_hash;
    std::size_t log_end = 0;  // the match's log lines up to the end of this frame
};

struct Breach {
    std::string kind;
    std::string detail;
    int frame = 0;
    int stroke = 0;
    std::string repro;  // the file written, or why it was not
};

struct MatchResult {
    std::vector<FrameTally> frames;  // the frames it finished, in order
    bool breached = false;
    Breach breach;
    std::vector<std::string> lines;
};

struct Totals {
    int frames = 0;
    std::uint64_t matches = 0;
    long long strokes = 0;
    int longest = 0;
    long long points = 0;
    long long penalties = 0;
    int highest_break = 0;
    long long foul_strokes = 0;
    long long misses = 0;
    long long reracks = 0;
    long long anomalies = 0;
    std::array<long long, kFoulKinds> fouls{};
    std::array<long long, kCallKinds> calls{};
    std::array<long long, kDecisionKinds> choices{};
    std::array<long long, kStrokeKindCount> kinds{};
    std::array<long long, kStrokeKindCount> kinds_scored{};
    std::array<long long, kStrokeKindCount> kinds_fouled{};
    std::array<long long, kEndReasons> ended{};
    double choose_us = 0.0;
    double choose_worst_us = 0.0;
    long long chosen = 0;
    std::string hashes;

    void add(const FrameTally& f) {
        ++frames;
        strokes += f.strokes;
        longest = std::max(longest, f.strokes);
        points += f.points;
        penalties += f.penalties;
        highest_break = std::max(highest_break, f.highest_break);
        foul_strokes += f.foul_strokes;
        misses += f.misses;
        reracks += f.reracks;
        anomalies += f.anomalies;
        for (std::size_t i = 0; i < fouls.size(); ++i) fouls[i] += f.fouls[i];
        for (std::size_t i = 0; i < calls.size(); ++i) calls[i] += f.calls[i];
        for (std::size_t i = 0; i < choices.size(); ++i) choices[i] += f.choices[i];
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            kinds[i] += f.kinds[i];
            kinds_scored[i] += f.kinds_scored[i];
            kinds_fouled[i] += f.kinds_fouled[i];
        }
        if (static_cast<std::size_t>(f.reason) < ended.size()) ++ended[static_cast<std::size_t>(f.reason)];
        choose_us += f.choose_us;
        choose_worst_us = std::max(choose_worst_us, f.choose_worst_us);
        chosen += f.chosen;
        hashes += f.final_hash;
    }
};

struct Run {
    std::uint64_t seed = 1;
    int frames_wanted = 100;
    int best_of = 3;
    std::string out_dir;
    bool keep_log = false;
    Setup setup;
    rules::RefParams ref;
    opponent::StandinParams standin;
    std::unique_ptr<rules::TableOracle> oracle;
    std::unique_ptr<PlayArea> area;

    std::atomic<std::uint64_t> next{0};
    std::atomic<bool> stop{false};

    // Finished matches wait here until every match before them is in, then join the totals.
    std::mutex mutex;
    std::map<std::uint64_t, MatchResult> waiting;
    std::uint64_t merged = 0;
    Totals totals;
    bool breached = false;
    std::uint64_t breach_match = 0;
    Breach breach;
    std::ofstream log;
    std::string log_error;

    void write_lines(const std::vector<std::string>& lines, std::size_t count) {
        if (!keep_log || !log_error.empty()) return;
        for (std::size_t i = 0; i < count && i < lines.size(); ++i) log << lines[i] << '\n';
        if (!log) log_error = "write failed";
    }

    void finish(std::uint64_t index, MatchResult result) {
        std::lock_guard<std::mutex> lock(mutex);
        waiting.emplace(index, std::move(result));
        while (!stop.load()) {
            const auto it = waiting.find(merged);
            if (it == waiting.end()) break;
            const MatchResult& r = it->second;
            ++totals.matches;
            std::size_t written = 0;
            for (const FrameTally& f : r.frames) {
                if (totals.frames >= frames_wanted) break;
                totals.add(f);
                written = f.log_end;
            }
            if (totals.frames >= frames_wanted) {
                stop.store(true);
            } else if (r.breached) {
                breached = true;
                breach_match = merged;
                breach = r.breach;
                written = r.lines.size();
                stop.store(true);
            } else {
                written = r.lines.size();
            }
            write_lines(r.lines, written);
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

    if (s.pending == rules::Pending::none) {
        if (rules::options(s, s.striker).empty()) return "the striker is offered nothing";
        return "";
    }
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

// One stroke or choice, kept so a breach can be written out whole.
struct Step {
    FrameState before;
    bool is_stroke = false;
    opponent::StandinShot shot;
    phys::SimInput in;
    bool simulated = false;
    rules::ShotFacts facts;
    rules::Judgement judgement;
    bool judged = false;
    rules::Decision decision;
    std::vector<rules::Call> calls;
};

class MatchPlayer {
public:
    MatchPlayer(Run& run, std::uint64_t index)
        : run_(run), index_(index), match_seed_(mix(run.seed, index)), rng_(mix(match_seed_, kPlayStream)) {
        capture_.keep = run.keep_log;
        match_id_ = format("sim-%llu-%llu", static_cast<unsigned long long>(run.seed),
                           static_cast<unsigned long long>(index));
    }

    MatchResult play() {
        t_capture = &capture_;
        try {
            rules::MatchConfig config;
            config.best_of = run_.best_of;
            config.seed = match_seed_;
            m_ = rules::start_match(config, run_.setup.params);
            rules::log_match_started(m_, {{"match_id", match_id_}});
            rules::log_frame_started(m_, context(false));
            bool ok = check_frame(Step{});
            while (ok && !m_.over && !run_.stop.load(std::memory_order_relaxed)) {
                capture_.invalid.clear();
                ok = m_.frame.pending == rules::Pending::none ? stroke() : pending_choice();
            }
        } catch (const std::exception& e) {
            // The engine and the stand-in promise never to throw.
            fail("exception", e.what(), Step{});
        }
        t_capture = nullptr;
        out_.lines = std::move(capture_.lines);
        return std::move(out_);
    }

private:
    json context(bool with_shot) const {
        json c = {{"match_id", match_id_}, {"frame", m_.frame_number}};
        if (with_shot) c["shot_id"] = format("%s-f%d-s%d", match_id_.c_str(), m_.frame_number, frame_strokes_);
        return c;
    }

    bool fail(const std::string& kind, const std::string& detail, const Step& step) {
        out_.breached = true;
        Breach& b = out_.breach;
        b.kind = kind;
        b.detail = detail;
        b.frame = m_.frame_number;
        b.stroke = frame_strokes_;
        json file = {{"match",
                      {{"seed", std::to_string(run_.seed)},
                       {"match", index_},
                       {"match_seed", std::to_string(match_seed_)},
                       {"best_of", run_.best_of},
                       {"frame", b.frame},
                       {"stroke", b.stroke}}},
                     {"breach", {{"kind", kind}, {"detail", detail}}},
                     {"frame", rules::frame_json(step.before)},
                     {"after", rules::frame_json(m_.frame)},
                     {"ref", rules::ref_params_values(run_.ref)}};
        if (step.is_stroke) {
            const opponent::StandinShot& shot = step.shot;
            file["stroke"] = {{"cue", shot.places_cue ? json::array({shot.cue_pos.x, shot.cue_pos.y}) : json()},
                              {"input", rules::input_json(shot.input)},
                              {"strike", phys::to_json(shot.strike)},
                              {"sim_seed", std::to_string(step.in.seed)},
                              {"why", shot.why}};
            if (step.simulated) file["bundle"] = phys::make_bundle(step.in, run_.setup.params);
        } else {
            file["decision"] = rules::decision_json(step.decision);
            file["calls"] = rules::calls_json(step.calls);
        }
        if (step.judged) {
            file["facts"] = rules::facts_json(step.facts);
            file["judgement"] = rules::judgement_json(step.judgement);
        }
        const std::string name =
            format("match-%llu-%06llu-f%02d-s%04d.json", static_cast<unsigned long long>(run_.seed),
                   static_cast<unsigned long long>(index_), b.frame, b.stroke);
        const std::string path = (fs::u8path(run_.out_dir) / name).u8string();
        std::string error;
        b.repro = write_text_file(path, file.dump(1) + "\n", error) ? path : "(not written: " + error + ")";
        return false;
    }

    // The frame in play after a step: the table, the turn and what is offered, and its JSON.
    bool check_frame(const Step& step) {
        if (!capture_.invalid.empty()) {
            return fail("invalid", "the engine refused an input: " + capture_.invalid.front(), step);
        }
        std::string problem = frame_problem(run_, m_.frame);
        if (!problem.empty()) return fail("frame", problem, step);
        problem = round_trip_problem(m_.frame);
        if (!problem.empty()) return fail("json", problem, step);
        return true;
    }

    void count_calls(const std::vector<rules::Call>& calls) {
        for (const rules::Call& c : calls) count(tally_.calls, c.kind);
    }

    void close_frame() {
        const rules::FrameSummary& s = m_.history.back();
        tally_.strokes = frame_strokes_;
        tally_.points = s.scores[0] + s.scores[1];
        tally_.penalties = s.stats[0].points_from_fouls + s.stats[1].points_from_fouls;
        tally_.highest_break = std::max(s.stats[0].highest_break, s.stats[1].highest_break);
        tally_.reason = s.reason;
        tally_.anomalies = capture_.anomalies;
        tally_.final_hash = rules::frame_hash(m_.frame);
        tally_.log_end = capture_.lines.size();
        out_.frames.push_back(tally_);
        tally_ = FrameTally{};
        capture_.anomalies = 0;
        frame_strokes_ = 0;
        pot_points_ = 0;
        pot_allowance_ = 147;
    }

    // What the match made of the frame after a stroke or choice, then the checks.
    bool after(rules::MatchEvent event, const Step& step) {
        if (!check_frame(step)) return false;
        switch (event) {
            case rules::MatchEvent::none: return true;
            case rules::MatchEvent::reracked:
                ++tally_.reracks;
                pot_points_ = 0;
                pot_allowance_ = 147;
                rules::log_frame_started(m_, context(false));
                return true;
            case rules::MatchEvent::frame_ended:
                rules::log_frame_ended(m_, context(false));
                close_frame();
                if (!rules::next_frame(m_, run_.setup.params)) return fail("match", "the next frame was refused", step);
                rules::log_frame_started(m_, context(false));
                return check_frame(step);
            case rules::MatchEvent::match_ended:
                rules::log_frame_ended(m_, context(false));
                rules::log_match_ended(m_, "frames", {{"match_id", match_id_}});
                close_frame();
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
        count(tally_.choices, kind);
        count_calls(step.calls);
        if (event != rules::MatchEvent::reracked) {
            if (m_.frame.scores != step.before.scores) return fail("scores", "a choice changed the scores", step);
            if (m_.frame.strokes != step.before.strokes) return fail("strokes", "a choice counted as a stroke", step);
        }
        return after(event, step);
    }

    bool pending_choice() {
        Step step;
        step.before = m_.frame;
        const int player = m_.frame.decider;
        const std::vector<DecisionKind> open = rules::options(m_.frame, player);
        if (open.empty()) return fail("options", "a decision is pending and its decider is offered nothing", step);
        const DecisionKind kind = opponent::choose_decision(m_.frame, player, open, run_.standin, rng_);
        if (capture_.keep) {
            json names = json::array();
            for (DecisionKind k : open) names.push_back(json(rules::decision_name(k)));
            ofs::log::info("opponent", "opponent.decision",
                           {{"kind", "choice"},
                            {"player", player},
                            {"choice", rules::decision_name(kind)},
                            {"options", names},
                            {"match_id", match_id_},
                            {"frame", m_.frame_number}});
        }
        if (!listed(open, kind)) {
            return fail("standin", "it chose " + std::string(rules::decision_name(kind)) + ", which was not offered",
                        step);
        }
        return choose(step, player, kind);
    }

    // Why the engine would not take the stand-in's stroke as given; "" when it would.
    std::string refusal(const opponent::StandinShot& shot) {
        FrameState& f = m_.frame;
        const phys::PhysicsParams& p = run_.setup.params;
        if (shot.places_cue && !f.cue_in_hand) return "it placed a cue ball that was not in hand";
        if (!shot.places_cue && f.cue_in_hand) return "it left the cue ball in hand unplaced";
        if (shot.places_cue) {
            std::string error;
            const bool ok = rules::place_cue_ball(f, shot.cue_pos, *run_.oracle, error);
            rules::log_cue_placed(f, shot.cue_pos, ok, error, context(true));
            if (!ok) {
                return format("its cue ball place (%.6f, %.6f) was refused: %s", shot.cue_pos.x, shot.cue_pos.y,
                              error.c_str());
            }
        }
        const std::vector<Ball> open = rules::nominations(f);
        if (f.on.kind == rules::OnKind::any_colour && !shot.input.has_nomination) return "it declared no colour";
        if (shot.input.has_nomination && std::find(open.begin(), open.end(), shot.input.nominated) == open.end()) {
            return "it declared " + std::string(ofs::table::ball_name(shot.input.nominated)) + ", which is not open";
        }
        const phys::CueStrike& c = shot.strike;
        if (!std::isfinite(c.aim_rad) || !(c.speed_m_s > 0.0) || c.speed_m_s > p.max_cue_speed_m_s) {
            return format("its strike has aim %g rad and speed %g m/s", c.aim_rad, c.speed_m_s);
        }
        if (!(std::hypot(c.tip_x, c.tip_y) <= p.miscue_limit)) return "its tip offset is past the miscue limit";
        return "";
    }

    bool stroke() {
        Step step;
        step.is_stroke = true;
        step.before = m_.frame;
        if (frame_strokes_ >= kStrokeCap) {
            return fail("stroke_cap", format("the frame is not over after %d strokes", kStrokeCap), step);
        }
        const phys::PhysicsParams& params = run_.setup.params;
        const phys::TableShape& table = run_.setup.table;

        const auto t0 = Clock::now();
        step.shot = opponent::choose_shot(m_.frame, table, params, *run_.oracle, run_.ref, run_.standin, rng_);
        const double us = microseconds_since(t0);
        tally_.choose_us += us;
        tally_.choose_worst_us = std::max(tally_.choose_worst_us, us);
        ++tally_.chosen;
        const opponent::StandinShot& shot = step.shot;
        if (shot.decides) {
            step.is_stroke = false;
            return choose(step, m_.frame.striker, shot.decision);
        }

        ++frame_strokes_;
        if (capture_.keep) {
            json line = {{"kind", "shot"},
                         {"player", m_.frame.striker},
                         {"strike", phys::to_json(shot.strike)},
                         {"nominated", shot.input.has_nomination ? ofs::table::ball_name(shot.input.nominated) : ""},
                         {"why", shot.why},
                         {"us", us}};
            if (shot.places_cue) line["cue"] = {shot.cue_pos.x, shot.cue_pos.y};
            line.update(context(true));
            ofs::log::info("opponent", "opponent.decision", std::move(line));
        }
        const std::string refused = refusal(shot);
        if (!refused.empty()) return fail("standin", refused, step);

        const FrameState& pre = m_.frame;
        for (const rules::BallOnTable& b : pre.balls) {
            phys::BallState state;
            state.ball = b.ball;
            state.index = b.index;
            state.r = {b.pos.x, b.pos.y, 0.0};
            step.in.start.balls.push_back(state);
        }
        step.in.cue_ball = pre.cue_ball;
        step.in.strike = shot.strike;
        step.in.seed = rng_.next_u64();
        step.in.params_hash = run_.setup.params_hash;
        step.in.table_hash = run_.setup.table_hash;
        const double least = phys::min_cue_elevation(step.in.start, pre.cue_ball, shot.strike.aim_rad, table, params);
        if (shot.strike.elevation_rad < least - 1e-12 || shot.strike.elevation_rad > params.max_elevation_rad) {
            return fail("standin",
                        format("its elevation %g rad is outside [%g, %g], what the cue allows there",
                               shot.strike.elevation_rad, least, params.max_elevation_rad),
                        step);
        }

        const phys::ShotRecord record = phys::simulate(step.in, table, params);
        step.simulated = true;
        step.facts = rules::observe(pre, record, table, run_.ref);
        step.judgement = rules::judge(pre, shot.input, step.facts, *run_.oracle, run_.ref);
        step.judged = true;
        const rules::Judgement& j = step.judgement;
        const json shot_context = context(true);
        if (capture_.keep) {
            phys::log_shot(step.in, params, record,
                           {{"shot_id", shot_context["shot_id"]}, {"mode", "match"}, {"strike_mode", "opponent"}});
        }
        capture_.decision_line.clear();
        rules::log_decision(pre, shot.input, step.facts, j, run_.ref, shot_context);

        std::string problem = stroke_problem(pre, j);
        if (!problem.empty()) return fail("stroke", problem, step);
        const rules::Judgement again = rules::judge_shot(pre, shot.input, record, table, *run_.oracle, run_.ref);
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

        count_calls(j.calls);
        tally_.foul_strokes += j.fouls.empty() ? 0 : 1;
        tally_.misses += j.miss ? 1 : 0;
        for (const rules::Foul& f : j.fouls) count(tally_.fouls, f.kind);
        const std::string kind = shot.why.value("kind", std::string());
        for (int k = 0; k < kStrokeKindCount; ++k) {
            if (kind != kStrokeKinds[k]) continue;
            ++tally_.kinds[static_cast<std::size_t>(k)];
            tally_.kinds_scored[static_cast<std::size_t>(k)] += j.points > 0 ? 1 : 0;
            tally_.kinds_fouled[static_cast<std::size_t>(k)] += j.fouls.empty() ? 0 : 1;
        }

        m_.frame = j.post;
        return after(rules::settle(m_, params), step);
    }

    Run& run_;
    std::uint64_t index_;
    std::uint64_t match_seed_;
    ofs::Rng rng_;  // the stand-in's errors and each stroke's simulator seed
    std::string match_id_;
    rules::MatchState m_;
    Capture capture_;
    MatchResult out_;
    FrameTally tally_;
    int frame_strokes_ = 0;  // in the frame in play, across its re-racks
    int pot_points_ = 0;     // scored by pots since the frame was last racked
    int pot_allowance_ = 147;
};

void work(Run& run) {
    while (!run.stop.load()) {
        const std::uint64_t index = run.next.fetch_add(1);
        // Every match has a frame, so the run's frames never need more matches than that.
        if (index >= static_cast<std::uint64_t>(run.frames_wanted)) break;
        MatchPlayer player(run, index);
        run.finish(index, player.play());
    }
}

template <std::size_t N, typename Name>
json named_counts(const std::array<long long, N>& counts, Name&& name, std::size_t first = 0) {
    json out = json::object();
    for (std::size_t i = first; i < N; ++i) out[std::string(name(i))] = counts[i];
    return out;
}

std::string listed_counts(const json& counts) {
    std::string out;
    for (const auto& [key, value] : counts.items()) {
        if (value.get<long long>() == 0) continue;
        out += (out.empty() ? "" : ", ") + key + " " + std::to_string(value.get<long long>());
    }
    return out.empty() ? "none" : out;
}

void clear_old_repros(const std::string& dir) {
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().u8string();
        std::error_code entry_ec;
        const bool ours = name.compare(0, 6, "match-") == 0 && it->path().extension() == ".json";
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
        std::fprintf(stderr, "%s: %s: %s\n", what, file.c_str(), problem.c_str());
    }
    return false;
}

}  // namespace

int cmd_match(const std::vector<std::string>& argv) {
    const ArgSpec spec{{"frames", "best-of", "seed", "threads", "out", "log", "params", "table"}, {}, 0, 0};
    Args args;
    std::string error;
    if (!parse_args(argv, spec, args, error)) return usage_error("match", error);
    long long frames = 100;
    long long best_of = 3;
    std::uint64_t seed = 1;
    long long threads = std::max(1u, std::thread::hardware_concurrency());
    if (!option_int(args, "frames", 1, 1000000, frames, error) || !option_int(args, "best-of", 1, 35, best_of, error) ||
        !option_u64(args, "seed", seed, error) || !option_int(args, "threads", 1, 1024, threads, error)) {
        return usage_error("match", error);
    }
    rules::MatchConfig config;
    config.best_of = static_cast<int>(best_of);
    if (!rules::valid_config(config, error)) return usage_error("match", "--best-of: " + error);

    Run run;
    run.seed = seed;
    run.frames_wanted = static_cast<int>(frames);
    run.best_of = static_cast<int>(best_of);
    run.out_dir = args.value("out") ? *args.value("out") : repo_path("build/match-failures");
    run.keep_log = args.value("log") != nullptr;

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
    if (run.keep_log) {
        const fs::path file = fs::u8path(*args.value("log"));
        std::error_code ec;
        if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
        run.log.open(file, std::ios::binary | std::ios::trunc);
        if (!run.log) return usage_error("match", "--log: cannot open " + *args.value("log") + " for writing");
    }

    bool loaded = load_setup(args, run.setup);
    if (loaded) {
        ofs::log::info("params", "params.loaded",
                       {{"source", shareable_path(run.setup.params_file)}, {"hash", run.setup.params_hash}});
        loaded = load_values("referee", data_path("referee_params.json"), run.ref, rules::load_ref_params) &&
                 load_values("stand-in", data_path("standin_params.json"), run.standin, opponent::load_standin_params);
    }
    if (!loaded) {
        // Each refused value's params.invalid is in the log.
        run.write_lines(head.lines, head.lines.size());
        return kExitUsage;
    }
    run.oracle = rules::make_geometry_oracle(run.setup.table, run.ref);
    run.area = std::make_unique<PlayArea>(run.setup.table);
    clear_old_repros(run.out_dir);
    run.write_lines(head.lines, head.lines.size());
    t_capture = nullptr;

    const std::size_t thread_count = static_cast<std::size_t>(std::min<long long>(threads, frames));
    std::printf("match: %lld frames, best of %lld, seed %llu, %zu threads, params %s, table %s\n", frames, best_of,
                static_cast<unsigned long long>(seed), thread_count, run.setup.params_hash.c_str(),
                run.setup.table_hash.c_str());
    std::fflush(stdout);

    const auto start = Clock::now();
    std::vector<std::thread> pool;
    for (std::size_t i = 0; i < thread_count; ++i) pool.emplace_back(work, std::ref(run));
    for (std::thread& t : pool) t.join();
    const double seconds = microseconds_since(start) / 1e6;
    if (run.keep_log) run.log.close();

    const Totals& t = run.totals;
    if (run.breached) {
        const Breach& b = run.breach;
        std::printf("BREACH match %llu, frame %d, stroke %d (%s): %s\n       repro: %s\n",
                    static_cast<unsigned long long>(run.breach_match), b.frame, b.stroke, b.kind.c_str(),
                    b.detail.c_str(), b.repro.c_str());
    }
    const double per_frame = t.frames > 0 ? 1.0 / t.frames : 0.0;
    const json fouls =
        named_counts(t.fouls, [](std::size_t i) { return rules::foul_name(static_cast<rules::FoulKind>(i)); });
    const json calls =
        named_counts(t.calls, [](std::size_t i) { return rules::call_name(static_cast<rules::CallKind>(i)); });
    const json choices =
        named_counts(t.choices, [](std::size_t i) { return rules::decision_name(static_cast<DecisionKind>(i)); });
    // No frame ends for no reason, so that name is left out.
    const json ended = named_counts(
        t.ended, [](std::size_t i) { return rules::end_reason_name(static_cast<rules::EndReason>(i)); }, 1);
    const json kinds = named_counts(t.kinds, [](std::size_t i) { return kStrokeKinds[i]; });
    const json scored = named_counts(t.kinds_scored, [](std::size_t i) { return kStrokeKinds[i]; });
    const json fouled = named_counts(t.kinds_fouled, [](std::size_t i) { return kStrokeKinds[i]; });
    const long long free_balls = t.calls[static_cast<std::size_t>(rules::CallKind::free_ball)];
    const long long respots = t.calls[static_cast<std::size_t>(rules::CallKind::respotted_black)];
    const double choose_mean = t.chosen > 0 ? t.choose_us / static_cast<double>(t.chosen) : 0.0;

    std::printf("frames %d of %lld in %llu matches, %lld strokes\n", t.frames, frames,
                static_cast<unsigned long long>(t.matches), t.strokes);
    std::printf("strokes per frame: mean %.1f, longest %d\n", static_cast<double>(t.strokes) * per_frame, t.longest);
    std::printf("points per frame: mean %.1f (%.1f from penalties); highest break %d\n",
                static_cast<double>(t.points) * per_frame, static_cast<double>(t.penalties) * per_frame,
                t.highest_break);
    std::printf("strokes by kind: %s\n", listed_counts(kinds).c_str());
    std::printf("        scoring: %s\n", listed_counts(scored).c_str());
    std::printf("          fouls: %s\n", listed_counts(fouled).c_str());
    std::printf("fouls: %lld strokes (%s)\n", t.foul_strokes, listed_counts(fouls).c_str());
    std::printf("misses %lld, free balls %lld, re-spotted blacks %lld, re-racks %lld\n", t.misses, free_balls, respots,
                t.reracks);
    std::printf("calls: %s\n", listed_counts(calls).c_str());
    std::printf("choices: %s\n", listed_counts(choices).c_str());
    std::printf("frames ended: %s\n", listed_counts(ended).c_str());
    std::printf("physics anomalies: %lld\n", t.anomalies);
    std::printf("choose_shot: mean %.0f us, worst %.0f us\n", choose_mean, t.choose_worst_us);
    std::printf("wall time: %.2f s\n", seconds);
    if (run.keep_log) {
        if (run.log_error.empty()) {
            std::printf("log: %s\n", args.value("log")->c_str());
        } else {
            std::fprintf(stderr, "match: --log: %s\n", run.log_error.c_str());
        }
    }
    const json line = {
        {"summary",
         {{"frames", t.frames},
          {"matches", t.matches},
          {"best_of", run.best_of},
          {"seed", std::to_string(seed)},
          {"breached", run.breached},
          {"strokes", t.strokes},
          {"strokes_per_frame", static_cast<double>(t.strokes) * per_frame},
          {"longest_frame", t.longest},
          {"points_per_frame", static_cast<double>(t.points) * per_frame},
          {"penalties_per_frame", static_cast<double>(t.penalties) * per_frame},
          {"highest_break", t.highest_break},
          {"stroke_kinds", kinds},
          {"scored", scored},
          {"fouled", fouled},
          {"foul_strokes", t.foul_strokes},
          {"fouls", fouls},
          {"calls", calls},
          {"misses", t.misses},
          {"free_balls", free_balls},
          {"respotted_blacks", respots},
          {"reracks", t.reracks},
          {"choices", choices},
          {"ended", ended},
          {"anomalies", t.anomalies},
          {"frames_digest", ofs::params::fnv1a64_hex(t.hashes)}}},
        {"timing",
         {{"choose_shot_us_mean", choose_mean},
          {"choose_shot_us_worst", t.choose_worst_us},
          {"wall_s", seconds},
          {"threads", thread_count}}}};
    std::printf("%s\n", line.dump().c_str());
    std::fflush(stdout);
    if (run.breached) return kExitFailed;
    return run.keep_log && !run.log_error.empty() ? kExitUsage : kExitOk;
}

}  // namespace sim_cli
