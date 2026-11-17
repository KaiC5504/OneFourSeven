#include "ofs/opponent/solve.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "ofs/log.hpp"
#include "ofs/physics/simulator.hpp"
#include "ofs/rules/rules_log.hpp"

namespace ofs::opponent {

namespace {

using rules::Vec2;

constexpr double kPi = 3.14159265358979323846;
constexpr double kR = table::kBallRadius;
// Step 2 stops once the object ball leaves this close to the wanted line: a hundredth of the
// narrowest pocket's room seen from across the table.
constexpr double kConverged_rad = 1.0e-4;
constexpr int kSecantSteps = 8;
constexpr int kAcquireSteps = 6;
constexpr int kNudges = 4;
constexpr int kWidenSteps = 8;
constexpr int kBisections = 6;
// A cut this close to a quarter turn is no pot; the gain's 1 / cos is held here.
constexpr double kMinCos = 0.1;

double wrap(double a) { return std::remainder(a, 2.0 * kPi); }

bool unclean(const std::string& kind) {
    return kind == "event_guard" || kind == "zero_time_chain" || kind == "time_cap" || kind == "nan";
}

nlohmann::json strike_json(const phys::CueStrike& s) {
    return {{"aim_rad", s.aim_rad},
            {"speed_m_s", s.speed_m_s},
            {"tip_x", s.tip_x},
            {"tip_y", s.tip_y},
            {"elevation_rad", s.elevation_rad}};
}

nlohmann::json balls_json(const rules::Position& p, Vec2 cue) {
    nlohmann::json out = nlohmann::json::array();
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        const rules::BallOnTable& b = p.balls[i];
        const Vec2 at = int(i) == p.cue_ball ? cue : b.pos;
        out.push_back({std::string(table::ball_name(b.ball)), b.index, at.x, at.y});
    }
    return out;
}

thread_local TrialFailures* g_failures = nullptr;

// app.error takes no fields of its own for the stroke, so it travels in the message: enough to
// simulate the stroke again from the log line alone.
void failure_logged(const char* function, const char* kind, const std::string& what, const rules::Position& p,
                    Vec2 cue, const phys::CueStrike& strike) {
    nlohmann::json data = {{"kind", kind}, {"function", function}};
    if (g_failures != nullptr && g_failures->count() > 0) {
        // Counted only: the message is not worth building.
        g_failures->note(std::move(data));
        return;
    }
    data["message"] = what + "; cue_ball " + std::to_string(p.cue_ball) + "; strike " + strike_json(strike).dump() +
                      "; balls " + balls_json(p, cue).dump();
    const nlohmann::json& context = rules::scoped_log_context();
    for (const char* key : {"match_id", "frame"}) {
        if (context.contains(key)) data[key] = context[key];
    }
    if (g_failures != nullptr) {
        g_failures->note(std::move(data));
    } else {
        log::error("app", "app.error", std::move(data));
    }
}

void read_record(const phys::ShotRecord& r, int cue_ball, Trial& out) {
    out.clean = true;
    for (const phys::Anomaly& a : r.anomalies) out.clean = out.clean && !unclean(a.kind);
    out.t_end = r.t_end;
    out.sim_us = r.sim_us;
    out.rest.reserve(r.final_state.balls.size());
    for (const phys::BallState& b : r.final_state.balls) out.rest.push_back({b.r.x, b.r.y});

    std::size_t first = r.events.size();
    for (std::size_t i = 0; i < r.events.size(); ++i) {
        const phys::ShotEvent& e = r.events[i];
        if (e.kind == phys::EventKind::guard) out.clean = false;
        if (e.kind == phys::EventKind::pocket) {
            if (e.a == cue_ball) {
                out.cue_potted = true;
            } else {
                out.potted.push_back(e.a);
            }
        }
        if (e.kind == phys::EventKind::ball_ball && (e.a == cue_ball || e.b == cue_ball)) {
            const int other = e.a == cue_ball ? e.b : e.a;
            if (first == r.events.size()) {
                first = i;
                out.first_hit = other;
            } else if (other != out.first_hit) {
                out.cannon = true;
            }
        }
        const bool cushion = e.kind == phys::EventKind::ball_line || e.kind == phys::EventKind::ball_arc;
        if (cushion && e.a == cue_ball && first < i) ++out.cue_cushions;
    }
}

// One aim tried, with what solve_pot reads from it.
struct Tried {
    double aim = 0.0;
    Trial trial;
    bool pots = false;
    bool readable = false;  // the cue ball met the target first and its departure was read
    double error = 0.0;     // departure minus wanted, when readable
    std::shared_ptr<const phys::ShotRecord> record;  // kept when it pots
};

class Solver {
public:
    Solver(const rules::Position& p, const Pot& pot, const Spec& spec, const Env& env, int max_sims)
        : p_(p), pot_(pot), spec_(spec), env_(env), max_sims_(max_sims) {
        const Vec2 target = p.balls[std::size_t(pot.target)].pos;
        wanted_ = std::atan2(pot.aim_point.y - target.y, pot.aim_point.x - target.x);
        half_ball_ = 0.5 * std::asin(std::min(1.0, kR / std::max(pot.cue_travel_m, 2.0 * kR)));
    }

