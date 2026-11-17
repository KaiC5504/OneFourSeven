#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "ofs/opponent/error_model.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;

namespace {

Level by_hand() {
    Level l;
    l.aim_sigma_rad = 0.01;
    l.aim_sigma_per_cut = 0.5;
    l.aim_sigma_per_m = 0.2;
    l.awkward_factor = 2.0;
    return l;
}

Pot pot_of(double cut_rad, double cue_travel_m, bool awkward = false) {
    Pot p;
    p.cut_rad = cut_rad;
    p.cue_travel_m = cue_travel_m;
    p.awkward = awkward;
    p.margin_rad = 0.02;
    return p;
}

phys::CueStrike stroke(double aim, double speed, double tip_x = 0.0, double tip_y = 0.0, double elevation = 0.0) {
    phys::CueStrike c;
    c.aim_rad = aim;
    c.speed_m_s = speed;
    c.tip_x = tip_x;
    c.tip_y = tip_y;
    c.elevation_rad = elevation;
    return c;
}

struct Moments {
    double mean = 0.0;
    double sd = 0.0;
};

Moments moments(const std::vector<double>& xs) {
    double sum = 0.0;
    for (double x : xs) sum += x;
    const double mean = sum / double(xs.size());
    double sq = 0.0;
    for (double x : xs) sq += (x - mean) * (x - mean);
    return {mean, std::sqrt(sq / double(xs.size() - 1))};
}

}  // namespace

TEST_CASE("aim_sigma grows with the cut and the cue travel, the awkward factor applied once") {
    const Level l = by_hand();
    const double plain = 0.01 * (1.0 + 0.5 * 0.4) * (1.0 + 0.2 * 1.5);
    CHECK(aim_sigma(l, pot_of(0.4, 1.5), Spec{}) == doctest::Approx(plain).epsilon(1e-12));
    CHECK(aim_sigma(l, pot_of(0.0, 0.0), Spec{}) == doctest::Approx(0.01).epsilon(1e-12));

    Spec raised;
    raised.elevation_rad = 0.3;
    CHECK(aim_sigma(l, pot_of(0.4, 1.5, true), Spec{}) == doctest::Approx(2.0 * plain).epsilon(1e-12));
    CHECK(aim_sigma(l, pot_of(0.4, 1.5), raised) == doctest::Approx(2.0 * plain).epsilon(1e-12));
    CHECK(aim_sigma(l, pot_of(0.4, 1.5, true), raised) == doctest::Approx(2.0 * plain).epsilon(1e-12));

    Spec at_the_line;
    at_the_line.elevation_rad = kRaised_rad;
    CHECK(aim_sigma(l, pot_of(0.4, 1.5), at_the_line) == doctest::Approx(plain).epsilon(1e-12));
}

TEST_CASE("pot_chance is the normal's mass inside the window") {
    Level l;
    l.aim_sigma_rad = 0.01;
    const Pot straight = pot_of(0.0, 0.0);
    CHECK(pot_chance(l, straight, Spec{}, 0.01) == doctest::Approx(0.6827).epsilon(1e-4));
    CHECK(pot_chance(l, straight, Spec{}, 0.02) == doctest::Approx(0.9545).epsilon(1e-4));
    CHECK(pot_chance(l, straight, Spec{}, 0.0) == 0.0);
    CHECK(pot_chance(l, straight, Spec{}, -0.01) == 0.0);

    const Level& maximum = rig::levels().maximum;
    CHECK(pot_chance(maximum, pot_of(0.9, 3.0, true), Spec{}, 1e-6) == 1.0);
    CHECK(pot_chance(maximum, straight, Spec{}, 0.0) == 0.0);
}

TEST_CASE("pot_chance rises with the window and falls with the cut and the cue travel") {
    const Level& b = rig::levels().beginner;
    const Pot base = pot_of(0.3, 1.0);
    const double at = pot_chance(b, base, Spec{}, 0.01);
    CHECK(at > 0.0);
    CHECK(at < 1.0);
    CHECK(pot_chance(b, base, Spec{}, 0.015) > at);
    CHECK(pot_chance(b, pot_of(0.6, 1.0), Spec{}, 0.01) < at);
    CHECK(pot_chance(b, pot_of(0.3, 2.0), Spec{}, 0.01) < at);
    CHECK(pot_chance(b, pot_of(0.3, 1.0, true), Spec{}, 0.01) < at);
}

