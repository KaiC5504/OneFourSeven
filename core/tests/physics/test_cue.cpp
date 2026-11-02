#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>

#include "ofs/physics/cue.hpp"
#include "ofs/physics/serialize.hpp"

using namespace ofs::phys;
using ofs::Vec3;

namespace {

constexpr double kR = ofs::table::kBallRadius;
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

BallState ball_at(ofs::table::Ball kind, double x, double y) {
    BallState b;
    b.ball = kind;
    b.r = {x, y, 0.0};
    return b;
}

TableState cue_ball_at(double x, double y) {
    TableState s;
    s.balls.push_back(ball_at(ofs::table::Ball::cue, x, y));
    return s;
}

// The four cushion noses of the playing area as lines with inward normals and no pockets: the
// real outline is table_shape.json's, which another file loads.
TableShape rectangle() {
    const double x = ofs::table::kPlayingLength / 2.0;
    const double y = ofs::table::kPlayingWidth / 2.0;
    TableShape t;
    t.lines = {
        {{-x, -y}, {-x, y}, {1.0, 0.0}},
        {{x, -y}, {x, y}, {-1.0, 0.0}},
        {{-x, -y}, {x, -y}, {0.0, 1.0}},
        {{-x, y}, {x, y}, {0.0, -1.0}},
    };
    t.rail_height_m = 0.04;
    t.rail_width_m = 0.05;
    return t;
}

struct Fixture {
    TableState start;
    int cue_ball = 0;
    CueStrike strike;
};

Fixture load_fixture(const std::string& name) {
    std::ifstream in(std::string(OFS_TEST_FIXTURES_DIR) + "/positions/" + name + ".json");
    REQUIRE(in.good());
    const nlohmann::json j = nlohmann::json::parse(in);
    Fixture f;
    std::string error;
    REQUIRE_MESSAGE(from_json(j["start"], f.start, error), error);
    REQUIRE_MESSAGE(from_json(j["strike"], f.strike, error), error);
    f.cue_ball = j["cue_ball"].get<int>();
    return f;
}

// A strike with nothing in the cue's way.
StrikeOutcome hit(const CueStrike& c, const PhysicsParams& p = PhysicsParams{}, std::uint64_t seed = 147) {
    ofs::Rng rng(seed);
    return strike(cue_ball_at(0.0, 0.0), 0, c, TableShape{}, p, rng);
}

double kinetic_energy(const StrikeOutcome& o, const PhysicsParams& p) {
    return 0.5 * p.ball_mass_kg * ofs::norm2(o.v) + 0.5 * (0.4 * p.ball_mass_kg * kR * kR) * ofs::norm2(o.w);
}

bool close(const Vec3& a, const Vec3& b, double tol) { return ofs::norm(a - b) <= tol * (1.0 + ofs::norm(b)); }

Vec3 rotate_z(const Vec3& v, double angle) {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {c * v.x - s * v.y, s * v.x + c * v.y, v.z};
}

struct Reference {
    Vec3 v;
    Vec3 w;
    double squirt = 0.0;
};

// pooltool (Apache-2.0) physics/resolve/stick_ball: InstantaneousPoint2D.solve, cue_strike and
// squirt.get_squirt_angle, transcribed in pooltool's own frames and signs (its a is positive to the
// left; its ball frame has +y back along the cue), with the elastic numerator 2 V0 written (1 + e) V0
// as in Alciatore TP A-30.
Reference pooltool_strike(double tip_x, double tip_y, double theta, double phi, double V0,
                          const PhysicsParams& p) {
    const double m = p.ball_mass_kg;
    const double M = p.cue_mass_kg;
    const double cue_a = -tip_x;
    const double cue_b = tip_y;
    const double cue_c = std::sqrt(1.0 - cue_a * cue_a - cue_b * cue_b);
    const double ball_a = cue_a;
    const double ball_c = std::cos(theta) * cue_c - std::sin(theta) * cue_b;
    const double ball_b = std::sin(theta) * cue_c + std::cos(theta) * cue_b;
    const double a = kR * ball_a;
    const double c = kR * ball_c;
    const double b = kR * ball_b;
    const double I_m = 2.0 / 5.0 * kR * kR;
    const double temp = a * a + std::pow(b * std::cos(theta), 2) + std::pow(c * std::sin(theta), 2) -
                        2.0 * b * c * std::cos(theta) * std::sin(theta);
    const double v = (1.0 + p.cue_tip_restitution) * V0 / (1.0 + m / M + temp / I_m);
    const Vec3 v_B = Vec3{0.0, std::cos(theta), std::sin(theta)} * -v;
    const Vec3 vec{-c * std::sin(theta) + b * std::cos(theta), a * std::sin(theta), -a * std::cos(theta)};
    const Vec3 w_B = vec * (v / I_m);
    Reference out;
    out.w = rotate_z(w_B, phi + kPi / 2.0);
    const double m_r = m / p.cue_end_mass_kg;
    const double A = 1.0 - ball_a * ball_a;
    out.squirt = -std::atan2(5.0 / 2.0 * ball_a * std::sqrt(A), 1.0 + m_r + 5.0 / 2.0 * A);
    out.v = rotate_z(rotate_z(v_B, phi + kPi / 2.0), out.squirt);
    out.v.z = 0.0;
    return out;
}

// The obstruction checked by brute force, apart from cue.cpp's closed forms: the cue is built from
// its contact point on the cue ball, sampled every 0.05 mm along its first `reach` metres, and has
// to keep its radius plus the clearance from a solid given by `gap` (distance from a point to the
// solid's surface). Bisection finds the lowest elevation at which every sample does.
template <class Gap>
double brute_force_elevation(const BallState& cue_ball, double aim, double tip_x, double tip_y, double reach,
                             Gap gap, const PhysicsParams& p) {
    const Vec3 aim_dir{std::cos(aim), std::sin(aim), 0.0};
    const Vec3 right{std::sin(aim), -std::cos(aim), 0.0};
    const Vec3 centre{cue_ball.r.x, cue_ball.r.y, kR};
    const double depth = std::sqrt(1.0 - tip_x * tip_x - tip_y * tip_y);
    auto clear = [&](double theta) {
        const Vec3 forward = aim_dir * std::cos(theta) - ofs::kUp * std::sin(theta);
        const Vec3 up = aim_dir * std::sin(theta) + ofs::kUp * std::cos(theta);
        const Vec3 tip = centre + (right * tip_x + up * tip_y - forward * depth) * kR;
        for (double along = 0.0; along <= reach; along += 5e-5) {
            const double taper = (p.cue_butt_diameter_m - p.cue_tip_diameter_m) * along / p.cue_length_m;
            const double radius = 0.5 * (p.cue_tip_diameter_m + taper);
            if (gap(tip - forward * along) < radius + p.cue_clearance_m) return false;
        }
        return true;
    };
    if (clear(0.0)) return 0.0;
    double lo = 0.0;
    double hi = p.max_elevation_rad;
    if (!clear(hi)) return hi;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        (clear(mid) ? hi : lo) = mid;
    }
    return hi;
}

