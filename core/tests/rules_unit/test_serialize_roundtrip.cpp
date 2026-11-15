#include <doctest/doctest.h>

#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "ofs/rules/serialize.hpp"

using namespace ofs::rules;
using nlohmann::json;

namespace {

// What the log and the tools see: the text, read back.
json through_text(const json& j) { return json::parse(j.dump()); }

// Equal as doubles is not enough: -0.0 == 0.0.
bool same_bits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

const std::vector<double>& awkward() {
    static const std::vector<double> values = {1e-300,
                                               0.1 + 0.2,
                                               -0.0,
                                               std::numeric_limits<double>::denorm_min(),
                                               -1.0 / 3.0,
                                               std::numeric_limits<double>::max(),
                                               123.456789012345678,
                                               3.0000000000000004,
                                               -1.4605000000000001};
    return values;
}

double awkward_at(std::size_t i) { return awkward()[i % awkward().size()]; }

constexpr Ball kColours[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};

// All 22 balls, each coordinate one of the awkward doubles.
Position awkward_position() {
    Position p;
    p.balls.push_back({Ball::cue, 0, {awkward_at(0), awkward_at(1)}});
    for (int i = 0; i < 15; ++i) {
        p.balls.push_back({Ball::red, i, {awkward_at(std::size_t(2 * i + 2)), awkward_at(std::size_t(2 * i + 3))}});
    }
    for (std::size_t i = 0; i < 6; ++i) p.balls.push_back({kColours[i], 0, {awkward_at(i + 5), awkward_at(i + 6)}});
    p.cue_ball = 0;
    p.cue_in_hand = true;
    p.free_ball = true;
    p.touching = {1, 21};
    return p;
}

void check_same(const Position& a, const Position& b) {
    REQUIRE(a.balls.size() == b.balls.size());
    for (std::size_t i = 0; i < a.balls.size(); ++i) {
        CAPTURE(i);
        CHECK(a.balls[i].ball == b.balls[i].ball);
        CHECK(a.balls[i].index == b.balls[i].index);
        CHECK(same_bits(a.balls[i].pos.x, b.balls[i].pos.x));
        CHECK(same_bits(a.balls[i].pos.y, b.balls[i].pos.y));
    }
    CHECK(a.cue_ball == b.cue_ball);
    CHECK(a.cue_in_hand == b.cue_in_hand);
    CHECK(a.on.kind == b.on.kind);
    if (a.on.kind == OnKind::colour) {
        CHECK(a.on.colour == b.on.colour);
    }
    CHECK(a.free_ball == b.free_ball);
    CHECK(a.touching == b.touching);
}

// A frame with no member at its default.
FrameState busy_frame() {
    FrameState s;
    static_cast<Position&>(s) = awkward_position();
    s.seed = std::numeric_limits<std::uint64_t>::max();
    s.striker = 1;
    s.scores = {57, 68};
    s.break_points = 12;
    s.stats[0] = {31, 14, 19, 3, 2, 12, 41};
    s.stats[1] = {30, 17, 22, 4, 1, 16, 38};
    s.strokes = 61;
    s.respotted_black = true;
    s.concede_any_time = {false, true};
    s.pending = Pending::rerack;
    s.decider = 1;
    s.offender = 1;
    s.last_penalty = 7;
    s.last_miss = true;
    s.original = awkward_position();
    s.original.cue_in_hand = false;
    s.original.on = {OnKind::colour, Ball::pink};
    s.original.touching = {3};
    s.miss_run = 2;
    s.miss_warned = true;
    s.quiet_strokes = 11;
    s.rerack_accepted = 1;
    s.rerack_deadline = 4;
    s.over = true;
    s.winner = 1;
    s.end_reason = EndReason::three_misses;
    s.restart = true;
    return s;
}

FrameState read_back(const FrameState& s) {
    FrameState back;
    std::string error;
    REQUIRE_MESSAGE(frame_from_json(through_text(frame_json(s)), back, error), error);
    return back;
}

}  // namespace