    SolvedPot solve(bool measure_window) {
        Tried start;
        if (!acquire(start)) return finish(start);
        Tried best = converge(start);
        if (!best.pots && !nudge(best)) return finish(best);
        if (measure_window) measure(best);
        return finish(best);
    }

private:
    bool spent() const { return sims_ >= max_sims_; }

    Tried run(double aim) {
        Tried t;
        t.aim = wrap(aim);
        phys::CueStrike strike;
        strike.aim_rad = t.aim;
        strike.speed_m_s = spec_.speed_m_s;
        strike.tip_x = spec_.tip_x;
        strike.tip_y = spec_.tip_y;
        strike.elevation_rad = spec_.elevation_rad;
        phys::ShotRecord record;
        t.trial = try_strike(p_, pot_.cue, strike, env_, &record);
        ++sims_;
        const Trial& r = t.trial;
        const bool dropped = std::find(r.potted.begin(), r.potted.end(), pot_.target) != r.potted.end() &&
                             pot_.target < int(record.final_state.balls.size()) &&
                             record.final_state.balls[std::size_t(pot_.target)].pocket == pot_.pocket;
        t.pots = r.clean && r.first_hit == pot_.target && dropped && !r.cue_potted;
        if (r.first_hit == pot_.target) read_departure(record, t);
        if (t.pots) t.record = std::make_shared<const phys::ShotRecord>(std::move(record));
        if (t.pots && (!have_success_ || better(t, success_))) {
            success_ = t;
            have_success_ = true;
        }
        return t;
    }

    static bool better(const Tried& a, const Tried& b) {
        if (a.readable != b.readable) return a.readable;
        return std::abs(a.error) < std::abs(b.error);
    }

    void read_departure(const phys::ShotRecord& record, Tried& t) const {
        const int cue_ball = p_.cue_ball;
        double hit = -1.0;
        for (const phys::ShotEvent& e : record.events) {
            if (e.kind != phys::EventKind::ball_ball) continue;
            if ((e.a == cue_ball && e.b == pot_.target) || (e.b == cue_ball && e.a == pot_.target)) {
                hit = e.t;
                break;
            }
        }
        if (hit < 0.0 || pot_.target >= int(record.segments.size())) return;
        const phys::Segment* leaving = nullptr;
        for (const phys::Segment& s : record.segments[std::size_t(pot_.target)]) {
            if (s.t0 == hit) leaving = &s;
        }
        if (leaving == nullptr) return;
        const double vx = leaving->start.v.x;
        const double vy = leaving->start.v.y;
        if (!(std::hypot(vx, vy) > 0.0)) return;
        t.error = wrap(std::atan2(vy, vx) - wanted_);
        t.readable = std::isfinite(t.error);
    }

    // Step 1: an aim at which the cue ball meets the target first.
    bool acquire(Tried& out) {
        if (spent()) return false;
        out = run(pot_.aim_rad);
        if (out.readable) return true;
        const Tried first = out;
        for (int k = 1; k <= kAcquireSteps; ++k) {
            for (double sign : {1.0, -1.0}) {
                if (spent()) {
                    out = first;
                    return false;
                }
                const Tried t = run(pot_.aim_rad + sign * k * half_ball_);
                if (t.readable) {
                    out = t;
                    return true;
                }
            }
        }
        out = first;
        return false;
    }

    // Departure per radian of aim from geometry: turning the aim anticlockwise moves the cue ball's
    // line left of the object ball, so the contact is on the object ball's left and it leaves
    // clockwise of before; the offset changes the cut by travel / (2R cos cut) per radian. The
    // aim's own turn adds 1.
    double geometric_gain() const {
        return 1.0 - pot_.cue_travel_m / (2.0 * kR * std::max(std::cos(pot_.cut_rad), kMinCos));
    }