// The rail behind the baulk cushion: x from the nose back rail_width_m, up to rail_height_m.
auto baulk_rail(const TableShape& t) {
    const double nose = -ofs::table::kPlayingLength / 2.0;
    return [nose, t](const Vec3& q) {
        const double dx = std::max({nose - t.rail_width_m - q.x, q.x - nose, 0.0});
        const double dz = std::max(q.z - t.rail_height_m, 0.0);
        return std::hypot(dx, dz);
    };
}

auto ball_solid(const BallState& b) {
    return [b](const Vec3& q) { return ofs::norm(q - Vec3{b.r.x, b.r.y, kR}) - kR; };
}

}  // namespace

TEST_CASE("cue: a centre-ball hit gives (1 + e) V / (1 + m/M) and no spin") {
    // Marlow / Alciatore TP A-30 (pooltool cue_strike with the restitution): v = (1 + e) V / (1 + m/M).
    const PhysicsParams p;
    for (double V : {0.25, 0.5, 1.0, 2.0, 3.0, 5.0, 7.5, 10.0}) {
        CAPTURE(V);
        const StrikeOutcome o = hit({0.0, 0.0, 0.0, 0.0, V});
        const double expected = (1.0 + p.cue_tip_restitution) * V / (1.0 + p.ball_mass_kg / p.cue_mass_kg);
        CHECK(o.v.x == doctest::Approx(expected));
        CHECK(o.v.y == 0.0);
        CHECK(o.v.z == 0.0);
        CHECK(o.w == Vec3{});
        CHECK(o.squirt_rad == 0.0);
        CHECK_FALSE(o.miscue);
    }
}

TEST_CASE("cue: strike matches pooltool's instantaneous point model over offsets, elevations and aims") {
    const PhysicsParams p;
    int compared = 0;
    for (double tip_x = -0.5; tip_x <= 0.5; tip_x += 0.25) {
        for (double tip_y = -0.5; tip_y <= 0.5; tip_y += 0.25) {
            if (std::hypot(tip_x, tip_y) > p.miscue_limit) continue;
            for (double theta : {0.0, 0.2, 0.7, 1.2}) {
                for (double aim : {0.0, 1.0, -2.5}) {
                    CAPTURE(tip_x);
                    CAPTURE(tip_y);
                    CAPTURE(theta);
                    CAPTURE(aim);
                    const StrikeOutcome o = hit({aim, tip_x, tip_y, theta, 2.7});
                    const Reference ref = pooltool_strike(tip_x, tip_y, theta, aim, 2.7, p);
                    CHECK(close(o.v, ref.v, 1e-12));
                    CHECK(close(o.w, ref.w, 1e-12));
                    CHECK(o.squirt_rad == doctest::Approx(ref.squirt).epsilon(1e-12));
                    CHECK(o.elevation_used_rad == theta);
                    ++compared;
                }
            }
        }
    }
    CHECK(compared > 100);
}

