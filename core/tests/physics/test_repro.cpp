#include <doctest/doctest.h>

#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/log_schema.hpp"
#include "ofs/params.hpp"
#include "ofs/physics/rack.hpp"
#include "ofs/physics/repro.hpp"
#include "ofs/physics/serialize.hpp"

using namespace ofs::phys;

namespace {

struct Capture : ofs::log::Sink {
    std::vector<ofs::log::Event> events;
    void write(const ofs::log::Event& event, const std::string&) override { events.push_back(event); }
};

const nlohmann::json& schema() {
    static const nlohmann::json s = ofs::log::load_schema(std::string(OFS_CORE_DATA_DIR) + "/log_schema.json");
    return s;
}

// Values chosen to catch any loss of precision or sign on the way through JSON.
SimInput sample_input() {
    SimInput in;
    in.start = opening_rack(147, PhysicsParams{});
    in.start.balls[3].v = {0.1 + 0.2, -1.0 / 3.0, 0.0};
    in.start.balls[3].w = {1e-300, -0.0, 5e-324};
    in.start.balls[3].motion = Motion::sliding;
    in.start.balls[5].motion = Motion::pocketed;
    in.start.balls[5].pocket = 4;
    in.cue_ball = 0;
    in.has_strike = true;
    in.strike = {0.7853981633974483, 0.12345678901234568, -0.3, 0.05, 4.2};
    in.seed = 18446744073709551615ULL;
    in.params_hash = "8efc364b3052045e";
    in.table_hash = "0f1e2d3c4b5a6978";
    return in;
}

PhysicsParams sample_params() {
    PhysicsParams p;
    p.roll_friction = 0.0061234567890123;
    p.max_events = 777;
    p.zero_time_s = 3e-10;
    return p;
}

ShotRecord sample_record(std::size_t n_events) {
    ShotRecord r;
    r.final_state = opening_rack(5, PhysicsParams{});
    r.final_state.balls[4].motion = Motion::pocketed;
    r.final_state.balls[4].pocket = 2;
    const EventKind kinds[] = {EventKind::ball_ball, EventKind::ball_line, EventKind::ball_arc,
                               EventKind::transition, EventKind::nap};
    for (std::size_t i = 0; i < n_events; ++i) {
        ShotEvent e;
        e.t = static_cast<double>(i) * 0.001;
        e.kind = i == 0 ? EventKind::strike : kinds[i % 5];
        e.a = static_cast<int>(i % 22);
        e.b = e.kind == EventKind::ball_ball ? 21 : -1;
        e.body = e.kind == EventKind::ball_line || e.kind == EventKind::ball_arc ? static_cast<int>(i % 7) : -1;
        r.events.push_back(e);
    }
    if (n_events > 10) r.events[10] = {r.events[10].t, EventKind::pocket, 4, -1, 2, Motion::rolling, Motion::pocketed};
    r.t_end = r.events.empty() ? 0.0 : r.events.back().t;
    r.has_strike = true;
    r.strike.elevation_used_rad = 0.25;
    r.strike.elevation_raised = true;
    r.strike.miscue = true;
    r.anomalies = {{"overlap", 0.0, {1, 2}, "x"}, {"energy_gain", 0.5, {3}, "y"}};
    r.sim_us = 321.5;
    return r;
}

nlohmann::json full_event_list(const ShotRecord& r) {
    nlohmann::json list = nlohmann::json::array();
    for (const ShotEvent& e : r.events) list.push_back({e.t, event_kind_name(e.kind), e.a, e.b, e.body});
    return list;
}

}  // namespace

