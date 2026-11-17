# The computer opponent

How spec section 10 is built. It binds the builders and the test writers alike; the headers in
`core/include/ofs/opponent/` are the interface and say the rest.

Stage 1 of M4 builds the two ends the owner asked for first: **Beginner** (level 1) and **the 147**
(perfect aim, plays for a maximum). Levels 2 to 5 and the tactics between them are stage 2. In
stage 1 every safety, escape and break-off is still the stand-in's (`standin.hpp`), played with the
level's error; the new code is the pots.

## Levels

One file each in `core/data`: `opponent_beginner.json`, `opponent_maximum.json` (`level.hpp` has
every value). The game lists them from `OfsMatch.levels()`. `id` is what the logs and the harness
use; `name` is what the game shows.

## How a stroke is chosen (`choose_stroke`, spec 10.2)

1. **Pots** (`pots.hpp`): every pot of a ball that may be played, by geometry alone.
2. **Solve** (`solve.hpp`): for a pot and a way of striking it (speed, tip offset), the aim that
   makes it on the real physics, and where every ball then stops (`Trial`).
3. **Chance** (`error_model.hpp`): the level's aim error against the window the pot forgives.
4. **Read** (`evaluate.hpp`): what the position left is worth to whoever plays it next.
5. **Choose** (`search.cpp`): the stroke with the best of chance x (points + what the leave is
   worth), within the budget; when no pot is good enough, the stand-in's safety or escape.
6. **Play** (`with_error`): the level's error drawn once and added. The game simulates the result
   with the same physics the person's strokes use.

Everything is deterministic for one `Rng` state and one budget counted in steps, so a logged
decision is made again exactly (`ofs_sim_cli opponent`). The wall clock is only a safety stop; a
decision it stopped says so (`think.stopped` is `time`) and logs the steps it had taken.

The trial simulations are the game's own: a stroke the 147 tried is the stroke that is then
played, ball for ball. The shot seed only matters to a miscue, and no level plays past
`kTipLimit` of the miscue limit.

## A pot's geometry (`pots.hpp`)

For object ball O, pocket P and cue ball C, with R the ball radius:

