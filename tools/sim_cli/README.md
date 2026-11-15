# ofs_sim_cli

The physics core on the command line (spec 16.1, 16.10, 21 M1): run one shot, replay a logged
shot, fuzz thousands of random shots, time the benchmark shots, write the calibration data and
measure how wide the pockets play. From M3 also the rules: play whole matches between two stand-in
opponents with every stroke checked, and judge a log's referee decisions again.

Build it with the core (`cmake --build build/windows --config Release --target ofs_sim_cli`); it
lands in `build/windows/tools/sim_cli/<config>/`. Every command reads the physics parameters from
`core/data/table_params.json` and the cushion outline from `core/data/table_shape.json` unless
`--params P` or `--table T` names other files, and reports every problem it finds rather than
stopping at the first.

Exit codes: 0 when everything passed, 1 when a check failed (a divergence, a failing fuzz shot, a
benchmark over budget, a frame that broke an invariant, a decision judged differently), 2 for bad
arguments or unreadable input. `ofs_sim_cli help` lists every option.

## simulate

Runs one shot and prints its events, end time, potted balls, the strike's outcome and any
anomaly.

```
ofs_sim_cli simulate core/tests/fixtures/positions/straight_1m.json
ofs_sim_cli simulate core/tests/fixtures/positions/cut_half_ball.json --strike '{"speed_m_s": 1.2, "tip_y": -0.4}'
ofs_sim_cli simulate rack:7 --seed 3 --out build/rack7.json --log build/rack7.jsonl
```

- The position is a fixture (`start`, `cue_ball`, `strike`), a bare table (`{"balls": [...]}`) or
  `rack:SEED`, the opening rack with a default break-off at 8 m/s.
- `--strike` overrides the position's strike key by key; `--no-strike` lets the balls move as the
  file gives them (set `v` and `w` there).
- `--seed` is the shot's random seed (miscues draw from it).
- `--out` writes the full shot record (`record_json`), `--log` a JSONL log with `params.loaded`,
  any `physics.anomaly` and the `shot.simulated` line: the repro bundle and its result, which
  `replay` reads and `tools/check_logs.py` accepts. Its lines' `build` is `sim_cli-<core version>`
  (plus `+<commit>` when the build defines `OFS_GIT_SHA`), and `params.loaded` names the parameter
  file by its path in the repository, or by its bare name when it is elsewhere, so a shared log
  never carries a home folder.

## replay

Re-runs logged shots and reports any divergence (spec 16.10).

```
ofs_sim_cli replay logs/2026-11-02/20261102-153012-a1b2c3.jsonl
ofs_sim_cli replay logs/2026-11-02/20261102-153012-a1b2c3.jsonl --line 214
ofs_sim_cli replay build/fuzz-failures/fuzz-1-000123.json
```

- A JSONL log: every `shot.simulated` line is re-simulated from its bundle and compared with the
  logged result (events exactly, times within 1e-9 s, potted balls, final positions within
  `--tolerance`, default 1e-6 m); it prints OK or DIVERGED with the first diverging event. Every
  `physics.anomaly` line is re-run and reported REPRODUCED or NOT REPRODUCED. `--line N` picks one
  line.
- A bare bundle is re-run and summarised; a `{bundle, result}` file (what `fuzz` writes) is
  compared like a log line.
- The bundle carries its own parameters, so the phone's values are used; the cushion outline comes
  from the local `table_shape.json` (or `--table`). A bundle names the outline it ran on by its
  `table_hash`: one from another outline is refused (an ERROR line) unless `--force-table` is given,
  and one without a `table_hash` is replayed with a warning that its table can't be checked.
- Exit 1 if any shot diverged, any anomaly didn't reproduce, any bundle couldn't be read or any was
  refused for its table.
- `--update` rewrites a single `{bundle, result}` file with the replayed result, for a recorded shot
  (`bench_shots/`) after a deliberate physics change. The bundle is written again from what it
  parsed to, so an old one comes out at the current version (`v` 2, with `params_values_hash`)
  naming the table it was replayed on; the file's other keys stay as they were.

## fuzz

The robustness and determinism checks of C12-C14 over a reproducible set of random shots.

```
ofs_sim_cli fuzz --shots 2000 --seed 1
ofs_sim_cli fuzz --shots 100000 --seed 1 --threads 8 --out build/fuzz-failures
```

- Shot `i` of seed `S` is always the same shot, whatever `--threads` is. A fifth of the shots are
  break-offs (seeded racks, the cue ball anywhere in the D, mostly a break-off strike); the rest
  are layouts of 1-22 balls at random spots, with a frozen cluster of 2-6 touching balls in a
  quarter of all shots (sometimes including the cue ball) and balls resting against cushions in
  another fifth. Strikes cover every speed up to the maximum, tip offsets out to the ball's edge
  (a fifth miscue) and elevations up to the maximum.
