// The tactics (spec 10.2, docs/opponent/README.md "Tactics"): safeties, snookers, escapes and the
// break-off. Each planned stroke is simulated once as planned, judged by the referee and weighed by
// what it leaves the other player as this level reads it, on the scale a pot is scored on.

#include "ofs/opponent/tactics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/evaluate.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/physics/cue.hpp"
#include "ofs/rules/engine.hpp"

namespace ofs::opponent {

namespace {

using rules::Ball;
using rules::BallOnTable;
using rules::FrameState;
using rules::OnKind;
using rules::Position;
using rules::Vec2;

constexpr double kPi = 3.14159265358979323846;
constexpr double kD = table::kBallDiameter;
constexpr double kR = table::kBallRadius;

// A path is clear when every other ball is this much more than a ball's width from it, and a ball
// this close to the cue ball is not played at (the stand-in's values).
constexpr double kPathMargin_m = 0.002;
constexpr double kTooClose_m = 0.004;
// The fastest a tactic is planned with, as a share of the cue's most, as for a pot.
constexpr double kTopSpeed = 0.95;

// Safeties: how full the cue ball takes its ball, thin to half ball, and how far it is asked to
// run after it at each rung of pace; the middle rung is the first tried. Tactics 3 adds a soft
// rung that leaves the cue ball near its ball, where snookers are laid.
constexpr double kThickness[] = {0.15, 0.3, 0.5};
constexpr double kRuns[] = {1.2, 2.5, 4.0};
constexpr double kRunsSnooker[] = {0.5, 1.2, 2.5, 4.0};
// A thick contact leaves the cue ball little of its pace: its run after is asked of the speed at
// contact over the share it keeps (about the sine of the cut), never under this.
constexpr double kLeastKept = 0.35;
// Tactics 1 plays at the nearest balls on only; from 2 at every one it can reach.
constexpr int kBasicBalls = 2;
// Places in the D tried with the cue ball in hand.
constexpr int kPlacesBasic = 3;
constexpr int kPlacesFull = 5;
// The share of the tries spent on the first pass, every contact at the middle pace and centre
// ball; the rest go to the best of them at other paces and, from tactics 2, with spin.
constexpr double kFirstPass = 0.5;
struct Tip {
    double x;
    double y;
};
// As shares of level.max_tip: screw, stun, left and right side.
constexpr Tip kTips[] = {{0.0, -1.0}, {0.0, -0.5}, {-1.0, 0.0}, {1.0, 0.0}};

// The break-off: from between the brown and the yellow or green, thin on the end red of the back
// row at the stand-in's pace (2.7 m/s from 0.3 of the ball brings the cue ball back to baulk),
// then faster and slower. From tactics 2 also with side and from nearer the ends of the D.
constexpr double kBreakY[] = {0.146, 0.22};
constexpr double kBreakThickness[] = {0.3, 0.2, 0.4};
constexpr double kBreakSpeed_m_s = 2.7;
constexpr double kBreakPaces[] = {0.88, 1.12};

// Escapes: straight strokes this many to the turn are followed off the cushions as a mirror would
// return them; the widest routes are tried, each at two paces (run after the ball, metres).
constexpr int kFanSteps = 1440;
constexpr int kRoutes = 8;
constexpr double kEscapeRuns[] = {1.0, 2.5};
// A cushion does not return a ball as a mirror does: each route is aimed again with the cue ball
// simulated alone, at most kTurns times, by at most kMaxTurn_rad, until it passes its ball within
// kCloseEnough_m of where the mirror sent it (the stand-in's way).
constexpr int kTurns = 3;
constexpr int kHalvings = 3;
constexpr double kProbe_rad = 0.003;
constexpr double kMaxTurn_rad = 0.08;
constexpr double kCloseEnough_m = 0.003;
// Cushion routes stay this far from a corner pocket's end of a cushion and a middle pocket's centre.
constexpr double kCornerGap_m = 0.13;
constexpr double kMiddleGap_m = 0.16;
// A cushion takes far more pace than its restitution says: the cue ball comes off it with its roll
// against it and slides until it rolls again (straight on, a third of its speed is left). A route
// is asked for this much more run per cushion, and the pace raised by kRaise, up to kRaises times,
// while the cue ball alone comes to rest short of its ball.
constexpr double kPerCushion = 0.6;
constexpr double kRaise = 1.3;
constexpr int kRaises = 3;
// Swerve (tactics 3): round the ball in the way, the cue raised, side on, aimed past it by the
// angle that clears it and half as much again.
constexpr int kSwerveTargets = 2;
constexpr double kSwerveElevations[] = {0.35, 0.6};
constexpr double kSwerveWide[] = {1.0, 1.5};
constexpr double kSwerveRun = 1.0;
constexpr double kSwerveClear_m = 0.01;
// The share of the tries a swerve may take when cushion routes are there too.
constexpr double kSwerveShare = 0.4;

// The rules of thumb for missing (docs/opponent/README.md "Tactics"; the report gives the numbers).
// A stroke at a window `w` radians wide (the aims that meet the ball first) misses with
//   1 - erf((w / 2) / (sigma sqrt 2)),
//   sigma = aim_sigma_rad * (1 + aim_sigma_per_m * route length) + cushions * kCushionSigma_rad,
// so a level misses more the narrower the window, the longer the way and the more cushions: a
// mirror's angle is not the cushion's, and the error grows at each. An escape needing more cushions
// than the level goes off fails kBeyondReach of the time; none within two, kHopeless. A swerve
// misses kSwerveMiss of the time. A thin contact of thickness t at d metres misses one way only:
// 0.5 erfc(t D / d / (sigma sqrt 2)).
constexpr double kCushionSigma_rad = 0.006;
constexpr double kBeyondReach = 0.75;
constexpr double kHopeless = 0.9;
constexpr double kSwerveMiss = 0.4;
// No snooker is certain to be escaped: its failure is read as at least this.
constexpr double kLeastFail = 0.01;
constexpr int kLeastPenalty = 4;
constexpr int kStudied = 2;  // cushions the other player's escape is looked for off

// The cue ball's path before its first ball is sampled this many times to say how near a miss
// came.
constexpr int kNearSamples = 48;

Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 operator*(Vec2 a, double k) { return {a.x * k, a.y * k}; }
double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
double length(Vec2 a) { return std::sqrt(dot(a, a)); }
Vec2 unit(Vec2 a) {
    const double n = length(a);
    return n > 0.0 ? a * (1.0 / n) : Vec2{1.0, 0.0};
}
double angle_of(Vec2 a) { return std::atan2(a.y, a.x); }
Vec2 turned(Vec2 a, double by) {
    const double c = std::cos(by);
    const double s = std::sin(by);
    return {a.x * c - a.y * s, a.x * s + a.y * c};
}

bool contains(const std::vector<int>& places, int place) {
    return std::find(places.begin(), places.end(), place) != places.end();
}

std::string label(const Position& p, int place) {
    if (place < 0 || place >= int(p.balls.size())) return std::string();
    const BallOnTable& b = p.balls[std::size_t(place)];
    std::string out(table::ball_name(b.ball));
    if (b.ball == Ball::red) out += " " + std::to_string(b.index);
    return out;
}

int reds_left(const Position& p) {
    int reds = 0;
    for (const BallOnTable& b : p.balls) reds += b.ball == Ball::red ? 1 : 0;
    return reds;
}

std::vector<BallOnTable> balls_with_cue(const Position& p, Vec2 cue) {
    std::vector<BallOnTable> out = p.balls;
    out[std::size_t(p.cue_ball)].pos = cue;
    return out;
}

phys::TableState table_state(const Position& p, Vec2 cue) {
    phys::TableState t;
    t.balls.reserve(p.balls.size());
    for (const BallOnTable& b : p.balls) {
        phys::BallState s;
        s.ball = b.ball;
        s.index = b.index;
        s.r = {b.pos.x, b.pos.y, 0.0};
        t.balls.push_back(s);
    }
    t.balls[std::size_t(p.cue_ball)].r = {cue.x, cue.y, 0.0};
    return t;
}

// As search.cpp: never under the minimum for the tip offsets nor the centre-ball one, so the stroke
// simulated is the stroke played.
double lowest_cue(const phys::TableState& table, int cue_ball, double aim_rad, double tip_x, double tip_y,
                  const Env& env) {
    phys::CueStrike c;
    c.aim_rad = aim_rad;
    c.tip_x = tip_x;
    c.tip_y = tip_y;
    return std::max(phys::min_cue_elevation(table, cue_ball, c, env.table, env.params),
                    phys::min_cue_elevation(table, cue_ball, aim_rad, env.table, env.params));
}

double distance_to_path(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double l2 = dot(ab, ab);
    const double t = l2 > 0.0 ? std::clamp(dot(p - a, ab) / l2, 0.0, 1.0) : 0.0;
    return length(p - (a + ab * t));
}

bool path_clear(const std::vector<BallOnTable>& balls, Vec2 a, Vec2 b, int skip, int also, double clear) {
    for (std::size_t i = 0; i < balls.size(); ++i) {
        if (int(i) == skip || int(i) == also) continue;
        if (distance_to_path(balls[i].pos, a, b) < clear) return false;
    }
    return true;
}

// The cue ball sent from `cue` to pass `ball` with its centre `offset` ball widths to the left of
// that ball's: its direction and where its centre is when the two touch.
bool contact_point(Vec2 cue, Vec2 ball, double offset, Vec2& dir, Vec2& contact) {
    const Vec2 to = ball - cue;
    const double d = length(to);
    if (d < kD + kTooClose_m) return false;
    const Vec2 u = to * (1.0 / d);
    dir = unit(ball + Vec2{-u.y, u.x} * (offset * kD) - cue);
    const double along = dot(to, dir);
    const double across = cross(dir, to);
    const double reach = along - std::sqrt(std::max(0.0, kD * kD - across * across));
    if (!(reach > 0.0)) return false;
    contact = cue + dir * reach;
    return true;
}

// The cue speed with which a centre-ball cue ball runs `run_m` on the cloth: it slides first and
// rolls at 5/7 of its speed, the cue's impulse as cue.cpp gives it (solve.cpp least_pot_speed).
double speed_for_run(double run_m, const Env& env) {
    const phys::PhysicsParams& p = env.params;
    const double rolling = p.roll_friction * p.gravity_m_s2;
    const double leaves = std::sqrt(2.0 * rolling * std::max(run_m, 0.0)) / (5.0 / 7.0);
    const double cue = leaves * (1.0 + p.ball_mass_kg / p.cue_mass_kg) / (1.0 + p.cue_tip_restitution);
    const double top = kTopSpeed * p.max_cue_speed_m_s;
    if (!std::isfinite(cue)) return top;
    return std::clamp(cue, std::min(kLeastSpeed_m_s, top), top);
}

// Where a ball sent from `from` along `dir` is after `run` metres, off the cushions as a mirror.
Vec2 run_to(Vec2 from, Vec2 dir, double run) {
    const double hx = table::kPlayingLength / 2.0 - kR;
    const double hy = table::kPlayingWidth / 2.0 - kR;
    for (int bounce = 0; bounce < 8 && run > 0.0; ++bounce) {
        const double tx = dir.x != 0.0 ? ((dir.x > 0.0 ? hx : -hx) - from.x) / dir.x : run;
        const double ty = dir.y != 0.0 ? ((dir.y > 0.0 ? hy : -hy) - from.y) / dir.y : run;
        const double leg = std::max(0.0, std::min({tx, ty, run}));
        from = from + dir * leg;
        run -= leg;
        if (run <= 0.0) break;
        if (tx < ty) {
            dir.x = -dir.x;
        } else {
            dir.y = -dir.y;
        }
    }
    return from;
}

// The line a ball's centre bounces on along one cushion.
struct Rail {
    bool across = true;  // the top and baulk cushions, which run along y
    double at = 0.0;

