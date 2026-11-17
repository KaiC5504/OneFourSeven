// The search after the check round of M4 stage 1: one rung for a pot that needs the top speed,
// the solver's simulation judged rather than run again, the fallback after an exception played as
// any stand-in stroke, trial failures counted,
// the others' chances, "a free ball", and the cue never below the obstruction for its tip.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/physics/cue.hpp"
#include "ofs/rules/rules_log.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::FrameState;
using rules::scenario::ball;

namespace {

constexpr double R = table::kBallRadius;

struct Capture : ofs::log::Sink {
    std::vector<ofs::log::Event> events;
    void write(const ofs::log::Event& event, const std::string&) override { events.push_back(event); }
};

// Captures what is logged while it lives.
struct Captured {
    std::shared_ptr<Capture> sink = std::make_shared<Capture>();
    Captured() {
        ofs::log::Logger::instance().clear_sinks();
        ofs::log::Logger::instance().add_sink(sink);
    }
    ~Captured() { ofs::log::Logger::instance().clear_sinks(); }
    std::vector<ofs::log::Event> errors() const {
        std::vector<ofs::log::Event> out;
        for (const ofs::log::Event& e : sink->events) {
            if (e.event == "app.error") out.push_back(e);
        }
        return out;
    }
};

double top_speed(const Env& env) { return 0.95 * env.params.max_cue_speed_m_s; }
bool needs_top(const Pot& pot, const Env& env) { return !(top_speed(env) > least_pot_speed(pot, env)); }

// A weak cue (2 m/s at most), so that a long pot needs all of it, as a thin long cut needs all of
// the real cue's.
struct WeakCue {
    phys::PhysicsParams params = rules::scenario::rig().params;
    WeakCue() { params.max_cue_speed_m_s = 2.0; }
    Env env() const {
        const rules::scenario::Rig& r = rules::scenario::rig();
        return Env{r.table, params, *r.oracle, r.ref, rig::levels().standin};
    }
};

// The cue ball at the baulk end and the black alone, somewhere up the table, found so that its
// pots are all (`every`) or partly too long for anything below the top speed.
FrameState black_needing_top(bool every, const Env& env) {
    for (double x = 1.7; x > -0.5; x -= 0.04) {
        for (double y = -0.85; y <= 0.85; y += 0.04) {
            const FrameState s = rules::scenario::frame({ball(Ball::cue, -1.6, -0.8), ball(Ball::black, x, y)});
            const std::vector<Pot> list = pots(s, env, rig::levels().maximum.max_cut_rad);
            if (list.empty()) continue;
            int fast = 0;
            for (const Pot& p : list) fast += needs_top(p, env) ? 1 : 0;
            if (every ? fast == int(list.size()) : (fast > 0 && fast < int(list.size()))) return s;
        }
    }
    FAIL("no such position");
    return {};
}

Think unlimited() {
    Think t;
    t.max_sims = 1000000;
    return t;
}

Stroke choose(const FrameState& s, const Level& level, const Think& think, std::uint64_t seed = 3) {
    Rng rng(seed);
    return choose_stroke(s, level, rig::env(), rng, think);
}

double lowest_for(const FrameState& s, const Stroke& st, const phys::CueStrike& c) {
    phys::TableState t = rules::scenario::table_state(s);
    if (st.places_cue) t.balls[std::size_t(s.cue_ball)].r = {st.cue_pos.x, st.cue_pos.y, 0.0};
    return phys::min_cue_elevation(t, s.cue_ball, c, rig::env().table, rig::env().params);
}

// Every oracle query but the cue's placement throws: the search fails inside the referee.
class Throwing : public rules::TableOracle {
public:
    explicit Throwing(const rules::TableOracle& real) : real_(real) {}
    bool snookered(const std::vector<rules::BallOnTable>&, int, const std::vector<int>&, bool) const override {
        throw std::runtime_error("oracle down");
    }
    std::vector<int> snookering_balls(const std::vector<rules::BallOnTable>&, int,
                                      const std::vector<int>&) const override {
        throw std::runtime_error("oracle down");
    }
    bool clear_path(const std::vector<rules::BallOnTable>&, int, const std::vector<int>&) const override {
        throw std::runtime_error("oracle down");
    }
    bool full_ball(const std::vector<rules::BallOnTable>&, int, const std::vector<int>&) const override {
        throw std::runtime_error("oracle down");
    }
    bool can_hit(const std::vector<rules::BallOnTable>&, int, const std::vector<int>&) const override {
        throw std::runtime_error("oracle down");
    }
    std::vector<int> touching(const std::vector<rules::BallOnTable>&, int) const override {
        throw std::runtime_error("oracle down");
    }
    std::vector<rules::BallOnTable> spot(const std::vector<rules::BallOnTable>&,
                                         const std::vector<Ball>&) const override {
        throw std::runtime_error("oracle down");
    }
    bool cue_placement_legal(const std::vector<rules::BallOnTable>& balls, int cue_ball, rules::Vec2 pos) const override {
        return real_.cue_placement_legal(balls, cue_ball, pos);
    }
    rules::Vec2 nearest_cue_placement(const std::vector<rules::BallOnTable>& balls, int cue_ball,
                                      rules::Vec2 wanted) const override {
        return real_.nearest_cue_placement(balls, cue_ball, wanted);
    }

private:
    const rules::TableOracle& real_;
};

}  // namespace

