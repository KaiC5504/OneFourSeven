// The choice of a stroke (spec 10.2, docs/opponent/README.md "The search"). Every pot the position
// offers is struck each way the level can on the simulator and judged by the referee; a level
// with error takes the best chance times what it wins, the 147 the pot whose plan keeps its break
// most alive. Safeties, escapes and break-offs are the stand-in's.

#include "ofs/opponent/opponent.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/physics/cue.hpp"
#include "ofs/rules/engine.hpp"
#include "ofs/rules/rules_log.hpp"
#include "ofs/rules/serialize.hpp"

#include "maximum.hpp"

namespace ofs::opponent {

namespace {

using rules::Ball;
using rules::BallOnTable;
using rules::DecisionKind;
using rules::FrameState;
using rules::OnKind;
using rules::Vec2;
using Clock = std::chrono::steady_clock;

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr int kMaxDepth = 3;  // level.depth's range

// Tip offsets as shares of level.max_tip, walking out from centre ball: top, screw, half of each,
// left and right, the diagonals, half side; then the quarters between for a level that tries more.
struct Tip {
    double x;
    double y;
};
constexpr double kDiagonal = 0.7071067811865476;
constexpr Tip kTips[] = {
    {0.0, 0.0},
    {0.0, 1.0},
    {0.0, -1.0},
    {0.0, 0.5},
    {0.0, -0.5},
    {-1.0, 0.0},
    {1.0, 0.0},
    {-kDiagonal, kDiagonal},
    {kDiagonal, kDiagonal},
    {-kDiagonal, -kDiagonal},
    {kDiagonal, -kDiagonal},
    {-0.5, 0.0},
    {0.5, 0.0},
    {0.0, 0.75},
    {0.0, -0.75},
    {0.0, 0.25},
    {0.0, -0.25},
    {-kDiagonal / 2.0, kDiagonal / 2.0},
    {kDiagonal / 2.0, kDiagonal / 2.0},
    {-kDiagonal / 2.0, -kDiagonal / 2.0},
    {kDiagonal / 2.0, -kDiagonal / 2.0},
    {-0.75, 0.0},
    {0.75, 0.0},
    {-0.25, 0.0},
    {0.25, 0.0},
};
constexpr int kTipCount = int(sizeof(kTips) / sizeof(kTips[0]));

// The fastest cue a pot is planned with, as a share of the physics' fastest: nearly full power,
// with room left for a speed error.
constexpr double kTopSpeed = 0.95;
// Where a level's speeds lie between the least that pots and the top, as shares of the way in
// ratio. Eight or more cover it all; two are both firm, the way a beginner hits a ball.
constexpr double kFewFrom = 0.35;
constexpr double kFewTo = 0.6;
constexpr int kFullLadder = 8;
constexpr double kOneSpeed = 0.45;

constexpr int kSolveSims = 6;

// The 147's search. Its first stage may spend this share of the level's simulations on the grid
// of every pot struck every way; what is left looks further along the best.
constexpr double kGridShare = 0.45;
// A cue ball's run doubles from one rung of the ladder to the next. While the grid's share lasts
// every pot that was made is tried again half way between the rungs, then at the quarters; after
// that the best few pots and spins are tried either side of their best speed, at a half and a
// quarter of the spacing reached.
constexpr int kBetweenPasses = 2;
constexpr int kRefined = 6;
constexpr double kRefineSteps[] = {0.5, 0.25};
// Candidates looked one stroke further at each stage beyond the first, and how many of a
// position's own pots are followed below it. A stage whose best plan is cut short goes on down
// its list, to twice as many.
constexpr int kBeam[kMaxDepth + 1] = {0, 8, 4, 2};
constexpr int kFollowed = 3;
// A position below the first is tried with fewer pots and about kDeepTries strokes in all: every
// rung of the ladder while it has one pot or two, else kDeepSpeeds of them, and as many spins as
// that leaves, never under kDeepSpins. The rungs and spins are among the first position's own, so
// a plan found below is found again when its position comes to be played.
constexpr int kDeepPots = 4;
constexpr int kDeepTries[kMaxDepth + 1] = {0, 120, 90, 60};
constexpr int kDeepSpeeds[kMaxDepth + 1] = {0, 6, 5, 4};
constexpr int kDeepSpins = 3;
// Two leaves are the same leave when the same ball went and the cue ball stops this close.
constexpr double kSameLeave_m = 0.05;
// A stroke that ends a maximum still on is worth this much less than any that keeps it.
constexpr double kLost = 1000.0;
// What a stroke costs, in points, at full pace and at the most spin the level plays, each growing
// as the square: of two strokes that leave the table as well the quieter is played, as a
// break-builder plays it. Small beside what a better position is worth.
constexpr double kPaceCost = 0.8;
constexpr double kSpinCost = 0.25;

double distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

std::string label(const BallOnTable& b) {
    std::string out(table::ball_name(b.ball));
    if (b.ball == Ball::red) out += " " + std::to_string(b.index);
    return out;
}

std::string label(const rules::Position& p, int place) {
    return place >= 0 && place < int(p.balls.size()) ? label(p.balls[std::size_t(place)]) : std::string();
}

int reds_left(const rules::Position& p) {
    int reds = 0;
    for (const BallOnTable& b : p.balls) reds += b.ball == Ball::red ? 1 : 0;
    return reds;
}

phys::TableState table_state(const rules::Position& p, Vec2 cue) {
    phys::TableState t;
    t.balls.reserve(p.balls.size());
    for (const BallOnTable& b : p.balls) {
        phys::BallState s;
        s.ball = b.ball;
        s.index = b.index;
        s.r = {b.pos.x, b.pos.y, 0.0};
        t.balls.push_back(s);
    }
    if (p.cue_ball >= 0 && p.cue_ball < int(t.balls.size())) t.balls[std::size_t(p.cue_ball)].r = {cue.x, cue.y, 0.0};
    return t;
}

// The cue speeds a pot is tried at: `count` of them from `from` to `to` of the way, in ratio,
// between the least that pots it and the top.
std::vector<double> ladder(const Pot& pot, int count, double from, double to, const Env& env) {
    const double top = kTopSpeed * env.params.max_cue_speed_m_s;
    const double least = std::min(least_pot_speed(pot, env), top);
    std::vector<double> out;
    for (int i = 0; i < count; ++i) {
        const double share = count > 1 ? from + (to - from) * i / (count - 1) : from;
        out.push_back(least * std::pow(top / least, share));
    }
    return out;
}

// How far up that range a speed lies, 0 to 1.
double pace(const Pot& pot, double speed, const Env& env) {
    const double top = kTopSpeed * env.params.max_cue_speed_m_s;
    const double least = std::min(least_pot_speed(pot, env), top);
    if (!(top > least) || !(speed > least)) return 0.0;
    return std::min(std::log(speed / least) / std::log(top / least), 1.0);
}

struct Range {
    double from = 0.0;
    double to = 1.0;
};

Range speed_range(int speeds) {
    if (speeds <= 1) return {kOneSpeed, kOneSpeed};
    const double wide = std::clamp(double(speeds - 2) / (kFullLadder - 2), 0.0, 1.0);
    return {kFewFrom * (1.0 - wide), kFewTo + (1.0 - kFewTo) * wide};
}

std::string maximum_word(const FrameState& s, const Level& level) {
    if (!level.maximum) return "off";
    return maximum_on(s) ? "on" : "gone";
}

void say_maximum(nlohmann::json& why, const FrameState& s, const Level& level) {
    const std::string word = maximum_word(s, level);
    why["maximum"] = word;
    if (word != "gone") return;
    why["maximum_why"] = reds_left(s) < 15 && s.break_points == 0 ? "a red is off the table" : "the break lost it";
}

// The budget of one decision, asked before each step.
class Budget {
public:
    Budget(const Level& level, const Think& think)
        : think_(think), max_sims_(think.max_sims > 0 ? think.max_sims : level.max_sims), started_(Clock::now()) {}

