#include "calib.hpp"

using namespace calib;

namespace {

const nlohmann::json& cushion_fixture() {
    static const nlohmann::json j = read_json(fixture_path("reference/cushion_mathavan.json"));
    return j;
}

Vec3 vec_of(const nlohmann::json& a) {
    return {a[0].get<double>(), a[1].get<double>(), a.size() > 2 ? a[2].get<double>() : 0.0};
}

// The fixture's parameters against the loaded ones: its numbers only hold for the values it was
// generated with.
std::vector<std::string> stale_params(const nlohmann::json& fx) {
    const PhysicsParams& p = params();
    const std::pair<const char*, double> loaded[] = {
        {"ball_mass_kg", p.ball_mass_kg},
        {"cushion_restitution", p.cushion_restitution},
        {"cushion_friction", p.cushion_friction},
        {"cushion_table_friction", p.cushion_table_friction},
        {"cushion_contact_height_m", p.cushion_contact_height_m},
        {"cushion_rigid_limit_m_s", p.cushion_rigid_limit_m_s},
        {"cushion_restitution_slope_per_m_s", p.cushion_restitution_slope_per_m_s},
        {"ball_radius_m", R},
    };
    std::vector<std::string> out;
    for (const auto& [name, value] : loaded) {
        if (!fx["params"].contains(name)) {
            out.push_back(std::string(name) + " missing from the fixture");
        } else if (fx["params"][name].get<double>() != value) {
            out.push_back(std::string(name) + ": fixture " + fx["params"][name].dump() + ", table_params.json " +
                          std::to_string(value));
        }
    }
    return out;
}

void require_current_fixture() {
    const nlohmann::json& fx = cushion_fixture();
    REQUIRE_MESSAGE(!fx.is_null(), "missing core/tests/fixtures/reference/cushion_mathavan.json: run "
                                   "`uv run tools/gen_reference.py`");
    const std::vector<std::string> stale = stale_params(fx);
    INFO("cushion parameters changed since cushion_mathavan.json was generated:\n"
         << join(stale) << "\nLead: regenerate it with `uv run tools/gen_reference.py` and commit the fixture.");
    REQUIRE(stale.empty());
}

// Proper rotations about z and the mirror x -> -x of the fixture's frame: the same impact
// against differently placed cushions. w is an axial vector, so the mirror maps it to
// (w.x, -w.y, -w.z).
struct Frame {
    int quarter_turns;
    bool mirror;
};

Vec3 to_table(Vec3 a, Frame f, bool axial) {
    if (f.mirror) a = axial ? Vec3{a.x, -a.y, -a.z} : Vec3{-a.x, a.y, a.z};
    for (int i = 0; i < f.quarter_turns; ++i) a = {-a.y, a.x, a.z};
    return a;
}

Vec3 from_table(Vec3 a, Frame f, bool axial) {
    for (int i = 0; i < f.quarter_turns; ++i) a = {a.y, -a.x, a.z};
    if (f.mirror) a = axial ? Vec3{a.x, -a.y, -a.z} : Vec3{-a.x, a.y, a.z};
    return a;
}

// The fixture's cushion: the line y = Y with its normal (0, -1) into the playing area, moved
// into the frame `f`; the ball touching it.
LineCushion cushion_in(Frame f, Vec3& ball_r) {
    const double Y = table::kPlayingWidth / 2.0;
    const Vec3 a = to_table({-1.5, Y, 0.0}, f, false);
    const Vec3 b = to_table({1.5, Y, 0.0}, f, false);
    const Vec3 n = to_table({0.0, -1.0, 0.0}, f, false);
    ball_r = to_table({0.0, Y - R, 0.0}, f, false);
    return LineCushion{{a.x, a.y}, {b.x, b.y}, {n.x, n.y}};
}

double rebound_angle_deg(Vec3 v) { return std::atan2(v.x, -v.y) / kDeg; }

struct Compared {
    double speed;
    double angle;
    double speed_expected;
    double angle_expected;
};

Compared compare(const nlohmann::json& c, Vec3 v_out_fixture_frame) {
    return {norm_xy(v_out_fixture_frame), rebound_angle_deg(v_out_fixture_frame), c["rebound_speed_m_s"].get<double>(),
            c["rebound_angle_deg"].get<double>()};
}

// Tolerances from the M1 plan: Mathavan's model is the reference, so a faithful implementation
// lands well inside them; the fixture's own N-vs-2N convergence is below 1e-6 and 1e-4 degrees.
constexpr double kAngleTolDeg = 1.0;
constexpr double kSpeedTolRel = 0.02;

std::string case_name(const nlohmann::json& c) {
    std::ostringstream o;
    o << c["group"].get<std::string>() << " V=" << c["speed_m_s"].get<double>()
      << " incidence=" << c["incidence_deg"].get<double>() << " topspin=" << c["topspin"].get<double>()
      << " side=" << c["side"].get<double>();
    return o.str();
}

// One fixture case against collide_line in frame f; a failure is described in full.
void check_case(const nlohmann::json& c, Frame f, std::vector<std::string>& failures) {
    Vec3 r;
    const LineCushion line = cushion_in(f, r);
    BallState b = make_ball(table::Ball::cue, 0, r, to_table(vec_of(c["in"]["v"]), f, false),
                            to_table(vec_of(c["in"]["w"]), f, true));
    b.motion = classify(b, params());
    collide_line(b, line, params());
    const Vec3 v_out = from_table(b.v, f, false);
    const Compared got = compare(c, v_out);
    const bool angle_ok = std::abs(got.angle - got.angle_expected) <= kAngleTolDeg;
    const bool speed_ok = std::abs(got.speed - got.speed_expected) <= kSpeedTolRel * got.speed_expected;
    if (angle_ok && speed_ok && v_out.y < 0.0) return;
    std::ostringstream o;
    o.precision(8);
    o << case_name(c) << ", frame " << f.quarter_turns << " quarter turns" << (f.mirror ? " mirrored" : "")
      << ": rebound angle " << got.angle << " deg (Mathavan " << got.angle_expected << " +- " << kAngleTolDeg
      << "), speed " << got.speed << " m/s (Mathavan " << got.speed_expected << " +- " << kSpeedTolRel * 100
      << "%); v " << vec_str(v_out) << " w " << vec_str(from_table(b.w, f, true)) << ", oracle w "
      << vec_str(vec_of(c["out"]["w"]));
    failures.push_back(o.str());
}

const Frame kFrames[] = {{0, false}, {1, false}, {2, false}, {3, false}, {0, true}, {1, true}, {2, true}, {3, true}};

}  // namespace

