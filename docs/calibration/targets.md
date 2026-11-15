# Calibration targets C1-C14

The independent calibration suite for spec 7.11, in `core/tests/calibration/` (the
`ofs_calibration_tests` executable, ctest label `calibration`). Every expected value is written
from a published formula or measurement, evaluated with the parameters in
`core/data/table_params.json` as loaded, so a tuned parameter moves the expectation wherever the
formula depends on it. Nothing is derived from the physics code under test.

The owner plays and judges, so a value he sets by feel wins over the literature's. Where that
takes a target off its published number, the published number stays as the reference, the suite
holds the table to a band round what his table does, and the report shows both with the
difference. Nothing stands off its published number at present: the cloth and cushions of KaiC's
feel sessions of 2026-11-13 to 15 (`roll_friction` 0.015, `cushion_restitution` 0.85) went back to
the published 0.010 and 0.98 when a shot tracked from a broadcast showed them too slow (Selby's
blue, C8 and the notes below).

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
| C7 | Rolling ball into a cushion at 0.5-4 m/s, incidence 0-80 deg from the normal, plus stun, draw and overspin at 1 m/s; on `collide_line` in 8 cushion placements (4 rotations x mirror) and through `simulate()` | Post-impact rebound angle and speed from an independent fine-step solution of the Mathavan model with the loaded cushion parameters. With `cushion_restitution` 0.98 and `cushion_side_grip` 4, rolling at 1 m/s: 0.911 V at 0 deg, 0.765 V and 36.9 deg at 50 deg. The literature reference, the paper's one cushion friction: the same at 0 deg, 0.847 V and 43.5 deg at 50 deg; the fixture's `literature` cases, which the suite checks with the paper's value | 1 deg and 2% (the M1 plan's); the oracle's own N vs 2N difference is 3e-6 in speed and 8e-5 deg | Mathavan, Jackson & Parkin (2010), Proc. IMechE C 224(9):1863-1873, https://doi.org/10.1243/09544062JMES1964, open copy https://hdl.handle.net/2134/15087; fixture `cushion_mathavan.json` from `tools/gen_reference.py` |
| C8 | Running and check side (R w_z / V = +-0.5, +-1) at 0.5-4 m/s, incidence 20, 40, 60 deg; heavy side (+-1.75, +-3) at 0.5 and 1.5 m/s, incidence 0, 10, 30 deg; through `simulate()`, a third, two thirds and all of the side struck straight across the table, and Selby's blue (`positions/selby_blue.json`) | The oracle's rebound angle and speed for each (at 1 m/s and 20 deg: check -3.6 deg, back the way it came, plain 13.4, running 28.5; with the paper's cushion: 5.2, 13.4, 28.4). Side straight in lands in step along the far cushion: the pool rule's 1, 2 and 3 diamonds for a third, two thirds and all of it, times 0.8 for a cushion that returns 0.91 of the speed across where pool's return 0.7 to 0.75 (0.94, 1.71 and 2.38 here; the paper's cushion gives 0.96, 1.5 and 1.56). Selby's blue as the broadcast shows it, timed from the contact with the blue: the right side cushion at x = -1.31 after 1.17 s, the baulk cushion at y = -0.585 after 1.60 s, the left side cushion at x = -0.37 after 3.84 s, the reds on the pink after 6.85 s (ours: -1.23 at 1.15 s, -0.61 at 1.60 s, -0.37 at 3.79 s, a red at 7.4 s; on the cloth and cushions of the owner's feel sessions the cue ball stops 0.3 m short); at each speed and incidence the angle must grow from check through plain to running side; right-hand side (`tip_x` > 0) gives w_z > 0, running side for a ball heading up the table into the left cushion | As C7 for the magnitude; the ordering strictly; 0.3 of a diamond; Selby's blue within 0.10 m at each cushion, 0.25 s at the first two, 0.4 s at the third and 1 s at the reds | Mathavan (2010) Fig. 8 and sec. 4; Patrick Johnson's rule on Dr. Dave Alciatore's bank and kick effects page, https://drdavepoolinfo.com/faq/bank-kick/effects/ ("1/3, 2/3 and 3/3 of maximum spin result in approximately 1, 2 or 3 diamonds of cross-table change with a kick straight into a cushion"); the broadcast of the 2014 World Championship final's last frame |
| C9 | `strike()` at `tip_x` = +-(miscue_limit - 0.001), level cue, three aims, 1-6 m/s; and at tip offsets 0.1-0.5 R | Squirt 1-3 deg, right-hand side to the left (positive angle); `squirt_rad` agrees with the velocity's angle; at each offset tan(squirt) = (5/2) b sqrt(1 - b^2) / (1 + m_ball/m_end + (5/2)(1 - b^2)) with the loaded `cue_end_mass_kg` | The range itself; 1e-9 rad for `squirt_rad`; 0.1 deg against the formula | Spec 7.3; Alciatore TP A.31 (published examples: 3.5 deg for a high-squirt cue, 1.4 deg for a low-squirt one at 50% side), https://billiards.colostate.edu/technical_proofs/new/TP_A-31.pdf; squirt FAQ https://billiards.colostate.edu/faq/squirt/ |
| C10 | Cue elevated 20, 30, 40 deg, `tip_x` +-0.4, 2 and 4 m/s | Starts squirted away from the side; while sliding the slip direction stays fixed, so the path is the parabola r0 + v0 t - mu_s g u_hat t^2 / 2 and the heading turns towards the side at every sample; rolls at t = 2 abs(u0) / (7 mu_s g); once rolling it runs straight (checked above `nap_speed_max_m_s`, where the nap is off) | 1e-9 m on the parabola, 1e-6 relative on the time, 1e-9 rad on the rolling heading | TP A.4 (curved post-impact path under sliding friction) and TP 4.1 |
| C11 | The lag of `positions/lag.json`: by bisection, the cue-ball speed (centre-ball, sliding, no spin) that takes the ball from the baulk line to the top cushion and back to the baulk cushion, on the WPBSA box; and the last length's time T | Speed within TP B.6's lag model on this table (slide to 5/7 per TP 4.1, roll at mu_r g, rebound at 0.818 as stun, skid to 5/7, roll back to rest at the cushion) for effective slopes 0.0055 (fast carom cloth: 10 s on a 10-foot table) to 0.0105 (competition pool cloth: 7 s on a 9-foot table): about 1.63 to 2.20 m/s; effective slope 2 d / (g T^2) in the same 0.0055-0.0105. That is the literature reference, and the table meets it (2.14 m/s, the last length in 8.4 s, slope 0.0101). A table the owner tunes off it is held to an `owner_tuned` block in the fixture instead, with the published range printed beside it (none at present) | Published speed range widened by 5% for TP B.6's pure-stun rebound (the Mathavan model leaves some spin, a few percent on the post-skid speed); slope range as published. The owner's values within 5% | Jewett's lag timing in Alciatore's cloth-speed FAQ, https://billiards.colostate.edu/faq/table/cloth-speed/; Waldron, https://billiards.colostate.edu/instructional-articles/pb-review/stimpmeter/; TP B.6, https://billiards.colostate.edu/technical_proofs/new/TP_B-6.pdf; Mathavan (2010) Fig. 5 (0.818); typical speeds https://billiards.colostate.edu/faq/speed/typical/; fixture `table_speed_published.json` |
| C12 | 2,000 random shots (seed 0x5eed0c12): scattered balls, frozen clusters, balls against cushions, break-offs, balls set moving with any spin | Total kinetic energy (1/2 m v^2 + 1/5 m R^2 w^2, written in the test) sampled 1e-10 s before and after every event and at t_end never rises; no event_guard, zero_time_chain, time_cap or energy_gain anomaly; t_end <= `time_cap_s`; every ball ends stationary with v = w = 0 exactly, or potted | 1e-9 relative + 1e-12 J (rounding) | Spec 7.11; simulator.hpp guards |
| C13 | A break-off and 200 random shots (seed 0x5eed0c13), each simulated twice | `record_json` dumps byte-identical; a struck shot has events | Exact | Spec 7.11, 16.10 |
| C14 | 5,000 random shots (seed 0x5eed0c14) as C12, at least 500 of them break-offs, on the real outline | No exception, no NaN or infinity in any segment or event time, no nan or guard anomaly; at every event and at the end no two balls closer than 2R - 1e-9 and no ball centre within R - 1e-6 of a cushion line (inside its extent) or a jaw arc, nor past the cushion noses more than 0.25 m from a pocket, unless potted | As stated: 1e-9 m between balls, 1e-6 m to cushions | Spec 7.11, 16.1 (sim_cli's fuzz runs the full 100,000 in the gate; this is the independent version) |

A sanity test checks the shot generator's own starts (no overlaps, nothing inside a cushion) and
that `opening_rack()` leaves every ball clear of every other, so a C12-C14 failure is the
simulator's, not a bad start.

The calibration report (`docs/calibration/report/`, written by `tools/calibration_report.py`)
measures C1-C11 again through `ofs_sim_cli` with the same targets and bands, for the record and
for the owner.

## Fixtures

- `core/tests/fixtures/reference/cushion_mathavan.json`: generated, do not edit. `uv run
  tools/gen_reference.py` writes it; `--check` says whether it is stale. It stores the cushion
  parameters it was made with; the C7/C8 tests fail with "regenerate" when `table_params.json`
  differs. The oracle is ported from the paper's equations (3)-(17), not from core/ or pooltool,
  Euler steps of the normal impulse with 200,000 steps, compression split exactly at zero normal
  velocity, restitution by Stronge's energetic coefficient, and the contracts' rule
  e = cushion_restitution - slope x max(0, v_n - cushion_rigid_limit_m_s), never below
  cushion_restitution_min (the floor is inside the 4 m/s grid once cushion_restitution is under
  0.94). It also holds the rolling cases and the 1 m/s side cases solved with the paper's
  cushion_restitution 0.98 (`literature`), the reference when the table's own is tuned away
  from it. Checked once against
  pooltool's mathavan_2010 solver (main branch, run without numba): 144 cases over 0.5-4 m/s,
  0-80 deg, topspin -1 to 1, side -1 to 1 agree to 1.4e-5 in speed and 0.0008 deg.
- `throw_published.json` and `table_speed_published.json`: published numbers with their URLs,
  read by hand from the articles and charts.

## Findings while building the suite

- **C6 fast (resolved):** the first friction fit (TP A.14's a, b, c from Marlow's data) predicted
  1.70 deg of throw at a half-ball hit at 7 mph, against 3.45 deg measured by both Alciatore and
  Jewett. The tuner refitted the friction to the measured throw (spec 7.4): 4.86 / 4.16 / 3.39 deg
  at 1 / 3 / 7 mph.
- **C7 data:** Mathavan et al. report that a rolling snooker ball hitting a cushion straight on
  rebounds at 0.818 of its speed on average, and that e = 0.98, mu_w = 0.14 reproduce it. Their
  own equations, solved here and by pooltool, give 0.911 for a rolling ball (0.837 for a stunned
  one). The post-skid speed, 0.584 V, equals 5/7 x 0.818, so the 0.818 may be a stun-equivalent
  rebound. C7 checks the model; the 0.818 is used only in C11's lag estimate, where it gives the
  same post-skid speed.
- **C9:** with the original `cue_end_mass_kg` 0.0075, TP A.31 gives 3.05 deg at the miscue limit;
  the cue agent's 0.0047 gives 2.0 deg, inside the range.

- **Owner's cloth and cushions (2026-11-15):** KaiC set `roll_friction` 0.015 and
  `cushion_restitution` 0.85 by feel. C11 left its published range (lag 2.14 to 2.87 m/s, band
  1.55-2.32; effective slope 0.0101 to 0.0153, band 0.0055-0.0105) and is now held to his table
  within 5%. C7 and C8 still compare the model with its fine-step solution, at his value: a ball
  rolling straight in keeps 0.797 of its speed where the paper's cushion keeps 0.911. Regenerating
  the cushion fixture for 0.85 showed that the oracle lacked the restitution floor
  (`cushion_restitution_min`, added to the physics after the oracle was written): with 0.98 the
  floor lay beyond the grid's 4 m/s, with 0.85 it is reached at 3.44 m/s. The oracle has it now.
  C1-C6, C9, C10 and C12-C14 do not depend on either value and are unchanged. Both values were
  withdrawn the same day (Selby's blue, below).
- **Owner's side grip (2026-11-15):** KaiC could not play Selby's blue from the 2014 World final,
  which was then taken, wrongly, for one side cushion and a run down to the reds. The paper
  fitted its one cushion friction, 0.14, to balls without side, and with it side runs out of
  grip: a third, two thirds and all of the side straight in gave 1.0, 1.7 and 1.75 diamonds
  where tables give about 1, 2 and 3, and a cue ball arriving slowly with three times its speed
  in side (after a full-ball pot) barely turned. `cushion_side_grip` (4) makes the friction
  along the cushion that many times the friction up its face, which stays 0.14: C8's new rows
  hold the rule and the shot. The oracle and the test's own fine-step solution carry the same
  change; with a grip of 1 both are the paper's model again, which is what the literature cases
  and every shot logged before it use. A ball without side straight in is untouched (C7 at 0
  deg, C11's lag); at an angle a rolling ball now comes off shorter and slower (50 deg in: 0.707
  V at 40.5 deg, was 0.801 V at 48.1). No measurement of a snooker cushion with side was found:
  the rule is from pool and the value is the owner's to judge.
- **Selby's blue, measured (2026-11-15):** KaiC sent the broadcast. Tracked frame by frame (30 a
  second; the picture mapped to the table by the five colours on their spots, checked where the
  cue ball touches each cushion), the shot is the blue to a middle pocket at about 8 degrees of
  cut, struck very hard with top and right-hand side: the cue ball hops, takes the right side
  cushion 0.25 m past the baulk line, the baulk cushion and the left side cushion, and reaches
  the two reds on the pink 6.85 s after the blue at about 0.4 m/s, 5.5 m in all. Its rolling legs
  slow at 0.086 to 0.12 m/s^2 (`roll_friction` 0.009 to 0.012). Fitting the pace and tip offset
  to the three cushions and their times, for each set of table values: with `cushion_restitution`
  0.98 and `roll_friction` 0.010 everything is within 5 cm and 0.2 s (pace 6.3 of the 6.5 m/s
  cap, tip 0.15 right and 0.17 high); 0.94 and 0.90 fit worse; with the feel values 0.85 and
  0.015 the cue ball stops 0.3 m short at any pace the cue has. Those values were set while the
  cue was half as strong again (10 m/s), so both went back to the published ones, C11 checks the
  published lag again, and C8 holds the shot (`positions/selby_blue.json`, whose `measured`
  block is the film). The shot does not tell side grips of 1, 2 and 4 apart (little side, all of
  it used at any of them), so the grip rests on the pool rule alone, restated for this cushion:
  with 0.98 the ball is across the table sooner and a third, two thirds and all of the side land
  0.94, 1.71 and 2.38 diamonds along (0.96, 1.5 and 1.56 on the paper's cushion).

No source was found for a measured snooker lag speed or snooker-ball throw; C11 and C6 use pool
measurements (the same phenolic balls, worsted vs napped cloth) and the spec's snooker cloth range.
