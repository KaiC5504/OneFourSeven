#include <doctest/doctest.h>

#include <string>

#include "ofs/rules/types.hpp"

using namespace ofs::rules;

TEST_CASE("every rules enum's name parses back to itself") {
    for (int i = 0; i <= int(OnKind::colour); ++i) {
        OnKind out{};
        REQUIRE(parse_on_kind(on_kind_name(OnKind(i)), out));
        CHECK(out == OnKind(i));
    }
    for (int i = 0; i <= int(Pending::toss); ++i) {
        Pending out{};
        REQUIRE(parse_pending(pending_name(Pending(i)), out));
        CHECK(out == Pending(i));
    }
    for (int i = 0; i <= int(EndReason::match_conceded); ++i) {
        EndReason out{};
        REQUIRE(parse_end_reason(end_reason_name(EndReason(i)), out));
        CHECK(out == EndReason(i));
    }
    for (int i = 0; i <= int(DecisionKind::concede_match); ++i) {
        DecisionKind out{};
        REQUIRE(parse_decision(decision_name(DecisionKind(i)), out));
        CHECK(out == DecisionKind(i));
    }
    for (int i = 0; i <= int(FoulKind::before_nominating); ++i) {
        FoulKind out{};
        REQUIRE(parse_foul(foul_name(FoulKind(i)), out));
        CHECK(out == FoulKind(i));
        CHECK(std::string(foul_clause(FoulKind(i))).rfind("S3.11", 0) == 0);
    }
    for (int i = 0; i <= int(CallKind::frame_over); ++i) {
        CallKind out{};
        REQUIRE(parse_call(call_name(CallKind(i)), out));
        CHECK(out == CallKind(i));
    }
}

TEST_CASE("an unknown name parses as nothing") {
    OnKind on = OnKind::colour;
    CHECK_FALSE(parse_on_kind("blue", on));
    CHECK(on == OnKind::colour);
    FoulKind foul = FoulKind::in_off;
    CHECK_FALSE(parse_foul("", foul));
    CHECK_FALSE(parse_foul("unknown", foul));
}
