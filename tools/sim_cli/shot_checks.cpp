// The fuzz's per-shot checks (spec 7.11 C12, C14), evaluated from the record's own segments at every
// event time, independently of the simulator's guards.

#include <algorithm>
#include <cmath>

#include "ofs/physics/motion.hpp"
#include "sim_cli.hpp"

namespace sim_cli {

namespace {

constexpr double kOverlapTolerance = 1e-9;  // m: closer than 2R by more than this is an overlap
constexpr double kCushionTolerance = 1e-9;  // m: into a cushion by more than this is through it
// Rounding in a closed-form evaluation is ~1e-16 relative; anything near 1e-9 is a real gain.
constexpr double kEnergyRelative = 1e-9;
constexpr double kEnergyAbsolute = 1e-12;  // J

// The first occurrence of a kind of failure in full, and how many more there were.
struct Finding {
    std::string kind;
    std::string first;
    int count = 0;

    void note(const std::string& detail) {
        if (count++ == 0) first = detail;
    }
};

bool potted(const phys::BallState& b) { return b.motion == phys::Motion::pocketed; }

}  // namespace

std::vector<ShotFailure> check_record(const phys::SimInput& in, const phys::ShotRecord& r,
                                      const PlayArea& area) {
    std::vector<ShotFailure> out;
    for (const phys::Anomaly& a : r.anomalies) {
        if (a.kind != "solver_fallback") out.push_back({"anomaly:" + a.kind, anomaly_text(a)});
    }
    const std::size_t n = in.start.balls.size();
    if (r.segments.size() != n || r.final_state.balls.size() != n) {
        out.push_back({"record", format("%zu balls in, %zu segment lists and %zu final balls out", n,
                                        r.segments.size(), r.final_state.balls.size())});
        return out;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (r.segments[i].empty() || r.segments[i].front().t0 != 0.0) {
            out.push_back({"record", format("ball %zu's segments don't start at t = 0", i)});
            return out;
        }
    }

    Finding nan{"nan"};
    Finding overlap{"overlap"};
    Finding tunnel{"tunnel"};
    Finding energy{"energy_gain"};
    Finding order{"record"};
    Finding rest{"not_at_rest"};

    std::vector<double> times{0.0};
    double previous_t = 0.0;
    for (std::size_t k = 0; k < r.events.size(); ++k) {
        const double t = r.events[k].t;
        if (!std::isfinite(t)) {
            nan.note(format("event %zu has t = %g", k, t));
            continue;
        }
        if (t < previous_t) {
            order.note(format("event %zu at t=%.9g s comes after one at %.9g s", k, t, previous_t));
        }
        previous_t = std::max(previous_t, t);
        times.push_back(t);
    }
    if (std::isfinite(r.t_end)) {
        times.push_back(r.t_end);
    } else {
        nan.note(format("t_end = %g", r.t_end));
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    std::vector<phys::BallState> states(n);
    double previous_energy = -1.0;
    double previous_energy_t = 0.0;
    const auto check_state = [&](const std::vector<phys::BallState>& balls, const char* when) {
        for (std::size_t i = 0; i < n; ++i) {
            if (!finite(balls[i])) {
                nan.note(format("%s (%zu) is not finite %s", ball_label(balls[i]).c_str(), i, when));
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (potted(balls[i]) || !finite(balls[i])) continue;
            for (std::size_t j = i + 1; j < n; ++j) {
                if (potted(balls[j]) || !finite(balls[j])) continue;
                const double d = planar_distance(balls[i].r, balls[j].r);
                if (d < 2.0 * kR - kOverlapTolerance) {
                    overlap.note(
                        format("balls %zu and %zu are %.12g m apart (2R = %.12g) %s", i, j, d, 2.0 * kR, when));
                }
            }
            const std::string where = area.violation(balls[i].r, kCushionTolerance);
            if (!where.empty()) {
                tunnel.note(format("%s (%zu) %s %s", ball_label(balls[i]).c_str(), i, where.c_str(), when));
            }
        }
    };

    for (double t : times) {
        for (std::size_t i = 0; i < n; ++i) states[i] = state_after(r, static_cast<int>(i), t);
        const std::string when = format("at t=%.9g s", t);
        check_state(states, when.c_str());
        double total = 0.0;
        for (const phys::BallState& b : states) {
            if (!potted(b)) total += phys::kinetic_energy(b, r.params);
        }
        if (previous_energy >= 0.0 && total > previous_energy * (1.0 + kEnergyRelative) + kEnergyAbsolute) {
            energy.note(format("total kinetic energy rose from %.12g J at t=%.9g s to %.12g J at t=%.9g s",
                               previous_energy, previous_energy_t, total, t));
        }
        if (std::isfinite(total)) {
            previous_energy = total;
            previous_energy_t = t;
        }
    }

    check_state(r.final_state.balls, "in the final state");
    for (std::size_t i = 0; i < n; ++i) {
        const phys::BallState& b = r.final_state.balls[i];
        const bool at_rest = b.motion == phys::Motion::pocketed ||
                             (b.motion == phys::Motion::stationary && norm2(b.v) == 0.0 && norm2(b.w) == 0.0);
        if (!at_rest) {
            rest.note(format("%s (%zu) ends %s with |v| %.3g m/s, |w| %.3g rad/s", ball_label(b).c_str(), i,
                             std::string(phys::motion_name(b.motion)).c_str(), norm(b.v), norm(b.w)));
        }
    }

    for (const Finding* f : {&nan, &overlap, &tunnel, &energy, &order, &rest}) {
        if (f->count == 0) continue;
        out.push_back({f->kind, f->count > 1 ? f->first + format(" (and %d more)", f->count - 1) : f->first});
    }
    return out;
}

}  // namespace sim_cli