TEST_CASE("the awkward doubles survive the text form bit for bit") {
    for (const double v : awkward()) {
        CAPTURE(v);
        const BallOnTable ball{Ball::blue, 0, {v, -v}};
        Position p;
        p.balls = {ball};
        Position back;
        std::string error;
        REQUIRE_MESSAGE(position_from_json(through_text(position_json(p)), back, error), error);
        CHECK(same_bits(back.balls[0].pos.x, v));
        CHECK(same_bits(back.balls[0].pos.y, -v));
    }
    CHECK(through_text(ball_json({Ball::red, 3, {-0.0, 0.1 + 0.2}})).dump() ==
          R"({"ball":"red","index":3,"x":-0.0,"y":0.30000000000000004})");
}

TEST_CASE("a position with every ball round-trips exactly, for every ball on") {
    Position p = awkward_position();
    std::vector<BallOn> ons = {{OnKind::red, Ball::yellow}, {OnKind::any_colour, Ball::yellow}};
    for (const Ball colour : kColours) ons.push_back({OnKind::colour, colour});
    for (const BallOn& on : ons) {
        p.on = on;
        CAPTURE(on_kind_name(on.kind));
        CAPTURE(ofs::table::ball_name(on.colour));
        const json j = position_json(p);
        CHECK(j["on"]["kind"] == std::string(on_kind_name(on.kind)));
        CHECK(j["on"].contains("colour") == (on.kind == OnKind::colour));
        Position back;
        std::string error;
        REQUIRE_MESSAGE(position_from_json(through_text(j), back, error), error);
        check_same(p, back);
        CHECK(position_json(back) == j);
    }
}

TEST_CASE("a position nobody has set round-trips") {
    Position back;
    std::string error;
    REQUIRE_MESSAGE(position_from_json(through_text(position_json(Position{})), back, error), error);
    CHECK(back.balls.empty());
    CHECK(back.cue_ball == 0);
    CHECK(back.touching.empty());
    CHECK(position_json(Position{})["touching"] == json::array());
}

TEST_CASE("a frame with nothing at its default round-trips exactly") {
    const FrameState s = busy_frame();
    const FrameState back = read_back(s);
    check_same(s, back);
    check_same(s.original, back.original);
    CHECK(frame_json(back) == frame_json(s));
    CHECK(frame_json(s)["seed"] == "18446744073709551615");
    CHECK(back.seed == s.seed);
    CHECK(back.striker == 1);
    CHECK(back.scores == s.scores);
    CHECK(back.break_points == 12);
    CHECK(back.stats[0].shots == 31);
    CHECK(back.stats[0].pots == 14);
    CHECK(back.stats[0].pot_attempts == 19);
    CHECK(back.stats[0].fouls == 3);
    CHECK(back.stats[0].misses == 2);
    CHECK(back.stats[0].points_from_fouls == 12);
    CHECK(back.stats[0].highest_break == 41);
    CHECK(back.stats[1].highest_break == 38);
    CHECK(back.strokes == 61);
    CHECK(back.respotted_black);
    CHECK(back.concede_any_time == s.concede_any_time);
    CHECK(back.decider == 1);
    CHECK(back.offender == 1);
    CHECK(back.last_penalty == 7);
    CHECK(back.last_miss);
    CHECK(back.miss_run == 2);
    CHECK(back.miss_warned);
    CHECK(back.quiet_strokes == 11);
    CHECK(back.rerack_accepted == 1);
    CHECK(back.rerack_deadline == 4);
    CHECK(back.over);
    CHECK(back.winner == 1);
    CHECK(back.restart);
}

TEST_CASE("a default frame round-trips") {
    const FrameState back = read_back(FrameState{});
    CHECK(frame_json(back) == frame_json(FrameState{}));
    CHECK(back.winner == -1);
    CHECK(back.rerack_accepted == -1);
    CHECK(back.rerack_deadline == -1);
}