    bool cancelled() const { return think_.cancel != nullptr && think_.cancel->load(std::memory_order_relaxed); }

    // Whether another step may be taken; once it may not, `stopped` says why and stays.
    bool open() {
        if (halted_) return false;
        const char* why = nullptr;
        if (cancelled()) {
            why = "cancelled";
        } else if (think_.max_steps > 0 && steps >= think_.max_steps) {
            why = "steps";
        } else if (steps > 0) {
            // The first step is always taken: a decision logged as stopped is made again from the
            // steps it took, and no steps is no limit.
            if (sims >= max_sims_) {
                why = "sims";
            } else if (think_.max_ms > 0.0 &&
                       std::chrono::duration<double, std::milli>(Clock::now() - started_).count() >= think_.max_ms) {
                why = "time";
            }
        }
        if (why == nullptr) return true;
        halted_ = true;
        stopped = why;
        return false;
    }

    bool halted() const { return halted_; }
    void halt(const char* why) {
        halted_ = true;
        stopped = why;
    }

    int sims = 0;
    int steps = 0;
    std::string stopped = "done";

private:
    const Think& think_;
    int max_sims_;
    Clock::time_point started_;
    bool halted_ = false;
};

// A pot that was made on the simulator and passed by the referee.
struct Made {
    phys::CueStrike strike;
    int points = 0;
    Vec2 cue_rest;
    FrameState post;
    bool goes_on = false;  // the same player is at the table after it, nothing pending
};

struct Bench {
    const Level& level;
    const Env& env;
    Budget& budget;
};

// The stroke judged as the match will judge it. False for a foul, a pot that stayed out or went
// elsewhere, or anything the simulator stopped.
bool judged(const Bench& b, const FrameState& s, const Pot& pot, const phys::CueStrike& strike, Made& out) {
    FrameState from = s;
    if (from.cue_in_hand) {
        std::string error;
        if (!rules::place_cue_ball(from, pot.cue, b.env.oracle, error)) return false;
    }
    phys::ShotRecord record;
    const Trial trial = try_strike(from, pot.cue, strike, b.env, &record);
    ++b.budget.sims;
    if (!trial.clean || trial.cue_potted) return false;
    if (pot.target >= int(record.final_state.balls.size()) ||
        record.final_state.balls[std::size_t(pot.target)].pocket != pot.pocket) {
        return false;
    }
    rules::Judgement j = rules::judge_shot(from, pot.input, record, b.env.table, b.env.oracle, b.env.ref);
    if (!j.fouls.empty() || j.points <= 0) return false;
    out.strike = strike;
    out.points = j.points;
    out.cue_rest = trial.rest[std::size_t(s.cue_ball)];
    out.goes_on = !j.post.over && j.post.pending == rules::Pending::none && j.post.striker == s.striker;
    out.post = std::move(j.post);
    return true;
}

// One candidate: the aim that makes `pot` struck with `spec`, starting the solver at `from_aim`,
// then the stroke judged. The cue lies as low as the table lets it along the aim, which the aim
// found can change: the stroke is judged with the cue where it must be, and solved once more from
// there when that spoils it.
bool attempt(const Bench& b, const FrameState& s, const Pot& pot, Spec spec, double from_aim, Made& out) {
    const phys::TableState table = table_state(s, pot.cue);
    Pot start = pot;
    start.aim_rad = from_aim;
    for (int round = 0; round < 2; ++round) {
        spec.elevation_rad = phys::min_cue_elevation(table, s.cue_ball, start.aim_rad, b.env.table, b.env.params);
        const SolvedPot solved = solve_pot(s, start, spec, b.env, false, kSolveSims);
        b.budget.sims += solved.sims;
        if (!solved.found) return false;
        phys::CueStrike strike = solved.trial.strike;
        strike.elevation_rad = std::max(
            spec.elevation_rad, phys::min_cue_elevation(table, s.cue_ball, strike.aim_rad, b.env.table, b.env.params));
        if (judged(b, s, pot, strike, out)) return true;
        if (strike.elevation_rad == spec.elevation_rad) return false;
        start.aim_rad = strike.aim_rad;
    }
    return false;
}

Spec spec_of(const Level& level, int spin, double speed) {
    const Tip& tip = kTips[std::size_t(std::clamp(spin, 0, kTipCount - 1))];
    Spec spec;
    spec.speed_m_s = speed;
    spec.tip_x = tip.x * level.max_tip;
    spec.tip_y = tip.y * level.max_tip;
    return spec;
}

// The stand-in's stroke as it means it: no error of its own (the level's is added afterwards) and
// no concession. Without pots it plays only its break-off, safeties and escapes.
StandinShot standin_stroke(const FrameState& s, const Env& env, bool pots_too, const Rng& rng) {
    StandinParams sp = env.standin;
    sp.aim_error_rad = 0.0;
    sp.speed_error = 0.0;
    sp.concede_required_points = std::numeric_limits<int>::max();
    // Its test is "wider than": below zero even a dead straight pot is left out.
    if (!pots_too) sp.max_cut_rad = -1.0;
    Rng copy = rng;
    Rng own(copy.next_u64());
    return choose_shot(s, env.table, env.params, env.oracle, env.ref, sp, own);
}

int number_in(const nlohmann::json& why, const char* key) {
    const auto it = why.find(key);
    return it != why.end() && it->is_number_integer() ? it->get<int>() : 0;
}

nlohmann::json drawn_json(const Drawn& d) {
    return {{"aim_rad", d.aim_rad}, {"speed", d.speed}, {"tip_x", d.tip_x}, {"tip_y", d.tip_y}};
}

// The level's error added to `meant`, four draws. The error turns the aim, and the cue must clear
// what lies behind the cue ball along the aim as played: the plan's cue is raised to that, so the
// stroke played still differs from the one meant only by what was drawn.
void play(Stroke& out, const FrameState& s, const Level& level, const Pot* pot, const Env& env, Rng& rng,
          Drawn& drawn) {
    out.strike = with_error(out.meant, level, pot, env, rng, &drawn);
    const phys::TableState table = table_state(s, out.places_cue ? out.cue_pos : s.balls[std::size_t(s.cue_ball)].pos);
    const double least = phys::min_cue_elevation(table, s.cue_ball, out.strike.aim_rad, env.table, env.params);
    if (out.strike.elevation_rad < least) {
        out.meant.elevation_rad = least;
        out.strike.elevation_rad = least;
    }
}

void fill_from_standin(Stroke& out, const StandinShot& shot, const FrameState& s, const Level& level, const Env& env,
                       Rng& rng) {
    out.why = shot.why.is_object() ? shot.why : nlohmann::json::object();
    out.why["level"] = level.id;
    if (shot.decides) {
        out.decides = true;
        out.decision = shot.decision;
        return;
    }
    out.places_cue = shot.places_cue;
    out.cue_pos = shot.cue_pos;
    out.input = shot.input;
    out.meant = shot.strike;
    Drawn drawn;
    play(out, s, level, nullptr, env, rng, drawn);
    out.why["drawn"] = drawn_json(drawn);
    say_maximum(out.why, s, level);
}

// What a pot's why holds beyond the search's own findings.
nlohmann::json pot_why(const FrameState& s, const Level& level, const Pot& pot, const phys::CueStrike& meant) {
    nlohmann::json why = {{"level", level.id},
                          {"kind", "pot"},
                          {"target", label(s, pot.target)},
                          {"pocket", pot.pocket},
                          {"band", band(pot)},
                          {"cut_rad", pot.cut_rad},
                          {"spec",
                           {{"speed_m_s", meant.speed_m_s},
                            {"tip_x", meant.tip_x},
                            {"tip_y", meant.tip_y},
                            {"elevation_rad", meant.elevation_rad}}}};
    say_maximum(why, s, level);
    return why;
}

// A level with error (README "The search"): every candidate scored by its chance and what it
// wins, each score misread, the best taken.
class Weighing {
public:
    Weighing(const FrameState& s, const Level& level, const Env& env, Budget& budget, const Rng& rng)
        : s_(s), level_(level), env_(env), bench_{level, env, budget}, misreads_(seed_of(rng)) {}