TEST_CASE("a pot that needs the top speed is tried at one speed, not at every rung") {
    const WeakCue weak;
    const Env env = weak.env();
    // Beginner tries every pot at each of its speeds: two, or one for a pot that needs the top.
    const Level& beginner = rig::levels().beginner;
    REQUIRE(beginner.speeds == 2);
    const FrameState mixed = black_needing_top(false, env);
    int expected = 0;
    for (const Pot& p : pots(mixed, env, beginner.max_cut_rad)) expected += beginner.spins * (needs_top(p, env) ? 1 : 2);
    Rng rng(3);
    const Stroke st = choose_stroke(mixed, beginner, env, rng, unlimited());
    CHECK(st.stopped == "done");
    CHECK(st.steps == expected);

    // The 147 reading the leave only: every spin once for each such pot, no speeds between rungs
    // that are the same speed, nothing refined between them.
    Level flat = rig::levels().maximum;
    flat.depth = 0;
    const FrameState all_top = black_needing_top(true, env);
    const int listed = int(pots(all_top, env, flat.max_cut_rad).size());
    Rng again(3);
    const Stroke planned = choose_stroke(all_top, flat, env, again, unlimited());
    CHECK(planned.stopped == "done");
    CHECK(planned.steps == flat.spins * listed);
}

TEST_CASE("the solver's simulation of the stroke is the one judged: no simulation is run twice") {
    // Beginner's first step on a straight red is its easiest pot at its softer speed (a tenth of
    // the way from the least that pots to the top, in ratio), centre ball.
    const FrameState s = rig::position("straight_red");
    const Level& beginner = rig::levels().beginner;
    const Pot pot = pots(s, rig::env(), beginner.max_cut_rad).front();
    const double least = least_pot_speed(pot, rig::env());
    Spec spec;
    spec.speed_m_s = least * std::pow(top_speed(rig::env()) / least, 0.1);
    phys::CueStrike centre;
    centre.aim_rad = pot.aim_rad;
    spec.elevation_rad =
        phys::min_cue_elevation(rules::scenario::table_state(s), s.cue_ball, centre, rig::env().table, rig::env().params);
    const SolvedPot solved = solve_pot(s, pot, spec, rig::env(), false, 6);
    REQUIRE(solved.found);
    REQUIRE(solved.record != nullptr);

    Think one;
    one.max_steps = 1;
    const Stroke st = choose(s, beginner, one);
    CHECK(st.steps == 1);
    CHECK(st.sims == solved.sims);
}

