#include "calib.hpp"

using namespace calib;

namespace {

struct LagRun {
    bool reached = false;
    double t_top = 0.0;
    double t_baulk = 0.0;
    ShotRecord rec;
};

// box_table() lines: 0 top, 1 baulk.
LagRun run_lag(Vec3 start, double speed) {
    LagRun out;
    // A level centre-ball hit: sliding, no spin (C1 checks strike() gives exactly that).
    out.rec = simulate(moving({make_ball(table::Ball::cue, 0, start, {speed, 0.0, 0.0})}), box_table(), params());
    const ShotEvent* top = nullptr;
    for (const ShotEvent& e : out.rec.events) {
        if (e.kind != EventKind::ball_line || e.a != 0) continue;
        if (!top && e.body == 0) top = &e;
        if (top && e.body == 1) {
            out.reached = true;
            out.t_top = top->t;
            out.t_baulk = e.t;
            break;
        }
    }
    return out;
}

// TP B.6's lag model on the snooker table: slide to 5/7 v0 over 12 v0^2 / (49 mu_s g) (TP 4.1),
// roll at deceleration mu_r g to the top cushion, rebound at ratio e_c as stun, skid to 5/7,
// roll back to the baulk cushion arriving at rest. Solved for the starting speed v0.
double tp_b6_lag_speed(double mu_r, double e_c, double mu_s, double g, double d_up, double d_back) {
    const double k = (25.0 + 24.0 * mu_r / mu_s) / 49.0;
    const double v_rebound_sq = 2.0 * mu_r * g * d_back / k;
    const double v_in_sq = v_rebound_sq / (e_c * e_c);
    return std::sqrt((v_in_sq + 2.0 * mu_r * g * d_up) / k);
}

}  // namespace

TEST_CASE("C11 table speed: the lag needs a realistic cue-ball speed for fast cloth") {
    const PhysicsParams& p = params();
    const nlohmann::json fx = read_json(fixture_path("reference/table_speed_published.json"));
    REQUIRE_MESSAGE(!fx.is_null(), "missing core/tests/fixtures/reference/table_speed_published.json");
    const nlohmann::json lag = read_json(fixture_path("positions/lag.json"));
    REQUIRE_MESSAGE(!lag.is_null(), "missing positions/lag.json");
    TableState start;
    std::string error;
    REQUIRE_MESSAGE(from_json(lag["start"], start, error), error);
    const Vec3 r0 = start.balls.at(0).r;

    double lo = 0.5;
    double hi = 6.0;
    const LagRun at_lo = run_lag(r0, lo);
    const LagRun at_hi = run_lag(r0, hi);
    INFO("at " << hi << " m/s: " << record_summary(at_hi.rec));
    REQUIRE_MESSAGE(at_hi.reached, "even " << hi << " m/s never reached the baulk cushion after the top cushion");
    REQUIRE_MESSAGE(!at_lo.reached, lo << " m/s already completes a lag: the cloth has no drag");
    for (int i = 0; i < 30; ++i) {
        const double mid = 0.5 * (lo + hi);
        (run_lag(r0, mid).reached ? hi : lo) = mid;
    }
    const LagRun found = run_lag(r0, hi);
    REQUIRE(found.reached);
    const double v_lag = hi;

    const double g = p.gravity_m_s2;
    const double top_contact = table::top_cushion_x() - R;
    const double d_up = top_contact - r0.x;
    const double d_back = table::kPlayingLength - 2.0 * R;
    const double e_c = fx["cushion_rebound_ratio"].get<double>();
    const double slope_fast = fx["effective_slope"]["fast"].get<double>();
    const double slope_slow = fx["effective_slope"]["slow"].get<double>();
    const double margin = fx["speed_margin_rel"].get<double>();
    const double v_min = tp_b6_lag_speed(slope_fast, e_c, p.slide_friction, g, d_up, d_back) * (1.0 - margin);
    const double v_max = tp_b6_lag_speed(slope_slow, e_c, p.slide_friction, g, d_up, d_back) * (1.0 + margin);

    // Jewett's table-speed measure: time the last length, from the far cushion to stopping at the
    // near one; the effective slope is 2 d / (g T^2).
    const double T = found.t_baulk - found.t_top;
    const double slope = 2.0 * d_back / (g * T * T);
    TableState s;
    s.balls = {make_ball(table::Ball::cue, 0, r0)};
    Rng rng(1);
    const double ball_per_cue = norm_xy(strike(s, 0, CueStrike{0.0, 0.0, 0.0, 0.0, 1.0}, open_table(), p, rng).v);
    CAPTURE(v_lag);
    CAPTURE(T);
    CAPTURE(slope);
    CAPTURE(p.roll_friction);
    INFO("cue speed for the lag with a centre-ball hit: " << (ball_per_cue > 0.0 ? v_lag / ball_per_cue : 0.0)
                                                         << " m/s (ball speed per cue speed " << ball_per_cue << ")");
    CHECK_MESSAGE((v_lag >= v_min && v_lag <= v_max),
                  "lag needs a cue-ball speed of " << v_lag << " m/s; TP B.6's lag model on this table gives " << v_min
                                                   << " to " << v_max << " m/s for effective slopes " << slope_fast
                                                   << " (fast carom cloth) to " << slope_slow
                                                   << " (competition pool cloth), Jewett via the Alciatore cloth "
                                                      "speed FAQ; tune roll_friction");
    CHECK_MESSAGE((slope >= slope_fast && slope <= slope_slow),
                  "last length took " << T << " s: effective slope " << slope << ", published fast cloth "
                                      << slope_fast << " to " << slope_slow << " (Jewett's lag timing)");
}
