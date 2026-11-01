---
name: running-a-milestone
description: Use when starting a One Four Seven milestone from docs/superpowers/specs/2026-11-01-one-four-seven-design.md section 21, when KaiC says "start M<n>" or approves a milestone plan, or before any milestone-sized work in this repo.
---

# Running a milestone

KaiC's words: efficiency and speed. Every build and every check is done by parallel agents. Claude plans, writes the
shared pieces, merges and ships. Building a milestone alone in the main session is the failure this skill prevents
(BiliFast's slice 3 was built solo and had to be redone).

Two rules shape everything below:
- **One milestone = one run.** KaiC approves the plan; from then on it runs straight through to a build he plays.
- **One shot.** Briefs are detailed enough that an agent lands its piece without guessing, and the logs are
  detailed enough that any problem, on the PC or on the phone, is diagnosed from its first occurrence (spec §16.10).
  A brief that says "implement the cushion model" is not detailed enough. A brief that names the paper, the equations,
  the units, the expected numbers and the test cases is.

Scripts named here (`scripts/gate.py`, `scripts/testflight.py`, `scripts/build_windows.py`, `tools/devlink.py`,
`tools/sim_cli`) are created in M0. Until they exist, M0's plan builds them.

## 1. Plan: KaiC's only stop

Read these before planning:
- the design's section 21 entry for the milestone, and every section it builds;
- CLAUDE.md's status section and the previous milestone's report;
- KaiC's open notes from earlier play sessions, and the logs of those sessions in `logs/`;
- the sources the work rests on: the papers in `docs/reference/` (Leckie & Greenspan, Mathavan 2010, Alciatore),
  the WPBSA rulebook PDF, and pooltool as a reference (check its licence before reusing code; re-deriving from the
  papers is always fine).

For anything on iOS, load the ios-testflight skill.

Show one screen:

1. **Done:** the automated gate from §21, plus KaiC's numbered play checklist. The checklist names concrete shots
   and situations ("from the blue spot, play a deep screw into the middle pocket: does the cue ball come back past
   the brown?"), never "check the physics".
2. **Contracts:** what Claude commits first (step 2).
3. **Waves and agents:** a table of wave | agent | builds | owns (files) | tests | test source. No file appears in
   two rows.
4. **Logging:** the log events this milestone adds (name, fields, when emitted), so every new failure path can be
   diagnosed from one log.
5. **Rounds:** only where this milestone differs from steps 3–7 (for example, no TestFlight build in M1).
6. **Only the phone can prove:** what KaiC should watch for on the iPhone.
7. **Decisions for KaiC:** anything only he can settle (a spec rule that turns out unworkable, a feel or product
   choice), each with your recommendation. He answers them with the go.
8. **Ship:** the Windows build, plus TestFlight with the next build number when the milestone changes anything he'd
   see or feel on the phone.

**Sizing the table:**
- Size it by files, not features: one agent per set of files that can change without the others.
- A file every piece needs (`register_types.cpp`, `CMakeLists.txt`, `project.godot`, the parameter file) belongs
  to the contracts commit or to a single owner. The others build against its signatures, which are stubbed in the
  contracts commit.
- Use up to about 8 builders per wave.

**Independent tests:** wherever behaviour comes from an outside source (a paper, the rulebook, the spec's
calibration targets), its tests are written by a separate agent. That agent owns only test files, works in the same
wave and builds against the contracts. Each test cites its source (the equation number or the rule clause).

**Waves:** a milestone is big, so plan all of its waves up front. A later wave exists only when its pieces need
another piece's real behaviour, not just its signatures. Stub generously in the contracts so more runs in wave 1.

Wait for his go. After it, run straight through to the ship step. Ask him only for a decision that is truly his.

## 2. Shared pieces (the contracts commit)

Before any agent starts, commit on `dev` everything two agents would both touch:
- C++ public headers in `core/`, with stub implementations that compile;
- binding class declarations and `register_types.cpp` registrations in `gdext/`;
- every new source file added to the build files (CMake and SCons). Agents never edit build files;
- data file schemas with starting values: the table parameters, the AI level files and the pocket geometry, each
  value with a source;
- shared test fixtures: named table positions as JSON in `core/tests/fixtures/`;
- `project.godot` changes (autoloads, input map, display settings), `export_presets.cfg` and Info.plist keys;
- scene skeletons with fixed node paths, signal names, and new events in the log schema.

`python scripts/gate.py --quick` must be green. Every brief names this commit.

## 3. Build round

Launch all builders of a wave in ONE message: `isolation: "worktree"`, background, one agent per independent piece.
Each brief contains, in order:

1. **First command:** `git log --oneline -1`. If it isn't the contracts commit, run `git merge --ff-only <id>`.
   Worktrees start on an old commit.
2. **Read CLAUDE.md.** Rules:
   - public repo: no secrets in code or logs;
   - tests never touch the network;
   - `core/` has no Godot types; it uses doubles and SI units, and no fast-math;
   - GDScript is statically typed;
   - assets are CC0 or generated, each with a line in `THIRD_PARTY.md`;
   - comments follow KaiC's style: sparse, they explain why, they don't narrate.
3. **The job:**
   - the spec sections by number;
   - for physics, the exact equations and the paper and page they come from;
   - for rules, the rulebook clauses;
   - the expected numbers and tolerances;
   - the fixtures to use;
   - the log events to emit, with their fields.
4. **Your files:** the full list. Edit nothing else, and report any needs outside it. For a shared file, name the
   exact spot to insert.
5. **Formatting:** never reformat a whole existing file. Run clang-format or the GDScript formatter only on files
   you created.
6. **Done:**
   - committed on the branch;
   - `python scripts/gate.py --quick` green;
   - a test for each behaviour;
   - benchmarks not regressed;
   - anything drawn on screen captured through the debug harness into the scratchpad and looked at;
   - every new failure path logged.
7. **Report**, under 600 words:
   - the branch and commits;
   - the change for each item, and the tests with their sources;
   - measured numbers: calibration values against their targets, timings;
   - what's not done, and needs outside its files.

## 4. Merge

Merge each branch as it finishes (`git merge --no-edit <branch>`); don't wait for all of them. Apply the agents'
needs outside their files, then run `python scripts/gate.py --quick`. Once a wave is fully merged, commit the next
wave's contracts if it needs any, then launch the next wave.

## 5. Check round

Launch all checkers in ONE message: background, read-only. Each prompt gives the diff range and the area, and asks
for verified findings only (file:line, severity, scenario, fix) under a word cap. Leave out any checker with
nothing to check in this milestone.

- **Physics review against the papers:** equations, signs, units, state transitions, and event-solver robustness
  (root tolerances, near-misses, touching balls, zero-time chains).
- **Rules review against the WPBSA rulebook,** clause by clause, including the referee heuristics in spec §9.1.
- **C++ review:** undefined behaviour, NaN and inf paths, catastrophic cancellation, determinism (no iteration
  order or uninitialized state affecting results), thread safety, performance.
- **Godot and GDScript review:** flow, input handling, signals, leaks, frame pacing, idle throttling.
- **Silent failures,** including secrets in logs and failure paths that log nothing.
- **Logging review.** For each new failure mode: could it be diagnosed from one log? Does every logged shot replay
  through `tools/sim_cli replay`? Run that check on a sample.
- **Test gaps,** with sketch tests run against the code.
- **Spec check:** the milestone entry and its sections against what was built.
- **Comments** against KaiC's comment style.
- **Statistics run, where relevant:** the full calibration suite, a 100k-shot fuzz, and an AI tournament against
  the §10.1 targets. Report the numbers, not just pass/fail.
- **Screens check:** render every screen and state the milestone touches through the harness at the phone's
  resolution (2868×1320 landscape, with the safe areas) and on the iOS simulator screenshots from CI. Compare them
  against the spec and the previous milestone. Give a verdict per screen: PASS, or FAIL with every difference listed.
- **Windows run,** by a general agent bound by the ghosthand rules:
  - load the ghosthand skill first; use no other screen tool; never `keys` with allow_focus;
  - Godot draws its own UI, which UI automation can't see. Drive the game through the debug harness (commands,
    state dumps, screenshots). Use ghosthand only for the window itself;
  - screenshots go to the scratchpad; measure idle CPU and frame times;
  - operate every control the milestone added or touched by scripted input, and confirm its effect from a state
    dump plus a screenshot. Report it as a table: control | worked | evidence. Nothing ships untested (KaiC): a
    control that fails is a finding.

## 6. Fix round

- Group the findings by file. Run one fix agent per group in worktrees, with the same brief, plus a contracts
  commit if the fixers share anything. Merge each fix as it lands.
- Re-run the check that failed (a screen, a calibration target, a tournament number) until it passes. If it still
  doesn't, report what's still failing and why.
- Then run these in parallel:
  - a final seam review of how the merged branches fit together;
  - a second Windows run.
- Fix small findings directly.

## 7. Ship

1. Add the CLAUDE.md status entry: the milestone, what works, measured numbers, and KaiC's open notes. Update the
   layout section for any new folders.
2. Run `python scripts/gate.py` on the committed tree.
3. Run `python scripts/build_windows.py` and give KaiC the launch command or shortcut.
4. **When the phone matters:** fast-forward `main`, push, then run `python scripts/testflight.py start`.
   - Check the run directly when its state matters (`python scripts/testflight.py status` or `gh run view <id>`).
   - On a failure, run `gh run view <id> --log-failed` at once.
   - No fixed sleep, no polling loop, no `gh run watch`.
5. Start `uv run tools/devlink.py` in the background unless it's already running.
6. **Report:**
   - the build number, what changed and how it works;
   - the numbered play checklist from the plan;
   - what to watch on the phone;
   - that his session's logs will stream to the PC.

## After KaiC plays

1. Read his session's logs in `logs/` before his notes. Every shot he mentions can be replayed with
   `tools/sim_cli replay`.
2. Make feel tuning live over the phone link where possible: edit the parameter file, and his next shot uses it.
3. Once he's happy, commit the value with a regression test.
4. Fixes and changes larger than tuning go through the same rounds: agents, checks, merge, ship.

## Common mistakes

| Mistake | Fix |
|---|---|
| Building a piece yourself "because it's small" | Small pieces still go to agents; you write only the contracts and post-merge fixes |
| Launching a round across several messages | One message per round (per wave) |
| Two agents editing one file | Move it into the contracts commit or give it one owner |
| An agent working on an old base | The ff-only line opens every brief |
| Waiting for every agent before merging | Merge each as it lands |
| The builder writing the tests for its own paper-sourced behaviour | A separate test agent writes them from the source |
| A vague brief ("add cushion physics") | Equations, sources, numbers, fixtures, log events, file list |
| A failure path that logs nothing | It's a finding; the logging review catches it |
| Asking KaiC to reproduce a problem | Read the logs and replay the shot; ask only if the logs can't show it, then fix the logging |
| Extra GitHub runs to check something | Gate locally; one run per round |
| Asking KaiC between rounds | He approves the plan; then run through |