TEST_CASE("the shot record solve_pot hands back is the record of its trial") {
    const FrameState s = rig::position("cut_red");
    const Pot pot = pots(s, rig::env(), 1.35).front();
    Spec spec;
    spec.speed_m_s = 2.0;
    const SolvedPot solved = solve_pot(s, pot, spec, rig::env(), false, 12);
    REQUIRE(solved.found);
    REQUIRE(solved.record != nullptr);
    phys::ShotRecord again;
    const Trial trial = try_strike(s, pot.cue, solved.trial.strike, rig::env(), &again);
    REQUIRE(again.final_state.balls.size() == solved.record->final_state.balls.size());
    CHECK(again.events.size() == solved.record->events.size());
    CHECK(again.t_end == solved.record->t_end);
    for (std::size_t i = 0; i < again.final_state.balls.size(); ++i) {
        CHECK(again.final_state.balls[i].r.x == solved.record->final_state.balls[i].r.x);
        CHECK(again.final_state.balls[i].r.y == solved.record->final_state.balls[i].r.y);
    }
    CHECK(trial.rest.size() == solved.trial.rest.size());
}

TEST_CASE("when the search throws, the stand-in's stroke is played with the level's error and says so") {
    const rules::scenario::Rig& r = rules::scenario::rig();
    const Throwing oracle(*r.oracle);
    const Env env{r.table, r.params, oracle, r.ref, rig::levels().standin};
    const FrameState s = rig::position("straight_red");
    Think think;
    think.seed = 77;
    Captured captured;
    Stroke st;
    {
        const rules::ScopedLogContext scope({{"shot_id", "fix-1"}});
        Rng rng(77);
        st = choose_stroke(s, rig::levels().beginner, env, rng, think);
    }
    CHECK(st.fallback.rfind("exception: ", 0) == 0);
    CHECK(st.why.value("level", std::string()) == "beginner");
    CHECK(st.why.contains("drawn"));
    CHECK(st.why.value("maximum", std::string()) == "off");
    // Beginner's error is in it: the stroke played is not the one meant.
    CHECK(st.strike.aim_rad != st.meant.aim_rad);

    bool found = false;
    for (const ofs::log::Event& e : captured.errors()) {
        if (e.data.value("function", std::string()) != "opponent::choose_stroke") continue;
        found = true;
        CHECK(e.data.value("level", std::string()) == "beginner");
        CHECK(e.data.value("seed", std::string()) == "77");
        CHECK(e.data.value("shot_id", std::string()) == "fix-1");
        CHECK(e.data.value("message", std::string()).find("rng next") == std::string::npos);
    }
    CHECK(found);
}

TEST_CASE("trial failures in one decision are logged once in full and then counted") {
    FrameState broken = rig::position("straight_red");
    broken.cue_ball = 99;
    const rules::Vec2 cue{0.0, 0.0};
    {
        Captured captured;
        int counted = 0;
        {
            TrialFailures failures({{"shot_id", "fix-2"}, {"level", "maximum"}, {"seed", "5"}});
            for (int i = 0; i < 5; ++i) try_strike(broken, cue, phys::CueStrike{}, rig::env());
            counted = failures.count();
        }
        CHECK(counted == 5);
        const std::vector<ofs::log::Event> lines = captured.errors();
        REQUIRE(lines.size() == 2);
        CHECK(lines[0].data.value("shot_id", std::string()) == "fix-2");
        CHECK(lines[0].data.value("level", std::string()) == "maximum");
        CHECK(lines[0].data.value("seed", std::string()) == "5");
        CHECK(lines[0].data.value("message", std::string()).find("cue_ball 99") != std::string::npos);
        CHECK_FALSE(lines[0].data.contains("more"));
        CHECK(lines[1].data.value("more", 0) == 4);
        CHECK(lines[1].data.value("shot_id", std::string()) == "fix-2");
    }
    {
        // Outside a decision every failure is its own line, as before.
        Captured captured;
        for (int i = 0; i < 3; ++i) try_strike(broken, cue, phys::CueStrike{}, rig::env());
        CHECK(captured.errors().size() == 3);
    }
    // A sound decision counts none.
    CHECK(choose(rig::position("straight_red"), rig::levels().beginner, Think{}).errors == 0);
}

