// Stage 2 leaves Beginner and the 147 deciding as they did in stage 1 (README "Stage 2"): their
// strokes over frames the stand-in played, the fixtures and the exhibition tables, recorded from
// the stage 1 search in fixtures/opponent/pl_stage1_strokes.json. Beginner is compared through a
// copy of its level that never concedes, whatever its file says.
//
// OFS_PL_RECORD=1 writes the fixture again from the code as it stands.

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "ofs/opponent/opponent.hpp"
#include "ofs/opponent/standin.hpp"
#include "ofs/rules/serialize.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::FrameState;

namespace {

constexpr int kPositions = 150;
// Most positions take the 147 with a smaller budget than its own, the stages all the same; a few
// take it whole.
constexpr int kMaximumSims = 500;
constexpr int kWhole = 6;

std::string fixture_path() { return std::string(OFS_TEST_FIXTURES_DIR) + "/opponent/pl_stage1_strokes.json"; }

// Frames the stand-in plays against itself, every third position in play kept.
std::vector<FrameState> standin_positions(int count) {
    const Env env = rig::env();
    const rules::scenario::Rig& r = rules::scenario::rig();
    std::vector<FrameState> out;
    for (std::uint64_t frame = 1; int(out.size()) < count && frame < 200; ++frame) {
        FrameState s = rig::racked(frame);
        Rng rng(1000 + frame);
        int seen = 0;
        for (int stroke = 0; stroke < 400 && !s.over && int(out.size()) < count; ++stroke) {
            if (s.pending != rules::Pending::none) {
                const int who = s.decider;
                const std::vector<rules::DecisionKind> open = rules::options(s, who);
                if (open.empty()) break;
                const rules::DecisionKind kind = choose_decision(s, who, open, env.standin, rng);
                std::vector<rules::Call> calls;
                std::string error;
                if (!rules::decide(s, {kind, who}, env.oracle, env.ref, calls, error)) break;
                continue;
            }
            if (seen++ % 3 == 0) out.push_back(s);
            const StandinShot shot = choose_shot(s, r.table, r.params, env.oracle, env.ref, env.standin, rng);
            std::vector<rules::Call> calls;
            std::string error;
            if (shot.decides) {
                if (!rules::decide(s, {shot.decision, s.striker}, env.oracle, env.ref, calls, error)) break;
                continue;
            }
            if (shot.places_cue && !rules::place_cue_ball(s, shot.cue_pos, env.oracle, error)) break;
            s = rules::scenario::play(s, shot.strike, shot.input, frame * 7919 + std::uint64_t(stroke)).judgement.post;
        }
    }
    return out;
}

std::vector<FrameState> positions() {
    std::vector<FrameState> out = standin_positions(kPositions - 19);
    for (const char* name : {"after_red", "black_only", "colours", "cut_red", "in_hand", "last_red", "long_red",
                             "snookered", "straight_red"}) {
        out.push_back(rig::position(name));
    }
    for (int i = 0; i < 10; ++i) {
        out.push_back(exhibition_frame(rig::levels().exhibitions[std::size_t(i * 5)], 0, std::uint64_t(i + 1)));
    }
    return out;
}

Level never_concedes() {
    Level l = rig::levels().beginner;
    l.concede_snookers = 0;
    return l;
}

nlohmann::json strike_json(const phys::CueStrike& c) {
    return {c.aim_rad, c.speed_m_s, c.tip_x, c.tip_y, c.elevation_rad};
}

nlohmann::json stroke_json(const Stroke& st) {
    return {{"decides", st.decides},
            {"strike", strike_json(st.strike)},
            {"meant", strike_json(st.meant)},
            {"kind", st.why.value("kind", std::string())},
            {"target", st.why.value("target", std::string())},
            {"pocket", st.why.value("pocket", -1)},
            {"steps", st.steps},
            {"sims", st.sims}};
}

Stroke decide_for(const FrameState& s, const Level& level, std::uint64_t seed) {
    Rng rng(seed);
    return choose_stroke(s, level, rig::env(), rng);
}

Level maximum_for(int i) {
    Level l = rig::levels().maximum;
    if (i % (kPositions / kWhole) != 0) l.max_sims = kMaximumSims;
    return l;
}

void record() {
    const Level beginner = never_concedes();
    nlohmann::json cases = nlohmann::json::array();
    const std::vector<FrameState> all = positions();
    for (int i = 0; i < int(all.size()); ++i) {
        const FrameState& s = all[std::size_t(i)];
        const std::uint64_t seed = 31 + std::uint64_t(i);
        cases.push_back({{"frame", rules::frame_json(s)},
                         {"seed", seed},
                         {"beginner", stroke_json(decide_for(s, beginner, seed))},
                         {"maximum", stroke_json(decide_for(s, maximum_for(i), seed))}});
    }
    std::ofstream out(fixture_path(), std::ios::binary);
    out << nlohmann::json{{"note", "Stage 1's Beginner and 147 strokes, by test_plan_stage1_unchanged.cpp"},
                          {"cases", cases}}
               .dump()
        << "\n";
}

}  // namespace

TEST_CASE("Beginner and the 147 choose their strokes as stage 1 did") {
    if (std::getenv("OFS_PL_RECORD") != nullptr) record();
    std::ifstream in(fixture_path(), std::ios::binary);
    const nlohmann::json fixture = nlohmann::json::parse(in, nullptr, false);
    REQUIRE(fixture.is_object());
    const nlohmann::json& cases = fixture["cases"];
    REQUIRE(cases.size() >= std::size_t(kPositions));

    const Level beginner = never_concedes();
    int pots = 0;
    for (int i = 0; i < int(cases.size()); ++i) {
        const nlohmann::json& c = cases[std::size_t(i)];
        FrameState s;
        std::string error;
        REQUIRE(rules::frame_from_json(c["frame"], s, error));
        const std::uint64_t seed = c["seed"].get<std::uint64_t>();
        CAPTURE(i);
        CHECK(stroke_json(decide_for(s, beginner, seed)) == c["beginner"]);
        CHECK(stroke_json(decide_for(s, maximum_for(i), seed)) == c["maximum"]);
        pots += c["beginner"]["kind"] == "pot" ? 1 : 0;
    }
    // The positions are not all one kind of stroke.
    CHECK(pots > 30);
    CHECK(pots < int(cases.size()) - 10);
}
