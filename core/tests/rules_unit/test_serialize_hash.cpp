#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ofs/params.hpp"
#include "ofs/rules/serialize.hpp"

using namespace ofs::rules;
using nlohmann::json;

namespace {

FrameState example_frame() {
    std::ifstream in(std::string(OFS_TEST_FIXTURES_DIR) + "/rules/example_frame.json", std::ios::binary);
    REQUIRE(in.good());
    FrameState s;
    std::string error;
    REQUIRE_MESSAGE(frame_from_json(json::parse(in), s, error), error);
    return s;
}

double next_up(double v) { return std::nextafter(v, std::numeric_limits<double>::infinity()); }

using Change = std::pair<const char*, std::function<void(FrameState&)>>;

// One change to each member a frame has, the smallest there is where it is a number.
std::vector<Change> changes() {
    std::vector<Change> out = {
        {"a ball's kind", [](FrameState& s) { s.balls[3].ball = Ball::pink; }},
        {"a red's index", [](FrameState& s) { s.balls[1].index = 4; }},
        {"a ball's x by one ulp", [](FrameState& s) { s.balls[2].pos.x = next_up(s.balls[2].pos.x); }},
        {"a ball's y by one ulp", [](FrameState& s) { s.balls[8].pos.y = next_up(s.balls[8].pos.y); }},
        {"the sign of a zero", [](FrameState& s) { s.balls[6].pos.x = -0.0; }},
        {"two balls' places", [](FrameState& s) { std::swap(s.balls[1], s.balls[2]); }},
        {"a ball fewer", [](FrameState& s) { s.balls.pop_back(); }},
        {"cue_ball", [](FrameState& s) { s.cue_ball = 1; }},
        {"cue_in_hand", [](FrameState& s) { s.cue_in_hand = !s.cue_in_hand; }},
        {"on.kind", [](FrameState& s) { s.on.kind = OnKind::any_colour; }},
        {"free_ball", [](FrameState& s) { s.free_ball = !s.free_ball; }},
        {"touching", [](FrameState& s) { s.touching.push_back(1); }},
        {"seed", [](FrameState& s) { s.seed += 1; }},
        {"striker", [](FrameState& s) { s.striker = 1 - s.striker; }},
        {"scores[0]", [](FrameState& s) { s.scores[0] += 1; }},
        {"scores[1]", [](FrameState& s) { s.scores[1] += 1; }},
        {"break_points", [](FrameState& s) { s.break_points += 1; }},
        {"strokes", [](FrameState& s) { s.strokes += 1; }},
        {"respotted_black", [](FrameState& s) { s.respotted_black = !s.respotted_black; }},
        {"concede_any_time[0]", [](FrameState& s) { s.concede_any_time[0] = !s.concede_any_time[0]; }},
        {"concede_any_time[1]", [](FrameState& s) { s.concede_any_time[1] = !s.concede_any_time[1]; }},
        {"pending", [](FrameState& s) { s.pending = Pending::toss; }},
        {"decider", [](FrameState& s) { s.decider = 1 - s.decider; }},
        {"offender", [](FrameState& s) { s.offender = 1 - s.offender; }},
        {"last_penalty", [](FrameState& s) { s.last_penalty += 1; }},
        {"last_miss", [](FrameState& s) { s.last_miss = !s.last_miss; }},
        {"original: a ball's x by one ulp",
         [](FrameState& s) { s.original.balls[0].pos.x = next_up(s.original.balls[0].pos.x); }},
        {"original.cue_ball", [](FrameState& s) { s.original.cue_ball = 2; }},
        {"original.cue_in_hand", [](FrameState& s) { s.original.cue_in_hand = !s.original.cue_in_hand; }},
        {"original.on", [](FrameState& s) { s.original.on = {OnKind::colour, Ball::green}; }},
        {"original.free_ball", [](FrameState& s) { s.original.free_ball = !s.original.free_ball; }},
        {"original.touching", [](FrameState& s) { s.original.touching = {4}; }},
        {"miss_run", [](FrameState& s) { s.miss_run += 1; }},
        {"miss_warned", [](FrameState& s) { s.miss_warned = !s.miss_warned; }},
        {"quiet_strokes", [](FrameState& s) { s.quiet_strokes += 1; }},
        {"rerack_accepted", [](FrameState& s) { s.rerack_accepted = 0; }},
        {"rerack_deadline", [](FrameState& s) { s.rerack_deadline = 5; }},
        {"over", [](FrameState& s) { s.over = !s.over; }},
        {"winner", [](FrameState& s) { s.winner = 0; }},
        {"end_reason", [](FrameState& s) { s.end_reason = EndReason::conceded; }},
        {"restart", [](FrameState& s) { s.restart = !s.restart; }},
    };
    for (const std::size_t player : {std::size_t(0), std::size_t(1)}) {
        out.push_back({"stats.shots", [player](FrameState& s) { s.stats[player].shots += 1; }});
        out.push_back({"stats.pots", [player](FrameState& s) { s.stats[player].pots += 1; }});
        out.push_back({"stats.pot_attempts", [player](FrameState& s) { s.stats[player].pot_attempts += 1; }});
        out.push_back({"stats.fouls", [player](FrameState& s) { s.stats[player].fouls += 1; }});
        out.push_back({"stats.misses", [player](FrameState& s) { s.stats[player].misses += 1; }});
        out.push_back({"stats.points_from_fouls", [player](FrameState& s) { s.stats[player].points_from_fouls += 1; }});
        out.push_back({"stats.highest_break", [player](FrameState& s) { s.stats[player].highest_break += 1; }});
    }
    return out;
}

}  // namespace