    void run() {
        const std::vector<Pot> listed = pots(s_, env_, level_.max_cut_rad);
        listed_ = int(listed.size());
        const Range range = speed_range(level_.speeds);
        const int spins = std::min(level_.spins, kTipCount);
        for (const Pot& pot : listed) {
            const std::vector<double> speeds = ladder(pot, level_.speeds, range.from, range.to, env_);
            for (int spin = 0; spin < spins; ++spin) {
                for (const double speed : speeds) {
                    if (!bench_.budget.open()) return;
                    ++bench_.budget.steps;
                    // Drawn for every candidate, made or not, so the same steps misread the same way.
                    const double misread = misreads_.normal();
                    Made made;
                    if (!attempt(bench_, s_, pot, spec_of(level_, spin, speed), pot.aim_rad, made)) continue;
                    weigh(pot, made, misread);
                }
            }
        }
    }

    bool found() const { return best_ >= 0; }
    int listed() const { return listed_; }
    int candidates() const { return int(scored_.size()); }
    // As it reads it, the chance of the pot it would play.
    double chance() const { return scored_[std::size_t(best_)].chance; }

    void fill(Stroke& out, Rng& rng) const {
        const Scored& c = scored_[std::size_t(best_)];
        out.places_cue = s_.cue_in_hand;
        out.cue_pos = c.pot.cue;
        out.input = c.pot.input;
        out.meant = c.strike;
        Drawn drawn;
        play(out, s_, level_, &c.pot, env_, rng, drawn);
        out.why = pot_why(s_, level_, c.pot, out.meant);
        out.why["chance"] = c.chance;
        out.why["worth"] = c.score;
        out.why["leave"] = {{"cue", {c.cue_rest.x, c.cue_rest.y}}, {"next", c.next}, {"chance", c.next_chance}};
        out.why["plan"] = nlohmann::json::array();
        out.why["others"] = others();
        out.why["drawn"] = drawn_json(drawn);
    }

private:
    struct Scored {
        Pot pot;
        phys::CueStrike strike;
        double chance = 0.0;
        double score = 0.0;
        Vec2 cue_rest;
        std::string next;
        double next_chance = 0.0;
    };