- Each shot fails on: any anomaly (`event_guard`, `zero_time_chain`, `time_cap`, `solver_fallback`,
  `nan`, `overlap`, `energy_gain`), a non-finite value, two balls closer than 2R - 1e-9 m
  (`kOverlapTolerance_m`) or a ball into a cushion or jaw, outside the playing area or unpotted
  inside a drop circle at any event time, total kinetic energy rising from one event time to the
  next, a ball not at rest at the end, or a shot still running after `--hang-seconds` (default 30).
  Every 100th shot is simulated twice and its records compared byte for byte (C13).
- A `solver_fallback` is a detection that returned NaN or a negative time and was dropped (a
  missed collision), a contact group's solve that didn't converge, or a cushion or jaw impact
  solve that gave up (which the friction and contact-height limits should prevent). The gate's
  100,000 shots on each seed have none. The summary still counts them on their own line.
- `--inject-anomaly KIND` adds a KIND anomaly to every shot's record before the checks: a test of
  the failure path (listing, bundle, exit code) that no real shot reaches.
- Between events every moving ball's path is checked too, so a ball that passes through a jaw or
  a cushion and then drops (legal at every event time) still fails as `tunnel`. The check is exact,
  not sampled: a segment's path is quadratic, so it finds every time the ball's distance from a
  cushion line, jaw or drop circle, or its position along a line or around a jaw, reaches a limit,
  and tests one point between each two of them. It adds about 13% to the fuzz's time.
- Each failing shot is listed and its bundle written to `--out` (default `build/fuzz-failures`,
  cleared of earlier `fuzz-*.json` first) for `replay`. The summary gives failures by kind, events,
  simulation-time percentiles, shots per second and a digest of the shot inputs.

## bench

Times fixed shots against the 2 ms budget of spec 7.13: break-offs at 3, 4.5 and 6.5 m/s (the hardest the game plays) on three
seeded racks, a long multi-cushion shot, a six-ball cluster split, three pots with the colours on
their spots, a slow roll with side and nap, and the recorded pack-heavy shots in `bench_shots/`
(bundles as `fuzz` writes them, each run with the parameters it carries and named after its file):

| Shot | Events | What |
| --- | --- | --- |
| `break_off_worst` | 317 | the slowest fuzz break-off found (6.7 ms over 1,139 events before the fix round), the bench's longest shot |
| `creeping_wedge` | 301 | a shot that leaves a ball creeping out from almost in line between two others |
| `pack_break_off` | 253 | a full 10 m/s break-off of rack 16: the most events of 480 tried on the bought table's outline, with the owner's cloth and cushions (the fuzz break-off recorded here before went into the pack off the old middle jaws and misses it off these) |
| `pack_slow_roll` | 162 | a 0.3 m/s roll into the pack (2,947 events before the fix round) |
| `pack_slow_worst` | 261 | the slowest 0.3 m/s roll into the pack found (34 ms before the fix round) |

Each file's `result` holds the events it recorded, and `replay` checks the file still gives them.
Each bundle names the cushion outline it was recorded on by `table_hash`. A recorded shot from
another outline is refused (exit 2, every such file listed), as `replay` refuses it: on another
outline it is another shot, often not the pack-heavy one it was chosen for, and its baseline times
a different event list. Re-record it with `replay FILE --update` (then `--write-baseline`), or pass
`--force-table` to time it anyway with a warning. A bundle without a `table_hash` is timed with a
warning.

```
ofs_sim_cli bench
ofs_sim_cli bench --check
ofs_sim_cli bench --write-baseline
```

- Each shot runs until it has at least 30 timings and a quarter of a second (at most 2,000);
  `--runs N` fixes the count. The table gives the median and p99 in microseconds.
- Each run of a shot is followed by a run of the same length of a speed probe: fixed
  floating-point work that shares no code with the physics. A shot's time in probe timings (the
  median over its pairs) barely moves when the PC is busy, while a slower simulator moves it;
  `scaled_us` is that time at the speed the baseline's machine had when it was written.
- On Windows the bench pins itself to the fastest kind of core (the `cores:` line). On a hybrid CPU
  such as this PC's, Windows otherwise moves the thread to an efficiency core a few seconds in, and
  there the probe slows about 2x but a pack shot only 1.5x: `break_off_worst` ran 1,815 us raw and
  938 us scaled. Pinned, it takes about 1.2 ms here, raw and scaled.
- `--check` fails when a shot is over the budget (`--budget-us`, default 2000) or, when the
  baseline file exists (`--baseline`, default `tools/sim_cli/bench_baseline.json`), a shot's
  `scaled_us` is more than 25% plus 50 us over its baseline. The 50 us keeps scheduler noise on
  the 10-30 us shots from failing it. A shot missing from the baseline shows `(new)` and isn't
  compared. A baseline from another machine (CPU name and logical core count) or another build
  type, or one written before baseline version 3, isn't compared at all; a note says why.
