#pragma once

// Tables laid out for the 147 to play a maximum from (spec 10.1): a break cannot start from the
// break-off, so the exhibition starts with the reds already open. core/data/exhibitions.json:
// {"schema_version": 1, "layouts": [{"name": "open 1", "cue_ball": 0, "balls": [ball, ...]}]},
// each ball in the rules' form (serialize.hpp), the cue ball listed, all fifteen reds and the six
// colours on the table.

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/rules/types.hpp"

namespace ofs::opponent {

struct Exhibition {
    std::string name;
    std::vector<rules::BallOnTable> balls;
    int cue_ball = 0;
};

// Every problem (a malformed layout, a ball missing or listed twice, two balls overlapping) is
// appended with the layout's name; true when there is none. `out` keeps the layouts that were
// sound either way.
bool load_exhibitions(const nlohmann::json& file, std::vector<Exhibition>& out, std::vector<std::string>& problems);

// A frame in play from the layout: `striker` at the table with red on, nothing pending, scores
// nil, the cue ball not in hand.
rules::FrameState exhibition_frame(const Exhibition& layout, int striker, std::uint64_t seed);

}  // namespace ofs::opponent
