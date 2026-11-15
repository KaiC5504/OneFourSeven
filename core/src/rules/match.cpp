// Contract stub (M3): replaced by the match builder.

#include "ofs/rules/match.hpp"

#include "ofs/physics/rack.hpp"

namespace ofs::rules {

std::string_view match_event_name(MatchEvent e) {
    switch (e) {
        case MatchEvent::none: return "none";
        case MatchEvent::reracked: return "reracked";
        case MatchEvent::frame_ended: return "frame_ended";
        case MatchEvent::match_ended: return "match_ended";
    }
    return "unknown";
}

bool valid_config(const MatchConfig&, std::string&) { return true; }

MatchState start_match(const MatchConfig& config, const phys::PhysicsParams& params) {
    MatchState m;
    m.config = config;
    m.frame_number = 1;
    m.breaker = config.first_breaker == 1 ? 1 : 0;
    m.frame = new_frame(m.breaker, phys::opening_rack(config.seed, params), config.seed);
    return m;
}

MatchEvent settle(MatchState&, const phys::PhysicsParams&) { return MatchEvent::none; }

bool match_decide(MatchState&, Decision, const TableOracle&, const RefParams&, const phys::PhysicsParams&,
                  std::vector<Call>&, MatchEvent& event, std::string& error) {
    event = MatchEvent::none;
    error = "not implemented";
    return false;
}

bool next_frame(MatchState&, const phys::PhysicsParams&) { return false; }

std::uint64_t frame_seed(const MatchConfig& config, int, int) { return config.seed; }

}  // namespace ofs::rules
