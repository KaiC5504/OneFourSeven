// The stand-in opponent (spec 21). It lists the strokes it could play in order of preference (the
// break-off, pots by ghost ball, safeties, ways out of a snooker), simulates the first few as aimed
// and takes the first that is no foul and, for a pot, scores; else the first with no foul; else
// the one with the lowest penalty. Only then does it add its own error. It strikes centre ball and
// plays no position.

#include "ofs/opponent/standin.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>

#include "ofs/log.hpp"
#include "ofs/physics/cue.hpp"
#include "ofs/physics/simulator.hpp"
#include "ofs/rules/facts.hpp"
#include "ofs/rules/rules_log.hpp"

namespace ofs::opponent {

namespace {

using rules::Ball;
using rules::BallOnTable;
using rules::DecisionKind;
using rules::FrameState;
using rules::OnKind;
using rules::Position;
using rules::Vec2;

struct Field {
    const char* name;
    double StandinParams::* real = nullptr;
    int StandinParams::* integer = nullptr;
    // Inclusive bounds; a value outside them is rejected rather than clamped.
    double min = 0.0;
    double max = 0.0;
};

const std::vector<Field>& fields() {
    static const std::vector<Field> all = {
        // 0 plays exactly where it aimed. A tenth of a radian misses a ball half a metre away, and
        // half the pace either way is no longer the stroke it chose.
        {"aim_error_rad", &StandinParams::aim_error_rad, nullptr, 0.0, 0.1},
        {"speed_error", &StandinParams::speed_error, nullptr, 0.0, 0.5},
        // At a quarter turn the object ball is not sent anywhere.
        {"max_cut_rad", &StandinParams::max_cut_rad, nullptr, 0.0, 1.5},
        // Slower than this a ball does not cross the table; the physics caps the top.
        {"pot_speed_m_s", &StandinParams::pot_speed_m_s, nullptr, 0.2, 10.0},
        {"safety_speed_m_s", &StandinParams::safety_speed_m_s, nullptr, 0.2, 10.0},
        // 0 takes its first choice unchecked. Each simulation is a share of a millisecond.
        {"max_sims", nullptr, &StandinParams::max_sims, 0.0, 64.0},
        // A frame holds 147 points; needing none it has not lost.
        {"concede_required_points", nullptr, &StandinParams::concede_required_points, 1.0, 147.0},
    };
    return all;
}

bool apply(const Field& field, const nlohmann::json& value, StandinParams& out, std::string& problem) {
    if (!value.is_number()) {
        problem = "not a number";
        return false;
    }
    const double v = value.get<double>();
    if (!std::isfinite(v)) {
        problem = "not finite";
        return false;
    }
    if (field.integer != nullptr && (!value.is_number_integer() || v != std::floor(v))) {
        problem = "not an integer";
        return false;
    }
    if (v < field.min || v > field.max) {
        problem = "outside [" + nlohmann::json(field.min).dump() + ", " + nlohmann::json(field.max).dump() + "]";
        return false;
    }
    if (field.integer != nullptr) {
        out.*field.integer = value.get<int>();
    } else {
        out.*field.real = v;
    }
    return true;
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kD = table::kBallDiameter;
// A path is clear when every other ball is this much more than a ball's width from it: the aim
// carries an error and the balls are not quite where a straight line takes them.
constexpr double kPathMargin_m = 0.002;
// A ball this close to the cue ball is not played at: the tip would still be on the cue ball.
constexpr double kTooClose_m = 0.004;
// The slowest cue it plays, where the physics allows that much.
constexpr double kMinSpeed_m_s = 0.3;
// A pot is aimed this far inside the rim of the drop circle, as a share of its radius.
constexpr double kPotDepth = 0.3;
// Into a middle pocket from no further off square than this (55 degrees): beyond it the near jaw
// is in the way.
constexpr double kMiddleMinCos = 0.5735764363510462;
// How many times a pot is tried again, and how much softer after an in-off.
constexpr int kPotRetries = 2;
constexpr double kSofter = 0.5;
// Safeties: across how much of the ball the cue ball passes, in ball widths, thin first.
constexpr double kSafetyOffsets[] = {0.7, -0.7, 0.4, -0.4, 0.0};
constexpr int kSafetyBalls = 4;
// Escapes: straight strokes this many to the turn are followed off at most this many cushions.
constexpr int kFanSteps = 1440;
constexpr int kMaxCushions = 2;
// A cushion does not return a ball as a mirror does, so each way found is aimed again with the
// cue ball simulated alone: at most this many such courses a stroke, for this many escapes, each
// turned at most twice by at most kMaxTurn_rad to pass within kCloseEnough_m of where it was meant.
constexpr int kCourses = 40;
constexpr int kEscapes = 6;
constexpr int kTurns = 2;
constexpr double kProbe_rad = 0.003;
constexpr double kMaxTurn_rad = 0.08;
constexpr double kCloseEnough_m = 0.003;
// With no way to a ball on, the strokes at the cheapest balls to hit that are tried.
constexpr int kLeastBad = 2;
// The aims a ball is taken as reachable by, in ball widths across it.
constexpr double kReachOffsets[] = {0.0, 0.5, -0.5, 0.95, -0.95};
// Cushion routes stay this far from a corner pocket's end of a cushion and from a middle pocket's
// centre, where the jaws are.
constexpr double kCornerGap_m = 0.13;
constexpr double kMiddleGap_m = 0.16;
// The break-off: past the end red of the back row by this share of a ball's width, at the pace
// that brings the cue ball back off the top and side cushions to rest in baulk. Thinner, its aim
// error misses the red one time in four; faster or fuller, the cue ball stops up the table.
constexpr double kBreakThin = 0.7;
constexpr double kBreakSpeed_m_s = 2.7;
// The reds are still the pack when all fifteen lie this near their middle.
constexpr double kPackReach_m = 0.16;
// A re-rack throws the scores away: it is taken only when they are no further apart than this.
constexpr int kRerackWithin = 20;

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
double wrap(double a) { return std::remainder(a, 2.0 * kPi); }
Vec2 turned(Vec2 a, double by) {
    const double c = std::cos(by);
    const double s = std::sin(by);
    return {a.x * c - a.y * s, a.x * s + a.y * c};
}

// The cue speeds it keeps to: inside the physics' limit, whatever that is.
double fastest(const phys::PhysicsParams& p) { return p.max_cue_speed_m_s > 0.0 ? p.max_cue_speed_m_s : kMinSpeed_m_s; }
double slowest(const phys::PhysicsParams& p) { return std::min(kMinSpeed_m_s, fastest(p)); }

bool contains(const std::vector<int>& places, int place) {
    return std::find(places.begin(), places.end(), place) != places.end();
}

bool valid_cue_ball(const Position& p) {
    return p.cue_ball >= 0 && p.cue_ball < int(p.balls.size()) &&
           p.balls[std::size_t(p.cue_ball)].ball == Ball::cue;
}

std::string label(const BallOnTable& b) {
    std::string out(table::ball_name(b.ball));
    if (b.ball == Ball::red) out += " " + std::to_string(b.index);
    return out;
}

// Where a pot is sent and from which side the pocket takes it.
struct PocketAim {
    Vec2 target;
    Vec2 axis;  // unit, out of the table through the pocket
    bool corner = true;
};

std::vector<PocketAim> pocket_aims(const phys::TableShape& t) {
    std::vector<PocketAim> out;
    for (const phys::Pocket& p : t.pockets) {
        const double sx = p.drop_centre.x > 0.0 ? 1.0 : -1.0;
        const double sy = p.drop_centre.y > 0.0 ? 1.0 : -1.0;
        const Vec2 axis = p.corner ? unit({sx, sy}) : Vec2{0.0, sy};
        out.push_back({p.drop_centre - axis * (p.drop_radius * (1.0 - kPotDepth)), axis, p.corner});
    }
    return out;
}

// The pockets of the rulebook's table, for a choice made without the outline: the corners of the
// playing area and the middles of its long sides.
std::vector<PocketAim> nominal_pocket_aims() {
    const double hx = table::kPlayingLength / 2.0;
    const double hy = table::kPlayingWidth / 2.0;
    std::vector<PocketAim> out;
    for (double sx : {1.0, 0.0, -1.0}) {
        for (double sy : {1.0, -1.0}) {
            const bool corner = sx != 0.0;
            out.push_back({{sx * hx, sy * hy}, corner ? unit({sx, sy}) : Vec2{0.0, sy}, corner});
        }
    }
    return out;
}

double distance_to_path(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double l2 = dot(ab, ab);
    const double t = l2 > 0.0 ? std::clamp(dot(p - a, ab) / l2, 0.0, 1.0) : 0.0;
    return length(p - (a + ab * t));
}

// No ball but `skip` and `also` has its centre within `clear` of the straight path from a to b.
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

// Some straight stroke from `cue` touches the ball at `place` before any other.
bool can_reach(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 cue, int place) {
    for (double offset : kReachOffsets) {
        Vec2 dir;
        Vec2 contact;
        if (contact_point(cue, balls[std::size_t(place)].pos, offset, dir, contact) &&
            path_clear(balls, cue, contact, cue_ball, place, kD)) {
            return true;
        }
    }
    return false;
}

// The balls a stroke may be played at: the balls on and, with FREE BALL called, every other
// object ball.
std::vector<int> targets_of(const Position& p) {
    std::vector<int> out = rules::places_on(p);
    if (!p.free_ball) return out;
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        if (int(i) != p.cue_ball && !contains(out, int(i))) out.push_back(int(i));
    }
    return out;
}

enum class Kind { pot, safety, escape, break_off };

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::pot: return "pot";
        case Kind::safety: return "safety";
        case Kind::escape: return "escape";
        case Kind::break_off: return "break";
    }
    return "safety";
}