    static std::uint64_t seed_of(const Rng& rng) {
        Rng copy = rng;
        // Apart from the stand-in's own stream, which takes the same copy's first number.
        return copy.next_u64() ^ 0x9e3779b97f4a7c15ull;
    }

    void weigh(const Pot& pot, const Made& made, double misread) {
        Scored c;
        c.pot = pot;
        c.strike = made.strike;
        c.cue_rest = made.cue_rest;
        const Spec spec{made.strike.speed_m_s, made.strike.tip_x, made.strike.tip_y, made.strike.elevation_rad};
        c.chance = std::clamp(pot_chance(level_, pot, spec, pot.margin_rad) * (1.0 + level_.judgement_sigma * misread),
                              0.0, 1.0);
        double leave = 0.0;
        if (made.goes_on) {
            const std::vector<Pot> next = pots(made.post, env_, level_.max_cut_rad);
            const Read read = read_pots(next, level_, env_);
            leave = read.worth;
            if (read.best >= 0) {
                c.next = label(made.post, next[std::size_t(read.best)].target);
                c.next_chance = read.chance;
            }
        }
        const double w = level_.position_weight;
        c.score = c.chance * (pot.value + kBreakOn * (1.0 - w) + w * leave);
        scored_.push_back(std::move(c));
        if (best_ < 0 || scored_.back().score > scored_[std::size_t(best_)].score) best_ = int(scored_.size()) - 1;
    }

    // The best of each other pot, up to three.
    nlohmann::json others() const {
        std::vector<int> order;
        for (int i = 0; i < int(scored_.size()); ++i) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return scored_[std::size_t(a)].score > scored_[std::size_t(b)].score;
        });
        const Scored& chosen = scored_[std::size_t(best_)];
        std::vector<std::pair<int, int>> seen = {{chosen.pot.target, chosen.pot.pocket}};
        nlohmann::json out = nlohmann::json::array();
        for (const int i : order) {
            const Scored& c = scored_[std::size_t(i)];
            const std::pair<int, int> key{c.pot.target, c.pot.pocket};
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            out.push_back({{"target", label(s_, c.pot.target)}, {"pocket", c.pot.pocket}, {"worth", c.score}});
            if (out.size() == 3) break;
        }
        return out;
    }

    const FrameState& s_;
    const Level& level_;
    const Env& env_;
    Bench bench_;
    Rng misreads_;
    std::vector<Scored> scored_;
    int listed_ = 0;
    int best_ = -1;
};

// The 147 (README "The 147", "The search"): the pots of the ball it needs, each read by where it
// leaves the table, then the best looked into a stroke further, stage by stage.
class Planning {
public:
    Planning(const FrameState& s, const Level& level, const Env& env, Budget& budget)
        : level_(level), env_(env), bench_{level, env, budget} {
        nodes_.emplace_back();
        nodes_.back().s = s;
    }

