#include "ofs/physics/repro.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "ofs/log.hpp"
#include "ofs/params.hpp"
#include "ofs/physics/serialize.hpp"

namespace ofs::phys {

namespace {

// Event times in a replay agree with the logged ones to well within this on one platform; across
// platforms they drift by rounding only.
constexpr double kEventTimeTolerance = 1e-9;

nlohmann::json event_entry(const ShotEvent& e) {
    return nlohmann::json::array({e.t, event_kind_name(e.kind), e.a, e.b, e.body});
}

bool parse_seed(const std::string& text, std::uint64_t& out) {
    if (text.empty()) return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

std::string fmt(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

const nlohmann::json* field(const nlohmann::json& j, const char* key) {
    if (!j.is_object()) return nullptr;
    const auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

const nlohmann::json* array_field(const nlohmann::json& j, const char* key) {
    const nlohmann::json* f = field(j, key);
    return f != nullptr && f->is_array() ? f : nullptr;
}

bool is_event_entry(const nlohmann::json& e) {
    return e.is_array() && e.size() == 5 && e[0].is_number() && e[1].is_string() && e[2].is_number_integer() &&
           e[3].is_number_integer() && e[4].is_number_integer();
}

bool same_event(const nlohmann::json& x, const nlohmann::json& y) {
    if (!is_event_entry(x) || !is_event_entry(y)) return false;
    return std::abs(x[0].get<double>() - y[0].get<double>()) <= kEventTimeTolerance && x[1] == y[1] && x[2] == y[2] &&
           x[3] == y[3] && x[4] == y[4];
}

bool same_pot(const nlohmann::json& x, const nlohmann::json& y) {
    const nlohmann::json* xb = field(x, "ball");
    const nlohmann::json* yb = field(y, "ball");
    const nlohmann::json* xp = field(x, "pocket");
    const nlohmann::json* yp = field(y, "pocket");
    const nlohmann::json* xt = field(x, "t");
    const nlohmann::json* yt = field(y, "t");
    if (!xb || !yb || !xp || !yp || !xt || !yt || !xt->is_number() || !yt->is_number()) return false;
    return *xb == *yb && *xp == *yp && std::abs(xt->get<double>() - yt->get<double>()) <= kEventTimeTolerance;
}

// The count of events a result reports, falling back to the length of its list.
std::size_t event_count(const nlohmann::json& result, const nlohmann::json& list) {
    const nlohmann::json* n = field(result, "events");
    if (n != nullptr && n->is_number_unsigned()) return n->get<std::size_t>();
    if (n != nullptr && n->is_number_integer() && n->get<std::int64_t>() >= 0) {
        return static_cast<std::size_t>(n->get<std::int64_t>());
    }
    return list.size();
}

const nlohmann::json* final_balls(const nlohmann::json& result) {
    const nlohmann::json* f = field(result, "final");
    return f == nullptr ? nullptr : array_field(*f, "balls");
}

bool read_xy(const nlohmann::json& ball, double& x, double& y) {
    const nlohmann::json* r = array_field(ball, "r");
    if (r == nullptr || r->size() != 2 || !(*r)[0].is_number() || !(*r)[1].is_number()) return false;
    x = (*r)[0].get<double>();
    y = (*r)[1].get<double>();
    return true;
}

}  // namespace

nlohmann::json make_bundle(const SimInput& in, const PhysicsParams& p) {
    return {{"v", kBundleVersion},
            {"seed", std::to_string(in.seed)},
            {"params_hash", in.params_hash},
            {"params", physics_params_values(p)},
            {"cue_ball", in.cue_ball},
            {"has_strike", in.has_strike},
            {"strike", to_json(in.strike)},
            {"start", to_json(in.start)}};
}

nlohmann::json make_result(const ShotRecord& r) {
    nlohmann::json all = nlohmann::json::array();
    nlohmann::json listed = nlohmann::json::array();
    nlohmann::json potted = nlohmann::json::array();
    for (const ShotEvent& e : r.events) {
        nlohmann::json entry = event_entry(e);
        if (listed.size() < static_cast<std::size_t>(kResultEventCap)) listed.push_back(entry);
        all.push_back(std::move(entry));
        if (e.kind == EventKind::pocket) potted.push_back({{"ball", e.a}, {"pocket", e.body}, {"t", e.t}});
    }
    return {{"events", r.events.size()},
            {"t_end", r.t_end},
            {"potted", std::move(potted)},
            {"final", to_json(r.final_state)},
            {"miscue", r.strike.miscue},
            {"elevation_used_rad", r.strike.elevation_used_rad},
            {"elevation_raised", r.strike.elevation_raised},
            {"anomalies", r.anomalies.size()},
            {"sim_us", r.sim_us},
            {"event_list", std::move(listed)},
            {"event_digest", params::content_hash(all)}};
}

bool parse_bundle(const nlohmann::json& bundle, SimInput& in, PhysicsParams& p, std::string& error) {
    if (!bundle.is_object()) {
        error = "bundle: want an object";
        return false;
    }
    const nlohmann::json* v = field(bundle, "v");
    if (v == nullptr || !v->is_number_integer()) {
        error = "v: want an integer version";
        return false;
    }
    const std::int64_t version = v->get<std::int64_t>();
    if (version > kBundleVersion) {
        error = "v: version " + std::to_string(version) + " is newer than this build reads (" +
                std::to_string(kBundleVersion) + ")";
        return false;
    }
    if (version < 1) {
        error = "v: version " + std::to_string(version) + " is not a bundle version";
        return false;
    }

    SimInput parsed;
    const nlohmann::json* seed = field(bundle, "seed");
    if (seed == nullptr || !seed->is_string() || !parse_seed(seed->get<std::string>(), parsed.seed)) {
        error = "seed: want a decimal string of an unsigned 64-bit integer";
        return false;
    }
    const nlohmann::json* hash = field(bundle, "params_hash");
    if (hash == nullptr || !hash->is_string()) {
        error = "params_hash: want a string";
        return false;
    }
    parsed.params_hash = hash->get<std::string>();

    const nlohmann::json* values = field(bundle, "params");
    if (values == nullptr) {
        error = "params: missing";
        return false;
    }
    PhysicsParams parsed_p;
    std::vector<std::string> problems;
    if (!physics_params_from_values(*values, parsed_p, problems)) {
        error = "params:";
        for (std::size_t i = 0; i < problems.size(); ++i) error += (i == 0 ? " " : "; ") + problems[i];
        return false;
    }

    const nlohmann::json* has_strike = field(bundle, "has_strike");
    if (has_strike == nullptr || !has_strike->is_boolean()) {
        error = "has_strike: want true or false";
        return false;
    }
    parsed.has_strike = has_strike->get<bool>();
    const nlohmann::json* strike = field(bundle, "strike");
    if (strike == nullptr) {
        error = "strike: missing";
        return false;
    }
    if (!from_json(*strike, parsed.strike, error)) return false;
    const nlohmann::json* start = field(bundle, "start");
    if (start == nullptr) {
        error = "start: missing";
        return false;
    }
    if (!from_json(*start, parsed.start, error)) {
        error = "start: " + error;
        return false;
    }
    if (parsed.start.balls.empty()) {
        error = "start: no balls";
        return false;
    }
    const nlohmann::json* cue = field(bundle, "cue_ball");
    if (cue == nullptr || !cue->is_number_integer() || cue->get<std::int64_t>() < 0 ||
        cue->get<std::int64_t>() >= static_cast<std::int64_t>(parsed.start.balls.size())) {
        error = "cue_ball: want the index of a ball in start (0-" + std::to_string(parsed.start.balls.size() - 1) +
                ")";
        return false;
    }
    parsed.cue_ball = cue->get<int>();
    if (parsed.has_strike &&
        parsed.start.balls[static_cast<std::size_t>(parsed.cue_ball)].motion == Motion::pocketed) {
        error = "cue_ball: ball " + std::to_string(parsed.cue_ball) + " is pocketed and can't be struck";
        return false;
    }

    in = std::move(parsed);
    p = parsed_p;
    return true;
}

Divergence compare_results(const nlohmann::json& logged, const nlohmann::json& replayed, double tolerance_m) {
    Divergence d;
    const nlohmann::json* le = array_field(logged, "event_list");
    const nlohmann::json* re = array_field(replayed, "event_list");
    if (le == nullptr || re == nullptr) {
        d.diverged = true;
        d.summary = std::string(le == nullptr ? "logged" : "replayed") + " result has no event_list";
        return d;
    }
    std::vector<std::string> notes;

    const std::size_t listed = std::min(le->size(), re->size());
    for (std::size_t i = 0; i < listed; ++i) {
        if (!same_event((*le)[i], (*re)[i])) {
            d.first_event = static_cast<int>(i);
            notes.push_back("first divergence at event " + std::to_string(i) + ": logged " + (*le)[i].dump() +
                            ", replayed " + (*re)[i].dump());
            break;
        }
    }
    const std::size_t nl = event_count(logged, *le);
    const std::size_t nr = event_count(replayed, *re);
    if (d.first_event < 0 && (le->size() != re->size() || nl != nr)) {
        // The lists agree as far as both go; the shorter one ends (or stops being listed) here.
        d.first_event = static_cast<int>(listed);
        notes.push_back("events agree up to " + std::to_string(listed) + ", then the logged shot has " +
                        std::to_string(nl) + " events and the replay " + std::to_string(nr));
    }
    if (d.first_event < 0 && nl > static_cast<std::size_t>(kResultEventCap)) {
        const nlohmann::json* ld = field(logged, "event_digest");
        const nlohmann::json* rd = field(replayed, "event_digest");
        if (ld == nullptr || rd == nullptr || *ld != *rd) {
            notes.push_back("the listed events agree, but the event_digest over all " + std::to_string(nl) +
                            " differs (bit-level differences beyond event " + std::to_string(kResultEventCap) + ")");
        }
    }

    const nlohmann::json* lp = array_field(logged, "potted");
    const nlohmann::json* rp = array_field(replayed, "potted");
    bool potted_agree = lp != nullptr && rp != nullptr && lp->size() == rp->size();
    for (std::size_t i = 0; potted_agree && i < lp->size(); ++i) potted_agree = same_pot((*lp)[i], (*rp)[i]);
    if (!potted_agree) {
        d.diverged = true;
        notes.push_back("potted differ: logged " + (lp ? lp->dump() : std::string("missing")) + ", replayed " +
                        (rp ? rp->dump() : std::string("missing")));
    }

    const nlohmann::json* lf = final_balls(logged);
    const nlohmann::json* rf = final_balls(replayed);
    if (lf == nullptr || rf == nullptr || lf->size() != rf->size()) {
        d.diverged = true;
        notes.push_back("final states can't be compared (missing or different ball counts)");
    } else {
        int worst = -1;
        bool readable = true;
        for (std::size_t i = 0; i < lf->size(); ++i) {
            double lx = 0.0, ly = 0.0, rx = 0.0, ry = 0.0;
            if (!read_xy((*lf)[i], lx, ly) || !read_xy((*rf)[i], rx, ry)) {
                readable = false;
                continue;
            }
            const double err = std::hypot(lx - rx, ly - ry);
            if (worst < 0 || err > d.max_position_error_m) {
                d.max_position_error_m = err;
                worst = static_cast<int>(i);
            }
        }
        if (!readable) {
            d.diverged = true;
            notes.push_back("a final position is malformed");
        }
        if (d.max_position_error_m > tolerance_m) {
            d.diverged = true;
            notes.push_back("final positions differ by up to " + fmt(d.max_position_error_m) + " m (ball " +
                            std::to_string(worst) + ", tolerance " + fmt(tolerance_m) + " m)");
        } else {
            notes.push_back("max final position error " + fmt(d.max_position_error_m) + " m");
        }
    }

    if (d.first_event >= 0) d.diverged = true;
    if (!d.diverged) {
        notes.insert(notes.begin(), "no divergence: " + std::to_string(nl) + " events agree within " +
                                        fmt(kEventTimeTolerance) + " s, potted balls agree");
    }
    for (std::size_t i = 0; i < notes.size(); ++i) d.summary += (i == 0 ? "" : "; ") + notes[i];
    return d;
}

void log_shot(const SimInput& in, const PhysicsParams& p, const ShotRecord& r) {
    log::info("shot", "shot.simulated", {{"bundle", make_bundle(in, p)}, {"result", make_result(r)}});
}

}  // namespace ofs::phys
