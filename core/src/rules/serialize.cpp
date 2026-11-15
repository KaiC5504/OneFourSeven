// Contract stub (M3): replaced by the match builder.

#include "ofs/rules/serialize.hpp"

namespace ofs::rules {

namespace {

bool not_implemented(std::string& error) {
    error = "not implemented";
    return false;
}

}  // namespace

nlohmann::json ball_json(const BallOnTable&) { return nlohmann::json::object(); }
nlohmann::json position_json(const Position&) { return nlohmann::json::object(); }
nlohmann::json frame_json(const FrameState&) { return nlohmann::json::object(); }
nlohmann::json input_json(const ShotInput&) { return nlohmann::json::object(); }
nlohmann::json facts_json(const ShotFacts&) { return nlohmann::json::object(); }
nlohmann::json foul_json(const Foul&) { return nlohmann::json::object(); }
nlohmann::json calls_json(const std::vector<Call>&) { return nlohmann::json::array(); }
nlohmann::json judgement_json(const Judgement&) { return nlohmann::json::object(); }
nlohmann::json decision_json(const Decision&) { return nlohmann::json::object(); }
nlohmann::json config_json(const MatchConfig&) { return nlohmann::json::object(); }
nlohmann::json summary_json(const FrameSummary&) { return nlohmann::json::object(); }
nlohmann::json match_json(const MatchState&) { return nlohmann::json::object(); }
nlohmann::json frame_view_json(const FrameState&) { return nlohmann::json::object(); }

bool position_from_json(const nlohmann::json&, Position&, std::string& error) { return not_implemented(error); }
bool frame_from_json(const nlohmann::json&, FrameState&, std::string& error) { return not_implemented(error); }
bool input_from_json(const nlohmann::json&, ShotInput&, std::string& error) { return not_implemented(error); }
bool facts_from_json(const nlohmann::json&, ShotFacts&, std::string& error) { return not_implemented(error); }
bool decision_from_json(const nlohmann::json&, Decision&, std::string& error) { return not_implemented(error); }
bool config_from_json(const nlohmann::json&, MatchConfig&, std::string& error) { return not_implemented(error); }
bool match_from_json(const nlohmann::json&, MatchState&, std::string& error) { return not_implemented(error); }

std::string frame_hash(const FrameState&) { return ""; }

}  // namespace ofs::rules