TEST_CASE("bundle carries every field, the seed as a decimal string") {
    const SimInput in = sample_input();
    const PhysicsParams p = sample_params();
    const nlohmann::json b = make_bundle(in, p);
    std::set<std::string> keys;
    for (const auto& [key, value] : b.items()) keys.insert(key);
    CHECK(keys == std::set<std::string>{"v", "seed", "params_hash", "params", "params_values_hash", "table_hash",
                                        "cue_ball", "has_strike", "strike", "start"});
    CHECK(b["v"] == 2);
    CHECK(b["seed"] == "18446744073709551615");
    CHECK(b["params_hash"] == "8efc364b3052045e");
    CHECK(b["params"] == physics_params_values(p));
    CHECK(b["params_values_hash"] == ofs::params::content_hash(physics_params_values(p)));
    CHECK(b["params_values_hash"].get<std::string>().size() == 16);
    CHECK(b["table_hash"] == "0f1e2d3c4b5a6978");
    CHECK(b["cue_ball"] == 0);
    CHECK(b["has_strike"] == true);
    CHECK(b["strike"] == to_json(in.strike));
    CHECK(b["start"] == to_json(in.start));
}

TEST_CASE("bundle round-trips to the same input and params, bit for bit") {
    const SimInput in = sample_input();
    const PhysicsParams p = sample_params();
    const std::string text = make_bundle(in, p).dump();
    SimInput back;
    PhysicsParams back_p;
    std::string error;
    REQUIRE_MESSAGE(parse_bundle(nlohmann::json::parse(text), back, back_p, error), error);
    CHECK(make_bundle(back, back_p).dump() == text);
    CHECK(back.seed == in.seed);
    CHECK(back.cue_ball == in.cue_ball);
    CHECK(back.has_strike == in.has_strike);
    CHECK(back.params_hash == in.params_hash);
    CHECK(back.table_hash == in.table_hash);
    CHECK(back.strike.tip_x == in.strike.tip_x);
    REQUIRE(back.start.balls.size() == in.start.balls.size());
    for (std::size_t i = 0; i < in.start.balls.size(); ++i) {
        CAPTURE(i);
        CHECK(back.start.balls[i].r == in.start.balls[i].r);
        CHECK(back.start.balls[i].v == in.start.balls[i].v);
        CHECK(back.start.balls[i].w == in.start.balls[i].w);
        CHECK(back.start.balls[i].motion == in.start.balls[i].motion);
        CHECK(back.start.balls[i].pocket == in.start.balls[i].pocket);
    }
    CHECK(physics_params_values(back_p) == physics_params_values(p));

    SimInput small;
    small.start = in.start;
    small.seed = 0;
    REQUIRE(parse_bundle(make_bundle(small, p), back, back_p, error));
    CHECK(back.seed == 0);
}

