#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

#include "ofs/physics/collide.hpp"
#include "ofs/rng.hpp"

using namespace ofs;
using namespace ofs::phys;

// Sources: Alciatore, TP A.5 (normal impulse), TP A.14 (throw, eq. 3-17), TP A.27 and the spin
// transfer FAQ (billiards.colostate.edu/faq/throw/spin-transfer/), the maximum throw FAQ
// (faq/throw/maximum/: "about 5 degrees" under typical conditions) and "Throw - Part II: results"
// (Billiards Digest, September 2006). TP A.14's typical speeds are 1, 3 and 7 mph.

namespace {

constexpr double R = table::kBallRadius;
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kSlow = 0.447;    // 1 mph, TP A.14
constexpr double kMedium = 1.341;  // 3 mph
constexpr double kFast = 3.129;    // 7 mph

double deg(double radians) { return radians * 180.0 / kPi; }
double rad(double degrees) { return degrees * kPi / 180.0; }

BallState ball(Vec3 r, Vec3 v, Vec3 w) {
    BallState b;
    b.r = r;
    b.v = v;
    b.w = w;
    return b;
}

// Kept here rather than motion.hpp's kinetic_energy so these tests stand on their own.
double energy(const BallState& b, const PhysicsParams& p) {
    return 0.5 * p.ball_mass_kg * (norm2(b.v) + 0.4 * R * R * norm2(b.w));
}

struct Pair {
    BallState a;
    BallState b;
};

// A cue ball at the origin moving +x at v with spin w hits an object ball at rest whose centre is
// at cut angle phi to the left (a half-ball hit is phi = 30 degrees).
Pair cut_shot(double v, double phi, Vec3 w, const PhysicsParams& p) {
    Pair s{ball({}, {v, 0.0, 0.0}, w), ball({2.0 * R * std::cos(phi), 2.0 * R * std::sin(phi), 0.0}, {}, {})};
    collide_balls(s.a, s.b, p);
    return s;
}

// Positive when the object ball is thrown off the line of centres towards the cue ball's
// direction of travel (cut-induced throw's direction).
double throw_angle(const BallState& ob, double phi) { return phi - std::atan2(ob.v.y, ob.v.x); }

// TP A.14 eq. 15-17 for a cue ball at speed v with topspin w_top (w.y) and side w_side (w.z)
// cutting at phi. TP A.14 eq. 6 assumes e = 1 (F_n = m v cos phi); with restitution the normal
// impulse is (1 + e)/2 of that.
double alciatore_throw(double v, double phi, double w_top, double w_side, const PhysicsParams& p) {
    const double tangential = v * std::sin(phi) - R * w_side;
    const double vertical = R * w_top * std::cos(phi);
    const double v_rel = std::sqrt(tangential * tangential + vertical * vertical);
    if (v_rel == 0.0) return 0.0;
    const double mu = p.ball_friction_a + p.ball_friction_b * std::exp(-p.ball_friction_c * v_rel);
    const double jn = 0.5 * (1.0 + p.ball_restitution) * v * std::cos(phi);
    return std::atan(std::min(mu * jn / v_rel, 1.0 / 7.0) * tangential / jn);
}

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// The slip of a over b at the contact, as collide_ball.cpp defines it.
Vec3 contact_slip_between(const BallState& a, const BallState& b) {
    const Vec3 n = unit_or_zero(b.r - a.r);
    const Vec3 vc = a.v - b.v + cross(a.w + b.w, n) * R;
    return vc - n * dot(vc, n);
}

Vec3 random_spin(Rng& rng) {
    // Up to about 1.5x the spin of a ball rolling at 10 m/s, and often far less.
    const double scale = 600.0 * std::pow(10.0, rng.uniform(-4.0, 0.0));
    return {rng.uniform(-scale, scale), rng.uniform(-scale, scale), rng.uniform(-scale, scale)};
}

// A random approaching pair in contact: speeds 0.01-10 m/s (log-uniform), any directions and
// spins, the object ball at rest a quarter of the time, any contact angle.
Pair random_contact(Rng& rng) {
    const double ta = rng.uniform(0.0, 2.0 * kPi);
    const double tb = rng.uniform(0.0, 2.0 * kPi);
    const double tn = rng.uniform(0.0, 2.0 * kPi);
    const double sa = std::pow(10.0, rng.uniform(-2.0, 1.0));
    const double sb = rng.uniform() < 0.25 ? 0.0 : std::pow(10.0, rng.uniform(-2.0, 1.0));
    Pair s;
    const Vec3 ra{rng.uniform(-1.5, 1.5), rng.uniform(-0.8, 0.8), 0.0};
    s.a = ball(ra, {sa * std::cos(ta), sa * std::sin(ta), 0.0}, random_spin(rng));
    const Vec3 wb = rng.uniform() < 0.25 ? Vec3{} : random_spin(rng);
    s.b = ball({}, {sb * std::cos(tb), sb * std::sin(tb), 0.0}, wb);
    Vec3 n{std::cos(tn), std::sin(tn), 0.0};
    if (dot(s.a.v - s.b.v, n) < 0.0) n = -n;
    s.b.r = s.a.r + n * (2.0 * R);
    return s;
}

}  // namespace

