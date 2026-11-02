# Calibration targets C1-C14

The independent calibration suite for spec 7.11, in `core/tests/calibration/` (the
`ofs_calibration_tests` executable, ctest label `calibration`). Every expected value is written
from a published formula or measurement, evaluated with the parameters in
`core/data/table_params.json` as loaded, so a tuned parameter moves the expectation wherever the
formula depends on it. Nothing is derived from the physics code under test.

Most targets are measured twice: through `simulate()` on an open table (no cushions, so the
measurement ends long before any cushion and the outline cannot interfere) and on the model
alone (`collide_balls`, `collide_line`, `strike`, `evolve`). When only the simulated one fails,
the fault is in the event loop or the record; when both fail, it is in the model.

R = 26.25 mm, e = `ball_restitution`, mu_s = `slide_friction`, mu_r = `roll_friction`,
g = `gravity_m_s2`, mu(v) = `ball_friction_a` + `ball_friction_b` exp(-`ball_friction_c` v).

| # | What is checked | Expected (formula or published numbers) | Tolerance and why | Source |
| --- | --- | --- | --- | --- |
| C1 | A ball sliding with no spin (and a level centre-ball `strike()`, which must give no spin) at v0 = 0.5, 1, 2, 4 m/s | Rolls after t = 2 v0 / (7 mu_s g), having slid 12 v0^2 / (49 mu_s g), at 5/7 v0; mid-slide x = v0 t - mu_s g t^2 / 2 | 1e-6 relative (1e-9 on the motion model alone): closed forms on both sides | Alciatore TP 4.1, https://billiards.colostate.edu/technical_proofs/TP_4-1.pdf |
| C2 | Stun full-ball: cue ball sliding with w = 0 at contact, 0.5-4 m/s | Cue ball (1-e)/2 v, object ball (1+e)/2 v; the cue ball then rolls at 5/7 (1-e)/2 v | 2e-3 v through `simulate()` (1e-7 m approach), 1e-6 v at the impulse | TP 3.1 with restitution e (Newton's law between equal masses, head-on, no tangential slip), https://billiards.colostate.edu/technical_proofs/TP_3-1.pdf; TP 4.1 |
| C3 | Rolling full-ball, 0.5-4 m/s | After re-gripping (5/7)(1-e)/2 v + (2/7) v: the topspin v/R is kept through the impulse and the slide conserves angular momentum about the cloth contact; e = 1 gives TP A.16's 2/7 | +2e-3 v above; below, up to 1.5 x (5/7) mu(v) (1+e)/2 v more, the topspin contact friction can remove (TP A.14's mu at the initial vertical slip; x1.5 because mu rises as the slip falls) | TP A.4 https://billiards.colostate.edu/technical_proofs/new/TP_A-4.pdf, TP A.16 https://billiards.colostate.edu/technical_proofs/new/TP_A-16.pdf, TP A.14 |
| C4 | Rolling cue ball at 3/4, 1/2 and 1/4 ball (cut 14.5, 30, 48.6 deg), 0.5-2 m/s; also from the `cut_*` named positions, struck from 1 m | Final direction off the original line: tan theta = (1-k) sin cos / (k cos^2 + sin^2), k = (9 - 5e)/14 (derived as in TP A.4 with restitution e; e = 1 gives TP 3.3's sin cos / (sin^2 + 2/5): 27.6, 33.7, 27.3 deg); with e = 0.94: 25.6, 32.0, 26.3 deg | Between that and the same with TP A.14's throw friction applied to both balls, +- 0.75 deg for a friction model other than TP A.14's single impulse; and inside the 30-degree rule's band 24-36 deg | TP 3.3 https://billiards.colostate.edu/technical_proofs/TP_3-3.pdf, TP A.4, TP A.14 |
| C5 | Stun cut at 3/4, 1/2, 1/4 ball, 0.5-2 m/s | Cue ball leaves atan((1-e)/2 cot phi) off the tangent line (TP 3.1 with restitution e: 3.0 deg at a half-ball hit with e = 0.94); the stun path stays straight until it rolls | Between that and TP A.14's throw-trimmed value, +- 0.5 deg; the path bends no more than 0.2 deg | TP 3.1, TP A.4, TP A.14 |
| C6 | Cut-induced throw, half-ball stun hit, at TP A.14's slow 0.447 m/s (1 mph), medium 1.341, fast 3.129 m/s (7 mph) | Published measurements at a 30 deg cut (pool balls, phenolic): slow 4.5 (Alciatore) and 6.0 (Jewett), medium 4.1 and 4.1, fast 3.45 and 3.45 deg; slow throws more than fast | Within [min, max] of the two sets +- 0.75 deg (their scatter between 25 and 35 deg cuts, and human-judged speeds); slow exceeds fast by 0.25 deg | Alciatore, "Throw - Part II: results", BD Sept 2006, Diagram 4, https://billiards.colostate.edu/bd_articles/2006/sept06.pdf; Jewett, http://www.sfbilliards.com/Misc/throw.gif; fixture `core/tests/fixtures/reference/throw_published.json` |
| C7 | Rolling ball into a cushion at 0.5-4 m/s, incidence 0-80 deg from the normal, plus stun, draw and overspin at 1 m/s; on `collide_line` in 8 cushion placements (4 rotations x mirror) and through `simulate()` | Post-impact rebound angle and speed from an independent fine-step solution of the Mathavan model with the loaded cushion parameters (rolling at 1 m/s: 0.911 V at 0 deg, 0.847 V and 43.5 deg at 50 deg) | 1 deg and 2% (the M1 plan's); the oracle's own N vs 2N difference is 5e-7 in speed and 3e-5 deg | Mathavan, Jackson & Parkin (2010), Proc. IMechE C 224(9):1863-1873, https://doi.org/10.1243/09544062JMES1964, open copy https://hdl.handle.net/2134/15087; fixture `cushion_mathavan.json` from `tools/gen_reference.py` |
| C8 | Running and check side (R w_z / V = +-0.5, +-1) at 0.5-4 m/s, incidence 20, 40, 60 deg | The oracle's rebound angle and speed for each (at 1 m/s and 20 deg: check 5.2 deg, plain 13.4, running 28.4); at each speed and incidence the angle must grow from check through plain to running side; right-hand side (`tip_x` > 0) gives w_z > 0, running side for a ball heading up the table into the left cushion | As C7 for the magnitude; the ordering strictly | Mathavan (2010) Fig. 8 and sec. 4 |
| C9 | `strike()` at `tip_x` = +-(miscue_limit - 0.001), level cue, three aims, 1-6 m/s; and at tip offsets 0.1-0.5 R | Squirt 1-3 deg, right-hand side to the left (positive angle); `squirt_rad` agrees with the velocity's angle; at each offset tan(squirt) = (5/2) b sqrt(1 - b^2) / (1 + m_ball/m_end + (5/2)(1 - b^2)) with the loaded `cue_end_mass_kg` | The range itself; 1e-9 rad for `squirt_rad`; 0.1 deg against the formula | Spec 7.3; Alciatore TP A.31 (published examples: 3.5 deg for a high-squirt cue, 1.4 deg for a low-squirt one at 50% side), https://billiards.colostate.edu/technical_proofs/new/TP_A-31.pdf; squirt FAQ https://billiards.colostate.edu/faq/squirt/ |
| C10 | Cue elevated 20, 30, 40 deg, `tip_x` +-0.4, 2 and 4 m/s | Starts squirted away from the side; while sliding the slip direction stays fixed, so the path is the parabola r0 + v0 t - mu_s g u_hat t^2 / 2 and the heading turns towards the side at every sample; rolls at t = 2 abs(u0) / (7 mu_s g); once rolling it runs straight (checked above `nap_speed_max_m_s`, where the nap is off) | 1e-9 m on the parabola, 1e-6 relative on the time, 1e-9 rad on the rolling heading | TP A.4 (curved post-impact path under sliding friction) and TP 4.1 |
| C11 | The lag of `positions/lag.json`: by bisection, the cue-ball speed (centre-ball, sliding, no spin) that takes the ball from the baulk line to the top cushion and back to the baulk cushion, on the WPBSA box; and the last length's time T | Speed within TP B.6's lag model on this table (slide to 5/7 per TP 4.1, roll at mu_r g, rebound at 0.818 as stun, skid to 5/7, roll back to rest at the cushion) for effective slopes 0.0055 (fast carom cloth: 10 s on a 10-foot table) to 0.0105 (competition pool cloth: 7 s on a 9-foot table): about 1.63 to 2.20 m/s; effective slope 2 d / (g T^2) in the same 0.0055-0.0105 | Speed range widened by 5% for TP B.6's pure-stun rebound (the Mathavan model leaves some spin, a few percent on the post-skid speed); slope range as published | Jewett's lag timing in Alciatore's cloth-speed FAQ, https://billiards.colostate.edu/faq/table/cloth-speed/; Waldron, https://billiards.colostate.edu/instructional-articles/pb-review/stimpmeter/; TP B.6, https://billiards.colostate.edu/technical_proofs/new/TP_B-6.pdf; Mathavan (2010) Fig. 5 (0.818); typical speeds https://billiards.colostate.edu/faq/speed/typical/; fixture `table_speed_published.json` |
| C12 | 2,000 random shots (seed 0x5eed0c12): scattered balls, frozen clusters, balls against cushions, break-offs, balls set moving with any spin | Total kinetic energy (1/2 m v^2 + 1/5 m R^2 w^2, written in the test) sampled 1e-10 s before and after every event and at t_end never rises; no event_guard, zero_time_chain, time_cap or energy_gain anomaly; t_end <= `time_cap_s`; every ball ends stationary with v = w = 0 exactly, or potted | 1e-9 relative + 1e-12 J (rounding) | Spec 7.11; simulator.hpp guards |
| C13 | A break-off and 200 random shots (seed 0x5eed0c13), each simulated twice | `record_json` dumps byte-identical; a struck shot has events | Exact | Spec 7.11, 16.10 |
| C14 | 5,000 random shots (seed 0x5eed0c14) as C12, at least 500 of them break-offs, on the real outline | No exception, no NaN or infinity in any segment or event time, no nan or guard anomaly; at every event and at the end no two balls closer than 2R - 1e-9 and no ball centre within R - 1e-6 of a cushion line (inside its extent) or a jaw arc, nor past the cushion noses more than 0.25 m from a pocket, unless potted | As stated: 1e-9 m between balls, 1e-6 m to cushions | Spec 7.11, 16.1 (sim_cli's fuzz runs the full 100,000 in the gate; this is the independent version) |

A sanity test checks the shot generator's own starts (no overlaps, nothing inside a cushion) and
that `opening_rack()` leaves every ball clear of every other, so a C12-C14 failure is the
simulator's, not a bad start.

## Fixtures

- `core/tests/fixtures/reference/cushion_mathavan.json`: generated, do not edit. `uv run
  tools/gen_reference.py` writes it; `--check` says whether it is stale. It stores the cushion
  parameters it was made with; the C7/C8 tests fail with "regenerate" when `table_params.json`
  differs. The oracle is ported from the paper's equations (3)-(17), not from core/ or pooltool,
  Euler steps of the normal impulse with 200,000 steps, compression split exactly at zero normal
  velocity, restitution by Stronge's energetic coefficient, and the contracts' rule
  e = cushion_restitution - slope x max(0, v_n - cushion_rigid_limit_m_s). Checked once against
  pooltool's mathavan_2010 solver (main branch, run without numba): 144 cases over 0.5-4 m/s,
  0-80 deg, topspin -1 to 1, side -1 to 1 agree to 1.4e-5 in speed and 0.0008 deg.
- `throw_published.json` and `table_speed_published.json`: published numbers with their URLs,
  read by hand from the articles and charts.

## Findings while building the suite

- **C6 fast (open):** the friction fit in `table_params.json` (TP A.14's a, b, c from Marlow's
  data) predicts 1.70 deg of throw at a half-ball hit at 7 mph, against 3.45 deg measured by both
  Alciatore and Jewett. Slow (4.86 deg) and medium are within the measurements. The tuner can
  refit the friction to the measured throw (spec 7.4 allows a re-fit).
- **C7 data:** Mathavan et al. report that a rolling snooker ball hitting a cushion straight on
  rebounds at 0.818 of its speed on average, and that e = 0.98, mu_w = 0.14 reproduce it. Their
  own equations, solved here and by pooltool, give 0.911 for a rolling ball (0.837 for a stunned
  one). The post-skid speed, 0.584 V, equals 5/7 x 0.818, so the 0.818 may be a stun-equivalent
  rebound. C7 checks the model; the 0.818 is used only in C11's lag estimate, where it gives the
  same post-skid speed.
- **C9:** with the original `cue_end_mass_kg` 0.0075, TP A.31 gives 3.05 deg at the miscue limit;
  the cue agent's 0.0047 gives 2.0 deg, inside the range.

No source was found for a measured snooker lag speed or snooker-ball throw; C11 and C6 use pool
measurements (the same phenolic balls, worsted vs napped cloth) and the spec's snooker cloth range.