    double limited(double step) const { return std::clamp(step, -half_ball_, half_ball_); }

    // Step 2: secant steps on the departure error from `start`; the readable aim with the least
    // error. An aim that loses the target is halved back towards the last one that had it.
    Tried converge(Tried start) {
        Tried best = start;
        Tried last = start;
        double next = start.aim + limited(-start.error / geometric_gain());
        for (int i = 0; i < kSecantSteps && std::abs(best.error) >= kConverged_rad; ++i) {
            if (spent()) break;
            const Tried t = run(next);
            if (!t.readable) {
                next = last.aim + 0.5 * wrap(t.aim - last.aim);
                continue;
            }
            if (better(t, best)) best = t;
            const Tried before = last;
            last = t;
            double gain = geometric_gain();
            const double da = wrap(last.aim - before.aim);
            if (da != 0.0) {
                const double measured = (last.error - before.error) / da;
                if (std::isfinite(measured) && measured != 0.0) gain = measured;
            }
            next = last.aim + limited(-last.error / gain);
        }
        return best;
    }

    // Step 3: a converged aim that stays out (a rattle, a jaw that refuses the line) is moved by
    // fractions of the margin, nearest first.
    bool nudge(Tried& best) {
        const double step = std::max(pot_.margin_rad, 1.0e-4) / 2.0;
        for (int j = 1; j <= kNudges; ++j) {
            for (double sign : {1.0, -1.0}) {
                if (spent()) return false;
                const Tried t = run(best.aim + sign * j * step);
                if (t.pots) {
                    best = t;
                    return true;
                }
            }
        }
        return false;
    }

    // How far from `from` the aim can go towards `sign` and still pot; `complete` false when the
    // budget ran out first, the answer then being the last offset that potted.
    double edge(double from, double sign, bool& complete) {
        complete = false;
        double ok = 0.0;
        double bad = -1.0;
        double offset = std::max(pot_.margin_rad, 2.0e-4) / 2.0;
        for (int i = 0; i < kWidenSteps; ++i) {
            if (spent()) return ok;
            if (!run(from + sign * offset).pots) {
                bad = offset;
                break;
            }
            ok = offset;
            offset *= 2.0;
        }
        if (bad < 0.0) {
            complete = true;
            return ok;
        }
        for (int i = 0; i < kBisections; ++i) {
            if (spent()) return ok;
            const double mid = 0.5 * (ok + bad);
            if (run(from + sign * mid).pots) {
                ok = mid;
            } else {
                bad = mid;
            }
        }
        complete = true;
        return ok;
    }

    // Step 4: the edges either side; the aim moved to their middle when that pots too.
    void measure(Tried& best) {
        bool left_done = false;
        bool right_done = false;
        const double left = edge(best.aim, 1.0, left_done);
        const double right = left_done ? edge(best.aim, -1.0, right_done) : 0.0;
        window_ = std::min(left, right);
        if (!left_done || !right_done || spent()) return;
        const double shift = 0.5 * (left - right);
        if (shift == 0.0) return;
        const Tried middle = run(best.aim + shift);
        if (middle.pots) {
            best = middle;
            window_ = 0.5 * (left + right);
        }
    }

    SolvedPot finish(const Tried& reached) const {
        SolvedPot out;
        out.sims = sims_;
        out.window_rad = window_;
        if (reached.pots) {
            out.found = true;
            out.trial = reached.trial;
            out.record = reached.record;
        } else if (have_success_) {
            out.found = true;
            out.trial = success_.trial;
            out.record = success_.record;
            out.window_rad = 0.0;
        } else {
            out.trial = reached.trial;
        }
        return out;
    }

    const rules::Position& p_;
    const Pot& pot_;
    const Spec& spec_;
    const Env& env_;
    const int max_sims_;
    double wanted_ = 0.0;
    double half_ball_ = 0.0;
    int sims_ = 0;
    double window_ = 0.0;
    bool have_success_ = false;
    Tried success_;
};

}  // namespace

TrialFailures::TrialFailures(nlohmann::json fields) : fields_(std::move(fields)), outer_(g_failures) {
    g_failures = this;
}

