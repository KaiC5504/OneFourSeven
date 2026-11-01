# One Four Seven — design

- **Date:** 2026-11-01
- **Owner:** KaiC (plays and judges; does not write or read code)
- **Builders:** Claude Code sessions and agent teams (write, test, ship)
- **Status:** design, awaiting owner approval

Each decision is labeled. **Owner** means KaiC chose it in the design conversation.
**Claude's call** means a default chosen on his behalf, which he can overrule. Anything
labeled neither is left to the builder.

---

## 1. What this is and why

One Four Seven is a single-player snooker game for the owner's iPhone 17 Pro Max. It is
built and tested on Windows first, then shipped to the phone through TestFlight.

The owner plays real snooker regularly. He wants the most realistic ball behaviour he
can get on a phone, closer to ShootersPool than to the usual mobile snooker games. The
bar for the whole project is that **shots behave the way they do on a real table**: stun,
screw, side, swerve, throw, the cushions, pocket jaws and the nap. Graphics, menus and
features all come second.

He wants to open the app and play: frames against a computer opponent at a chosen
difficulty, or practice on his own. There is nothing social, nothing to buy and nothing to
grind.

**Success:** after a session, the owner feels the table played like a real one, the
computer opponent felt like a real person of that standard, and the controls let him play
the shot he meant to play.

## 2. Goals, non-goals, anti-goals

### Goals
1. Realistic ball physics as the core of the game (section 7). **Owner**
2. Play against a computer opponent with difficulty levels that play like real players of
   that standard. **Owner**
3. Solo training: free table, classic routines, shot replay, shot breakdown. **Owner**
4. Full official WPBSA snooker rules. **Owner**
5. Precise, comfortable touch controls (section 8). **Owner**
6. Built and testable on Windows first, then shipped to iPhone via TestFlight. **Owner**

### Non-goals (not being built)
- Multiplayer of any kind: online, local, pass-and-play. **Owner**
- Accounts, leaderboards, cloud sync, social features. **Owner**
- Ads, in-app purchases, analytics, tracking or any third-party SDK that phones home.
- A career mode, licensed players, venues or tournaments.
- Music or commentary; a referee voice. **Owner** (table sounds only)
- Portrait orientation. **Owner**
- Resuming an unfinished match after the app is closed. **Owner** (stats are saved;
  matches are not)
- Adjustable table conditions such as cloth speed or cushion bounce. **Owner** (one fixed
  table)
- Top-down or free cameras during play. **Owner** (behind the cue only; replays get more
  angles)
- Other cue games (pool, 6-reds, billiards), Android, iPad layouts, a Windows release.
  Windows is the test bed; a playable Windows build exists only as a by-product.
- Balls leaving the table surface: jump shots and balls flying off the table (section 7.9).

### Anti-goals (would count as failure even if everything technically works)
- Balls that feel like a generic game physics engine: floaty, bouncy, or with spin that
  does nothing.
- Hidden aim assistance that quietly corrects the player's shot.
- A computer opponent that cheats. It uses the same physics, the same rules and the same
  information as the player. Easier levels miss because of human-like error, not
  scripted "deliberate misses" that look fake.
- Controls that make a thin cut a fight, or a stroke that doesn't do what the finger did.
- Guide lines that can't be turned off.
- The phone getting hot or the battery draining while the table sits still.
- Pop-ups, nags, rating requests, tutorials that can't be skipped.

## 3. Who does what

- **Owner:** plays builds on Windows and on his iPhone and says how they feel. The only
  other work is a one-time Apple setup of about 15 minutes, with exact click paths,
  before the first TestFlight build (section 17.4).
- **Claude Code / agent teams:** everything else. That covers design, code, assets,
  tests, physics calibration, AI calibration, CI, signing and TestFlight uploads. Agents
  verify their own work with automated tests, screenshots, headless simulations and the
  debug harness (section 16) before asking the owner to play anything.
- **Owner feedback loop:** each owner play session comes with a short, concrete checklist
  ("play a deep screw from the blue spot: did it come back as far as you expected?").
  Answers turn into tuning changes and regression tests.

## 4. Platform and technology

| Decision | Choice | Label |
| --- | --- | --- |
| Engine | Godot 4, pinned to **4.7.1-stable** (already at `D:\Tools\Godot`); upgrades only on purpose | Owner (Godot); Claude's call (version) |
| Ball physics | Custom simulator, not Godot's physics engine | Claude's call, owner agreed |
| Physics, rules, AI language | C++17, compiled as a GDExtension via `godot-cpp` pinned to the matching 4.7 branch | Claude's call |
| Game, UI, camera, audio, saving | GDScript | Claude's call |
| Renderer | Godot **Mobile** renderer on both Windows and iOS, so Windows shows what the phone will show | Claude's call |
| Target device | iPhone 17 Pro Max, landscape only (both landscape directions), iPhone only | Owner |
| Windows toolchain | MSVC from VS 18 Build Tools (installed), CMake (installed), Python 3.13 (installed), SCons (install via pip) | Claude's call |
| iOS build | GitHub Actions macOS runner: build the GDExtension for iOS, export Godot's Xcode project headlessly, sign, upload to TestFlight | Owner (pipeline), section 17 |
| Repo | `KaiC5504/OneFourSeven`, public, so macOS CI minutes are free | Claude's call (pipeline default) |
| App identity | Store name **One Four Seven**, home-screen label **One Four Seven** (builder may shorten it to fit under the icon), bundle id `com.kaichuan.onefourseven` | Owner |

**Why C++ for the core:** the computer opponent decides each shot by simulating hundreds
to thousands of candidate shots (section 10). GDScript is far too slow for that on a
phone. C++ also lets the physics and rules run as plain native programs on Windows, so
agents can test them in milliseconds without starting Godot.

## 5. Architecture

```
┌──────────────────────────── Godot project (GDScript) ────────────────────────────┐
│  Screens & flow │ Input (touch/mouse) │ Camera │ Table rendering │ Audio/haptics │
│  Match controller │ Training controller │ Replay & breakdown views │ Save/settings │
└───────────────▲─────────────────────────────────────────────────────▲────────────┘
                │ GDExtension API (shot in → shot record out,           │ debug harness
                │ rules state, AI requests on a worker thread)          │ (debug builds)
┌───────────────┴──────────────── core (C++, no Godot dependency) ──────┴────────────┐
│  table geometry │ physics simulator │ shot record │ rules engine │ computer opponent │
└────────────────────────────────────────────────────────────────────────────────────┘
        ▲ also built as native Windows test programs: unit tests, calibration suite,
          rules scenarios, AI tournaments, simulation CLI
```

- `core/` is a self-contained C++ library with no Godot types. A thin binding layer in
  `gdext/` wraps it for Godot. The same library builds as native Windows executables for
  testing.
- **Shot record:** simulating a shot produces a complete record: every ball's trajectory
  as analytic segments between events, plus the event list (collisions, cushion hits,
  pockets, state changes). The game animates by evaluating the record at the current
  time. Replay, slow motion, the shot breakdown and sound/haptic cues all read the same
  record. Nothing is simulated twice, and replays are exact.
