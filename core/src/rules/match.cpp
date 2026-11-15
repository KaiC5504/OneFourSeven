#include "ofs/rules/match.hpp"

#include "ofs/physics/rack.hpp"
#include "ofs/rng.hpp"
#include "ofs/rules/rules_log.hpp"

namespace ofs::rules {

namespace {

bool is_player(int p) { return p == 0 || p == 1; }

// Racks frame m.frame_number for m.breaker, with the seed its number and re-racks give.
void rack(MatchState& m, const phys::PhysicsParams& params) {
    const std::uint64_t seed = frame_seed(m.config, m.frame_number, m.reracks);
    m.frame = new_frame(m.breaker, phys::opening_rack(seed, params), seed);
    m.frame.concede_any_time = m.config.concede_any_time;
}

// Frame numbers only go up, so the last summary tells whether the frame in play has been counted.
bool summarised(const MatchState& m) { return !m.history.empty() && m.history.back().number == m.frame_number; }

}  // namespace

std::string_view match_event_name(MatchEvent e) {
    switch (e) {
        case MatchEvent::none: return "none";
        case MatchEvent::reracked: return "reracked";
        case MatchEvent::frame_ended: return "frame_ended";
        case MatchEvent::match_ended: return "match_ended";
    }
    return "unknown";
}

bool valid_config(const MatchConfig& c, std::string& error) {
    if (c.best_of < 1 || c.best_of > 35 || c.best_of % 2 == 0) {
        error = "best_of: " + std::to_string(c.best_of) + " is not an odd number from 1 to 35";
        return false;
    }
    if (c.first_breaker < -1 || c.first_breaker > 1) {
        error = "first_breaker: " + std::to_string(c.first_breaker) + " is not -1 (lots), 0 or 1";
        return false;
    }
    return true;
}

MatchState start_match(const MatchConfig& config, const phys::PhysicsParams& params) {
    MatchState m;
    m.config = config;
    m.frame_number = 1;
    m.breaker = is_player(config.first_breaker) ? config.first_breaker : int(Rng(config.seed).next_u64() & 1);
    rack(m, params);
    return m;
}

MatchEvent settle(MatchState& m, const phys::PhysicsParams& params) {
    if (m.over) return MatchEvent::none;
    if (m.frame.restart) {
        const std::array<bool, 2> concede_any_time = m.frame.concede_any_time;
        ++m.reracks;
        rack(m, params);
        m.frame.concede_any_time = concede_any_time;
        return MatchEvent::reracked;
    }
    if (!m.frame.over || summarised(m)) return MatchEvent::none;

    const FrameState& f = m.frame;
    FrameSummary s;
    s.number = m.frame_number;
    s.breaker = m.breaker;
    s.winner = f.winner;
    s.reason = f.end_reason;
    s.scores = f.scores;
    s.stats = f.stats;
    s.strokes = f.strokes;
    s.reracks = m.reracks;
    m.history.push_back(s);

    if (!is_player(f.winner)) {
        // The engine never ends a frame without a winner. The frame is recorded and counts for
        // nobody, so the match can still be played on.
        log_invalid("settle", "frame " + std::to_string(m.frame_number) + " is over with winner " +
                                  std::to_string(f.winner) + " (" + std::string(end_reason_name(f.end_reason)) + ")");
        return MatchEvent::frame_ended;
    }
    ++m.frames[std::size_t(f.winner)];
    if (f.end_reason == EndReason::match_conceded || 2 * m.frames[std::size_t(f.winner)] > m.config.best_of) {
        m.over = true;
        m.winner = f.winner;
        return MatchEvent::match_ended;
    }
    return MatchEvent::frame_ended;
}

bool match_decide(MatchState& m, Decision d, const TableOracle& oracle, const RefParams& ref,
                  const phys::PhysicsParams& params, std::vector<Call>& calls, MatchEvent& event, std::string& error) {
    event = MatchEvent::none;
    if (m.over) {
        error = "the match is over";
        return false;
    }
    if (!decide(m.frame, d, oracle, ref, calls, error)) return false;
    event = settle(m, params);
    return true;
}

bool next_frame(MatchState& m, const phys::PhysicsParams& params) {
    const char* why = nullptr;
    if (m.over) {
        why = "the match is over";
    } else if (!m.frame.over) {
        why = "the frame in play is not over";
    } else if (!summarised(m)) {
        why = "the finished frame has not been settled";
    }
    if (why != nullptr) {
        log_invalid("next_frame", "frame " + std::to_string(m.frame_number) + ": " + why);
        return false;
    }
    ++m.frame_number;
    m.breaker = 1 - m.breaker;
    m.reracks = 0;
    rack(m, params);
    return true;
}

// splitmix64's finaliser over a mix of the three. It is a bijection, so two frames or re-racks of
// one match share a seed only if their mixes are equal, which no frame number and re-rack count a
// match reaches are.
std::uint64_t frame_seed(const MatchConfig& config, int number, int reracks) {
    std::uint64_t z = config.seed + 0x9E3779B97F4A7C15ULL * std::uint64_t(number) +
                      0xBF58476D1CE4E5B9ULL * std::uint64_t(reracks);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

}  // namespace ofs::rules
