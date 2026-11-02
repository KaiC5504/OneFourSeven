#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "ofs/physics/collide.hpp"
#include "ofs/physics/params.hpp"
#include "ofs/rng.hpp"

using namespace ofs;
using namespace ofs::phys;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double R = table::kBallRadius;

// A cushion along the x axis facing -y: its frame (x along, y into it) is the table frame, so
// the reference below can work on the ball's numbers directly.
const LineCushion kCushion{{-10.0, 0.0}, {10.0, 0.0}, {0.0, -1.0}};

// vx, vy, R wx, R wy, R wz in that frame.
using Local = std::array<double, 5>;

// The reference: Mathavan, Jackson & Parkin (2010) section 3.4, forward Euler in the normal
// impulse with dP = (1 + e) M V0 sin(alpha) / N and N = 5000 (the paper's choice), slip angles
// from the current state, the work at I by the trapezium rule (16a), compression until the
// normal velocity is zero and restitution until e^2 of its work is returned (16b). The same
// scheme as pooltool's physics/resolve/ball_cushion/mathavan_2010/model.py (Apache-2.0).
Local mathavan_reference(Local y, double sin_theta, double mu_w, double mu_s, double e, int n = 5000) {
    const double st = sin_theta;
    const double ct = std::sqrt(1.0 - st * st);
    const double dp = (1.0 + e) * y[1] / n;
    auto rates = [&](const Local& s) {
        const double sIx = s[0] + s[3] * st - s[4] * ct;
        const double sIy = -s[1] * st + s[2];
        const double sCx = s[0] - s[3];
        const double sCy = s[1] + s[2];
        const double nI = std::hypot(sIx, sIy);
        const double nC = std::hypot(sCx, sCy);
        const double fIx = nI > 0.0 ? -mu_w * sIx / nI : 0.0;
        const double fIy = nI > 0.0 ? -mu_w * sIy / nI : 0.0;
        const double nc = std::max(0.0, st - fIy * ct);
        const double fCx = nC > 0.0 ? -mu_s * nc * sCx / nC : 0.0;
        const double fCy = nC > 0.0 ? -mu_s * nc * sCy / nC : 0.0;
        return Local{fIx + fCx, -ct - fIy * st + fCy, 2.5 * (fIy + fCy), 2.5 * (fIx * st - fCx), -2.5 * fIx * ct};
    };
    auto advance = [](Local& s, const Local& r, double h) {
        for (std::size_t i = 0; i < 5; ++i) s[i] += r[i] * h;
    };
    double work = 0.0;
    for (;;) {
        const Local r = rates(y);
        if (y[1] + r[1] * dp <= 0.0) {
            const double h = y[1] / -r[1];
            work += h * ct * y[1] / 2.0;
            advance(y, r, h);
            y[1] = 0.0;
            break;
        }
        const double before = y[1];
        advance(y, r, dp);
        work += dp * ct * (before + y[1]) / 2.0;
    }
    const double target = e * e * work;
    double done = 0.0;
    for (;;) {
        const Local r = rates(y);
        const double dw = dp * ct * (-2.0 * y[1] - r[1] * dp) / 2.0;
        if (done + dw >= target) {
            const double qa = -ct * r[1] / 2.0;
            const double qb = -ct * y[1];
            const double h = (-qb + std::sqrt(qb * qb + 4.0 * qa * (target - done))) / (2.0 * qa);
            advance(y, r, h);
            return y;
        }
        done += dw;
        advance(y, r, dp);
    }
}

double sin_theta(const PhysicsParams& p) { return (p.cushion_contact_height_m - R) / R; }

BallState ball_from_local(const Local& y) {
    BallState b;
    b.ball = table::Ball::cue;
    b.r = {0.0, -R, 0.0};
    b.v = {y[0], y[1], 0.0};
    b.w = {y[2] / R, y[3] / R, y[4] / R};
    b.motion = Motion::sliding;
    return b;
}