// The line a ball's centre bounces on along one cushion.
struct Rail {
    bool across = true;  // the top and baulk cushions, which run along y
    double at = 0.0;     // x of the line for those two, y for the side cushions

    double along(Vec2 p) const { return across ? p.y : p.x; }
    // Whether a ball meeting it there meets a straight cushion and not a pocket.
    bool straight(double along_it) const {
        const double a = std::abs(along_it);
        if (across) return a <= table::kPlayingWidth / 2.0 - kCornerGap_m;
        return a >= kMiddleGap_m && a <= table::kPlayingLength / 2.0 - kCornerGap_m;
    }
};

std::vector<Rail> rails() {
    const double hx = table::kPlayingLength / 2.0 - table::kBallRadius;
    const double hy = table::kPlayingWidth / 2.0 - table::kBallRadius;
    return {{true, hx}, {true, -hx}, {false, hy}, {false, -hy}};
}

struct Candidate {
    Kind kind = Kind::safety;
    Vec2 cue;            // where the cue ball is played from
    double aim = 0.0;
    double speed = 0.0;
    int target = -1;     // the place of the ball played at
    int pocket = -1;
    double cut = 0.0;
    double cost = 0.0;   // within a kind, the lower the better
    Vec2 line;           // a pot: the direction the object ball is to leave in
    int nominate = -1;   // the place of the colour to declare when it is not the ball played at
};