- With a baseline from this machine and build, the budget applies to `scaled_us`: other work on the
  PC slows a shot and its probe alike, so a busy PC doesn't push a shot over, while a shot over
  2 ms at the speed this PC had when the baseline was written still fails. With every core busy
  (a 20-thread fuzz alongside) the raw medians doubled and `scaled_us` read 15-20% low, since a
  busy hyperthread sibling slows the probe a little more than the physics; a shot that close to
  the budget is only caught on a quieter PC. Without a baseline from this machine only the raw
  median is left and the budget applies to that; the `note:` and `check:` lines say which.
- `--write-baseline` records this run as the baseline, with the machine, the build type and the
  probe's time. Write it from a Release build on the PC the gate runs on, while it is otherwise
  idle: its speed is the one the budget is checked at.
- The gate runs `bench --check` on this PC. On CI (`CI` or `GITHUB_ACTIONS` set to `true`) it runs
  plain `bench`, which only reports: a shared runner is neither the desktop core of the budget nor
  the baseline's machine.

## calibrate

Writes the data behind the calibration report, one JSON file per series with its columns, rows,
parameter hash, setup (`summary`, which the report's texts and chart labels quote) and notes, and
every anomaly of the shots behind its rows (`anomalies`: shot, kind, t, balls, detail), which the
report lists under "Data problems".

```
ofs_sim_cli calibrate --out build/calibration
uv run tools/calibration_report.py build/calibration build/calibration-report
```

| Series | What | Target |
| --- | --- | --- |
| `deflection` | cue ball deflection and separation angle against cut, rolling and stunned | C4, C5 |
| `throw` | cut-induced throw against cut at 0.5, 1.5 and 4 m/s | C6 |
| `cushion_rebound` | rebound angle and speed kept against incidence at four speeds | C7 |
| `cushion_side` | rebound angle with check, no and running side at 45 degrees | C8 |
| `squirt` | squirt against tip offset, into miscues | C9 |
| `swerve` | cue ball paths with the cue raised 0-45 degrees and side | C10 |
| `lag` | where the cue ball stops against cue speed, and the lag speed | C11 |
| `screw_follow` | screw-back and follow distance against tip height at three speeds | 7.12 |
| `break_sim_time` | simulation time of `--breaks` break-offs (default 200) | 7.13 |

Run it from a Release build: the break-off timings of a Debug build mean nothing for the budget.
`tools/calibration_report.py` turns the folder into PNG charts and `report.md`, a sentence per
chart saying what it shows and whether it meets its target.

## pots

How wide each pocket plays (spec 6.3: the owner's rule is that pots must not get harder when the
outline changes). A lone ball rolls at a pocket from `--distance` away (default 0.6 m), paced to
arrive at 0.5, 1.5 and 4 m/s, and its line is swept sideways in `--step-mm` steps (default 0.1).
The width of the band of lines that drop is the pocket's size from that angle at that pace.

```
ofs_sim_cli pots
ofs_sim_cli pots --against old_table_shape.json --out build/pots.json
```

- The corner pocket (top left) at 0, 15, 30 and 43 degrees from its diagonal, turned towards
  running along the side cushion (43 is 2 degrees off the cushion), and the middle pocket (left)
  at 0, 20, 40 and 60 degrees from square. A line's offset is how far it lies to the left of the
  one through the corner point (the middle of the mouth on the nose line for a middle pocket), so
  the bands of two outlines compare. Lines the ball could not start on, inside a cushion, are left
  out: that is what ends the 43 degree band on the cushion's side.
- A ball counts as potted only in that pocket and without leaving its neighbourhood (0.75 m)
  first: one that rattles out and drops after a trip round the table is a fluke.
- Then the same along the side cushion into the corner: the ball touching the cushion, and the
  gaps off the cushion that still drop.
- `--against OTHER` runs the same sweeps on another outline with the same parameters and prints
  both widths and their ratio (this table over the other).
- `--out FILE` writes every row as JSON: `width_mm`, `lo_mm`, `hi_mm`, the `lines` swept and
  `out_inside`, the lines inside the band that stay out.
- Exit 1 when any of its shots had a physics anomaly.

## match

The stand-in opponent (`core/include/ofs/opponent/standin.hpp`) against itself over whole matches,
as the game plays them: `choose_shot`, the cue ball placed when in hand, the stroke simulated,
judged and settled into the match, each pending decision answered by `choose_decision`. It is the
rules engine's fuzz (spec 16.2): every stroke and every choice is checked, and the first check that
fails stops the run.

```
ofs_sim_cli match
ofs_sim_cli match --frames 500 --seed 1 --threads 8
ofs_sim_cli match --frames 6 --best-of 5 --seed 3 --log build/match.jsonl --out build/match-failures
```

- Matches are best of `--best-of` (default 3) and are played until `--frames` frames (default 100)
  are done; the last match stops there. Match `k` of seed `S` is always the same match, and frames
  are counted in match order, so the summary, the log and any failure are the same whatever
  `--threads` is. Each stroke's simulator seed and the stand-in's errors come from the match's own
  stream. The referee's and the stand-in's values are `core/data/referee_params.json` and
  `standin_params.json`.
- Checked after every stroke and choice:
  - the scores change only by the stroke's points for the striker and its penalty for the other
    player, and a choice changes neither them nor the stroke count; a penalty is 4 to 7, the
    highest foul's value, never with points; the break is the points scored in a row;
  - the table: the cue ball listed once, no ball twice, no two balls overlapping, every ball on the
    cloth inside the cushions, no red back on the table; while a red remains all six colours are
    on it, and afterwards exactly the colours from the one on upwards;
  - the turn: `on`, `striker`, `decider` and `pending` agree with each other and with `options` (a
    pending frame offers its decider something and the other player nothing; a frame that is over
    offers nobody anything and has a winner and a reason), FREE BALL only after a foul, the cue ball
    in hand lying where it may be placed;
  - the points potted in a frame never exceed 147, plus 8 for each free ball potted and 7 for a
    re-spotted black;
  - judging the stroke a second time from its shot record gives the same frame (`frame_hash`), the
    frame's JSON reads back as the same frame, and `rules::rejudge` finds no difference in the
    stroke's `rules.decision` line as logged;
  - the engine refused nothing (`rules.invalid`): the stand-in's cue place, nomination and strike
    were ones it accepts, and its choice one of those offered;
  - a frame ends within 3,000 strokes, re-racks included.
- A broken check prints one `BREACH` line (match, frame, stroke, what broke) and writes
  `match-<seed>-<match>-f<frame>-s<stroke>.json` to `--out` (default `build/match-failures`, cleared
  of earlier `match-*.json` first): the frame before the stroke or choice (`frame`) and after it
  (`after`), the stroke (cue place, nomination, strike, simulator seed, the stand-in's `why`) with
  its repro `bundle`, `facts` and `judgement`, or the `decision` and its calls, and the referee's
  values. The run stops there with the summary of the frames before it and exit 1.
- The summary: frames, matches and strokes; strokes per frame (mean, longest); points per frame and
  how many of them were penalties; the highest break; the stand-in's strokes by kind (pot, safety,
  escape, break) with how many scored and how many were fouls; fouls by kind, misses, free balls,
  re-spotted blacks and re-racks; every call and every choice made; how the frames ended; physics
  anomalies (the stand-in's trial simulations included); `choose_shot`'s mean and worst time; the
  wall time. The last line is the same as JSON: `summary`, which depends only on the seed and
  the arguments (`frames_digest` is a hash over every frame's final state), and `timing`.
- `--log FILE` writes the run's log as JSONL: `params.loaded`, then each match in order with its
  `match.started`, `frame.started`, `opponent.decision`, `rules.cue_placed`, `shot.simulated`,
  `rules.decision`, `rules.choice`, `frame.ended` and `match.ended` lines, the events the game
  logs, with `match_id` `sim-<seed>-<match>` and a `shot_id` per stroke. `referee`, `replay` and
  `tools/check_logs.py` all read it. It is about 10 kB a stroke.

## referee

Judges a log's referee decisions again (spec 16.10), whether the log came from the phone or from
`match --log`.

```
ofs_sim_cli referee logs/2026-11-16/20261116-101500-a1b2c3.jsonl
ofs_sim_cli referee build/match.jsonl --line 412
```

- Every `rules.decision` line (or just `--line N`) carries the frame before the stroke, the
  nomination, the facts read from the shot and the referee's values: `rules::rejudge` judges it
  again and compares the fouls, penalty, miss and its reason, points, nomination, spotted colours
  and calls, then the hash of the frame afterwards.
- Where the log also holds the stroke's `shot.simulated` line (the same `shot_id`), the shot is
  replayed from its bundle and read again from the decision's own frame, and the facts are compared
  with the logged ones: the first ball hit, the pots (ball and pocket), the cue ball potted, a push,
  touching balls moved and the anomaly flag exactly, every resting position within 1e-6 m. The
  bundle carries its parameters; the cushion outline is the local `table_shape.json` (or `--table`),
  and a shot that ran on another outline is reported as unreadable, not compared.
- It prints one line per difference, with its line number in the log and what was logged against
  what came out, and a count at the end. Exit 1 on any difference; 2 when the log cannot be read,
  has no `rules.decision` line, has a line that is not a log line or a shot whose bundle cannot be
  used; 0 otherwise.