Local local_from_ball(const BallState& b) { return {b.v.x, b.v.y, R * b.w.x, R * b.w.y, R * b.w.z}; }

Local hit_line(const Local& y, const PhysicsParams& p) {
    BallState b = ball_from_local(y);
    collide_line(b, kCushion, p);
    return local_from_ball(b);
}

// Mathavan section 3.1: speed v0 at alpha from the cushion line, topspin k_top v0 / R and
// sidespin k_side v0 / R (positive side is running for a ball moving along +x).
Local paper_state(double v0, double alpha, double k_top, double k_side) {
    return {v0 * std::cos(alpha), v0 * std::sin(alpha), -k_top * v0 * std::sin(alpha), k_top * v0 * std::cos(alpha),
            k_side * v0};
}

double speed(const Local& y) { return std::hypot(y[0], y[1]); }
// Rebound angle from the cushion line, degrees.
double rebound_deg(const Local& y) { return std::atan2(-y[1], y[0]) * 180.0 / kPi; }
// Rebound angle from the cushion's normal, degrees, positive towards +x.
double from_normal_deg(const Local& y) { return std::atan2(y[0], -y[1]) * 180.0 / kPi; }

double energy(const Local& y) {
    // Per unit mass: v^2 / 2 + (2/5) R^2 w^2 / 2.
    return 0.5 * (y[0] * y[0] + y[1] * y[1]) + 0.2 * (y[2] * y[2] + y[3] * y[3] + y[4] * y[4]);
}

}  // namespace

TEST_CASE("cushion: fast Mathavan model matches the 5000-step reference over the C7 grid") {
    // Spec 7.11 C7; Mathavan et al. (2010). Incidence 5-85 degrees, 0.2-6 m/s, plain, top, stun,
    // screw and running or check side.
    const PhysicsParams p;
    double worst_speed = 0.0;
    double worst_angle = 0.0;
    int cases = 0;
    for (int alpha_deg = 5; alpha_deg <= 85; alpha_deg += 10) {
        for (double v0 : {0.2, 1.0, 3.0, 6.0}) {
            for (double top : {-1.0, 0.0, 0.5, 1.0, 2.0}) {
                for (double side : {-2.0, -1.0, 0.0, 1.0, 2.0}) {
                    const Local y0 = paper_state(v0, alpha_deg * kPi / 180.0, top, side);
                    const Local ref = mathavan_reference(y0, sin_theta(p), p.cushion_friction,
                                                         p.cushion_table_friction, p.cushion_restitution);
                    const Local got = hit_line(y0, p);
                    const double ds = std::abs(speed(got) - speed(ref)) / speed(ref);
                    const double da = std::abs(rebound_deg(got) - rebound_deg(ref));
                    worst_speed = std::max(worst_speed, ds);
                    worst_angle = std::max(worst_angle, da);
                    CHECK_MESSAGE(ds <= 0.005, "alpha " << alpha_deg << " v0 " << v0 << " top " << top
                                                         << " side " << side);
                    CHECK_MESSAGE(da <= 0.1, "alpha " << alpha_deg << " v0 " << v0 << " top " << top
                                                       << " side " << side);
                    ++cases;
                }
            }
        }
    }
    MESSAGE("C7 grid, " << cases << " impacts: worst speed error " << worst_speed * 100.0
                        << "%, worst angle error " << worst_angle << " deg");
}

TEST_CASE("cushion: a rolling ball at normal incidence rebounds at Mathavan's 0.91 of its speed") {
    // Mathavan et al. (2010) Fig. 7(b): the model's rebound for a rolling ball at normal incidence
    // with e = 0.98 and mu_w = 0.14 is 0.900-0.912 of the incident speed (digitised).
    const PhysicsParams p;
    for (double v0 : {0.3, 1.0, 2.0}) {
        const Local got = hit_line(paper_state(v0, kPi / 2.0, 1.0, 0.0), p);
        CHECK(speed(got) / v0 == doctest::Approx(0.906).epsilon(0.01));
    }
}