TEST_CASE("why.others gives each other pot its chance, 1 for the 147") {
    Think t;
    t.max_sims = 1500;
    int others[2] = {0, 0};
    // Tables with many reds open.
    for (std::size_t k = 0; k < 2 && k < rig::levels().exhibitions.size(); ++k) {
        CAPTURE(k);
        const FrameState s = exhibition_frame(rig::levels().exhibitions[k], 0, 1);
        const Stroke maximum = choose(s, rig::levels().maximum, t);
        const Stroke beginner = choose(s, rig::levels().beginner, Think{});
        for (const Stroke* st : {&maximum, &beginner}) {
            REQUIRE(st->why.value("kind", std::string()) == "pot");
            REQUIRE(st->why["others"].is_array());
            for (const nlohmann::json& o : st->why["others"]) {
                REQUIRE(o.contains("chance"));
                const double chance = o["chance"].get<double>();
                CHECK(chance >= 0.0);
                CHECK(chance <= 1.0);
                if (st == &maximum) CHECK(chance == 1.0);
                ++others[st == &maximum ? 0 : 1];
            }
        }
    }
    CHECK(others[0] > 0);
    CHECK(others[1] > 0);
}

TEST_CASE("with a free ball on a full table the maximum is gone because of the free ball") {
    // README "The search": `a free ball` (147 is what it plays for, not 155).
    FrameState s = rig::racked(4);
    s.cue_in_hand = false;
    s.balls[std::size_t(s.cue_ball)].pos = {-1.2, 0.3};
    s.free_ball = true;
    Think t;
    t.max_sims = 200;
    const Stroke st = choose(s, rig::levels().maximum, t);
    CHECK(st.why.value("maximum", std::string()) == "gone");
    CHECK(st.why.value("maximum_why", std::string()) == "a free ball");
}

TEST_CASE("the cue is never below the obstruction for the aim and the tip actually played") {
    // The cue ball close to a cushion and to a ball behind it, where screw needs the cue raised
    // more than centre ball does.
    const double hx = table::kPlayingLength / 2.0 - R;
    std::vector<FrameState> tables = {
        rig::position("straight_red"), rig::position("cut_red"), rig::position("long_red"),
        rules::scenario::frame({ball(Ball::cue, -hx + 0.004, 0.1), ball(Ball::red, -0.6, 0.3, 0)}),
        rules::scenario::frame(
            {ball(Ball::cue, 0.9, 0.2), ball(Ball::blue, 0.9 - 2.0 * R - 0.01, 0.2), ball(Ball::red, 1.4, 0.55, 0)}),
    };
    Think t;
    t.max_sims = 1500;
    for (std::size_t i = 0; i < tables.size(); ++i) {
        CAPTURE(i);
        for (const Level* level : {&rig::levels().beginner, &rig::levels().maximum}) {
            CAPTURE(level->id);
            for (std::uint64_t seed = 1; seed <= 3; ++seed) {
                const Stroke st = choose(tables[i], *level, level->maximum ? t : Think{}, seed);
                if (st.decides) continue;
                CHECK(st.meant.elevation_rad >= lowest_for(tables[i], st, st.meant));
                CHECK(st.strike.elevation_rad >= lowest_for(tables[i], st, st.strike));
                phys::CueStrike centre = st.strike;
                centre.tip_x = 0.0;
                centre.tip_y = 0.0;
                CHECK(st.strike.elevation_rad >= lowest_for(tables[i], st, centre));
                CHECK(st.strike.elevation_rad == st.meant.elevation_rad);
                if (level->maximum) break;
            }
        }
    }
}