TEST_CASE("ball-ball: a head-on stun hit leaves (1-e)/2 v with the cue ball and gives (1+e)/2 v") {
    // TP A.5 eq. 8 with restitution; spec 7.11 C2. No spin means no slip, so no friction.
    for (double e : {0.94, 0.5, 1.0, 0.0}) {
        PhysicsParams p;
        p.ball_restitution = e;
        for (double v : {0.01, kSlow, kFast, 10.0}) {
            CAPTURE(e);
            CAPTURE(v);
            Pair s = cut_shot(v, 0.0, {}, p);
            CHECK(s.a.v.x == doctest::Approx(0.5 * (1.0 - e) * v).epsilon(1e-12));
            CHECK(s.b.v.x == doctest::Approx(0.5 * (1.0 + e) * v).epsilon(1e-12));
            CHECK(s.a.v.y == 0.0);
            CHECK(s.b.v.y == 0.0);
            CHECK(s.a.w == Vec3{});
            CHECK(s.b.w == Vec3{});
        }
    }
}

TEST_CASE("ball-ball: momentum is conserved, energy never rises and no slip reverses (C12)") {
    // 10,000 random contacts per parameter set: the shipped values, perfectly elastic balls (only
    // friction dissipates), and sticky balls (mu = 0.6, mostly stuck at the contact).
    PhysicsParams shipped;
    PhysicsParams elastic;
    elastic.ball_restitution = 1.0;
    PhysicsParams sticky = elastic;
    sticky.ball_friction_a = 0.6;
    sticky.ball_friction_b = 0.0;

    for (const PhysicsParams& p : {shipped, elastic, sticky}) {
        Rng rng(147);
        int contacts = 0;
        while (contacts < 10000) {
            Pair s = random_contact(rng);
            const Vec3 n = (s.b.r - s.a.r) / (2.0 * R);
            const double approach = dot(s.a.v - s.b.v, n);
            if (!(approach > 0.0)) continue;
            ++contacts;

            const Pair before = s;
            const Vec3 slip0 = contact_slip_between(s.a, s.b);
            collide_balls(s.a, s.b, p);

            REQUIRE(finite(s.a.v));
            REQUIRE(finite(s.a.w));
            REQUIRE(finite(s.b.v));
            REQUIRE(finite(s.b.w));
            CHECK(s.a.v.z == 0.0);
            CHECK(s.b.v.z == 0.0);

            const double scale = norm(before.a.v) + norm(before.b.v);
            const Vec3 dp = (s.a.v + s.b.v) - (before.a.v + before.b.v);
            CHECK(norm(dp) <= 1e-12 * scale);

            const double e0 = energy(before.a, p) + energy(before.b, p);
            const double e1 = energy(s.a, p) + energy(s.b, p);
            CHECK(e1 <= e0 * (1.0 + 1e-12));

            // Leaving at -e times the approach speed: never moving into each other afterwards.
            const double leaving = dot(s.a.v - s.b.v, n);
            CHECK(leaving == doctest::Approx(-p.ball_restitution * approach).epsilon(1e-9).scale(scale));

            // Friction slows each component of the slip (in-plane and vertical) and never reverses it.
            const Vec3 slip1 = contact_slip_between(s.a, s.b);
            const Vec3 t{-n.y, n.x, 0.0};
            const double tol = 1e-12 * (scale + norm(slip0));
            const double t0 = dot(slip0, t);
            const double t1 = dot(slip1, t);
            CHECK(t1 * (t0 < 0.0 ? -1.0 : 1.0) >= -tol);
            CHECK(std::abs(t1) <= std::abs(t0) + tol);
            CHECK(slip1.z * (slip0.z < 0.0 ? -1.0 : 1.0) >= -tol);
            CHECK(std::abs(slip1.z) <= std::abs(slip0.z) + tol);
        }
    }
}