TEST_CASE("cushion: normal incidence without side gives no sideways velocity") {
    // Symmetry of Mathavan's equations about the plane of incidence.
    const PhysicsParams p;
    for (double top : {-1.0, 0.0, 1.0, 2.0}) {
        const Local got = hit_line({0.0, 1.5, -top * 1.5, 0.0, 0.0}, p);
        CHECK(std::abs(got[0]) < 1e-12);
        CHECK(std::abs(got[3]) < 1e-12);
        CHECK(std::abs(got[4]) < 1e-12);
        CHECK(got[1] < 0.0);
    }
}

TEST_CASE("cushion: mirrored incidence gives the mirrored rebound") {
    // Reflecting x -> -x flips vx, wy and wz (w is a pseudovector).
    const PhysicsParams p;
    Rng rng(147);
    for (int i = 0; i < 200; ++i) {
        const double v0 = rng.uniform(0.1, 5.0);
        const Local y0 = paper_state(v0, rng.uniform(0.05, kPi - 0.05), rng.uniform(-1.0, 2.0), rng.uniform(-2.0, 2.0));
        const Local mirrored_in{-y0[0], y0[1], y0[2], -y0[3], -y0[4]};
        const Local a = hit_line(y0, p);
        const Local b = hit_line(mirrored_in, p);
        const double tol = 1e-12 * v0;
        CHECK(std::abs(b[0] + a[0]) <= tol);
        CHECK(std::abs(b[1] - a[1]) <= tol);
        CHECK(std::abs(b[2] - a[2]) <= tol);
        CHECK(std::abs(b[3] + a[3]) <= tol);
        CHECK(std::abs(b[4] + a[4]) <= tol);
    }
}

TEST_CASE("cushion: running side widens the rebound and check side narrows it") {
    // Spec 7.11 C8; Mathavan et al. (2010) Fig. 10: running ("right") side rebounds wider and
    // faster, check side narrower and, with enough of it, back to the side it came from.
    const PhysicsParams p;
    for (int alpha_deg : {30, 45, 60}) {
        const double a = alpha_deg * kPi / 180.0;
        const Local plain = hit_line(paper_state(1.0, a, 1.0, 0.0), p);
        const Local running = hit_line(paper_state(1.0, a, 1.0, 1.0), p);
        const Local check = hit_line(paper_state(1.0, a, 1.0, -1.0), p);
        const double plain_deg = from_normal_deg(plain);
        MESSAGE("incidence " << 90 - alpha_deg << " deg from normal: plain " << plain_deg << ", running "
                             << from_normal_deg(running) << ", check " << from_normal_deg(check));
        CHECK(from_normal_deg(running) > plain_deg + 3.0);
        CHECK(from_normal_deg(check) < plain_deg - 0.3);
        CHECK(speed(running) > speed(plain));
        if (alpha_deg == 45) {
            CHECK(from_normal_deg(running) - plain_deg > 5.0);
            CHECK(plain_deg - from_normal_deg(check) > 1.0);
        }
    }
    // Heavy check side near normal incidence comes back on the side it approached from.
    const Local back = hit_line(paper_state(1.0, 80.0 * kPi / 180.0, 1.0, -2.0), p);
    CHECK(back[0] < 0.0);
}