TEST_CASE("malformed bundles are refused with a reason and leave the outputs alone") {
    const nlohmann::json good = make_bundle(sample_input(), sample_params());
    struct Case {
        const char* name;
        std::function<void(nlohmann::json&)> mutate;
        const char* reason;
    };
    const std::vector<Case> cases = {
        {"not an object", [](nlohmann::json& b) { b = nlohmann::json::array(); }, "bundle"},
        {"no version", [](nlohmann::json& b) { b.erase("v"); }, "v:"},
        {"newer version", [](nlohmann::json& b) { b["v"] = kBundleVersion + 1; }, "newer"},
        {"version 0", [](nlohmann::json& b) { b["v"] = 0; }, "v:"},
        {"version as text", [](nlohmann::json& b) { b["v"] = "1"; }, "v:"},
        {"no seed", [](nlohmann::json& b) { b.erase("seed"); }, "seed"},
        {"numeric seed", [](nlohmann::json& b) { b["seed"] = 147; }, "seed"},
        {"seed with a letter", [](nlohmann::json& b) { b["seed"] = "14a7"; }, "seed"},
        {"negative seed", [](nlohmann::json& b) { b["seed"] = "-1"; }, "seed"},
        {"empty seed", [](nlohmann::json& b) { b["seed"] = ""; }, "seed"},
        {"seed past uint64", [](nlohmann::json& b) { b["seed"] = "18446744073709551616"; }, "seed"},
        {"no params_hash", [](nlohmann::json& b) { b.erase("params_hash"); }, "params_hash"},
        {"no params", [](nlohmann::json& b) { b.erase("params"); }, "params"},
        {"a param out of range", [](nlohmann::json& b) { b["params"]["slide_friction"] = -1.0; }, "slide_friction"},
        {"a param missing", [](nlohmann::json& b) { b["params"].erase("roll_friction"); }, "roll_friction"},
        {"an unknown param", [](nlohmann::json& b) { b["params"]["surprise"] = 1; }, "surprise"},
        {"a param changed after logging", [](nlohmann::json& b) { b["params"]["roll_friction"] = 0.0075; },
         "params_values_hash"},
        {"params_values_hash as a number", [](nlohmann::json& b) { b["params_values_hash"] = 1; },
         "params_values_hash"},
        {"table_hash as a number", [](nlohmann::json& b) { b["table_hash"] = 5; }, "table_hash"},
        {"no has_strike", [](nlohmann::json& b) { b.erase("has_strike"); }, "has_strike"},
        {"has_strike as a number", [](nlohmann::json& b) { b["has_strike"] = 1; }, "has_strike"},
        {"no strike", [](nlohmann::json& b) { b.erase("strike"); }, "strike"},
        {"strike without speed", [](nlohmann::json& b) { b["strike"].erase("speed_m_s"); }, "strike"},
        {"no start", [](nlohmann::json& b) { b.erase("start"); }, "start"},
        {"unknown ball", [](nlohmann::json& b) { b["start"]["balls"][2]["ball"] = "purple"; }, "start"},
        {"no balls", [](nlohmann::json& b) { b["start"]["balls"] = nlohmann::json::array(); }, "start"},
        {"cue_ball past the end", [](nlohmann::json& b) { b["cue_ball"] = 22; }, "cue_ball"},
        {"negative cue_ball", [](nlohmann::json& b) { b["cue_ball"] = -1; }, "cue_ball"},
        {"fractional cue_ball", [](nlohmann::json& b) { b["cue_ball"] = 1.5; }, "cue_ball"},
        {"pocketed cue ball struck", [](nlohmann::json& b) { b["cue_ball"] = 5; }, "pocketed"},
    };
    for (const Case& c : cases) {
        CAPTURE(c.name);
        nlohmann::json bundle = good;
        c.mutate(bundle);
        SimInput in;
        in.params_hash = "untouched";
        PhysicsParams p;
        p.max_events = 4242;
        std::string error;
        CHECK_FALSE(parse_bundle(bundle, in, p, error));
        CHECK_MESSAGE(error.find(c.reason) != std::string::npos, error);
        CHECK(in.params_hash == "untouched");
        CHECK(p.max_events == 4242);
    }
}

TEST_CASE("bundle params_values_hash refuses an edited value, naming both hashes") {
    const nlohmann::json good = make_bundle(sample_input(), sample_params());
    SimInput in;
    PhysicsParams p;
    std::string error;
    CHECK_MESSAGE(parse_bundle(nlohmann::json::parse(good.dump()), in, p, error), error);

    for (const char* name : {"slide_friction", "max_events"}) {
        CAPTURE(name);
        nlohmann::json tampered = good;
        tampered["params"][name] = name == std::string("max_events") ? nlohmann::json(778) : nlohmann::json(0.21);
        const std::string edited_hash = ofs::params::content_hash(tampered["params"]);
        CHECK_FALSE(parse_bundle(tampered, in, p, error));
        CHECK_MESSAGE(error.rfind("params_values_hash:", 0) == 0, error);
        CHECK_MESSAGE(error.find(good["params_values_hash"].get<std::string>()) != std::string::npos, error);
        CHECK_MESSAGE(error.find(edited_hash) != std::string::npos, error);
    }

    // The hash is over the parsed values, so a value written another way but read the same passes.
    nlohmann::json reworded = good;
    REQUIRE(reworded["params"]["time_cap_s"] == 120.0);
    reworded["params"]["time_cap_s"] = 120;
    CHECK_MESSAGE(parse_bundle(nlohmann::json::parse(reworded.dump()), in, p, error), error);
    CHECK(p.time_cap_s == 120.0);

    nlohmann::json unhashed = good;
    unhashed.erase("params_values_hash");
    unhashed["params"]["slide_friction"] = 0.21;
    REQUIRE_MESSAGE(parse_bundle(unhashed, in, p, error), error);
    CHECK(p.slide_friction == 0.21);
}

