#pragma once

// The referee's tunable thresholds (spec 9.1). core/data/referee_params.json holds one value per
// field under "values", keyed by the field name, and a unit, source and note per key under "docs",
// as table_params.json does. SI units; angles in radians.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ofs::rules {

struct RefParams {
    // Two balls closer than this are touching (S3.8), and a spot is occupied when a ball on it
    // would be this close to another (S2.18, S3.7(i)).
    double touching_gap_m = 5.0e-5;
    // Cue-ball contacts no further apart than this are one impact (S3.6).
    double simultaneous_hit_s = 1.0e-9;

    // Push stroke (S2.19(b)): the tip is taken to be on the cue ball for this long after it starts
    // to move, so a first contact sooner than that is a push, unless the cut is finer than
    // push_fine_edge_rad (the angle between the cue ball's line and the line of centres).
    double push_contact_time_s = 1.0e-3;
    double push_fine_edge_rad = 1.3962634015954636;  // 80 degrees

    // Miss (S3.14(a)): with no clear path to a ball on, a failed attempt that passed within this
    // of one is a foul only. 0 is the television standard, which the owner chose: every failed
    // escape is a miss.
    double miss_lenient_gap_m = 0.0;
    // S3.14(a)(ii): the ball on counts as impossible to hit when no route with at most this many
    // cushions reaches any part of it.
    int escape_max_cushions = 2;

    // A stroke was an attempt to pot when the first ball hit was potted or passed within this of a
    // pocket's drop circle (frame summary's pot success).
    double pot_attempt_radius_m = 0.2;

    // Stalemate (S3.17): a stroke is quiet when nothing is potted and the object balls end less
    // than stalemate_quiet_travel_m from where they began, summed over all of them. A re-rack is
    // offered after stalemate_offer_strokes quiet strokes in a row; after an objection the frame
    // is re-racked when stalemate_grace_strokes more are all quiet.
    double stalemate_quiet_travel_m = 0.6;
    int stalemate_offer_strokes = 12;
    int stalemate_grace_strokes = 6;

    // In-hand snookers (S2.17(a)) are tested from points of the D this far apart.
    double d_sample_step_m = 0.01;
};

// Every field of RefParams by name, in declaration order.
const std::vector<std::string>& ref_param_names();

// Reads the "values" object of referee_params.json. Unknown keys, missing keys, non-numbers,
// non-finite numbers, non-integral values for the integer fields and values outside each field's
// valid range are problems: each is logged as params.invalid (error) and appended to `problems`,
// and that field keeps its default. Returns true when there were none.
bool load_ref_params(const nlohmann::json& file, RefParams& out, std::vector<std::string>& problems);

// {"name": value, ...} for every field, as rules.decision logs them.
nlohmann::json ref_params_values(const RefParams& p);
// The inverse, strict: any problem makes it false and is appended to `problems`.
bool ref_params_from_values(const nlohmann::json& values, RefParams& out, std::vector<std::string>& problems);

}  // namespace ofs::rules