    double along(Vec2 p) const { return across ? p.y : p.x; }
    bool straight(double along_it) const {
        const double a = std::abs(along_it);
        if (across) return a <= table::kPlayingWidth / 2.0 - kCornerGap_m;
        return a >= kMiddleGap_m && a <= table::kPlayingLength / 2.0 - kCornerGap_m;
    }
};

std::vector<Rail> rails() {
    const double hx = table::kPlayingLength / 2.0 - kR;
    const double hy = table::kPlayingWidth / 2.0 - kR;
    return {{true, hx}, {true, -hx}, {false, hy}, {false, -hy}};
}

// A straight stroke followed off the cushions as a mirror would return it, to the first ball it
// touches.
struct Route {
    int ball = -1;  // its place; -1 when it meets none, or a pocket's jaws first
    int cushions = 0;
    Vec2 from;  // its last leg: where it starts and which way it goes
    Vec2 dir;
    double length = 0.0;  // to where it touches the ball
};

Route follow(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 from, Vec2 dir, int most,
             const std::vector<Rail>& all) {
    const double inf = std::numeric_limits<double>::infinity();
    const double hx = all[0].at;
    const double hy = all[2].at;
    Route out;
    double travelled = 0.0;
    for (;;) {
        const double tx = dir.x != 0.0 ? ((dir.x > 0.0 ? hx : -hx) - from.x) / dir.x : inf;
        const double ty = dir.y != 0.0 ? ((dir.y > 0.0 ? hy : -hy) - from.y) / dir.y : inf;
        const double leg = std::max(0.0, std::min(tx, ty));
        double first = leg;
        for (std::size_t i = 0; i < balls.size(); ++i) {
            if (int(i) == cue_ball) continue;
            const Vec2 to = balls[i].pos - from;
            const double along = dot(to, dir);
            if (along <= 0.0) continue;
            const double across = cross(dir, to);
            if (across * across >= kD * kD) continue;
            const double at = along - std::sqrt(kD * kD - across * across);
            if (at < first) {
                first = at;
                out.ball = int(i);
            }
        }
        out.from = from;
        out.dir = dir;
        if (out.ball >= 0) {
            out.length = travelled + first;
            return out;
        }
        if (out.cushions == most || !(leg < inf)) return out;
        from = from + dir * leg;
        travelled += leg;
        const Rail& rail = all[std::size_t(tx < ty ? (dir.x > 0.0 ? 0 : 1) : (dir.y > 0.0 ? 2 : 3))];
        if (!rail.straight(rail.along(from))) return out;
        ++out.cushions;
        if (tx < ty) {
            dir.x = -dir.x;
        } else {
            dir.y = -dir.y;
        }
    }
}

// Neighbouring strokes of the fan that reach the same ball the same way.
struct Window {
    Route route;
    double aim = 0.0;    // its middle
    double width = 0.0;  // radians
};

std::vector<Window> windows(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 cue, int most) {
    const std::vector<Rail> all = rails();
    const double step = 2.0 * kPi / kFanSteps;
    const auto stroke = [&](double aim) {
        return follow(balls, cue_ball, cue, {std::cos(aim), std::sin(aim)}, most, all);
    };
    std::vector<Window> out;
    Route last;
    int run = 0;
    for (int i = 0; i <= kFanSteps; ++i) {
        const Route r = i < kFanSteps ? stroke(i * step - kPi) : Route{};
        if (i < kFanSteps && run > 0 && r.ball == last.ball && r.cushions == last.cushions) {
            ++run;
            continue;
        }
        if (run > 0 && last.ball >= 0) {
            const double aim = (i - 0.5 * (run + 1)) * step - kPi;
            const Route middle = stroke(aim);
            if (middle.ball == last.ball && middle.cushions == last.cushions) out.push_back({middle, aim, run * step});
        }
        last = r;
        run = 1;
    }
    return out;
}

double miss_chance(double width_rad, double sigma) {
    if (!(sigma > 0.0)) return width_rad > 0.0 ? 0.0 : 1.0;
    return 1.0 - std::erf(0.5 * width_rad / (sigma * std::sqrt(2.0)));
}

// The balls a stroke may be played at: the balls on and, with FREE BALL called, every other object
// ball.
std::vector<int> targets_of(const Position& p) {
    std::vector<int> out = rules::places_on(p);
    if (!p.free_ball) return out;
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        if (int(i) != p.cue_ball && !contains(out, int(i))) out.push_back(int(i));
    }
    return out;
}

enum class Kind { safety, escape, break_off };

struct Plan {
    Kind kind = Kind::safety;
    Vec2 cue;
    int target = -1;
    double thickness = 0.0;  // as planned
    double offset = 0.0;     // signed, in ball widths to the left of the target's centre
    double contact_m = 0.0;  // the cue ball's way to its ball
    int run = 0;             // the rung of pace
    int tip = -1;            // index into kTips; -1 centre ball
    int group = 0;           // plans that differ only in pace and spin share one
    int cushions = 0;        // planned, for an escape
    double window_rad = 0.0;
    bool swerve = false;
    Route route;
    phys::CueStrike strike;  // aim, speed and tip; the elevation is the least the stroke asks for
};

// What one candidate came to.
struct Weighed {
    Plan plan;
    phys::CueStrike strike;
    std::string kind;
    rules::ShotInput input;
    int first_hit = -1;
    int cushions = 0;
    double thickness = 0.0;  // as met
    Vec2 cue_rest;
    bool snookered = false;
    std::string next;
    double next_chance = 0.0;
    double leave = 0.0;  // to the other player, as read; less than 0 when the striker stays at the table
    double gain = 0.0;
    double risk = 0.0;
    double worth = 0.0;  // as read
    double read = 0.0;   // as misread
    double nearest = 0.0;  // a miss: how far the cue ball passed from a ball it may play at
};

class Tactics {
public:
    Tactics(const FrameState& s, const Level& level, const Env& env, Budget& budget, Rng& judgement)
        : s_(s), level_(level), env_(env), budget_(budget), judgement_(judgement) {
        targets_ = targets_of(s);
        open_ = rules::nominations(s);
        const int me = s.striker == 1 ? 1 : 0;
        const int lead = s.scores[std::size_t(me)] - s.scores[std::size_t(1 - me)];
        const int left = rules::points_remaining(s);
        if (lead > left) {
            score_ = "ahead";
        } else if (left < -lead) {
            score_ = "snookers";
        }
    }