TEST_CASE("bundle carries the table hash, and an older bundle without one reads as unknown") {
    SimInput in = sample_input();
    const PhysicsParams p = sample_params();
    SimInput back;
    PhysicsParams back_p;
    std::string error;
    REQUIRE(parse_bundle(make_bundle(in, p), back, back_p, error));
    CHECK(back.table_hash == "0f1e2d3c4b5a6978");

    in.table_hash.clear();
    const nlohmann::json unknown = make_bundle(in, p);
    CHECK(unknown["table_hash"] == "");
    back.table_hash = "stale";
    REQUIRE(parse_bundle(unknown, back, back_p, error));
    CHECK(back.table_hash.empty());

    // A version-1 bundle as logged before either hash existed.
    nlohmann::json v1 = make_bundle(sample_input(), p);
    v1["v"] = 1;
    v1.erase("table_hash");
    v1.erase("params_values_hash");
    back.table_hash = "stale";
    REQUIRE_MESSAGE(parse_bundle(nlohmann::json::parse(v1.dump()), back, back_p, error), error);
    CHECK(back.table_hash.empty());
    CHECK(back.seed == 18446744073709551615ULL);
    CHECK(physics_params_values(back_p) == physics_params_values(p));
}

TEST_CASE("bundle with a non-finite start state and strike round-trips through text") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    SimInput in = sample_input();
    in.start.balls[16].r.x = nan;
    in.start.balls[17].v = {inf, -inf, 0.0};
    in.start.balls[18].w = {0.0, 0.0, nan};
    in.strike.aim_rad = nan;
    in.strike.speed_m_s = inf;
    const PhysicsParams p = sample_params();
    const std::string text = make_bundle(in, p).dump();
    CHECK(text.find("null") == std::string::npos);
    SimInput back;
    PhysicsParams back_p;
    std::string error;
    REQUIRE_MESSAGE(parse_bundle(nlohmann::json::parse(text), back, back_p, error), error);
    CHECK(std::isnan(back.start.balls[16].r.x));
    CHECK(back.start.balls[16].r.y == in.start.balls[16].r.y);
    CHECK(back.start.balls[17].v.x == inf);
    CHECK(back.start.balls[17].v.y == -inf);
    CHECK(std::isnan(back.start.balls[18].w.z));
    CHECK(std::isnan(back.strike.aim_rad));
    CHECK(back.strike.speed_m_s == inf);
    CHECK(make_bundle(back, back_p).dump() == text);
}

TEST_CASE("bundle of a shot whose start state wasn't finite replays to the same result") {
    SimInput in = sample_input();
    in.start.balls[16].r.x = std::numeric_limits<double>::quiet_NaN();
    in.start.balls[19].v = {std::numeric_limits<double>::infinity(), 0.0, 0.0};
    const PhysicsParams p = sample_params();
    const TableShape table;
    const ShotRecord r = simulate(in, table, p);
    bool nan_anomaly = false;
    for (const Anomaly& a : r.anomalies) nan_anomaly = nan_anomaly || a.kind == "nan";
    CHECK(nan_anomaly);
    const nlohmann::json logged = nlohmann::json::parse(make_result(r).dump());

    SimInput back;
    PhysicsParams back_p;
    std::string error;
    REQUIRE_MESSAGE(parse_bundle(nlohmann::json::parse(make_bundle(in, p).dump()), back, back_p, error), error);
    const nlohmann::json replayed = make_result(simulate(back, table, back_p));
    const Divergence d = compare_results(logged, replayed, 0.0);
    CHECK_MESSAGE(!d.diverged, d.summary);
    CHECK(replayed["event_digest"] == logged["event_digest"]);
}