void add_pots(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 cue, const std::vector<int>& targets,
              const std::vector<PocketAim>& pockets, const StandinParams& sp, double min_speed, double max_speed,
              std::vector<Candidate>& out) {
    for (int place : targets) {
        const Vec2 obj = balls[std::size_t(place)].pos;
        if (length(obj - cue) < kD + kTooClose_m) continue;
        for (std::size_t k = 0; k < pockets.size(); ++k) {
            const PocketAim& pocket = pockets[k];
            const double run = length(pocket.target - obj);
            if (run < 1.0e-6) continue;
            const Vec2 line = (pocket.target - obj) * (1.0 / run);
            if (dot(line, pocket.axis) < (pocket.corner ? 0.0 : kMiddleMinCos)) continue;
            const Vec2 ghost = obj - line * kD;
            const double travel = length(ghost - cue);
            if (travel < 1.0e-6) continue;
            const double cut = std::acos(std::clamp(dot(ghost - cue, line) / travel, -1.0, 1.0));
            if (cut > sp.max_cut_rad) continue;
            if (!path_clear(balls, cue, ghost, cue_ball, place, kD + kPathMargin_m)) continue;
            if (!path_clear(balls, obj, pocket.target, cue_ball, place, kD + kPathMargin_m)) continue;
            Candidate c;
            c.kind = Kind::pot;
            c.cue = cue;
            c.aim = angle_of(ghost - cue);
            // A cut leaves the object ball the cosine of the cue ball's pace.
            c.speed = std::clamp(sp.pot_speed_m_s * std::sqrt(travel + run) / std::max(std::cos(cut), 0.5),
                                 min_speed, max_speed);
            c.target = place;
            c.pocket = int(k);
            c.cut = cut;
            c.cost = (travel + run) / std::max(std::cos(cut), 0.05);
            c.line = line;
            out.push_back(c);
        }
    }
}

std::vector<int> nearest_first(const std::vector<BallOnTable>& balls, Vec2 cue, std::vector<int> places) {
    std::stable_sort(places.begin(), places.end(), [&](int a, int b) {
        return length(balls[std::size_t(a)].pos - cue) < length(balls[std::size_t(b)].pos - cue);
    });
    return places;
}

void add_safeties(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 cue, const std::vector<int>& targets,
                  double speed, std::vector<Candidate>& out) {
    int found = 0;
    for (int place : nearest_first(balls, cue, targets)) {
        const Vec2 obj = balls[std::size_t(place)].pos;
        bool any = false;
        int order = 0;
        for (double offset : kSafetyOffsets) {
            ++order;
            Vec2 dir;
            Vec2 contact;
            if (!contact_point(cue, obj, offset, dir, contact)) continue;
            if (!path_clear(balls, cue, contact, cue_ball, place, kD + kPathMargin_m)) continue;
            Candidate c;
            c.kind = Kind::safety;
            c.cue = cue;
            c.aim = angle_of(dir);
            c.speed = speed;
            c.target = place;
            c.cut = std::asin(std::clamp(std::abs(cross(dir, obj - contact)) / kD, 0.0, 1.0));
            c.cost = 10.0 * found + order;
            out.push_back(c);
            any = true;
        }
        if (any && ++found >= kSafetyBalls) break;
    }
}

// A straight stroke followed off the cushions as a mirror would return it, to the first ball it
// touches.
struct Route {
    int ball = -1;  // its place; -1 when the stroke meets none, or a pocket's jaws first
    int cushions = 0;
    Vec2 from;  // its last leg: where it starts and which way it goes
    Vec2 dir;
};

Route follow(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 from, Vec2 dir, const std::vector<Rail>& all) {
    const double inf = std::numeric_limits<double>::infinity();
    const double hx = all[0].at;
    const double hy = all[2].at;
    Route out;
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
        if (out.ball >= 0 || out.cushions == kMaxCushions || !(leg < inf)) return out;
        from = from + dir * leg;
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
    double width = 0.0;  // in radians
};

std::vector<Window> windows(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 cue) {
    const std::vector<Rail> all = rails();
    const double step = 2.0 * kPi / kFanSteps;
    const auto stroke = [&](double aim) { return follow(balls, cue_ball, cue, {std::cos(aim), std::sin(aim)}, all); };
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

bool pack_intact(const Position& p) {
    Vec2 middle;
    int reds = 0;
    for (const BallOnTable& b : p.balls) {
        if (b.ball != Ball::red) continue;
        middle = middle + b.pos;
        ++reds;
    }
    if (reds != 15) return false;
    middle = middle * (1.0 / reds);
    for (const BallOnTable& b : p.balls) {
        if (b.ball == Ball::red && length(b.pos - middle) > kPackReach_m) return false;
    }
    return true;
}

// A thin hit on the outside of the end red of the back row, on the cue ball's side of the table.
bool break_off(const Position& p, Vec2 cue, Candidate& out) {
    const double side = cue.y > 0.0 ? 1.0 : -1.0;
    double back = -std::numeric_limits<double>::infinity();
    for (const BallOnTable& b : p.balls) {
        if (b.ball == Ball::red) back = std::max(back, b.pos.x);
    }
    int target = -1;
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        const BallOnTable& b = p.balls[i];
        if (b.ball != Ball::red || b.pos.x < back - 0.005) continue;
        if (target < 0 || b.pos.y * side > p.balls[std::size_t(target)].pos.y * side) target = int(i);
    }
    if (target < 0) return false;
    const Vec2 red = p.balls[std::size_t(target)].pos;
    const Vec2 dir = unit(red - cue);
    Vec2 out_side{-dir.y, dir.x};
    if (out_side.y * side < 0.0) out_side = out_side * -1.0;
    out.kind = Kind::break_off;
    out.cue = cue;
    out.aim = angle_of(red + out_side * (kD * kBreakThin) - cue);
    out.speed = kBreakSpeed_m_s;
    out.target = target;
    out.cut = std::asin(kBreakThin);
    return true;
}

