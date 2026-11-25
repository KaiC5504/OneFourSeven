#pragma once

// One level of the computer opponent (spec 10.1, 10.3): how well it strikes and how well it
// chooses. Each level is a file in core/data (opponent_<id>.json) laid out as table_params.json
// is: "values" is what is loaded, "docs" gives each value's unit, source and note.
// docs/opponent/README.md says how the values are measured against spec 10.1.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ofs::opponent {

struct Level {
    // "beginner", "club", "amateur", "pro", "world", "maximum": the file is opponent_<id>.json
    std::string id;
    std::string name;  // as the game shows it: "Beginner", "The 147"
    int tier = 1;      // 1 to 5 on the ladder of spec 10.1; 0 for the 147, which is outside it

    // Execution (spec 10.3): standard deviations of what it adds to the stroke it meant. All 0
    // for the 147.
    double aim_sigma_rad = 0.0;      // the aim, on a short straight pot with nothing in the way
    double aim_sigma_per_cut = 0.0;  // the aim's sigma grows by this fraction per radian of cut
    double aim_sigma_per_m = 0.0;    // and by this fraction per metre the cue ball travels to its ball
    double awkward_factor = 1.0;     // and is multiplied by this when cueing is hampered (Pot::awkward)
    double speed_sigma = 0.0;        // the cue speed, as a fraction of it
    double tip_sigma = 0.0;          // each tip offset, as a fraction of the ball's radius

    // Choice (spec 10.2, 10.3).
    double max_cut_rad = 1.2;      // it does not consider a pot with a wider cut
    double max_tip = 0.0;          // the largest tip offset it plays on purpose, fraction of R; 0 is centre ball
    int speeds = 1;                // cue speeds it tries for a pot
    int spins = 1;                 // tip offsets it tries for a pot; the first is centre ball
    int depth = 0;                 // strokes it plans beyond this one, each simulated; 0 reads the leave only
    double position_weight = 0.0;  // how much the position left counts beside the pot, 0 to 1
    double judgement_sigma = 0.0;  // the error in its own reading of a pot's chance, as a fraction of it
    double safety_below = 0.0;     // it plays safe when its best pot's chance, as it reads it, is under this
    int max_sims = 200;            // simulations it may run for one stroke

    // Tactics and risk (spec 10.1's last column, 10.2 steps 4 and 5).
    // 0 the stand-in's plain safety; 1 basic safeties, centre ball, at the two nearest balls on;
    // 2 also with side, stun and screw, at every ball on it can reach; 3 also lays snookers and
    // escapes with swerve.
    int tactics = 0;
    int safety_tries = 0;      // safeties, snookers and escapes it weighs for one stroke
    int escape_cushions = 0;   // cushions it will go off to escape a snooker, 0 to 2
    double miss_weight = 0.0;  // how much of what a miss leaves the other player it counts, 0 to 1
    // Strokes with its own error it simulates to learn a pot's chance; 0 reads the margin.
    int noisy_sims = 0;
    // How far the score moves its risk: ahead it plays safer, needing snookers it lays them.
    double score_aware = 0.0;
    int concede_snookers = 0;  // it concedes a frame in which it needs this many snookers; 0 never
    bool maximum = false;          // it plays for a maximum, then for the highest break left (spec 10.1)
};

// The keys of "values", in the struct's order ("name" is a string, "maximum" a bool, the rest
// numbers).
const std::vector<std::string>& level_value_names();

// Reads a level file. `id` is taken from the file's "id". As load_ref_params: every problem (a
// missing or unknown key, a wrong type, a value outside its range) is logged as params.invalid and
// appended; true when there is none, and `out` is unchanged otherwise.
bool load_level(const nlohmann::json& file, Level& out, std::vector<std::string>& problems);

// {"id", "name", "tier", then every value}: what opponent.decision and rules.configured carry.
nlohmann::json level_json(const Level& level);

}  // namespace ofs::opponent