TEST_CASE("bundle result lists the first 400 events and digests all of them") {
    const ShotRecord r = sample_record(1000);
    const nlohmann::json result = make_result(r);
    CHECK(result["events"] == 1000);
    REQUIRE(result["event_list"].size() == static_cast<std::size_t>(kResultEventCap));
    const nlohmann::json all = full_event_list(r);
    for (std::size_t i = 0; i < result["event_list"].size(); ++i) {
        CAPTURE(i);
        CHECK(result["event_list"][i] == all[i]);
    }
    CHECK(result["event_digest"] == ofs::params::fnv1a64_hex(ofs::params::canonical(all)));

    ShotRecord later = r;
    later.events[900].body = 6;
    later.events[900].kind = EventKind::ball_arc;
    const nlohmann::json changed = make_result(later);
    CHECK(changed["event_list"] == result["event_list"]);
    CHECK(changed["event_digest"] != result["event_digest"]);
}

TEST_CASE("bundle result reports potted balls, the final state, the strike and the anomalies") {
    const ShotRecord r = sample_record(50);
    const nlohmann::json result = make_result(r);
    std::set<std::string> keys;
    for (const auto& [key, value] : result.items()) keys.insert(key);
    CHECK(keys == std::set<std::string>{"events", "t_end", "potted", "final", "miscue", "elevation_used_rad",
                                        "elevation_raised", "anomalies", "sim_us", "event_list", "event_digest"});
    CHECK(result["potted"] == nlohmann::json::array({{{"ball", 4}, {"pocket", 2}, {"t", r.events[10].t}}}));
    CHECK(result["final"] == to_json(r.final_state));
    CHECK(result["t_end"] == r.t_end);
    CHECK(result["miscue"] == true);
    CHECK(result["elevation_used_rad"] == 0.25);
    CHECK(result["elevation_raised"] == true);
    CHECK(result["anomalies"] == 2);
    CHECK(result["sim_us"] == 321.5);
    CHECK(result["event_list"].size() == 50);
}

TEST_CASE("bundle result digest is stable") {
    // FNV-1a 64 of [[0.0,"strike",0,-1,-1],[0.25,"ball_ball",0,3,-1],[1.5,"pocket",3,-1,2]],
    // computed independently of this code. A change here changes every logged digest.
    ShotRecord r;
    r.events = {{0.0, EventKind::strike, 0, -1, -1, Motion::stationary, Motion::sliding},
                {0.25, EventKind::ball_ball, 0, 3, -1, Motion::rolling, Motion::sliding},
                {1.5, EventKind::pocket, 3, -1, 2, Motion::rolling, Motion::pocketed}};
    CHECK(make_result(r)["event_digest"] == "1634e816ad969315");
}

TEST_CASE("bundle compare_results: identical results agree") {
    const nlohmann::json a = make_result(sample_record(1000));
    const Divergence d = compare_results(a, a, 1e-9);
    CHECK_FALSE(d.diverged);
    CHECK(d.first_event == -1);
    CHECK(d.max_position_error_m == 0.0);
    CHECK_MESSAGE(d.summary.rfind("no divergence", 0) == 0, d.summary);
}

TEST_CASE("bundle compare_results finds the first differing event") {
    const nlohmann::json logged = make_result(sample_record(300));
    nlohmann::json replayed = logged;
    replayed["event_list"][37][1] = "guard";
    replayed["event_list"][90][4] = 99;
    Divergence d = compare_results(logged, replayed, 1e-9);
    CHECK(d.diverged);
    CHECK(d.first_event == 37);
    CHECK_MESSAGE(d.summary.find("event 37") != std::string::npos, d.summary);

    replayed = logged;
    replayed["event_list"][12][4] = 99;
    d = compare_results(logged, replayed, 1e-9);
    CHECK(d.first_event == 12);

    replayed = logged;
    replayed["event_list"][20][2] = 7;
    CHECK(compare_results(logged, replayed, 1e-9).first_event == 20);
}