TEST_CASE("cue: off-centre hits lose ball speed as 1 / (1 + m/M + 5/2 (a^2 + b^2))") {
    // Alciatore TP A-30: the offset adds the ball's rotational inertia at the contact point.
    const PhysicsParams p;
    const double V = 3.0;
    double previous = 1e9;
    for (double offset = 0.0; offset <= 0.5; offset += 0.05) {
        CAPTURE(offset);
        const double expected = (1.0 + p.cue_tip_restitution) * V /
                                (1.0 + p.ball_mass_kg / p.cue_mass_kg + 2.5 * offset * offset);
        const double side = ofs::norm_xy(hit({0.0, offset, 0.0, 0.0, V}).v);
        const double top = ofs::norm_xy(hit({0.0, 0.0, offset, 0.0, V}).v);
        const double diagonal = ofs::norm_xy(hit({0.0, offset * 0.6, -offset * 0.8, 0.0, V}).v);
        CHECK(side == doctest::Approx(expected));
        CHECK(top == doctest::Approx(expected));
        CHECK(diagonal == doctest::Approx(expected));
        CHECK(top <= previous);
        previous = top;
    }
}

TEST_CASE("cue: topspin and backspin signs, and natural roll at 2R/5 above centre") {
    const StrikeOutcome top = hit({0.0, 0.0, 0.3, 0.0, 2.0});
    CHECK(top.w.y > 0.0);
    CHECK(top.w.x == 0.0);
    CHECK(top.w.z == 0.0);
    const StrikeOutcome back = hit({0.0, 0.0, -0.3, 0.0, 2.0});
    CHECK(back.w.y < 0.0);
    // The centre of percussion of a solid sphere: a level hit 2R/5 above centre rolls at once
    // (contact slip v.x - R w.y = 0, types.hpp).
    const StrikeOutcome roll = hit({0.0, 0.0, 0.4, 0.0, 2.0});
    CHECK(roll.v.x - kR * roll.w.y == doctest::Approx(0.0));
}

TEST_CASE("cue: right-hand side spins the ball anticlockwise seen from above") {
    // The tip drives the ball's right-hand side forward, and r x F = (right) x (forward) points up, so
    // tip_x > 0 gives w.z > 0: anticlockwise from above, the ball's front turning left. That is what
    // throws an object ball left and makes it run right off a cushion. pooltool agrees: its
    // w_z = -a v / (2/5 R) with its a = -tip_x.
    const StrikeOutcome right = hit({0.0, 0.3, 0.0, 0.0, 2.0});
    CHECK(right.w.z > 0.0);
    CHECK(right.w.x == 0.0);
    CHECK(right.w.y == 0.0);
    CHECK(right.w.z == doctest::Approx(2.5 * 0.3 * ofs::norm_xy(right.v) / kR));
    CHECK(hit({0.0, -0.3, 0.0, 0.0, 2.0}).w.z < 0.0);
}

TEST_CASE("squirt: right-hand side sends the ball left of the aim, more with more side") {
    // pooltool get_squirt_angle (Alciatore TP A-31): tan(squirt) = 5/2 a sqrt(1 - a^2) / (1 + m/m_e +
    // 5/2 (1 - a^2)), rising with a up to 1/sqrt(2), well past the miscue limit.
    const PhysicsParams p;
    for (double aim : {0.0, 2.0, -1.2}) {
        CAPTURE(aim);
        const StrikeOutcome right = hit({aim, 0.4, 0.0, 0.0, 2.0});
        CHECK(right.squirt_rad > 0.0);
        CHECK(std::remainder(std::atan2(right.v.y, right.v.x) - aim - right.squirt_rad, 2.0 * kPi) ==
              doctest::Approx(0.0).epsilon(1e-12));
        const StrikeOutcome left = hit({aim, -0.4, 0.0, 0.0, 2.0});
        CHECK(left.squirt_rad == doctest::Approx(-right.squirt_rad));
    }
    // Seen down the cue along +x, left is +y.
    CHECK(hit({0.0, 0.4, 0.0, 0.0, 2.0}).v.y > 0.0);
    CHECK(hit({0.0, 0.0, 0.4, 0.0, 2.0}).squirt_rad == 0.0);

    double previous = 0.0;
    for (int i = 1; i <= 11; ++i) {
        const double side = std::min(0.05 * i, p.miscue_limit);
        const double squirt = hit({0.0, side, 0.0, 0.0, 2.0}).squirt_rad;
        CHECK(squirt > previous);
        previous = squirt;
    }
}

TEST_CASE("squirt: maximum side gives 1-3 degrees with a snooker cue's end mass (C9)") {
    PhysicsParams p;
    const double defaults = hit({0.0, p.miscue_limit, 0.0, 0.0, 2.0}, p).squirt_rad / kDeg;
    MESSAGE("squirt at maximum side, cue_end_mass_kg " << p.cue_end_mass_kg << ": " << defaults << " degrees");
    // pooltool's SNOOKER_GENERIC cue (objects/cue/datatypes.py): end_mass = 0.140 / 30 kg.
    p.cue_end_mass_kg = 0.140 / 30.0;
    const double snooker = hit({0.0, p.miscue_limit, 0.0, 0.0, 2.0}, p).squirt_rad / kDeg;
    MESSAGE("squirt at maximum side, cue_end_mass_kg " << p.cue_end_mass_kg << ": " << snooker << " degrees");
    CHECK(snooker >= 1.0);
    CHECK(snooker <= 3.0);
    CHECK(defaults > snooker);
}

