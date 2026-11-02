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
cue_end_mass_kg 0.0047 (squirt 3.05 -> 2.0 deg at max side).

## After wave 1
Enable ofs_calibration_tests in ctest and the simulator's skipped integration tests; apply
the agents' param recommendations to table_params.json; write tools/sim_cli/bench_baseline.json.
Wave 2: tuner (table_params.json values against C1-C11, calibration report). Then checks,
fixes, ship (no TestFlight in M1; the next signed build needs a dry run first).
