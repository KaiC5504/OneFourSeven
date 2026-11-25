# ofs_sim_cli

The physics core on the command line (spec 16.1, 16.10, 21 M1): run one shot, replay a logged
shot, fuzz thousands of random shots, time the benchmark shots, write the calibration data and
measure how wide the pockets play. From M3 also the rules: play whole matches between two stand-in
opponents with every stroke checked, and judge a log's referee decisions again. From M4 the
computer opponent: play its levels against each other and count what they do, and make a log's
opponent decisions again.

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
  `--threads` is. Each stroke's simulator seed and the seed of each of the stand-in's strokes and
  choices come from the match's own stream; the stand-in's seed is on its `opponent.decision`
  line, so a choice can be made again from the log. The referee's and the stand-in's values are
  `core/data/referee_params.json` and `standin_params.json`.
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
  - nothing logged `app.error`: the stand-in falls back on a plain stroke or the first option when
    it throws, and the run stops on that rather than playing on;
  - a frame ends within 3,000 strokes, re-racks included.
- A broken check prints one `BREACH` line (match, frame, stroke, what broke) and writes
  `match-<seed>-<match>-f<frame>-s<stroke>.json` to `--out` (default `build/match-failures`, cleared
  of earlier `match-*.json` first): the frame before the stroke or choice (`frame`) and after it
  (`after`), the stand-in's seed (`standin_seed`), the stroke (cue place, nomination, strike,
  simulator seed, the stand-in's `why`) with its repro `bundle`, `facts` and `judgement`, or the
  `decision` and its calls, and the referee's values. The run stops there with the summary of the
  frames before it and exit 1.
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
  logs, with `match_id` `sim-<seed>-<match>` and a `shot_id` per stroke (a choice has none), and
  `params_hash` on `frame.started`. A `rules.invalid` logged inside the engine carries the
  `match_id`, `frame` and `shot_id` of the stroke or choice it came from. `referee`, `replay` and
  `tools/check_logs.py` all read it. It is about 10 kB a stroke.

## referee

Judges a log's referee decisions again (spec 16.10), whether the log came from the phone or from
`match --log`.