TEST_CASE("ball-ball: a rolling half-ball hit throws the object ball forward, slow shots more (C6)") {
    // TP A.14's natural-roll curve at 30 degrees, evaluated from its eq. 15-17 with the same
    // friction fit: about 2.2 degrees at 1 mph and 0.4 at 7 mph. Throw is at most about 5 degrees
    // under typical conditions (maximum throw FAQ).
    const PhysicsParams p;
    const double phi = rad(30.0);
    const Pair slow = cut_shot(kSlow, phi, {0.0, kSlow / R, 0.0}, p);
    const Pair fast = cut_shot(kFast, phi, {0.0, kFast / R, 0.0}, p);
    const double slow_throw = throw_angle(slow.b, phi);
    const double fast_throw = throw_angle(fast.b, phi);
    MESSAGE("rolling half-ball throw: slow (1 mph) " << deg(slow_throw) << " deg, fast (7 mph) "
                                                       << deg(fast_throw) << " deg");
    CHECK(slow_throw == doctest::Approx(alciatore_throw(kSlow, phi, kSlow / R, 0.0, p)).epsilon(1e-12));
    CHECK(fast_throw == doctest::Approx(alciatore_throw(kFast, phi, kFast / R, 0.0, p)).epsilon(1e-12));
    CHECK(deg(slow_throw) > 1.5);
    CHECK(deg(slow_throw) < 3.0);
    CHECK(deg(fast_throw) > 0.2);
    CHECK(deg(fast_throw) < 1.0);
    CHECK(slow_throw > fast_throw);

    // Stun (TP A.14 stun curves; BD Sep 2006 Diagram 1, about 5-6 degrees at its slowest near a
    // half-ball hit): throw is larger without the vertical rubbing of follow.
    const Pair stun = cut_shot(kSlow, phi, {}, p);
    const double stun_throw = throw_angle(stun.b, phi);
    MESSAGE("stun half-ball throw: slow (1 mph) " << deg(stun_throw) << " deg");
    CHECK(stun_throw == doctest::Approx(alciatore_throw(kSlow, phi, 0.0, 0.0, p)).epsilon(1e-12));
    CHECK(deg(stun_throw) > 4.0);
    CHECK(deg(stun_throw) < 6.0);
    CHECK(stun_throw > slow_throw);
}

TEST_CASE("ball-ball: a resting object ball throws as TP A.14 eq. 15-17 predict for any cut, speed, spin") {
    const PhysicsParams p;
    for (double v : {kSlow, kMedium, kFast}) {
        const double roll = v / R;
        for (int cut = 0; cut <= 85; cut += 5) {
            for (double top : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
                for (double side = -1.25; side <= 1.25; side += 0.25) {
                    CAPTURE(v);
                    CAPTURE(cut);
                    CAPTURE(top);
                    CAPTURE(side);
                    const double phi = rad(cut);
                    const Pair s = cut_shot(v, phi, {0.0, top * roll, side * roll}, p);
                    const double expected = alciatore_throw(v, phi, top * roll, side * roll, p);
                    CHECK(std::abs(throw_angle(s.b, phi) - expected) < 1e-12);
                }
            }
        }
    }
}