TEST_CASE("squirt: a vanishing end mass gives a finite strike and next to no squirt") {
    // cue.cpp multiplies the squirt terms through by the end mass, so there is no ball_mass / 0;
    // tan(squirt) = 5/2 a sqrt(1 - a^2) m_e / (m_e (1 + 5/2 (1 - a^2)) + m), at most 5/4 m_e / m.
    PhysicsParams p;
    for (double end_mass : {1e-4, 1e-12, 0.0}) {
        p.cue_end_mass_kg = end_mass;
        for (double tip_x : {-0.54, -0.2, 0.3, 0.54}) {
            CAPTURE(end_mass);
            CAPTURE(tip_x);
            const StrikeOutcome o = hit({0.7, tip_x, 0.1, 0.2, 3.0}, p);
            const double speed = (1.0 + p.cue_tip_restitution) * 3.0 * std::cos(0.2) /
                                 (1.0 + p.ball_mass_kg / p.cue_mass_kg + 2.5 * (tip_x * tip_x + 0.01));
            CHECK(std::isfinite(o.v.x));
            CHECK(std::isfinite(o.v.y));
            CHECK(ofs::norm_xy(o.v) == doctest::Approx(speed).epsilon(1e-12));
            CHECK(std::abs(o.squirt_rad) <= 1.25 * end_mass / p.ball_mass_kg);
            CHECK(std::atan2(o.v.y, o.v.x) - 0.7 == doctest::Approx(o.squirt_rad).epsilon(1e-12));
        }
    }

    // At the default end mass the multiplied-through form is the divided one to rounding.
    const PhysicsParams defaults;
    for (double tip_x = -0.55; tip_x <= 0.55; tip_x += 0.05) {
        CAPTURE(tip_x);
        const double rest = 1.0 - tip_x * tip_x;
        const double divided = std::atan2(2.5 * tip_x * std::sqrt(rest),
                                          1.0 + defaults.ball_mass_kg / defaults.cue_end_mass_kg + 2.5 * rest);
        CHECK(hit({0.0, tip_x, 0.0, 0.0, 2.0}).squirt_rad == doctest::Approx(divided).epsilon(1e-12));
    }
}

TEST_CASE("cue: elevation tilts the side-spin axis forward and leaves top and back spin level") {
    // pooltool cue_strike: w = v / (2/5 R^2) (b cos - c sin, a sin, -a cos) in its ball frame, so side
    // spin's axis leans from vertical towards the shot by the elevation.
    for (double theta : {0.2, 0.5, 1.0}) {
        CAPTURE(theta);
        for (double aim : {0.0, 0.7}) {
            const Vec3 aim_dir{std::cos(aim), std::sin(aim), 0.0};
            const Vec3 right{std::sin(aim), -std::cos(aim), 0.0};
            const StrikeOutcome side = hit({aim, 0.3, 0.0, theta, 2.0});
            CHECK(std::atan2(ofs::dot(side.w, aim_dir), side.w.z) == doctest::Approx(theta));
            CHECK(ofs::dot(side.w, right) == doctest::Approx(0.0));
            // Swerve (C10): the forward-tilted axis makes the cloth contact slip left
            // (u = v + w x (0, 0, -R), types.hpp), so friction pulls the ball right, towards the side.
            const Vec3 spin_slip = ofs::cross(side.w, Vec3{0.0, 0.0, -kR});
            CHECK(ofs::dot(spin_slip, right) < 0.0);
            const StrikeOutcome level = hit({aim, 0.3, 0.0, 0.0, 2.0});
            CHECK(ofs::dot(ofs::cross(level.w, Vec3{0.0, 0.0, -kR}), right) == doctest::Approx(0.0));

            const StrikeOutcome screw = hit({aim, 0.0, -0.3, theta, 2.0});
            CHECK(screw.w.z == 0.0);
            CHECK(ofs::dot(screw.w, aim_dir) == doctest::Approx(0.0));
            CHECK(ofs::dot(screw.w, right) > 0.0);
        }
    }
}

TEST_CASE("cue: the vertical velocity goes into the cloth") {
    // Spec 7.9 and pooltool InstantaneousPoint2D, which zeroes v_z after the 3D strike.
    const PhysicsParams p;
    for (double theta : {0.0, 0.3, 0.8, 1.3}) {
        CAPTURE(theta);
        const StrikeOutcome o = hit({0.4, 0.1, -0.2, theta, 4.0});
        CHECK(o.v.z == 0.0);
        const double along_cue = (1.0 + p.cue_tip_restitution) * 4.0 /
                                 (1.0 + p.ball_mass_kg / p.cue_mass_kg + 2.5 * (0.01 + 0.04));
        CHECK(ofs::norm_xy(o.v) == doctest::Approx(along_cue * std::cos(theta)));
    }
}

TEST_CASE("cue: the ball never gets more kinetic energy than the cue had") {
    // An impact with restitution e <= 1 conserves energy at best (C12), so the ball's share, linear plus
    // rotational, stays below 1/2 M V^2, elastic tip included.
    PhysicsParams p;
    for (double e : {p.cue_tip_restitution, 1.0}) {
        p.cue_tip_restitution = e;
        for (double tip_x = -0.55; tip_x <= 0.55; tip_x += 0.11) {
            for (double tip_y = -0.55; tip_y <= 0.55; tip_y += 0.11) {
                if (std::hypot(tip_x, tip_y) > p.miscue_limit) continue;
                for (double theta : {0.0, 0.3, 0.8, 1.3}) {
                    for (double V : {0.5, 3.0, 10.0}) {
                        const StrikeOutcome o = hit({0.3, tip_x, tip_y, theta, V}, p);
                        CHECK(kinetic_energy(o, p) <= 0.5 * p.cue_mass_kg * V * V);
                    }
                }
            }
        }
    }
}

