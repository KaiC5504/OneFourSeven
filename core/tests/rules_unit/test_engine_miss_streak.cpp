#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ofs/rules/serialize.hpp"

#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;
using nlohmann::json;

namespace {

// Player 0 misses every ball with red on.
Judgement missed(const FrameState& s, const FakeOracle& oracle) {
    return judge(s, {}, scenario::quiet_facts(s), oracle, RefParams{});
}

FrameState chosen(FrameState s, DecisionKind kind, const FakeOracle& oracle) {
    std::vector<Call> calls;
    std::string error;
    REQUIRE_MESSAGE(decide(s, {kind, s.decider}, oracle, RefParams{}, calls, error), error);
    return s;
}

// Snookered, a ball on within reach: a miss is called and S3.14(d)'s run never starts.
FakeOracle snookered() {
    FakeOracle oracle;
    oracle.is_snookered = true;
    oracle.has_clear_path = false;
    oracle.has_full_ball = false;
    return oracle;
}

}  // namespace

TEST_CASE("miss_streak counts the misses put back in a row, also from a snooker where miss_run stays 0") {
    const FakeOracle oracle = snookered();
    FrameState s = open_table(6);
    REQUIRE(s.miss_streak == 0);
    for (int n = 1; n <= 4; ++n) {
        CAPTURE(n);
        const Judgement j = missed(s, oracle);
        REQUIRE(j.miss);
        CHECK(j.post.miss_streak == n);
        CHECK(j.post.miss_run == 0);
        s = chosen(j.post, DecisionKind::replace, oracle);
        CHECK(s.miss_streak == n);
        CHECK(s.striker == 0);
        CHECK(s.pending == Pending::none);
    }
}

TEST_CASE("miss_streak goes back to 0 when the position is not put back") {
    const FakeOracle oracle = snookered();
    for (const DecisionKind kind : {DecisionKind::play, DecisionKind::play_again}) {
        const Judgement j = missed(open_table(6), oracle);
        REQUIRE(j.post.miss_streak == 1);
        CHECK(chosen(j.post, kind, oracle).miss_streak == 0);
    }
}

TEST_CASE("miss_streak goes back to 0 at the next stroke that is not a miss") {
    const FakeOracle oracle = snookered();
    const Judgement first = missed(open_table(6), oracle);
    const FrameState again = chosen(first.post, DecisionKind::replace, oracle);
    REQUIRE(again.miss_streak == 1);

    ShotFacts hit = scenario::quiet_facts(again);
    hit.first_hit = {places_on(again).front()};
    const Judgement fair = judge(again, {}, hit, oracle, RefParams{});
    REQUIRE(fair.fouls.empty());
    CHECK(fair.post.miss_streak == 0);

    // A foul that is no miss: a ball on was hit first and the cue ball went in.
    ShotFacts in_off = hit;
    in_off.cue_potted = true;
    const Judgement foul = judge(again, {}, in_off, oracle, RefParams{});
    REQUIRE_FALSE(foul.fouls.empty());
    REQUIRE_FALSE(foul.miss);
    CHECK(foul.post.miss_streak == 0);
}

TEST_CASE("miss_streak travels in the frame's JSON only when above 0, and a frame without it reads as 0") {
    FrameState s = open_table(6);
    const json plain = frame_json(s);
    CHECK_FALSE(plain.contains("miss_streak"));
    const std::string plain_hash = frame_hash(s);

    s.miss_streak = 3;
    const json counted = frame_json(s);
    REQUIRE(counted.contains("miss_streak"));
    CHECK(counted["miss_streak"] == 3);
    CHECK(frame_hash(s) != plain_hash);

    FrameState back;
    std::string error;
    REQUIRE_MESSAGE(frame_from_json(counted, back, error), error);
    CHECK(back.miss_streak == 3);
    CHECK(frame_json(back) == counted);

    back.miss_streak = 7;
    REQUIRE_MESSAGE(frame_from_json(plain, back, error), error);
    CHECK(back.miss_streak == 0);
    CHECK(frame_hash(back) == plain_hash);
}

TEST_CASE("miss_streak is refused when it is not a count") {
    FrameState s = open_table(6);
    s.miss_streak = 1;
    for (const json& bad : {json(-1), json("two"), json(1.5), json(true)}) {
        json doc = frame_json(s);
        doc["miss_streak"] = bad;
        FrameState out;
        std::string error;
        CHECK_FALSE(frame_from_json(doc, out, error));
        CHECK_MESSAGE(error.rfind("miss_streak: ", 0) == 0, error);
    }
}