    void run() {
        const double grid = kGridShare * level_.max_sims;
        if (!expand(0, grid)) return;
        if (!refine()) return;
        const int depth = std::min(level_.depth, kMaxDepth);
        for (int d = 1; d <= depth; ++d) {
            const std::vector<int> list = d == 1 ? nodes_[0].beam : finished(d - 1);
            int done = 0;
            bool whole = false;
            for (const int c : list) {
                if (done >= kBeam[d] && (whole || done >= 2 * kBeam[d])) break;
                if (!deepen(0, c, d)) break;
                ++done;
                whole = whole || nodes_[0].cands[std::size_t(c)].whole[d];
            }
            if (done == 0) break;
            stage_ = d;
            if (bench_.budget.halted()) break;
        }
    }

    bool found() const { return !nodes_[0].cands.empty(); }
    int candidates() const { return int(nodes_[0].cands.size()); }

    void fill(Stroke& out, Rng& rng) const {
        const Node& root = nodes_[0];
        const std::vector<int> order = finished(stage_);
        const Cand& c = root.cands[std::size_t(order.front())];
        const Pot& pot = root.pots[std::size_t(c.pot)];
        out.places_cue = root.s.cue_in_hand;
        out.cue_pos = pot.cue;
        out.input = pot.input;
        out.meant = c.strike;
        Drawn drawn;
        play(out, root.s, level_, &pot, env_, rng, drawn);
        out.why = pot_why(root.s, level_, pot, out.meant);
        const Spec spec{c.strike.speed_m_s, c.strike.tip_x, c.strike.tip_y, c.strike.elevation_rad};
        out.why["chance"] = pot_chance(level_, pot, spec, pot.margin_rad);
        out.why["worth"] = c.value[stage_];
        out.why["leave"] = {{"cue", {c.cue_rest.x, c.cue_rest.y}},
                            {"next", label(c.post, c.leaf.next)},
                            {"chance", c.leaf.chance}};

        nlohmann::json plan = nlohmann::json::array();
        const Cand* at = &c;
        for (int d = stage_; d >= 1 && at->best[d] >= 0; --d) {
            const Node& below = nodes_[std::size_t(at->node)];
            const Cand& next = below.cands[std::size_t(at->best[d])];
            const Pot& its = below.pots[std::size_t(next.pot)];
            plan.push_back({{"target", label(below.s, its.target)}, {"pocket", its.pocket}});
            at = &next;
        }
        out.depth = int(plan.size());
        out.why["plan"] = std::move(plan);

        std::vector<std::pair<int, int>> seen = {{pot.target, pot.pocket}};
        nlohmann::json others = nlohmann::json::array();
        for (const int i : order) {
            const Cand& other = root.cands[std::size_t(i)];
            const Pot& its = root.pots[std::size_t(other.pot)];
            const std::pair<int, int> key{its.target, its.pocket};
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            others.push_back(
                {{"target", label(root.s, its.target)}, {"pocket", its.pocket}, {"worth", other.value[stage_]}});
            if (others.size() == 3) break;
        }
        out.why["others"] = std::move(others);
        out.why["drawn"] = drawn_json(drawn);
    }

private:
    struct Try {
        int pot = 0;
        int spin = 0;
        int rung = -1;  // the speed's place on the pot's ladder; -1 for one between
        double speed = 0.0;
    };

    struct Cand {
        int pot = 0;
        int spin = 0;
        double speed = 0.0;
        phys::CueStrike strike;
        Vec2 cue_rest;
        FrameState post;
        bool goes_on = false;
        Outlook leaf;
        // The points it pots, less what the stroke costs: its pace and spin, and kLost when it
        // ends a maximum that was on.
        double own = 0.0;
        int node = -1;  // the position it leaves, once looked into
        // By how many strokes beyond it were looked into: what its plan is worth, the candidate
        // of its node that plan plays next (-1 none), and whether the plan runs the whole way.
        double value[kMaxDepth + 1] = {};
        int best[kMaxDepth + 1] = {-1, -1, -1, -1};
        bool whole[kMaxDepth + 1] = {true, false, false, false};
        int reached = 0;
    };

    struct Node {
        FrameState s;
        int depth = 0;
        bool on = false;  // the maximum is on here: only the ball it needs is played
        bool listed = false;
        bool done = false;
        std::vector<Pot> pots;
        // Pots from here on are tried only if none before them was made: with the maximum gone a
        // black after a red comes before any other colour.
        int second = 0;
        bool second_queued = false;
        std::vector<std::vector<double>> speeds;  // each pot's ladder
        std::vector<Try> todo;
        std::size_t next = 0;
        int queued = 0;         // pots queued so far
        int spins = 0;          // spins they are tried with
        int between = 0;        // passes between the rungs queued
        std::vector<int> made;  // candidates made, by pot
        // Aims found, to start the solver near: by pot and spin at the last speed tried, and by
        // pot and rung at centre ball.
        std::vector<std::vector<double>> last_aim;
        std::vector<std::vector<double>> centre_aim;
        std::vector<Cand> cands;
        std::vector<int> beam;  // the candidates by value, one for each leave
    };

    bool keeps(const Node& n, const Made& made) const {
        if (!n.on) return true;
        if (made.post.over) return n.s.break_points + made.points == 147;
        return made.goes_on && maximum_on(made.post);
    }