TEST_CASE("miscue: just beyond miscue_limit miscues, at it does not") {
    // Spec 7.3: offsets beyond the limit miscue; cue.hpp: hypot(tip_x, tip_y) > miscue_limit.
    const PhysicsParams p;
    CHECK_FALSE(hit({0.0, p.miscue_limit, 0.0, 0.0, 3.0}).miscue);
    CHECK(hit({0.0, std::nextafter(p.miscue_limit, 1.0), 0.0, 0.0, 3.0}).miscue);
    CHECK_FALSE(hit({0.0, 0.0, -p.miscue_limit, 0.0, 3.0}).miscue);
    CHECK(hit({0.0, 0.0, std::nextafter(-p.miscue_limit, -1.0), 0.0, 3.0}).miscue);
    CHECK(hit({0.0, 0.45, 0.45, 0.0, 3.0}).miscue);
    CHECK(hit({0.0, 1.5, 0.0, 0.0, 3.0}).miscue);
}

TEST_CASE("miscue: a seed replays exactly and different seeds differ") {
    // C13: the only randomness is the Rng passed in.
    const CueStrike c{0.3, 0.6, 0.1, 0.1, 4.0};
    const StrikeOutcome a = hit(c, PhysicsParams{}, 99);
    const StrikeOutcome b = hit(c, PhysicsParams{}, 99);
    REQUIRE(a.miscue);
    CHECK(a.v == b.v);
    CHECK(a.w == b.w);
    CHECK(a.squirt_rad == b.squirt_rad);
    const StrikeOutcome other = hit(c, PhysicsParams{}, 100);
    CHECK(other.v != a.v);
    CHECK(other.squirt_rad != a.squirt_rad);
}

TEST_CASE("miscue: weak, within the angle bound, with no spin") {
    // Spec 7.3 and cue.hpp: at most miscue_speed_factor of the full-hit speed, within
    // miscue_max_angle_rad of the aim, little spin.
    const PhysicsParams p;
    const double V = 6.0;
    const double theta = 0.2;
    const double full_hit =
        (1.0 + p.cue_tip_restitution) * V / (1.0 + p.ball_mass_kg / p.cue_mass_kg) * std::cos(theta);
    double widest = 0.0;
    double fastest = 0.0;
    for (std::uint64_t seed = 0; seed < 2000; ++seed) {
        const StrikeOutcome o = hit({1.1, -0.4, 0.5, theta, V}, p, seed);
        REQUIRE(o.miscue);
        const double speed = ofs::norm_xy(o.v);
        const double angle = std::remainder(std::atan2(o.v.y, o.v.x) - 1.1, 2.0 * kPi);
        CHECK(speed <= p.miscue_speed_factor * full_hit);
        CHECK(std::abs(angle) <= p.miscue_max_angle_rad + 1e-12);
        CHECK(angle == doctest::Approx(o.squirt_rad));
        CHECK(o.v.z == 0.0);
        CHECK(o.w == Vec3{});
        CHECK(o.elevation_used_rad == theta);
        widest = std::max(widest, std::abs(angle));
        fastest = std::max(fastest, speed);
    }
    CHECK(widest > 0.95 * p.miscue_max_angle_rad);
    CHECK(fastest > 0.95 * p.miscue_speed_factor * full_hit);
}

TEST_CASE("miscue: the pace is uniform up to miscue_speed_factor of the full hit") {
    // cue.cpp: speed = full hit * miscue_speed_factor * u, u the strike's first uniform draw, and
    // the angle its second, uniform within miscue_max_angle_rad.
    const PhysicsParams p;
    const double V = 4.0;
    const double theta = 0.3;
    const double full_hit =
        (1.0 + p.cue_tip_restitution) * V / (1.0 + p.ball_mass_kg / p.cue_mass_kg) * std::cos(theta);
    const double bound = p.miscue_speed_factor * full_hit;
    constexpr int kSeeds = 20000;
    double sum = 0.0;
    double sum2 = 0.0;
    int bins[10] = {};
    for (std::uint64_t seed = 0; seed < kSeeds; ++seed) {
        const StrikeOutcome o = hit({-0.6, 0.5, -0.4, theta, V}, p, seed);
        REQUIRE(o.miscue);
        ofs::Rng draws(seed);
        const double pace = draws.uniform();
        const double angle = draws.uniform(-p.miscue_max_angle_rad, p.miscue_max_angle_rad);
        const double fraction = ofs::norm_xy(o.v) / bound;
        CHECK(fraction == doctest::Approx(pace).epsilon(1e-12));
        CHECK(o.squirt_rad == angle);
        sum += fraction;
        sum2 += fraction * fraction;
        ++bins[std::min(9, static_cast<int>(fraction * 10.0))];
    }
    // Uniform on [0, 1): mean 1/2, standard deviation 1/sqrt(12) (standard errors 0.0020 and
    // 0.0009 over 20000 draws) and a tenth in each tenth; five standard errors of slack each.
    const double mean = sum / kSeeds;
    const double spread = std::sqrt(sum2 / kSeeds - mean * mean);
    CHECK(std::abs(mean - 0.5) < 0.010);
    CHECK(std::abs(spread - 1.0 / std::sqrt(12.0)) < 0.0046);
    for (int b : bins) CHECK(std::abs(b - kSeeds / 10) < 5.0 * std::sqrt(kSeeds * 0.09));
}

