#include <chrono>
#include <set>

#include "random_shots.hpp"

using namespace calib;

namespace {

// C12 and C13 hold on any closed table; until the cushions agent's outline lands (the contracts
// placeholder has none) they run on the bare WPBSA box.
const TableShape& fuzz_table() {
    static const TableShape box = box_table();
    return loaded_table().ok ? loaded_table().t : box;
}

std::string shot_label(int i, const Shot& s) {
    std::string in = input_json(s.in);
    if (in.size() > 4000) in = in.substr(0, 4000) + "...";
    return "shot " + std::to_string(i) + " (" + s.kind + "): " + in;
}

bool is_guard(const std::string& kind) {
    return kind == "event_guard" || kind == "zero_time_chain" || kind == "time_cap";
}

ShotRecord run_shot(const Shot& s, const TableShape& t, std::string& error) {
    try {
        return simulate(s.in, t, params());
    } catch (const std::exception& e) {
        error = std::string("simulate threw: ") + e.what();
    } catch (...) {
        error = "simulate threw a non-standard exception";
    }
    return {};
}

void check_ends(const Shot& s, const ShotRecord& r, std::vector<std::string>& problems) {
    const PhysicsParams& p = params();
    for (const Anomaly& a : r.anomalies)
        if (is_guard(a.kind) || a.kind == "energy_gain")
            problems.push_back("anomaly " + a.kind + " at t=" + std::to_string(a.t) + ": " + a.detail);
    if (!(r.t_end <= p.time_cap_s)) problems.push_back("t_end " + std::to_string(r.t_end) + " s beyond time_cap_s");
    if (s.in.has_strike && (r.events.empty() || r.events.front().kind != EventKind::strike))
        problems.push_back("a struck shot's record does not start with the strike event");
    if (r.final_state.balls.size() != s.in.start.balls.size()) {
        problems.push_back("final state has " + std::to_string(r.final_state.balls.size()) + " balls, the start " +
                           std::to_string(s.in.start.balls.size()));
        return;
    }
    for (std::size_t i = 0; i < r.final_state.balls.size(); ++i) {
        const BallState& b = r.final_state.balls[i];
        if (b.motion == Motion::pocketed) continue;
        if (b.motion != Motion::stationary || norm2(b.v) != 0.0 || norm2(b.w) != 0.0)
            problems.push_back("ball " + std::to_string(i) + " not at rest at the end: " +
                               std::string(motion_name(b.motion)) + " v " + vec_str(b.v) + " w " + vec_str(b.w));
    }
}

// Total kinetic energy sampled just before and after every event, in time order, may only fall.
void check_energy(const ShotRecord& r, std::vector<std::string>& problems) {
    const PhysicsParams& p = params();
    constexpr double eps = 1e-10;
    std::set<double> times;
    for (const ShotEvent& e : r.events) {
        if (e.t - eps > 0.0) times.insert(e.t - eps);
        if (e.t + eps > 0.0) times.insert(e.t + eps);
    }
    times.insert(r.t_end);
    double prev = -1.0;
    double prev_t = 0.0;
    for (double t : times) {
        if (t <= 0.0) continue;
        const double ke = energy(r.state_at(t), p);
        if (prev >= 0.0 && ke > prev * (1.0 + 1e-9) + 1e-12) {
            std::ostringstream o;
            o.precision(15);
            o << "kinetic energy rose from " << prev << " J at t=" << prev_t << " to " << ke << " J at t=" << t;
            problems.push_back(o.str());
            return;
        }
        prev = ke;
        prev_t = t;
    }
}

bool outside_outline(Vec3 r, const TableShape& t, std::string& why) {
    constexpr double tol = 1e-6;
    for (std::size_t i = 0; i < t.lines.size(); ++i) {
        const LineCushion& l = t.lines[i];
        const double dx = l.p2.x - l.p1.x;
        const double dy = l.p2.y - l.p1.y;
        const double u = ((r.x - l.p1.x) * dx + (r.y - l.p1.y) * dy) / (dx * dx + dy * dy);
        const double d = (r.x - l.p1.x) * l.normal.x + (r.y - l.p1.y) * l.normal.y;
        if (u >= 0.0 && u <= 1.0 && d < R - tol) {
            why = "centre " + std::to_string(d) + " m from cushion line " + std::to_string(i) + " (R = " +
                  std::to_string(R) + ")";
            return true;
        }
    }
    for (std::size_t i = 0; i < t.arcs.size(); ++i) {
        const ArcCushion& a = t.arcs[i];
        const double d = std::hypot(r.x - a.centre.x, r.y - a.centre.y);
        if (d < a.radius + R - tol && angle_in_arc(std::atan2(r.y - a.centre.y, r.x - a.centre.x), a)) {
            why = "inside jaw arc " + std::to_string(i);
            return true;
        }
    }
    const double hx = table::kPlayingLength / 2.0 - R + tol;
    const double hy = table::kPlayingWidth / 2.0 - R + tol;
    if (std::abs(r.x) > hx || std::abs(r.y) > hy) {
        double nearest = 1e9;
        for (const Pocket& p : t.pockets)
            nearest = std::min(nearest, std::hypot(r.x - p.drop_centre.x, r.y - p.drop_centre.y));
        // Past the cushion lines only in a pocket's mouth.
        if (nearest > 0.25) {
            why = "beyond the cushion noses, " + std::to_string(nearest) + " m from the nearest pocket";
            return true;
        }
    }
    return false;
}

void check_robust(const ShotRecord& r, const TableShape& shape, std::vector<std::string>& problems) {
    for (const Anomaly& a : r.anomalies)
        if (a.kind == "nan" || is_guard(a.kind))
            problems.push_back("anomaly " + a.kind + " at t=" + std::to_string(a.t) + ": " + a.detail);
    for (std::size_t b = 0; b < r.segments.size(); ++b)
        for (const Segment& s : r.segments[b])
            if (!std::isfinite(s.t0) || !finite(s.start.r) || !finite(s.start.v) || !finite(s.start.w) ||
                !finite(s.nap)) {
                problems.push_back("non-finite segment for ball " + std::to_string(b) + " at t0=" +
                                   std::to_string(s.t0));
                return;
            }
    double last = 0.0;
    std::set<double> times;
    for (const ShotEvent& e : r.events) {
        if (!std::isfinite(e.t) || e.t < last - 1e-12) {
            problems.push_back("event times out of order or non-finite at t=" + std::to_string(e.t));
            return;
        }
        last = e.t;
        times.insert(e.t);
    }
    times.insert(r.t_end);
    for (double t : times) {
        const TableState st = r.state_at(t);
        for (std::size_t i = 0; i < st.balls.size(); ++i) {
            const BallState& a = st.balls[i];
            if (a.motion == Motion::pocketed) continue;
            if (!finite(a.r)) {
                problems.push_back("ball " + std::to_string(i) + " position not finite at t=" + std::to_string(t));
                return;
            }
            std::string why;
            if (outside_outline(a.r, shape, why)) {
                problems.push_back("ball " + std::to_string(i) + " outside the outline at t=" + std::to_string(t) +
                                   " " + vec_str(a.r) + ": " + why);
                return;
            }
            for (std::size_t j = i + 1; j < st.balls.size(); ++j) {
                const BallState& b = st.balls[j];
                if (b.motion == Motion::pocketed) continue;
                const double d = norm_xy(a.r - b.r);
                if (d < 2.0 * R - 1e-9) {
                    std::ostringstream o;
                    o.precision(15);
                    o << "balls " << i << " and " << j << " " << d << " m apart at t=" << t << " (2R - 1e-9 = "
                      << 2.0 * R - 1e-9 << ")";
                    problems.push_back(o.str());
                    return;
                }
            }
        }
    }
}

struct Failures {
    int shots = 0;
    int failed = 0;
    std::vector<std::string> first;