phys::TableState table_state(const std::vector<BallOnTable>& balls) {
    phys::TableState t;
    t.balls.reserve(balls.size());
    for (const BallOnTable& b : balls) {
        phys::BallState s;
        s.ball = b.ball;
        s.index = b.index;
        s.r = {b.pos.x, b.pos.y, 0.0};
        t.balls.push_back(s);
    }
    return t;
}

// What the referee made of a candidate played exactly as aimed.
struct Checked {
    bool clean = false;
    int penalty = 0;
    int points = 0;
    bool hit_target = false;  // the first impact was on the candidate's ball alone
    bool in_off = false;
};

class Planner {
public:
    Planner(const FrameState& s, const phys::TableShape& table, const phys::PhysicsParams& params,
            const rules::TableOracle& oracle, const rules::RefParams& ref, const StandinParams& sp)
        : s_(s), table_(table), params_(params), oracle_(oracle), ref_(ref), sp_(sp), work_(s) {
        cue_ball_ = s.cue_ball;
        in_.start = table_state(s.balls);
        in_.cue_ball = s.cue_ball;
        open_ = rules::nominations(s);
        speed_cap_ = fastest(params);
        speed_floor_ = slowest(params);
    }

    // Every stroke it would consider, the best first. Never empty.
    void list() {
        const std::vector<int> on = rules::places_on(s_);
        const std::vector<int> targets = targets_of(s_);
        const std::vector<PocketAim> pockets = pocket_aims(table_);
        const std::vector<Vec2> places = cue_places();
        const double safety_speed = std::clamp(sp_.safety_speed_m_s, speed_floor_, speed_cap_);

        Candidate opening;
        if (s_.cue_in_hand && s_.on.kind == OnKind::red && pack_intact(s_) &&
            break_off(s_, places.front(), opening)) {
            opening.speed = std::min(opening.speed, speed_cap_);
            candidates_.push_back(opening);
        }

        std::vector<Candidate> pots;
        std::vector<Candidate> safeties;
        for (Vec2 cue : places) {
            add_pots(s_.balls, cue_ball_, cue, targets, pockets, sp_, speed_floor_, speed_cap_, pots);
            add_safeties(s_.balls, cue_ball_, cue, targets, safety_speed, safeties);
        }
        const auto by_cost = [](const Candidate& a, const Candidate& b) { return a.cost < b.cost; };
        std::stable_sort(pots.begin(), pots.end(), by_cost);
        std::stable_sort(safeties.begin(), safeties.end(), by_cost);
        candidates_.insert(candidates_.end(), pots.begin(), pots.end());

        // S3.8: from a touching ball that is on, playing away from it is a stroke at it.
        if (!s_.cue_in_hand) {
            for (int place : s_.touching) {
                if (place < 0 || place >= int(s_.balls.size()) || place == cue_ball_) continue;
                Candidate c;
                c.cue = places.front();
                c.aim = angle_of(c.cue - s_.balls[std::size_t(place)].pos);
                c.speed = safety_speed;
                c.target = place;
                candidates_.push_back(c);
            }
        }
        candidates_.insert(candidates_.end(), safeties.begin(), safeties.end());

        if (pots.empty() && safeties.empty()) {
            std::vector<Candidate> escapes;
            std::vector<Candidate> least_bad;
            for (Vec2 cue : places) {
                set_cue(cue);
                // S3.14(a)(ii): what the referee takes as impossible to hit is not looked for.
                if (!oracle_.can_hit(work_.balls, cue_ball_, on)) continue;
                add_escapes(cue, targets, safety_speed, escapes, least_bad);
                if (!escapes.empty()) break;
            }
            std::stable_sort(escapes.begin(), escapes.end(), by_cost);
            std::stable_sort(least_bad.begin(), least_bad.end(), by_cost);
            if (int(least_bad.size()) > kLeastBad) least_bad.resize(std::size_t(kLeastBad));
            candidates_.insert(candidates_.end(), escapes.begin(), escapes.end());
            candidates_.insert(candidates_.end(), least_bad.begin(), least_bad.end());
        }

        // Failing all, straight at the nearest ball on.
        Candidate last;
        last.kind = pots.empty() && safeties.empty() ? Kind::escape : Kind::safety;
        last.cue = places.front();
        last.speed = safety_speed;
        const std::vector<int> nearest = nearest_first(s_.balls, last.cue, on.empty() ? targets : on);
        if (!nearest.empty()) {
            last.target = nearest.front();
            last.aim = angle_of(s_.balls[std::size_t(last.target)].pos - last.cue);
        }
        candidates_.push_back(last);
    }