TEST_CASE("C7 plain-ball cushion rebounds match Mathavan et al. (2010), 10-80 degrees, 0.5-4 m/s") {
    require_current_fixture();
    int n = 0;
    std::vector<std::string> failures;
    for (const Frame& f : kFrames) {
        for (const nlohmann::json& c : cushion_fixture()["cases"]) {
            if (c["group"] == "side") continue;
            check_case(c, f, failures);
            ++n;
        }
    }
    CHECK(n >= 50 * 8);
    INFO(failures.size() << " of " << n << " impacts (cases x 8 cushion placements) off Mathavan et al. "
                         << "(2010), cushion_mathavan.json; the first:\n" << join(failures));
    CHECK(failures.empty());
}

TEST_CASE("C7 through simulate(): a rolling ball's rebound off a cushion line") {
    require_current_fixture();
    const PhysicsParams& p = params();
    for (const nlohmann::json& c : cushion_fixture()["cases"]) {
        if (c["group"] != "rolling" || c["speed_m_s"] != 2.0) continue;
        // The fixture's own frame is the left cushion's: approach it from 1e-6 m away.
        TableShape t;
        const double Y = table::kPlayingWidth / 2.0;
        t.lines.push_back({{-1.7, Y}, {1.7, Y}, {0.0, -1.0}});
        const Vec3 v = vec_of(c["in"]["v"]);
        const Vec3 r = Vec3{0.0, Y - R, 0.0} - unit_or_zero(v) * (1e-6 / unit_or_zero(v).y);
        const ShotRecord rec = simulate(moving({make_ball(table::Ball::cue, 0, r, v, vec_of(c["in"]["w"]))}), t, p);
        INFO(case_name(c) << "; record: " << record_summary(rec));
        const ShotEvent* hit = first_event(rec, EventKind::ball_line, 0);
        REQUIRE_MESSAGE(hit != nullptr, "no ball_line event");
        CHECK(hit->body == 0);
        const Segment* after = segment_from(rec, 0, hit->t);
        REQUIRE(after != nullptr);
        const Compared got = compare(c, after->start.v);
        CHECK_MESSAGE(std::abs(got.angle - got.angle_expected) <= kAngleTolDeg,
                      "rebound angle " << got.angle << " deg, Mathavan (2010) " << got.angle_expected);
        CHECK_MESSAGE(std::abs(got.speed - got.speed_expected) <= kSpeedTolRel * got.speed_expected,
                      "rebound speed " << got.speed << " m/s, Mathavan (2010) " << got.speed_expected);
    }
}