    void add(int i, const Shot& s, const std::vector<std::string>& problems) {
        if (problems.empty()) return;
        ++failed;
        // The first two with their full input (a repro), the next few by index and problem only.
        if (first.size() < 2) {
            first.push_back(shot_label(i, s) + "\n    " + join(problems, 4));
        } else if (first.size() < 8) {
            first.push_back("shot " + std::to_string(i) + " (" + s.kind + "): " + problems.front());
        }
    }
};

}  // namespace

TEST_CASE("C12-C14 shot generator: every start is a legal table state") {
    // A C14 failure must be the simulator's, not a bad start: no overlaps, nothing in a cushion.
    const TableShape& t = loaded_table().ok ? loaded_table().t : fuzz_table();
    Rng rng(0x5eed0000);
    std::vector<std::string> bad;
    std::vector<std::string> bad_rack;
    std::map<std::string, int> kinds;
    int touching_pairs = 0;
    for (int i = 0; i < 1000; ++i) {
        const Shot s = random_shot(rng, t, params());
        ++kinds[s.kind.substr(0, s.kind.find('+'))];
        // Break-off starts come from opening_rack(), whose contract is that no two balls touch.
        const bool rack = s.kind == "break-off";
        std::vector<std::string>& out = rack ? bad_rack : bad;
        const std::vector<BallState>& b = s.in.start.balls;
        for (std::size_t a = 0; a < b.size(); ++a) {
            std::string why;
            if (outside_outline(b[a].r, t, why)) out.push_back(shot_label(i, s).substr(0, 200) + ": " + why);
            for (std::size_t c = a + 1; c < b.size(); ++c) {
                const double d = norm_xy(b[a].r - b[c].r);
                if (d < 2.0 * R || (rack && d <= 2.0 * R)) {
                    std::ostringstream o;
                    o << "shot " << i << " (" << s.kind << ") balls " << a << " and " << c << " are " << d - 2.0 * R
                      << " m from touching";
                    out.push_back(o.str());
                }
                if (d < 2.0 * R + 1e-12) ++touching_pairs;
            }
        }
    }
    {
        INFO(bad.size() << " problems in the generator's own starts:\n" << join(bad));
        CHECK(bad.empty());
    }
    {
        INFO(bad_rack.size() << " touching or overlapping pairs in opening_rack() starts (rack.hpp: no two balls "
                             << "touch):\n" << join(bad_rack));
        CHECK(bad_rack.empty());
    }
    CHECK(kinds["break-off"] > 100);
    CHECK(kinds["cluster"] > 100);
    CHECK(kinds["cushion"] > 100);
    CHECK(touching_pairs > 500);
}