    // Played as aimed: the first that is no foul and, for a pot, scores; else the first with no
    // foul; else the lowest penalty; else (nothing simulated) the first. `penalty` is what it is
    // expected to cost, -1 when it was not simulated.
    Candidate choose(int& penalty) {
        const int budget = std::max(sp_.max_sims, 0);
        // Pots that stay out leave two simulations for a safety.
        const int pot_budget = budget > 2 ? budget - 2 : budget;
        bool have_clean = false;
        Candidate clean;
        bool have_foul = false;
        Candidate foul;
        int least = 0;
        const auto fouled = [&](const Candidate& c, const Checked& r) {
            if (have_foul && r.penalty >= least) return;
            have_foul = true;
            foul = c;
            least = r.penalty;
        };

        const auto wanted = [](const Candidate& c, const Checked& r) {
            return r.clean && (c.kind != Kind::pot || r.points > 0);
        };

        for (const Candidate& listed : candidates_) {
            if (sims_ >= budget) break;
            if (listed.kind == Kind::pot && sims_ >= pot_budget) continue;
            Candidate c = listed;
            phys::ShotRecord record;
            Checked r = check(c, record);
            if (wanted(c, r)) {
                penalty = 0;
                return c;
            }
            // A pot that stays out is tried again with the throw allowed for, and one the cue ball
            // follows in is tried softer: a centre-ball strike has no other way to hold it back.
            for (int retry = 0; c.kind == Kind::pot && retry < kPotRetries && sims_ < budget; ++retry) {
                Candidate again = c;
                if (r.in_off) {
                    again.speed = std::max(c.speed * kSofter, speed_floor_);
                    if (again.speed == c.speed) break;
                } else if (!(r.clean && r.hit_target && allow_for_throw(record, again))) {
                    break;
                }
                const Checked second = check(again, record);
                if (wanted(again, second)) {
                    penalty = 0;
                    return again;
                }
                if (!second.clean && !(second.in_off && r.in_off)) break;
                c = again;
                r = second;
            }
            if (r.clean) {
                if (!have_clean) clean = c;
                have_clean = true;
            } else {
                fouled(c, r);
            }
        }
        if (have_clean) {
            penalty = 0;
            return clean;
        }
        if (have_foul) {
            penalty = least;
            return foul;
        }
        penalty = -1;
        return candidates_.front();
    }

    rules::ShotInput declaration(const Candidate& c) const {
        const int place = c.nominate >= 0 ? c.nominate : c.target;
        if (place < 0 || place >= int(s_.balls.size())) return {};
        const Ball b = s_.balls[std::size_t(place)].ball;
        if (std::find(open_.begin(), open_.end(), b) == open_.end()) return {};
        return {true, b};
    }

    int candidates() const { return int(candidates_.size()); }
    int sims() const { return sims_; }
    // The stroke as it is played: the candidate with the errors added, the cue as low as it can lie.
    phys::CueStrike strike(const Candidate& c, double aim_error, double speed_error) {
        phys::CueStrike out;
        out.aim_rad = wrap(c.aim + aim_error);
        out.speed_m_s = std::clamp(c.speed * (1.0 + speed_error), speed_floor_, speed_cap_);
        set_cue(c.cue);
        out.elevation_rad = phys::min_cue_elevation(in_.start, cue_ball_, out.aim_rad, table_, params_);
        return out;
    }

private:
    void set_cue(Vec2 cue) {
        work_.balls[std::size_t(cue_ball_)].pos = cue;
        in_.start.balls[std::size_t(cue_ball_)].r = {cue.x, cue.y, 0.0};
    }

    // Escapes from `cue`: the windows of the fan that reach a ball it may play at, the widest and
    // most direct first, each aimed again for the cushions as they are. Beside them, for when none
    // works, the straight strokes at the balls that cost least to hit.
    void add_escapes(Vec2 cue, const std::vector<int>& targets, double speed, std::vector<Candidate>& escapes,
                     std::vector<Candidate>& least_bad) {
        // S3.11(d): with a colour of choice on, the penalty is at least the declared colour's value.
        int lowest = -1;
        if (s_.on.kind == OnKind::any_colour) {
            for (int place : targets) {
                if (lowest < 0 || table::ball_value(s_.balls[std::size_t(place)].ball) <
                                      table::ball_value(s_.balls[std::size_t(lowest)].ball)) {
                    lowest = place;
                }
            }
        }
        std::vector<Window> all = windows(s_.balls, cue_ball_, cue);
        const auto cost = [](const Window& w) { return (w.route.cushions + 1) / w.width; };
        std::stable_sort(all.begin(), all.end(), [&](const Window& a, const Window& b) { return cost(a) < cost(b); });
        for (const Window& w : all) {
            Candidate c;
            c.kind = Kind::escape;
            c.cue = cue;
            c.aim = w.aim;
            c.speed = speed;
            c.target = w.route.ball;
            if (contains(targets, w.route.ball)) {
                if (int(escapes.size()) >= kEscapes) continue;
                if (w.route.cushions > 0 && !aim_off_cushions(w, cue, speed, c.aim)) continue;
                c.cost = cost(w);
                escapes.push_back(c);
            } else if (w.route.cushions == 0) {
                c.nominate = lowest;
                c.cost = table::ball_value(s_.balls[std::size_t(w.route.ball)].ball) - std::min(w.width, 0.5);
                least_bad.push_back(c);
            }
        }
    }

