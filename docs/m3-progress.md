# M3 progress: rules and matches

Plan approved by KaiC on 2026-11-15 with both recommendations: miss calls to the television
standard (every failed escape is a miss unless a player needs snookers or the ball cannot be hit),
and the person may concede a frame at any time (the opponent only when snookers are needed).

Done means: every rules scenario green, a full best-of-3 played start to finish through the debug
harness with every referee decision re-judged from its log line, a native stand-in against stand-in
run with no broken invariant, `python scripts/gate.py` green on the committed tree, and the build on
KaiC's iPhone through `python scripts/mac.py phone` with his play checklist.

The rulebook (WPBSA Official Rules of Snooker 2024-25 and the August 2024 updates, from wpbsa.com)
was not in the repository. It stays out (copyright): the main checkout's gitignored
`local/reference/wpbsa/` holds the PDFs and `rulebook.txt` (snooker: Section 2 from line 277,
Section 3 from 532, Section 4 from 1426) and `updates.txt`.

## Contracts

- `core/include/ofs/rules/`: `types.hpp` (frame state, decisions, fouls, calls), `facts.hpp`
  (what the referee saw in a shot record), `oracle.hpp` (the geometry interface),
  `referee_params.hpp`, `engine.hpp` (`judge`, `decide`, `options`, `place_cue_ball`),
  `match.hpp`, `serialize.hpp` (the JSON forms), `rules_log.hpp`; `ofs/opponent/standin.hpp`.
  `core/src/rules/names.cpp` is complete; every other source is a stub that compiles.
- `docs/rules/README.md`: how each clause is read with no human referee. It binds builders and
  test writers alike.
