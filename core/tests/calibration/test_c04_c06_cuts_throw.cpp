#include "calib.hpp"

using namespace calib;

namespace {

struct Hit {
    double fraction;
    double cut;  // asin(1 - f), TP 3.3
    const char* name;
};

const Hit kHits[] = {
    {0.75, std::asin(0.25), "3/4 ball"},
    {0.5, std::asin(0.5), "1/2 ball"},
    {0.25, std::asin(0.75), "1/4 ball"},
};

// Rolling cue ball, frictionless, restitution e: just after impact the cue ball keeps v sin(phi)
// along the tangent line and (1-e)/2 v cos(phi) along the line of centres, with its topspin v/R;
// it then rolls at 5/7 of that plus 2/7 v along the original line (TP A.4). Resolving gives
// tan(theta) = (1-k) sin cos / (k cos^2 + sin^2), k = (9 - 5e)/14; e = 1 gives TP 3.3's
// sin cos / (sin^2 + 2/5).
double natural_roll_deflection(double cut, double e) {
    const double k = (9.0 - 5.0 * e) / 14.0;
    const double s = std::sin(cut);
    const double c = std::cos(cut);
    return std::atan2((1.0 - k) * s * c, k * c * c + s * s);
}

const nlohmann::json& throw_fixture() {
    static const nlohmann::json j = read_json(fixture_path("reference/throw_published.json"));
    return j;
}

// Where the measurement comes from: the whole simulation, or collide_balls() alone on balls
// placed exactly at contact (which tells a collision fault from an event-loop one).
enum class Via { simulate, collide };

const char* via_name(Via via) { return via == Via::simulate ? "simulate()" : "collide_balls()"; }

// The two balls just after the hit, and (for a rolling cue ball) its velocity once it rolls again.
struct After {
    bool ok = false;
    BallState cue;
    BallState obj;
    Vec3 cue_rolling;
    std::string why;
};

After run_cut(Via via, double cut, Vec3 cue_v, Vec3 cue_w) {
    After out;
    std::vector<BallState> balls;
    cut_setup(cut, cue_v, cue_w, balls);
    if (via == Via::collide) {
        out.cue = balls[0];
        out.obj = balls[1];
        out.cue.r = {0.0, 0.0, 0.0};
        out.cue.motion = classify(out.cue, params());
        out.obj.motion = classify(out.obj, params());
        collide_balls(out.cue, out.obj, params());
        out.cue_rolling = final_rolling_velocity(out.cue);
        out.ok = true;
        return out;
    }
    const ShotRecord rec = simulate(moving(balls), open_table(), params());
    const ShotEvent* hit = first_event(rec, EventKind::ball_ball, 0, 1);
    if (!hit) {
        out.why = "the cue ball never hit the object ball; record: " + record_summary(rec);
        return out;
    }
    const Segment* cue = segment_from(rec, 0, hit->t);
    const Segment* obj = segment_from(rec, 1, hit->t);
    const Segment* roll = first_segment_in(rec, 0, Motion::rolling, hit->t + 1e-9);
    if (!cue || !obj || !roll) {
        out.why = "no segments after the hit, or the cue ball never rolled again; record: " + record_summary(rec);
        return out;
    }
    out.cue = cue->start;
    out.obj = obj->start;
    out.cue_rolling = roll->start.v;
    out.ok = true;
    return out;
}

void check_c4(Via via) {
    const PhysicsParams& p = params();
    const double e = p.ball_restitution;
    for (const Hit& h : kHits) {
        for (double v : {0.5, 1.0, 2.0}) {
            const After a = run_cut(via, h.cut, {v, 0.0, 0.0}, rolling_spin({v, 0.0, 0.0}));
            INFO(std::string(via_name(via)) << ", hit: " << std::string(h.name));
            CAPTURE(v);
            REQUIRE_MESSAGE(a.ok, a.why);
            // The object ball sits on +y, so the cue ball deflects to -y.
            const double got = std::atan2(-a.cue_rolling.y, a.cue_rolling.x) / kDeg;
            const double ideal =
                std::atan2(std::sin(h.cut) * std::cos(h.cut), std::sin(h.cut) * std::sin(h.cut) + 0.4) / kDeg;
            const double frictionless = natural_roll_deflection(h.cut, e) / kDeg;
            std::vector<BallState> balls;
            const Vec3 n = cut_setup(h.cut, {v, 0.0, 0.0}, rolling_spin({v, 0.0, 0.0}), balls);
            tp_a14_collision(balls[0], balls[1], n, p);
            const Vec3 vf = final_rolling_velocity(balls[0]);
            const double with_friction = std::atan2(-vf.y, vf.x) / kDeg;
            // Between no contact friction and TP A.14's friction (throw), plus 0.75 degrees for how a
            // friction model may differ from TP A.14's single impulse.
            const double lo = std::min(frictionless, with_friction) - 0.75;
            const double hi = std::max(frictionless, with_friction) + 0.75;
            CHECK_MESSAGE((got >= lo && got <= hi),
                          "rolling cue ball deflected " << got << " deg, expected " << lo << " to " << hi
                                                        << " (TP 3.3 at e = 1: " << ideal << "; e = " << e << ": "
                                                        << frictionless << "; with TP A.14 throw: " << with_friction
                                                        << ")");
            CHECK_MESSAGE((got >= 24.0 && got <= 36.0),
                          "deflection " << got << " deg is outside the 30-degree rule's band of about 27-34 (TP 3.3)");
        }
    }
}

void check_c5(Via via) {
    const PhysicsParams& p = params();
    const double e = p.ball_restitution;
    for (const Hit& h : kHits) {
        for (double v : {0.5, 1.0, 2.0}) {
            const After a = run_cut(via, h.cut, {v, 0.0, 0.0}, {});
            INFO(std::string(via_name(via)) << ", hit: " << std::string(h.name));
            CAPTURE(v);
            REQUIRE_MESSAGE(a.ok, a.why);
            std::vector<BallState> balls;
            const Vec3 n = cut_setup(h.cut, {v, 0.0, 0.0}, {}, balls);
            // The tangent line: the cue ball's direction across the line of centres.
            const Vec3 t = unit_or_zero(Vec3{v, 0.0, 0.0} - n * dot(Vec3{v, 0.0, 0.0}, n));
            const Vec3 vc = a.cue.v;
            const double got = std::atan2(dot(vc, n), dot(vc, t)) / kDeg;
            // TP 3.1 with restitution e: the cue ball keeps v sin(phi) along t and (1-e)/2 v cos(phi)
            // along n, so it leaves atan((1-e)/2 cot(phi)) off the tangent line, towards the object
            // ball's line. Throw friction trims the tangential part (TP A.14).
            const double frictionless = std::atan(0.5 * (1.0 - e) / std::tan(h.cut)) / kDeg;
            tp_a14_collision(balls[0], balls[1], n, p);
            const double with_friction = std::atan2(dot(balls[0].v, n), dot(balls[0].v, t)) / kDeg;
            const double lo = std::min(frictionless, with_friction) - 0.5;
            const double hi = std::max(frictionless, with_friction) + 0.5;
            const double between =
                std::acos(std::clamp(dot(unit_or_zero(vc), unit_or_zero(a.obj.v)), -1.0, 1.0)) / kDeg;
            CHECK_MESSAGE((got >= lo && got <= hi),
                          "stunned cue ball left " << got << " deg off the tangent line, expected " << lo << " to " << hi
                                                   << " (e = " << e << " frictionless " << frictionless
                                                   << ", with TP A.14 throw " << with_friction
                                                   << "); angle between the balls " << between << " deg");
            if (via == Via::simulate) {
                // With no spin the slide keeps its direction (TP A.4): the stun path is straight.
                const double bend = std::abs(wrap(angle_of(a.cue_rolling) - angle_of(vc))) / kDeg;
                CHECK_MESSAGE(bend <= 0.2, "stunned cue ball's path bent " << bend << " deg before rolling");
            }
        }
    }
}

void check_c6(Via via) {
    const nlohmann::json& fx = throw_fixture();
    REQUIRE_MESSAGE(!fx.is_null(), "missing core/tests/fixtures/reference/throw_published.json");
    const PhysicsParams& p = params();
    const double cut = fx["cut_deg"].get<double>() * kDeg;
    const double tol = fx["tolerance_deg"].get<double>();
    std::map<std::string, double> got;
    for (const std::string speed : {"slow", "medium", "fast"}) {
        const double v = fx["speeds_m_s"][speed].get<double>();
        const After a = run_cut(via, cut, {v, 0.0, 0.0}, {});
        INFO(std::string(via_name(via)) << ", speed: " << speed);
        CAPTURE(v);
        REQUIRE_MESSAGE(a.ok, a.why);
        std::vector<BallState> balls;
        const Vec3 n = cut_setup(cut, {v, 0.0, 0.0}, {}, balls);
        // Throw: the object ball's departure off the line of centres, towards the cue ball's
        // tangential motion (Alciatore, "Throw - Part II", Diagram 2).
        const Vec3 t = unit_or_zero(Vec3{v, 0.0, 0.0} - n * dot(Vec3{v, 0.0, 0.0}, n));
        got[speed] = std::atan2(dot(a.obj.v, t), dot(a.obj.v, n)) / kDeg;
        tp_a14_collision(balls[0], balls[1], n, p);
        const double tp_a14 = std::atan2(dot(balls[1].v, t), dot(balls[1].v, n)) / kDeg;
        std::vector<double> measured;
        for (const auto& [source, values] : fx["half_ball_stun_throw_deg"].items())
            if (values.contains(speed)) measured.push_back(values[speed].get<double>());
        REQUIRE(!measured.empty());
        const double lo = *std::min_element(measured.begin(), measured.end()) - tol;
        const double hi = *std::max_element(measured.begin(), measured.end()) + tol;
        CHECK_MESSAGE((got[speed] >= lo && got[speed] <= hi),
                      speed << " (" << v << " m/s) half-ball stun throw " << got[speed] << " deg, published "
                            << lo + tol << " to " << hi - tol << " deg +- " << tol
                            << " (Alciatore BD Sept 2006 Diagram 4, Jewett throw.gif); TP A.14 with the loaded "
                            << "ball_friction_a/b/c gives " << tp_a14 << " deg");
    }
    CHECK_MESSAGE(got["slow"] > got["fast"] + 0.25,
                  "slow throw " << got["slow"] << " deg should exceed fast throw " << got["fast"]
                                << " deg (TP A.14: friction falls with sliding speed)");
}

}  // namespace

