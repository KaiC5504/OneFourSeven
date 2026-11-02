#include "calib.hpp"

using namespace calib;

namespace {

// Alciatore TP A.31 eq. 13 (https://billiards.colostate.edu/technical_proofs/new/TP_A-31.pdf):
// tan(squirt) = (5/2) b sqrt(1 - b^2) / (1 + m_ball/m_end + (5/2)(1 - b^2)), b the tip offset over R.
double tp_a31_squirt(double b, double ball_mass, double end_mass) {
    return std::atan(2.5 * b * std::sqrt(1.0 - b * b) / (1.0 + ball_mass / end_mass + 2.5 * (1.0 - b * b)));
}

StrikeOutcome strike_alone(Vec3 at, const CueStrike& c) {
    TableState s;
    s.balls = {make_ball(table::Ball::cue, 0, at)};
    Rng rng(11);
    return strike(s, 0, c, open_table(), params(), rng);
}

}  // namespace

TEST_CASE("C9 maximum side squirts 1-3 degrees, right-hand side to the left (Alciatore squirt FAQ, TP A.31)") {
    const PhysicsParams& p = params();
    // Just inside the miscue limit: the most side a player can use.
    const double side = p.miscue_limit - 1e-3;
    for (double aim : {0.0, 0.7, -2.0}) {
        for (double speed : {1.0, 3.0, 6.0}) {
            for (double sign : {1.0, -1.0}) {
                const CueStrike c{aim, sign * side, 0.0, 0.0, speed};
                const StrikeOutcome out = strike_alone({0.0, 0.0, 0.0}, c);
                CAPTURE(aim);
                CAPTURE(speed);
                CAPTURE(c.tip_x);
                INFO("outcome v " << vec_str(out.v) << " w " << vec_str(out.w) << " squirt_rad " << out.squirt_rad
                                  << " elevation_used " << out.elevation_used_rad);
                REQUIRE_FALSE(out.miscue);
                REQUIRE_MESSAGE(norm_xy(out.v) > 0.0, "the strike produced no velocity");
                const double squirt = wrap(angle_of(out.v) - aim);
                const double deg = squirt / kDeg;
                // Spec 7.3/7.11: 1-3 degrees at maximum side (Alciatore's measured range for real cues
                // at 50% side is about 1.4-3.5 degrees, TP A.31 p. 6).
                CHECK_MESSAGE((std::abs(deg) >= 1.0 && std::abs(deg) <= 3.0),
                              "squirt at maximum side " << deg << " deg, expected 1-3 deg (TP A.31 with the loaded "
                                                        << "cue_end_mass_kg gives "
                                                        << tp_a31_squirt(side, p.ball_mass_kg, p.cue_end_mass_kg) / kDeg
                                                        << ")");
                // Right-hand side (tip_x > 0) pushes the ball left of the aim: anticlockwise, positive.
                CHECK_MESSAGE(squirt * sign > 0.0, "squirt went " << deg << " deg for tip_x " << c.tip_x
                                                                   << " (right-hand side must squirt left, +)");
                CHECK_MESSAGE(std::abs(out.squirt_rad - squirt) <= 1e-9,
                              "squirt_rad " << out.squirt_rad << " disagrees with the velocity's angle " << squirt);
            }
        }
    }
}

TEST_CASE("C9 squirt follows TP A.31's end-mass formula over the tip offsets") {
    const PhysicsParams& p = params();
    for (double b : {0.1, 0.25, 0.4, 0.5}) {
        const StrikeOutcome out = strike_alone({0.0, 0.0, 0.0}, CueStrike{0.0, b, 0.0, 0.0, 2.0});
        const double got = angle_of(out.v) / kDeg;
        const double expected = tp_a31_squirt(b, p.ball_mass_kg, p.cue_end_mass_kg) / kDeg;
        CAPTURE(b);
        // 0.1 degree: the impulse model and the formula share their physics (pooltool squirt.py
        // implements TP A.31), so they differ only in how the offset is measured.
        CHECK_MESSAGE(std::abs(got - expected) <= 0.1,
                      "squirt " << got << " deg at tip offset " << b << " R, TP A.31 gives " << expected
                                << " deg for m_end " << p.cue_end_mass_kg << " kg");
    }
}

TEST_CASE("C10 in the strike and motion models alone (cue.hpp, motion.hpp)") {
    const PhysicsParams& p = params();
    const double mu = p.slide_friction;
    const double g = p.gravity_m_s2;
    for (double elevation_deg : {20.0, 30.0, 40.0}) {
        for (double sign : {1.0, -1.0}) {
            const StrikeOutcome out = strike_alone({-1.2, 0.0, 0.0}, CueStrike{0.0, sign * 0.4, 0.0, elevation_deg * kDeg, 4.0});
            BallState b = make_ball(table::Ball::cue, 0, {-1.2, 0.0, 0.0}, out.v, out.w);
            b.motion = classify(b, p);
            CAPTURE(elevation_deg);
            CAPTURE(sign);
            INFO("strike v " << vec_str(out.v) << " w " << vec_str(out.w));
            REQUIRE(b.motion == Motion::sliding);
            CHECK_MESSAGE(b.v.y * sign > 0.0, "not squirted away from the side");
            const Vec3 u0 = contact_slip(b);
            const double t_roll = 2.0 * norm_xy(u0) / (7.0 * mu * g);
            const double t = transition_time(b, p, nap_acceleration(b, p));
            CHECK_MESSAGE(std::abs(t - t_roll) <= 1e-9 * t_roll, "transition_time " << t << ", TP A.4 " << t_roll);
            double prev = angle_of(b.v);
            for (int i = 1; i <= 8; ++i) {
                const BallState bt = evolve(b, t_roll * i / 9.0, p, {});
                CHECK_MESSAGE(wrap(angle_of(bt.v) - prev) * sign < 0.0, "path does not curve towards the side");
                prev = angle_of(bt.v);
            }
            const BallState rolled = complete_transition(evolve(b, t, p, {}), p);
            CHECK(rolled.motion == Motion::rolling);
            const Vec3 nap = nap_acceleration(rolled, p);
            if (norm_xy(rolled.v) > p.nap_speed_max_m_s) {
                const double dt = 0.9 * (norm_xy(rolled.v) - p.nap_speed_max_m_s) / (p.roll_friction * g);
                const BallState later = evolve(rolled, std::min(dt, transition_time(rolled, p, nap)), p, nap);
                CHECK_MESSAGE(std::abs(wrap(angle_of(later.v) - angle_of(rolled.v))) <= 1e-9,
                              "rolling ball turned: it should run straight");
            }
        }
    }
}