TEST_CASE("ball-ball: side on a straight hit throws the object ball the other way and spins it opposite") {
    // Spin-induced throw (TP A.14; SIT FAQ): right side throws the object ball left. Spin
    // transfer (TP A.27; spin transfer FAQ): the friction that throws also spins the object ball,
    // the opposite way to the cue ball's side, like gears, and the cue ball loses as much.
    const PhysicsParams p;
    const double side = 0.625 * kMedium / R;  // 50% side, TP A.14's spin rate factor 1.25
    // Struck right of centre as the player sees it (towards -y), the cue ball spins
    // anticlockwise from above: w.z > 0.
    const Pair right = cut_shot(kMedium, 0.0, {0.0, 0.0, side}, p);
    MESSAGE("50% side, straight, 3 mph: throw " << deg(std::atan2(right.b.v.y, right.b.v.x))
                                                << " deg, object ball side " << right.b.w.z * R / right.b.v.x
                                                << " of its roll rate");
    CHECK(right.b.v.y > 0.0);  // thrown to the left
    CHECK(deg(std::atan2(right.b.v.y, right.b.v.x)) > 1.0);
    CHECK(deg(std::atan2(right.b.v.y, right.b.v.x)) < 5.0);
    CHECK(right.b.w.z < 0.0);
    CHECK(-right.b.w.z < side);
    // The cue ball reacts: pushed right, and its side falls by exactly what the object ball got.
    CHECK(right.a.v.y < 0.0);
    CHECK(right.a.w.z - side == doctest::Approx(right.b.w.z).epsilon(1e-12));
    // Angular and linear impulse come from the same friction force: dw = 5 dv / (2R).
    CHECK(right.b.w.z == doctest::Approx(-2.5 * right.b.v.y / R).epsilon(1e-12));

    const Pair left = cut_shot(kMedium, 0.0, {0.0, 0.0, -side}, p);
    CHECK(left.b.v.y == doctest::Approx(-right.b.v.y).epsilon(1e-12));
    CHECK(left.b.w.z == doctest::Approx(-right.b.w.z).epsilon(1e-12));

    // Top and bottom spin transfer the same way through the vertical rubbing: follow gives the
    // object ball a little bottom spin, draw a little top (spin transfer FAQ).
    const Pair follow = cut_shot(kMedium, 0.0, {0.0, kMedium / R, 0.0}, p);
    const Pair draw = cut_shot(kMedium, 0.0, {0.0, -kMedium / R, 0.0}, p);
    CHECK(follow.b.w.y < 0.0);
    CHECK(follow.a.w.y < kMedium / R);
    CHECK(draw.b.w.y > 0.0);
    CHECK(-follow.b.w.y < 0.2 * kMedium / R);
    // Straight on, vertical rubbing throws nothing sideways.
    CHECK(follow.b.v.y == 0.0);
    CHECK(follow.b.v.z == 0.0);
}

TEST_CASE("ball-ball: no slip at the contact means no friction impulse") {
    const PhysicsParams p;
    // Spin about the line of centres moves neither contact point; b's spin cancels a's otherwise.
    Pair s{ball({}, {1.0, 0.0, 0.0}, {30.0, 40.0, 25.0}),
           ball({2.0 * R, 0.0, 0.0}, {}, {-5.0, -40.0, -25.0})};
    REQUIRE(contact_slip_between(s.a, s.b) == Vec3{});
    const Pair before = s;
    collide_balls(s.a, s.b, p);
    CHECK(s.a.w == before.a.w);
    CHECK(s.b.w == before.b.w);
    CHECK(s.a.v.y == 0.0);
    CHECK(s.b.v.y == 0.0);
    CHECK(s.a.v.x == doctest::Approx(0.5 * (1.0 - p.ball_restitution)).epsilon(1e-12));
    CHECK(s.b.v.x == doctest::Approx(0.5 * (1.0 + p.ball_restitution)).epsilon(1e-12));
}

TEST_CASE("ball-ball: gearing outside side gives no throw") {
    // TP A.14 ("gearing outside english results in absolutely no throw"): with w.z = v sin(phi) / R
    // the cue ball's surface rolls across the object ball's without sliding.
    const PhysicsParams p;
    for (double v : {kSlow, kMedium, kFast}) {
        for (double cut : {10.0, 30.0, 45.0, 60.0}) {
            CAPTURE(v);
            CAPTURE(cut);
            const double phi = rad(cut);
            const double gearing = v * std::sin(phi) / R;
            const Pair s = cut_shot(v, phi, {0.0, 0.0, gearing}, p);
            CHECK(std::abs(throw_angle(s.b, phi)) < 1e-12);
            CHECK(std::abs(s.a.w.z - gearing) < 1e-9 * gearing);
            CHECK(norm(s.b.w) < 1e-9 * gearing);
        }
    }
}