    Tactic run() {
        if (s_.cue_in_hand && s_.on.kind == OnKind::red && reds_left(s_) == 15) {
            break_offs();
        } else {
            std::vector<Vec2> places;
            if (s_.cue_in_hand) {
                places = cue_placements(s_, env_, level_.tactics >= 2 ? kPlacesFull : kPlacesBasic);
            } else {
                places.push_back(s_.balls[std::size_t(s_.cue_ball)].pos);
            }
            bool direct = false;
            for (const Vec2 cue : places) {
                direct = direct || env_.oracle.clear_path(balls_with_cue(s_, cue), s_.cue_ball, targets_);
            }
            const bool planned = direct && safeties(places);
            if (!planned) escapes(places.front());
        }
        return answer();
    }

private:
    bool take_step() {
        if (steps_ >= level_.safety_tries) return false;
        if (!budget_.open()) return false;
        ++budget_.steps;
        ++steps_;
        return true;
    }

    double aim_sigma_at(double travel_m, double elevation_rad) const {
        double sigma = level_.aim_sigma_rad * (1.0 + level_.aim_sigma_per_m * travel_m);
        if (elevation_rad > kRaised_rad) sigma *= level_.awkward_factor;
        return sigma;
    }

    // The penalty for failing to hit a ball of `on` from `p`: four, or the dearest of the balls on
    // and the balls in the way.
    int miss_penalty(const Position& p, const std::vector<int>& on) const {
        int penalty = kLeastPenalty;
        if (p.on.kind != OnKind::red) {
            for (const int place : on) penalty = std::max(penalty, table::ball_value(p.balls[std::size_t(place)].ball));
        }
        for (const int place : env_.oracle.snookering_balls(p.balls, p.cue_ball, on)) {
            penalty = std::max(penalty, table::ball_value(p.balls[std::size_t(place)].ball));
        }
        return penalty;
    }

    // How likely this level, at the table in `p`, fails to hit a ball on (the rule of thumb above).
    double escape_fails(const Position& p, const std::vector<int>& on) const {
        double fails = kHopeless;
        const Vec2 cue = p.balls[std::size_t(p.cue_ball)].pos;
        for (const Window& w : windows(p.balls, p.cue_ball, cue, kStudied)) {
            if (!contains(on, w.route.ball)) continue;
            double chance = kBeyondReach;
            if (w.route.cushions <= level_.escape_cushions) {
                const double sigma =
                    aim_sigma_at(w.route.length, 0.0) + w.route.cushions * kCushionSigma_rad;
                chance = miss_chance(w.width, sigma);
            }
            fails = std::min(fails, chance);
        }
        return fails;
    }