TEST_CASE("C4 rolling cue ball, 1/4 to 3/4 ball: deflects about 30 degrees (Alciatore TP 3.3 / A.4)") {
    check_c4(Via::simulate);
}

TEST_CASE("C4 at the collision alone (collide_balls, then TP A.4's re-roll)") { check_c4(Via::collide); }

TEST_CASE("C4 from the named positions: struck from 1 m, rolling at contact") {
    const PhysicsParams& p = params();
    for (const char* name : {"cut_three_quarter_ball", "cut_half_ball", "cut_quarter_ball"}) {
        const nlohmann::json fx = read_json(fixture_path(std::string("positions/") + name + ".json"));
        REQUIRE_MESSAGE(!fx.is_null(), "missing fixture " << name);
        SimInput in;
        std::string error;
        REQUIRE_MESSAGE(from_json(fx["start"], in.start, error), error);
        REQUIRE(from_json(fx["strike"], in.strike, error));
        in.cue_ball = fx["cue_ball"].get<int>();
        in.has_strike = true;
        in.seed = 3;
        // C4 is for a rolling cue ball. The fixtures' 2 m/s centre-ball strike leaves it at
        // v0 = (1 + e_tip) V / (1 + m/M) (TP A.30), about 2.8 m/s, which slides 12 v0^2 / (49 mu_s g)
        // (TP 4.1), about 0.96 m: still sliding at the red. At 1 m/s it rolls within half the run.
        in.strike.speed_m_s = 1.0;
        const double v0 = (1.0 + p.cue_tip_restitution) * in.strike.speed_m_s / (1.0 + p.ball_mass_kg / p.cue_mass_kg);
        const double slide = 12.0 * v0 * v0 / (49.0 * p.slide_friction * p.gravity_m_s2);
        const double run = norm_xy(in.start.balls[1].r - in.start.balls[0].r) - 2.0 * R;
        REQUIRE_MESSAGE(slide < 0.5 * run, name << ": slides " << slide << " m of a " << run << " m run");
        const ShotRecord rec = simulate(in, open_table(), p);
        INFO("position: " << std::string(name));
        INFO("record: " << record_summary(rec));
        const ShotEvent* hit = first_event(rec, EventKind::ball_ball, 0, 1);
        REQUIRE_MESSAGE(hit != nullptr, "the cue ball never hit the object ball");
        const BallState cue = rec.ball_at(0, hit->t - 1e-9);
        const BallState obj = rec.ball_at(1, hit->t - 1e-9);
        CHECK_MESSAGE(cue.motion == Motion::rolling, "cue ball not rolling at contact: " << motion_name(cue.motion));
        const Vec3 n = unit_or_zero(obj.r - cue.r);
        const double cut = std::acos(std::min(1.0, dot(n, unit_or_zero(cue.v))));
        const Segment* roll = first_segment_in(rec, 0, Motion::rolling, hit->t + 1e-9);
        REQUIRE_MESSAGE(roll != nullptr, "the cue ball never rolled again after the cut");
        const double got = std::abs(wrap(angle_of(roll->start.v) - angle_of(cue.v))) / kDeg;
        BallState a = cue;
        BallState b = obj;
        tp_a14_collision(a, b, n, p);
        const double frictionless = natural_roll_deflection(cut, p.ball_restitution) / kDeg;
        const double with_friction = std::abs(wrap(angle_of(final_rolling_velocity(a)) - angle_of(cue.v))) / kDeg;
        const double lo = std::min(frictionless, with_friction) - 0.75;
        const double hi = std::max(frictionless, with_friction) + 0.75;
        CAPTURE(cut / kDeg);
        CHECK_MESSAGE((got >= lo && got <= hi), "deflected " << got << " deg, expected " << lo << " to " << hi
                                                             << " (frictionless " << frictionless
                                                             << ", TP A.14 throw " << with_friction << ")");
    }
}

TEST_CASE("C5 stun cut: the cue ball leaves along the tangent line (Alciatore TP 3.1, e < 1)") {
    check_c5(Via::simulate);
}

TEST_CASE("C5 at the collision alone (collide_balls)") { check_c5(Via::collide); }

TEST_CASE("C6 cut-induced throw at a half-ball stun hit: within the published measurements, slow throws more") {
    check_c6(Via::simulate);
}

TEST_CASE("C6 at the collision alone (collide_balls)") { check_c6(Via::collide); }