TEST_CASE("bundle compare_results allows event times within 1e-9 s") {
    const nlohmann::json logged = make_result(sample_record(300));
    nlohmann::json replayed = logged;
    replayed["event_list"][50][0] = replayed["event_list"][50][0].get<double>() + 5e-10;
    CHECK_FALSE(compare_results(logged, replayed, 1e-9).diverged);
    replayed["event_list"][50][0] = replayed["event_list"][50][0].get<double>() + 1e-6;
    const Divergence d = compare_results(logged, replayed, 1e-9);
    CHECK(d.diverged);
    CHECK(d.first_event == 50);
}

TEST_CASE("bundle compare_results catches a shorter replay and a different count past the cap") {
    const nlohmann::json logged = make_result(sample_record(300));
    const nlohmann::json shorter = make_result(sample_record(250));
    Divergence d = compare_results(logged, shorter, 1.0);
    CHECK(d.diverged);
    CHECK(d.first_event == 250);

    const nlohmann::json long_a = make_result(sample_record(1000));
    const nlohmann::json long_b = make_result(sample_record(1001));
    d = compare_results(long_a, long_b, 1.0);
    CHECK(d.diverged);
    CHECK(d.first_event == kResultEventCap);
}

TEST_CASE("bundle compare_results calls a digest difference past the cap a divergence") {
    const ShotRecord r = sample_record(1000);
    const nlohmann::json logged = nlohmann::json::parse(make_result(r).dump());
    ShotRecord later = r;
    later.events[900].body = 6;
    later.events[900].kind = EventKind::ball_arc;
    const nlohmann::json replayed = make_result(later);
    Divergence d = compare_results(logged, replayed, 1e-9);
    CHECK(d.diverged);
    CHECK(d.first_event == -1);
    CHECK_MESSAGE(d.summary.rfind("events past the first 400 differ (digest)", 0) == 0, d.summary);

    // The listed events agree within the time tolerance but not bit for bit, so the digest can't say
    // where the difference is.
    ShotRecord drifted = r;
    drifted.events[50].t += 5e-10;
    d = compare_results(logged, make_result(drifted), 1e-9);
    CHECK(d.diverged);
    CHECK(d.first_event == -1);
    CHECK_MESSAGE(d.summary.find("not bit for bit") != std::string::npos, d.summary);

    nlohmann::json undigested = logged;
    undigested.erase("event_digest");
    d = compare_results(undigested, logged, 1e-9);
    CHECK(d.diverged);
    CHECK_MESSAGE(d.summary.find("logged result has no event_digest") != std::string::npos, d.summary);

    // Up to the cap every event is listed and compared within the tolerance; the digest isn't used.
    const nlohmann::json short_logged = make_result(sample_record(300));
    nlohmann::json short_replayed = short_logged;
    short_replayed["event_digest"] = "0000000000000000";
    CHECK_FALSE(compare_results(short_logged, short_replayed, 1e-9).diverged);
}

TEST_CASE("bundle compare_results names a final position it can't read") {
    const nlohmann::json logged = make_result(sample_record(20));
    nlohmann::json replayed = logged;
    replayed["final"]["balls"][6]["r"][1] = "nan";
    const Divergence d = compare_results(logged, replayed, 1.0);
    CHECK(d.diverged);
    CHECK_MESSAGE(d.summary.find("ball 6") != std::string::npos, d.summary);
    CHECK_MESSAGE(d.summary.find("\"nan\"") != std::string::npos, d.summary);
}