TrialFailures::~TrialFailures() {
    g_failures = outer_;
    if (count_ <= 1) return;
    try {
        nlohmann::json data = {{"kind", "repeated"},
                               {"function", "opponent::choose_stroke"},
                               {"message", "trial simulations that failed in one decision after the first logged"},
                               {"more", count_ - 1}};
        const nlohmann::json& context = rules::scoped_log_context();
        for (const char* key : {"match_id", "frame"}) {
            if (context.contains(key)) data[key] = context[key];
        }
        for (auto it = fields_.begin(); it != fields_.end(); ++it) data[it.key()] = it.value();
        log::error("app", "app.error", std::move(data));
    } catch (const std::exception&) {
        // A destructor must not throw; the first failure is in the log already.
    }
}

void TrialFailures::note(nlohmann::json data) {
    ++count_;
    if (count_ > 1) return;
    for (auto it = fields_.begin(); it != fields_.end(); ++it) data[it.key()] = it.value();
    log::error("app", "app.error", std::move(data));
}

Trial try_strike(const rules::Position& p, rules::Vec2 cue, const phys::CueStrike& strike, const Env& env,
                 phys::ShotRecord* record) {
    Trial out;
    out.strike = strike;
    if (p.cue_ball < 0 || p.cue_ball >= int(p.balls.size())) {
        failure_logged("opponent::try_strike", "invalid_input", "the position has no ball at its cue ball's place", p,
                       cue, strike);
        return out;
    }
    try {
        phys::SimInput in;
        in.start.balls.reserve(p.balls.size());
        for (const rules::BallOnTable& b : p.balls) {
            phys::BallState s;
            s.ball = b.ball;
            s.index = b.index;
            s.r = {b.pos.x, b.pos.y, 0.0};
            in.start.balls.push_back(s);
        }
        in.start.balls[std::size_t(p.cue_ball)].r = {cue.x, cue.y, 0.0};
        in.cue_ball = p.cue_ball;
        in.has_strike = true;
        in.strike = strike;
        in.seed = 0;
        phys::ShotRecord r = phys::simulate(in, env.table, env.params);
        read_record(r, p.cue_ball, out);
        if (record != nullptr) *record = std::move(r);
    } catch (const std::exception& e) {
        out = Trial{};
        out.strike = strike;
        failure_logged("opponent::try_strike", "exception", e.what(), p, cue, strike);
    }
    return out;
}

SolvedPot solve_pot(const rules::Position& p, const Pot& pot, const Spec& spec, const Env& env, bool measure_window,
                    int max_sims) {
    if (pot.target < 0 || pot.target >= int(p.balls.size()) || pot.target == p.cue_ball) {
        failure_logged("opponent::solve_pot", "invalid_input", "no object ball at place " + std::to_string(pot.target),
                       p, pot.cue, phys::CueStrike{});
        return {};
    }
    Solver solver(p, pot, spec, env, max_sims);
    return solver.solve(measure_window);
}

double least_pot_speed(const Pot& pot, const Env& env) {
    const phys::PhysicsParams& p = env.params;
    const double rolling = p.roll_friction * p.gravity_m_s2;
    // A ball struck through its centre, or knocked on by a ball, slides first and rolls at 5/7 of
    // its speed. The distance covered while sliding is taken as rolling, which only adds margin.
    constexpr double kRollShare = 5.0 / 7.0;
    const double object_rolls = std::sqrt(2.0 * rolling * (pot.object_travel_m + kPotCarry_m));
    const double object_leaves = object_rolls / kRollShare;
    const double transfer = 0.5 * (1.0 + p.ball_restitution) * std::max(std::cos(pot.cut_rad), 1.0e-3);
    const double cue_arrives = object_leaves / transfer;
    const double cue_rolls = std::sqrt(cue_arrives * cue_arrives + 2.0 * rolling * pot.cue_travel_m);
    const double cue_leaves = cue_rolls / kRollShare;
    // cue.cpp's impulse for a centre-ball hit: (1 + e) V / (1 + m / M).
    const double cue_speed = cue_leaves * (1.0 + p.ball_mass_kg / p.cue_mass_kg) / (1.0 + p.cue_tip_restitution);
    const double fastest = std::max(p.max_cue_speed_m_s, kLeastSpeed_m_s);
    const double speed = 1.1 * cue_speed;
    if (!std::isfinite(speed)) return fastest;
    return std::clamp(speed, kLeastSpeed_m_s, fastest);
}

}  // namespace ofs::opponent