TEST_CASE("C12 energy never rises across events, and every shot ends within the time cap (2,000 shots)") {
    const TableShape& t = fuzz_table();
    if (!loaded_table().ok) WARN_MESSAGE(false, "table_shape.json not usable yet, C12 runs on the WPBSA box:\n" << join(loaded_table().problems));
    Rng rng(0x5eed0c12);
    Failures f;
    for (int i = 0; i < 2000; ++i) {
        const Shot s = random_shot(rng, t, params());
        std::string error;
        const ShotRecord r = run_shot(s, t, error);
        std::vector<std::string> problems;
        if (!error.empty()) {
            problems.push_back(error);
        } else {
            check_ends(s, r, problems);
            check_energy(r, problems);
        }
        f.add(i, s, problems);
        ++f.shots;
    }
    INFO(f.failed << " of " << f.shots << " shots failed (seed 0x5eed0c12); the first:\n" << join(f.first));
    CHECK(f.failed == 0);
}

TEST_CASE("C13 determinism: the same input gives a byte-identical record (break-off and 200 shots)") {
    const TableShape& t = fuzz_table();
    Rng rng(0x5eed0c13);
    std::vector<Shot> shots;
    shots.push_back(break_off(rng, params()));
    for (int i = 0; i < 200; ++i) shots.push_back(random_shot(rng, t, params()));
    Failures f;
    for (std::size_t i = 0; i < shots.size(); ++i) {
        const Shot& s = shots[i];
        std::string e1;
        std::string e2;
        const ShotRecord a = run_shot(s, t, e1);
        const ShotRecord b = run_shot(s, t, e2);
        std::vector<std::string> problems;
        if (!e1.empty() || !e2.empty()) problems.push_back(e1 + e2);
        if (s.in.has_strike && a.events.empty()) problems.push_back("a struck shot produced no events");
        const std::string ja = record_json(a).dump();
        const std::string jb = record_json(b).dump();
        if (ja.size() < 3) problems.push_back("record_json is empty: " + ja);
        if (ja != jb) {
            std::size_t at = 0;
            while (at < ja.size() && at < jb.size() && ja[at] == jb[at]) ++at;
            const std::size_t from = at > 120 ? at - 120 : 0;
            problems.push_back("records differ from byte " + std::to_string(at) + ":\n      first:  ..." +
                               ja.substr(from, 300) + "\n      second: ..." + jb.substr(from, 300));
        }
        f.add(static_cast<int>(i), s, problems);
        ++f.shots;
    }
    INFO(f.failed << " of " << f.shots << " shots not reproducible; the first:\n" << join(f.first));
    CHECK(f.failed == 0);
}

TEST_CASE("C14 robustness: 5,000 random shots with break-offs, clusters and cushion-frozen balls") {
    const LoadedTable& lt = loaded_table();
    INFO("core/data/table_shape.json:\n" << join(lt.problems));
    REQUIRE_MESSAGE(lt.ok, "C14 needs the real outline (pockets and jaws) from table_shape.json");
    const TableShape& t = lt.t;
    Rng rng(0x5eed0c14);
    Failures f;
    std::map<std::string, int> kinds;
    double slowest_ms = 0.0;
    std::string slowest;
    for (int i = 0; i < 5000; ++i) {
        const Shot s = random_shot(rng, t, params());
        ++kinds[s.kind];
        std::string error;
        const auto t0 = std::chrono::steady_clock::now();
        const ShotRecord r = run_shot(s, t, error);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms > slowest_ms) {
            slowest_ms = ms;
            slowest = shot_label(i, s).substr(0, 300);
        }
        std::vector<std::string> problems;
        if (!error.empty()) {
            problems.push_back(error);
        } else {
            check_robust(r, t, problems);
        }
        f.add(i, s, problems);
        ++f.shots;
    }
    std::ostringstream mix;
    for (const auto& [k, n] : kinds) mix << " " << k << " " << n << ";";
    INFO("shot mix:" << mix.str() << " slowest " << slowest_ms << " ms: " << slowest);
    INFO(f.failed << " of " << f.shots << " shots failed (seed 0x5eed0c14); the first:\n" << join(f.first));
    CHECK(f.failed == 0);
    CHECK(kinds["break-off"] >= 500);
}
