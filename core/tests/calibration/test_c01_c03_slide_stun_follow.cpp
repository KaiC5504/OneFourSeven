#include "calib.hpp"

using namespace calib;

namespace {

void check_params_loaded() {
    const LoadedParams& lp = loaded_params();
    INFO("table_params.json problems:\n" << join(lp.problems));
    REQUIRE(lp.problems.empty());
}

// The slide-to-roll checks of C1 for a ball that starts sliding with v0 along +x and no spin.
void check_slide_to_roll(const ShotRecord& rec, Vec3 r0, double v0) {
    const PhysicsParams& p = params();
    const double mu = p.slide_friction;
    const double g = p.gravity_m_s2;
    // Alciatore TP 4.1 (https://billiards.colostate.edu/technical_proofs/TP_4-1.pdf): for a stun
    // shot the ball rolls after t = 2 v0 / (7 mu g), having slid 12 v0^2 / (49 mu g), at 5/7 v0.
    const double t_roll = 2.0 * v0 / (7.0 * mu * g);
    const double d_roll = 12.0 * v0 * v0 / (49.0 * mu * g);
    const double v_roll = 5.0 / 7.0 * v0;
    const double rel = 1e-6;
    CAPTURE(v0);
    CAPTURE(mu);
    INFO("record: " << record_summary(rec));

    const ShotEvent* ev = first_transition(rec, 0, Motion::sliding, Motion::rolling);
    REQUIRE_MESSAGE(ev != nullptr, "no sliding->rolling transition for the cue ball (TP 4.1 expects one at t = "
                                       << t_roll << " s)");
    CHECK_MESSAGE(std::abs(ev->t - t_roll) <= rel * t_roll,
                  "slide-to-roll time " << ev->t << " s, expected 2 v0/(7 mu g) = " << t_roll << " s within " << rel
                                        << " relative (TP 4.1)");

    const Segment* rolling = segment_from(rec, 0, ev->t);
    REQUIRE_MESSAGE(rolling != nullptr, "no segment starts at the slide-to-roll transition");
    const BallState& b = rolling->start;
    CHECK_MESSAGE(b.motion == Motion::rolling, "segment after the transition is " << motion_name(b.motion));
    const double d = b.r.x - r0.x;
    CHECK_MESSAGE(std::abs(d - d_roll) <= rel * d_roll,
                  "distance slid " << d << " m, expected 12 v0^2/(49 mu g) = " << d_roll << " m within " << rel
                                   << " relative (TP 4.1)");
    CHECK_MESSAGE(std::abs(b.r.y - r0.y) <= 1e-12, "drifted sideways while sliding straight: y = " << b.r.y);
    CHECK_MESSAGE(std::abs(norm_xy(b.v) - v_roll) <= rel * v_roll,
                  "speed at rolling " << norm_xy(b.v) << " m/s, expected 5/7 v0 = " << v_roll << " m/s (TP 4.1)");
    CHECK_MESSAGE(std::abs(b.w.y - b.v.x / R) <= rel * v_roll / R,
                  "not rolling without slip: w.y = " << b.w.y << ", v.x / R = " << b.v.x / R);

    // Halfway through the slide the closed form r = r0 + v0 t - mu g t^2 / 2 (constant friction
    // against the slip, which keeps its direction while there is no spin about x) must hold.
    const double th = 0.5 * t_roll;
    const BallState mid = rec.ball_at(0, th);
    const double x_expected = r0.x + v0 * th - 0.5 * mu * g * th * th;
    CHECK_MESSAGE(std::abs(mid.r.x - x_expected) <= 1e-9,
                  "mid-slide position x = " << mid.r.x << ", expected " << x_expected << " (sliding friction mu g)");
}

}  // namespace

TEST_CASE("C1 centre-ball hit: rolls at 12 v0^2 / (49 mu_s g) with 5/7 v0 (Alciatore TP 4.1)") {
    check_params_loaded();
    for (double v0 : {0.5, 1.0, 2.0, 4.0}) {
        const Vec3 r0{-1.5, 0.0, 0.0};
        // A horizontal centre-ball hit leaves the ball sliding with no spin.
        const SimInput in = moving({make_ball(table::Ball::cue, 0, r0, {v0, 0.0, 0.0})});
        check_slide_to_roll(simulate(in, open_table(), params()), r0, v0);
    }
}

TEST_CASE("C1 through strike(): a level centre-ball hit gives no spin, then TP 4.1") {
    check_params_loaded();
    for (double cue_speed : {0.5, 1.0, 2.0, 3.0}) {
        SimInput in;
        const Vec3 r0{-1.5, 0.0, 0.0};
        in.start.balls = {make_ball(table::Ball::cue, 0, r0)};
        in.has_strike = true;
        in.strike = CueStrike{0.0, 0.0, 0.0, 0.0, cue_speed};
        in.seed = 7;
        const ShotRecord rec = simulate(in, open_table(), params());
        CAPTURE(cue_speed);
        INFO("record: " << record_summary(rec));
        REQUIRE(rec.has_strike);
        const double v0 = norm_xy(rec.strike.v);
        REQUIRE_MESSAGE(v0 > 0.0, "the strike produced no velocity");
        CHECK_MESSAGE(std::abs(rec.strike.v.y) <= 1e-12 * v0, "a centre hit aimed along +x left the line: v = "
                                                                   << vec_str(rec.strike.v));
        CHECK_MESSAGE(norm(rec.strike.w) * R <= 1e-9 * v0, "a level centre-ball hit gave spin w = "
                                                                << vec_str(rec.strike.w) << " (expected none)");
        CHECK_FALSE(rec.strike.miscue);
        check_slide_to_roll(rec, r0, v0);
    }
}

