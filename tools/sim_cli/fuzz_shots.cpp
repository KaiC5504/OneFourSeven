// The fuzz's shot generator (spec 7.11 C14): random layouts, frozen clusters, balls on cushions and
// break-offs, each struck with a random strike across the full ranges.

#include <algorithm>
#include <cmath>
#include <limits>

#include "ofs/physics/rack.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInf = std::numeric_limits<double>::infinity();

std::size_t pick(ofs::Rng& rng, std::size_t n) { return static_cast<std::size_t>(rng.next_u64() % n); }

std::uint64_t shot_seed(std::uint64_t seed, std::uint64_t index) {
    std::uint64_t z = seed + 0x9e3779b97f4a7c15ULL * (index + 1);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

phys::BallState make_ball(ofs::table::Ball ball, int index) {
    phys::BallState b;
    b.ball = ball;
    b.index = index;
    return b;
}

// The 21 object balls in a random order.
std::vector<phys::BallState> object_balls(ofs::Rng& rng) {
    std::vector<phys::BallState> out;
    for (int i = 0; i < 15; ++i) out.push_back(make_ball(ofs::table::Ball::red, i));
    for (ofs::table::Ball colour : {ofs::table::Ball::yellow, ofs::table::Ball::green, ofs::table::Ball::brown,
                                    ofs::table::Ball::blue, ofs::table::Ball::pink, ofs::table::Ball::black}) {
        out.push_back(make_ball(colour, 0));
    }
    for (std::size_t i = out.size() - 1; i > 0; --i) std::swap(out[i], out[pick(rng, i + 1)]);
    return out;
}

struct Layout {
    ofs::Rng& rng;
    const PlayArea& area;
    std::vector<phys::BallState> balls;

    bool clear_of_balls(const Vec3& c) const {
        for (const phys::BallState& b : balls) {
            if (planar_distance(c, b.r) < 2.0 * kR) return false;
        }
        return true;
    }

    bool add_at(phys::BallState b, const Vec3& c) {
        if (!area.placement_ok(c) || !clear_of_balls(c)) return false;
        b.r = c;
        balls.push_back(b);
        return true;
    }

    bool add_random(const phys::BallState& b) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            const Vec3 c{area.centre_x() + rng.uniform(-area.half_x(), area.half_x()),
                         area.centre_y() + rng.uniform(-area.half_y(), area.half_y()), 0.0};
            if (add_at(b, c)) return true;
        }
        return false;
    }

    // Frozen to `other`: the centres exactly 2R apart, rounded up so they never overlap.
    bool add_touching_ball(const phys::BallState& b, std::size_t other) {
        const Vec3 o = balls[other].r;
        for (int attempt = 0; attempt < 30; ++attempt) {
            const double angle = rng.uniform(0.0, 2.0 * kPi);
            const Vec3 u{std::cos(angle), std::sin(angle), 0.0};
            double separation = 2.0 * kR;
            Vec3 c = o + u * separation;
            for (int i = 0; i < 64 && planar_distance(c, o) < 2.0 * kR; ++i) {
                separation = std::nextafter(separation, kInf);
                c = o + u * separation;
            }
            if (add_at(b, c)) return true;
        }
        return false;
    }

    // Resting against a cushion nose, R from it to the last bit.
    bool add_touching_cushion(const phys::BallState& b) {
        const auto& lines = area.table().lines;
        for (int attempt = 0; attempt < 50; ++attempt) {
            Vec3 c;
            if (lines.empty()) {
                const double along = rng.uniform(-1.0, 1.0);
                const double hx = area.half_x();
                const double hy = area.half_y();
                switch (pick(rng, 4)) {
                    case 0: c = {hx, along * hy, 0.0}; break;
                    case 1: c = {-hx, along * hy, 0.0}; break;
                    case 2: c = {along * hx, hy, 0.0}; break;
                    default: c = {along * hx, -hy, 0.0}; break;
                }
                c += Vec3{area.centre_x(), area.centre_y(), 0.0};
            } else {
                const std::size_t k = pick(rng, lines.size());
                const phys::LineCushion& line = lines[k];
                const Vec3 p1{line.p1.x, line.p1.y, 0.0};
                const Vec3 dir{line.p2.x - line.p1.x, line.p2.y - line.p1.y, 0.0};
                const double length = norm_xy(dir);
                if (length < 2.0 * kR) continue;
                const Vec3 base = p1 + dir * (rng.uniform(kR, length - kR) / length);
                const Vec3 n{line.normal.x, line.normal.y, 0.0};
                double offset = kR;
                c = base + n * offset;
                bool on_segment = false;
                for (int i = 0; i < 64 && area.line_distance(k, c, on_segment) < kR; ++i) {
                    offset = std::nextafter(offset, kInf);
                    c = base + n * offset;
                }
            }
            if (add_at(b, c)) return true;
        }
        return false;
    }
};

