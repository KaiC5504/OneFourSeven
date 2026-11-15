#include "ofs/rules/types.hpp"

#include <array>
#include <utility>

namespace ofs::rules {

namespace {

template <typename E, std::size_t N>
std::string_view name_of(const std::array<std::pair<E, std::string_view>, N>& names, E value) {
    for (const auto& entry : names) {
        if (entry.first == value) return entry.second;
    }
    return "unknown";
}

template <typename E, std::size_t N>
bool parse_in(const std::array<std::pair<E, std::string_view>, N>& names, std::string_view name, E& out) {
    for (const auto& entry : names) {
        if (entry.second == name) {
            out = entry.first;
            return true;
        }
    }
    return false;
}

constexpr std::array<std::pair<OnKind, std::string_view>, 3> kOnKinds{{
    {OnKind::red, "red"},
    {OnKind::any_colour, "any_colour"},
    {OnKind::colour, "colour"},
}};

constexpr std::array<std::pair<Pending, std::string_view>, 4> kPendings{{
    {Pending::none, "none"},
    {Pending::after_foul, "after_foul"},
    {Pending::rerack, "rerack"},
    {Pending::toss, "toss"},
}};

constexpr std::array<std::pair<EndReason, std::string_view>, 8> kEndReasons{{
    {EndReason::none, "none"},
    {EndReason::black, "black"},
    {EndReason::respotted_black, "respotted_black"},
    {EndReason::claimed, "claimed"},
    {EndReason::awarded, "awarded"},
    {EndReason::conceded, "conceded"},
    {EndReason::three_misses, "three_misses"},
    {EndReason::match_conceded, "match_conceded"},
}};

constexpr std::array<std::pair<DecisionKind, std::string_view>, 10> kDecisions{{
    {DecisionKind::play, "play"},
    {DecisionKind::play_again, "play_again"},
    {DecisionKind::replace, "replace"},
    {DecisionKind::accept_rerack, "accept_rerack"},
    {DecisionKind::refuse_rerack, "refuse_rerack"},
    {DecisionKind::play_first, "play_first"},
    {DecisionKind::play_second, "play_second"},
    {DecisionKind::claim_frame, "claim_frame"},
    {DecisionKind::concede_frame, "concede_frame"},
    {DecisionKind::concede_match, "concede_match"},
}};

constexpr std::array<std::pair<FoulKind, std::string_view>, 9> kFouls{{
    {FoulKind::no_hit, "no_hit"},
    {FoulKind::in_off, "in_off"},
    {FoulKind::free_ball_snooker, "free_ball_snooker"},
    {FoulKind::pot_not_on, "pot_not_on"},
    {FoulKind::first_hit_not_on, "first_hit_not_on"},
    {FoulKind::push_stroke, "push_stroke"},
    {FoulKind::simultaneous_hit, "simultaneous_hit"},
    {FoulKind::reds_in_succession, "reds_in_succession"},
    {FoulKind::before_nominating, "before_nominating"},
}};

constexpr std::array<std::pair<FoulKind, std::string_view>, 9> kFoulClauses{{
    {FoulKind::no_hit, "S3.11(a)(vi)"},
    {FoulKind::in_off, "S3.11(a)(vii)"},
    {FoulKind::free_ball_snooker, "S3.11(a)(viii)"},
    {FoulKind::pot_not_on, "S3.11(b)(iii)"},
    {FoulKind::first_hit_not_on, "S3.11(b)(iv)"},
    {FoulKind::push_stroke, "S3.11(b)(v)"},
    {FoulKind::simultaneous_hit, "S3.11(c)"},
    {FoulKind::reds_in_succession, "S3.11(d)(iii)"},
    {FoulKind::before_nominating, "S3.11(d)(vi)"},
}};

constexpr std::array<std::pair<CallKind, std::string_view>, 9> kCalls{{
    {CallKind::foul, "foul"},
    {CallKind::miss, "miss"},
    {CallKind::free_ball, "free_ball"},
    {CallKind::touching_ball, "touching_ball"},
    {CallKind::warning, "warning"},
    {CallKind::respotted_black, "respotted_black"},
    {CallKind::rerack_offer, "rerack_offer"},
    {CallKind::rerack, "rerack"},
    {CallKind::frame_over, "frame_over"},
}};

}  // namespace

std::string_view on_kind_name(OnKind k) { return name_of(kOnKinds, k); }
std::string_view pending_name(Pending p) { return name_of(kPendings, p); }
std::string_view end_reason_name(EndReason r) { return name_of(kEndReasons, r); }
std::string_view decision_name(DecisionKind k) { return name_of(kDecisions, k); }
std::string_view foul_name(FoulKind k) { return name_of(kFouls, k); }
std::string_view call_name(CallKind k) { return name_of(kCalls, k); }
std::string_view foul_clause(FoulKind k) { return name_of(kFoulClauses, k); }

bool parse_on_kind(std::string_view name, OnKind& out) { return parse_in(kOnKinds, name, out); }
bool parse_pending(std::string_view name, Pending& out) { return parse_in(kPendings, name, out); }
bool parse_end_reason(std::string_view name, EndReason& out) { return parse_in(kEndReasons, name, out); }
bool parse_decision(std::string_view name, DecisionKind& out) { return parse_in(kDecisions, name, out); }
bool parse_foul(std::string_view name, FoulKind& out) { return parse_in(kFouls, name, out); }
bool parse_call(std::string_view name, CallKind& out) { return parse_in(kCalls, name, out); }

}  // namespace ofs::rules