TEST_CASE("miscue: every strike draws from the rng exactly twice") {
    const Fixture behind = load_fixture("ball_behind_cue_ball");
    const TableShape t = rectangle();
    const CueStrike strikes[] = {{0.0, 0.7, 0.0, 0.0, 3.0}, {0.0, 0.2, -0.3, 0.1, 3.0}, behind.strike};
    for (const CueStrike& c : strikes) {
        ofs::Rng used(5);
        strike(behind.start, behind.cue_ball, c, t, PhysicsParams{}, used);
        ofs::Rng expected(5);
        expected.next_u64();
        expected.next_u64();
        CHECK(used.next_u64() == expected.next_u64());
    }
}

TEST_CASE("cue: speed and elevation are capped") {
    const PhysicsParams p;
    const StrikeOutcome capped = hit({0.2, 0.1, 0.2, 0.1, 3.0 * p.max_cue_speed_m_s});
    const StrikeOutcome at_max = hit({0.2, 0.1, 0.2, 0.1, p.max_cue_speed_m_s});
    CHECK(capped.v == at_max.v);
    CHECK(capped.w == at_max.w);
    CHECK(hit({0.2, 0.1, 0.2, 1.5, 3.0}).elevation_used_rad == p.max_elevation_rad);
    CHECK(hit({0.2, 0.1, 0.2, -0.2, 3.0}).elevation_used_rad == 0.0);
    CHECK_FALSE(hit({0.2, 0.1, 0.2, -0.2, 3.0}).elevation_raised);
    CHECK(hit({0.2, 0.1, 0.2, 0.1, -1.0}).v == Vec3{});
}

TEST_CASE("obstruction: a cue ball touching the baulk cushion needs the cue over the rail") {
    // Spec 7.3: the cue as a tapered cylinder that must clear the rail; checked against the brute force.
    const PhysicsParams p;
    const Fixture f = load_fixture("cue_ball_on_baulk_cushion");
    const TableShape t = rectangle();
    const double got = min_cue_elevation(f.start, f.cue_ball, f.strike.aim_rad, t, p);
    const double expected =
        brute_force_elevation(f.start.balls[f.cue_ball], f.strike.aim_rad, 0.0, 0.0, 0.2, baulk_rail(t), p);
    MESSAGE("cue ball on the baulk cushion, centre ball: " << got / kDeg << " degrees");
    CHECK(got > 0.0);
    CHECK(got == doctest::Approx(expected).epsilon(1e-5));

    // Over the rail the cue rests on its edge: hitting higher lets it come down.
    for (double tip_x : {0.0, 0.3, -0.3}) {
        for (double tip_y : {0.4, 0.0, -0.3}) {
            CAPTURE(tip_x);
            CAPTURE(tip_y);
            CueStrike c = f.strike;
            c.tip_x = tip_x;
            c.tip_y = tip_y;
            ofs::Rng rng(1);
            const StrikeOutcome o = strike(f.start, f.cue_ball, c, t, p, rng);
            const BallState& cb = f.start.balls[f.cue_ball];
            const double brute = brute_force_elevation(cb, c.aim_rad, tip_x, tip_y, 0.2, baulk_rail(t), p);
            CHECK(o.elevation_raised);
            CHECK(o.elevation_used_rad == doctest::Approx(brute).epsilon(1e-5));
            if (tip_y > 0.0) CHECK(o.elevation_used_rad < got);
            if (tip_y < 0.0) CHECK(o.elevation_used_rad > got);
        }
    }
}

TEST_CASE("obstruction: a rail crossed at an angle") {
    const PhysicsParams p;
    const TableShape t = rectangle();
    const TableState s = cue_ball_at(-ofs::table::kPlayingLength / 2.0 + kR + 0.01, 0.3);
    for (double aim : {0.5, -0.9, 1.2}) {
        CAPTURE(aim);
        const double got = min_cue_elevation(s, 0, aim, t, p);
        const double expected = brute_force_elevation(s.balls[0], aim, 0.0, 0.0, 0.3, baulk_rail(t), p);
        CHECK(got > 0.0);
        CHECK(got == doctest::Approx(expected).epsilon(1e-5));
    }
}

TEST_CASE("obstruction: a ball just behind the cue ball needs the cue over it") {
    const PhysicsParams p;
    const Fixture f = load_fixture("ball_behind_cue_ball");
    const TableShape t = rectangle();
    const BallState& red = f.start.balls[1];
    const double got = min_cue_elevation(f.start, f.cue_ball, f.strike.aim_rad, t, p);
    const double expected =
        brute_force_elevation(f.start.balls[f.cue_ball], f.strike.aim_rad, 0.0, 0.0, 0.2, ball_solid(red), p);
    MESSAGE("red 7.5 mm behind the cue ball, centre ball: " << got / kDeg << " degrees");
    CHECK(got > 0.0);
    CHECK(got == doctest::Approx(expected).epsilon(1e-5));

    ofs::Rng rng(1);
    const StrikeOutcome raised = strike(f.start, f.cue_ball, f.strike, t, p, rng);
    CHECK(raised.elevation_raised);
    CHECK(raised.elevation_used_rad == got);

    CueStrike steeper = f.strike;
    steeper.elevation_rad = got + 0.1;
    const StrikeOutcome kept = strike(f.start, f.cue_ball, steeper, t, p, rng);
    CHECK_FALSE(kept.elevation_raised);
    CHECK(kept.elevation_used_rad == steeper.elevation_rad);

    TableState potted = f.start;
    potted.balls[1].motion = Motion::pocketed;
    CHECK(min_cue_elevation(potted, f.cue_ball, f.strike.aim_rad, t, p) == 0.0);
}