TEST_CASE("a frame round-trips with every pending state and end reason") {
    FrameState s = busy_frame();
    for (int i = 0; i <= int(Pending::toss); ++i) {
        s.pending = Pending(i);
        CHECK(frame_json(s)["pending"] == std::string(pending_name(s.pending)));
        CHECK(read_back(s).pending == s.pending);
    }
    for (int i = 0; i <= int(EndReason::match_conceded); ++i) {
        s.end_reason = EndReason(i);
        CHECK(frame_json(s)["end_reason"] == std::string(end_reason_name(s.end_reason)));
        CHECK(read_back(s).end_reason == s.end_reason);
    }
}

TEST_CASE("a seed is a decimal string that carries every uint64") {
    for (const std::uint64_t seed : {std::uint64_t(0), std::uint64_t(1), (std::uint64_t(1) << 53) + 1,
                                     std::uint64_t(0x8000000000000000ULL), std::numeric_limits<std::uint64_t>::max()}) {
        FrameState s;
        s.seed = seed;
        CHECK(frame_json(s)["seed"] == std::to_string(seed));
        CHECK(read_back(s).seed == seed);

        MatchConfig c;
        c.seed = seed;
        MatchConfig back;
        std::string error;
        REQUIRE_MESSAGE(config_from_json(through_text(config_json(c)), back, error), error);
        CHECK(back.seed == seed);
    }
}

TEST_CASE("an input round-trips with every ball and with none") {
    for (int i = 0; i <= int(Ball::black); ++i) {
        const ShotInput in{true, Ball(i)};
        const json j = input_json(in);
        CHECK(j == json{{"nominated", std::string(ofs::table::ball_name(Ball(i)))}});
        ShotInput back;
        std::string error;
        REQUIRE_MESSAGE(input_from_json(through_text(j), back, error), error);
        CHECK(back.has_nomination);
        CHECK(back.nominated == Ball(i));
    }
    CHECK(input_json(ShotInput{}) == json{{"nominated", ""}});
    ShotInput back{true, Ball::pink};
    std::string error;
    REQUIRE_MESSAGE(input_from_json(input_json(ShotInput{}), back, error), error);
    CHECK_FALSE(back.has_nomination);
}

TEST_CASE("facts round-trip exactly") {
    ShotFacts f;
    f.first_hit = {2, 5};
    f.first_hit_t = 0.1 + 0.2;
    f.pots = {{2, 4, 1e-300}, {7, 0, 2.5000000000000004}};
    f.cue_potted = true;
    f.cue_pocket = 3;
    f.push = true;
    f.touching_moved = {1, 6};
    for (std::size_t i = 0; i < 8; ++i) {
        f.rest.push_back({awkward_at(i), awkward_at(i + 3)});
        f.closest_gap_m.push_back(awkward_at(i + 1));
    }
    f.cue_travel_m = 1.0 / 3.0;
    f.object_travel_m = -0.0;
    f.pot_attempt = true;
    f.anomaly = true;

    const json j = facts_json(f);
    ShotFacts back;
    std::string error;
    REQUIRE_MESSAGE(facts_from_json(through_text(j), back, error), error);
    CHECK(facts_json(back) == j);
    CHECK(back.first_hit == f.first_hit);
    CHECK(same_bits(back.first_hit_t, f.first_hit_t));
    REQUIRE(back.pots.size() == 2);
    CHECK(back.pots[0].ball == 2);
    CHECK(back.pots[0].pocket == 4);
    CHECK(same_bits(back.pots[0].t, 1e-300));
    CHECK(same_bits(back.pots[1].t, 2.5000000000000004));
    CHECK(back.cue_potted);
    CHECK(back.cue_pocket == 3);
    CHECK(back.push);
    CHECK(back.touching_moved == f.touching_moved);
    REQUIRE(back.rest.size() == 8);
    REQUIRE(back.closest_gap_m.size() == 8);
    for (std::size_t i = 0; i < 8; ++i) {
        CAPTURE(i);
        CHECK(same_bits(back.rest[i].x, f.rest[i].x));
        CHECK(same_bits(back.rest[i].y, f.rest[i].y));
        CHECK(same_bits(back.closest_gap_m[i], f.closest_gap_m[i]));
    }
    CHECK(same_bits(back.cue_travel_m, f.cue_travel_m));
    CHECK(same_bits(back.object_travel_m, -0.0));
    CHECK(back.pot_attempt);
    CHECK(back.anomaly);

    CHECK(j["pots"][0] == json{{"ball", 2}, {"pocket", 4}, {"t", 1e-300}});
    CHECK(j["rest"][1] == json::array({awkward_at(1), awkward_at(4)}));

    ShotFacts nothing;
    REQUIRE_MESSAGE(facts_from_json(through_text(facts_json(ShotFacts{})), nothing, error), error);
    CHECK(facts_json(nothing) == facts_json(ShotFacts{}));
}