TEST_CASE("frame_hash is the content hash of the frame's JSON") {
    const FrameState s = example_frame();
    const std::string hash = frame_hash(s);
    CHECK(hash == ofs::params::content_hash(frame_json(s)));
    CHECK(hash.size() == 16);
    CHECK(hash.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(frame_hash(example_frame()) == hash);
    CHECK(frame_hash(FrameState{}) != hash);
}

TEST_CASE("frame_hash changes with any member of the frame") {
    const FrameState base = example_frame();
    const std::string hash = frame_hash(base);
    const json before = frame_json(base);
    std::set<std::string> hashes = {hash};
    std::set<std::string> keys_changed;
    for (const Change& change : changes()) {
        CAPTURE(change.first);
        FrameState s = base;
        change.second(s);
        CHECK(frame_hash(s) != hash);
        hashes.insert(frame_hash(s));
        const json after = frame_json(s);
        for (auto it = before.begin(); it != before.end(); ++it) {
            if (after.at(it.key()).dump() != it.value().dump()) keys_changed.insert(it.key());
        }
    }
    CHECK(hashes.size() == changes().size() + 1);

    // The list above reaches every key the frame's JSON has, so a member added to the form later
    // without a change here fails this.
    std::set<std::string> keys;
    for (auto it = before.begin(); it != before.end(); ++it) keys.insert(it.key());
    CHECK(keys.size() == 29);
    CHECK(keys_changed == keys);
}

TEST_CASE("the colour on is part of the hash only while a colour is on") {
    FrameState a = example_frame();
    FrameState b = a;
    a.on = {OnKind::colour, Ball::blue};
    b.on = {OnKind::colour, Ball::pink};
    CHECK(frame_hash(a) != frame_hash(b));
    a.on = {OnKind::red, Ball::blue};
    b.on = {OnKind::red, Ball::pink};
    CHECK(frame_hash(a) == frame_hash(b));
}

TEST_CASE("a frame with a number that is not finite still has a hash") {
    FrameState s = example_frame();
    const std::string hash = frame_hash(s);
    s.balls[2].pos.x = std::numeric_limits<double>::quiet_NaN();
    CHECK(frame_hash(s).size() == 16);
    CHECK(frame_hash(s) != hash);
}
