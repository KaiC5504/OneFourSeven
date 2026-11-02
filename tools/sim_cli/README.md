# ofs_sim_cli

The physics core on the command line (spec 16.1, 16.10, 21 M1): run one shot, replay a logged
shot, fuzz thousands of random shots, time the benchmark shots and write the calibration data.

Build it with the core (`cmake --build build/windows --config Release --target ofs_sim_cli`); it
lands in `build/windows/tools/sim_cli/<config>/`. Every command reads the physics parameters from
`core/data/table_params.json` and the cushion outline from `core/data/table_shape.json` unless
`--params P` or `--table T` names other files, and reports every problem it finds rather than
stopping at the first.

Exit codes: 0 when everything passed, 1 when a check failed (a divergence, a failing fuzz shot, a
benchmark over budget), 2 for bad arguments or unreadable input. `ofs_sim_cli help` lists every
option.

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
- `--update` rewrites a single `{bundle, result}` file with the replayed result and the table's
  hash, for a recorded shot (`bench_shots/`) after a deliberate physics change.

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
- Each shot fails on: an anomaly other than a solver fallback (`event_guard`, `zero_time_chain`,
  `time_cap`, `nan`, `overlap`, `energy_gain`), a non-finite value, two balls closer than 2R - 1e-9
  m (`kOverlapTolerance_m`) or a ball into a cushion or jaw, outside the playing area or unpotted
  inside a drop circle at any event time, total kinetic energy rising from one event time to the
  next, a ball not at rest at the end, or a shot still running after `--hang-seconds` (default 30).
  Every 100th shot is simulated twice and its records compared byte for byte (C13).
- Between events every moving ball's path is checked too, so a ball that passes through a jaw or
  a cushion and then drops (legal at every event time) still fails as `tunnel`. The check is exact,
  not sampled: a segment's path is quadratic, so it finds every time the ball's distance from a
  cushion line, jaw or drop circle, or its position along a line or around a jaw, reaches a limit,
  and tests one point between each two of them. It adds about 13% to the fuzz's time.
- Each failing shot is listed and its bundle written to `--out` (default `build/fuzz-failures`,
  cleared of earlier `fuzz-*.json` first) for `replay`. The summary gives failures by kind, events,
  simulation-time percentiles, shots per second and a digest of the shot inputs.

## bench

Times fixed shots against the 2 ms budget of spec 7.13: break-offs at 4.5, 7 and 10 m/s on three
seeded racks, a long multi-cushion shot, a six-ball cluster split, three pots with the colours on
their spots, a slow roll with side and nap, and the recorded pack-heavy shots in `bench_shots/`
(bundles as `fuzz` writes them, each run with the parameters it carries and named after its file):
`pack_break_off`, a break-off of 792 events, and `pack_slow_roll`, a slow roll into the pack of
2,947.

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
- `--check` fails when a median is over the budget (`--budget-us`, default 2000) or, when the
  baseline file exists (`--baseline`, default `tools/sim_cli/bench_baseline.json`), a shot's
  `scaled_us` is more than 25% plus 50 us over its baseline. The 50 us keeps scheduler noise on
  the 10-30 us shots from failing it. A shot missing from the baseline shows `(new)` and isn't
  compared. A baseline from another machine (CPU name and logical core count) or another build
  type, or one written before baseline version 2, isn't compared at all; a note says why.
- `--write-baseline` records this run as the baseline, with the machine, the build type and the
  probe's time. Write it from a Release build on the PC the gate runs on.
- The gate runs `bench --check` on this PC. On CI (`CI` or `GITHUB_ACTIONS` set to `true`) it runs
  plain `bench`, which only reports: a shared runner is neither the desktop core of the budget nor
  the baseline's machine.

## calibrate

Writes the data behind the calibration report, one JSON file per series with its columns, rows,
parameter hash and setup notes.

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
