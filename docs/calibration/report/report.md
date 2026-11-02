# Calibration report

Physics parameters `588862c086c7b2c1`, table outline `cf92efd6db3fff39`, core 0.1.0, release build, from `calibration`.

## In plain words

Speeds are the cue ball's speed as it leaves the tip unless a figure says cue speed (how fast the cue is moving). A centre-ball hit sends the cue ball off 1.39 times as fast as the cue; with the tip 0.5 off centre (above, below or to one side) it leaves only 67% as fast as that for the same cue speed. Tip positions are fractions of the ball's radius from its centre: 0.5 is half-way to the edge, just inside the miscue limit of 0.55. Distances between two balls are from centre to centre, and the table is 3.57 m long.

- **Pace.** A centre-ball cue ball left to run: at 1 m/s it runs 2.7 m (76% of the table's length); at 2 m/s it would run 10.9 m on an endless table (3.0 table lengths); at 3 m/s it would run 24.5 m on an endless table (6.9 table lengths).
- **Stun.** A cue ball arriving full-ball at 2 m/s with no spin stops almost dead: it keeps 3.0% of its speed and creeps on 1.0 cm. Striking a third of the way from centre to the bottom of the cue ball (tip -0.33), it stuns from 50 cm when it leaves at 1.83 m/s (it then creeps on 0.4 cm) and from 1 m at 2.66 m/s: from further away you hit harder, because the cloth rubs the backspin off on the way. From 50 cm, hit a tenth harder (2.01 m/s) and it comes back 0.5 cm; a tenth softer (1.65 m/s) and it runs on 5.3 cm. Centre ball from 50 cm at 3 m/s is a stun-run-through: the cue ball arrives still sliding and runs on 23 cm.
- **Screw needs pace.** The cloth takes backspin off quickly. Striking 0.5 below centre, from 50 cm at 2 m/s it comes back 30 cm, at 3 m/s it comes back 2.4 m and at 4 m/s it would come back 5.6 m on an endless table; from 1 m at 2 m/s it runs on 17 cm, at 3 m/s it comes back 83 cm and at 4 m/s it would come back 3.7 m on an endless table. At 2 m/s from 1 m the backspin is gone before the cue ball arrives, so it runs on.
- **Follow.** Striking 0.5 above centre, from 1 m at 2 m/s it follows through 1.7 m and at 3 m/s it would follow through 3.9 m on an endless table. A cue ball already rolling when it hits a ball full keeps 27% of its speed, close to the textbook 2/7 (29%).
- **Cut angles.** A rolling cue ball arriving at 1 m/s and cut between a quarter and three-quarter ball comes off 29-34 degrees from the aim line (the 30-degree rule). Stunned, the two balls part at 80-84 degrees, a little under 90 because the balls are not perfectly elastic and the friction between them drags the object ball along.
- **Throw.** On a half-ball cut, friction between the balls drags the object ball a little the way the cue ball is going, so it goes fuller than the line through the two centres: 4.9 degrees with a stunned cue ball arriving at 0.45 m/s, 4.2 at 1.34 m/s and 3.4 at 3.13 m/s. Over 1 m that is 8.5 cm on the slow cut and 5.9 cm on the firm one, so aim that much thinner. A rolling cue ball throws less: 1.7 degrees at 1.34 m/s.
- **Side and squirt.** Side pushes the cue ball off the aim line, away from the side used: with the most side before a miscue (tip 0.55 off centre) it leaves 2.0 degrees off, 3.5 cm over 1 m.
- **Swerve.** With the cue raised and side, the cue ball first squirts away from the side, then curves back towards it from the moment it is struck while it slides, bending most just before it grips the cloth; once it rolls it runs straight. Raised 30 degrees with the tip 0.4 right of centre, at a cue speed of 1.5 m/s, it squirts 1.4 degrees left, turns 12.9 degrees right over the first 26 cm and then holds its line: 0.1 degrees more over the next 1.7 m, the nap's drift on a slowing ball.
- **Cushions.** A rolling ball arriving straight into a cushion at 1 m/s comes off with 91% of its speed; firm hits lose more, 70% at 4 m/s. (The one set of measurements on a snooker table, by Mathavan and colleagues, averaged 82% below 1.5 m/s, where their own cushion model, which the game uses, gives 91%.) Rolling into a side cushion at 45 degrees with no side, the ball leaves at 37.3 degrees just after the impact, then bends out to 50.1 as it grips the cloth again: that is the line you see. Running side widens both (46.6 and 58.8 degrees, with as much side as a tip 0.4 off centre gives) and check side narrows them (35.7 and 49.5).
- **Cloth speed.** A lag (from the baulk line up to the top cushion and back down to the baulk cushion) needs the cue ball to leave at 2.14 m/s, a cue speed of 1.54 m/s; coming back down from the top cushion to the baulk cushion takes 8.4 seconds.
- **Still starting guesses, for you to judge by feel in M2:** how lively the pocket jaws are (firm balls rattling, slow ones dropping), the nap drift on slow balls with and against the nap, what a miscue does, how much a very hard ball dies on a cushion, and the cloth's overall pace (the one snooker measurement found a slower cloth; tournament tables are heated and faster, so this sits between the two).

## Results against the targets

What this report measured itself, from `ofs_sim_cli calibrate` and its own shots. The calibration suite (`ofs_calibration_tests`, run by the gate) checks the same targets with the exact tolerances in `docs/calibration/targets.md`.

| # | What | Measured | Target | Verdict |
| --- | --- | --- | --- | --- |
| C1 | Centre ball, no spin: slides, then rolls | at 2 m/s rolls after 0.4710 m at 1.4286 m/s | 12 v^2 / (49 mu_s g) = 0.4710 m at 5/7 v = 1.4286 m/s (TP 4.1), 1e-6 relative | met |
| C2 | Full-ball stun | cue ball keeps 3.00% of its speed | (1-e)/2 = 3.00%, within 0.2% of the speed | met |
| C3 | Full-ball, rolling | follows at 0.267 of its speed | (5/7)(1-e)/2 + 2/7 = 0.307, less up to 0.061 for topspin friction (TP A.14) | met |
| C4 | Rolling cut, 1/4 to 3/4 ball, cue ball at 1 m/s | 29.1-34.2 deg off the aim line | 30-degree rule, inside 24-36 | met |
| C5 | Stun cut, 1/4 to 3/4 ball, cue ball at 1 m/s | 80.3-84.1 deg between the balls | 90-degree rule less restitution and throw: 80-92 | met |
| C6 | Half-ball stun throw at 0.447 / 1.341 / 3.129 m/s | 4.86 / 4.16 / 3.39 deg | Alciatore 4.5 / 4.1 / 3.45, Jewett 6 / 4.1 / 3.45 (+- 0.75); slow above fast by 0.25 | met |
| C7 | Cushion rebound, rolling, no side, 0.5-4 m/s, 0-80 deg | worst 0.05 deg and 0.08% of the speed off the fine-step Mathavan solution over 36 cases | 1 deg, 2% (the suite also checks side, stun and screw) | met |
| C8 | Running and check side, into the cushion at 45 deg at 1.5 m/s | check 35.7 < none 37.3 < running 46.6 deg | angles ordered check < plain < running (Mathavan Fig. 8) | met |
| C9 | Squirt at maximum side (tip 0.55) | 2.01 deg | 1-3 deg | met |
| C10 | Swerve, cue raised 15-45 deg with side | while sliding pulled at 2.080 m/s^2 in a direction fixed to 0.00 deg, turning towards the side; once rolling at most 0.07 deg more | TP A.4 parabola: mu_s g = 2.080 m/s^2 in one direction (0.5%, 0.2 deg); straight once rolling apart from the nap | met |
| C11 | Lag: baulk line to the top cushion and back to baulk | cue ball 2.14 m/s; back down from the top cushion in 8.4 s, effective slope 0.0101 | 1.55-2.32 m/s (TP B.6 for slopes 0.0055-0.0105, +-5%); slope 0.0055-0.0105 | met |
| C12-C14 | Energy, determinism, robustness | not measured by this report | ofs_calibration_tests and `ofs_sim_cli fuzz` in the gate | - |

## What changed

What changed in M1's tuning from the starting values, each with its source in `core/data/table_params.json`:

- Ball-ball friction (`ball_friction_a` 0.009951 to 0.058, `ball_friction_b` 0.108 to 0.092, `ball_friction_c` 1.088 to 2.73): refitted by least squares to the published half-ball stun throw (Alciatore, Billiards Digest Sept 2006; Jewett 1995). The old fit threw 1.7 degrees on a firm cut against 3.45 measured.
- Sliding friction and ball mass (`slide_friction` 0.2 to 0.212, `ball_mass_kg` 0.141 to 0.1406): Mathavan, Jackson & Parkin's measurements on a snooker table and ball (Am. J. Phys. 2009; Proc. IMechE 2010), the same source as the cushion model.
- Rolling resistance (`roll_friction` 0.008 to 0.01): the fast end of the snooker evidence that the lag target allows (Mathavan measured 0.0127 on an unheated lab table).
- Cue end mass (`cue_end_mass_kg` 0.0075 to 0.0047): pooltool's snooker cue (ball mass / 30): squirt at maximum side drops from 3.05 to 2.0 degrees, inside C9's 1-3.
- Cushion restitution on hard hits (`cushion_restitution_slope_per_m_s` 0.0 to 0.16, `cushion_restitution_min` new, 0.7): the slope is a least-squares fit to Mathavan's measured rebounds above 2.5 m/s (Proc. IMechE 2010, Fig. 7b); the floor is Alciatore's typical rail restitution (TP B.6), reached at 4.25 m/s into the cushion.

## Charts

Each chart says what it shows and whether it meets its target from spec 7.11. "For review" means the target is the owner's feel rather than a number this report can check.

| Target | Chart | Verdict |
| --- | --- | --- |
| C4, C5 | [Cue ball deflection against cut, rolling and stunned](#c4-c5-cue-ball-deflection-against-cut-rolling-and-stunned) | met |
| C6 | [Cut-induced throw against cut](#c6-cut-induced-throw-against-cut) | met |
| C7 | [Cushion rebound angle and speed against incidence](#c7-cushion-rebound-angle-and-speed-against-incidence) | met |
| C8 | [Running and check side off a cushion](#c8-running-and-check-side-off-a-cushion) | met |
| C9 | [Squirt against side](#c9-squirt-against-side) | met |
| C10 | [Swerve paths with an elevated cue and side](#c10-swerve-paths-with-an-elevated-cue-and-side) | met |
| C11 | [The lag: cue speed to reach the top cushion and back to baulk](#c11-the-lag-cue-speed-to-reach-the-top-cushion-and-back-to-baulk) | met |
| 7.12 | [Screw-back and follow distance against tip height](#712-screw-back-and-follow-distance-against-tip-height) | for review |
| 7.13 | [Simulation time of break-offs](#713-simulation-time-of-break-offs) | met |

### C4, C5: Cue ball deflection against cut, rolling and stunned

![Cue ball deflection against cut, rolling and stunned](deflection.png)

How far the cue ball turns from its aim line after hitting a red, against how full the hit is (1 is full-ball, 0 the thinnest), for a rolling cue ball and a stunned one, the cue ball at 1 m/s. A rolling cue ball cut between a quarter and three-quarter ball deflects 29.1 to 34.2 degrees from its aim line, inside the 30 degree rule's 24 to 36 (C4). Stunned, the two balls part at 80.3 to 84.1 degrees, inside the 90 degree rule's 80 to 92 (C5).

Setup: At 1 m/s. separation_deg is between the two balls' directions just after impact; cue_deflection_deg is the cue ball's direction from the aim line once it rolls again.

### C6: Cut-induced throw against cut

![Cut-induced throw against cut](throw.png)

How far friction between the balls pushes the object ball off the line of centres, against how full the hit is, for a stunned cue ball arriving at 0.5, 1.5 and 4 m/s. At a half-ball hit the object ball is thrown 4.9 degrees with the cue ball at 0.5 m/s, 4.0 degrees with the cue ball at 1.5 m/s, 3.3 degrees with the cue ball at 4 m/s. Slower shots should throw more (they do) and stay within the few degrees published measurements show, 1 to 6 here (they do) (C6).

Setup: Stun shots (no spin at contact): throw is the angle between the object ball's direction and the line of centres at impact.

### C7: Cushion rebound angle and speed against incidence

![Cushion rebound angle and speed against incidence](cushion_rebound.png)

The angle a plain rolling cue ball comes off the side cushion at, against the angle it goes in at, for four speeds, and how much of its speed it keeps. Rebound angles run 0.0 to 79.7 degrees and the ball keeps 70.2 to 94.3% of its speed. Up to 2 m/s the speeds give the same angles to within 0.05 degrees, so their lines lie on top of each other; only the firm hit's restitution falls. Against the target (C7): worst 0.05 deg and 0.08% of the speed off the fine-step Mathavan solution over 36 cases; the target is 1 deg, 2% (the suite also checks side, stun and screw).

Setup: A rolling ball without side into the left side cushion. Angles from the cushion normal; rebound_deg just after the impact, rebound_rolling_deg once the ball rolls again.

### C8: Running and check side off a cushion

![Running and check side off a cushion](cushion_side.png)

The rebound angle of a ball rolling into the cushion at 45 degrees at 1.5 m/s with check side (left), none and running side (right). Side 1 is as much as a tip 0.4 R off centre gives. Into the cushion at 45 degrees, running side sends the ball off at 46.6 degrees just after the impact, no side at 37.3 degrees and check side at 35.7 degrees. Once the ball grips the cloth again its path opens out to 58.8, 50.1 and 49.5 degrees. Running side should widen the angle and check side narrow it (it does) (C8).

Setup: side is w_z R / v at the start, positive for running side, negative for check side.

### C9: Squirt against side

![Squirt against side](squirt.png)

How far side pushes the cue ball off the aim line, against how far from centre the tip strikes (a level cue at a cue speed of 3 m/s); past the miscue limit the strike miscues. With the most side before a miscue (0.55 R) the cue ball leaves 2.0 degrees off the aim line, inside the 1 to 3 degree range (C9).

Setup: A level cue at 3 m/s. squirt_deg is signed from the aim line.

### C10: Swerve paths with an elevated cue and side

![Swerve paths with an elevated cue and side](swerve.png)

The cue ball's path seen from above with the cue raised and side applied, aiming up the table at a cue speed of 1.5 m/s; the right-hand panel is the first 40 cm, with a dot where each starts to roll. With a raised cue, 15 deg, tip +0.40 turns 6.0 degrees right over its first 29 cm and 0.0 more in the 1.7 m after it grips; 30 deg, tip +0.40 turns 12.9 degrees right over its first 26 cm and 0.1 more in the 1.7 m after it grips; 30 deg, tip -0.40 turns 12.9 degrees left over its first 26 cm and 0.1 more in the 1.7 m after it grips; 45 deg, tip +0.40 turns 21.6 degrees right over its first 22 cm and 0.1 more in the 1.9 m after it grips. Each curves towards its side while it slides, on a parabola (a fixed pull of 2.08 m/s^2, the cloth's sliding friction), bending most just before it grips, and runs straight once it rolls apart from the nap's slight drift. (C10)

Setup: Aiming along +x at 1.5 m/s from (-0.20, 0.00); positive tip_x is right of centre as the player sees it, which is towards -y. Each path stops at the cue ball's first contact.

### C11: The lag: cue speed to reach the top cushion and back to baulk

![The lag: cue speed to reach the top cushion and back to baulk](lag.png)

Where a centre-ball cue ball stops for each cue speed, played from the baulk line to the top cushion and back. A lag, up to the top cushion and back to baulk, needs a cue speed of 1.54 m/s, sending the cue ball off at 2.14 m/s. Against the target (C11): cue ball 2.14 m/s; back down from the top cushion in 8.4 s, effective slope 0.0101; the target is 1.55-2.32 m/s (TP B.6 for slopes 0.0055-0.0105, +-5%); slope 0.0055-0.0105.

Setup: Centre ball from the middle of the baulk line straight up the table. The lag is the slowest cue speed whose cue ball touches the top cushion and then the baulk cushion.

### 7.12: Screw-back and follow distance against tip height

![Screw-back and follow distance against tip height](screw_follow.png)

How far the cue ball screws back or follows through after a full-ball hit on a red 1 m away, against how high or low the tip strikes, at three cue speeds. After a full-ball hit the cue ball travels at a cue speed of 1 m/s from +0.02 m (tip -0.4) to +0.30 m (tip +0.5); at a cue speed of 2 m/s from +0.14 m (tip -0.5) to +1.44 m (tip +0.5); at a cue speed of 3 m/s from -0.41 m (tip -0.5) to +3.33 m (tip +0.5), negative being screw back. At a cue speed of 1 m/s with the tip -0.5 the cue ball never reaches the red, stopping 0.74 m from where it started: the screw slows it too much, so the chart shows that point as missing. The owner judges screw and follow by feel (spec 7.12).

Setup: Full-ball hit on a red 1 m away (centre to centre), simulated by this report on an endless table (the real outline scaled up 20 times), so no cushion is in reach. Distance from the contact to where the cue ball stops, along the aim: negative is screw back. The speeds are cue speeds; the cue ball leaves slower the further the tip is from centre.

### 7.13: Simulation time of break-offs

![Simulation time of break-offs](break_sim_time.png)

How long the simulation of each break-off took on this PC. Over 200 break-offs the simulation took a median of 268 us and at most 589 us, within the 2000 us budget (spec 7.13).

Setup: Wall-clock time of simulate() per break-off, measured in a release build; only release timings count against the budget.