TEST_CASE("bundle compare_results compares potted balls exactly") {
    const nlohmann::json logged = make_result(sample_record(300));
    nlohmann::json replayed = logged;
    replayed["potted"][0]["pocket"] = 3;
    Divergence d = compare_results(logged, replayed, 1e-9);
    CHECK(d.diverged);
    CHECK(d.first_event == -1);
    CHECK_MESSAGE(d.summary.find("potted") != std::string::npos, d.summary);

    replayed = logged;
    replayed["potted"] = nlohmann::json::array();
    CHECK(compare_results(logged, replayed, 1e-9).diverged);
}

TEST_CASE("bundle compare_results measures final position error against the tolerance") {
    const nlohmann::json logged = make_result(sample_record(300));
    nlohmann::json replayed = logged;
    const double x = replayed["final"]["balls"][7]["r"][0].get<double>();
    const double y = replayed["final"]["balls"][7]["r"][1].get<double>();
    replayed["final"]["balls"][7]["r"] = {x + 0.006, y - 0.008};
    Divergence d = compare_results(logged, replayed, 1e-3);
    CHECK(d.diverged);
    CHECK(d.first_event == -1);
    CHECK(d.max_position_error_m == doctest::Approx(0.01).epsilon(1e-9));
    CHECK_MESSAGE(d.summary.find("ball 7") != std::string::npos, d.summary);
    d = compare_results(logged, replayed, 0.1);
    CHECK_FALSE(d.diverged);
    CHECK(d.max_position_error_m == doctest::Approx(0.01).epsilon(1e-9));
}

TEST_CASE("bundle compare_results reports malformed results instead of throwing") {
    const nlohmann::json good = make_result(sample_record(20));
    for (const char* key : {"event_list", "potted", "final"}) {
        CAPTURE(key);
        nlohmann::json broken = good;
        broken.erase(key);
        Divergence d;
        CHECK_NOTHROW(d = compare_results(good, broken, 1e-9));
        CHECK(d.diverged);
        CHECK_FALSE(d.summary.empty());
    }
    nlohmann::json bad_entry = good;
    bad_entry["event_list"][3] = "nonsense";
    CHECK(compare_results(good, bad_entry, 1e-9).first_event == 3);
    CHECK(compare_results(nlohmann::json::array(), good, 1e-9).diverged);
}

TEST_CASE("bundle replay of a simulated shot matches its logged result") {
    const SimInput in = sample_input();
    const PhysicsParams p = sample_params();
    const TableShape table;
    const nlohmann::json logged = make_result(simulate(in, table, p));
    SimInput back;
    PhysicsParams back_p;
    std::string error;
    REQUIRE(parse_bundle(nlohmann::json::parse(make_bundle(in, p).dump()), back, back_p, error));
    const nlohmann::json replayed = make_result(simulate(back, table, back_p));
    const Divergence d = compare_results(logged, replayed, 0.0);
    CHECK_FALSE(d.diverged);
    CHECK(d.first_event == -1);
    CHECK(d.max_position_error_m == 0.0);
    CHECK(replayed["event_digest"] == logged["event_digest"]);
}

TEST_CASE("bundle log_shot emits a schema-valid shot.simulated with the bundle and result") {
    const SimInput in = sample_input();
    const PhysicsParams p = sample_params();
    const ShotRecord r = sample_record(30);
    auto capture = std::make_shared<Capture>();
    ofs::log::Logger::instance().clear_sinks();
    ofs::log::Logger::instance().add_sink(capture);
    log_shot(in, p, r);
    ofs::log::Logger::instance().clear_sinks();
    REQUIRE(capture->events.size() == 1);
    const ofs::log::Event& e = capture->events[0];
    CHECK(e.event == "shot.simulated");
    CHECK(e.subsystem == "shot");
    CHECK(e.level == ofs::log::Level::info);
    const auto problem = ofs::log::check(e, schema());
    CHECK_MESSAGE(!problem.has_value(), problem.value_or(""));
    CHECK(e.data["bundle"] == make_bundle(in, p));
    CHECK(e.data["result"] == make_result(r));
}