- **Rules engine:** consumes the table state before the shot, the shot record and any
  nomination, and returns fouls, points, the next player, the ball on and the choices
  available. It lives in C++ because the AI must evaluate rules inside its simulations.
- **Computer opponent:** runs on a worker thread through Godot's `WorkerThreadPool` or a
  `std::thread` owned by the extension. It must never block the frame.

### Suggested repository layout (the builder may refine it)

```
core/         C++ library: geometry, physics, rules, ai, shot record
core/tests/   native unit, calibration, rules-scenario and AI tests
tools/        native CLIs (simulate a shot from JSON, run AI tournaments), plotting scripts
gdext/        godot-cpp binding, SConstruct or CMake for Windows + iOS
game/         Godot project (project.godot, scenes, scripts, assets)
docs/         this design, development notes, physics calibration notes
.github/      ios-compile.yml, testflight.yml, windows-ci.yml
scripts/      testflight.py and helpers (from the ios-testflight skill)
THIRD_PARTY.md  licences of everything not written here
```

## 6. The table

One fixed, tournament-standard full-size table. **Owner**

### 6.1 Dimensions (WPBSA Official Rules, 2022 rulebook)

| Item | Value |
| --- | --- |
| Playing area (between cushion noses) | 3569 mm × 1778 mm |
| Baulk line | 737 mm from the face of the bottom (baulk) cushion |
| The D | Semicircle of radius 292 mm, centred on the middle of the baulk line, on the baulk side |
| Ball diameter | 52.5 mm (tolerance ±0.05 mm), all balls equal |
| Ball mass | Starting value 0.141 kg; the builder confirms from published phenolic snooker ball specs |

### 6.2 Spots (viewed from the baulk end)
- **Yellow:** right-hand end of the D, on the baulk line. **Green:** left-hand end.
  **Brown:** middle of the baulk line.
- **Blue:** centre of the table.
- **Pink:** midway between the blue spot and the face of the top cushion.
- **Black:** 324 mm from the face of the top cushion, on the centre line.
- **Reds:** a tight triangle of 15. The apex red sits as close to the pink as possible
  without touching it, and the base is parallel to the top cushion. In the simulation,
  the rack has microscopic random gaps (section 7.7).

### 6.3 Cushions and pockets
- The WPBSA pocket templates (opening widths, jaw curvature, undercut) are **not public**.
  The builder sources the best available approximations. Candidates: pooltool's snooker
  table spec, published measurements of tournament templates, and table makers' figures.
  The aim is tight tournament-style pockets with **curved, rounded jaws**, as snooker
  pockets have, not pool's straight angled facings. The geometry is recorded in one data
  file with its sources.
- Corner and middle pockets differ in mouth width and jaw shape. The middle pockets are
  set slightly back.
- **Owner check:** pockets feel "tournament tight". A ball hit firmly into the jaws
  rattles and stays out; a slow, accurate ball drops even when it touches a jaw.
- Cushion nose height comes from the cushion model's source (Mathavan et al., 2010, on
  snooker cushions). The builder records the value used.

### 6.4 Cloth
- Nap runs from the baulk end towards the top (black) end. **Owner:** nap is modelled,
  conditions are fixed.
- The cloth colour is standard tournament green.

## 7. Physics

This is the heart of the project, so it is specified in the most detail.

### 7.1 Model
- **Event-based simulation with analytic equations of motion**, the approach of Leckie &
  Greenspan (2006) and of the open-source simulator *pooltool* (Kiefl). There is no
  fixed time step. Each ball's motion between events has a closed form, the simulator
  solves for the exact time of the next event, jumps every ball to that moment, resolves
  the event and repeats until every ball is at rest or pocketed.
- pooltool is a **reference** for equations, event detection and test cases. The builder
  checks its licence before reusing any code; re-deriving from the papers is fine either
  way. Anything reused is attributed in `THIRD_PARTY.md`.
- Doubles everywhere. SI units (metres, seconds, kilograms, radians) inside the core.
- Motion is planar: ball centres stay at height R above the cloth. Angular velocity is a
  full 3D vector, which is what makes swerve and masse possible.

### 7.2 Ball motion states
Each ball is always in one of these states. The transitions between them are events.

| State | Meaning | Motion |
| --- | --- | --- |
| Stationary | At rest | none |
| Spinning | No translation, side spin only (ω about the vertical axis) | ω_z decays linearly (spin friction) |
| Sliding | Contact point slipping on the cloth | Sliding friction (μ_s) acts against the slip velocity; linear velocity and spin change linearly until slip reaches zero |
| Rolling | Contact point not slipping | Rolling resistance (μ_r) decelerates the ball; side spin decays independently |

Sliding to rolling happens at the analytic time t = 2|u₀| / (7 μ_s g), where u₀ is the
initial slip velocity. Swerve comes out of the sliding equations naturally when the spin
axis is tilted.

### 7.3 Cue strike
Inputs: aim direction, tip offset (horizontal a, vertical b, as fractions of R), cue
elevation θ and cue speed V.
- **Impulse model:** the instantaneous cue-tip impact of the pooltool / Marlow model, with
  effective cue mass (starting value 0.54 kg, a typical snooker cue) and tip restitution.
  It produces the cue ball's initial linear and angular velocity, including the loss of
  speed for off-centre hits.
- **Squirt (cue ball deflection):** with side, the cue ball leaves at a small angle away
  from the aim line. The angle grows with the offset and depends on the cue's end mass.
  It is tunable; the owner judges it.
- **Swerve and masse:** come from elevation plus offset tilting the spin axis.
- **Miscue:** tip offsets beyond a miscue limit (starting value 0.55 R, tunable)
  miscue. The cue ball leaves weakly at an unpredictable small angle, with a distinct
  sound. A miscue is not a foul in itself; whatever happens afterwards is judged
  normally.
- **Cue obstruction:** the cue is a tapered cylinder (tip 9.5 mm, butt 29 mm, length
  1450 mm). The game computes the **minimum elevation** at which the cue clears the
  cushion rail and any ball behind the cue ball along the aim line. If the requested
  elevation is lower, it is raised to that minimum and the elevation control shows it.
  This is realistic, and it is how real players get forced into awkward cueing near the
  cushion.

### 7.4 Ball–ball collisions
- Normal impulse with ball–ball restitution e_b (starting value 0.94; phenolic balls
  0.92–0.98, tuned against the calibration targets).
- **Friction during contact:** gives cut-induced throw, spin-induced throw and spin
  transfer to the object ball. Use Alciatore's speed-dependent friction model
  (μ = a + b·e^(−c·v_rel)) as the starting point, re-fitted if snooker-ball data differs.
- Exactly touching balls, clusters and the break-off pack need a robust policy for
  simultaneous or chained collisions (section 7.7).