phys::CueStrike random_strike(ofs::Rng& rng, const phys::PhysicsParams& p,
                              const std::vector<phys::BallState>& balls) {
    phys::CueStrike c;
    const Vec3 cue = balls[0].r;
    if (balls.size() > 1 && rng.uniform() < 0.6) {
        const Vec3 target = balls[1 + pick(rng, balls.size() - 1)].r;
        c.aim_rad = std::atan2(target.y - cue.y, target.x - cue.x) + 0.03 * rng.normal();
    } else {
        c.aim_rad = rng.uniform(-kPi, kPi);
    }
    const double speed = rng.uniform();
    if (speed < 0.02) {
        c.speed_m_s = 0.0;
    } else if (speed < 0.07) {
        c.speed_m_s = p.max_cue_speed_m_s;
    } else {
        c.speed_m_s = rng.uniform(0.05, p.max_cue_speed_m_s);
    }
    // A fifth of the strikes are beyond the miscue limit, up to the ball's edge.
    const double offset =
        rng.uniform() < 0.8 ? rng.uniform(0.0, p.miscue_limit) : rng.uniform(p.miscue_limit, 1.0);
    const double around = rng.uniform(0.0, 2.0 * kPi);
    c.tip_x = offset * std::cos(around);
    c.tip_y = offset * std::sin(around);
    const double elevation = rng.uniform();
    if (elevation < 0.15) {
        c.elevation_rad = 0.0;
    } else if (elevation < 0.2) {
        c.elevation_rad = p.max_elevation_rad;
    } else if (elevation < 0.65) {
        c.elevation_rad = rng.uniform(0.0, std::min(0.2, p.max_elevation_rad));
    } else {
        c.elevation_rad = rng.uniform(0.0, p.max_elevation_rad);
    }
    return c;
}

void break_off(ofs::Rng& rng, const Setup& setup, const PlayArea& area, FuzzShot& shot) {
    shot.in.start = phys::opening_rack(rng.next_u64(), setup.params);
    std::vector<phys::BallState>& balls = shot.in.start.balls;
    // Half the break-offs move the cue ball somewhere else in the D.
    if (!balls.empty() && rng.uniform() < 0.5) {
        const ofs::table::Vec2 d = ofs::table::d_centre();
        for (int attempt = 0; attempt < 50; ++attempt) {
            const ofs::table::Vec2 p{d.x - rng.uniform(0.0, ofs::table::kDRadius),
                                     d.y + rng.uniform(-ofs::table::kDRadius, ofs::table::kDRadius)};
            const Vec3 c{p.x, p.y, 0.0};
            bool clear = ofs::table::in_d(p) && area.placement_ok(c);
            for (std::size_t i = 1; clear && i < balls.size(); ++i) {
                clear = planar_distance(c, balls[i].r) >= 2.0 * kR;
            }
            if (clear) {
                balls[0].r = c;
                break;
            }
        }
    }
    if (rng.uniform() < 0.7) {
        shot.in.strike = break_off_strike(shot.in.start, rng.uniform(3.0, setup.params.max_cue_speed_m_s));
        shot.in.strike.aim_rad += 0.01 * rng.normal();
        shot.in.strike.tip_x += 0.1 * rng.normal();
        shot.in.strike.tip_y += 0.1 * rng.normal();
    } else {
        shot.in.strike = random_strike(rng, setup.params, balls);
    }
}

}  // namespace

const char* fuzz_kind_name(FuzzKind k) {
    switch (k) {
        case FuzzKind::break_off: return "break_off";
        case FuzzKind::scattered: return "scattered";
        case FuzzKind::cluster: return "cluster";
        case FuzzKind::cushion: return "cushion";
    }
    return "scattered";
}

FuzzShot make_fuzz_shot(std::uint64_t seed, std::uint64_t index, const Setup& setup, const PlayArea& area) {
    ofs::Rng rng(shot_seed(seed, index));
    FuzzShot shot;
    shot.in.cue_ball = 0;
    shot.in.has_strike = true;
    shot.in.params_hash = setup.params_hash;
    const double kind = rng.uniform();
    shot.kind = kind < 0.2    ? FuzzKind::break_off
                : kind < 0.55 ? FuzzKind::scattered
                : kind < 0.8  ? FuzzKind::cluster
                              : FuzzKind::cushion;
    if (shot.kind == FuzzKind::break_off) {
        break_off(rng, setup, area, shot);
        shot.in.seed = rng.next_u64();
        return shot;
    }

    Layout layout{rng, area, {}};
    std::vector<phys::BallState> objects = object_balls(rng);
    const std::size_t n = 1 + pick(rng, 22);
    objects.resize(n - 1);
    const phys::BallState cue = make_ball(ofs::table::Ball::cue, 0);
    std::size_t next = 0;
    if (shot.kind == FuzzKind::cluster) {
        // The cue ball is in the cluster a quarter of the time: frozen to a ball it is striking.
        const bool cue_in_cluster = rng.uniform() < 0.25;
        layout.add_random(cue);
        const std::size_t first = layout.balls.size();
        if (!cue_in_cluster && next < objects.size()) layout.add_random(objects[next++]);
        const std::size_t size = 2 + pick(rng, 5);
        const std::size_t cluster_start = cue_in_cluster ? 0 : first;
        while (next < objects.size() && layout.balls.size() - cluster_start < size) {
            const std::size_t members = layout.balls.size() - cluster_start;
            layout.add_touching_ball(objects[next++], cluster_start + pick(rng, members));
        }
    } else if (shot.kind == FuzzKind::cushion) {
        if (rng.uniform() < 0.3) {
            layout.add_touching_cushion(cue);
        }
        if (layout.balls.empty()) layout.add_random(cue);
        const std::size_t touching = 1 + pick(rng, 4);
        for (std::size_t i = 0; i < touching && next < objects.size(); ++i) {
            layout.add_touching_cushion(objects[next++]);
        }
    } else {
        layout.add_random(cue);
    }
    if (layout.balls.empty() || layout.balls[0].ball != ofs::table::Ball::cue) {
        phys::BallState centred = cue;
        centred.r = {area.centre_x(), area.centre_y(), 0.0};
        layout.balls.insert(layout.balls.begin(), centred);
    }
    while (next < objects.size()) layout.add_random(objects[next++]);

    shot.in.start.balls = layout.balls;
    shot.in.strike = random_strike(rng, setup.params, layout.balls);
    shot.in.seed = rng.next_u64();
    return shot;
}

}  // namespace sim_cli