    // The cue ball's course for a stroke with nothing else on the table: the first ball of the
    // frame it would touch (-1 for none), and how far to the left of its line `ball` lies as it
    // passes it on the leg after `cushions` cushions. False when it never plays that leg.
    bool course(Vec2 cue, double aim, double speed, int cushions, int ball, int& first, double& passes) {
        set_cue(cue);
        phys::SimInput alone;
        alone.start.balls.push_back(in_.start.balls[std::size_t(cue_ball_)]);
        alone.cue_ball = 0;
        alone.strike.aim_rad = aim;
        alone.strike.speed_m_s = speed;
        // The cue as the balls about it let it lie, so the cue ball leaves as it will in the stroke.
        alone.strike.elevation_rad = phys::min_cue_elevation(in_.start, cue_ball_, aim, table_, params_);
        const phys::ShotRecord record = phys::simulate(alone, table_, params_);
        ++courses_;

        std::vector<double> met;
        for (const phys::ShotEvent& e : record.events) {
            if (e.kind == phys::EventKind::ball_line || e.kind == phys::EventKind::ball_arc) met.push_back(e.t);
        }
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
            for (double share : {1.0 / 3.0, 2.0 / 3.0}) {
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
        double nearest = std::numeric_limits<double>::infinity();
        const Vec2 target = s_.balls[std::size_t(ball)].pos;
        for (std::size_t i = 0; i + 1 < at.size(); ++i) {
            const double piece = length(at[i + 1] - at[i]);
            if (!(piece > 0.0)) continue;
            const Vec2 dir = (at[i + 1] - at[i]) * (1.0 / piece);
            if (first < 0) {
                double reach = piece;
                for (std::size_t k = 0; k < s_.balls.size(); ++k) {
                    if (int(k) == cue_ball_) continue;
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
            const Vec2 foot = at[i] + dir * std::clamp(dot(target - at[i], dir), 0.0, piece);
            if (length(target - foot) < nearest) {
                nearest = length(target - foot);
                passes = cross(dir, target - foot);
                passed = true;
            }
        }
        return passed;
    }

    // Turns the middle of a window found by mirrors until the cue ball, simulated alone, meets
    // the ball where the mirror's stroke would have. False when it cannot be brought to it.
    bool aim_off_cushions(const Window& w, Vec2 cue, double speed, double& aim) {
        const int ball = w.route.ball;
        const double wanted = cross(w.route.dir, s_.balls[std::size_t(ball)].pos - w.route.from);
        double at = w.aim;
        bool found = false;
        for (int turn = 0;; ++turn) {
            int first = -1;
            double passes = 0.0;
            if (courses_ >= kCourses || !course(cue, at, speed, w.route.cushions, ball, first, passes)) break;
            if (first == ball) {
                aim = at;
                found = true;
            }
            if (std::abs(passes - wanted) <= kCloseEnough_m || turn == kTurns) break;
            int probed_first = -1;
            double probed = 0.0;
            if (courses_ >= kCourses ||
                !course(cue, at + kProbe_rad, speed, w.route.cushions, ball, probed_first, probed)) {
                break;
            }
            const double per_rad = (probed - passes) / kProbe_rad;
            if (!(std::abs(per_rad) > 1.0e-3)) break;
            at += std::clamp((wanted - passes) / per_rad, -kMaxTurn_rad, kMaxTurn_rad);
        }
        return found;
    }

    // The D's legal places nearest a handful of wanted ones when in hand, else where it lies.
    std::vector<Vec2> cue_places() const {
        const Vec2 here = s_.balls[std::size_t(cue_ball_)].pos;
        if (!s_.cue_in_hand) return {here};
        const double line = table::baulk_line_x();
        // The usual break-off place (between brown and yellow), its mirror between brown and
        // green, near either end of the D, and half way back behind the brown; each 2 cm off the
        // baulk line, which the colours stand on.
        const Vec2 wanted[] = {table::default_cue_ball_position(),
                               {line - 0.02, 0.146},
                               {line - 0.02, -0.26},
                               {line - 0.02, 0.26},
                               {line - 0.15, 0.0}};
        std::vector<Vec2> out;
        for (Vec2 want : wanted) {
            const Vec2 p = oracle_.nearest_cue_placement(s_.balls, cue_ball_, want);
            if (!oracle_.cue_placement_legal(s_.balls, cue_ball_, p)) continue;
            bool fresh = true;
            for (Vec2 q : out) fresh = fresh && length(p - q) > 1.0e-3;
            if (fresh) out.push_back(p);
        }
        if (out.empty()) out.push_back(here);
        return out;
    }

    Checked check(const Candidate& c, phys::ShotRecord& record) {
        set_cue(c.cue);
        in_.strike = phys::CueStrike{};
        in_.strike.aim_rad = c.aim;
        in_.strike.speed_m_s = c.speed;
        record = phys::simulate(in_, table_, params_);
        const rules::ShotFacts facts = rules::observe(work_, record, table_, ref_);
        const rules::Judgement j = rules::judge(work_, declaration(c), facts, oracle_, ref_);
        ++sims_;
        Checked out;
        out.clean = j.fouls.empty();
        out.penalty = j.penalty;
        out.points = j.points;
        out.hit_target = facts.first_hit.size() == 1 && facts.first_hit.front() == c.target;
        out.in_off = facts.cue_potted;
        return out;
    }

    // The cue ball's first event from `from` on of the kinds that change its course, -1 when it
    // met nothing more.
    int next_meeting(const phys::ShotRecord& record, int from) const {
        for (std::size_t i = std::size_t(from); i < record.events.size(); ++i) {
            const phys::ShotEvent& e = record.events[i];
            const bool meets = e.kind == phys::EventKind::ball_ball || e.kind == phys::EventKind::ball_line ||
                               e.kind == phys::EventKind::ball_arc || e.kind == phys::EventKind::pocket;
            const bool its_own = e.a == cue_ball_ || (e.kind == phys::EventKind::ball_ball && e.b == cue_ball_);
            if (meets && its_own) return int(i);
        }
        return -1;
    }

    // A cut throws the object ball off the line of centres. The pot was simulated: turn the line
    // of centres back by how far the ball left off its line. False when it left on it.
    bool allow_for_throw(const phys::ShotRecord& record, Candidate& c) const {
        const int first = next_meeting(record, 0);
        if (first < 0 || c.target < 0 || c.target >= int(record.segments.size())) return false;
        const double t = record.events[std::size_t(first)].t;
        for (const phys::Segment& seg : record.segments[std::size_t(c.target)]) {
            if (seg.t0 < t - 1.0e-9) continue;
            const Vec2 v{seg.start.v.x, seg.start.v.y};
            if (length(v) <= 0.0) return false;
            const double off = wrap(angle_of(v) - angle_of(c.line));
            // Throw is a few degrees at most; a ball 17 degrees or more off its line was not sent
            // by a clean contact (a kiss, a double hit), and turning the aim would not help.
            if (std::abs(off) < 1.0e-4 || std::abs(off) > 0.3) return false;
            const Vec2 obj = s_.balls[std::size_t(c.target)].pos;
            const Vec2 ghost = obj - turned(c.line, -off) * kD;
            c.aim = angle_of(ghost - c.cue);
            return true;
        }
        return false;
    }

    const FrameState& s_;
    const phys::TableShape& table_;
    const phys::PhysicsParams& params_;
    const rules::TableOracle& oracle_;
    const rules::RefParams& ref_;
    const StandinParams& sp_;
    FrameState work_;    // the frame with the cue ball where the candidate in hand plays from
    phys::SimInput in_;
    int cue_ball_ = 0;
    std::vector<Ball> open_;
    double speed_cap_ = 0.0;
    double speed_floor_ = 0.0;
    std::vector<Candidate> candidates_;
    int sims_ = 0;
    int courses_ = 0;  // simulations of the cue ball alone
};

int reds_left(const Position& p) {
    int reds = 0;
    for (const BallOnTable& b : p.balls) reds += b.ball == Ball::red ? 1 : 0;
    return reds;
}

bool offered(const std::vector<DecisionKind>& options, DecisionKind kind) {
    return std::find(options.begin(), options.end(), kind) != options.end();
}

// A straight stroke reaches some ball the position's striker may play at.
bool has_clear_path(const Position& p) {
    if (!valid_cue_ball(p)) return false;
    // S3.8: a touching ball that is on is played at by playing away from it.
    if (p.cue_in_hand || !p.touching.empty()) return true;
    const Vec2 cue = p.balls[std::size_t(p.cue_ball)].pos;
    for (int place : targets_of(p)) {
        if (can_reach(p.balls, p.cue_ball, cue, place)) return true;
    }
    return false;
}

bool has_pot(const Position& p, const StandinParams& sp) {
    if (!valid_cue_ball(p)) return false;
    if (p.cue_in_hand) return true;
    // Only whether one is on: the speed it would be played at does not matter.
    std::vector<Candidate> pots;
    const Vec2 cue = p.balls[std::size_t(p.cue_ball)].pos;
    add_pots(p.balls, p.cue_ball, cue, targets_of(p), nominal_pocket_aims(), sp, 1.0, 1.0, pots);
    return !pots.empty();
}

// The plainest stroke the engine accepts, for when nothing better can be worked out: along the
// table from where the cue ball lies, the lowest colour declared when one must be.
StandinShot plain_stroke(const FrameState& s, const phys::TableShape& table, const phys::PhysicsParams& params,
                         const StandinParams& sp) {
    StandinShot shot;
    shot.why = nlohmann::json::object();
    shot.strike.speed_m_s = std::clamp(sp.safety_speed_m_s, slowest(params), fastest(params));
    if (!valid_cue_ball(s)) return shot;
    shot.places_cue = s.cue_in_hand;
    shot.cue_pos = s.balls[std::size_t(s.cue_ball)].pos;
    shot.strike.elevation_rad = phys::min_cue_elevation(table_state(s.balls), s.cue_ball, 0.0, table, params);
    if (s.on.kind == OnKind::any_colour) {
        const std::vector<Ball> open = rules::nominations(s);
        if (!open.empty()) shot.input = {true, open.front()};
    }
    return shot;
}

// The callers get a fallback, but a throw is a fault to fix: app.error, with the match the scoped
// log context names.
void exception_logged(const char* function, const std::exception& e) {
    nlohmann::json data = {{"kind", "exception"}, {"function", function}, {"message", e.what()}};
    const nlohmann::json& context = rules::scoped_log_context();
    for (const char* key : {"match_id", "frame"}) {
        if (context.contains(key)) data[key] = context[key];
    }
    log::error("app", "app.error", std::move(data));
}

}  // namespace

const std::vector<std::string>& standin_param_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const Field& field : fields()) out.emplace_back(field.name);
        return out;
    }();
    return names;
}