- `core/data/referee_params.json`, `core/data/standin_params.json`: values with sources.
- `core/tests/rules/scenario.hpp`: the shared test rig (real table, physics and referee values;
  `play` runs a stroke through simulator, `observe` and `judge`). Two globbed test targets:
  `ofs_rules_tests` (`core/tests/rules/*.cpp`, the independent scenarios) and
  `ofs_rules_unit_tests` (`core/tests/rules_unit/*.cpp`, the builders' own).
- `core/tests/fixtures/rules/`: the JSON forms in full; `scripts/sync_data.py` copies them to
  `game/data/rules/` for the game's tests.
- `gdext/src/ofs_match.h`: `OfsMatch`, registered; `OfsShotRecord::record()` and
  `OfsPhysics::physics_params()`, `table()` for it.
- `tools/sim_cli`: `match` and `referee` commands declared and dispatched.
- Game skeletons with fixed class names, signals and signatures: `MatchController`,
  `OpponentDriver` (`game/scripts/match/`), `Scoreboard`, `RefereeLine`, `ChoiceStrip`,
  `Nomination`, `CuePlacer` (`game/scripts/match/hud/`), `HomeScreen`, `MatchSetupScreen`,
  `PauseMenu`, `FrameSummary`, `MatchSummary` (`game/scripts/ui/screens/`), and
  `game/tests/support/fake_match_engine.gd`.
- Log events (`core/data/log_schema.json`, `docs/log-schema.md`): `rules.configured`,
  `rules.decision`, `rules.choice`, `rules.cue_placed`, `rules.invalid`, `match.started`,
  `match.ended`, `frame.started`, `frame.ended`, `match.call`, `match.nominated`, `match.turn`,
  `opponent.decision`.

### The table scene in wave 2

`main.gd` keeps one scene with three modes: `home`, `free_table`, `match`. The integrator adds to
`main.tscn`: `Match` (MatchController) and `Nomination` under `Main`; `Scoreboard`, `RefereeLine`,
`ChoiceStrip`, `CuePlacer`, `HomeScreen`, `MatchSetupScreen`, `PauseMenu`, `FrameSummary`,
`MatchSummary` under `Hud`. In a match the table's balls are `MatchController.balls`; the free
table's bar, history and edit mode are hidden; a stroke's context is `mode: "match"` with
`strike_mode` `stroke`, `pull`, `harness`, `opponent` or `autoplay`. The app opens at Home.

### Harness (wave 2)

| Command | Args | Result |
| --- | --- | --- |
| `open_screen` | `name`: `home`, `match_setup`, `table` | `{"screen"}` |
| `start_match` | `best_of`, `first_breaker` (`random`, `you`, `computer`), `seed`, `autoplay`, `fast` | `{"match_id"}` |
| `match_state` | | `MatchController.harness_state()` |
| `match_shot` | a strike, `nominated` (optional), `cue` `[x, y]` (optional, when in hand) | `{"shot_id"}`; the person's stroke exactly as given |
| `match_choose` | `kind` | `{"kind"}` |
| `match_wait` | `timeout_s` | once the person must act or the match is over: `{"waiting"}` |
| `match_autoplay` | `on`, `fast` | `{"on", "fast"}`: the stand-in plays the person too; `fast` skips each stroke's playback |
| `match_continue` | | from a frame summary to the next frame, from a match summary to Home |
| `match_pause` | `on` | opens or closes the pause menu; `{"on", "can_concede"}` |

`--demo-screen` gains `home`, `match_setup`, `match`, `pause`, `frame_summary`, `match_summary`.
`tools/play_match.py` plays a whole match through these and checks the log with
`ofs_sim_cli referee`.

## Waves

Wave 1 (on the contracts commit):

| Agent | Builds | Owns |
| --- | --- | --- |
| A engine | `judge`, `decide`, `options`, frame helpers | `core/src/rules/engine.cpp`, `frame_state.cpp`, `core/tests/rules_unit/test_engine_*.cpp` |
| B geometry | the oracle, spotting | `core/src/rules/geometry.cpp`, `spotting.cpp`, `spotting.hpp`, `core/tests/rules_unit/test_geometry_*.cpp` |
| C referee's eyes | `observe`, referee values | `core/src/rules/observe.cpp`, `referee_params.cpp`, `core/data/referee_params.json`, `core/tests/rules_unit/test_observe_*.cpp` |
| D match | match, JSON, log lines | `core/src/rules/match.cpp`, `serialize.cpp`, `rules_log.cpp`, `core/tests/rules_unit/test_match_*.cpp` |
| E stand-in | the stand-in opponent | `core/src/opponent/standin.cpp`, `core/data/standin_params.json`, `core/tests/rules_unit/test_standin_*.cpp` |
| F binding | `OfsMatch` | `gdext/src/ofs_match.cpp`, `ofs_match.h`, `game/tests/test_match_binding.gd` |
| G tools | `ofs_sim_cli match`, `referee` | `tools/sim_cli/match.cpp`, `referee.cpp`, `README.md`, `tools/test_sim_cli_match.py` |
| H match flow | `MatchController`, `OpponentDriver` | `game/scripts/match/match_controller.gd`, `opponent_driver.gd`, `game/tests/test_match_controller.gd` |
| I screens | the five screens | `game/scripts/ui/screens/*`, `game/tests/test_screens.gd` |
| J match HUD | scoreboard, calls, choices, nomination, cue placer | `game/scripts/match/hud/*`, `game/tests/test_match_hud.gd` |
| T1 | scenarios: S2, S3 rules 1-5, S4 rule 2 | `core/tests/rules/test_s2_*.cpp`, `test_s3_r01_05_*.cpp`, `test_s4_*.cpp`, `docs/rules/clauses-t1.md` |
| T2 | scenarios: S3 rules 6-11 | `core/tests/rules/test_s3_r06_11_*.cpp`, `docs/rules/clauses-t2.md` |
| T3 | scenarios: S3 rules 12-14 and 17, spec 9.1 | `core/tests/rules/test_s3_r12_17_*.cpp`, `test_spec_9_1_*.cpp`, `docs/rules/clauses-t3.md` |
| T4 | match flow tests from spec 8.6, 8.7, 11 | `game/tests/test_match_spec_*.gd` |

Wave 2: K integrator: `game/scripts/main.gd`, `game/scenes/main.tscn`, `game/scripts/hud.gd`,
`game/scripts/autoload/harness.gd`, `docs/harness.md`, `tools/play_match.py`,
`tools/test_play_match.py`, `game/tests/test_main.gd`, `test_flow.gd`, `test_harness.gd`,
`test_match_main.gd`.

## Log

- 2026-11-15: contracts committed.
- 2026-11-15: wave 1 merged (engine, geometry and spotting, observe and referee values, match
  and JSON forms, screens, HUD, match flow, the independent rulebook scenarios and spec tests).
  The independent scenarios changed four readings, all in `docs/rules/README.md`: the spotting
  order when spots are taken (S3.7), a touching ball moved only as the cue ball leaves (S3.8(b)),
  the touching gap (5 µm, under the rack's smallest seeded gap), and a miss with the ball on
  impossible to hit (S3.14(a)(ii)).
- 2026-11-16: wave 2 merged (the binding, the stand-in with `ofs_sim_cli match` and `referee`,
  the table scene and harness commands); the stand-in's match run joined the gate's physics lane.
- 2026-11-16: wave 3, the gate's best of 3 through the harness: `python tools/play_match.py` on
  seeds 147 (person breaks, 174 strokes, 46-30 and 48-26), 7 (computer breaks, 164 strokes, 23-56
  and 34-43) and 2026 (random, 161 strokes, 55-33 and 62-14). Each ended at the match summary,
  its log passed `tools/check_logs.py` with no `app.error` and no `rules.invalid`, and
  `ofs_sim_cli referee` re-judged every decision with no difference. Driven by `match_shot` and
  `match_choose` on the real engine: a red then a nominated colour, the wrong ball first, play
  again, an in-off and placing in the D (outside it refused or snapped in), a conceded frame into
  the next frame with the other player breaking, a free ball. The re-spotted black was reached
  only by the native run (38 times in 2,000 frames).
- 2026-11-16: native match run, 2,000 frames (seed 7): 168,440 strokes, no invariant broken, no
  physics anomaly.
- 2026-11-16: check round, eleven checkers, no high-severity finding. Two rulebook reviews
  re-derived about 51,000 stand-in strokes independently with no difference; the logging check
  re-judged 2,926 decisions and replayed 2,926 shots with none diverging; 33 gap tests added.