TEST_CASE("C1 in the motion model alone (motion.hpp)") {
    check_params_loaded();
    const PhysicsParams& p = params();
    const double mu = p.slide_friction;
    const double g = p.gravity_m_s2;
    for (double v0 : {0.5, 1.0, 2.0, 4.0}) {
        BallState b = make_ball(table::Ball::cue, 0, {-1.5, 0.0, 0.0}, {v0, 0.0, 0.0});
        b.motion = classify(b, p);
        CAPTURE(v0);
        REQUIRE(b.motion == Motion::sliding);
        // TP 4.1, as in the simulated C1.
        const double t_roll = 2.0 * v0 / (7.0 * mu * g);
        const double t = transition_time(b, p, {});
        CHECK_MESSAGE(std::abs(t - t_roll) <= 1e-9 * t_roll, "transition_time " << t << " s, TP 4.1 " << t_roll);
        const BallState end = complete_transition(evolve(b, t, p, {}), p);
        CHECK(end.motion == Motion::rolling);
        const double d_roll = 12.0 * v0 * v0 / (49.0 * mu * g);
        CHECK_MESSAGE(std::abs(end.r.x - b.r.x - d_roll) <= 1e-9 * d_roll,
                      "slid " << end.r.x - b.r.x << " m, TP 4.1 " << d_roll);
        CHECK_MESSAGE(std::abs(end.v.x - 5.0 / 7.0 * v0) <= 1e-9 * v0, "rolls at " << end.v.x << ", TP 4.1 5/7 v0");
    }
}

TEST_CASE("C2 stun full-ball: the cue ball keeps (1-e)/2 v, the object ball takes (1+e)/2 v") {
    check_params_loaded();
    const PhysicsParams& p = params();
    const double e = p.ball_restitution;
    for (double v : {0.5, 1.0, 2.0, 4.0}) {
        // Sliding with w = 0 at contact; the 1e-7 m approach changes v by about 2e-7 m/s.
        std::vector<BallState> balls;
        cut_setup(0.0, {v, 0.0, 0.0}, {}, balls);
        const ShotRecord rec = simulate(moving(balls), open_table(), p);
        CAPTURE(v);
        CAPTURE(e);
        INFO("record: " << record_summary(rec));
        const ShotEvent* hit = first_event(rec, EventKind::ball_ball, 0, 1);
        REQUIRE_MESSAGE(hit != nullptr, "the cue ball never hit the object ball");
        const Segment* cue = segment_from(rec, 0, hit->t);
        const Segment* obj = segment_from(rec, 1, hit->t);
        REQUIRE(cue != nullptr);
        REQUIRE(obj != nullptr);
        // Newton's restitution between equal masses, head-on and frictionless (no tangential slip):
        // Alciatore TP 3.1 with e < 1 (https://billiards.colostate.edu/technical_proofs/TP_3-1.pdf).
        // Tolerance 2e-3 v: the 1e-7 m approach plus a stepped impulse integration of ~1e-3.
        const double tol = 2e-3 * v;
        const double cue_expected = 0.5 * (1.0 - e) * v;
        const double obj_expected = 0.5 * (1.0 + e) * v;
        CHECK_MESSAGE(std::abs(cue->start.v.x - cue_expected) <= tol,
                      "cue ball after a stun hit " << vec_str(cue->start.v) << ", expected (1-e)/2 v = " << cue_expected
                                                    << " +- " << tol);
        CHECK_MESSAGE(std::abs(obj->start.v.x - obj_expected) <= tol,
                      "object ball " << vec_str(obj->start.v) << ", expected (1+e)/2 v = " << obj_expected << " +- "
                                     << tol);
        CHECK_MESSAGE(std::abs(cue->start.v.y) + std::abs(obj->start.v.y) <= 1e-9 * v, "a head-on hit went sideways");

        // The stunned ball has no spin, so it slides to 5/7 of its small speed (TP 4.1).
        const Segment* roll = first_segment_in(rec, 0, Motion::rolling, hit->t);
        REQUIRE_MESSAGE(roll != nullptr, "the cue ball never rolled after the stun hit");
        const double roll_expected = 5.0 / 7.0 * cue_expected;
        CHECK_MESSAGE(std::abs(norm_xy(roll->start.v) - roll_expected) <= tol,
                      "cue ball rolls on at " << norm_xy(roll->start.v) << " m/s, expected 5/7 (1-e)/2 v = "
                                              << roll_expected << " +- " << tol);
        CHECK_MESSAGE(roll->start.v.x >= 0.0, "the stunned cue ball came back: v = " << vec_str(roll->start.v));
    }
}

