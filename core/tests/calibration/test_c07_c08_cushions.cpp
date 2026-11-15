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
        {"cushion_side_grip", p.cushion_side_grip},
        {"cushion_table_friction", p.cushion_table_friction},
        {"cushion_contact_height_m", p.cushion_contact_height_m},
        {"cushion_rigid_limit_m_s", p.cushion_rigid_limit_m_s},
        {"cushion_restitution_slope_per_m_s", p.cushion_restitution_slope_per_m_s},
        {"cushion_restitution_min", p.cushion_restitution_min},
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
// The band is round the model at the loaded cushion_restitution: 0.85 since KaiC's feel sessions on
// the phone (2026-11-13 to 15), where the paper fitted 0.98. The paper's own rebounds are the
// fixture's "literature" cases, checked below with the paper's value.
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
void check_case(const nlohmann::json& c, Frame f, const PhysicsParams& p, std::vector<std::string>& failures) {
    Vec3 r;
    const LineCushion line = cushion_in(f, r);
    BallState b = make_ball(table::Ball::cue, 0, r, to_table(vec_of(c["in"]["v"]), f, false),
                            to_table(vec_of(c["in"]["w"]), f, true));
    b.motion = classify(b, p);
    if (!collide_line(b, line, p)) {
        failures.push_back(case_name(c) + ": the impact solver gave up (plain restitution bounce)");
        return;
    }
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
            check_case(c, f, params(), failures);
            ++n;
        }
    }
    CHECK(n >= 50 * 8);
    INFO(failures.size() << " of " << n << " impacts (cases x 8 cushion placements) off Mathavan et al. "
                         << "(2010), cushion_mathavan.json; the first:\n" << join(failures));
    CHECK(failures.empty());
}

TEST_CASE("C7 with the paper's cushion (e_e = 0.98) the model gives the paper's rebounds") {
    // The literature reference: Mathavan et al. (2010) sec. 3.5 fitted e_e = 0.98 to their measured
    // snooker rebounds. The owner's cushion is softer, so the paper's numbers are held here with
    // the paper's value and every other parameter as loaded.
    require_current_fixture();
    const nlohmann::json& lit = cushion_fixture()["literature"];
    REQUIRE_MESSAGE(lit.is_object(), "cushion_mathavan.json has no literature cases: run `uv run tools/gen_reference.py`");
    PhysicsParams paper = params();
    paper.cushion_restitution = lit["cushion_restitution"].get<double>();
    paper.cushion_side_grip = lit["cushion_side_grip"].get<double>();
    CHECK(paper.cushion_restitution == 0.98);
    CHECK(paper.cushion_side_grip == 1.0);
    int n = 0;
    std::vector<std::string> failures;
    double straight_in = 0.0;
    for (const Frame& f : kFrames) {
        for (const nlohmann::json& c : lit["cases"]) {
            check_case(c, f, paper, failures);
            ++n;
            if (c["group"] == "rolling" && c["speed_m_s"] == 1.0 && c["incidence_deg"] == 0.0) {
                straight_in = c["rebound_speed_m_s"].get<double>();
            }
        }
    }
    CHECK(n >= 50 * 8);
    INFO(failures.size() << " of " << n << " impacts off the paper's cushion; the first:\n" << join(failures));
    CHECK(failures.empty());
    // Rolling straight in at 1 m/s: 0.911 of the speed with the paper's cushion (targets.md).
    CHECK(straight_in == doctest::Approx(0.911).epsilon(0.002));
    for (const nlohmann::json& c : cushion_fixture()["cases"]) {
        if (c["group"] != "rolling" || c["speed_m_s"] != 1.0 || c["incidence_deg"] != 0.0) continue;
        const double ours = c["rebound_speed_m_s"].get<double>();
        MESSAGE("rolling straight into a cushion at 1 m/s: " << ours << " of the speed at cushion_restitution "
                                                            << params().cushion_restitution << ", " << straight_in
                                                            << " with the paper's " << paper.cushion_restitution << " ("
                                                            << (ours / straight_in - 1.0) * 100.0 << "%)");
    }
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
            if (c["group"] != "side" && c["group"] != "heavy_side") continue;
            check_case(c, f, params(), failures);
            ++n;
        }
    }
    CHECK(n >= 70 * 8);
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
        const bool solved = collide_line(b, line, params());
        std::ostringstream key;
        key << "V=" << c["speed_m_s"].get<double>() << " incidence=" << c["incidence_deg"].get<double>();
        CHECK_MESSAGE(solved, key.str() << " side=" << c["side"].get<double>() << ": the impact solver gave up");
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

