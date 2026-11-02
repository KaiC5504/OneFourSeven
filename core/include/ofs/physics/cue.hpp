#pragma once

// The cue strike (spec 7.3): an instantaneous tip impulse (Marlow's model as pooltool's
// stick_ball/instantaneous_point implements it) with cue_mass_kg and cue_tip_restitution, squirt
// from cue_end_mass_kg (pooltool stick_ball/squirt.py), swerve set up by the tilted spin axis
// that elevation and offset give, miscues beyond miscue_limit, and the cue obstruction.

#include "ofs/physics/params.hpp"
#include "ofs/physics/table_shape.hpp"
#include "ofs/physics/types.hpp"
#include "ofs/rng.hpp"

namespace ofs::phys {

struct StrikeOutcome {
    Vec3 v;  // z = 0: balls never leave the cloth (spec 7.9), the vertical impulse goes into it
    Vec3 w;
    double elevation_used_rad = 0.0;
    bool elevation_raised = false;  // the requested elevation was below the obstruction minimum
    double squirt_rad = 0.0;        // signed angle from the aim to the cue ball's direction
    bool miscue = false;
};

// The lowest elevation at which the cue, a tapered cylinder (tip and butt diameters, length,
// clearance) along aim_rad behind the cue ball, clears the cushion rail and every other ball.
// 0 when nothing is in the way. Never above max_elevation_rad.
double min_cue_elevation(const TableState& s, int cue_ball, double aim_rad, const TableShape& t,
                         const PhysicsParams& p);
// The same for the actual tip offsets: the minimum depends strongly on them (a cue ball against
// the cushion needs about 41 degrees struck at centre, about 20 with tip_y 0.4). strike() uses
// this one; the elevation control should too.
double min_cue_elevation(const TableState& s, int cue_ball, const CueStrike& c, const TableShape& t,
                         const PhysicsParams& p);

// The cue ball's velocity and spin just after the tip leaves it. The elevation used is
// max(requested, min_cue_elevation), capped at max_elevation_rad; the speed is capped at
// max_cue_speed_m_s. A tip offset beyond miscue_limit miscues: the ball leaves at no more than
// miscue_speed_factor of the full-hit speed, at an angle within miscue_max_angle_rad of the aim
// drawn from `rng`, with little spin. Nothing else draws from `rng`.
StrikeOutcome strike(const TableState& s, int cue_ball, const CueStrike& c, const TableShape& t,
                     const PhysicsParams& p, Rng& rng);

}  // namespace ofs::phys