TEST_CASE("C8 running side widens and check side narrows the rebound, by Mathavan's amounts") {
    require_current_fixture();
    const nlohmann::json& cases = cushion_fixture()["cases"];
    int n = 0;
    std::vector<std::string> failures;
    for (const Frame& f : kFrames) {
        for (const nlohmann::json& c : cases) {
            if (c["group"] != "side") continue;
            check_case(c, f, failures);
            ++n;
        }
    }
    CHECK(n >= 40 * 8);
    {
        INFO(failures.size() << " of " << n << " side-spin impacts (cases x 8 cushion placements) off Mathavan et "
                             << "al. (2010); the first:\n" << join(failures));
        CHECK(failures.empty());
    }

    // Direction, measured on collide_line itself: at each speed and incidence, rebound angle
    // grows from check side (side < 0) through none to running side (side > 0) (Mathavan Fig. 8).
    std::map<std::string, std::vector<std::pair<double, double>>> by_shot;
    for (const nlohmann::json& c : cases) {
        const bool plain = c["group"] == "rolling";
        if (!plain && c["group"] != "side") continue;
        Vec3 r;
        const LineCushion line = cushion_in({0, false}, r);
        BallState b = make_ball(table::Ball::cue, 0, r, vec_of(c["in"]["v"]), vec_of(c["in"]["w"]));
        b.motion = classify(b, params());
        collide_line(b, line, params());
        std::ostringstream key;
        key << "V=" << c["speed_m_s"].get<double>() << " incidence=" << c["incidence_deg"].get<double>();
        by_shot[key.str()].push_back({c["side"].get<double>(), rebound_angle_deg(b.v)});
    }
    std::vector<std::string> unordered;
    for (auto& [shot, list] : by_shot) {
        if (list.size() < 5) continue;  // plain-only rows with no side cases
        std::sort(list.begin(), list.end());
        bool ordered = true;
        for (std::size_t i = 1; i < list.size(); ++i) ordered = ordered && list[i].second > list[i - 1].second;
        if (ordered) continue;
        std::ostringstream o;
        for (const auto& [side, angle] : list) o << " side " << side << ": " << angle << " deg;";
        unordered.push_back(shot + ":" + o.str());
    }
    INFO("rebound angles not growing from check side (side < 0) to running side (side > 0), Mathavan (2010) "
         "Fig. 8:\n" << join(unordered));
    CHECK(unordered.empty());
}

TEST_CASE("C8 strike convention: right-hand side runs off a cushion on the left of a ball heading up the table") {
    // A ball struck up and to the left (towards +x, +y) meets the left cushion; running side is
    // w.z > 0 there (the fixture's frame). Right-hand side (tip_x > 0) must give that spin.
    SimInput in;
    in.start.balls = {make_ball(table::Ball::cue, 0, {0.0, 0.0, 0.0})};
    Rng rng(5);
    const StrikeOutcome out = strike(in.start, 0, CueStrike{0.6, 0.4, 0.0, 0.0, 2.0}, open_table(), params(), rng);
    INFO("strike outcome v " << vec_str(out.v) << " w " << vec_str(out.w));
    CHECK_MESSAGE(out.w.z > 0.0, "right-hand side gave w.z = " << out.w.z << " (anticlockwise from above expected)");
}