    void queue(Node& n, int from, int to) {
        std::vector<int> rungs;
        const int all = level_.speeds;
        const int few = n.depth == 0 || to - from <= 2 ? all : std::min(all, kDeepSpeeds[n.depth]);
        for (int i = 0; i < few; ++i) rungs.push_back(few > 1 ? int(std::lround(double(i) * (all - 1) / (few - 1))) : 0);
        int spins = std::min(level_.spins, kTipCount);
        if (n.depth > 0) {
            const int room = kDeepTries[n.depth] / std::max(1, (to - from) * few);
            spins = std::clamp(room, std::min(kDeepSpins, spins), spins);
        }
        for (int spin = 0; spin < spins; ++spin) {
            for (int pot = from; pot < to; ++pot) {
                const std::vector<double>& speeds = n.speeds[std::size_t(pot)];
                for (const int rung : rungs) n.todo.push_back({pot, spin, rung, speeds[std::size_t(rung)]});
            }
        }
        n.queued = to;
        n.spins = spins;
    }

    // The speeds between the rungs, for every pot of the first position that has been made: the
    // halves on the first pass, the quarters on the second.
    void queue_between(Node& n) {
        const std::vector<double> shares = n.between == 0 ? std::vector<double>{0.5} : std::vector<double>{0.25, 0.75};
        ++n.between;
        for (int spin = 0; spin < n.spins; ++spin) {
            for (int pot = 0; pot < n.queued; ++pot) {
                if (n.made[std::size_t(pot)] == 0) continue;
                const std::vector<double>& speeds = n.speeds[std::size_t(pot)];
                for (std::size_t rung = 0; rung + 1 < speeds.size(); ++rung) {
                    for (const double share : shares) {
                        n.todo.push_back(
                            {pot, spin, -1, speeds[rung] * std::pow(speeds[rung + 1] / speeds[rung], share)});
                    }
                }
            }
        }
    }

    void list(Node& n) {
        n.listed = true;
        n.on = level_.maximum && maximum_on(n.s);
        const Ball needed = maximum_next(n.s);
        const bool black_first = !n.on && n.s.on.kind == OnKind::any_colour;
        std::vector<Pot> later;
        const int most = n.depth == 0 ? std::numeric_limits<int>::max() : kDeepPots;
        for (const Pot& pot : pots(n.s, env_, level_.max_cut_rad)) {
            const Ball ball = n.s.balls[std::size_t(pot.target)].ball;
            if (n.on && ball != needed) continue;
            if (black_first && ball != Ball::black) {
                if (int(later.size()) < most) later.push_back(pot);
            } else if (int(n.pots.size()) < most) {
                n.pots.push_back(pot);
            }
        }
        n.second = int(n.pots.size());
        n.pots.insert(n.pots.end(), later.begin(), later.end());

        const Range range = speed_range(level_.speeds);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        for (const Pot& pot : n.pots) {
            n.speeds.push_back(ladder(pot, level_.speeds, range.from, range.to, env_));
            n.last_aim.emplace_back(std::size_t(kTipCount), nan);
            n.centre_aim.emplace_back(n.speeds.back().size(), nan);
        }
        n.made.assign(n.pots.size(), 0);
        queue(n, 0, n.second);
    }

    // One step: the candidate tried and, when it is made, read. Its place in the node's
    // candidates, or -1.
    int step(Node& n, const Try& t) {
        ++bench_.budget.steps;
        const Pot& pot = n.pots[std::size_t(t.pot)];
        double& last = n.last_aim[std::size_t(t.pot)][std::size_t(t.spin)];
        double from = pot.aim_rad;
        if (!std::isnan(last)) {
            from = last;
        } else if (t.rung >= 0 && !std::isnan(n.centre_aim[std::size_t(t.pot)][std::size_t(t.rung)])) {
            from = n.centre_aim[std::size_t(t.pot)][std::size_t(t.rung)];
        }
        Made made;
        if (!attempt(bench_, n.s, pot, spec_of(level_, t.spin, t.speed), from, made)) return -1;
        last = made.strike.aim_rad;
        if (t.spin == 0 && t.rung >= 0) n.centre_aim[std::size_t(t.pot)][std::size_t(t.rung)] = made.strike.aim_rad;
        ++n.made[std::size_t(t.pot)];

        Cand c;
        c.pot = t.pot;
        c.spin = t.spin;
        c.speed = t.speed;
        c.strike = made.strike;
        c.cue_rest = made.cue_rest;
        c.goes_on = made.goes_on;
        const Tip& tip = kTips[std::size_t(t.spin)];
        const double share = pace(pot, t.speed, env_);
        c.own = made.points - (keeps(n, made) ? 0.0 : kLost) - kPaceCost * share * share -
                kSpinCost * (tip.x * tip.x + tip.y * tip.y);
        if (made.goes_on) c.leaf = read_table(made.post, pots(made.post, env_, level_.max_cut_rad), env_);
        c.value[0] = c.own + c.leaf.worth;
        c.post = std::move(made.post);
        n.cands.push_back(std::move(c));
        return int(n.cands.size()) - 1;
    }

