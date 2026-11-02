#include "ofs/physics/shot_record.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>

#include "ofs/log.hpp"
#include "ofs/physics/motion.hpp"
#include "ofs/physics/serialize.hpp"

namespace ofs::phys {

namespace {

constexpr std::array<std::string_view, 8> kEventKindNames = {"strike",   "ball_ball",  "ball_line", "ball_arc",
                                                             "pocket",   "transition", "nap",       "guard"};

nlohmann::json vec3_json(const Vec3& v) { return nlohmann::json::array({v.x, v.y, v.z}); }

}  // namespace

std::string_view event_kind_name(EventKind k) {
    const auto i = static_cast<std::size_t>(k);
    return i < kEventKindNames.size() ? kEventKindNames[i] : "unknown";
}

BallState ShotRecord::ball_at(int ball, double t) const {
    if (ball < 0 || static_cast<std::size_t>(ball) >= segments.size() || segments[ball].empty()) {
        OFS_ASSERT(false && "ShotRecord::ball_at: no segments for that ball");
        return {};
    }
    const std::vector<Segment>& segs = segments[static_cast<std::size_t>(ball)];
    auto next = std::upper_bound(segs.begin(), segs.end(), t,
                                 [](double time, const Segment& s) { return time < s.t0; });
    if (next == segs.begin()) return evolve(segs.front().start, 0.0, params, segs.front().nap);
    const Segment& seg = *std::prev(next);
    double dt = std::max(0.0, t - seg.t0);
    if (next != segs.end()) dt = std::min(dt, next->t0 - seg.t0);
    // The simulator may start the next segment a hair after this one's transition (simultaneous
    // events resolve in turn); evolve() is only defined up to the transition.
    dt = std::min(dt, transition_time(seg.start, params, seg.nap));
    return evolve(seg.start, dt, params, seg.nap);
}

TableState ShotRecord::state_at(double t) const {
    TableState s;
    s.balls.reserve(segments.size());
    for (std::size_t i = 0; i < segments.size(); ++i) s.balls.push_back(ball_at(static_cast<int>(i), t));
    return s;
}

nlohmann::json record_json(const ShotRecord& r) {
    nlohmann::json segments = nlohmann::json::array();
    for (const std::vector<Segment>& ball : r.segments) {
        nlohmann::json list = nlohmann::json::array();
        for (const Segment& s : ball) {
            list.push_back({{"t0", s.t0}, {"start", to_json(s.start)}, {"nap", vec3_json(s.nap)}});
        }
        segments.push_back(std::move(list));
    }
    nlohmann::json events = nlohmann::json::array();
    for (const ShotEvent& e : r.events) {
        events.push_back({{"t", e.t},
                          {"kind", event_kind_name(e.kind)},
                          {"a", e.a},
                          {"b", e.b},
                          {"body", e.body},
                          {"from", motion_name(e.from)},
                          {"to", motion_name(e.to)}});
    }
    nlohmann::json anomalies = nlohmann::json::array();
    for (const Anomaly& a : r.anomalies) {
        anomalies.push_back({{"kind", a.kind}, {"t", a.t}, {"balls", a.balls}, {"detail", a.detail}});
    }
    const nlohmann::json strike = {{"v", vec3_json(r.strike.v)},
                                   {"w", vec3_json(r.strike.w)},
                                   {"elevation_used_rad", r.strike.elevation_used_rad},
                                   {"elevation_raised", r.strike.elevation_raised},
                                   {"squirt_rad", r.strike.squirt_rad},
                                   {"miscue", r.strike.miscue}};
    // nlohmann::json keeps object keys sorted, which fixes the field order of the dump.
    return {{"params", physics_params_values(r.params)},
            {"has_strike", r.has_strike},
            {"strike", strike},
            {"segments", std::move(segments)},
            {"events", std::move(events)},
            {"final", to_json(r.final_state)},
            {"t_end", r.t_end},
            {"anomalies", std::move(anomalies)}};
}

}  // namespace ofs::phys