TEST_CASE("a foul carries its kind, clause, value and ball, for every kind") {
    for (int i = 0; i <= int(FoulKind::before_nominating); ++i) {
        const Foul f{FoulKind(i), 4 + i % 4, i - 1};
        const json j = foul_json(f);
        CHECK(j.size() == 4);
        CHECK(j["kind"] == std::string(foul_name(f.kind)));
        CHECK(j["clause"] == std::string(foul_clause(f.kind)));
        CHECK(j["value"] == f.value);
        CHECK(j["ball"] == f.ball);
    }
}

TEST_CASE("calls keep their order, with every kind") {
    std::vector<Call> calls;
    for (int i = 0; i <= int(CallKind::frame_over); ++i) {
        calls.push_back({CallKind(i), i, "detail " + std::to_string(i)});
    }
    const json j = calls_json(calls);
    REQUIRE(j.size() == calls.size());
    for (std::size_t i = 0; i < calls.size(); ++i) {
        CHECK(j[i] == json{{"kind", std::string(call_name(calls[i].kind))},
                           {"value", calls[i].value},
                           {"detail", calls[i].detail}});
    }
    CHECK(calls_json({}) == json::array());
}

TEST_CASE("a judgement carries everything it decided") {
    Judgement j;
    j.post = busy_frame();
    j.fouls = {{FoulKind::pot_not_on, 7, 21}, {FoulKind::in_off, 4, -1}};
    j.penalty = 7;
    j.miss = true;
    j.miss_reason = "S3.14(a)";
    j.points = 3;
    j.nominated = {true, Ball::green};
    j.free_ball_used = true;
    j.spotted = {{Ball::black, 0, {1.4605000000000001, -0.0}}, {Ball::pink, 0, {0.1 + 0.2, 1e-300}}};
    j.calls = {{CallKind::foul, 7, "pot_not_on"}, {CallKind::miss, 0, "S3.14(a)"}};

    const json out = through_text(judgement_json(j));
    CHECK(out.size() == 10);
    CHECK(out["post"] == frame_json(j.post));
    CHECK(out["fouls"] == json::array({foul_json(j.fouls[0]), foul_json(j.fouls[1])}));
    CHECK(out["penalty"] == 7);
    CHECK(out["miss"] == true);
    CHECK(out["miss_reason"] == "S3.14(a)");
    CHECK(out["points"] == 3);
    CHECK(out["nominated"] == "green");
    CHECK(out["free_ball_used"] == true);
    CHECK(out["spotted"] == json::array({ball_json(j.spotted[0]), ball_json(j.spotted[1])}));
    CHECK(out["spotted"][0]["y"].dump() == "-0.0");
    CHECK(out["calls"] == calls_json(j.calls));

    CHECK(judgement_json(Judgement{})["nominated"] == "");
}