### 7.5 Cushions
- **Cushion impact model:** Mathavan, Jackson & Parkin (2010), "A theoretical analysis of
  billiard ball dynamics under cushion impacts". It was derived and checked for snooker.
  It accounts for the cushion's contact height and for slip at both the cushion and the
  cloth during the impact. That produces correct rebound angles, the effect of running
  and check side, and speed-dependent loss. Han (2005) is an acceptable fallback only if
  Mathavan proves unworkable, and the builder notes the trade-off.
- Straight cushion sections are line segments. Pocket jaws are curved segments using the
  same impact model with their own tunable restitution.

### 7.6 Pockets
- A ball is potted when its centre passes the pocket's drop boundary, a region behind
  the jaws.
- Rattles and "hanging on the lip" emerge from jaw collisions, not from scripted
  outcomes.
- A potted ball's fall into the pocket is animated cosmetically. It doesn't need
  physical accuracy, but it should look natural and make the pocket sound (section 13.2).

### 7.7 The break-off and clusters
- Simultaneous contact between three or more balls is resolved as a sequence of
  pairwise collisions. The rack is built with microscopic random gaps (order of
  0.01–0.1 mm, seeded) so collisions arrive in a defined order, as pooltool does.
- There is a guard against infinite or near-zero-time event loops, such as a ball wedged
  between two balls or a ball in a jaw corner.
- **Owner check:** break-off pack spread looks and feels right. A thin hit on the end red
  with check side brings the cue ball back towards baulk and moves only a few reds; a
  heavy contact splits the pack.

### 7.8 Nap
- A small velocity-dependent force acts on moving balls. Its direction depends on the
  ball's heading relative to the nap (baulk → top), and its strength grows as the ball
  slows down.
- A side-spin interaction term makes side "hold" when playing with the nap and behave
  differently against it, at slow pace.
- Qualitative behaviours to reproduce, which the owner confirms by feel:
  - a slow ball played up the table with side drifts in the direction of the side;
  - against the nap, at slow pace, the drift effect reverses;
  - firm shots are barely affected.
- All magnitudes are tunable constants. The owner's feel decides them.

### 7.9 Explicit simplifications (non-goals)
- Balls never leave the cloth: no jump shots, no balls off the table. Shots that would
  make a real ball jump are clamped to the table. Because a jump-shot foul can't happen,
  the rules engine never sees one.
- Rendering is purely cosmetic: ball spin is shown by rotating the ball texture
  according to its angular velocity, but nothing visual feeds back into the physics.

### 7.10 Parameters
All tunable constants live in **one data file** (e.g. `core/data/table_params.json`), with
a starting value, a source and a note for each. The physics core loads it.
- **Debug builds** reload it without restarting on Windows.
- **TestFlight builds** fetch a newer version live from the PC over the phone link
  (section 16.10).
- **Release builds** embed it.

The parameters a game uses are versioned by content hash, and the hash is logged with
every shot.

Starting points: μ_s ≈ 0.2, μ_r ≈ 0.005–0.01 (snooker cloth is fast; calibrate), spin
friction from pooltool's default, e_b ≈ 0.94. Cushion and jaw constants from Mathavan.

### 7.11 Calibration targets (agent-checked; each is an automated test)