TEST_CASE("the 147 plays the stroke it meant, bit for bit") {
    const Level& maximum = rig::levels().maximum;
    const Env env = rig::env();
    const phys::CueStrike meant = stroke(0.7123, 2.5, 0.1, -0.2, 0.05);
    const Pot pot = pot_of(0.5, 1.2, true);
    for (const Pot* p : {static_cast<const Pot*>(nullptr), &pot}) {
        Rng rng(42);
        Drawn drawn{1.0, 1.0, 1.0, 1.0};
        const phys::CueStrike out = with_error(meant, maximum, p, env, rng, &drawn);
        CHECK(out.aim_rad == meant.aim_rad);
        CHECK(out.speed_m_s == meant.speed_m_s);
        CHECK(out.tip_x == meant.tip_x);
        CHECK(out.tip_y == meant.tip_y);
        CHECK(out.elevation_rad == meant.elevation_rad);
        CHECK(drawn.aim_rad == 0.0);
        CHECK(drawn.speed == 0.0);
        CHECK(drawn.tip_x == 0.0);
        CHECK(drawn.tip_y == 0.0);
    }
}

TEST_CASE("Beginner's error has the spread its values give") {
    const Level& b = rig::levels().beginner;
    const Env env = rig::env();
    const Pot pot = pot_of(0.3, 1.0);
    const phys::CueStrike meant = stroke(1.0, 2.0);
    const double sigma = aim_sigma(b, pot, Spec{2.0, 0.0, 0.0, 0.0});

    const int n = 20000;
    std::vector<double> aim, speed, tip_x, tip_y;
    Rng rng(2026);
    for (int i = 0; i < n; ++i) {
        const phys::CueStrike out = with_error(meant, b, &pot, env, rng);
        aim.push_back(out.aim_rad - meant.aim_rad);
        speed.push_back(out.speed_m_s / meant.speed_m_s - 1.0);
        tip_x.push_back(out.tip_x);
        tip_y.push_back(out.tip_y);
        CHECK(out.elevation_rad == meant.elevation_rad);
    }
    const auto near = [](const Moments& m, double sd) {
        CHECK(std::abs(m.mean) < 0.03 * sd);
        CHECK(m.sd == doctest::Approx(sd).epsilon(0.03));
    };
    near(moments(aim), sigma);
    near(moments(speed), b.speed_sigma);
    near(moments(tip_x), b.tip_sigma);
    near(moments(tip_y), b.tip_sigma);

    std::vector<double> plain_aim;
    Rng plain(7);
    for (int i = 0; i < n; ++i) plain_aim.push_back(with_error(meant, b, nullptr, env, plain).aim_rad - meant.aim_rad);
    near(moments(plain_aim), b.aim_sigma_rad);
}

TEST_CASE("with_error always draws exactly four normals") {
    const Env env = rig::env();
    const Pot pot = pot_of(0.4, 1.0);
    const phys::CueStrike meant = stroke(0.2, 1.5, 0.05, 0.0);
    for (const Level* level : {&rig::levels().beginner, &rig::levels().maximum}) {
        for (const Pot* p : {static_cast<const Pot*>(nullptr), &pot}) {
            for (std::uint64_t seed : {1u, 99u, 123456u}) {
                Rng through(seed);
                Rng counted(seed);
                with_error(meant, *level, p, env, through);
                for (int i = 0; i < 4; ++i) counted.normal();
                CHECK(through.next_u64() == counted.next_u64());
            }
        }
    }
}

