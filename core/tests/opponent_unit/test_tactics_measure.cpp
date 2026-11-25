// What choose_tactic does over positions from frames the stand-in plays: run by hand in Release,
// `ofs_opponent_unit_tests -tc="tactics measured*" --no-skip`, for the builder's report.

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "ofs/opponent/budget.hpp"
#include "ofs/opponent/error_model.hpp"
#include "ofs/opponent/solve.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/opponent/tactics.hpp"
#include "ofs/rules/engine.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::FrameState;

namespace {

// Every position the stand-in is to strike from in frames it plays against itself, nothing
// pending, until `count` are taken.
std::vector<FrameState> standin_positions(int count) {
    const rules::scenario::Rig& r = rules::scenario::rig();
    const StandinParams& sp = rig::levels().standin;
    std::vector<FrameState> out;
    for (std::uint64_t frame = 1; int(out.size()) < count && frame < 200; ++frame) {
        FrameState s = rig::racked(frame);
        Rng rng(frame * 7919);
        for (int stroke = 0; stroke < 400 && !s.over && int(out.size()) < count; ++stroke) {
            std::vector<rules::Call> calls;
            std::string error;
            if (s.pending != rules::Pending::none) {
                const std::vector<rules::DecisionKind> open = rules::options(s, s.decider);
                const rules::DecisionKind kind = choose_decision(s, s.decider, open, sp, rng);
                if (!rules::decide(s, {kind, s.decider}, *r.oracle, r.ref, calls, error)) break;
                continue;
            }
            out.push_back(s);
            const StandinShot shot = choose_shot(s, r.table, r.params, *r.oracle, r.ref, sp, rng);
            if (shot.decides) {
                if (!rules::decide(s, {shot.decision, s.striker}, *r.oracle, r.ref, calls, error)) break;
                continue;
            }
            FrameState from = s;
            if (shot.places_cue && !rules::place_cue_ball(from, shot.cue_pos, *r.oracle, error)) break;
            const phys::ShotRecord record = rules::scenario::simulate(from, shot.strike, frame * 131 + stroke);
            s = rules::judge_shot(from, shot.input, record, r.table, *r.oracle, r.ref).post;
        }
    }
    return out;
}

struct Tally {
    int calls = 0;
    std::map<std::string, int> found;
    double sims = 0.0;
    int sims_max = 0;
    double ms = 0.0;
    double ms_max = 0.0;
    int safeties = 0;
    int safe_leaves = 0;  // the other player has no pot over 0.5 as read
    int escapes = 0;
    int escapes_hit = 0;
    int snookers = 0;
    int fouls = 0;  // answered strokes the referee calls a foul, an escape's planned miss aside
};

void measure(const char* name, const Level& level, const std::vector<FrameState>& positions) {
    const Env env = rig::env();
    Tally t;
    for (const FrameState& s : positions) {
        Think think;
        Budget budget(level, think);
        Rng judgement(17);
        const auto t0 = std::chrono::steady_clock::now();
        const Tactic tactic = choose_tactic(s, level, env, budget, judgement);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ++t.calls;
        t.sims += budget.sims;
        t.sims_max = std::max(t.sims_max, budget.sims);
        t.ms += ms;
        t.ms_max = std::max(t.ms_max, ms);
        if (!tactic.found) {
            ++t.found["none"];
            continue;
        }
        ++t.found[tactic.kind];
        if (tactic.kind == "safety" || tactic.kind == "snooker") {
            ++t.safeties;
            if (tactic.why["leave"]["chance"].get<double>() <= 0.5) ++t.safe_leaves;
            if (tactic.kind == "snooker") ++t.snookers;
        }
        const bool planned_miss = tactic.kind == "escape" && !tactic.why["hit"].get<bool>();
        if (tactic.kind == "escape") {
            ++t.escapes;
            if (!planned_miss) ++t.escapes_hit;
        }
        FrameState from = s;
        std::string error;
        if (tactic.places_cue) rules::place_cue_ball(from, tactic.cue_pos, env.oracle, error);
        phys::ShotRecord record;
        try_strike(from, tactic.places_cue ? tactic.cue_pos : s.balls[std::size_t(s.cue_ball)].pos, tactic.meant, env,
                   &record);
        const rules::Judgement j = rules::judge_shot(from, tactic.input, record, env.table, env.oracle, env.ref);
        if (!j.fouls.empty() && !planned_miss) ++t.fouls;
    }
    std::printf("%s (tactics %d, tries %d): %d calls\n", name, level.tactics, level.safety_tries, t.calls);
    for (const auto& [kind, n] : t.found) std::printf("  %-8s %5.1f%%\n", kind.c_str(), 100.0 * n / t.calls);
    std::printf("  sims per call mean %.1f max %d; ms per call mean %.2f max %.2f\n", t.sims / t.calls, t.sims_max,
                t.ms / t.calls, t.ms_max);
    if (t.safeties > 0) {
        std::printf("  safeties leaving no pot over 0.5: %.1f%% of %d\n", 100.0 * t.safe_leaves / t.safeties,
                    t.safeties);
        if (level.tactics >= 3) std::printf("  snookers per 100 safeties: %.1f\n", 100.0 * t.snookers / t.safeties);
    }
    if (t.escapes > 0) std::printf("  escapes hitting a ball on: %d of %d\n", t.escapes_hit, t.escapes);
    std::printf("  fouls as answered (planned misses aside): %d\n", t.fouls);
    CHECK(t.fouls == 0);
}

}  // namespace

TEST_CASE("tactics measured over the stand-in's frames" * doctest::skip()) {
    const std::vector<FrameState> positions = standin_positions(240);
    int in_hand = 0;
    int breaks = 0;
    int snookered = 0;
    const Env env = rig::env();
    for (const FrameState& s : positions) {
        in_hand += s.cue_in_hand ? 1 : 0;
        int reds = 0;
        for (const rules::BallOnTable& b : s.balls) reds += b.ball == rules::Ball::red ? 1 : 0;
        breaks += s.cue_in_hand && reds == 15 ? 1 : 0;
        snookered += !s.cue_in_hand && !env.oracle.clear_path(s.balls, s.cue_ball, rules::places_on(s)) ? 1 : 0;
    }
    std::printf("%zu positions: %d in hand (%d break-offs), %d with no ball on in direct view\n", positions.size(),
                in_hand, breaks, snookered);
    const rig::Levels& l = rig::levels();
    measure("club", l.club, positions);
    measure("amateur", l.amateur, positions);
    measure("pro", l.pro, positions);
    measure("world", l.world, positions);
}