    rules::ShotInput declaration(int target) const {
        if (target < 0 || target >= int(s_.balls.size())) return {};
        const Ball b = s_.balls[std::size_t(target)].ball;
        if (std::find(open_.begin(), open_.end(), b) == open_.end()) return {};
        return {true, b};
    }

    // The stroke as the plan has it, the cue as low as the table lets it lie.
    phys::CueStrike finished(const Plan& plan, double aim) const {
        phys::CueStrike c = plan.strike;
        c.aim_rad = aim;
        const double tip_limit = kTipLimit * env_.params.miscue_limit;
        const double tip = std::hypot(c.tip_x, c.tip_y);
        if (tip > tip_limit) {
            c.tip_x *= tip_limit / tip;
            c.tip_y *= tip_limit / tip;
        }
        c.speed_m_s = std::clamp(c.speed_m_s, std::min(kLeastSpeed_m_s, env_.params.max_cue_speed_m_s),
                                 kTopSpeed * env_.params.max_cue_speed_m_s);
        const phys::TableState table = table_state(s_, plan.cue);
        c.elevation_rad = std::min(std::max(c.elevation_rad, lowest_cue(table, s_.cue_ball, c.aim_rad, c.tip_x,
                                                                        c.tip_y, env_)),
                                   env_.params.max_elevation_rad);
        return c;
    }

    // The cue ball after a contact, as a rolling ball leaves one, run on to where the plan's pace
    // would take it: the further from the balls the other player will be on, the likelier a safety.
    double promise(const Plan& plan, Vec2 dir, Vec2 contact) const {
        const double sin_cut = std::min(std::abs(plan.offset), 1.0);
        const double cos_cut = std::sqrt(1.0 - sin_cut * sin_cut);
        const double deflect = std::atan2(sin_cut * cos_cut, sin_cut * sin_cut + 2.0 / 7.0);
        const Vec2 away = turned(dir, plan.offset >= 0.0 ? deflect : -deflect);
        const Vec2 end = run_to(contact, away, runs()[std::size_t(plan.run)]);
        double nearest = std::numeric_limits<double>::infinity();
        for (const int place : theirs_) {
            if (place == plan.target) continue;
            nearest = std::min(nearest, length(s_.balls[std::size_t(place)].pos - end));
        }
        return std::isfinite(nearest) ? nearest : 0.0;
    }

    const double* runs() const { return level_.tactics >= 3 ? kRunsSnooker : kRuns; }
    int run_count() const { return level_.tactics >= 3 ? int(std::size(kRunsSnooker)) : int(std::size(kRuns)); }
    int middle_run() const { return level_.tactics >= 3 ? 2 : 1; }

    void price_safety(Plan& plan) const {
        const double kept = std::max(1.0 - plan.thickness, kLeastKept);
        const double after = runs()[std::size_t(plan.run)];
        plan.strike.speed_m_s = speed_for_run(plan.contact_m + after / (kept * kept), env_);
        if (plan.tip >= 0) {
            plan.strike.tip_x = kTips[std::size_t(plan.tip)].x * level_.max_tip;
            plan.strike.tip_y = kTips[std::size_t(plan.tip)].y * level_.max_tip;
        } else {
            plan.strike.tip_x = 0.0;
            plan.strike.tip_y = 0.0;
        }
    }

    // The balls the other player is likely to be on after a safety: the reds while there are any,
    // else the lowest colour.
    void find_theirs() {
        theirs_.clear();
        int lowest = -1;
        for (std::size_t i = 0; i < s_.balls.size(); ++i) {
            const Ball b = s_.balls[i].ball;
            if (b == Ball::cue) continue;
            if (b == Ball::red) {
                theirs_.push_back(int(i));
            } else if (lowest < 0 || table::ball_value(b) < table::ball_value(s_.balls[std::size_t(lowest)].ball)) {
                lowest = int(i);
            }
        }
        if (theirs_.empty() && lowest >= 0) theirs_.push_back(lowest);
    }

    // False when no contact was found to plan.
    bool safeties(const std::vector<Vec2>& places) {
        find_theirs();
        struct Listed {
            Plan plan;
            double promise = 0.0;
        };
        std::vector<Listed> listed;
        int group = 0;
        for (const Vec2 cue : places) {
            const std::vector<BallOnTable> balls = balls_with_cue(s_, cue);
            std::vector<int> order = targets_;
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                return length(balls[std::size_t(a)].pos - cue) < length(balls[std::size_t(b)].pos - cue);
            });
            int reached = 0;
            for (const int target : order) {
                if (level_.tactics < 2 && reached >= kBasicBalls) break;
                bool any = false;
                for (const double thickness : kThickness) {
                    for (const double side : {1.0, -1.0}) {
                        Plan plan;
                        plan.kind = Kind::safety;
                        plan.cue = cue;
                        plan.target = target;
                        plan.thickness = thickness;
                        plan.offset = side * (1.0 - thickness);
                        Vec2 dir;
                        Vec2 contact;
                        if (!contact_point(cue, balls[std::size_t(target)].pos, plan.offset, dir, contact)) continue;
                        if (!path_clear(balls, cue, contact, s_.cue_ball, target, kD + kPathMargin_m)) continue;
                        plan.contact_m = length(contact - cue);
                        plan.run = middle_run();
                        plan.group = group++;
                        plan.strike.aim_rad = angle_of(dir);
                        price_safety(plan);
                        listed.push_back({plan, promise(plan, dir, contact)});
                        any = true;
                    }
                }
                if (any) ++reached;
            }
        }
        if (listed.empty()) return false;
        std::stable_sort(listed.begin(), listed.end(),
                         [](const Listed& a, const Listed& b) { return a.promise > b.promise; });