TEST_CASE("with_error gives a stroke the game accepts whatever the sigmas") {
    const Env env = rig::env();
    Level wild;
    wild.aim_sigma_rad = 1.0;
    wild.speed_sigma = 5.0;
    wild.tip_sigma = 3.0;
    const double tip_limit = kTipLimit * env.params.miscue_limit;
    const Pot pot = pot_of(0.8, 2.0, true);
    Rng rng(5);
    int clamped_tips = 0;
    int clamped_speeds = 0;
    for (int i = 0; i < 2000; ++i) {
        const phys::CueStrike meant = stroke(0.0, i % 2 == 0 ? 0.5 : 6.0, 0.3, -0.3, 0.25);
        Drawn drawn;
        const phys::CueStrike out = with_error(meant, wild, i % 3 == 0 ? nullptr : &pot, env, rng, &drawn);
        CHECK(out.speed_m_s >= kLeastSpeed_m_s);
        CHECK(out.speed_m_s <= env.params.max_cue_speed_m_s);
        CHECK(std::hypot(out.tip_x, out.tip_y) <= tip_limit * (1.0 + 1e-12));
        CHECK(out.elevation_rad == meant.elevation_rad);

        const double pre_x = meant.tip_x + drawn.tip_x;
        const double pre_y = meant.tip_y + drawn.tip_y;
        if (std::hypot(pre_x, pre_y) > tip_limit) {
            ++clamped_tips;
            CHECK(std::hypot(out.tip_x, out.tip_y) == doctest::Approx(tip_limit).epsilon(1e-12));
            CHECK(out.tip_x * pre_y - out.tip_y * pre_x == doctest::Approx(0.0).epsilon(1e-12));
            CHECK(out.tip_x * pre_x + out.tip_y * pre_y > 0.0);
        }
        const double pre_speed = meant.speed_m_s * (1.0 + drawn.speed);
        if (pre_speed < kLeastSpeed_m_s || pre_speed > env.params.max_cue_speed_m_s) ++clamped_speeds;
    }
    CHECK(clamped_tips > 100);
    CHECK(clamped_speeds > 100);
}

TEST_CASE("drawn is what was added, and a raised cue's aim is drawn wider") {
    const Level& b = rig::levels().beginner;
    const Env env = rig::env();
    const Pot pot = pot_of(0.2, 0.8);
    const phys::CueStrike meant = stroke(-0.4, 2.0, 0.05, 0.1);
    Rng rng(31);
    for (int i = 0; i < 200; ++i) {
        Drawn drawn;
        const phys::CueStrike out = with_error(meant, b, &pot, env, rng, &drawn);
        CHECK(out.aim_rad == meant.aim_rad + drawn.aim_rad);
        CHECK(out.speed_m_s == meant.speed_m_s * (1.0 + drawn.speed));
        CHECK(out.tip_x == meant.tip_x + drawn.tip_x);
        CHECK(out.tip_y == meant.tip_y + drawn.tip_y);
    }

    phys::CueStrike raised = meant;
    raised.elevation_rad = 0.3;
    Rng level_rng(8);
    Rng raised_rng(8);
    Drawn level_drawn;
    Drawn raised_drawn;
    with_error(meant, b, &pot, env, level_rng, &level_drawn);
    with_error(raised, b, &pot, env, raised_rng, &raised_drawn);
    CHECK(raised_drawn.aim_rad == doctest::Approx(b.awkward_factor * level_drawn.aim_rad).epsilon(1e-12));
    CHECK(raised_drawn.speed == level_drawn.speed);
}

TEST_CASE("the same seed gives the same stroke") {
    const Level& b = rig::levels().beginner;
    const Env env = rig::env();
    const Pot pot = pot_of(0.5, 1.5);
    const phys::CueStrike meant = stroke(2.0, 3.0, -0.1, 0.2, 0.1);
    Rng first(77);
    Rng second(77);
    for (int i = 0; i < 50; ++i) {
        const phys::CueStrike a = with_error(meant, b, &pot, env, first);
        const phys::CueStrike c = with_error(meant, b, &pot, env, second);
        CHECK(a.aim_rad == c.aim_rad);
        CHECK(a.speed_m_s == c.speed_m_s);
        CHECK(a.tip_x == c.tip_x);
        CHECK(a.tip_y == c.tip_y);
    }
}