| # | Situation | Expected |
| --- | --- | --- |
| C1 | Centre-ball hit with no spin | Becomes rolling at distance 12v₀²/(49 μ_s g) |
| C2 | Full-ball contact while the cue ball is still sliding with zero spin (stun) | Cue ball stops dead, apart from the small forward motion e_b causes |
| C3 | Full-ball contact with a rolling cue ball | Cue ball follows through, settling at about 2/7 of its pre-impact speed |
| C4 | Rolling cue ball, cut between ¼ and ¾ ball | Cue ball deflects about 30° from the aim line (Alciatore's 30° rule) |
| C5 | Stun cut | Cue ball leaves along the tangent line (90° rule) |
| C6 | Cut-induced throw at a half-ball hit, slow vs fast | Throw within the range of published measurements; slower shots throw more |
| C7 | Cushion rebounds, plain ball, a range of angles and speeds | Rebound angle and speed match Mathavan's model and data |
| C8 | Running vs check side off a cushion | Widens vs narrows the rebound angle in the right direction and rough magnitude |
| C9 | Maximum side | Squirt angle within the tunable range (starting 1–3°) |
| C10 | Swerve (elevated cue, side) | Curves in the direction of the side and only once the cue ball starts to grip |
| C11 | Table speed | Rolling resistance calibrated so a "lag" (cue ball to the top cushion and back to the baulk cushion) needs a cue-ball speed in a realistic range for a fast tournament cloth; the source is recorded |
| C12 | Energy | Total kinetic energy never increases; every shot ends with all balls at rest within a time cap |
| C13 | Determinism | Same start state, shot inputs and seed give a bit-identical shot record on the same platform |
| C14 | Robustness | 100,000 random shots (including break-offs and frozen clusters) finish without hangs, NaNs or balls passing through each other or through cushions |

### 7.12 Owner feel checklist (each owner play session uses part of it)
Stun; stun-run-through; screw-back distance at several distances; deep screw; follow
through; side off the cushion (running and check); swerve round a ball; plants; doubles;
pace-sensitive pots into the jaws (rattle vs drop); slow ball drift with and against the
nap; cushion-hugging balls; break-off spread; thin safety clinging back to baulk.

### 7.13 Performance budget
- **One shot:** a full shot simulation, including a break-off, completes in **≤ 2 ms**
  on one desktop core.
- **The phone:** the iPhone is assumed similar per core, and this is confirmed on device
  via the debug overlay (section 16.6).
- **The opponent:** these numbers are what make its shot search affordable (section 10.6).

## 8. Controls

All gestures are designed for touch first. Windows uses mouse equivalents that feed the
same input layer (section 8.8).

### 8.1 Aiming. **Owner:** drag plus fine-tune
- Dragging anywhere on the table area swings the cue around the cue ball. Sensitivity
  scales with camera zoom, so zoomed in means finer control.
- A **fine-tune control** (a thumb wheel along the right screen edge, or a similar
  control the builder may choose) turns the aim in very small steps, about 0.01°, for
  thin cuts. Its sensitivity is in settings.
- Aim never snaps to balls or pockets. There is no hidden assistance.

### 8.2 Spin and elevation. **Owner:** widget plus elevation
- A cue-ball widget in a corner. Tap it to enlarge, then drag the tip marker for top,
  screw and side. The miscue limit is drawn as a ring.
- An elevation control next to it, from the minimum allowed by obstruction (section 7.3)
  up to near-vertical for masse. It shows when obstruction has forced the cue up.
- Both reset to centre ball and minimum elevation after each shot. The builder's call
  whether this is a setting.

### 8.3 Striking. **Owner:** both modes, switchable in settings
- **Finger stroke:** pull back in the stroke area, then push forward and release. The
  push speed over the last ~80 ms before release sets cue speed, through a tunable
  curve that gives fine control at low power. The cue on screen follows the finger's
  backswing. Dragging out of the stroke area cancels the stroke.
- **Power bar:** set power on a slider with a non-linear scale for fine low-power
  control, then tap Shoot.
- **Claude's call:** an optional setting, off by default, that adds a small aim error
  when the finger stroke wanders sideways, like a crooked cue action.
- Maximum cue speed is tuned so a full-power shot matches a strong real break-off or
  power shot.

### 8.4 Guide lines. **Owner:** adjustable in settings
Levels 0–2 are **straight-line geometry only**. **Owner:** they ignore squirt, swerve,
throw and nap. The player allows for side and throw, as on a real table.
- Level 0: no guides, only the cue.
- Level 1: a straight aim line to the first contact plus a ghost ball at the geometric
  contact position.
- Level 2: level 1 plus a short line along the geometric object-ball direction (the line
  of centres).

Level 3 is **simulated**. **Owner:** it shows the full predicted paths of the cue ball
and object balls, taking account of squirt, swerve, throw, spin and nap.
- The paths come from the noise-free simulator.
- **Power-bar mode:** they use the currently set power.
- **Finger-stroke mode:** they use the pace of the player's previous stroke, defaulting
  to a medium pace before the first stroke of a session. The pace assumed is shown
  beside the guide.
- The prediction is recomputed only when the aim, spin, elevation or pace changes, never
  every frame. Recomputation is limited to a handful of simulations per second while
  aiming.

Guides never alter the shot. Applies in every mode. **Claude's call:** the default is
level 1.

### 8.5 Camera. **Owner:** behind the cue only, during play
- The camera sits behind the cue ball, looking along the aim line, and follows aim
  changes.
- **Pinch** zooms. A **two-finger vertical drag** tilts the camera from cue level (low)
  up to a high, nearly overhead angle. That high angle doubles as the planning view
  without adding a separate top-down mode.
- After the strike, the camera holds steady so the player can watch the shot. The
  builder may add a gentle auto-rise if owner testing wants it. Afterwards it returns
  behind the cue ball for the next shot.
- Replays get more camera angles (section 12.3).

### 8.6 Ball in hand
- After an in-off, the cue ball is placed by dragging it within the D. Placement is
  limited to legal spots: inside or on the D, not overlapping balls.
- A confirm button commits the position.

### 8.7 Nomination
- When the ball on is "a colour" or a free ball must be nominated, the game pre-selects
  the ball the aim line hits first. The player can tap any eligible ball to change it.
- The nominated ball is highlighted, and the referee logic uses it.

### 8.8 Windows equivalents. **Owner:** agreed
- A left-drag acts as a one-finger drag, the scroll wheel as pinch-zoom, and a right-drag
  as a two-finger drag.
- Keyboard shortcuts are allowed as test aids, but they add no features the phone lacks.
- Real two-finger gestures are verified on the iPhone.

### 8.9 Layout
- The table stays unobstructed, and controls sit at the screen edges.
- The layout respects the iPhone's safe areas and the Dynamic Island in both landscape
  directions.
- **Claude's call:** a left-handed mirror layout setting.

## 9. Rules. **Owner:** full official rules

The rules engine implements the **WPBSA Official Rules of Snooker** (the current rulebook,
2022 edition or newer), section by section. Agents read the official rulebook PDF and
build one scenario test per rule clause they implement. The engine covers at least:

- Frame setup, break-off from the D, players alternating, and who breaks in later frames
  (alternate).
- **Ball on:** red / colour alternation, the colour sequence once the reds are gone, and
  re-spotting colours while reds remain. When a colour's spot is occupied, the spotting
  rules apply: highest available spot, then as near as possible to its own spot on the
  centre line towards the top cushion, or towards baulk if needed, per the rulebook.
- **Scoring:** ball values, breaks, points remaining, and the "snookers required"
  calculation.
- **Fouls and penalties:** a minimum of 4, otherwise the value of the ball on or the
  highest-value ball involved, whichever is higher, up to 7. Fouls that can happen in a
  digital game:
  - failing to hit the ball on first;
  - potting a ball not on;
  - in-off;
  - hitting two balls simultaneously when not allowed;
  - push shot;
  - failing to nominate where required;
  - a free-ball snooker where not allowed;
  - and the rest as per the rulebook.
  Fouls that only exist physically (touching balls with a hand, both feet off the floor,
  playing before the balls stop) are not applicable.
- **After a foul:** the incoming player chooses to play from where the balls lie or to
  have the offender play again.
- **Free ball:** offered when the incoming player is snookered after a foul, as the
  rulebook defines it. The free ball counts as the ball on. A snooker behind a free ball
  is a foul, except when only pink and black remain.
- **The miss rule:**
  - Foul and miss are called per the rulebook, including when a miss is not called
    because snookers are required.
  - The non-offender may have the balls replaced to their pre-shot positions, from the
    stored pre-shot state, and the offender plays again.
  - The **three-miss rule**: warning after the second miss, frame awarded after the third,
    where the rulebook's condition applies.
- **Re-spotted black** when scores are tied after the final black. A coin toss decides
  who plays from hand.
- **Touching ball:** the cue ball must be played away. No penalty for moving the touching
  ball when it's the ball on or could be on; otherwise per the rulebook.
- **Conceding** a frame or the match.
- **End of frame and match:** best of N frames.

### 9.1 Referee judgement, where the rulebook relies on a human referee
- **Miss: "best possible attempt to hit the ball on."** The rulebook's objective case
  applies first. Where it doesn't, a deterministic heuristic decides using geometry and
  the shot record, such as the closest distance the cue ball came to the ball on and
  whether a direct path existed. All thresholds are tunable. The same function judges
  the human and the AI.
- **Push stroke:** detected when the cue ball is very close to an object ball at the
  strike and the contact and speeds show the tip would still be on the cue ball.
  Thresholds are tunable and tested.
- **Stalemate:** if no ball has been potted for a long stretch of safety exchanges, the
  game offers a re-rack, which the player can accept or refuse. The AI's answer follows
  its level. Thresholds are the builder's call.

### 9.2 Rules engine contract
Pure, deterministic C++. Input: the pre-shot state (balls, scores, ball on, nomination,
miss count, player) and the shot record. Output: the post-shot state, a list of
fouls/events with reasons, and the decisions available to the next player. The
match UI, the AI and the tests all go through this one engine.

## 10. Computer opponent. **Owner:** plays like a real player of that standard

### 10.1 Levels. **Claude's call:** five levels

| Level | Name | Pot success, medium shots | Long-pot success | Typical break when in | Tactics |
| --- | --- | --- | --- | --- | --- |
| 1 | Beginner | ~35% | ~10% | under 10 | Rarely plays safe, poor position, mostly centre ball |
| 2 | Club | ~60% | ~25% | 10–20 | Basic safeties, simple stun/screw |
| 3 | Amateur | ~80% | ~45% | 20–40, occasional 50s | Solid safety, uses side, plans a few shots ahead |
| 4 | Pro | ~90% | ~65% | 40–70, occasional centuries | Strong safety, lays snookers, escapes with swerve and cushions |
| 5 | World class | ~95% | ~75% | regular big breaks and centuries | Full tactical play, snookers, score-aware risk, concedes hopeless frames |

These targets are measured automatically (section 16.4). Each level must beat the level
below it clearly over a long series. **Claude's call:** at least 70% of frames.

### 10.2 How it plays a shot
1. **Generate candidates:**
   - pots: every ball on × every pocket, including plants, doubles off cushions and
     kicks where plausible;
   - safeties: thin contacts and returns to baulk, plus snooker attempts;
   - escapes when snookered: direct paths, one- and two-cushion paths, swerve and masse
     at higher levels;
   - break-off options.
2. **Find the shot parameters** for each candidate: aim, speed, spin and elevation, by
   searching with the noise-free simulator to hit the intended contact and aim for good
   position.
3. **Estimate success** by simulating several noisy versions of each candidate with that
   level's error model.
4. **Score each candidate:**
   - (probability of potting × (ball value + value of the position left)) plus
     (probability of missing × value of what's left for the opponent);
   - safety value comes from the opponent's best option afterwards, using a fast
     heuristic and not a full search;
   - higher levels look further ahead, planning break-building and choosing which red to
     take for position on the black.
5. **Adjust for the score situation:** points remaining, snookers needed and frame score
   change how much risk it takes.
6. **Execute:** the chosen parameters plus this level's random error, sampled once, then
   simulated by the same physics the player uses.

### 10.3 Error model (human-like, per level)
- **Sources of error:** aim error (angle), speed error (percentage), tip-position error,
  and extra error for long shots, thin cuts, raised cue and awkward cueing near cushions.
- **Shot choice:** lower levels also choose worse, with shallower search, less safety
  play and more unforced misjudgements, not just worse execution.
- **Calibration:** all error parameters live in a data file per level and are calibrated
  to the section 10.1 targets.

### 10.4 Decisions besides shots
- After a foul by the player: play on, put the player back in, take a free ball, or have
  balls replaced after a miss.
- Re-rack offers, conceding (top levels, only when the frame is genuinely hopeless) and
  re-spotted black.

### 10.5 Presentation
- Before striking, the AI's cue lines up the shot and feathers like a real player. There
  is a natural pause of at least ~0.6 s, so even instant decisions don't feel robotic.
- The camera shows the AI's shot from a pleasant angle behind its cue.

### 10.6 Thinking-time budget
- Decisions arrive within **≤ 1.5 s** on the iPhone at level 5, and faster at lower
  levels.
- If the search would run longer, it is cut down by the time budget, so it is anytime
  and never freezes.
- The opponent runs off the main thread.

## 11. Modes and flow

### 11.1 Screens
- **Home:** Play vs Computer · Training · Stats · Settings.
- **Match setup:**
  - opponent level (1–5);
  - match length: best of 1, 3, 5, 7, 9 or 11 frames;
  - who breaks first: random, you, or the computer (default random); alternates
    afterwards.
- **In match:**
  - a minimal scoreboard: both scores, frames, current break, points remaining,
    "snookers required" when relevant, and the ball on;
  - a pause button with Resume, Concede frame, Concede match, Settings and Quit;
  - foul decision prompts, ball-in-hand placement and nomination;
  - after each frame, a short summary (frame score, highest break, pot success);
  - at the end, a match summary.
- **Training:** Free table · Routines (section 12).
- **Stats** (section 14).
- **Settings** (section 15).

### 11.2 Flow rules
- **No interruptions.** No pop-ups beyond the rule decisions the game requires.
- **Replay** is reachable after any shot via one button (section 12.3).
- **Quitting mid-match** asks to confirm, and the match is discarded. **Owner:** no resume.

## 12. Training. **Owner:** all four parts

### 12.1 Free table
- **Starting setups:** full rack, empty table, or a saved custom setup. **Claude's call:**
  save and load custom positions.
- **Edit mode:** drag any ball anywhere legal, add or remove balls, and place the cue
  ball anywhere.
- **Shot controls:** retake (restore the exact pre-shot state), plus undo and redo
  across the session's shots.
- **No rules or fouls**, but the shot breakdown and replay are always available.

### 12.2 Routines (each has a best score saved). **Claude's call:** this set
1. **The Line-up:** the 15 reds in a line up the centre of the table between the colour
   spots, the colours on their spots. Score: the break made until the first miss.
2. **Clear the Colours:** colours on their spots, cue ball in hand in the D. Pot them in
   order. Score: points (27 maximum).
3. **Long Pots:** ten attempts from set positions with the cue ball in or near the D and
   the object ball around the blue or pink area, into the top corner pockets. Score:
   pots out of 10.
4. **Red–Black:** reds placed around the black spot. Pot red, then black, repeatedly.
   Score: the break made.
5. **Position drills:** pot a ball, then land the cue ball in a marked target zone. There
   are stun, screw, follow and side versions. Score: points by zone accuracy over ten
   shots.
6. **Break-off practice:** score depends on where the cue ball finishes near the baulk
   cushion and how few reds come loose.

The builder can tune the exact setups and scoring.

### 12.3 Shot replay
- Available after any shot, in any mode.
- **Speeds:** 0.1×, 0.25×, 0.5× and 1×, plus scrubbing.
- **Cameras:** behind the cue, top-down, free orbit and follow-ball.
- It plays the stored shot record, so the replay is exact.

### 12.4 Shot breakdown
An overlay drawn from the shot record:
- where the tip struck the cue ball, the elevation, and the cue speed or power;
- the cut angle;
- the cue-ball path, coloured by state (sliding, rolling, spinning);
- object-ball paths;
- collision and cushion points;
- measured throw and squirt angles.

It is always available in training. In matches it is available from the replay screen.
**Claude's call:** whether it opens automatically after each training shot is a setting,
off by default.

## 13. Look, sound and feel

### 13.1 Visual style. **Owner:** clean and minimal
- **Scene:** a realistic table on a plain, softly lit dark backdrop, with no room, crowd
  or arena. The camera framing keeps the focus on the table.
- **Materials:** realistic PBR (physically based) materials for the cloth (subtle nap
  texture), wooden rails, cushions, leather pockets, balls with clear-coat reflections,
  and the cue.
- **Lighting:** soft contact shadows under the balls from a single overhead table light.
- **Spin shown on the balls:** balls carry small markings, so spin is visible on the cue
  ball. The cue ball has one or more small spots, as on TV-style practice balls; the
  builder decides, and the owner can say if he dislikes them.
- **Assets:** built procedurally in code or Godot, or from CC0 (public-domain) sources,
  with licences recorded (section 18).

### 13.2 Sound. **Owner:** table sounds plus haptics, no music or voice
- **Sounds:** ball–ball clicks, cushion hits, pocket drops and rattles, cue-tip strike,
  miscue, and a faint ball roll.
- **Variation:** volume and pitch vary with impact speed, with random variation so
  repeated sounds don't sound identical. Sounds are panned by position relative to the
  camera.
- **Timing:** all of it is driven by events in the shot record.

### 13.3 Haptics
- iPhone vibration on cue strike, ball contacts and pockets, scaled by impact strength.
- **Claude's call:** use Core Haptics transient events through a small iOS plugin if
  Godot's built-in vibration API can't give crisp, short pulses on iOS 26.
- Haptics can be turned off in settings.

### 13.4 Frame rate and battery
- **Frame rate:** the target is 120 fps on ProMotion if Godot 4.7 on iOS supports it;
  otherwise 60 fps locked. The builder verifies this and records the result.
- **Idle:** when nothing moves and the player isn't touching the screen, the game
  throttles rendering to save battery.
- **Heat:** a 30-minute session must not visibly heat the phone. The owner reports this.
- **Launch:** under 3 s to the home screen.

## 14. Saving. **Owner:** stats only

Saved to `user://` as versioned JSON, with a migration path:
- **Per level:** frames and matches won and lost, highest break, number of 50+ and 100+
  breaks, pot success overall and for long pots, and safety success (the opponent
  doesn't pot on the next visit).
- **Overall:** highest break and total centuries.
- **Routine best scores.**
- **Free-table saved setups.**
- **Settings.**

There is a "Reset stats" option with confirmation. A match in progress is never saved.

## 15. Settings
- Strike mode: finger stroke or power bar.
- Guide lines: 0–3.
- Fine-tune sensitivity; aim drag sensitivity.
- Stroke wander adds error: on or off.
- Spin and elevation reset after each shot: on or off.
- Shot breakdown after each training shot: on or off.
- Left-handed layout.
- Sound volume; haptics on or off.
- Frame rate cap, if 120 Hz is supported.
- Reset stats.

## 16. Testing and verification (all done by agents)

### 16.1 Core tests (native Windows, no Godot)
- Unit tests for geometry, event solvers (quadratic and quartic roots, including edge
  cases), and state transitions.
- The calibration suite C1–C14 (section 7.11), run as tests with tolerances. A
  calibration report (plots of rebound angles, throw, screw distances) is generated for
  the record.
- A determinism and robustness fuzz of 100k random shots (C13, C14).
- **Performance benchmarks:** shot simulation time and AI decision time, with
  regression alerts.

### 16.2 Rules tests
One scenario test per implemented rule clause: a table state, a scripted shot or shot
record, then the expected fouls, points and next-player options. This is the bulk of the
rules verification and is written from the rulebook text.

### 16.3 Game tests (Godot, headless where possible)
- GDScript tests for flow, scoring display, settings and save/load including migration.
- **Input tests:** scripted touch and mouse event sequences fed into the input layer, for
  example "drag from A to B, fine-tune +0.3°, stroke at 2.5 m/s", asserting the resulting
  shot parameters. This verifies the controls without a human.

### 16.4 AI calibration
- A headless tournament runner (native) plays thousands of frames level-vs-level and
  level-vs-itself.
- It reports pot success, long-pot success, average break, century rate, safety success,
  foul rate and win rates.
- The targets from section 10.1 are asserted with tolerances, plus decision-time
  measurements.
- Agents also play through AI shot logs to spot shots that look implausible.

### 16.5 Debug harness (debug builds only, compiled out of release)
- Launch arguments and an optional local command channel, for example a localhost socket
  enabled only in debug Windows builds. With them an agent can:
  - load a table position from JSON;
  - play a shot with exact parameters;
  - switch screens;
  - set any setting;
  - dump the full game state as JSON;
  - capture a screenshot to a file;
  - quit.
- **Why it is needed:** Godot draws its own UI, which desktop automation tools can't read,
  so this harness is how agents drive and inspect the real running game on Windows.
- **Demo launch arguments:** the same mechanism provides arguments such as `-DemoScreen
  match -DemoPosition <name>` for the iOS simulator screenshots in CI, following the
  ios-testflight pattern.

### 16.6 On-device checks
- **Main channel:** on-device numbers reach agents through the phone link's logs
  (section 16.10): fps, frame times, shot simulation time, AI think time and thermal
  state.
- **Overlay:** TestFlight builds also have a hidden debug overlay, opened with a
  three-finger tap, that shows the same numbers live. It's for when the link is down or
  the owner wants to look.

### 16.7 Visual checks
- Agents capture Windows and iOS-simulator screenshots of every screen and key
  situations, and review them before any owner session.
- Screenshot comparisons catch visual regressions.

### 16.8 CI (GitHub Actions)
- **Windows job:** builds the core and runs core tests, rules tests, a short fuzz and a
  short AI tournament. It also exports the Godot project to check that it builds.
- **iOS compile check:** builds the GDExtension for iOS, exports the Xcode project, builds
  for the simulator, launches the app and takes screenshots.
- **TestFlight workflow:** signed build and upload, dispatched only (section 17).

### 16.9 Definition of done for any change
- Tests green.
- Screenshots reviewed by the agent.
- Nothing regressed in benchmarks.
- New failure modes are logged (section 16.10).
- For feel changes, the owner has played it.

### 16.10 Logging and the phone link. **Owner:** logs and live tuning, detailed enough to find a problem from one occurrence

**The rule:** any problem the owner hits must be diagnosable, and every shot reproducible
on the PC, from the logs alone, the first time it happens. That means no "can you do it
again?" and no screenshots from the owner unless he wants to send one.

#### What is logged
JSONL, one event per line: `ts`, `session`, `build`, `platform`, `subsystem`, `level`,
`event`, `data`. The event names and their fields live in a schema file
(`docs/log-schema.md` or similar). Every subsystem logs against that schema, and a test
checks that emitted events match it.
- **Every shot:**
  - the exact pre-shot table state: every ball's position as full-precision doubles, plus
    the scores, ball on, nomination and miss count;
  - the shot inputs: aim, tip offset, elevation, cue speed and strike mode, plus the raw
    gesture samples for finger strokes and fine-tune;
  - the seed and the parameter hash;
  - the result: events count, balls potted, final positions, simulation time;
  - the rules engine's decision with its reasons.
  Together these are a **repro bundle**: `tools/sim_cli replay <bundle>` re-runs the shot
  on the PC and reports any divergence from the logged result.
- **Every AI decision:**
  - the top candidates with their scores and success estimates;
  - the chosen shot, the error actually sampled and the think time;
  - whether the time budget cut the search short.
- **Physics anomalies** at warn level, each with a repro bundle: event-loop guard
  triggered, near-zero-time event chains, solver fallbacks, energy gain, a ball inside a
  ball or cushion, a NaN.
- **Performance:**
  - per second: a frame-time summary (average, p95, max, dropped frames);
  - memory and iOS thermal state on change;
  - any frame that took more than 50 ms, with what was running.
- **Lifecycle and UI:** app start (build, device, OS, settings), screen changes,
  settings changes, app backgrounding, saves and loads.
- **Errors:** every caught error and GDScript or C++ assertion, with its context.

#### Where logs go
- **On the device:** a ring buffer of recent sessions in `user://logs/`, capped in size.
  Logs survive a crash and upload when the link next connects.
- **On Windows:** the same JSONL is written straight to `logs/<date>/<session>.jsonl` in
  the repo. That folder is gitignored.

#### The phone link (TestFlight builds; owner-only app, so on by default, switch in settings)
- **PC side:** `tools/devlink.py` (Python, run with `uv`), modelled on BiliFast's
  `tools/logserver.py`. It advertises itself over Bonjour on the local Wi-Fi and:
  - receives batched logs into `logs/<date>/<session>.jsonl`, with a coloured live tail;
  - serves the current physics parameters and AI level files at `GET /params`, re-read
    from disk on every request, with their hash.
- **Phone side:**
  - finds the PC via Bonjour. **Claude's call:** a small iOS plugin using
    `NWBrowser`, the same native plugin as the Core Haptics fallback if one is needed.
  - uploads logs in batches;
  - checks `/params` every few seconds. New parameters apply **at the next shot, never
    mid-shot**, and the change is logged with both hashes.
- **Info.plist** gets `NSLocalNetworkUsageDescription` and `NSBonjourServices`. iOS
  asks the owner once for local network access.
- **Without the link** (no PC, other Wi-Fi), the game plays normally on its built-in
  parameters and keeps logs for later.
- **Tuning loop:** the owner says "screw feels too strong". An agent edits the parameter
  file on the PC, and the owner's next shot uses it. Once he's happy, the value is
  committed with a regression test, and the next build embeds it.
- **Agent requests (Claude's call: read-only):** agents may ask the phone app for a state
  dump or a screenshot over the link. They never play shots or change anything on the
  phone while the owner is using it.

#### Determinism across machines
- The core builds with `-ffp-contract=off` and no fast-math on every platform. It avoids
  standard maths functions whose results differ between platforms where the outcome
  depends on them, or wraps them.
- Replays are bit-identical on the same platform. Across phone and PC they are expected
  to match to within tiny tolerances. `sim_cli replay` reports the first diverging event
  if they don't.

## 17. Build and ship

### 17.1 Windows (the test bed)
- **Toolchain:** Godot 4.7.1 editor, MSVC (VS 18 Build Tools), CMake, Python and SCons.
  Agents install anything missing.
- **Debug build:** a one-command build of the GDExtension, then running the game, with
  the hot-reloadable physics parameters.
- **Playable build for the owner:** a Windows export with a desktop shortcut or a
  one-line launch command, so he can play the latest build without any tools.

### 17.2 iOS build path (on GitHub's macOS runner)
1. Build the core and the GDExtension for iOS arm64 (device) and the simulator, as an
   `.xcframework`.
2. Download the pinned Godot 4.7.1 macOS editor and iOS export templates. The URLs are
   pinned and checked against their checksums.
3. Export the Godot project headlessly to an Xcode project, using an iOS export preset
   committed in the repo.
4. **Signed build:** follow the ios-testflight skill's flow for a committed or generated
   Xcode project: patch the bundle id and settings, then fetch signing files. After that
   comes `xcodebuild archive` and export, the upload, then waiting for VALID.
5. **Compile check:** the same steps without signing, then simulator launch and
   screenshots.

### 17.3 Pipeline-critical settings (from the ios-testflight skill)
These are carried into the Godot export preset and any post-export patch step:
- the build number from CI;
- `ITSAppUsesNonExemptEncryption = false`;
- all four iPad orientation entries in Info.plist (required at upload even for an
  iPhone-only app);
- landscape-only for iPhone;
- `NSLocalNetworkUsageDescription` and `NSBonjourServices` for the phone link
  (section 16.10);
- an app icon at 1024 px with no alpha;
- no bundled scripts;
- the shared scheme;
- iOS 26 SDK on the latest Xcode on the runner.

### 17.4 Owner one-time setup (the only non-playing owner work)
Following the ios-testflight skill's `manual-steps.md`:
- create the App Store Connect record "One Four Seven" (§3) and add himself as an
  internal tester (§4);
- create the `testflight` environment and its secrets on the repo (§5);
- install from TestFlight the first time (§6).

Agents give the exact click paths when the time comes and verify each step themselves
where they can. No capabilities beyond the defaults are needed. If the Core Haptics
plugin is used, it needs no entitlement.

### 17.5 Ship loop
As in the skill: iterate on `dev` with the compile check, then fast-forward `main` and
dispatch the TestFlight build. Each build to the owner comes with its build number, what
changed and a numbered play checklist.

## 18. Assets and licences
- Code: Godot (MIT), godot-cpp (MIT), test framework (e.g. doctest, MIT).
- pooltool: used as a reference; its code is reused only if its licence allows it, with
  attribution.
- Textures, models and sounds: generated by us or CC0. No assets with attribution or
  non-commercial clauses unless the owner agrees. Every external asset is listed in
  `THIRD_PARTY.md` with its source and licence.
- No trademarks: no WPBSA/World Snooker branding, real player names or real venues.
  Using the rules is fine.

## 19. Risks and how they are handled

| Risk | Handling |
| --- | --- |
| Physics feels wrong even when the tests pass | Calibration suite catches gross errors; owner feel sessions with checklists decide the rest; one parameter file makes tuning fast |
| Break-off and clusters behave unrealistically or hang | Seeded micro-gaps, event-loop guards, a fuzz of 100k shots, a dedicated owner check |
| Pocket geometry unknown (WPBSA templates private) | Best public approximation in one data file; owner judges tightness |
| GDExtension plus Godot iOS export in CI is fiddly | Done early (the first milestone), before features pile up; simulator screenshots prove it |
| AI too slow on the phone | Anytime search with a hard time budget; worker thread; on-device overlay measurement |
| Fine aiming on a touchscreen feels imprecise | Fine-tune control, zoom-scaled drag, scripted input tests, early owner session |
| Haptics too mushy through the Godot API | Core Haptics plugin fallback |
| 120 Hz unsupported | 60 fps locked, recorded |
| Touch gestures can't be fully tested on Windows | Mouse mapping for logic; owner checks real gestures on the phone |

## 20. Summary of decisions

**Owner:**
- Godot;
- single-player only: vs computer with difficulty levels, plus training;
- drag + fine-tune aiming;
- both strike modes;
- adjustable guide lines: levels 0–2 straight geometry; level 3 simulated at the set
  power or the previous stroke's pace;
- spin widget + elevation;
- AI that plays like real players;
- full official rules;
- clean minimal look;
- behind-the-cue camera;
- landscape only;
- table sounds + haptics;
- nap modelled with fixed conditions;
- mouse mapping on PC;
- stats saved, no match resume;
- phone link with logs and live tuning, logs detailed enough to diagnose any problem from
  one occurrence;
- one run per milestone, shipping to TestFlight whenever the milestone changes the phone;
- name "One Four Seven".

**Claude's calls (overrule any):**
- custom C++ physics, rules and AI as a GDExtension, with GDScript for the rest;
- the Mobile renderer on both platforms;
- five AI levels and their targets;
- the routine set;
- custom free-table setups;
- the optional stroke-wander error;
- guide default at level 1;
- the left-handed layout;
- cue-obstruction-forced elevation;
- a public GitHub repo;
- a hidden on-device debug overlay;
- the Core Haptics fallback.

## 21. Milestones (built by Claude Code agent teams)

Every milestone is built by an agent team. The owner's only part is the **Owner plays**
line, plus the one-time Apple setup in M0.

### How each milestone runs
Every milestone runs through the project skill
`.claude/skills/running-a-milestone/SKILL.md`. **Owner:** one milestone is one run.
1. **Plan:** the lead session shows one plan screen. The owner's go is his only stop.
2. **Contracts commit:** the shared pieces go in first.
3. **Build waves:** parallel builder agents in worktrees, with independent test agents
   for anything sourced from papers or the rulebook.
4. **Merge:** each branch merges as it lands.
5. **Checks:** a parallel read-only check round.
6. **Fixes:** a fix round.
7. **Ship:** a Windows build, plus TestFlight whenever the milestone changes something
   on the phone. **Owner**

The owner's feedback after playing is read together with that session's logs, tuned
live over the phone link where possible, then committed with regression tests.

The section 16.4 (AI calibration) and section 16.10 (logging) tooling is built early,
so every later milestone is diagnosed from logs rather than from reproducing problems.

### M0 — Foundations and the pipeline, end to end
The goal is to prove the whole Windows → iPhone path before any feature exists.
- **Workstreams:**
  - repo, layout and toolchain: install SCons, pin godot-cpp, add the test framework;
  - `core/` skeleton and native test runner;
  - GDExtension binding loaded by Godot 4.7.1 on Windows;
  - Windows CI;
  - iOS compile check: GDExtension `.xcframework`, headless Godot export, simulator
    launch and screenshot;
  - TestFlight workflow and `testflight.py`, with a dry run;
  - `scripts/gate.py`: every local check in parallel lanes, with `--quick` for the edit
    loop. A pass on a clean tree is recorded so `testflight.py` can see that the shipped
    commit passed;
  - `scripts/build_windows.py`: the playable Windows export;
  - the logging framework and log schema (section 16.10) in both `core/` and GDScript,
    with schema tests;
  - `tools/devlink.py` receiving logs from Windows, plus the iOS Bonjour plugin and log
    upload, with `/params` served.
- **Gate:**
  - a placeholder build showing a static table on screen reaches TestFlight as VALID;
  - its startup logs arrive in `logs/` on the PC over the phone link.
- **Owner:** the one-time Apple setup (section 17.4). Then install the placeholder build
  on the iPhone and confirm it opens.

### M1 — Physics core
The goal is the complete simulator (section 7), with no game around it yet.
- **Workstreams:**
  - event engine, ball states and motion equations, and the shot record;
  - ball–ball collisions with throw and spin transfer;
  - cushions and pocket jaws using the Mathavan model, plus pocket capture and table
    geometry data;
  - cue strike: squirt, swerve, miscue, cue obstruction;
  - nap;
  - an independent calibration and test team for C1–C14, the 100k fuzz, benchmarks and
    the calibration report;
  - a native simulation CLI, including `replay <bundle>` with a divergence report;
  - physics anomaly logging with repro bundles.
- **Gate:**
  - C1–C14 green;
  - shot simulation ≤ 2 ms;
  - calibration report reviewed;
  - the parameter file documented with sources.
- **Owner plays:** nothing yet. Agents send the calibration report's highlights in plain
  words, with charts.

### M2 — The feel build: a playable table on Windows and iPhone
The goal is a real table you can shoot on, so the physics and controls can be judged.
- **Workstreams:**
  - table, balls and cue rendered with the Mobile renderer: procedural or CC0 assets,
    spin visible on the balls;
  - shot playback from the shot record;
  - controls: drag aim, fine-tune, spin widget, elevation, both strike modes, guide
    levels 0–3, the Windows mouse mapping, and scripted input tests;
  - behind-the-cue camera with pinch and tilt;
  - free table mode: edit, retake, undo and redo;
  - first table sounds and haptics;
  - the debug harness and the on-device overlay;
  - per-shot repro bundles logged from the game;
  - live parameter loading from `/params`, applied at the next shot.
- **Gate:**
  - input tests green;
  - screenshots reviewed;
  - Windows playable build ready;
  - TestFlight build VALID.
- **Owner plays:** a feel session on Windows, then on the iPhone, with the section 7.12
  checklist and a controls checklist. This is the most important checkpoint in the
  project. Agents keep tuning live over the phone link until the owner says the table
  feels real enough to build on.

### M3 — Rules and matches
The goal is full official rules and complete match flow.
- **Workstreams:**
  - rules engine from the WPBSA rulebook;
  - an independent rules-scenario test team, one test per clause;
  - referee judgement heuristics (section 9.1);
  - match screens and flow: setup, scoreboard, foul decisions, ball in hand,
    nomination, free ball, miss and replace, re-spotted black, concessions, summaries;
  - a stand-in simple opponent to drive matches.
- **Gate:** all rules scenarios green, plus a full best-of-3 match played start to finish
  through the harness.
- **Owner plays:** a few frames against the stand-in opponent, checking that the referee
  calls fouls, free balls and misses the way a real referee would.

### M4 — The computer opponent
The goal is five levels that play like real players (section 10).
- **Workstreams:**
  - candidate shot generation and parameter search;
  - evaluation: position play, safety and score-aware tactics;
  - error model per level;
  - the tournament runner and calibration to the section 10.1 targets;
  - performance: worker thread, anytime search, think time on device;
  - presentation: cue addressing, feathering, camera.
- **Gate:**
  - section 10.1 targets met within tolerance;
  - each level beats the one below in at least 70% of frames;
  - level 5 decides within 1.5 s on the iPhone, shown on the overlay.
- **Owner plays:** at least one match at each level. Does each one feel like a real
  player of that standard? Do the safeties and snookers look human?

### M5 — Training, replay and stats
The goal is everything outside matches.
- **Workstreams:**
  - routines 1–6 with best scores;
  - shot replay with speeds and cameras;
  - shot breakdown overlay;
  - stats and saving with migrations;
  - the complete settings screen;
  - custom free-table setups.
- **Gate:**
  - save and load tests green, including migration;
  - every screen screenshotted on Windows and the simulator;
  - the routines' scoring verified through the harness.
- **Owner plays:** every routine, replay and breakdown after a few shots, then a look at
  the stats after some matches.

### M6 — Polish and the finished game
The goal is a game that's finished, not just working.
- **Workstreams:**
  - visual polish: materials, lighting, ball markings, the app icon;
  - final audio;
  - haptics, with the Core Haptics plugin if needed;
  - 120 Hz verification, idle throttling, launch time;
  - left-handed layout;
  - the edge-case sweep: long fuzz runs, rare rules situations, interrupted gestures,
    app backgrounding;
  - final physics and AI tuning from the owner's notes.
- **Gate:**
  - every suite green;
  - long fuzz and AI tournament clean;
  - benchmarks within budget.
- **Owner plays:** normal 30-minute sessions over a few days, noting anything that feels
  off, plus heat and battery. The project is done when the owner signs off.
