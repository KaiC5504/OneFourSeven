#pragma once

// Finding the stroke that does what a candidate wants on the real physics (spec 10.2 step 2): the
// simulator is run noise-free and its result read back, so throw, the cue ball's deflection with
// side, the nap and the jaws are all allowed for without modelling any of them here.

#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "ofs/opponent/env.hpp"
#include "ofs/opponent/pots.hpp"
#include "ofs/physics/shot_record.hpp"
#include "ofs/physics/types.hpp"
#include "ofs/rules/types.hpp"

namespace ofs::opponent {

// How a pot is struck, the aim aside.
struct Spec {
    double speed_m_s = 0.0;
    double tip_x = 0.0;
    double tip_y = 0.0;
    double elevation_rad = 0.0;
};

// What one simulated stroke did.
struct Trial {
    phys::CueStrike strike;
    bool clean = false;        // no simulator guard stopped it
    int first_hit = -1;        // place of the first ball the cue ball met; -1 none
    std::vector<int> potted;   // places of the object balls potted, in the order they dropped
    bool cue_potted = false;
    // Where every ball stopped, by place; a potted ball's entry is where it dropped.
    std::vector<rules::Vec2> rest;
    int cue_cushions = 0;      // cushions the cue ball met after its first ball
    bool cannon = false;       // the cue ball met a second ball
    double t_end = 0.0;
    double sim_us = 0.0;
};

// Simulates `strike` from `p` with the cue ball at `cue` (which replaces its listed position: a
// place in the D when it is in hand). `record`, when given, receives the shot record, for
// rules::judge_shot. Never throws; a stroke the simulator refuses gives clean false, and is
// logged as app.error (counted instead while a TrialFailures lives on the thread).
Trial try_strike(const rules::Position& p, rules::Vec2 cue, const phys::CueStrike& strike, const Env& env,
                 phys::ShotRecord* record = nullptr);

struct SolvedPot {
    // An aim was found with which the cue ball meets pot.target first, the target drops in
    // pot.pocket and the cue ball stays on the table.
    bool found = false;
    Trial trial;  // of that aim; of the nearest miss when not found
    // The shot record of `trial` when found: what judging the stroke needs, without simulating
    // it again.
    std::shared_ptr<const phys::ShotRecord> record;
    // How far the aim can turn either way and the pot still go, the smaller side, in radians.
    // Measured only when asked for (it costs about a dozen simulations); else 0.
    double window_rad = 0.0;
    int sims = 0;
};

// The aim for `pot` struck with `spec`, starting from pot.aim_rad. With measure_window the aim
// returned is the middle of the window, the most forgiving one. Uses at most max_sims simulations.
SolvedPot solve_pot(const rules::Position& p, const Pot& pot, const Spec& spec, const Env& env, bool measure_window,
                    int max_sims);

// While one lives on a thread, the failures of try_strike and solve_pot made there (a stroke
// the simulator threw on, an input they refuse) are counted for one decision: the first is logged
// as app.error in full with `fields` added ({"shot_id", "level", "seed"}), and when it ends one
// closing app.error carries "more", how many followed it. Without one every failure is logged.
// Scopes nest; the innermost counts.
class TrialFailures {
public:
    explicit TrialFailures(nlohmann::json fields);
    ~TrialFailures();
    TrialFailures(const TrialFailures&) = delete;
    TrialFailures& operator=(const TrialFailures&) = delete;

    int count() const { return count_; }
    // Logs a failure, or counts it after the first.
    void note(nlohmann::json data);

private:
    nlohmann::json fields_;
    int count_ = 0;
    TrialFailures* outer_;
};

// The lowest cue speed, centre ball, that still carries pot.target into its pocket with
// kPotCarry_m to spare, from the cloth's rolling resistance and the cut; never below
// kLeastSpeed_m_s nor above the physics' maximum.
inline constexpr double kPotCarry_m = 0.3;
inline constexpr double kLeastSpeed_m_s = 0.3;
double least_pot_speed(const Pot& pot, const Env& env);

}  // namespace ofs::opponent