TEST_CASE("C8 filmed shots: the cue ball meets each cushion where and when the film shows and makes the cannon") {
    // Professional shots tracked frame by frame from broadcasts (positions/selby_blue.json and
    // film_*.json; docs/calibration/targets.md has how they were measured). Each fixture is the
    // layout, the pace and tip offset that fit the film best, and `measured`: the cushions the cue
    // ball takes between its first ball and the cannon, with their places and times counted from
    // that first contact, and the ball or cluster it reaches.
    const char* films[] = {"selby_blue",
                           "film_allen_two_cushions",
                           "film_osullivan_baulk_cushion",
                           "film_lee_hugs_cushion",
                           "film_trump_screw_left",
                           "film_trump_screw_check",
                           "film_selby_one_cushion",
                           "film_osullivan_stun_back",
                           "film_higgins_left_side"};
    // The films' places are good to 1 to 4 cm and the fitted strikes leave up to 7.
    const double place = 0.10;
    const double cushion_time = 0.2;
    const double cannon_time = 0.5;
    const double side = table::kPlayingWidth / 2.0 - table::kBallRadius;
    const double end = table::kPlayingLength / 2.0 - table::kBallRadius;
    REQUIRE(loaded_table().ok);
    const TableShape& t = loaded_table().t;

    struct Cushion {
        Vec3 r;
        double t = 0.0;
    };
    struct Run {
        bool struck = false;
        bool potted = false;
        std::vector<Cushion> cushions;  // the cue ball's, between its first ball and the cannon
        int cannon = -1;
        double cannon_t = 0.0;
        std::string summary;
    };
    auto play = [&](const nlohmann::json& fx, const PhysicsParams& p) {
        Run out;
        SimInput in;
        std::string error;
        if (!from_json(fx["start"], in.start, error) || !from_json(fx["strike"], in.strike, error)) {
            out.summary = error;
            return out;
        }
        in.cue_ball = fx["cue_ball"].get<int>();
        in.has_strike = true;
        in.seed = 1;
        in.params_hash = "calibration";
        const nlohmann::json& real = fx["measured"];
        const int object = real["object_ball"].get<int>();
        std::vector<int> passed = real["allowed_on_the_way"].get<std::vector<int>>();
        const ShotRecord rec = simulate(in, t, p);
        out.summary = record_summary(rec, 40);
        out.potted = rec.final_state.balls[static_cast<std::size_t>(object)].motion == Motion::pocketed;
        const ShotEvent* hit = first_event(rec, EventKind::ball_ball, 0, object);
        if (hit == nullptr) return out;
        out.struck = true;
        for (const ShotEvent& e : rec.events) {
            if (e.t <= hit->t || (e.a != 0 && e.b != 0)) continue;
            if (e.kind == EventKind::ball_ball) {
                const int other = e.a == 0 ? e.b : e.a;
                if (std::find(passed.begin(), passed.end(), other) != passed.end()) continue;
                out.cannon = other;
                out.cannon_t = e.t - hit->t;
                break;
            }
            if (e.kind != EventKind::ball_line && e.kind != EventKind::ball_arc) continue;
            const Segment* at = segment_from(rec, 0, e.t);
            if (at != nullptr) out.cushions.push_back({at->start.r, e.t - hit->t});
        }
        return out;
    };

    for (const char* name : films) {
        const nlohmann::json fx = read_json(fixture_path(std::string("positions/") + name + ".json"));
        REQUIRE_MESSAGE(!fx.is_null(), "missing positions/" << name << ".json");
        const Run ours = play(fx, params());
        INFO(name << "; record: " << ours.summary);
        REQUIRE(ours.struck);
        CHECK(ours.potted);
        const nlohmann::json& real = fx["measured"];
        const nlohmann::json& cushions = real["cushions"];
        REQUIRE(ours.cushions.size() >= cushions.size());
        std::ostringstream told;
        for (std::size_t k = 0; k < cushions.size(); ++k) {
            const std::string which = cushions[k]["cushion"].get<std::string>();
            const Vec3 want{cushions[k]["r"][0].get<double>(), cushions[k]["r"][1].get<double>(), 0.0};
            const Cushion& got = ours.cushions[k];
            const bool along_x = which == "left" || which == "right";
            const double face = which == "left" ? side : which == "right" ? -side : which == "top" ? end : -end;
            INFO("cushion " << k << " (" << which << "): ours " << vec_str(got.r) << " at " << got.t << " s");
            CHECK(std::abs((along_x ? got.r.y : got.r.x) - face) <= 1e-3);
            CHECK(std::abs((along_x ? got.r.x : got.r.y) - (along_x ? want.x : want.y)) <= place);
            CHECK(std::abs(got.t - cushions[k]["t_s"].get<double>()) <= cushion_time);
            told << which << " " << (along_x ? got.r.x : got.r.y) << " at " << got.t << " s ("
                 << (along_x ? want.x : want.y) << ", " << cushions[k]["t_s"].get<double>() << "); ";
        }
        const std::vector<int> pack = real["cannon"]["pack"].get<std::vector<int>>();
        CHECK_MESSAGE(std::find(pack.begin(), pack.end(), ours.cannon) != pack.end(),
                      "the cue ball reached ball " << ours.cannon);
        CHECK(std::abs(ours.cannon_t - real["cannon"]["t_s"].get<double>()) <= cannon_time);
        MESSAGE(std::string(name) << ", ours (the film): " << told.str() << "cannon at " << ours.cannon_t << " s ("
                << real["cannon"]["t_s"].get<double>() << ")");
    }

    // The cloth and cushions of the owner's feel sessions of 2026-11-13 to 15, set while the cue
    // was half as strong again: Selby's cue ball stops short of the reds.
    PhysicsParams slow = params();
    slow.roll_friction = 0.015;
    slow.cushion_restitution = 0.85;
    const nlohmann::json selby = read_json(fixture_path("positions/selby_blue.json"));
    const Run before = play(selby, slow);
    INFO("roll_friction 0.015, cushion_restitution 0.85; record: " << before.summary);
    const std::vector<int> pack = selby["measured"]["cannon"]["pack"].get<std::vector<int>>();
    CHECK(std::find(pack.begin(), pack.end(), before.cannon) == pack.end());
}