bool load_standin_params(const nlohmann::json& file, StandinParams& out, std::vector<std::string>& problems) {
    const std::size_t before = problems.size();
    const auto report = [&](const std::string& key, const nlohmann::json& value, const std::string& reason) {
        problems.push_back(key + ": " + reason);
        log::error("params", "params.invalid", {{"key", key}, {"value", value}, {"reason", reason}});
    };
    if (!file.is_object() || !file.contains("values")) {
        report("values", nullptr, "missing");
        return false;
    }
    const nlohmann::json& values = file["values"];
    if (!values.is_object()) {
        report("values", values, "not an object");
        return false;
    }
    for (const Field& field : fields()) {
        const auto it = values.find(field.name);
        if (it == values.end()) {
            report(field.name, nullptr, "missing");
            continue;
        }
        std::string problem;
        if (!apply(field, *it, out, problem)) report(field.name, *it, problem);
    }
    for (const auto& [key, value] : values.items()) {
        bool known = false;
        for (const Field& field : fields()) known = known || key == field.name;
        if (!known) report(key, value, "unknown key");
    }
    return problems.size() == before;
}

StandinShot choose_shot(const rules::FrameState& s, const phys::TableShape& table, const phys::PhysicsParams& params,
                        const rules::TableOracle& oracle, const rules::RefParams& ref, const StandinParams& sp,
                        Rng& rng) {
    StandinShot shot = plain_stroke(s, table, params, sp);
    try {
        if (s.striker == 0 || s.striker == 1) {
            const std::vector<DecisionKind> open = rules::options(s, s.striker);
            if (offered(open, DecisionKind::claim_frame)) {
                shot.decides = true;
                shot.decision = DecisionKind::claim_frame;
                shot.why = {{"kind", "claim"}};
                return shot;
            }
            const int required = rules::penalty_points_required(s, s.striker);
            if (offered(open, DecisionKind::concede_frame) && reds_left(s) == 0 &&
                required >= sp.concede_required_points) {
                shot.decides = true;
                shot.decision = DecisionKind::concede_frame;
                shot.why = {{"kind", "concede"}, {"required", required}};
                return shot;
            }
        }
        if (!valid_cue_ball(s)) {
            shot.why = {{"kind", "safety"}, {"error", "the frame has no cue ball"}};
            return shot;
        }

        Planner plan(s, table, params, oracle, ref, sp);
        plan.list();
        int penalty = -1;
        const Candidate c = plan.choose(penalty);

        const double aim_error = sp.aim_error_rad * rng.normal();
        const double speed_error = sp.speed_error * rng.normal();
        shot.places_cue = s.cue_in_hand;
        shot.cue_pos = c.cue;
        shot.input = plan.declaration(c);
        shot.strike = plan.strike(c, aim_error, speed_error);
        shot.why = {{"kind", kind_name(c.kind)},
                    {"target", c.target >= 0 ? label(s.balls[std::size_t(c.target)]) : std::string()},
                    {"pocket", c.pocket},
                    {"cut_rad", c.cut},
                    {"candidates", plan.candidates()},
                    {"sims", plan.sims()},
                    {"penalty", penalty},
                    {"aim_error_rad", aim_error},
                    {"speed_error", speed_error}};
    } catch (const std::exception& e) {
        // Whatever went wrong, the caller still gets a stroke the engine accepts.
        exception_logged("choose_shot", e);
        shot = plain_stroke(s, table, params, sp);
        shot.why = {{"kind", "safety"}, {"error", e.what()}};
    }
    return shot;
}