- **aim point** A: the point in the pocket's mouth the object ball is sent to, on the line from O
  that passes the jaws with the most room either side. The mouth is the gap between the jaw arcs
  of `TableShape` (the pocket's two `ArcCushion`s); the game's next-ball pick builds the same
  mouths in `game/scripts/play/next_target.gd` (`_mouths`, `_pot_line`), which is the reference
  for this geometry: port it, do not re-derive it.
- **ghost** G = O - 2R * unit(A - O): where the cue ball's centre is at contact.
- **cut** = the angle between (G - C) and (A - O).
- **margin** (radians at the cue): the half-angle of the mouth as O sees it, times
  `2R cos(cut) / |G - C|` to bring it back to the cue ball, and no more than the room either
  path has past its nearest neighbouring ball (`next_target.gd` `_room`). A pot is listed only
  when both paths are clear and the margin is above 0.
- A middle pocket is refused when the object ball comes at it too far off square
  (`next_target.gd` `MAX_MIDDLE_APPROACH_DEG`).
- **Along a cushion** (`Pot::along_cushion`, the one pot the game's suggestion does not list): a
  ball with no straight line into a corner, 0.12 m or more from it and no further off
  the cushion than 0.15 of its run to the cushion's end, is sent at the cushion's end and runs along it into the corner. Its margin is
  a half-width of 6.5 mm there (4 mm when it touches the cushion, rising to the full 6.5 over the
  first 4 mm off it), about the tenth percentile of 42,560
  measured strokes, brought back to the cue ball as any pot's. The cut may be up to 70 degrees.
  Nothing goes into a middle pocket this way.

`band(pot)` is `short` under 1.0 m of cue travel plus object travel, `long` from 2.2 m, `medium`
between: spec 10.1's "medium shots" and "long pots".

## Error (`error_model.hpp`, spec 10.3)

    aim sigma = aim_sigma_rad * (1 + aim_sigma_per_cut * cut) * (1 + aim_sigma_per_m * cue travel)
                * awkward_factor (when cueing is hampered or the cue is raised past 0.17 rad)
    chance    = erf(window / (sigma * sqrt 2))

`window` is the pot's geometric margin (`Pot::margin_rad`); `solve_pot` can measure the real one
(`measure_window`) but the search does not spend simulations on it. Speed error is a fraction of the speed; tip error is in each axis.
Four normals are always drawn, in the order aim, speed, tip x, tip y.

## The 147 (`maximum`, spec 10.1)

- **The maximum is on** (`maximum_on`) when the break being made, or about to start, can still be
  147: every red is on the table or was potted in this break, each followed by the black.
- While it is on, the 147 pots only what the maximum needs (`maximum_next`): a red, then the black,
  then the colours in order. Among the pots of that ball it takes the one whose leave keeps the
  maximum most alive: it plans `depth` strokes ahead, each one simulated, and beyond that reads
  the table by rules of thumb (an easy pot of the next ball needed, the black open to both top
  corners, reds opened from the pack, the cue ball not on a cushion or behind a colour).
- With no pot of the ball it needs, it does not take another ball: it plays the stand-in's safety
  or escape, with no error, and waits. The same when no break is under way and no red it could
  pot leaves the black to follow: potting one would only end the maximum.
- Once the maximum is gone (the owner, 2026-11-17) it plays on for the highest break still
  possible: the same search, taking the black after a red whenever it can and the best colour
  when it cannot. A second red potted in the same stroke costs it the black that would have
  followed (7), so it does not smash the reds for points.
- It has no error at all: the stroke played is the stroke planned.
- **Watch a 147** starts it on a table from `core/data/exhibitions.json`, the reds already open.
  The exhibition ends when its break does.

## The search (`search.cpp`, `maximum.cpp`)

What can be seen from outside, and so tested:

- **Candidates**: each listed pot (`pots`) struck each way the level can: `level.speeds` cue speeds
  on a ladder between `least_pot_speed` and the cue's most (few speeds sit in the firm middle,
  10% to 45% of the way for two, so a soft pace is among a beginner's choices; eight or more start at the least and reach the top), and
  `level.spins` tip offsets within `level.max_tip`, centre ball first. One candidate is one
  **step**. A candidate counts when `solve_pot` finds its aim and the referee
  (`rules::judge_shot` on the simulated stroke) calls no foul.
- **A level with error** (Beginner) scores a candidate
  `chance * (value + kBreakOn * (1 - w) + w * leave)`: `chance` is `pot_chance` on the pot's
  margin, `leave` is `read_position(...).worth` of the position the stroke leaves it in (0 when
  the turn passes), `w` is `level.position_weight`. It then misreads each score by
  `level.judgement_sigma` (a normal draw per candidate, from a stream of its own so the stroke's
  error draws stay four) and takes the best. When the best pot's chance as it reads it is under
  `level.safety_below`, or there is no pot, it plays the stand-in's safety, escape or break-off.
- **The 147** takes, among the candidates for the ball it needs, the one whose plan is worth
  most: the points potted along the plan plus what the last position is worth
  (`maximum.cpp`: an easy pot of the next ball needed, the black open to its corners while reds
  remain, reds open, the cue ball free). It looks `level.depth` strokes beyond this one, in
  stages: every candidate is read first, then the best few are looked into one stroke further,
  then the best of those again; when the budget stops it, the answer is the best of the deepest
  stage that finished at least one candidate. A candidate that would end the maximum (two reds
  in a stroke, a colour other than the black after a red) is not taken while another keeps it.
  With simulations to spare in its first stage it also tries the speeds between the ladder's
  rungs, and of two strokes worth the same it plays the quieter (a small charge for pace and
  spin).
- **Budget**: checked before each step but the first, which is always taken (a decision logged
  with no steps would replay with no limit). `stopped` is `done` when the search ran out of
  candidates by itself, else `sims`, `steps`, `time` or `cancelled`. The simulations may pass
  `max_sims` by the step in hand (at most 14), and the stand-in's few for a safety or escape are
  outside it. A negative `max_sims`, `max_steps` or `max_ms` is as 0: the level's own, or no
  limit. The answer is a pure function of the frame, the level, the
  seed and the steps taken, so `max_steps` set to a logged `think.steps` gives that stroke again.
  Stopped before the first step finished, it plays the stand-in's stroke with the level's error
  and says so (`fallback`).
- **Played**: `with_error(meant, level, &pot, ...)`, four draws. For the 147 `strike == meant`,
  and since nothing in it is random its stroke is the same whatever the seed. When the error
  turns the aim over a cushion the cue is raised to clear it, in `meant` as in `strike`.
- **`why.maximum`**: `on` when the level plays for a maximum and `maximum_on` holds for the frame
  it was asked about, `gone` when the level plays for one and it does not, `off` for any other
  level. With `gone`, `why.maximum_why` says which: `a red is off the table` (fewer than fifteen
  reds and no break under way), `a free ball` (the frame's first stroke is a free ball: 147 is
  what it plays for, not 155) or `the break lost it`.
- **A claim** (`claim_frame` open to the striker, which the rules offer only with the black alone
  left) is taken in place of a stroke by Beginner. The 147
  never claims while it has a pot of the ball on, the maximum on or gone: it plays for the
  break, to the last black. With no pot it takes the claim. No level concedes in stage 1.

## Choices (`decide.cpp`, spec 10.4)

After a foul, a re-rack offer and the toss a level chooses as the stand-in does
(`choose_decision`), with one exception: after the other player's foul the 147 plays itself
(`play`) whenever the position left has a pot of the ball its maximum needs, or, with the maximum
gone, any pot.

## What the tournament counts (`ofs_sim_cli tournament`, spec 16.4)

Two levels play whole frames under the rules, each stroke chosen by `choose_stroke`, each choice
by `choose_option`, judged by `rules::judge_shot`, exactly as a match in the game. Per level:

- **pot attempt**: a stroke whose `why.kind` is `pot`. **Success**: the stroke scored (the
  judgement's `points` above 0 and no foul). Reported for all attempts and by `why.band`.
- **break when in**: the mean and the median of every break of at least one point; also the
  highest break, and the counts of breaks of 50 and over, 100 and over, and 147.
- **safeties**: strokes whose `why.kind` is `safety`, as a share of all strokes.
- **fouls** per stroke, **frames won**, **strokes** per frame.
- **thinking**: per stroke the simulations run, the steps, and the time (mean, p99, max), and how
  each stopped; the count of fallbacks.
- With `--exhibitions`: level A alone plays each table of `exhibitions.json` until its break
  ends; the break made on each.

Stage 1's marks (the gate): Beginner against itself over 2,000 frames pots 30 to 40% of medium
attempts and 5 to 15% of long ones, its mean break when in is under 10, and it plays safe on
fewer than 10% of the strokes where it had a pot it could make (a pot attempt, or a safety or
escape whose `why.pots` is above 0). It has no such pot on about one stroke in ten, escapes
included (one in six before pots along a cushion were listed); that share is reported, not marked. The 147 pots every attempt it plans (no error: a planned pot that stays out is a
bug), makes 147 on every table of `exhibitions.json`, and wins at least 95% of frames against
Beginner. Its rate of maximums from real break-offs is reported, not marked.

## `opponent.decision`'s `why` for a level

A stroke: `level`; `kind` (`pot`, `safety`, `escape`, `break`); `target` (`red 3`, `black`);
`pocket` (index, -1 none); `band`; `cut_rad`; `along_cushion` (true on such a pot, absent otherwise); `chance` (as it read it); `worth` (the score it
chose by); `spec` (`speed_m_s`, `tip_x`, `tip_y`, `elevation_rad` as meant); `leave` (`cue`:
`[x, y]` where the cue ball stops as simulated, `next`: the ball it leaves itself on, `chance`:
that pot's); `plan` (the strokes planned after this one: `target`, `pocket`); `others` (up to
three next best: `target`, `pocket`, `worth`); `drawn` (`aim_rad`, `speed`, `tip_x`, `tip_y`);
`maximum` (`on`, `off`, `gone`). For a safety, escape or break-off the stand-in's `why`
(`standin.hpp`) with `level`, `drawn` and `maximum` added, and for a level with error `listed`
(the pots it had), `pots` and `declined`. A claim or concession: `level`, `kind`.

## The game

- `OfsMatch` (`gdext/src/ofs_match.h`): `configure_opponent`, `levels`, `exhibitions`,
  `opponent_think` (a worker thread), `opponent_thinking`, `opponent_take`, `opponent_cancel`,
  `opponent_choice`; `start`'s config takes `level` and `exhibition`.
- `MatchController.start(config)`: `level` (a level id; absent or `""` is the stand-in, as the
  tests and autoplay use it; the setup screen always gives one) and `exhibition` (an
  index: best of 1 from that table, the opponent at the table, the person only watching). New:
  `levels()`, `level()`, `level_name()`, `is_exhibition()`, `signal opponent_thinking(on: bool)`,
  `signal exhibition_ended(result: Dictionary)` (`break_points`, `maximum`, `why`). The stroke
  handed to `opponent_ready` gains `level`, `meant` and `think`. `harness_state()` gains `level`,
  `exhibition` and `thinking`. Autoplay for the person stays the stand-in.
- `OpponentDriver.request_shot(engine, player, seconds, level := "")` and `request_choice(...,
  level := "")`: with a level it calls `opponent_think` and polls `opponent_take` each frame,
  answering once the stroke is there and `seconds` have passed; `""` is the stand-in as before.
  A level's stroke is asked for with `max_ms` 800 (`LEVEL_MAX_MS`: spec 10.6's bound with room
  for the show). `cancel(why := "left")` also stops the thinking (`opponent_cancel`), saying
  `left`, `replaced`, `conceded` or `claimed`; pausing holds the thinking, it does not cancel it.
- `MatchController.judged_break()`: the break as the last stroke left it, kept after an
  exhibition's end clears the frame. In an exhibition nothing can be conceded or claimed.
- `OpponentShow` (`game/scripts/match/opponent_show.gd`): the cue standing, feathering and going
  through, as a clock the table scene poses the cue from.
- `CameraRig.set_watch(view)`, `watch()`, `signal watch_changed(view)` with `cue`, `above` (the
  whole table from straight above, its length across the screen, inside the keep-clear area and
  the safe area given by `set_safe_area(rect)`) and
  `round` (the overview's walk); `WatchButton` steps through them. A stroke played while watching
  from `above` or `round` is watched whole from there (`shot.camera` reason `watching`).
- Screens: `MatchSetupScreen.set_levels(levels)` and `level` in its config; `HomeScreen` has
  `watch_pressed` (button id `watch`); `ExhibitionSummary`; `Scoreboard`, `FrameSummary` and
  `MatchSummary` take `set_opponent_name(name)`; `Scoreboard.set_exhibition(on)` shows the one
  side that plays: its name, its break, the ball on and the points remaining.
- During the opponent's show, in a match and in an exhibition, the dimmed spin ball and power
  bar show its strike (the owner, 2026-11-17); while it thinks or chooses no stroke control
  shows; the person's own come back at their turn.
- Harness (`docs/harness.md`): `start_match` takes `level`; `start_exhibition` (`layout`, `seed`,
  `fast`); `watch` (`view`); `match_state` gains `level`, `exhibition`, `thinking`, `watch`;
  `--demo-screen exhibition_summary`.