TEST_CASE("C2 and C3 at the collision alone (collide_balls, then TP A.4's re-roll)") {
    check_params_loaded();
    const PhysicsParams& p = params();
    const double e = p.ball_restitution;
    for (double v : {0.5, 1.0, 2.0, 4.0}) {
        for (bool rolling : {false, true}) {
            BallState cue = make_ball(table::Ball::cue, 0, {}, {v, 0.0, 0.0}, rolling ? rolling_spin({v, 0.0, 0.0}) : Vec3{});
            BallState obj = make_ball(table::Ball::red, 0, {2.0 * R, 0.0, 0.0});
            cue.motion = classify(cue, p);
            obj.motion = classify(obj, p);
            collide_balls(cue, obj, p);
            CAPTURE(v);
            CAPTURE(rolling);
            INFO("after: cue v " << vec_str(cue.v) << " w " << vec_str(cue.w) << "; object v " << vec_str(obj.v));
            // The same expectations as the simulated C2 / C3 above, at the impulse itself.
            CHECK_MESSAGE(std::abs(cue.v.x - 0.5 * (1.0 - e) * v) <= 1e-6 * v,
                          "cue ball keeps " << cue.v.x << ", expected (1-e)/2 v = " << 0.5 * (1.0 - e) * v);
            CHECK_MESSAGE(std::abs(obj.v.x - 0.5 * (1.0 + e) * v) <= 1e-6 * v,
                          "object ball takes " << obj.v.x << ", expected (1+e)/2 v = " << 0.5 * (1.0 + e) * v);
            const double follow = final_rolling_velocity(cue).x;
            const double expected = 5.0 / 7.0 * 0.5 * (1.0 - e) * v + (rolling ? 2.0 / 7.0 * v : 0.0);
            const double mu = p.ball_friction_a + p.ball_friction_b * std::exp(-p.ball_friction_c * v);
            const double loss = rolling ? 1.5 * 5.0 / 7.0 * mu * 0.5 * (1.0 + e) * v : 0.0;
            CHECK_MESSAGE((follow <= expected + 1e-6 * v && follow >= expected - loss - 1e-6 * v),
                          "re-rolls at " << follow << " m/s, expected " << expected << " less at most " << loss
                                         << " for contact friction (TP A.4, TP A.14)");
        }
    }
}

TEST_CASE("C3 rolling full-ball: follows through to (5/7)(1-e)/2 v + (2/7) v") {
    check_params_loaded();
    const PhysicsParams& p = params();
    const double e = p.ball_restitution;
    for (double v : {0.5, 1.0, 2.0, 4.0}) {
        std::vector<BallState> balls;
        cut_setup(0.0, {v, 0.0, 0.0}, rolling_spin({v, 0.0, 0.0}), balls);
        const ShotRecord rec = simulate(moving(balls), open_table(), p);
        CAPTURE(v);
        CAPTURE(e);
        INFO("record: " << record_summary(rec));
        const ShotEvent* hit = first_event(rec, EventKind::ball_ball, 0, 1);
        REQUIRE_MESSAGE(hit != nullptr, "the cue ball never hit the object ball");
        // Just after impact the cue ball keeps (1-e)/2 v and all its topspin v/R; sliding then
        // conserves angular momentum about the cloth contact, so it rolls at 5/7 of its speed plus
        // 2/7 of the speed its spin rolls at (TP A.4, TP 4.1). e = 1 gives the 2/7 of TP A.16.
        const double expected = 5.0 / 7.0 * 0.5 * (1.0 - e) * v + 2.0 / 7.0 * v;
        // Contact friction (TP A.14's mu at the initial vertical slip v, times 1.5 because mu
        // rises as the slip falls) may remove topspin, costing up to (5/7) mu (1+e)/2 v.
        const double mu = p.ball_friction_a + p.ball_friction_b * std::exp(-p.ball_friction_c * v);
        const double loss = 1.5 * 5.0 / 7.0 * mu * 0.5 * (1.0 + e) * v;
        const double tol = 2e-3 * v;
        const Segment* roll = first_segment_in(rec, 0, Motion::rolling, hit->t + 1e-9);
        REQUIRE_MESSAGE(roll != nullptr, "the cue ball never rolled again after the hit");
        const double got = roll->start.v.x;
        CAPTURE(got);
        CHECK_MESSAGE(got <= expected + tol, "follow-through " << got << " m/s exceeds (5/7)(1-e)/2 v + 2/7 v = "
                                                               << expected << " (+" << tol << ")");
        CHECK_MESSAGE(got >= expected - loss - tol,
                      "follow-through " << got << " m/s, expected " << expected << " less at most " << loss + tol
                                        << " for contact friction (TP A.14)");
        CHECK_MESSAGE(std::abs(roll->start.v.y) <= 1e-9 * v, "a head-on follow went sideways");
    }
}
