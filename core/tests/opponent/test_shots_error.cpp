// A level's error (spec 10.3, docs/opponent/README.md "Error", error_model.hpp): the formula for the
// aim's sigma, the chance it gives a pot, and the error drawn into a stroke.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/rng.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::scenario::place_of;

namespace {

constexpr double kSqrt2 = 1.41421356237309504880;

Level made_up() {
    Level l;
    l.id = "test";
    l.name = "Test";
    l.aim_sigma_rad = 0.01;
    l.aim_sigma_per_cut = 0.5;
    l.aim_sigma_per_m = 0.2;
    l.awkward_factor = 2.0;
    l.speed_sigma = 0.1;
    l.tip_sigma = 0.05;
    return l;
}

Pot hand_pot(double cut, double cue_m, bool awkward, double margin = 0.01) {
    Pot p;
    p.target = 1;
    p.pocket = 0;
    p.cut_rad = cut;
    p.cue_travel_m = cue_m;
    p.object_travel_m = 0.5;
    p.awkward = awkward;
    p.margin_rad = margin;
    return p;
}

Spec level_cue(double elevation = 0.0) { return Spec{2.0, 0.0, 0.0, elevation}; }

phys::CueStrike meant_stroke() {
    phys::CueStrike c;
    c.aim_rad = 0.3;
    c.speed_m_s = 2.0;
    c.tip_x = 0.1;
    c.tip_y = -0.2;
    c.elevation_rad = 0.05;
    return c;
}

Spec spec_of(const phys::CueStrike& c) { return Spec{c.speed_m_s, c.tip_x, c.tip_y, c.elevation_rad}; }

struct Moments {
    double mean = 0.0;
    double sd = 0.0;
};

Moments moments(const std::vector<double>& xs) {
    Moments m;
    if (xs.empty()) return m;
    for (const double x : xs) m.mean += x;
    m.mean /= double(xs.size());
    double ss = 0.0;
    for (const double x : xs) ss += (x - m.mean) * (x - m.mean);
    m.sd = std::sqrt(ss / double(xs.size()));
    return m;
}

int top_left() {
    const auto& pockets = rules::scenario::rig().table.pockets;
    for (std::size_t i = 0; i < pockets.size(); ++i) {
        if (pockets[i].name == "top_left") return int(i);
    }
    return -1;
}

bool fixture_pot(const char* name, Pot& out) {
    const rules::FrameState s = rig::position(name);
    const int red = place_of(s, Ball::red);
    for (const Pot& p : pots_from(s, s.balls[std::size_t(s.cue_ball)].pos, rig::env(), 1.35)) {
        if (p.target == red && p.pocket == top_left()) {
            out = p;
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("README, Error: aim sigma = aim_sigma_rad * (1 + per_cut * cut) * (1 + per_m * cue travel)") {
    const Level l = made_up();
    CHECK(aim_sigma(l, hand_pot(0.0, 0.0, false), level_cue()) == doctest::Approx(0.01).epsilon(1e-12));
    CHECK(aim_sigma(l, hand_pot(0.4, 1.5, false), level_cue()) ==
          doctest::Approx(0.01 * 1.2 * 1.3).epsilon(1e-12));
    CHECK(aim_sigma(l, hand_pot(1.0, 0.5, false), level_cue()) ==
          doctest::Approx(0.01 * 1.5 * 1.1).epsilon(1e-12));
    // Below kRaised_rad the cue's elevation changes nothing.
    CHECK(aim_sigma(l, hand_pot(0.4, 1.5, false), level_cue(0.1)) ==
          doctest::Approx(0.01 * 1.2 * 1.3).epsilon(1e-12));
}

TEST_CASE("README, Error: the awkward factor applies once for awkward cueing, once for a raised cue, and once for both") {
    const Level l = made_up();
    const double plain = 0.01 * 1.2 * 1.3;
    CHECK(aim_sigma(l, hand_pot(0.4, 1.5, true), level_cue()) == doctest::Approx(plain * 2.0).epsilon(1e-12));
    CHECK(aim_sigma(l, hand_pot(0.4, 1.5, false), level_cue(kRaised_rad + 0.05)) ==
          doctest::Approx(plain * 2.0).epsilon(1e-12));
    CHECK(aim_sigma(l, hand_pot(0.4, 1.5, true), level_cue(kRaised_rad + 0.05)) ==
          doctest::Approx(plain * 2.0).epsilon(1e-12));
}

TEST_CASE("README, Error: chance = erf(window / (sigma sqrt 2)): 0.6827 at one sigma, 0.9545 at two") {
    Level l = made_up();
    l.aim_sigma_per_cut = 0.0;
    l.aim_sigma_per_m = 0.0;
    const Pot p = hand_pot(0.3, 1.0, false);
    CHECK(std::abs(pot_chance(l, p, level_cue(), 0.01) - 0.6827) < 1e-4);
    CHECK(std::abs(pot_chance(l, p, level_cue(), 0.02) - 0.9545) < 1e-4);
    const Level full = made_up();
    const Pot q = hand_pot(0.4, 1.5, true);
    const double sigma = 0.01 * 1.2 * 1.3 * 2.0;
    CHECK(pot_chance(full, q, level_cue(), 0.005) == doctest::Approx(std::erf(0.005 / (sigma * kSqrt2))).epsilon(1e-9));
}

TEST_CASE("error_model.hpp: a level with no error pots every open window; a closed window never pots") {
    const Level& maximum = rig::levels().maximum;
    const Level& beginner = rig::levels().beginner;
    const Pot p = hand_pot(0.8, 2.0, true);
    CHECK(pot_chance(maximum, p, level_cue(), 0.0005) == 1.0);
    CHECK(pot_chance(maximum, p, level_cue(0.3), 1e-6) == 1.0);
    CHECK(pot_chance(maximum, p, level_cue(), 0.0) == 0.0);
    CHECK(pot_chance(beginner, p, level_cue(), 0.0) == 0.0);
    CHECK(pot_chance(beginner, p, level_cue(), -0.01) == 0.0);
}

TEST_CASE("spec 10.3: at Beginner's values a longer, a thinner and an awkward pot each have less chance") {
    const Level& b = rig::levels().beginner;
    const double base = pot_chance(b, hand_pot(0.2, 0.5, false), level_cue(), 0.01);
    CHECK(base > 0.0);
    CHECK(base < 1.0);
    CHECK(pot_chance(b, hand_pot(0.2, 2.0, false), level_cue(), 0.01) < base);
    CHECK(pot_chance(b, hand_pot(0.9, 0.5, false), level_cue(), 0.01) < base);
    CHECK(pot_chance(b, hand_pot(0.2, 0.5, true), level_cue(), 0.01) < base);
    CHECK(pot_chance(b, hand_pot(0.2, 0.5, false), level_cue(0.3), 0.01) < base);
}

TEST_CASE("spec 10.3: on the table, Beginner's chance of the long and the cut red is below the straight red's") {
    const Level& b = rig::levels().beginner;
    Pot straight;
    Pot cut;
    Pot longest;
    REQUIRE(fixture_pot("straight_red", straight));
    REQUIRE(fixture_pot("cut_red", cut));
    REQUIRE(fixture_pot("long_red", longest));
    const double c_straight = pot_chance(b, straight, level_cue(), straight.margin_rad);
    CHECK(c_straight > 0.0);
    CHECK(pot_chance(b, cut, level_cue(), cut.margin_rad) < c_straight);
    CHECK(pot_chance(b, longest, level_cue(), longest.margin_rad) < c_straight);
    CHECK(aim_sigma(b, longest, level_cue()) > aim_sigma(b, straight, level_cue()));
    CHECK(aim_sigma(b, cut, level_cue()) > aim_sigma(b, straight, level_cue()));
}

TEST_CASE("README, The 147: with the 147's level the stroke played is the stroke meant") {
    const phys::CueStrike meant = meant_stroke();
    const Pot p = hand_pot(0.5, 1.0, true);
    for (const Pot* pot : {static_cast<const Pot*>(nullptr), &p}) {
        Rng rng(11);
        Drawn drawn;
        const phys::CueStrike played = with_error(meant, rig::levels().maximum, pot, rig::env(), rng, &drawn);
        CHECK(played.aim_rad == meant.aim_rad);
        CHECK(played.speed_m_s == meant.speed_m_s);
        CHECK(played.tip_x == meant.tip_x);
        CHECK(played.tip_y == meant.tip_y);
        CHECK(played.elevation_rad == meant.elevation_rad);
        CHECK(drawn.aim_rad == 0.0);
        CHECK(drawn.speed == 0.0);
        CHECK(drawn.tip_x == 0.0);
        CHECK(drawn.tip_y == 0.0);
    }
}

TEST_CASE("spec 10.3: Beginner's aim error over 20,000 strokes is centred and has its sigma (5% of sigma, 3%)") {
    const Level& b = rig::levels().beginner;
    phys::CueStrike meant = meant_stroke();
    meant.elevation_rad = 0.0;
    const Pot p = hand_pot(0.5, 1.0, false);
    struct Case {
        const Pot* pot;
        double sigma;
    };
    const Case cases[] = {{nullptr, b.aim_sigma_rad}, {&p, aim_sigma(b, p, spec_of(meant))}};
    for (const Case& c : cases) {
        CAPTURE(c.sigma);
        REQUIRE(c.sigma > 0.0);
        Rng rng(2026);
        std::vector<double> errors;
        for (int i = 0; i < 20000; ++i) {
            errors.push_back(with_error(meant, b, c.pot, rig::env(), rng).aim_rad - meant.aim_rad);
        }
        const Moments m = moments(errors);
        CHECK(std::abs(m.mean) < 0.05 * c.sigma);
        CHECK(m.sd == doctest::Approx(c.sigma).epsilon(0.03));
    }
}

TEST_CASE("spec 10.3: Beginner's speed error is a fraction of the speed with sd speed_sigma, tip error tip_sigma per axis (3%)") {
    const Level& b = rig::levels().beginner;
    phys::CueStrike meant = meant_stroke();
    meant.tip_x = 0.0;
    meant.tip_y = 0.0;
    meant.elevation_rad = 0.0;
    Rng rng(77);
    std::vector<double> speed;
    std::vector<double> tip_x;
    std::vector<double> tip_y;
    for (int i = 0; i < 20000; ++i) {
        const phys::CueStrike c = with_error(meant, b, nullptr, rig::env(), rng);
        speed.push_back((c.speed_m_s - meant.speed_m_s) / meant.speed_m_s);
        tip_x.push_back(c.tip_x);
        tip_y.push_back(c.tip_y);
    }
    CHECK(moments(speed).sd == doctest::Approx(b.speed_sigma).epsilon(0.03));
    CHECK(std::abs(moments(speed).mean) < 0.05 * b.speed_sigma);
    CHECK(moments(tip_x).sd == doctest::Approx(b.tip_sigma).epsilon(0.03));
    CHECK(moments(tip_y).sd == doctest::Approx(b.tip_sigma).epsilon(0.03));
}

TEST_CASE("error_model.hpp: whatever the sigmas, the stroke is one the game accepts, its tip's direction kept") {
    Level wild = made_up();
    wild.aim_sigma_rad = 1.0;
    wild.speed_sigma = 5.0;
    wild.tip_sigma = 5.0;
    const double lo = kLeastSpeed_m_s;
    const double hi = rig::env().params.max_cue_speed_m_s;
    const double tip_limit = kTipLimit * rig::env().params.miscue_limit;
    const phys::CueStrike meant = meant_stroke();
    const Pot p = hand_pot(0.5, 1.0, true);
    Rng rng(3);
    int clamped_tips = 0;
    for (int i = 0; i < 2000; ++i) {
        Drawn drawn;
        const phys::CueStrike c = with_error(meant, wild, (i % 2) ? &p : nullptr, rig::env(), rng, &drawn);
        CHECK(c.speed_m_s >= lo);
        CHECK(c.speed_m_s <= hi);
        CHECK(std::hypot(c.tip_x, c.tip_y) <= tip_limit + 1e-12);
        CHECK(c.elevation_rad == meant.elevation_rad);
        CHECK(std::isfinite(c.aim_rad));
        const double rx = meant.tip_x + drawn.tip_x;
        const double ry = meant.tip_y + drawn.tip_y;
        if (std::hypot(rx, ry) > tip_limit) {
            ++clamped_tips;
            CHECK(std::abs(rx * c.tip_y - ry * c.tip_x) <= 1e-9 * std::hypot(rx, ry));
            CHECK(rx * c.tip_x + ry * c.tip_y > 0.0);
        }
    }
    CHECK(clamped_tips > 100);
}

TEST_CASE("README, Error: four normals are always drawn, whatever the level") {
    Level wild = made_up();
    wild.aim_sigma_rad = 1.0;
    wild.speed_sigma = 5.0;
    wild.tip_sigma = 5.0;
    const Pot p = hand_pot(0.5, 1.0, false);
    for (const Level* level : {&rig::levels().beginner, &rig::levels().maximum, static_cast<const Level*>(&wild)}) {
        for (const Pot* pot : {static_cast<const Pot*>(nullptr), &p}) {
            CAPTURE(level->id);
            for (std::uint64_t seed = 1; seed <= 20; ++seed) {
                Rng used(seed);
                Rng counted(seed);
                with_error(meant_stroke(), *level, pot, rig::env(), used);
                for (int k = 0; k < 4; ++k) counted.normal();
                CHECK(used.next_u64() == counted.next_u64());
                CHECK(used.normal() == counted.normal());
            }
        }
    }
}

TEST_CASE("README, Error: the normals go to aim, speed, tip x and tip y in that order, and drawn is what was added") {
    const Level& b = rig::levels().beginner;
    phys::CueStrike meant = meant_stroke();
    meant.tip_x = 0.0;
    meant.tip_y = 0.0;
    const Pot p = hand_pot(0.5, 1.0, false);
    const double sigma = aim_sigma(b, p, spec_of(meant));
    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
        CAPTURE(seed);
        Rng rng(seed);
        Rng shadow(seed);
        const double n[4] = {shadow.normal(), shadow.normal(), shadow.normal(), shadow.normal()};
        Drawn drawn;
        const phys::CueStrike c = with_error(meant, b, &p, rig::env(), rng, &drawn);
        CHECK(drawn.aim_rad == doctest::Approx(sigma * n[0]).epsilon(1e-12));
        CHECK(drawn.speed == doctest::Approx(b.speed_sigma * n[1]).epsilon(1e-12));
        CHECK(drawn.tip_x == doctest::Approx(b.tip_sigma * n[2]).epsilon(1e-12));
        CHECK(drawn.tip_y == doctest::Approx(b.tip_sigma * n[3]).epsilon(1e-12));
        // No clamp can apply here: speed and tip stay many sigmas inside their limits.
        CHECK(c.aim_rad == doctest::Approx(meant.aim_rad + drawn.aim_rad).epsilon(1e-12));
        CHECK(c.speed_m_s == doctest::Approx(meant.speed_m_s * (1.0 + drawn.speed)).epsilon(1e-12));
        CHECK(c.tip_x == doctest::Approx(meant.tip_x + drawn.tip_x).epsilon(1e-12));
        CHECK(c.tip_y == doctest::Approx(meant.tip_y + drawn.tip_y).epsilon(1e-12));
        CHECK(c.elevation_rad == meant.elevation_rad);
    }
}

TEST_CASE("README, how a stroke is chosen: the same seed draws the same error") {
    const Level& b = rig::levels().beginner;
    const Pot p = hand_pot(0.5, 1.0, true);
    Rng a(99);
    Rng c(99);
    const phys::CueStrike x = with_error(meant_stroke(), b, &p, rig::env(), a);
    const phys::CueStrike y = with_error(meant_stroke(), b, &p, rig::env(), c);
    CHECK(x.aim_rad == y.aim_rad);
    CHECK(x.speed_m_s == y.speed_m_s);
    CHECK(x.tip_x == y.tip_x);
    CHECK(x.tip_y == y.tip_y);
}
