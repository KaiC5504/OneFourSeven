// The geometry oracle. Every path is the path of the cue ball's centre: a ball is in the way of
// one when its centre comes within a ball's diameter of it, and the cue ball touches a ball where
// its centre is a diameter from that ball's.

#include "ofs/rules/oracle.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

#include "spotting.hpp"

namespace ofs::rules {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kContact = table::kBallDiameter;  // centre to centre when two balls touch
// A path that exactly grazes a ball is not obstructed by it.
constexpr double kGraze = 1.0e-12;
constexpr double kInTheWay2 = (kContact - kGraze) * (kContact - kGraze);
// S2.17(b)(ii): obstructing balls this nearly the same distance from the cue ball are equidistant.
constexpr double kEquidistant = 1.0e-9;
// A gap between obstructing balls narrower than this, as an angle at the cue ball, is rounding.
constexpr double kMinGapRad = 1.0e-9;
// can_hit aims at this many points across each ball on, out to this share of its half width.
constexpr int kAimPoints = 15;
constexpr double kAimSpread = 0.99;
// Limits on two parameters, so that a bad value cannot stall the referee: the cushion routes
// triple with each further cushion, and the in-hand samples grow as the step shrinks squared.
constexpr int kMaxCushions = 5;
constexpr double kMinSampleStep = 1.0e-3;

enum Side : int { kTop, kBaulk, kLeft, kRight };

// A straight stretch of cushion nose: on kTop and kBaulk from y = lo to hi, on the sides from x.
struct Run {
    int side = kTop;
    double lo = 0.0;
    double hi = 0.0;
};

struct Obj {
    Vec2 pos;
    int place = 0;
    bool red = false;
};

// The object balls of a ball list: the balls on first, then the rest, each in place order.
struct Scene {
    std::vector<Obj> objs;
    std::size_t n_on = 0;
    bool has_cue = false;
    Vec2 cue;
};

Scene make_scene(const std::vector<BallOnTable>& balls, int cue_ball, const std::vector<int>& on) {
    Scene s;
    const int n = static_cast<int>(balls.size());
    s.has_cue = cue_ball >= 0 && cue_ball < n;
    if (s.has_cue) s.cue = balls[std::size_t(cue_ball)].pos;
    std::vector<char> is_on(balls.size(), 0);
    for (int place : on) {
        if (place >= 0 && place < n && place != cue_ball) is_on[std::size_t(place)] = 1;
    }
    s.objs.reserve(balls.size());
    for (char pass : {char(1), char(0)}) {
        for (int i = 0; i < n; ++i) {
            if (i == cue_ball || is_on[std::size_t(i)] != pass) continue;
            const BallOnTable& b = balls[std::size_t(i)];
            s.objs.push_back({b.pos, i, b.ball == Ball::red});
        }
        if (pass == 1) s.n_on = s.objs.size();
    }
    return s;
}

double dist2(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// To (-pi, pi], for the difference of two atan2 results.
double wrap(double a) {
    if (a > kPi) return a - 2.0 * kPi;
    if (a <= -kPi) return a + 2.0 * kPi;
    return a;
}

// Whether a ball at `k` is in the way of the cue ball's centre going `len` from `from` along the
// unit `dir`. A ball level with the start or behind it never is: the cue ball leaves it, which
// matters for one it rests against, a hair inside touching.
bool in_the_way(Vec2 from, Vec2 dir, double len, Vec2 k) {
    const double wx = k.x - from.x;
    const double wy = k.y - from.y;
    const double along = wx * dir.x + wy * dir.y;
    if (along <= 0.0) return false;
    if (along <= len) {
        const double side = wx * dir.y - wy * dir.x;
        return side * side < kInTheWay2;
    }
    const double ex = wx - len * dir.x;
    const double ey = wy - len * dir.y;
    return ex * ex + ey * ey < kInTheWay2;
}

// How far the cue ball's centre goes from `from` along the unit `dir` before it touches the ball
// at `k`, for a direction that does lead to it.
double reach(Vec2 from, Vec2 dir, Vec2 k) {
    const double wx = k.x - from.x;
    const double wy = k.y - from.y;
    const double along = wx * dir.x + wy * dir.y;
    const double side = wx * dir.y - wy * dir.x;
    return std::max(0.0, along - std::sqrt(std::max(0.0, kContact * kContact - side * side)));
}

// S2.17: the two straight paths from `c` that end touching the ball at `b` at its extreme edges.
struct Edges {
    Vec2 dir[2];
    double len = 0.0;
};

// False when the cue ball is already as close to `b` as touching it.
bool extreme_edges(Vec2 c, Vec2 b, Edges& e) {
    const double vx = b.x - c.x;
    const double vy = b.y - c.y;
    const double d2 = vx * vx + vy * vy;
    if (!(d2 > kContact * kContact)) return false;
    const double d = std::sqrt(d2);
    const double sin_a = kContact / d;
    const double cos_a = std::sqrt(1.0 - sin_a * sin_a);
    const double ux = vx / d;
    const double uy = vy / d;
    e.dir[0] = {ux * cos_a - uy * sin_a, uy * cos_a + ux * sin_a};
    e.dir[1] = {ux * cos_a + uy * sin_a, uy * cos_a - ux * sin_a};
    e.len = d * cos_a;
    return true;
}

// The ball on at `b` can be hit at both extreme edges from `c`. One the cue ball already touches
// can be hit.
bool both_edges_free(const Scene& s, Vec2 c, Vec2 b) {
    Edges e;
    if (!extreme_edges(c, b, e)) return true;
    for (const Vec2& dir : e.dir) {
        for (std::size_t k = s.n_on; k < s.objs.size(); ++k) {
            if (in_the_way(c, dir, e.len, s.objs[k].pos)) return false;
        }
    }
    return true;
}

bool snookered_from(const Scene& s, Vec2 c) {
    for (std::size_t i = 0; i < s.n_on; ++i) {
        if (both_edges_free(s, c, s.objs[i].pos)) return false;
    }
    return true;
}

// A ball as the cue ball sees it: how far, in which direction, and the half angle of the
// directions that lead to it (a quarter turn for one it touches).
struct Seen {
    double d = 0.0;
    double theta = 0.0;
    double alpha = 0.0;
};

Seen see(Vec2 c, Vec2 b) {
    Seen v;
    v.d = std::sqrt(dist2(c, b));
    v.theta = std::atan2(b.y - c.y, b.x - c.x);
    v.alpha = v.d > kContact ? std::asin(kContact / v.d) : kPi / 2.0;
    return v;
}

// S3.14(c): some direction from `c` in which the first ball touched is a ball on. Exact: for each
// ball on, the directions leading to it less those in which a ball not on is touched no later.
bool clear_path_from(const Scene& s, Vec2 c) {
    const std::size_t n = s.objs.size();
    std::vector<Seen> off(n - s.n_on);
    for (std::size_t k = s.n_on; k < n; ++k) off[k - s.n_on] = see(c, s.objs[k].pos);
    std::vector<std::pair<double, double>> hidden;

    for (std::size_t i = 0; i < s.n_on; ++i) {
        const Vec2 b = s.objs[i].pos;
        const Seen sb = see(c, b);
        const double furthest = sb.d > kContact ? std::sqrt(sb.d * sb.d - kContact * kContact) : 0.0;
        hidden.clear();
        for (std::size_t k = s.n_on; k < n; ++k) {
            const Seen& sk = off[k - s.n_on];
            if (sk.d - kContact > furthest) continue;
            // Angles from here on are measured from the direction of `b`.
            const double mid = wrap(sk.theta - sb.theta);
            const double lo = std::max(mid - sk.alpha, -sb.alpha);
            const double hi = std::min(mid + sk.alpha, sb.alpha);
            if (!(lo < hi)) continue;
            // Which of the two is touched first can change only in the directions of the two
            // places where the cue ball would touch both at once.
            const Vec2 q = s.objs[k].pos;
            double cuts[4] = {lo, hi, hi, hi};
            int pieces = 1;
            const double e2 = dist2(b, q);
            if (e2 > 0.0 && e2 < 4.0 * kContact * kContact) {
                const double e = std::sqrt(e2);
                const double h = std::sqrt(kContact * kContact - e2 / 4.0);
                const double nx = -(q.y - b.y) / e;
                const double ny = (q.x - b.x) / e;
                for (double sign : {1.0, -1.0}) {
                    const double px = (b.x + q.x) / 2.0 + sign * h * nx;
                    const double py = (b.y + q.y) / 2.0 + sign * h * ny;
                    const double a = wrap(std::atan2(py - c.y, px - c.x) - sb.theta);
                    if (a > lo && a < hi) cuts[pieces++] = a;
                }
                if (pieces == 3 && cuts[1] > cuts[2]) std::swap(cuts[1], cuts[2]);
                cuts[pieces] = hi;
            }
            for (int j = 0; j < pieces; ++j) {
                const double a = sb.theta + (cuts[j] + cuts[j + 1]) / 2.0;
                const Vec2 dir{std::cos(a), std::sin(a)};
                if (reach(c, dir, q) <= reach(c, dir, b)) hidden.push_back({cuts[j], cuts[j + 1]});
            }
        }
        // Anything of `b` left showing: a gap before a hidden stretch, or after the last.
        std::sort(hidden.begin(), hidden.end());
        double covered_to = -sb.alpha;
        for (const std::pair<double, double>& h : hidden) {
            if (h.first - covered_to > kMinGapRad) return true;
            covered_to = std::max(covered_to, h.second);
        }
        if (sb.alpha - covered_to > kMinGapRad) return true;
    }
    return false;
}

Vec2 mirror(Vec2 p, int side, double half_x, double half_y) {
    switch (side) {
        case kTop: return {2.0 * half_x - p.x, p.y};
        case kBaulk: return {-2.0 * half_x - p.x, p.y};
        case kLeft: return {p.x, 2.0 * half_y - p.y};
        default: return {p.x, -2.0 * half_y - p.y};
    }
}

// The points where two circles cross, given to `use`.
template <typename Use>
void circle_crossings(Vec2 a, double ra, Vec2 b, double rb, Use&& use) {
    const double d2 = dist2(a, b);
    if (!(d2 > 0.0)) return;
    const double d = std::sqrt(d2);
    const double along = (d2 + ra * ra - rb * rb) / (2.0 * d);
    const double h2 = ra * ra - along * along;
    if (!(h2 >= 0.0)) return;
    const double h = std::sqrt(h2);
    const double ux = (b.x - a.x) / d;
    const double uy = (b.y - a.y) / d;
    use(Vec2{a.x + along * ux - h * uy, a.y + along * uy + h * ux});
    use(Vec2{a.x + along * ux + h * uy, a.y + along * uy - h * ux});
}

// The D drawn kJustClear inside its lines, so that a place on its edge is still in the D after
// rounding: the baulk line's x, the arc's radius, and half the length of the straight edge.
struct InnerD {
    Vec2 centre = table::d_centre();
    double line = table::baulk_line_x() - kJustClear;
    double radius = table::kDRadius - kJustClear;
    double half = std::sqrt(radius * radius - kJustClear * kJustClear);
};

Vec2 into_d(const InnerD& d, Vec2 p) {
    Vec2 a = p;
    const double from_centre = std::sqrt(dist2(p, d.centre));
    if (from_centre > d.radius) {
        a = {d.centre.x + (p.x - d.centre.x) * d.radius / from_centre,
             d.centre.y + (p.y - d.centre.y) * d.radius / from_centre};
    }
    if (a.x <= d.line) return a;
    const Vec2 b{std::min(p.x, d.line), p.y};
    if (dist2(b, d.centre) <= d.radius * d.radius) return b;
    return {d.line, p.y >= 0.0 ? d.half : -d.half};
}

// S2.17(a): points of the D no more than `step` apart, its lines included, each given to `use`
// until it answers false.
template <typename Use>
void each_d_point(double step, double fallback, Use&& use) {
    // A step the loader would have refused must not hang the referee.
    const double h = std::max(step > 0.0 ? step : fallback, kMinSampleStep);
    const Vec2 c = table::d_centre();
    const int n = static_cast<int>(table::kDRadius / h);
    for (int i = 0; i <= n; ++i) {
        for (int j = -n; j <= n; ++j) {
            const Vec2 p{c.x - i * h, j * h};
            if (table::in_d(p) && !use(p)) return;
        }
    }
    const int arc = std::max(2, static_cast<int>(std::ceil(kPi * table::kDRadius / h)));
    for (int i = 0; i <= arc; ++i) {
        const double a = kPi * i / arc;
        const Vec2 p{c.x - table::kDRadius * std::sin(a), c.y - table::kDRadius * std::cos(a)};
        if (table::in_d(p) && !use(p)) return;
    }
}

std::vector<Vec2> d_samples(double step) {
    std::vector<Vec2> out;
    each_d_point(step, RefParams{}.d_sample_step_m, [&out](Vec2 p) {
        out.push_back(p);
        return true;
    });
    return out;
}

class GeometryOracle final : public TableOracle {
public:
    GeometryOracle(const phys::TableShape& table, const RefParams& ref)
        : ref_(ref),
          clear_(kContact + ref.touching_gap_m),
          max_cushions_(std::min(std::max(ref.escape_max_cushions, 0), kMaxCushions)),
          d_samples_(d_samples(ref.d_sample_step_m)) {
        // The noses are the lines that face straight across or along the table; the pocket
        // facings are skew.
        for (const phys::LineCushion& l : table.lines) {
            const bool across = std::abs(l.normal.x) > 1.0 - 1.0e-9;
            if (!across && !(std::abs(l.normal.y) > 1.0 - 1.0e-9)) continue;
            const int side = across ? (l.normal.x < 0.0 ? kTop : kBaulk) : (l.normal.y < 0.0 ? kLeft : kRight);
            const double a = across ? l.p1.y : l.p1.x;
            const double b = across ? l.p2.y : l.p2.x;
            runs_.push_back({side, std::min(a, b), std::max(a, b)});
        }
    }

    bool snookered(const std::vector<BallOnTable>& balls, int cue_ball, const std::vector<int>& on,
                   bool in_hand) const override {
        const Scene s = make_scene(balls, cue_ball, on);
        if (s.n_on == 0) return false;
        if (!in_hand) return s.has_cue && snookered_from(s, s.cue);
        bool placed = false;
        for (const Vec2& p : d_samples_) {
            if (!clear_of(s, p)) continue;
            if (!snookered_from(s, p)) return false;
            placed = true;
        }
        // Every point snookered: a free strip narrower than the step can still lie between them.
        bool free = false;
        each_d_point(ref_.d_fine_step_m, RefParams{}.d_fine_step_m, [&](Vec2 p) {
            if (!clear_of(s, p)) return true;
            placed = true;
            free = !snookered_from(s, p);
            return !free;
        });
        return placed && !free;
    }

    std::vector<int> snookering_balls(const std::vector<BallOnTable>& balls, int cue_ball,
                                      const std::vector<int>& on) const override {
        const Scene s = make_scene(balls, cue_ball, on);
        if (!s.has_cue || s.n_on == 0 || !snookered_from(s, s.cue)) return {};
        std::vector<int> effective;
        std::vector<int> mine;
        std::vector<double> how_far;
        for (std::size_t i = 0; i < s.n_on; ++i) {
            Edges e;
            if (!extreme_edges(s.cue, s.objs[i].pos, e)) return {};
            mine.clear();
            how_far.clear();
            double nearest = std::numeric_limits<double>::infinity();
            for (std::size_t k = s.n_on; k < s.objs.size(); ++k) {
                const Vec2 q = s.objs[k].pos;
                if (!in_the_way(s.cue, e.dir[0], e.len, q) && !in_the_way(s.cue, e.dir[1], e.len, q)) continue;
                mine.push_back(s.objs[k].place);
                how_far.push_back(std::sqrt(dist2(s.cue, q)));
                nearest = std::min(nearest, how_far.back());
            }
            std::size_t kept = 0;
            for (std::size_t k = 0; k < mine.size(); ++k) {
                if (how_far[k] <= nearest + kEquidistant) mine[kept++] = mine[k];
            }
            mine.resize(kept);
            // S2.17(c): different balls on obstructed by different balls leave no effective one.
            if (i == 0) {
                effective = mine;
            } else if (mine != effective) {
                return {};
            }
        }
        return effective;
    }

    bool clear_path(const std::vector<BallOnTable>& balls, int cue_ball, const std::vector<int>& on) const override {
        const Scene s = make_scene(balls, cue_ball, on);
        return s.has_cue && s.n_on > 0 && clear_path_from(s, s.cue);
    }

    bool full_ball(const std::vector<BallOnTable>& balls, int cue_ball, const std::vector<int>& on) const override {
        const Scene s = make_scene(balls, cue_ball, on);
        if (!s.has_cue) return false;
        for (std::size_t i = 0; i < s.n_on; ++i) {
            const Obj& b = s.objs[i];
            const double d = std::sqrt(dist2(s.cue, b.pos));
            if (!(d > kContact)) return true;
            const Vec2 dir{(b.pos.x - s.cue.x) / d, (b.pos.y - s.cue.y) / d};
            bool free = true;
            for (std::size_t k = 0; k < s.objs.size() && free; ++k) {
                // S3.14(d): a red counts as open to full-ball contact unless a colour is in front.
                if (k == i || (b.red && s.objs[k].red)) continue;
                free = !in_the_way(s.cue, dir, d - kContact, s.objs[k].pos);
            }
            if (free) return true;
        }
        return false;
    }

    // The cushion routes are a heuristic: straight lines reflected as light would be off the
    // lines a ball's centre bounces on, aimed at a few points across each ball on. It leaves out
    // what a cushion and side spin do to the angle, swerve and elevated strokes, routes off the
    // jaws, routes of more than escape_max_cushions cushions, gaps too narrow for the aim points
    // to find, and whether the stroke would have the pace to get there.
    bool can_hit(const std::vector<BallOnTable>& balls, int cue_ball, const std::vector<int>& on) const override {
        const Scene s = make_scene(balls, cue_ball, on);
        if (!s.has_cue || s.n_on == 0) return false;
        if (clear_path_from(s, s.cue)) return true;
        for (int cushions = 1; cushions <= max_cushions_; ++cushions) {
            for (std::size_t i = 0; i < s.n_on; ++i) {
                if (route_to_image(s, s.objs[i].pos, -1, cushions)) return true;
            }
        }
        return false;
    }

    std::vector<int> touching(const std::vector<BallOnTable>& balls, int cue_ball) const override {
        std::vector<int> out;
        const int n = static_cast<int>(balls.size());
        if (cue_ball < 0 || cue_ball >= n) return out;
        const Vec2 c = balls[std::size_t(cue_ball)].pos;
        for (int i = 0; i < n; ++i) {
            if (i != cue_ball && dist2(c, balls[std::size_t(i)].pos) <= clear_ * clear_) out.push_back(i);
        }
        return out;
    }

    std::vector<BallOnTable> spot(const std::vector<BallOnTable>& on_table,
                                  const std::vector<Ball>& colours) const override {
        return spot_colours(on_table, colours, ref_.touching_gap_m);
    }

    bool cue_placement_legal(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 pos) const override {
        if (!table::in_d(pos)) return false;
        const int n = static_cast<int>(balls.size());
        for (int i = 0; i < n; ++i) {
            if (i != cue_ball && !(dist2(pos, balls[std::size_t(i)].pos) > clear_ * clear_)) return false;
        }
        return true;
    }

    Vec2 nearest_cue_placement(const std::vector<BallOnTable>& balls, int cue_ball, Vec2 wanted) const override {
        if (!(std::isfinite(wanted.x) && std::isfinite(wanted.y))) wanted = table::default_cue_ball_position();
        if (cue_placement_legal(balls, cue_ball, wanted)) return wanted;
        const InnerD d{};
        const Vec2 start = into_d(d, wanted);
        if (cue_placement_legal(balls, cue_ball, start)) return start;

        // A ball is in the way. The nearest legal place is then straight out of one such ball to
        // just clear of it, or a corner of what is left of the D: where two balls' clearances
        // cross, or one crosses the D's edge, or an end of the baulk line's part of the D.
        const double clear = clear_ + kJustClear;
        std::vector<Vec2> around;
        const int n = static_cast<int>(balls.size());
        for (int i = 0; i < n; ++i) {
            const Vec2 q = balls[std::size_t(i)].pos;
            const double reach_out = d.radius + clear;
            if (i != cue_ball && q.x - clear <= d.line && dist2(q, d.centre) <= reach_out * reach_out) {
                around.push_back(q);
            }
        }
        Vec2 best = start;
        double best_d2 = std::numeric_limits<double>::infinity();
        const auto consider = [&](Vec2 p) {
            const double d2 = dist2(p, start);
            if (d2 < best_d2 && cue_placement_legal(balls, cue_ball, p)) {
                best = p;
                best_d2 = d2;
            }
        };
        consider({d.line, d.half});
        consider({d.line, -d.half});
        for (std::size_t i = 0; i < around.size(); ++i) {
            const Vec2 q = around[i];
            const double out = std::sqrt(dist2(start, q));
            // From a ball's very centre every way out is as near; any one will do, the corners
            // cover the rest.
            const Vec2 dir = out > 0.0 ? Vec2{(start.x - q.x) / out, (start.y - q.y) / out} : Vec2{-1.0, 0.0};
            consider({q.x + clear * dir.x, q.y + clear * dir.y});
            const double dx = d.line - q.x;
            if (std::abs(dx) <= clear) {
                const double h = std::sqrt(clear * clear - dx * dx);
                consider({d.line, q.y + h});
                consider({d.line, q.y - h});
            }
            circle_crossings(q, clear, d.centre, d.radius, consider);
            for (std::size_t j = i + 1; j < around.size(); ++j) {
                circle_crossings(q, clear, around[j], clear, consider);
            }
        }
        // Nothing found would take balls laid out to cover the whole D, which play cannot produce.
        return best;
    }

private:
    bool clear_of(const Scene& s, Vec2 p) const {
        for (const Obj& o : s.objs) {
            if (!(dist2(p, o.pos) > clear_ * clear_)) return false;
        }
        return true;
    }

    bool on_run(int side, double along) const {
        for (const Run& r : runs_) {
            if (r.side == side && along >= r.lo && along <= r.hi) return true;
        }
        return false;
    }

    // The cue ball sent along the unit `dir`, bouncing off at most max_cushions_ cushions: the
    // first ball it touches is a ball on. A bounce off anything but a straight nose ends the route.
    bool route_reaches_on(const Scene& s, Vec2 dir) const {
        const double inf = std::numeric_limits<double>::infinity();
        Vec2 from = s.cue;
        for (int bounces = 0;; ++bounces) {
            const double tx = dir.x != 0.0 ? ((dir.x > 0.0 ? half_x_ : -half_x_) - from.x) / dir.x : inf;
            const double ty = dir.y != 0.0 ? ((dir.y > 0.0 ? half_y_ : -half_y_) - from.y) / dir.y : inf;
            const double leg = std::max(0.0, std::min(tx, ty));
            double first = leg;
            std::size_t touched = s.objs.size();
            for (std::size_t k = 0; k < s.objs.size(); ++k) {
                const double wx = s.objs[k].pos.x - from.x;
                const double wy = s.objs[k].pos.y - from.y;
                const double along = wx * dir.x + wy * dir.y;
                if (along <= 0.0) continue;
                const double side = wx * dir.y - wy * dir.x;
                if (!(side * side < kInTheWay2)) continue;
                const double at = std::max(0.0, along - std::sqrt(kContact * kContact - side * side));
                if (at < first) {
                    first = at;
                    touched = k;
                }
            }
            if (touched < s.objs.size()) return touched < s.n_on;
            if (bounces == max_cushions_ || !(leg < inf)) return false;
            from = {from.x + leg * dir.x, from.y + leg * dir.y};
            if (tx < ty) {
                if (!on_run(dir.x > 0.0 ? kTop : kBaulk, from.y)) return false;
                from.x = dir.x > 0.0 ? half_x_ : -half_x_;
                dir.x = -dir.x;
            } else if (ty < tx) {
                if (!on_run(dir.y > 0.0 ? kLeft : kRight, from.x)) return false;
                from.y = dir.y > 0.0 ? half_y_ : -half_y_;
                dir.y = -dir.y;
            } else {
                return false;
            }
        }
    }

    // Aims across the width of a ball on as mirrored in `more` further cushions, none the same
    // as the one before; `image` is the ball as mirrored so far, last in `last_side`.
    bool route_to_image(const Scene& s, Vec2 image, int last_side, int more) const {
        if (more == 0) {
            const double vx = image.x - s.cue.x;
            const double vy = image.y - s.cue.y;
            const double d = std::sqrt(vx * vx + vy * vy);
            if (!(d > 0.0)) return false;
            for (int i = 0; i < kAimPoints; ++i) {
                const double off = kContact * kAimSpread * (2.0 * i / (kAimPoints - 1) - 1.0);
                const double ax = vx - off * vy / d;
                const double ay = vy + off * vx / d;
                const double len = std::sqrt(ax * ax + ay * ay);
                if (route_reaches_on(s, {ax / len, ay / len})) return true;
            }
            return false;
        }
        for (int side = 0; side < 4; ++side) {
            if (side != last_side && route_to_image(s, mirror(image, side, half_x_, half_y_), side, more - 1)) {
                return true;
            }
        }
        return false;
    }

    RefParams ref_;
    double clear_;  // centre distance two balls must exceed not to be touching
    int max_cushions_;
    // The lines a ball's centre bounces on.
    double half_x_ = table::kPlayingLength / 2.0 - table::kBallRadius;
    double half_y_ = table::kPlayingWidth / 2.0 - table::kBallRadius;
    std::vector<Run> runs_;
    std::vector<Vec2> d_samples_;
};

}  // namespace

std::unique_ptr<TableOracle> make_geometry_oracle(const phys::TableShape& table, const RefParams& ref) {
    return std::make_unique<GeometryOracle>(table, ref);
}

}  // namespace ofs::rules