TEST_CASE("obstruction: balls partly in the way, with side and screw") {
    const PhysicsParams p;
    const TableShape t = rectangle();
    TableState s = cue_ball_at(0.0, 0.0);
    s.balls.push_back(ball_at(ofs::table::Ball::red, -0.08, 0.02));
    s.balls.push_back(ball_at(ofs::table::Ball::blue, -0.5, -0.03));
    for (double tip_x : {0.0, 0.4}) {
        for (double tip_y : {0.0, -0.4}) {
            for (int target : {1, 2}) {
                CAPTURE(tip_x);
                CAPTURE(tip_y);
                CAPTURE(target);
                TableState one = cue_ball_at(0.0, 0.0);
                one.balls.push_back(s.balls[static_cast<std::size_t>(target)]);
                const CueStrike c{0.0, tip_x, tip_y, 0.0, 2.0};
                ofs::Rng rng(1);
                const double got = strike(one, 0, c, t, p, rng).elevation_used_rad;
                const double expected =
                    brute_force_elevation(one.balls[0], 0.0, tip_x, tip_y, 0.6, ball_solid(one.balls[1]), p);
                CHECK(got > 0.0);
                CHECK(got == doctest::Approx(expected).epsilon(1e-5));
            }
        }
    }
}

TEST_CASE("obstruction: random rails and balls match the brute force") {
    const PhysicsParams p;
    const TableShape rails = rectangle();
    ofs::Rng rng(2026);
    for (int i = 0; i < 40; ++i) {
        CAPTURE(i);
        const bool near_rail = i % 2 == 0;
        TableState s = near_rail ? cue_ball_at(-ofs::table::kPlayingLength / 2.0 + kR + rng.uniform(0.0, 0.05),
                                               rng.uniform(-0.5, 0.5))
                                 : cue_ball_at(0.0, 0.0);
        const double aim = rng.uniform(-1.2, 1.2);
        const double offset = rng.uniform(0.0, p.miscue_limit);
        const double around = rng.uniform(0.0, 2.0 * kPi);
        const CueStrike c{aim, offset * std::cos(around), offset * std::sin(around), 0.0, 2.0};
        ofs::Rng unused(1);
        double got = 0.0;
        double brute = 0.0;
        if (near_rail) {
            got = strike(s, 0, c, rails, p, unused).elevation_used_rad;
            brute = brute_force_elevation(s.balls[0], aim, c.tip_x, c.tip_y, 0.25, baulk_rail(rails), p);
        } else {
            const double behind = rng.uniform(2.0 * kR + 0.001, 0.3);
            const double side = rng.uniform(-0.04, 0.04);
            s.balls.push_back(ball_at(ofs::table::Ball::red, -behind * std::cos(aim) + side * std::sin(aim),
                                      -behind * std::sin(aim) - side * std::cos(aim)));
            got = strike(s, 0, c, TableShape{}, p, unused).elevation_used_rad;
            brute = brute_force_elevation(s.balls[0], aim, c.tip_x, c.tip_y, 0.4, ball_solid(s.balls[1]), p);
        }
        CHECK(got == doctest::Approx(brute).epsilon(1e-5));
    }
}

TEST_CASE("obstruction: nothing in the way gives 0") {
    const PhysicsParams p;
    const TableShape t = rectangle();
    // Up the table from the centre the rail is further back than the cue is long.
    CHECK(min_cue_elevation(cue_ball_at(0.0, 0.0), 0, 0.0, t, p) == 0.0);
    CHECK(min_cue_elevation(cue_ball_at(0.0, 0.0), 0, kPi, t, p) == 0.0);

    TableState s = cue_ball_at(0.0, 0.0);
    s.balls.push_back(ball_at(ofs::table::Ball::red, -0.1, 0.1));
    s.balls.push_back(ball_at(ofs::table::Ball::black, 0.1, 0.0));
    CHECK(min_cue_elevation(s, 0, 0.0, t, p) == 0.0);
}

TEST_CASE("obstruction: a ball on the cue's line beyond its butt is not in the way") {
    const PhysicsParams p;
    const TableShape t = rectangle();
    TableState s = cue_ball_at(1.2, 0.0);
    s.balls.push_back(ball_at(ofs::table::Ball::red, 1.2 - kR - p.cue_length_m - 0.05, 0.0));
    CHECK(min_cue_elevation(s, 0, 0.0, t, p) == 0.0);
    // 10 cm closer it lies under the butt end, and the cue has to go over.
    s.balls[1].r.x += 0.1;
    CHECK(min_cue_elevation(s, 0, 0.0, t, p) > 0.0);
}

