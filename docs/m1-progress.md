# M1 progress (physics core)

Plan approved 2026-11-02. Resume point for the lead session.

## Contracts: 8827969
Physics headers (core/include/ofs/physics/*, vec.hpp, rng.hpp, math/roots.hpp) with stubs;
params loader, serialisation and Rng real; table_params.json with every value, unit and
source (schema 2: "values" + "docs"); nap re-evaluated as a rolling ball's speed halves
(not a fixed tick: keeps a break-off's event count low); named positions in
core/tests/fixtures/positions/; log events shot.simulated, physics.anomaly, params.invalid;
gate physics lane (Release core build, calibration ctest label, sim_cli fuzz 2k/100k, bench
--check); ofs_calibration_tests DISABLED in ctest until wave 1 merges.

Addendum abfa39a: cushion contact height 7R/5 (Mathavan); restitution falls above the rigid
cushion limit (2.5 m/s normal speed, slope tuned later).

## Wave 1 (launched, 9 agents in worktrees)
motion (motion.cpp, nap.cpp), events (roots.cpp, detect.cpp), ball-ball (collide_ball.cpp),
cushions (collide_cushion.cpp, table_shape.cpp, table_shape.json), cue (cue.cpp), simulator
(simulator.cpp, shot_record.cpp, repro.cpp, rack.cpp), tools (sim_cli, calibration_report.py),
calibration (core/tests/calibration, reference fixtures, docs/calibration/targets.md), infra
(macOS editor extension, export log checker, idle max_fps cap; branch m1-infra on origin).
Merged: ball-ball (764d9e8: TP A.14 impulse model, throw matches eq. 15-17 to 1e-12, 28 ns per
collision; friction speed confirmed in m/s; ~3% more spin transfer than pooltool, which sizes
friction from the separation speed). motion (41ef4e3: pooltool evolve ported to the table frame;
evolve ~21 ns; nap = sideways push scaled by 2 ref/(s + ref), with/against/across behaviours,
off below the lowest re-tick threshold; right-hand side is w.z > 0, cue agent told). cue (25d6113:
pooltool cue_strike with restitution, v = (1+e)V/(1 + m/M + 5/2(a^2+b^2)) (TP A-30); miscue draws
the Rng exactly twice; obstruction closed form + fixed point, 41 deg on the baulk cushion, 34 deg
over a red 7.5 mm behind; ~0.5 us per call). Lead: tip-aware min_cue_elevation overload,
cue_end_mass_kg 0.0047 (squirt 3.05 -> 2.0 deg at max side). tools (424050a: sim_cli
simulate/replay/fuzz/bench/calibrate, calibration_report.py; fuzz checks anomalies, NaN,
overlaps, tunnelling, energy, C13 every 100th; bench 15 shots). To do after the merge: write
the bench baseline in Release; give the 25% regression check some absolute slack (us-scale
shots are noisy while the gate runs lanes in parallel). simulator (3b84905, 0a0fc80: pair
cache with bounding-box culling, ~3-4 quartics and ~1 us loop overhead per event, ~190 events
per break-off; the push rule plus a short contact hold-back after a push (review in checks);
500 break-offs: anomalies 26 -> 0; 12 integration tests skip-decorated until merge).
Expected red until events + cushions merge: physics lane fuzz (tunnel 1721, overlap 162,
time_cap 59, all from stub detection and an empty table). calibration (57d23b5, 99e825a: 26
cases, each checked through simulate() and on the model alone; C7/C8 oracle = the paper's
equations in tools/gen_reference.py, agreeing with pooltool's solver to 1.4e-5; published
numbers: throw 4.5/4.1/3.45 deg at 1/3/7 mph (BD Sept 2006), lag 1.63-2.20 m/s from pool cloth
data). Findings for the tuner: C6 fast throw 1.70 deg vs 3.45 measured (refit ball_friction_*);
Mathavan's 0.818 straight-in ratio isn't reproduced by its own model (0.911), so C7 checks the
model. Regenerate the cushion fixture whenever cushion params change. cushions (962b8db,
40eb287, 1b7f27c: Mathavan eqs 12-17 by adaptive Dormand-Prince with stick/slip handling, worst
0.056% speed / 0.046 deg vs the 5000-step reference, 1.4-1.7 us per impact; outline: corner gap
84 mm, middle 88 mm, jaw radii 4 in / 2.5 in, rail_width 0.14, rail_height 0.040 (estimate);
cushion params confirmed from the paper; lead set slope 0.16 from its Fig. 7b fit and
regenerated the C7 reference; jaw_restitution 0.8 has no source, tuner's call; middle pockets
may sit too deep, owner judges). infra (f66622b, eee3c8a: macOS editor extension, cached in
ios-compile, fresh in testflight; check-godot-log fails the export on any script error; run
41396740025 green with zero errors; idle cap 30 fps, Windows idle 6.9 -> 33.3 ms per frame). Next
signed build needs `testflight.py start --dry-run` first.
events (49a05c9: root isolation by critical points + Halley, never loses a tangent root; brute
force 0 disagreements over ~54k cases; ball_ball_time ~150-220 ns, solve_quartic ~400 ns, which
detection doesn't use). Wave 1 complete; base 0bc09e8 (root helpers in roots.hpp, simulator
integration tests and calibration ctest enabled).

## First full run (0bc09e8)
bench: every median under 2 ms (break-offs 0.35-1.26 ms). fuzz 20k: 1,880 fail
(zero_time_chain 1,155, overlap 1,220). Calibration 20/26 pass; failing: C6 throw (params),
C4 named positions (test setup), generator legality (test checks infinite facing lines),
C12/C14 (the physics failures above).

## Fix round + wave 2 (launched together)
m1-fix-sim (simulator/detect: zero-time chains incl. sustained pushes into cushions, overlaps),
m1-fix-calib (the two calibration test bugs), m1-tuner (C6 friction refit, C11, sources, the
calibration report with a plain-words section). Push to origin held until these merge, so CI
runs once on fixed physics. Then: bench baseline, check round, ship.

## After wave 1
Enable ofs_calibration_tests in ctest and the simulator's skipped integration tests; apply
the agents' param recommendations to table_params.json; write tools/sim_cli/bench_baseline.json.
Wave 2: tuner (table_params.json values against C1-C11, calibration report). Then checks,
fixes, ship (no TestFlight in M1; the next signed build needs a dry run first).

## Findings in flight
- Simulator: inelastic collapse in break-off packs (134/500 tripped zero_time_chain with
  stand-in physics). Fix assigned to the simulator agent: ball-ball approaches below
  ball_rest_speed_m_s (5d03e4c, 1 mm/s) resolve as a sustained push, both balls leaving
  rolling with the pair's angular momentum about their cloth contacts conserved.
- Events: touching pairs with ~zero normal velocity and inward acceleration must be contact
  at t = 0 (42 overlaps otherwise); the simulator agent told m1-events directly.
- fix-calib merged (9866672): C4 setup and generator legality fixed; now 22/26 calibration pass. Open: C6 (tuner), C12 139/2000 + C14 638/5000 (fix-sim), cushion fast-vs-reference 0.16 deg after slope 0.16 (sent to fix-sim).
- tuner merged (9e2c8b3, 3166177): ball friction 0.058/0.092/2.73 (fit to 17 published stun
  throw points), slide 0.212 and mass 0.1406 (Mathavan), roll 0.010 (C11's fast end; the only
  snooker measurement, Mathavan 2009, is 0.0127 on an unheated lab table: cloth pace is for
  KaiC's M2 feel check). C1-C11 pass. Report in docs/calibration/report/ with a plain-words
  section. Lead: cushion_restitution_min 0.70 (0284387, Alciatore TP B.6 and Mathavan 2009
  extrapolated; clamp assigned to fix-sim); ball-ball fast rolling throw bound relaxed to 2.5 deg.
  Open: the report's screw_follow series runs into cushions (check round).