TEST_CASE("cushion: kinetic energy never increases and the ball always leaves") {
    // Spec 7.11 C12, collide.hpp. 10,000 random impacts with random spin and random cushion
    // parameters across their valid ranges.
    Rng rng(20261102);
    double worst_gain = -1.0;
    for (int i = 0; i < 10000; ++i) {
        PhysicsParams p;
        p.cushion_restitution = rng.uniform(0.3, 1.0);
        p.cushion_friction = rng.uniform(0.0, 0.5);
        p.cushion_table_friction = rng.uniform(0.0, 0.5);
        p.cushion_contact_height_m = rng.uniform(1.1 * R, 1.6 * R);
        p.cushion_restitution_slope_per_m_s = rng.uniform() < 0.5 ? 0.0 : rng.uniform(0.0, 0.5);
        const double v0 = rng.uniform(0.01, 8.0);
        const double a = rng.uniform(0.01, kPi - 0.01);
        const Local y0{v0 * std::cos(a), v0 * std::sin(a), rng.uniform(-3.0, 3.0) * v0, rng.uniform(-3.0, 3.0) * v0,
                       rng.uniform(-3.0, 3.0) * v0};
        const Local got = hit_line(y0, p);
        bool finite = true;
        for (double x : got) finite = finite && std::isfinite(x);
        REQUIRE(finite);
        const double gain = (energy(got) - energy(y0)) / energy(y0);
        worst_gain = std::max(worst_gain, gain);
        CHECK_MESSAGE(gain <= 1e-9, "impact " << i);
        CHECK(got[1] <= 0.0);
    }
    MESSAGE("largest relative energy change over 10,000 impacts: " << worst_gain);
}

TEST_CASE("cushion: the fraction of speed lost does not depend on speed, the amount lost does") {
    // Mathavan et al. (2010) section 4, Fig. 8: rebound angles are identical at every speed and
    // rebound speeds scale with it; the model has no speed scale of its own. With the restitution
    // slope at 0 that holds at every speed.
    PhysicsParams p;
    p.cushion_restitution_slope_per_m_s = 0.0;
    for (int alpha_deg : {20, 50, 90}) {
        const Local slow_in = paper_state(0.3, alpha_deg * kPi / 180.0, 1.0, 0.5);
        const Local fast_in = paper_state(4.0, alpha_deg * kPi / 180.0, 1.0, 0.5);
        const Local slow = hit_line(slow_in, p);
        const Local fast = hit_line(fast_in, p);
        CHECK(speed(slow) / 0.3 == doctest::Approx(speed(fast) / 4.0).epsilon(1e-9));
        CHECK(rebound_deg(slow) == doctest::Approx(rebound_deg(fast)).epsilon(1e-9));
        CHECK(4.0 - speed(fast) > 0.3 - speed(slow));
        CHECK(energy(fast) < energy(fast_in));
    }
}

TEST_CASE("cushion: a restitution slope lowers rebounds only above the rigid-cushion limit") {
    // Mathavan et al. (2010) Fig. 7: measured rebounds fall below the rigid model above 2.5 m/s.
    PhysicsParams flat;
    flat.cushion_restitution_slope_per_m_s = 0.0;
    PhysicsParams sloped = flat;
    sloped.cushion_restitution_slope_per_m_s = 0.16;
    const Local below = paper_state(2.0, kPi / 2.0, 1.0, 0.0);
    const Local above = paper_state(3.4, kPi / 2.0, 1.0, 0.0);
    CHECK(speed(hit_line(below, sloped)) == speed(hit_line(below, flat)));
    CHECK(speed(hit_line(above, sloped)) < 0.95 * speed(hit_line(above, flat)));
    // Oblique: only the normal component counts, 4 m/s at 30 degrees is 2 m/s into the cushion.
    const Local oblique = paper_state(4.0, kPi / 6.0, 1.0, 0.0);
    CHECK(speed(hit_line(oblique, sloped)) == speed(hit_line(oblique, flat)));
    // The jaws follow the same rule with their own restitution.
    const ArcCushion jaw{{0.0, 0.0}, 0.03, -kPi, 0.0, 0};
    auto hit_jaw = [&](double v, const PhysicsParams& p) {
        BallState b;
        b.r = {0.0, -(0.03 + R), 0.0};
        b.v = {0.0, v, 0.0};
        b.w = {-v / R, 0.0, 0.0};
        collide_arc(b, jaw, p);
        return std::hypot(b.v.x, b.v.y);
    };
    CHECK(hit_jaw(2.0, sloped) == hit_jaw(2.0, flat));
    CHECK(hit_jaw(3.4, sloped) < hit_jaw(3.4, flat));
}