    void rank(Node& n) {
        std::vector<int> order;
        for (int i = 0; i < int(n.cands.size()); ++i) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return n.cands[std::size_t(a)].value[0] > n.cands[std::size_t(b)].value[0];
        });
        n.beam.clear();
        for (const int i : order) {
            const Cand& c = n.cands[std::size_t(i)];
            const bool seen = std::any_of(n.beam.begin(), n.beam.end(), [&](int k) {
                const Cand& other = n.cands[std::size_t(k)];
                return n.pots[std::size_t(other.pot)].target == n.pots[std::size_t(c.pot)].target &&
                       other.goes_on == c.goes_on && distance(other.cue_rest, c.cue_rest) < kSameLeave_m;
            });
            if (!seen) n.beam.push_back(i);
        }
    }

    // Every candidate of the position tried, or as many as `sims_until` allows. False when the
    // budget stopped it; what was made by then is ranked all the same.
    bool expand(int at, double sims_until = kInf) {
        Node& n = nodes_[std::size_t(at)];
        if (!n.listed) list(n);
        bool stopped = false;
        while (!n.done) {
            if (n.next == n.todo.size()) {
                if (n.cands.empty() && !n.second_queued && n.second < int(n.pots.size())) {
                    n.second_queued = true;
                    queue(n, n.second, int(n.pots.size()));
                    continue;
                }
                if (n.depth == 0 && n.between < kBetweenPasses && bench_.budget.sims < sims_until) {
                    const std::size_t before = n.todo.size();
                    queue_between(n);
                    if (n.todo.size() > before) continue;
                }
                n.done = true;
            } else if (bench_.budget.sims >= sims_until) {
                n.done = true;
            } else if (!bench_.budget.open()) {
                stopped = true;
                break;
            } else {
                step(n, n.todo[n.next++]);
            }
        }
        rank(n);
        return !stopped;
    }

    // Between the grid's speeds, for the best pots and spins of the first position.
    bool refine() {
        Node& n = nodes_[0];
        std::vector<int> picks;
        for (const int i : n.beam) {
            const Cand& c = n.cands[std::size_t(i)];
            const bool seen = std::any_of(picks.begin(), picks.end(), [&](int k) {
                return n.cands[std::size_t(k)].pot == c.pot && n.cands[std::size_t(k)].spin == c.spin;
            });
            if (!seen) picks.push_back(i);
            if (int(picks.size()) == kRefined) break;
        }
        bool stopped = false;
        for (const int pick : picks) {
            const std::vector<double>& speeds = n.speeds[std::size_t(n.cands[std::size_t(pick)].pot)];
            if (speeds.size() < 2 || !(speeds.back() > speeds.front())) continue;
            const double rung =
                std::pow(speeds.back() / speeds.front(), 1.0 / double(speeds.size() - 1) / double(1 << n.between));
            int best = pick;
            for (const double share : kRefineSteps) {
                const double middle = n.cands[std::size_t(best)].speed;
                for (const double way : {share, -share}) {
                    const double speed = middle * std::pow(rung, way);
                    if (speed < speeds.front() || speed > speeds.back()) continue;
                    if (!bench_.budget.open()) {
                        stopped = true;
                        break;
                    }
                    const Cand& from = n.cands[std::size_t(best)];
                    const int made = step(n, {from.pot, from.spin, -1, speed});
                    if (made >= 0 && n.cands[std::size_t(made)].value[0] > n.cands[std::size_t(best)].value[0]) {
                        best = made;
                    }
                }
                if (stopped) break;
            }
            if (stopped) break;
        }
        rank(n);
        return !stopped;
    }

    // The first position's candidates looked into at least `d` strokes further, best first.
    std::vector<int> finished(int d) const {
        const Node& root = nodes_[0];
        std::vector<int> out;
        for (int i = 0; i < int(root.cands.size()); ++i) {
            if (root.cands[std::size_t(i)].reached >= d) out.push_back(i);
        }
        std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
            return root.cands[std::size_t(a)].value[d] > root.cands[std::size_t(b)].value[d];
        });
        return out;
    }

    // What the candidate's plan is worth looked `d` strokes beyond it. False when the budget
    // stopped it short.
    bool deepen(int at, int which, int d) {
        Cand& c = nodes_[std::size_t(at)].cands[std::size_t(which)];
        if (c.reached >= d) return true;
        if (d > 1 && !deepen(at, which, d - 1)) return false;
        if (!c.goes_on) {
            c.value[d] = c.own;
            c.whole[d] = true;
            c.reached = d;
            return true;
        }
        if (c.node < 0) {
            c.node = int(nodes_.size());
            nodes_.emplace_back();
            nodes_.back().s = c.post;
            nodes_.back().depth = nodes_[std::size_t(at)].depth + 1;
        }
        if (!nodes_[std::size_t(c.node)].done && !expand(c.node)) return false;
        Node& below = nodes_[std::size_t(c.node)];

        double best = -kInf;
        int pick = -1;
        if (d == 1) {
            for (int i = 0; i < int(below.cands.size()); ++i) {
                if (below.cands[std::size_t(i)].value[0] > best) {
                    best = below.cands[std::size_t(i)].value[0];
                    pick = i;
                }
            }
        } else {
            int done = 0;
            bool whole = false;
            for (const int i : below.beam) {
                if (done >= kFollowed && (whole || done >= 2 * kFollowed)) break;
                if (!deepen(c.node, i, d - 1)) return false;
                ++done;
                const Cand& next = below.cands[std::size_t(i)];
                whole = whole || next.whole[d - 1];
                if (next.value[d - 1] > best) {
                    best = next.value[d - 1];
                    pick = i;
                }
            }
        }
        c.value[d] = c.own + (pick >= 0 ? best : 0.0);
        c.best[d] = pick;
        c.whole[d] = pick >= 0 && below.cands[std::size_t(pick)].whole[d - 1];
        c.reached = d;
        return true;
    }

    const Level& level_;
    const Env& env_;
    Bench bench_;
    // A deque: a position looked into adds positions, and those in hand must stay where they are.
    std::deque<Node> nodes_;
    int stage_ = 0;
};

