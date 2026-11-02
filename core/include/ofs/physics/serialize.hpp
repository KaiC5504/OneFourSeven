#pragma once

// JSON forms shared by fixtures, repro bundles, sim_cli and (from M2) the game. Doubles are written
// at full precision, so a state round-trips exactly. JSON has no NaN or infinity, so non-finite
// doubles are written as the strings "nan", "inf" and "-inf"; a NaN reads back as a quiet NaN,
// without its sign or payload.
//
//   ball:   {"ball": "red", "index": 3, "r": [x, y], "v": [x, y], "w": [x, y, z],
//            "motion": "rolling", "pocket": -1}   ("v", "w", "motion", "pocket" optional on input:
//            zero, zero, classified later, -1)
//   table:  {"balls": [ball, ...]}
//   strike: {"aim_rad", "tip_x", "tip_y", "elevation_rad", "speed_m_s"}

#include <string>

#include <nlohmann/json.hpp>

#include "ofs/physics/types.hpp"

namespace ofs::phys {

nlohmann::json to_json(const BallState& b);
nlohmann::json to_json(const TableState& s);
nlohmann::json to_json(const CueStrike& c);
// A double as JSON, with NaN and the infinities as "nan", "inf", "-inf" (JSON has neither).
nlohmann::json number_to_json(double v);

// False with a reason on anything malformed: unknown ball names, wrong array lengths, a value that
// is neither a finite number nor one of those three strings, an out-of-range red index.
bool from_json(const nlohmann::json& j, BallState& out, std::string& error);
bool from_json(const nlohmann::json& j, TableState& out, std::string& error);
bool from_json(const nlohmann::json& j, CueStrike& out, std::string& error);

}  // namespace ofs::phys
