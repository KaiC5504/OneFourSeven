// Contract stub (M3): replaced by the match builder.

#include "ofs/rules/rules_log.hpp"

namespace ofs::rules {

void log_decision(const FrameState&, const ShotInput&, const ShotFacts&, const Judgement&, const RefParams&,
                  const nlohmann::json&) {}
void log_choice(const FrameState&, Decision, bool, const std::string&, const FrameState&, const std::vector<Call>&,
                const nlohmann::json&) {}
void log_cue_placed(const FrameState&, Vec2, bool, const std::string&, const nlohmann::json&) {}
void log_match_started(const MatchState&, const nlohmann::json&) {}
void log_frame_started(const MatchState&, const nlohmann::json&) {}
void log_frame_ended(const MatchState&, const nlohmann::json&) {}
void log_match_ended(const MatchState&, const std::string&, const nlohmann::json&) {}
void log_invalid(const std::string&, const std::string&) {}

std::string rejudge(const nlohmann::json&, const phys::TableShape&) { return "not implemented"; }

}  // namespace ofs::rules