// Enough to make the decision again from the one line: app.error has no fields of its own for it.
void failure_logged(const char* what, const FrameState& s, const Level& level, const Rng& rng, const Think& think,
                    const Budget* budget) {
    std::string frame;
    try {
        frame = rules::frame_json(s).dump();
    } catch (const std::exception&) {
        frame = "unreadable";
    }
    Rng copy = rng;
    std::string message = std::string(what) + "; level " + level.id + "; rng next " + std::to_string(copy.next_u64()) +
                          "; think max_sims " + std::to_string(think.max_sims) + " max_steps " +
                          std::to_string(think.max_steps);
    if (budget != nullptr) {
        message += "; steps " + std::to_string(budget->steps) + " sims " + std::to_string(budget->sims);
    }
    message += "; frame " + frame;
    nlohmann::json data = {{"kind", "exception"}, {"function", "opponent::choose_stroke"}, {"message", message}};
    const nlohmann::json& context = rules::scoped_log_context();
    for (const char* key : {"match_id", "frame"}) {
        if (context.contains(key)) data[key] = context[key];
    }
    log::error("app", "app.error", std::move(data));
}

void count(Stroke& out, const Budget& budget, int candidates) {
    out.sims = budget.sims;
    out.steps = budget.steps;
    out.stopped = budget.stopped;
    out.candidates = candidates;
}

Stroke choose(const FrameState& s, const Level& level, const Env& env, Rng& rng, Budget& budget) {
    Stroke out;
    const bool playing_for_it = level.maximum && maximum_on(s);
    if (s.striker == 0 || s.striker == 1) {
        const std::vector<DecisionKind> open = rules::options(s, s.striker);
        // A maximum still on is played out to the last black, however far ahead it is.
        if (!playing_for_it && std::find(open.begin(), open.end(), DecisionKind::claim_frame) != open.end()) {
            out.decides = true;
            out.decision = DecisionKind::claim_frame;
            out.why = {{"level", level.id}, {"kind", "claim"}};
            return out;
        }
    }

    const auto from_standin = [&](bool pots_too) {
        const StandinShot shot = standin_stroke(s, env, pots_too, rng);
        budget.sims += number_in(shot.why, "sims");
        const int weighed = number_in(shot.why, "candidates");
        fill_from_standin(out, shot, s, level, env, rng);
        return weighed;
    };

    if (budget.cancelled()) {
        budget.halt("cancelled");
        // The 147 plays what it planned: a pot the stand-in only hopes for is not its stroke.
        const int weighed = from_standin(!level.maximum);
        count(out, budget, weighed);
        out.fallback = "cancelled before the first step";
        return out;
    }

    // The break-off is the stand-in's: it knows the pack when it sees it.
    if (s.cue_in_hand && s.on.kind == OnKind::red && reds_left(s) == 15) {
        const StandinShot shot = standin_stroke(s, env, false, rng);
        budget.sims += number_in(shot.why, "sims");
        if (!shot.decides && shot.why.is_object() && shot.why.value("kind", std::string()) == "break") {
            fill_from_standin(out, shot, s, level, env, rng);
            count(out, budget, number_in(shot.why, "candidates"));
            return out;
        }
    }

    if (level.maximum) {
        Planning planning(s, level, env, budget);
        planning.run();
        if (planning.found()) {
            planning.fill(out, rng);
            count(out, budget, planning.candidates());
            return out;
        }
        const int weighed = from_standin(false);
        count(out, budget, weighed);
        return out;
    }

    Weighing weighing(s, level, env, budget, rng);
    weighing.run();
    if (weighing.found() && weighing.chance() >= level.safety_below) {
        weighing.fill(out, rng);
        count(out, budget, weighing.candidates());
        return out;
    }
    const int weighed = from_standin(false);
    // Why it did not pot: the pots the table offered, how many strokes made one, and the chance it
    // gave the best when it had one.
    out.why["listed"] = weighing.listed();
    out.why["pots"] = weighing.candidates();
    if (weighing.found()) out.why["declined"] = weighing.chance();
    count(out, budget, std::max(weighed, weighing.candidates()));
    return out;
}

}  // namespace

Stroke choose_stroke(const rules::FrameState& s, const Level& level, const Env& env, Rng& rng, const Think& think) {
    Budget budget(level, think);
    try {
        return choose(s, level, env, rng, budget);
    } catch (const std::exception& e) {
        failure_logged(e.what(), s, level, rng, think, &budget);
        Stroke out;
        // choose_shot catches what it throws itself and answers with its plainest stroke.
        const StandinShot shot = standin_stroke(s, env, !level.maximum, rng);
        out.why = shot.why.is_object() ? shot.why : nlohmann::json::object();
        out.why["level"] = level.id;
        out.decides = shot.decides;
        out.decision = shot.decision;
        out.places_cue = shot.places_cue;
        out.cue_pos = shot.cue_pos;
        out.input = shot.input;
        out.meant = shot.strike;
        out.strike = shot.strike;
        out.sims = budget.sims;
        out.steps = budget.steps;
        out.stopped = budget.stopped;
        out.fallback = std::string("exception: ") + e.what();
        return out;
    }
}

}  // namespace ofs::opponent