TEST_CASE("a decision round-trips with every kind and both players") {
    for (int i = 0; i <= int(DecisionKind::concede_match); ++i) {
        for (int player = 0; player < 2; ++player) {
            const Decision d{DecisionKind(i), player};
            const json j = decision_json(d);
            CHECK(j == json{{"kind", std::string(decision_name(d.kind))}, {"player", player}});
            Decision back;
            std::string error;
            REQUIRE_MESSAGE(decision_from_json(through_text(j), back, error), error);
            CHECK(back.kind == d.kind);
            CHECK(back.player == player);
        }
    }
}

TEST_CASE("a config round-trips") {
    for (const int best_of : {1, 3, 35}) {
        for (const int first_breaker : {-1, 0, 1}) {
            for (int flags = 0; flags < 4; ++flags) {
                MatchConfig c;
                c.best_of = best_of;
                c.first_breaker = first_breaker;
                c.seed = 0xDEADBEEFCAFEF00DULL;
                c.concede_any_time = {(flags & 1) != 0, (flags & 2) != 0};
                MatchConfig back;
                std::string error;
                REQUIRE_MESSAGE(config_from_json(through_text(config_json(c)), back, error), error);
                CHECK(back.best_of == best_of);
                CHECK(back.first_breaker == first_breaker);
                CHECK(back.seed == c.seed);
                CHECK(back.concede_any_time == c.concede_any_time);
            }
        }
    }
}

TEST_CASE("a match round-trips with its history, every end reason among it") {
    MatchState m;
    m.config.best_of = 35;
    m.config.first_breaker = 1;
    m.config.seed = 987654321987654321ULL;
    m.config.concede_any_time = {true, false};
    m.frames = {5, 3};
    m.frame_number = 9;
    m.breaker = 1;
    m.reracks = 2;
    m.frame = busy_frame();
    for (int i = 0; i <= int(EndReason::match_conceded); ++i) {
        FrameSummary s;
        s.number = i + 1;
        s.breaker = i % 2;
        s.winner = 1 - i % 2;
        s.reason = EndReason(i);
        s.scores = {40 + i, 70 - i};
        s.stats[0] = {20 + i, 9, 12, 1, 0, 4, 30};
        s.stats[1] = {21 + i, 11, 15, 2, 1, 8, 45};
        s.strokes = 41 + 2 * i;
        s.reracks = i % 3;
        m.history.push_back(s);
    }
    m.over = true;
    m.winner = 0;

    const json j = match_json(m);
    MatchState back;
    std::string error;
    REQUIRE_MESSAGE(match_from_json(through_text(j), back, error), error);
    CHECK(match_json(back) == j);
    check_same(m.frame, back.frame);
    CHECK(back.config.seed == m.config.seed);
    CHECK(back.frames == m.frames);
    CHECK(back.frame_number == 9);
    CHECK(back.breaker == 1);
    CHECK(back.reracks == 2);
    CHECK(back.over);
    CHECK(back.winner == 0);
    REQUIRE(back.history.size() == m.history.size());
    for (std::size_t i = 0; i < m.history.size(); ++i) {
        CAPTURE(i);
        CHECK(back.history[i].number == m.history[i].number);
        CHECK(back.history[i].breaker == m.history[i].breaker);
        CHECK(back.history[i].winner == m.history[i].winner);
        CHECK(back.history[i].reason == m.history[i].reason);
        CHECK(back.history[i].scores == m.history[i].scores);
        CHECK(back.history[i].stats[1].shots == m.history[i].stats[1].shots);
        CHECK(back.history[i].strokes == m.history[i].strokes);
        CHECK(back.history[i].reracks == m.history[i].reracks);
        CHECK(summary_json(back.history[i]) == j["history"][i]);
        CHECK(j["history"][i]["reason"] == std::string(end_reason_name(m.history[i].reason)));
    }
}