        const int first = std::max(1, int(std::ceil(kFirstPass * level_.safety_tries)));
        for (std::size_t i = 0; i < listed.size() && int(i) < first; ++i) {
            if (!take_step()) return true;
            attempt(listed[i].plan);
        }
        refine();
        return true;
    }

    // The best contacts at the other paces, then (tactics 2 and up) with spin at their best pace.
    void refine() {
        std::vector<int> picks;
        for (const int i : by_read()) {
            const int group = weighed_[std::size_t(i)].plan.group;
            const bool seen = std::any_of(picks.begin(), picks.end(),
                                          [&](int k) { return weighed_[std::size_t(k)].plan.group == group; });
            if (!seen) picks.push_back(i);
        }
        for (const int pick : picks) {
            const Plan base = weighed_[std::size_t(pick)].plan;
            for (int run = 0; run < run_count(); ++run) {
                if (run == base.run) continue;
                Plan plan = base;
                plan.run = run;
                plan.tip = -1;
                price_safety(plan);
                if (!take_step()) return;
                attempt(plan);
            }
            if (level_.tactics < 2 || !(level_.max_tip > 0.0)) continue;
            int best = pick;
            for (std::size_t i = 0; i < weighed_.size(); ++i) {
                if (weighed_[i].plan.group == base.group && weighed_[i].read > weighed_[std::size_t(best)].read) {
                    best = int(i);
                }
            }
            const int run = weighed_[std::size_t(best)].plan.run;
            for (int tip = 0; tip < int(std::size(kTips)); ++tip) {
                Plan plan = base;
                plan.run = run;
                plan.tip = tip;
                price_safety(plan);
                if (!take_step()) return;
                attempt(plan);
            }
        }
    }

    void break_offs() {
        find_theirs();
        std::vector<Plan> first;
        const int places = level_.tactics >= 2 ? int(std::size(kBreakY)) : 1;
        int group = 0;
        for (int at = 0; at < places; ++at) {
            for (const double side : {-1.0, 1.0}) {
                const Vec2 wanted{table::baulk_line_x() - 0.02, side * kBreakY[at]};
                const Vec2 cue = env_.oracle.nearest_cue_placement(s_.balls, s_.cue_ball, wanted);
                if (!env_.oracle.cue_placement_legal(s_.balls, s_.cue_ball, cue)) continue;
                const int target = end_red(cue);
                if (target < 0) continue;
                const std::vector<BallOnTable> balls = balls_with_cue(s_, cue);
                const Vec2 red = balls[std::size_t(target)].pos;
                const Vec2 u = unit(red - cue);
                // Passed on its outside, the cue ball's side of the table: left of the line to it
                // is +y while the cue points up the table.
                const double outside = u.x * (cue.y > 0.0 ? 1.0 : -1.0) > 0.0 ? 1.0 : -1.0;
                for (const double thickness : kBreakThickness) {
                    Plan plan;
                    plan.kind = Kind::break_off;
                    plan.cue = cue;
                    plan.target = target;
                    plan.thickness = thickness;
                    plan.offset = outside * (1.0 - thickness);
                    Vec2 dir;
                    Vec2 contact;
                    if (!contact_point(cue, red, plan.offset, dir, contact)) continue;
                    if (!path_clear(balls, cue, contact, s_.cue_ball, target, kD + kPathMargin_m)) continue;
                    plan.contact_m = length(contact - cue);
                    plan.group = group++;
                    plan.strike.aim_rad = angle_of(dir);
                    plan.strike.speed_m_s = kBreakSpeed_m_s;
                    first.push_back(plan);
                }
            }
        }
        for (const Plan& plan : first) {
            if (!take_step()) return;
            attempt(plan);
        }
        std::vector<int> picks;
        for (const int i : by_read()) {
            const int group_of = weighed_[std::size_t(i)].plan.group;
            const bool seen = std::any_of(picks.begin(), picks.end(),
                                          [&](int k) { return weighed_[std::size_t(k)].plan.group == group_of; });
            if (!seen) picks.push_back(i);
        }
        for (const int pick : picks) {
            const Plan base = weighed_[std::size_t(pick)].plan;
            for (const double pace : kBreakPaces) {
                Plan plan = base;
                plan.strike.speed_m_s = kBreakSpeed_m_s * pace;
                if (!take_step()) return;
                attempt(plan);
            }
            if (level_.tactics < 2 || !(level_.max_tip > 0.0)) continue;
            int best = pick;
            for (std::size_t i = 0; i < weighed_.size(); ++i) {
                if (weighed_[i].plan.group == base.group && weighed_[i].read > weighed_[std::size_t(best)].read) {
                    best = int(i);
                }
            }
            for (const double side : {1.0, -1.0}) {
                Plan plan = weighed_[std::size_t(best)].plan;
                plan.strike.tip_x = side * level_.max_tip;
                plan.strike.tip_y = 0.0;
                if (!take_step()) return;
                attempt(plan);
            }
        }
    }

    // The end red of the back row on the cue ball's side of the table.
    int end_red(Vec2 cue) const {
        const double side = cue.y > 0.0 ? 1.0 : -1.0;
        double back = -std::numeric_limits<double>::infinity();
        for (const BallOnTable& b : s_.balls) {
            if (b.ball == Ball::red) back = std::max(back, b.pos.x);
        }
        int target = -1;
        for (std::size_t i = 0; i < s_.balls.size(); ++i) {
            const BallOnTable& b = s_.balls[i];
            if (b.ball != Ball::red || b.pos.x < back - 0.005) continue;
            if (target < 0 || b.pos.y * side > s_.balls[std::size_t(target)].pos.y * side) target = int(i);
        }
        return target;
    }

    void escapes(Vec2 cue) {
        const int most = std::min(level_.escape_cushions, kStudied);
        const bool swerves = level_.tactics >= 3;
        if (most <= 0 && !swerves) return;
        const std::vector<BallOnTable> balls = balls_with_cue(s_, cue);
        std::vector<Window> routes;
        if (most > 0) {
            for (const Window& w : windows(balls, s_.cue_ball, cue, most)) {
                if (w.route.cushions >= 1 && contains(targets_, w.route.ball)) routes.push_back(w);
            }
            const auto cost = [](const Window& w) { return (w.route.cushions + 1) / w.width; };
            std::stable_sort(routes.begin(), routes.end(),
                             [&](const Window& a, const Window& b) { return cost(a) < cost(b); });
            if (int(routes.size()) > kRoutes) routes.resize(std::size_t(kRoutes));
        }
        std::vector<Plan> by_cushion[std::size(kEscapeRuns)];
        int group = 0;
        for (const Window& w : routes) {
            for (std::size_t r = 0; r < std::size(kEscapeRuns); ++r) {
                Plan plan;
                plan.kind = Kind::escape;
                plan.cue = cue;
                plan.target = w.route.ball;
                plan.cushions = w.route.cushions;
                plan.window_rad = w.width;
                plan.route = w.route;
                plan.contact_m = w.route.length;
                plan.group = group;
                plan.strike.aim_rad = w.aim;
                plan.strike.speed_m_s =
                    speed_for_run(w.route.length * (1.0 + kPerCushion * w.route.cushions) + kEscapeRuns[r], env_);
                by_cushion[r].push_back(plan);
            }
            ++group;
        }
        std::vector<Plan> swerve;
        if (swerves) swerve = swerve_plans(cue, balls, group);
        if (by_cushion[0].empty() && swerve.empty()) return;

        std::vector<Plan> order = by_cushion[0];
        int swerve_room = int(swerve.size());
        if (!routes.empty()) {
            swerve_room = std::min(swerve_room, std::max(1, int(kSwerveShare * level_.safety_tries)));
        }
        order.insert(order.end(), swerve.begin(), swerve.begin() + swerve_room);
        for (std::size_t r = 1; r < std::size(kEscapeRuns); ++r) {
            order.insert(order.end(), by_cushion[r].begin(), by_cushion[r].end());
        }
        for (const Plan& plan : order) {
            if (!take_step()) return;
            attempt(plan);
        }
    }

    // Round the nearest ball in the way of each of the nearest balls on: aimed past it to one side
    // with side that bends the cue ball back (right-hand side swerves it right with the cue
    // raised), those bending the other way after.
    std::vector<Plan> swerve_plans(Vec2 cue, const std::vector<BallOnTable>& balls, int group) const {
        std::vector<int> order = targets_;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return length(balls[std::size_t(a)].pos - cue) < length(balls[std::size_t(b)].pos - cue);
        });
        if (int(order.size()) > kSwerveTargets) order.resize(std::size_t(kSwerveTargets));
        std::vector<Plan> bent_back;
        std::vector<Plan> bent_away;
        if (!(level_.max_tip > 0.0)) return {};
        for (const int target : order) {
            const Vec2 to = balls[std::size_t(target)].pos;
            const double straight = angle_of(to - cue);
            double nearest = length(to - cue);
            for (std::size_t i = 0; i < balls.size(); ++i) {
                if (int(i) == s_.cue_ball || int(i) == target) continue;
                if (distance_to_path(balls[i].pos, cue, to) < kD) {
                    nearest = std::min(nearest, length(balls[i].pos - cue));
                }
            }
            const double clear = std::asin(std::min(0.9, (kD + kSwerveClear_m) / std::max(nearest, kD)));
            for (const double wide : kSwerveWide) {
                for (const double elevation : kSwerveElevations) {
                    for (const double way : {1.0, -1.0}) {
                        Plan plan;
                        plan.kind = Kind::escape;
                        plan.swerve = true;
                        plan.cue = cue;
                        plan.target = target;
                        plan.contact_m = length(to - cue);
                        plan.group = group++;
                        plan.strike.aim_rad = straight + way * clear * wide;
                        plan.strike.elevation_rad = elevation;
                        plan.strike.speed_m_s = speed_for_run(plan.contact_m + kSwerveRun, env_);
                        plan.strike.tip_x = way * level_.max_tip;
                        bent_back.push_back(plan);
                        plan.strike.tip_x = -way * level_.max_tip;
                        plan.group = group++;
                        bent_away.push_back(plan);
                    }
                }
            }
        }
        bent_back.insert(bent_back.end(), bent_away.begin(), bent_away.end());
        return bent_back;
    }

    // The cue ball's course for a stroke with nothing else on the table: the first ball of the
    // frame it would touch (-1 none), and how far to the left of its line `ball` lies as it passes
    // it on the leg after `cushions` cushions. False when it never plays that leg.
    bool course(const Plan& plan, double aim, int& first, double& passes, bool& stopped_short) {
        stopped_short = false;
        Position alone;
        alone.balls.push_back(s_.balls[std::size_t(s_.cue_ball)]);
        alone.cue_ball = 0;
        phys::ShotRecord record;
        const Trial trial = try_strike(alone, plan.cue, finished(plan, aim), env_, &record);
        ++budget_.sims;
        if (!trial.clean || record.segments.empty()) return false;

        std::vector<double> met;
        for (const phys::ShotEvent& e : record.events) {
            if (e.kind == phys::EventKind::ball_line || e.kind == phys::EventKind::ball_arc) met.push_back(e.t);
        }
        const int cushions = plan.cushions;
        if (int(met.size()) < cushions) return false;
        const double leg_from = cushions == 0 ? 0.0 : met[std::size_t(cushions) - 1];
        const double leg_to = int(met.size()) > cushions ? met[std::size_t(cushions)] : record.t_end;

        // Its path as straight pieces; a sliding ball curves, so those stretches are cut in three.
        std::vector<Vec2> at;
        std::vector<double> when;
        const std::vector<phys::Segment>& segments = record.segments.front();
        for (std::size_t i = 0; i < segments.size(); ++i) {
            const phys::Segment& seg = segments[i];
            if (seg.start.motion == phys::Motion::pocketed || seg.t0 > leg_to) break;
            const double until = std::min(i + 1 < segments.size() ? segments[i + 1].t0 : record.t_end, leg_to);
            at.push_back({seg.start.r.x, seg.start.r.y});
            when.push_back(seg.t0);
            if (seg.start.motion != phys::Motion::sliding || !(until > seg.t0)) continue;
            for (const double share : {1.0 / 3.0, 2.0 / 3.0}) {
                const double t = seg.t0 + share * (until - seg.t0);
                const phys::BallState b = record.ball_at(0, t);
                at.push_back({b.r.x, b.r.y});
                when.push_back(t);
            }
        }
        if (!at.empty()) {
            const phys::BallState b = record.ball_at(0, leg_to);
            if (b.motion != phys::Motion::pocketed) {
                at.push_back({b.r.x, b.r.y});
                when.push_back(leg_to);
            }
        }

        first = -1;
        bool passed = false;
        Vec2 last_dir;
        double nearest = std::numeric_limits<double>::infinity();
        const Vec2 target = s_.balls[std::size_t(plan.target)].pos;
        for (std::size_t i = 0; i + 1 < at.size(); ++i) {
            const double piece = length(at[i + 1] - at[i]);
            if (!(piece > 0.0)) continue;
            const Vec2 dir = (at[i + 1] - at[i]) * (1.0 / piece);
            if (first < 0) {
                double reach = piece;
                for (std::size_t k = 0; k < s_.balls.size(); ++k) {
                    if (int(k) == s_.cue_ball) continue;
                    const Vec2 to = s_.balls[k].pos - at[i];
                    const double along = dot(to, dir);
                    const double across = cross(dir, to);
                    if (along <= 0.0 || across * across >= kD * kD) continue;
                    const double touch = along - std::sqrt(kD * kD - across * across);
                    if (touch <= reach) {
                        reach = touch;
                        first = int(k);
                    }
                }
            }
            if (when[i] < leg_from - 1.0e-9) continue;
            last_dir = dir;
            const Vec2 foot = at[i] + dir * std::clamp(dot(target - at[i], dir), 0.0, piece);
            if (length(target - foot) < nearest) {
                nearest = length(target - foot);
                passes = cross(dir, target - foot);
                passed = true;
            }
        }
        // At rest on its leg before it came level with the ball.
        if (passed && leg_to == record.t_end) {
            const Vec2 rest = at.back();
            stopped_short = dot(target - rest, last_dir) > 0.0 && length(target - rest) <= nearest + 1.0e-9;
        }
        return passed;
    }

    // The cue ball alone at `aim`, its pace raised while it comes to rest short of the ball. False
    // when it never plays the route's last leg.
    bool course_at(Plan& plan, double aim, int& first, double& passes, int& raised) {
        const double top = kTopSpeed * env_.params.max_cue_speed_m_s;
        for (;;) {
            bool short_of = false;
            if (!course(plan, aim, first, passes, short_of)) return false;
            if (!short_of || raised >= kRaises || !(plan.strike.speed_m_s < top)) return true;
            plan.strike.speed_m_s = std::min(plan.strike.speed_m_s * kRaise, top);
            ++raised;
        }
    }

    // The mirror's aim turned until the cue ball, simulated alone, meets the ball where the mirror
    // sent it: Newton's steps on how far it passes the ball, each halved while the cue ball would
    // not play the route that way (into a pocket, off another cushion). The aim kept is the nearest
    // to the mirror's line that meets the ball first; the mirror's own when none does.
    void solve_route(Plan& plan) {
        const double wanted = cross(plan.route.dir, s_.balls[std::size_t(plan.target)].pos - plan.route.from);
        double at = plan.strike.aim_rad;
        int raised = 0;
        int first = -1;
        double passes = 0.0;
        if (!course_at(plan, at, first, passes, raised)) return;
        double found = at;
        double found_off = first == plan.target ? std::abs(passes - wanted) : std::numeric_limits<double>::infinity();
        for (int turn = 0; turn < kTurns && std::abs(passes - wanted) > kCloseEnough_m; ++turn) {
            int probed_first = -1;
            double probed = 0.0;
            if (!course_at(plan, at + kProbe_rad, probed_first, probed, raised)) break;
            const double per_rad = (probed - passes) / kProbe_rad;
            if (!(std::abs(per_rad) > 1.0e-3)) break;
            double step = std::clamp((wanted - passes) / per_rad, -kMaxTurn_rad, kMaxTurn_rad);
            bool moved = false;
            for (int halving = 0; halving < kHalvings && !moved; ++halving, step *= 0.5) {
                int next_first = -1;
                double next = 0.0;
                if (!course_at(plan, at + step, next_first, next, raised)) continue;
                at += step;
                first = next_first;
                passes = next;
                moved = true;
            }
            if (!moved) break;
            if (first == plan.target && std::abs(passes - wanted) < found_off) {
                found = at;
                found_off = std::abs(passes - wanted);
            }
        }
        plan.strike.aim_rad = found;
    }

    void attempt(Plan plan) {
        if (plan.kind == Kind::escape && !plan.swerve && plan.cushions > 0) solve_route(plan);
        const double aim = plan.strike.aim_rad;
        const phys::CueStrike strike = finished(plan, aim);
        FrameState from = s_;
        if (s_.cue_in_hand) {
            std::string error;
            if (!rules::place_cue_ball(from, plan.cue, env_.oracle, error)) return;
        }
        phys::ShotRecord record;
        const Trial trial = try_strike(from, plan.cue, strike, env_, &record);
        ++budget_.sims;
        if (!trial.clean) return;
        const rules::ShotInput input = declaration(plan.target);
        rules::Judgement j = rules::judge_shot(from, input, record, env_.table, env_.oracle, env_.ref);
        const bool legal = j.fouls.empty();
        if (!legal && plan.kind != Kind::escape) return;
        // A stroke after which the striker is still at the table potted a ball: it is a pot, which
        // the search weighs with its chance; as a tactic the level's error would not repeat it.
        if (legal && (j.post.over || j.post.striker == s_.striker)) return;
        if (legal && plan.kind == Kind::escape && !plan.swerve &&
            cushions_before(record) > level_.escape_cushions) {
            return;
        }

        Weighed w;
        w.plan = plan;
        w.strike = strike;
        w.input = input;
        w.first_hit = trial.first_hit;
        w.cue_rest = trial.rest[std::size_t(s_.cue_ball)];
        w.cushions = cushions_before(record);
        // A safety's thickness is the one planned (0 to half ball); an escape's is as it met.
        w.thickness = plan.kind == Kind::escape ? thickness_met(record) : plan.thickness;
        const double p_leave = read_leave(j, w);

        int penalty = kLeastPenalty;
        double p_miss = 0.0;
        if (plan.kind == Kind::escape) {
            penalty = miss_penalty(from, targets_);
            if (plan.swerve) {
                p_miss = kSwerveMiss;
            } else {
                p_miss = miss_chance(plan.window_rad, aim_sigma_at(plan.contact_m, strike.elevation_rad) +
                                                          plan.cushions * kCushionSigma_rad);
            }
        } else {
            if (plan.target >= 0) penalty = std::max(penalty, table::ball_value(s_.balls[std::size_t(plan.target)].ball));
            const double sigma = aim_sigma_at(plan.contact_m, strike.elevation_rad);
            const double margin = plan.thickness * kD / std::max(plan.contact_m, kD);
            p_miss = sigma > 0.0 ? 0.5 * std::erfc(margin / (sigma * std::sqrt(2.0))) : 0.0;
        }

        double p_fail = 0.0;
        int their_penalty = 0;
        if (legal && w.snookered && level_.tactics >= 3) {
            const FrameState& post = j.post;
            const std::vector<int> on = rules::places_on(post);
            their_penalty = miss_penalty(post, on);
            p_fail = std::max(escape_fails(post, on), kLeastFail);
        }
        w.kind = plan.kind == Kind::break_off ? "break" : plan.kind == Kind::escape ? "escape" : "safety";
        if (plan.kind == Kind::safety && w.snookered && level_.tactics >= 3) w.kind = "snooker";

        if (!legal) {
            // A miss as planned, kept only in case nothing hits: its foul is known.
            w.gain = 0.0;
            w.risk = double(j.penalty);
            w.worth = score(w.gain, w.leave, w.risk);
            w.read = w.worth;
            w.nearest = nearest(record, w);
            misses_.push_back(std::move(w));
            return;
        }

        // The one misreading of this candidate, applied to every chance its worth is built from as
        // the search misreads a pot's chance.
        const double misread = judgement_.normal();
        const auto misread_chance = [&](double p) {
            return std::clamp(p * (1.0 + level_.judgement_sigma * misread), 0.0, 1.0);
        };
        w.gain = their_penalty * p_fail;
        w.risk = penalty * p_miss;
        w.worth = score(w.gain, w.leave, w.risk);
        const double leave_read = p_leave > 0.0 ? w.leave * misread_chance(p_leave) / p_leave : w.leave;
        w.read = score(their_penalty * misread_chance(p_fail), leave_read, penalty * misread_chance(p_miss));
        weighed_.push_back(std::move(w));
    }

    // The cushions the cue ball met before its first ball, in the stroke as simulated.
    int cushions_before(const phys::ShotRecord& record) const {
        const int cue = s_.cue_ball;
        int cushions = 0;
        for (const phys::ShotEvent& e : record.events) {
            if (e.kind == phys::EventKind::ball_ball && (e.a == cue || e.b == cue)) break;
            if ((e.kind == phys::EventKind::ball_line || e.kind == phys::EventKind::ball_arc) && e.a == cue) {
                ++cushions;
            }
        }
        return cushions;
    }

    // How full the cue ball took its first ball, from its heading as it met it; 0 for none.
    double thickness_met(const phys::ShotRecord& record) const {
        const int cue = s_.cue_ball;
        for (const phys::ShotEvent& e : record.events) {
            if (e.kind != phys::EventKind::ball_ball || (e.a != cue && e.b != cue)) continue;
            const int other = e.a == cue ? e.b : e.a;
            const phys::BallState at = record.ball_at(cue, std::max(0.0, e.t - 1.0e-6));
            const phys::BallState before = record.ball_at(cue, std::max(0.0, e.t - 1.0e-3));
            const Vec2 v{at.r.x, at.r.y};
            const Vec2 heading = unit(v - Vec2{before.r.x, before.r.y});
            const Vec2 centres = unit(s_.balls[std::size_t(other)].pos - v);
            return std::clamp(1.0 - std::abs(cross(heading, centres)), 0.0, 1.0);
        }
        return 0.0;
    }

    // How far the cue ball came from a ball it may play at before it met anything.
    double nearest(const phys::ShotRecord& record, const Weighed& w) const {
        if (contains(targets_, w.first_hit)) return 0.0;
        const int cue = s_.cue_ball;
        double until = record.t_end;
        for (const phys::ShotEvent& e : record.events) {
            if (e.kind == phys::EventKind::ball_ball && (e.a == cue || e.b == cue)) {
                until = e.t;
                break;
            }
        }
        double out = std::numeric_limits<double>::infinity();
        for (int i = 0; i <= kNearSamples; ++i) {
            const phys::BallState b = record.ball_at(cue, until * i / kNearSamples);
            if (b.motion == phys::Motion::pocketed) break;
            for (const int place : targets_) {
                out = std::min(out, std::max(0.0, length(s_.balls[std::size_t(place)].pos - Vec2{b.r.x, b.r.y}) - kD));
            }
        }
        return out;
    }

    // What the position left is worth to the other player as this level reads it, into w.leave,
    // with what the leave says; the chance it is built from is returned. After a foul the one who
    // chooses is read as playing it.
    double read_leave(const rules::Judgement& j, Weighed& w) const {
        FrameState post = j.post;
        if (post.over) {
            w.leave = 0.0;
            return 0.0;
        }
        if (post.pending == rules::Pending::after_foul) post.striker = post.decider;
        post.pending = rules::Pending::none;
        w.snookered = env_.oracle.snookered(post.balls, post.cue_ball, rules::places_on(post), post.cue_in_hand);
        const std::vector<Pot> listed = pots(post, env_, level_.max_cut_rad);
        const Read read = read_pots(listed, level_, env_);
        if (read.best >= 0) {
            w.next = label(post, listed[std::size_t(read.best)].target);
            w.next_chance = read.chance;
        }
        w.leave = read.worth;
        return read.chance;
    }

    double score(double gain, double leave, double risk) const {
        const double a = level_.score_aware;
        if (score_ == "ahead") return (1.0 - a) * gain - leave - (1.0 + a) * risk;
        if (score_ == "snookers") return (1.0 + a) * gain - (1.0 - a) * leave - risk;
        return gain - leave - risk;
    }

    // The legal candidates by worth as misread, the first tried first among equals.
    std::vector<int> by_read() const {
        std::vector<int> order;
        for (int i = 0; i < int(weighed_.size()); ++i) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return weighed_[std::size_t(a)].read > weighed_[std::size_t(b)].read;
        });
        return order;
    }

    Tactic answer() const {
        Tactic out;
        out.candidates = steps_;
        const Weighed* chosen = nullptr;
        std::vector<int> order = by_read();
        if (!order.empty()) {
            chosen = &weighed_[std::size_t(order.front())];
        } else {
            for (const Weighed& m : misses_) {
                if (chosen == nullptr || m.nearest < chosen->nearest) chosen = &m;
            }
        }
        if (chosen == nullptr) {
            out.why = {{"candidates", steps_}};
            return out;
        }
        const bool hit = !order.empty();
        out.found = true;
        out.kind = chosen->kind;
        out.places_cue = s_.cue_in_hand;
        out.cue_pos = chosen->plan.cue;
        out.input = chosen->input;
        out.meant = chosen->strike;
        out.worth = chosen->read;
        const phys::CueStrike& m = chosen->strike;
        out.why = {{"kind", chosen->kind},
                   {"target", label(s_, chosen->first_hit)},
                   {"thickness", chosen->thickness},
                   {"cushions", chosen->cushions},
                   {"spec",
                    {{"speed_m_s", m.speed_m_s}, {"tip_x", m.tip_x}, {"tip_y", m.tip_y}, {"elevation_rad", m.elevation_rad}}},
                   {"leave",
                    {{"cue", {chosen->cue_rest.x, chosen->cue_rest.y}},
                     {"snookered", chosen->snookered},
                     {"next", chosen->next},
                     {"chance", chosen->next_chance},
                     {"worth", chosen->leave}}},
                   {"worth", chosen->read},
                   {"gain", chosen->gain},
                   {"risk", chosen->risk},
                   {"candidates", steps_}};
        if (chosen->kind == "escape") {
            out.why["hit"] = hit;
            if (chosen->plan.swerve) out.why["swerve"] = true;
        }
        if (!score_.empty()) out.why["score"] = score_;
        nlohmann::json others = nlohmann::json::array();
        std::vector<std::pair<std::string, int>> seen = {{chosen->kind, chosen->first_hit}};
        for (const int i : order) {
            const Weighed& c = weighed_[std::size_t(i)];
            if (&c == chosen) continue;
            const std::pair<std::string, int> key{c.kind, c.first_hit};
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            others.push_back({{"kind", c.kind}, {"target", label(s_, c.first_hit)}, {"worth", c.read}});
            if (others.size() == 3) break;
        }
        out.why["others"] = std::move(others);
        return out;
    }

    const FrameState& s_;
    const Level& level_;
    const Env& env_;
    Budget& budget_;
    Rng& judgement_;
    std::vector<int> targets_;
    std::vector<Ball> open_;
    std::vector<int> theirs_;
    std::string score_;  // "ahead", "snookers" or empty
    std::vector<Weighed> weighed_;
    std::vector<Weighed> misses_;
    int steps_ = 0;
};

}  // namespace

Tactic choose_tactic(const rules::FrameState& s, const Level& level, const Env& env, Budget& budget, Rng& judgement) {
    if (level.tactics <= 0) return {};
    Tactic out;
    try {
        if (s.over || s.pending != rules::Pending::none || s.cue_ball < 0 || s.cue_ball >= int(s.balls.size()) ||
            s.balls[std::size_t(s.cue_ball)].ball != Ball::cue) {
            out.why = {{"error", "the frame is over, pending or has no cue ball"}};
            return out;
        }
        std::unique_ptr<PocketMouths> mouths;
        Env local = env;
        if (env.mouths == nullptr || &env.mouths->table() != &env.table) {
            mouths = std::make_unique<PocketMouths>(env.table);
            local.mouths = mouths.get();
        }
        Tactics tactics(s, level, local, budget, judgement);
        out = tactics.run();
    } catch (const std::exception& e) {
        out = Tactic{};
        out.why = {{"error", e.what()}};
    } catch (...) {
        out = Tactic{};
        out.why = {{"error", "unknown exception"}};
    }
    return out;
}

}  // namespace ofs::opponent