TEST_CASE("obstruction: the rail's far edge can set the elevation alone") {
    // The cue thickens towards the butt, so held level it can clear the near edge of the rail top
    // and still clip the far one. With the 40 mm rail that takes a tip high on the ball: tip_y 0.8
    // miscues, but the miscue's speed still follows the elevation. 6.5 cm from the cushion the
    // level cue clears the near edge by 0.2 mm.
    const PhysicsParams p;
    const TableShape t = rectangle();
    const TableState s = cue_ball_at(-ofs::table::kPlayingLength / 2.0 + 0.065, 0.0);
    const CueStrike c{0.0, 0.0, 0.8, 0.0, 2.0};
    TableShape near_edge_only = t;
    near_edge_only.rail_width_m = 0.0;
    CHECK(brute_force_elevation(s.balls[0], 0.0, 0.0, 0.8, 0.3, baulk_rail(near_edge_only), p) == 0.0);
    const double got = min_cue_elevation(s, 0, c, t, p);
    const double brute = brute_force_elevation(s.balls[0], 0.0, 0.0, 0.8, 0.3, baulk_rail(t), p);
    MESSAGE("far edge alone: " << got / kDeg << " degrees, brute force " << brute / kDeg);
    CHECK(got > 0.0);
    CHECK(std::abs(got - brute) < 1e-6);
}

TEST_CASE("obstruction: across the table the cue rises a little over the far rail") {
    const PhysicsParams p;
    const TableShape t = rectangle();
    const double nose = -ofs::table::kPlayingWidth / 2.0;
    auto right_rail = [nose, t](const Vec3& q) {
        const double dy = std::max({nose - t.rail_width_m - q.y, q.y - nose, 0.0});
        return std::hypot(dy, std::max(q.z - t.rail_height_m, 0.0));
    };
    const TableState s = cue_ball_at(0.0, 0.0);
    const double got = min_cue_elevation(s, 0, kPi / 2.0, t, p);
    const double expected =
        brute_force_elevation(s.balls[0], kPi / 2.0, 0.0, 0.0, p.cue_length_m, right_rail, p);
    MESSAGE("centre of the table, aiming across: " << got / kDeg << " degrees");
    CHECK(got > 0.0);
    CHECK(got == doctest::Approx(expected).epsilon(1e-4));
}

TEST_CASE("obstruction: a jaw in the way counts only on its arc") {
    const PhysicsParams p;
    TableShape t;
    t.rail_height_m = 0.04;
    t.rail_width_m = 0.05;
    ArcCushion jaw;
    jaw.centre = {-0.1, 0.0};
    jaw.radius = 0.02;
    jaw.angle_start = -kPi / 2.0;
    jaw.angle_end = kPi / 2.0;
    jaw.pocket = 0;
    t.arcs.push_back(jaw);
    const TableState s = cue_ball_at(0.0, 0.0);
    auto disc = [jaw, t](const Vec3& q) {
        const double dh = std::max(std::hypot(q.x - jaw.centre.x, q.y - jaw.centre.y) - jaw.radius, 0.0);
        return std::hypot(dh, std::max(q.z - t.rail_height_m, 0.0));
    };
    const double got = min_cue_elevation(s, 0, 0.0, t, p);
    CHECK(got > 0.0);
    CHECK(got == doctest::Approx(brute_force_elevation(s.balls[0], 0.0, 0.0, 0.0, 0.2, disc, p)).epsilon(1e-5));

    t.arcs[0].angle_start = kPi / 2.0;
    t.arcs[0].angle_end = 3.0 * kPi / 2.0;
    CHECK(min_cue_elevation(s, 0, 0.0, t, p) == 0.0);
}

TEST_CASE("obstruction: the minimum is capped at max_elevation_rad") {
    const PhysicsParams p;
    TableShape t = rectangle();
    t.rail_height_m = 0.5;
    const Fixture f = load_fixture("cue_ball_on_baulk_cushion");
    CHECK(min_cue_elevation(f.start, f.cue_ball, 0.0, t, p) == p.max_elevation_rad);
    ofs::Rng rng(3);
    const StrikeOutcome o = strike(f.start, f.cue_ball, f.strike, t, p, rng);
    CHECK(o.elevation_used_rad == p.max_elevation_rad);
    CHECK(o.elevation_raised);
}

TEST_CASE("obstruction: the strike overload uses the tip offsets") {
    // cue.hpp: the CueStrike form gives what strike() uses, so the elevation control can show it.
    const PhysicsParams p;
    const Fixture f = load_fixture("cue_ball_on_baulk_cushion");
    const TableShape t = rectangle();
    CueStrike centre = f.strike;
    centre.tip_x = 0.0;
    centre.tip_y = 0.0;
    CHECK(min_cue_elevation(f.start, f.cue_ball, centre, t, p) ==
          min_cue_elevation(f.start, f.cue_ball, f.strike.aim_rad, t, p));
    CueStrike high = centre;
    high.tip_y = 0.4;
    ofs::Rng rng(1);
    const double expected = min_cue_elevation(f.start, f.cue_ball, high, t, p);
    CHECK(expected < min_cue_elevation(f.start, f.cue_ball, centre, t, p));
    high.elevation_rad = 0.0;
    CHECK(strike(f.start, f.cue_ball, high, t, p, rng).elevation_used_rad == expected);
}
