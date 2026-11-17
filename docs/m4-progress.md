# M4 progress: the computer opponent

Plan approved by KaiC on 2026-11-17 with the three recommendations: once the maximum is gone the
147 plays on for the highest break still possible; Watch a 147 on Home starts it from a table
already open; its spin and power show on screen as it plays. Builders, checkers and fixers run on
Opus 5.5.

Two stages, at his word: **stage 1** is the opponent's engine and its two ends, Beginner (level 1)
and the 147, on his phone; **stage 2** is levels 2 to 5 and the tactics between them, carried
straight on while he plays stage 1. In stage 1 the opponent picker offers Beginner and The 147;
the stand-in leaves the game (it stays for the rules fuzz, for autoplay, and plays the safeties,
escapes and break-offs with the level's error).

Done (stage 1) means: the marks in `docs/opponent/README.md` ("What the tournament counts") met;
every stroke of either level legal and every decision re-made from its log line; the 147 deciding
within 1.5 s on the iPhone with no frame dropped while it thinks; `python scripts/gate.py` green on
the committed tree; a best of 3 against each level through the harness; the build on his iPhone
through `python scripts/mac.py phone` with the play checklist below.

## Contracts

- `core/include/ofs/opponent/`: `level.hpp` (built), `env.hpp`, `pots.hpp`, `solve.hpp`,
  `error_model.hpp`, `evaluate.hpp`, `opponent.hpp`, `exhibition.hpp` (built). They were
  committed as stubs that compiled and played the stand-in, so a match ran end to end from the
  first day; waves 1 and 2 made them real.
- `core/data/opponent_beginner.json`, `opponent_maximum.json` (starting values), `exhibitions.json`
  (one hand-laid table).
- `docs/opponent/README.md`: how spec 10 is built, the pot's geometry, the 147's rule, what the
  tournament counts, the `why` of a decision, and the game's side.
- `core/tests/opponent/rig.hpp` (levels, `env()`, `position(name)`, `racked()`), the named
  positions in `core/tests/fixtures/opponent/`, and two globbed test targets:
  `ofs_opponent_tests` (`core/tests/opponent/*.cpp`, written from the spec) and
  `ofs_opponent_unit_tests` (`core/tests/opponent_unit/*.cpp`, the builders' own).
- `gdext/src/ofs_match.h`: the level methods, declared and bound (not built).
- `tools/sim_cli`: `tournament` and `opponent` declared and dispatched.
- Game skeletons: `OpponentShow`, `WatchButton`, `ExhibitionSummary`; `FakeMatchEngine` has the
  level methods.
- Log events: `opponent.think`, `opponent.maximum`, `opponent.fallback`, `opponent.cancelled`,
  `exhibition.started`, `exhibition.ended`, `camera.watch`; `opponent.decision` gains `level`,
  `meant`, `think`; `match.started` gains `level`, `exhibition`.

## Waves (stage 1)

Wave 1 (on the contracts commit):

| Agent | Builds | Owns |
| --- | --- | --- |
| A solver | `try_strike`, `solve_pot`, `least_pot_speed` | `core/src/opponent/solve.cpp`, `core/tests/opponent_unit/test_solve_*.cpp` |
| B pots | `pots_from`, `pots`, `cue_placements`, `pockets_open_to`, `open_reds` | `core/src/opponent/pots.cpp`, `core/tests/opponent_unit/test_pots_*.cpp` |
| C error and evaluation | `aim_sigma`, `pot_chance`, `with_error`; `read_pots`, `read_position`, `maximum_on`, `maximum_next` | `core/src/opponent/error_model.cpp`, `evaluate.cpp`, `core/tests/opponent_unit/test_error_*.cpp`, `test_evaluate_*.cpp` |
| F binding | the level methods of `OfsMatch`, the worker thread, `start`'s `level` and `exhibition` | `gdext/src/ofs_match.cpp`, `ofs_match.h`, `game/tests/test_opponent_binding.gd` |
| G tools | `ofs_sim_cli tournament` and its report, `ofs_sim_cli opponent` | `tools/sim_cli/tournament.cpp`, `opponent.cpp`, `README.md`, `tools/opponent_report.py`, `tools/test_sim_cli_tournament.py`, `test_sim_cli_opponent.py` |
| H match flow | levels and the exhibition in the match flow, the thinking wait | `game/scripts/match/match_controller.gd`, `opponent_driver.gd`, `game/tests/test_match_controller.gd`, `test_opponent_driver.gd`, and the other tests that drive `MatchController` on the fake engine |
| I screens | the picker, Watch a 147, the exhibition summary, the opponent's name | `game/scripts/ui/screens/*`, `game/scripts/match/hud/scoreboard.gd`, `game/tests/test_screens.gd`, the scoreboard's tests |
| J show | `OpponentShow` | `game/scripts/match/opponent_show.gd`, `game/tests/test_opponent_show.gd` |
| L watching | the views from above and round the table, `WatchButton` | `game/scripts/input/camera_rig.gd`, `game/scripts/match/hud/watch_button.gd`, `game/tests/test_camera_rig.gd`, `test_watch_button.gd` |
| T1 | independent tests of A to C | `core/tests/opponent/test_shots_*.cpp`, `core/tests/fixtures/opponent/t1_*.json` |

Wave 2: E search (`core/src/opponent/search.cpp`, `decide.cpp`, `maximum.cpp`,
`core/tests/opponent_unit/test_search_*.cpp`); T2 (`core/tests/opponent/test_spec_10_*.cpp`,
`fixtures/opponent/t2_*.json`); K integrator (`game/scripts/main.gd`, `game/scenes/main.tscn`,
`game/scripts/hud.gd`, `game/scripts/autoload/harness.gd`, `game/scripts/ui/debug_overlay.gd`,
`game/scripts/play/shot_cycle.gd`, `docs/harness.md`, `tools/play_match.py`, the main, flow and
harness tests).

Wave 3: N Beginner's tuning and the gate's tournament lane (`core/data/opponent_beginner.json`,
`scripts/gate.py`, `docs/opponent/report.md`); O the 147's tuning and the exhibition tables
(`core/data/opponent_maximum.json`, `core/data/exhibitions.json`, `tools/make_exhibitions.py`).

Build files, `level.hpp`, `opponent.hpp` and the other headers change only through the main
session: an agent that needs a change reports it.

## KaiC's play checklist (stage 1)

1. Home, Play: pick Beginner, best of 3. Does it miss like a beginner (most long ones, about two
   in three middling ones), over-hit, and leave you chances?
2. Watch its cue: it lines up, feathers, then strikes, seen from behind its cue.
3. While it is at the table, tap the view button: the table from above; again: drag to walk
   round it; again: back behind its cue.
4. Its spin dot and power show as it plays.
5. Pick The 147, best of 1, you break and leave a red on. Does it take the black after every red,
   with the cue ball landing for the next one?
6. Pot a red yourself first: the maximum is gone. Does it play for the highest break left?
7. Leave it no pot at all: does it play safe and wait?
8. Home, Watch a 147: an open table, and the break counts up on the scoreboard.
9. Foul against each: do their choices make sense (putting you back in, taking a free ball)?
10. The overlay's think time, on the phone.

Only the phone can show: the think time and no hitch while it thinks, the walk-round drag under a
finger, the picker and the view button against the screen's edges, heat over a long break.

## Log

- 2026-11-17: contracts committed.
- 2026-11-17: wave 1 merged (pots, solve, error model, evaluation, binding, tournament runner,
  controller, show, camera views, screens). Quick gate green.
- 2026-11-18: wave 2 merged (the search, the independent behaviour tests, the table scene).
  The 147 against Beginner over 300 frames: 300 won, 284 maximums, no foul, no planned pot that
  stayed out; a decision takes 0.2 s on average, 0.74 s at worst (Release, this PC). Beginner
  against itself: medium 45.7%, long 16.8%, mean break 3.7 (wave 3 tunes it). The 147 takes no
  claim while its maximum is on. Beginner's safety mark now counts only strokes where it had a
  pot: it has none in about one position in seven.
- Known of the 147 after wave 2: a ball on a cushion is shut to it (wave 3 adds pots along a
  cushion); it plans no split or cannon and mostly sees one stroke ahead (stage 2).
- For the check round: the exhibition's scoreboard still reads "You 0 ... best of 1"; the
  elevation label reads "raised to 25 degrees" during the opponent's turn (its cue or a leftover
  of the person's?).
- 2026-11-18: wave 3 started: Beginner's tuning with the gate's opponent lane, eight exhibition
  tables with `tools/make_exhibitions.py`, pots along a cushion.
- 2026-11-18: wave 3 merged. Beginner tuned (aim sigma 0.0026, 0.8 per metre): medium 35.0%, long
  10.7%, mean break 3.2, a foul about every eighth stroke. Eight exhibition tables, each a 147 in
  36 strokes. Pots along a cushion: Beginner's strokes with no pot fell from 17.9% to 10.8%; the
  147 made all 558 it played. Every maximum the 147 has lost against Beginner was Beginner
  potting a red first.
- For stage 2, from the spec check: the evaluation handed to the game (the next-ball pick and the
  camera's "excellent position", spec 8.5); a pot's chance from noisy simulations rather than
  the geometric margin, and the cost of a miss in a level's score (spec 10.2 steps 3 and 4, for
  the levels that plan); the break-off and first safety chosen by what they leave.