TEST_CASE("cushion: a jaw hit on its centre line rebounds straight back") {
    // collide.hpp: the jaw's normal runs from the arc centre to the ball.
    const PhysicsParams p;
    const ArcCushion jaw{{0.2, 0.4}, 0.05, -kPi, 0.0, 0};
    for (double dir_deg : {-90.0, -60.0, -135.0}) {
        const double d = dir_deg * kPi / 180.0;
        const double nx = std::cos(d);
        const double ny = std::sin(d);
        BallState b;
        b.r = {0.2 + nx * (0.05 + R), 0.4 + ny * (0.05 + R), 0.0};
        b.v = {-nx * 1.2, -ny * 1.2, 0.0};
        // Rolling towards the jaw: w = (z x v) / R.
        b.w = {ny * 1.2 / R, -nx * 1.2 / R, 0.0};
        collide_arc(b, jaw, p);
        const double along = b.v.x * nx + b.v.y * ny;
        const double across = -b.v.x * ny + b.v.y * nx;
        CHECK(along > 0.0);
        CHECK(std::abs(across) < 1e-12);
    }
}

TEST_CASE("cushion: an off-centre jaw hit deflects away from the jaw, and jaws use jaw restitution") {
    PhysicsParams p;
    const ArcCushion jaw{{0.0, 0.0}, 0.04, -kPi, 0.0, 0};
    for (double off : {0.005, 0.02, 0.05}) {
        for (double sign : {1.0, -1.0}) {
            BallState b;
            const double x = sign * off;
            b.r = {x, -std::sqrt((0.04 + R) * (0.04 + R) - x * x), 0.0};
            b.v = {0.0, 1.0, 0.0};
            b.w = {-1.0 / R, 0.0, 0.0};
            collide_arc(b, jaw, p);
            CHECK(b.v.x * sign > 0.0);
            CHECK(b.v.y < 1.0);
        }
    }
    // Straight on, a jaw with lower restitution than the cushion sends the ball back slower.
    p.jaw_restitution = 0.6;
    p.jaw_friction = p.cushion_friction;
    BallState on_jaw;
    on_jaw.r = {0.0, -(0.04 + R), 0.0};
    on_jaw.v = {0.0, 1.0, 0.0};
    on_jaw.w = {-1.0 / R, 0.0, 0.0};
    collide_arc(on_jaw, jaw, p);
    const Local on_line = hit_line({0.0, 1.0, -1.0, 0.0, 0.0}, p);
    CHECK(std::abs(on_jaw.v.y) < 0.8 * speed(on_line));
}

TEST_CASE("cushion: a ball moving away from the cushion is left alone") {
    const PhysicsParams p;
    const Local y0{0.3, -0.5, 0.2, 0.1, 0.4};
    const Local got = hit_line(y0, p);
    for (std::size_t i = 0; i < 5; ++i) CHECK(got[i] == y0[i]);
}

TEST_CASE("cushion: micro-benchmark" * doctest::skip()) {
    const PhysicsParams p;
    Rng rng(7);
    std::vector<Local> inputs;
    for (int i = 0; i < 4096; ++i) {
        const double v0 = rng.uniform(0.2, 6.0);
        inputs.push_back(paper_state(v0, rng.uniform(5.0, 85.0) * kPi / 180.0, rng.uniform(-1.0, 2.0),
                                     rng.uniform(-2.0, 2.0)));
    }
    const int rounds = 50;
    double sink = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int r = 0; r < rounds; ++r) {
        for (const Local& y : inputs) sink += hit_line(y, p)[0];
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    std::printf("cushion collision: %.3f us each (%d impacts, checksum %.6f)\n",
                us / (rounds * static_cast<double>(inputs.size())), rounds * static_cast<int>(inputs.size()), sink);
}