rules::DecisionKind choose_decision(const rules::FrameState& s, int /*player*/,
                                    const std::vector<rules::DecisionKind>& options, const StandinParams& sp, Rng&,
                                    nlohmann::json* why) {
    nlohmann::json weighed = nlohmann::json::object();
    const auto taken = [&](DecisionKind kind, const char* rule) {
        weighed["rule"] = rule;
        if (why != nullptr) *why = std::move(weighed);
        return kind;
    };
    if (options.empty()) return taken(DecisionKind::play, "first_option");
    try {
        nlohmann::json names = nlohmann::json::array();
        for (const DecisionKind k : options) names.push_back(rules::decision_name(k));
        weighed["options"] = std::move(names);
        weighed["scores"] = {s.scores[0], s.scores[1]};
        weighed["points_remaining"] = rules::points_remaining(s);
        if (offered(options, DecisionKind::play)) {
            // A miss from a snooker goes back: the same escape must be found again.
            if (offered(options, DecisionKind::replace)) {
                const bool clear_before = has_clear_path(s.original);
                weighed["clear_path_before"] = clear_before;
                if (!clear_before) return taken(DecisionKind::replace, "replace");
            }
            if (offered(options, DecisionKind::play_again)) {
                const bool pot = has_pot(s, sp);
                const bool clear = pot || has_clear_path(s);
                weighed["pot"] = pot;
                if (!pot) weighed["clear_path"] = clear;
                if (!clear) return taken(DecisionKind::play_again, "play_again");
            }
            return taken(DecisionKind::play, "play");
        }
        if (offered(options, DecisionKind::accept_rerack)) {
            const int gap = std::abs(s.scores[0] - s.scores[1]);
            weighed["score_gap"] = gap;
            weighed["rerack_within"] = kRerackWithin;
            if (gap <= kRerackWithin) return taken(DecisionKind::accept_rerack, "accept_rerack");
        }
        if (offered(options, DecisionKind::refuse_rerack)) return taken(DecisionKind::refuse_rerack, "refuse_rerack");
        if (offered(options, DecisionKind::play_first)) return taken(DecisionKind::play_first, "play_first");
    } catch (const std::exception& e) {
        exception_logged("choose_decision", e);
        weighed["error"] = e.what();
    }
    return taken(options.front(), "first_option");
}

}  // namespace ofs::opponent