TEST_CASE("ball-ball: swapping a and b, or mirroring the table, gives the same result mirrored") {
    const PhysicsParams p;
    Rng rng(2026);
    for (int i = 0; i < 1000; ++i) {
        const Pair s = random_contact(rng);

        Pair ab = s;
        collide_balls(ab.a, ab.b, p);
        Pair ba = s;
        collide_balls(ba.b, ba.a, p);
        CHECK(ab.a.v == ba.a.v);
        CHECK(ab.a.w == ba.a.w);
        CHECK(ab.b.v == ba.b.v);
        CHECK(ab.b.w == ba.b.w);

        // Reflecting y: positions and velocities flip y; spin is a pseudovector, so x and z flip.
        auto mirror = [](BallState b) {
            b.r.y = -b.r.y;
            b.v.y = -b.v.y;
            b.w.x = -b.w.x;
            b.w.z = -b.w.z;
            return b;
        };
        Pair m{mirror(s.a), mirror(s.b)};
        collide_balls(m.a, m.b, p);
        const BallState ma = mirror(m.a);
        const BallState mb = mirror(m.b);
        CHECK(ma.v == ab.a.v);
        CHECK(ma.w == ab.a.w);
        CHECK(mb.v == ab.b.v);
        CHECK(mb.w == ab.b.w);
    }
}

TEST_CASE("ball-ball: coincident centres and separating or resting pairs are left unchanged") {
    const PhysicsParams p;
    auto unchanged = [](const BallState& x, const BallState& y) {
        return x.r == y.r && x.v == y.v && x.w == y.w && x.motion == y.motion;
    };
    const Vec3 spin{10.0, -20.0, 30.0};

    // Coincident centres: no normal exists.
    Pair same{ball({0.3, 0.2, 0.0}, {1.0, 0.5, 0.0}, spin), ball({0.3, 0.2, 0.0}, {-1.0, 0.0, 0.0}, spin)};
    Pair before = same;
    collide_balls(same.a, same.b, p);
    CHECK(unchanged(same.a, before.a));
    CHECK(unchanged(same.b, before.b));

    // So close that the squared distance underflows: treated as coincident.
    Pair underflow{ball({}, {1.0, 0.0, 0.0}, spin), ball({1e-300, 0.0, 0.0}, {}, {})};
    before = underflow;
    collide_balls(underflow.a, underflow.b, p);
    CHECK(unchanged(underflow.a, before.a));
    CHECK(unchanged(underflow.b, before.b));

    // Absurdly close but representable: still a finite answer.
    Pair tiny{ball({}, {1.0, 0.0, 0.0}, spin), ball({1e-150, 1e-151, 0.0}, {}, {})};
    collide_balls(tiny.a, tiny.b, p);
    CHECK(finite(tiny.a.v));
    CHECK(finite(tiny.a.w));
    CHECK(finite(tiny.b.v));
    CHECK(finite(tiny.b.w));

    // Separating: b runs away from a faster than a follows.
    Pair apart{ball({}, {1.0, 0.0, 0.0}, spin), ball({2.0 * R, 0.0, 0.0}, {2.0, 0.0, 0.0}, {})};
    before = apart;
    collide_balls(apart.a, apart.b, p);
    CHECK(unchanged(apart.a, before.a));
    CHECK(unchanged(apart.b, before.b));

    // Touching with no closing speed (a frozen pair), however they spin.
    Pair frozen{ball({}, {0.0, 0.7, 0.0}, spin), ball({2.0 * R, 0.0, 0.0}, {0.0, 0.7, 0.0}, -spin)};
    before = frozen;
    collide_balls(frozen.a, frozen.b, p);
    CHECK(unchanged(frozen.a, before.a));
    CHECK(unchanged(frozen.b, before.b));
}

TEST_CASE("ball-ball: benchmark" * doctest::skip()) {
    // Spec 7.13: a break-off has hundreds of collisions inside a 2 ms shot, so each should stay
    // well under 0.5 us. Run a Release build with --no-skip; the copy of the pair is included.
    const PhysicsParams p;
    Rng rng(7);
    std::vector<Pair> pairs;
    for (int i = 0; i < 1024; ++i) pairs.push_back(random_contact(rng));
    constexpr int kRuns = 4000000;
    double sink = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kRuns; ++i) {
        Pair s = pairs[static_cast<std::size_t>(i) & 1023];
        collide_balls(s.a, s.b, p);
        sink += s.a.v.x + s.b.w.z;
    }
    const auto stop = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(stop - start).count() / kRuns;
    MESSAGE("collide_balls: " << ns << " ns per collision (checksum " << sink << ")");
    CHECK(std::isfinite(sink));
}