TEST_CASE("C10 swerve: an elevated cue with side curves towards the side while sliding, straight once rolling") {
    const PhysicsParams& p = params();
    const double mu = p.slide_friction;
    const double g = p.gravity_m_s2;
    for (double elevation_deg : {20.0, 30.0, 40.0}) {
        for (double speed : {2.0, 4.0}) {
            for (double sign : {1.0, -1.0}) {
                SimInput in;
                const Vec3 r0{-1.2, 0.0, 0.0};
                in.start.balls = {make_ball(table::Ball::cue, 0, r0)};
                in.strike = CueStrike{0.0, sign * 0.4, 0.0, elevation_deg * kDeg, speed};
                in.seed = 13;
                const ShotRecord rec = simulate(in, open_table(), p);
                CAPTURE(elevation_deg);
                CAPTURE(speed);
                CAPTURE(in.strike.tip_x);
                INFO("record: " << record_summary(rec));
                REQUIRE(!rec.segments.empty());
                REQUIRE(!rec.segments[0].empty());
                const BallState b0 = rec.segments[0][0].start;
                INFO("first segment v " << vec_str(b0.v) << " w " << vec_str(b0.w) << " " << motion_name(b0.motion));
                REQUIRE_MESSAGE(b0.motion == Motion::sliding, "an elevated side shot should start sliding");
                CHECK_MESSAGE(std::abs(rec.strike.elevation_used_rad - in.strike.elevation_rad) <= 1e-12,
                              "elevation changed on an open table: " << rec.strike.elevation_used_rad);

                // Squirt: the ball starts off on the side opposite to the side used.
                CHECK_MESSAGE(b0.v.y * sign > 0.0, "initial direction " << vec_str(b0.v)
                                                                        << " is not squirted away from the side");

                // TP A.4: while sliding, friction mu g acts against the slip u = v + w x (0, 0, -R),
                // whose direction stays fixed, so the path is a parabola until t = 2|u0| / (7 mu g).
                const Vec3 u0 = b0.v + cross(b0.w, Vec3{0.0, 0.0, -R});
                const Vec3 uh = unit_or_zero(Vec3{u0.x, u0.y, 0.0});
                const double t_roll = 2.0 * norm_xy(u0) / (7.0 * mu * g);
                const ShotEvent* rolls = first_transition(rec, 0, Motion::sliding, Motion::rolling);
                REQUIRE_MESSAGE(rolls != nullptr, "the swerving ball never started rolling");
                CHECK_MESSAGE(std::abs(rolls->t - t_roll) <= 1e-6 * t_roll,
                              "rolled at t = " << rolls->t << " s, TP A.4 / 4.1 give 2|u0|/(7 mu g) = " << t_roll);
                double prev_heading = angle_of(b0.v);
                for (int i = 1; i <= 8; ++i) {
                    const double t = t_roll * i / 9.0;
                    const BallState bt = rec.ball_at(0, t);
                    const Vec3 r_expected = r0 + b0.v * t - uh * (0.5 * mu * g * t * t);
                    CHECK_MESSAGE(norm(bt.r - r_expected) <= 1e-9,
                                  "sliding path at t = " << t << ": " << vec_str(bt.r) << ", TP A.4 parabola "
                                                         << vec_str(r_expected));
                    const double heading = angle_of(bt.v);
                    // Right-hand side (sign +) swerves right: the heading turns clockwise.
                    CHECK_MESSAGE(wrap(heading - prev_heading) * sign < 0.0,
                                  "heading went from " << prev_heading << " to " << heading << " rad at t = " << t
                                                       << ": the path must curve towards the side");
                    prev_heading = heading;
                }

                // Rolling: no further curve while the nap is out of play (above nap_speed_max_m_s).
                const Segment* roll = segment_from(rec, 0, rolls->t);
                REQUIRE(roll != nullptr);
                const double v_roll = norm_xy(roll->start.v);
                if (v_roll > p.nap_speed_max_m_s) {
                    const double t_fast = (v_roll - p.nap_speed_max_m_s) / (p.roll_friction * g);
                    const BallState later = rec.ball_at(0, rolls->t + 0.9 * t_fast);
                    const double turn = std::abs(wrap(angle_of(later.v) - angle_of(roll->start.v)));
                    CHECK_MESSAGE(turn <= 1e-9, "rolling ball turned " << turn << " rad: it should run straight");
                }
            }
        }
    }
}