```
ofs_sim_cli referee logs/2026-11-16/20261116-101500-a1b2c3.jsonl
ofs_sim_cli referee build/match.jsonl --line 412
ofs_sim_cli referee rulings_only.jsonl --allow-unreplayed
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
- A decision with no `shot.simulated` line of its `shot_id` (or no `shot_id`) has only its ruling
  checked: it prints `NOT REPLAYED line N shot_id S` and is counted.
- It prints one line per difference, with its line number in the log and what was logged against
  what came out, and a count at the end (`re-judged D decisions, replayed R of their shots: X
  differences, U unreadable, N not replayed`). Exit 1 on any difference, and on a decision not
  replayed unless `--allow-unreplayed` is given (a log cut down to its rulings); 2 when the log
  cannot be read, has no `rules.decision` line, has a line that is not a log line or a shot whose
  bundle cannot be used; 0 otherwise.

## tournament

Two levels of the computer opponent play whole frames under the full rules and what each did is
counted (spec 10.1, 16.4); `docs/opponent/README.md` ("What the tournament counts") defines every
count, and `tools/opponent_report.py` turns the report into `docs/opponent/report.md` and checks a
stage's marks.

```
ofs_sim_cli tournament --a beginner --b beginner --frames 2000 --seed 1 --threads 4 --report build/beginner.json
ofs_sim_cli tournament --a maximum --b beginner --frames 200 --exhibitions --report build/maximum.json
ofs_sim_cli tournament --a-file build/beginner_try.json --b beginner --frames 500 --log build/tournament.jsonl
ofs_sim_cli tournament --a maximum --frames 0 --exhibitions
```

- `--a ID` and `--b ID` name the levels (`core/data/opponent_<id>.json`); A is player 0. `--a-file
  PATH` or `--b-file PATH` reads a level file from elsewhere instead, for tuning without touching
  `core/data`. Each level is printed with its file and content hash. Any level with a file there
  can play either side. An unknown id, a file that is not a level, and the game's position reader
  (`person`, `opponent_person.json`, which is not an opponent) exit 2.
- Each frame is played as a match in the game: `opponent::choose_stroke` for the striker, the cue
  ball placed when the stroke places it, the shot simulated and judged, and every pending decision
  answered by `opponent::choose_option` and applied with `rules::decide`; a stroke that would claim
  or concede the frame is applied the same way. A frame is a best of one, so a re-rack (S3.17)
  plays the frame again within it. The referee's and stand-in's values are
  `core/data/referee_params.json` and `standin_params.json`.
- `--frames N` (default 100). Frame `i` of `--seed S` is always the same frame: its rack and lots
  come from a seed mixed from `S` and `i`, and player `i % 2` breaks, so A breaks in the even
  frames. The opponent's `Rng` for the frame's `n`th decision (strokes and choices counted
  together, from 0) is seeded from the frame's seed and `n`; that seed is the `seed` on the
  decision's `opponent.decision` line. Frames run on `--threads` (default 2, at most 4) and are
  counted in frame order, so the report and the log are the same whatever the thread count, the
  thinking times aside.
- `--max-ms X` stops each decision's thinking after `X` ms of wall clock (default 0, no clock, so
  a run is reproducible); with it set, how far each decision got depends on the PC.
- Failures, each printed (the first 100) and counted, and exit 1:
  - every check `match` makes of a stroke or a choice (the table, the turn, the scores and break,
    the judgement read twice, the logged decision re-judged, the engine refusing nothing, no
    `app.error`); the frame stops there. A decision the engine would not take is logged again at
    warn with its `error` and `pre`, as the game logs one; an exception logs `app.error` (`kind`
    `exception`, `level`, `seed`, `frame`) and its repro holds the frame it was thrown in;
  - a frame still not over after `--max-strokes` strokes (default 600, re-racks included);
  - a planned pot that stayed out: a stroke whose `why.kind` is `pot` by a level whose execution
    has no error (`aim_sigma_rad`, `speed_sigma` and `tip_sigma` all 0), which did not pot the ball
    its `why.target` names. The frame plays on.

  The first 20 are written to `--out` (default `build/tournament-failures`, cleared of earlier
  `tournament-*.json` first) as `tournament-<seed>-f<frame>-s<stroke>-<kind>.json` (an
  exhibition's `-x<table>-`): the frame before and after, the opponent's seed, the level, the
  stroke (cue place, nomination, strike played and meant, simulator seed, `why`) with its repro
  `bundle`, `facts` and `judgement`, or the choice and its calls.
- `--exhibitions` (with `--exhibitions-file F`, default `core/data/exhibitions.json`): level A
  alone plays each table from `opponent::exhibition_frame` until its break ends (the turn passes,
  a foul, or the frame ends), and the break made on each is reported. `--frames 0 --exhibitions`
  plays only the tables and needs no `--b`.
- It prints a table of the counts for A and B, each exhibition's break, the failures and the wall
  time. `--report FILE` writes them as JSON: `frames`, `seed`, `params_hash`, `table_hash`,
  `levels` (`a`, `b`: each level's values), `level_hashes`, `a` and `b` (the counts below),
  `frame_best` (below), `exhibitions` (`name`, `break_points`, `maximum`, `strokes`), `failures` (`where`, `stroke`,
  `player`, `level`, `kind`, `detail`, `repro`) and `timing` (`wall_s`, `threads`,
  `frames_per_s`). Each level's counts: `frames_won`; `strokes`; `pots` (`attempts`, `made`, and
  by `bands`: `short`, `medium`, `long`, and `unknown` for a pot whose `why` names no band);
  `breaks` (`count`, `mean`, `median`, `highest`, `fifty_plus`, `hundred_plus`, `maximums`), every
  break of a point or more, its value the frame's `break_points` as it ended; `safeties` (strokes
  whose `why.kind` is `safety` or `snooker`) and `escapes` (`escape`); those two split by whether
  the search found a stroke that
  makes a pot, `safeties_with_pot` (`why.pots` above 0: it had a pot and played safe) and `no_pot`
  (`why.pots` 0 or absent; break-offs are of kind `break`, so in neither); `fouls` (strokes that
  fouled); `points` (scored at the table, penalties received not counted); `maximum` (`visits_on`:
  visits to the table that began
  with `opponent::maximum_on`, `completed`: those that made 147); `think` per decision
  (`sims_mean`, `sims_max`, `steps_mean`, `us_mean`, `us_p99`, `us_max`, and `stopped`: how many
  ended `done`, `sims`, `steps`, `time`, `cancelled`); `fallbacks`. Everything but `think`'s times and `timing`
  is the same for the same options.
- The ladder's counts (`docs/opponent/README.md`, "The ladder"), in frames only, never in an
  exhibition. "Then" is the next stroke of the frame (a choice between is passed over) when it is
  the other player's; when the next stroke is the same player's again, or none comes (the frame
  ends, is conceded or claimed, or is re-racked), nothing followed. Each level's:
  - `tactics.safeties`: `played`, every stroke of kind `safety` or `snooker`, fouled or not;
    `then_pot_attempt`, those then followed by a pot attempt (`why.kind` `pot`); `then_potted`,
    the attempts among them that scored (points above 0, no foul); `then_no_attempt`, `played`
    less `then_pot_attempt`; `snooker_kind`, the strokes of `played` of kind `snooker`.
  - `tactics.snookers`: `laid`, strokes of any kind with no foul that passed the turn (the frame
    not over, not re-racked, nothing pending) and left the other player snookered as the engine
    calls a free ball: `rules::TableOracle::snookered` on the position left, the balls on by
    `rules::places_on`; never what the level's `why` claims. `called`, those of kind `snooker`;
    `then_fouled`, those whose next stroke fouled; `then_foul_points`, the penalties of those next
    strokes added up.
  - `tactics.escapes`: `played`, strokes of kind `escape`; `hit`, those whose judgement has none
    of the fouls for not hitting a ball on first (`no_hit`, `first_hit_not_on`,
    `simultaneous_hit`); `by_cushions`, both again by `why.cushions` (`0` to `4`, `5+`, or
    `unknown` when the `why` has none, as the stand-in's has not), only the counts that occur.
  - `tactics.break_offs`: `played`, strokes of kind `break`; `then_pot`, those followed by a pot
    attempt or by a stroke whose `why.pots` is above 0 (the other player had a pot);
    `then_no_pot`, the rest.
  - `frame_highs`: the side's own highest break in each frame it played, re-racks included, 0 when
    it made none: `frames`, how many; `typical`, their median; `fifty_plus` and `century`, the
    frames whose highest is 50 or more and 100 or more; `counts`, `[break, frames]` for each
    highest break that occurred, lowest first, so reports can be merged. In a frame of a level
    against itself each side counts its own, so this is not the typical break: the loser of a
    frame rarely makes a break when the winner made a big one.
  - `concessions` (`concede_frame` or `concede_match`) and `claims` (`claim_frame`): the choices
    the engine took, made in place of a stroke or when offered.
- `frame_best`, at the top of the report: each frame's highest break by either player (the higher
  of the two sides' `frame_highs` for that frame), frames only, never an exhibition. The same keys
  as `frame_highs`: `frames`, the frames played; `typical`, the median over them (in a report of a
  level against itself, the level's **typical break**); `fifty_plus`, `century`, `counts`. A run
  of exhibitions alone has `frames` 0. The printed table shows it as "frame's best break" and each
  side's own median as "own typical break".
- `--log FILE` writes the run's log as JSONL, the lines a game session logs: `params.loaded` (the
  physics file, then each level file), `rules.configured` (the referee's and stand-in's values and
  each level's, with its file's `hash`, as the game's binding logs them), then each frame in order
  with its `match.started` (with `level`: B's id, the game's opponent seat), `frame.started`,
  `opponent.decision` (with `level`, `seed`, `meant`, `think` and `pre_hash`), `opponent.fallback`,
  `rules.cue_placed`, `shot.simulated`, `rules.decision`, `rules.choice`, `frame.ended` and
  `match.ended` lines; an exhibition's `exhibition.started` and `exhibition.ended` lines with its
  strokes between (`why` `claimed` when the level claimed the frame, `failed` when a failure
  stopped the break). `match_id` is
  `tournament-<seed>-<frame>` (an exhibition's `tournament-<seed>-x<table>`). `referee`,
  `opponent`, `replay` and `tools/check_logs.py` all read it.

## opponent

Makes a log's opponent decisions again (spec 16.10) and compares them with what was logged,
whether the log came from the phone or from `tournament --log`.

```
ofs_sim_cli opponent logs/2026-11-17/20261117-201500-a1b2c3.jsonl
ofs_sim_cli opponent build/tournament.jsonl --line 2041
ofs_sim_cli opponent logs/2026-11-17/20261117-201500-a1b2c3.jsonl --strict
```

- Every `opponent.decision` line with a `level` (or just `--line N`) is made again; the
  stand-in's lines, which have none, are skipped and counted.
- A decision the game refused (logged at warn, with `error`) is not made again: it prints `line N
  shot  refused in the game: <error> (shot_id S)`, says whether its `pre` holds the frame it was
  decided from to rebuild it, and is counted apart. It does not fail the exit code.
- The level's and the stand-in's values are those of the last `rules.configured` before the
  decision that lists the level (`levels`, `standin`), the values the session was playing with.
  Each level is printed once: `level beginner: the log's values, the same as
  core/data/opponent_beginner.json (rules.configured at line L, hash H)`, or `the log's values
  differ from core/data's; using the log's`, the hint when a session from before a tuning is
  read. A log with no `rules.configured` (one from before M4's fix round) falls back on
  `core/data/opponent_<level>.json` and `standin_params.json` and says so. When one
  `rules.configured` lists two levels of one id (a tournament of a level against a retuned copy),
  the decision is made with each and the one that gives the logged decision is taken.
- A stroke (`kind` `shot`) is made from the `pre` of the `rules.decision` line with the same
  `shot_id`. That frame has the cue ball where the stroke placed it; when the stroke placed it
  (`cue` logged) it is put back where the engine leaves a ball in hand, the legal place nearest
  the usual one (`table::default_cue_ball_position`). The physics values are the bundle's of the
  `shot.simulated` line with that `shot_id`, the referee's that `rules.decision`'s.
- A choice (no `shot_id`) is made from the `before` of the next `rules.choice` line of the same
  `match_id` and player, which is the frame the session had reached; with nothing pending there it
  was a claim or concession in place of a stroke, made by `choose_stroke`. Its physics and referee
  values are those of the nearest stroke of the match, before it or else after it.
- When the decision has `pre_hash`, the rebuilt frame's `frame_hash` must be it; when it is not
  (`line N  frame  NOT REBUILT  the frame could not be rebuilt: ...`), the decision is not made
  again, and that is counted apart from the differences. A stroke's `strike` must also be the one
  its `shot.simulated` bundle played (`line N  struck  DIFFERS  decided ..., struck ...`).
  `params_changed: true` (the physics values changed while it thought) is noted beside any of
  these.
- Each is made with `Rng(seed)` and, when `think.stopped` is not `done`, `Think{max_steps =
  think.steps}` (otherwise no limit, and never a clock), on the table from `--table` or
  `core/data/table_shape.json`. For a line logged on this build's platform (the line's
  `platform`) `strike`, `meant`, `nominated` and `cue`, or a choice's `choice`, are compared
  exactly, JSON numbers at full precision.
- A line from another platform (the phone's `ios` read on Windows) was made with another libm,
  which rounds the last bits otherwise: nearly every solved aim differs in its last digits, and a
  candidate near a jaw can pot on one and stay out on the other. There `why.kind`, `why.target`,
  `why.pocket`, `nominated` and a choice's `choice` are compared exactly and the numbers of
  `strike`, `meant` and `cue` within 1e-9. A mismatch is printed as `line N  strike  DIFFERS
  ACROSS PLATFORMS (ios -> windows)  logged ..., remade ...` with a `think` line beside it (the
  logged and remade `candidates`, `sims`, `steps`), and counted apart; so is a frame that does not
  rebuild there. Neither fails the exit code unless `--strict` is given.
- It prints one line per difference with its line number and both values (`line N  strike
  DIFFERS  logged ..., remade ...`), `NOT REPLAYED line N: why` for a decision whose frame or
  values the log does not hold, and a count at the end (`made again R of D decisions: X
  differences, U unreadable, N not replayed, B frames not rebuilt, K struck otherwise; P differ
  across platforms; F refused in the game; S without a level skipped`). Exit 1 on any difference,
  unreadable line (a line that is not JSON, a level that does not load, a bundle from another
  table), frame not rebuilt or strike struck otherwise of this platform, and with `--strict` on
  any across platforms too; 2 when the log cannot be read or has no `opponent.decision` line; 0
  otherwise.
