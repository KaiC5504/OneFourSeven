# Log schema

The machine-readable schema is `core/data/log_schema.json`; this is the readable version.
Every line is one JSON object:

| Key | Meaning |
| --- | --- |
| `ts` | seconds since the Unix epoch, UTC, as a number with millisecond precision or better |
| `session` | `YYYYMMDD-HHMMSS-xxxxxx`, one per app launch |
| `build` | the build number (TestFlight build on iOS, `dev` for local builds) |
| `platform` | `windows` or `ios` (`ios-simulator` in CI) |
| `subsystem` | one of `app`, `perf`, `table`, `params`, `devlink`, `log`, `harness`, `physics`, `shot`, `input`, `ui`, `audio`, `settings`, `freetable`, `rules`, `match`, `opponent` |
| `level` | `debug`, `info`, `warn`, `error` |
| `event` | the event name below |
| `data` | the event's fields |

## Events (M0)

| Event | Level | When | Fields |
| --- | --- | --- | --- |
| `app.start` | info | once, as soon as the Log autoload is ready | `version`, `build_number`, `git_sha`, `core_version`, `godot_version`, `os`, `os_version`, `device_model`, `renderer`, `screen_size` [w, h] px, `safe_area` [x, y, w, h] px, `refresh_rate` Hz, `locale`, `debug_build`, `launch_args`, `settings` |
| `app.lifecycle` | info | foreground, background, focus lost/gained, quit (`state`) | `state` |
| `app.error` | warn/error | every engine error and warning, GDScript error, C++ assertion | `kind` (`error`, `warning`, `script`, `shader`, `message`, `assert`, `log` for the logger's own failures, `exception` for a C++ exception the physics binding caught), `message`, `file`, `line`, `function`, `stack` (array of `file:line function` strings) |
| `perf.second` | info | once a second while the app runs | `fps`, `frame_ms_avg`, `frame_ms_p95`, `frame_ms_max`, `dropped_frames` (frames over 1.5x the target interval), `static_memory_mb`, `thermal_state` (`nominal`, `fair`, `serious`, `critical`, `unknown`), `idle` |
| `perf.hitch` | warn | any frame over 50 ms | `frame_ms`, `scene`, `activity` (what was running: `startup`, `idle`, `loading`, ...) |
| `table.loaded` | info | the table and balls are built | `balls`, `layout` (`opening`), `params_hash` |
| `params.loaded` | info | parameters loaded at start | `source` (`embedded`, `file`), `hash` |
| `params.applied` | info | parameters replaced at runtime | `source` (`devlink`, `file`), `old_hash`, `new_hash` |
| `devlink.discovered` | info | the PC was found | `host`, `port`, `via` (`bonjour`, `localhost`) |
| `devlink.connected` | info | `/health` answered | `host`, `port`, `server_version` |
| `devlink.lost` | warn | a connected PC stopped answering | `reason`, `queued_lines` |
| `devlink.upload_failed` | warn | a `/logs` POST failed | `status` (HTTP status, 0 for no response), `error`, `queued_lines`, `attempt` |
| `devlink.browse_failed` | warn | Bonjour browsing failed | `error`, `local_network_denied` |
| `log.dropped` | warn | the upload ring buffer dropped lines (emitted at most once a minute) | `count` (since the last report), `total_dropped` |
| `harness.command` | info/warn | each debug harness command | `command`, `args`, `ok`, `result` |

## Added in the M0 fix round (schema version 2)

| Event / field | Level | When | Fields |
| --- | --- | --- | --- |
| `app.start` + | | | `display_server` (`DisplayServer.get_name()`, e.g. `headless`), `max_fps` (`Engine.max_fps`), `git_dirty` (the build had uncommitted changes) |
| `perf.second` + | | | `footprint_mb` (`OfsDevice.memory_footprint_mb()`, MB of 10^6 bytes like `static_memory_mb`, real in release builds; -1 when the OS query fails, reported once as an `app.error`), `frames_drawn` (frames actually rendered this second; `idle` must be the real state) |
| `devlink.request_failed` | warn | a `/health` or `/params` request failed (first of each distinct error, then at most one per 30 s) | `endpoint`, `status` (0 = no response), `error`, `attempt`, `body` (server's error text, ≤ 500 chars) |
| `devlink.resolve_failed` | warn | a Bonjour result couldn't be resolved to an IPv4 address and port | `name`, `error`, `local_network_denied` |
| `devlink.upload_rejected` | warn | the server refused a batch it will never accept (400, 413, 404); those lines are dropped | `status`, `dropped_lines`, `suppressed_since_last`, `body` |
| `log.dropped` (changed) | warn | lines FileSink's size cap pruned before they were uploaded, reported after app.start | `count`, `total_dropped` |
| `queued_lines` (changed) | | in devlink events: lines written but not yet acknowledged by the PC | |
| `app.start` `settings` | | | `{link_enabled}` |
| `devlink.backlog` | info | on connect, earlier sessions are waiting to upload | `sessions`, `bytes` |

## Added in M1 (physics core)

| Event | Level | When | Fields |
| --- | --- | --- | --- |
| `shot.simulated` | info | every simulated shot (sim_cli now, the game from M2) | `bundle` (the repro bundle), `result` |
| `physics.anomaly` | warn | the simulator hit a guard or found something impossible | `kind`, `t` (seconds into the shot), `balls` (indices), `detail`, `bundle` |
| `params.invalid` | error | a parameter in `table_params.json` was refused; that field keeps its default | `key`, `value`, `reason` |
| `table.invalid` | error | the table shape file was refused; `problems` lists every fault found | `hash`, `problems` |
| `table.view` | info/warn | once each time the table is built: which table is drawn | `table` (`model`: the bought table, `res://licensed/table.gltf`; `procedural`: the one drawn from the physics outline), `reason` (`found` for the model; for the procedural table `no_model`: the file isn't in the build, as in CI and TestFlight; `forced`; `headless`: a run that draws nothing; and at warn, when the file is there and didn't come up, `load_failed`, `part_missing` or `texture_missing`), `detail` (warn only: what was wrong with the file and what to run) |

`physics.anomaly` kinds: `event_guard` (max_events reached), `zero_time_chain` (too many
simultaneous events in a row), `time_cap` (still moving at time_cap_s), `solver_fallback` (a
cushion impact solve that gave up and took a plain bounce, a group contact solve that didn't
converge, or a detection time that came back NaN or negative and was ignored; the shot carries on),
`energy_gain`, `overlap`, `nan`. The first three end the shot with every ball at rest where it is.
The fuzz fails a shot on any of them; calibration C12 checks `energy_gain` and C14 the rest.

**Repro bundle** (`core/include/ofs/physics/repro.hpp`): `v` (2), `seed` (decimal string),
`params_hash` (the parameter file's hash), `params` (every physics value by name),
`params_values_hash` (content hash of `params`, integer fields as integers and the rest as doubles,
in the canonical form of `docs/devlink-protocol.md`), `table_hash` (content hash of the table shape
file, empty when unknown), `cue_ball`, `has_strike`, `strike` (`aim_rad`, `tip_x`, `tip_y`,
`elevation_rad`, `speed_m_s`), `start` (every ball: `ball`, `index`, `r`, `v`, `w`, `motion`,
`pocket`). Doubles are at full precision; NaN and the infinities are the strings `"nan"`, `"inf"`
and `"-inf"`. Reading recomputes `params_values_hash` from the values and refuses a bundle whose
values were changed. Version 1 bundles (neither hash, non-finite numbers as `null`) still read when
every number is finite. `tools/sim_cli replay` re-runs a bundle.

**Result:** `events`, `t_end`, `potted` (`ball`, `pocket`, `t`), `final` (as `start`), `miscue`,
`elevation_used_rad`, `elevation_raised`, `anomalies`, `sim_us`, `event_list` (the first 400 events as
`[t, kind, a, b, body]`), `event_digest` (content hash of every event's `[kind, a, b, body]` in
order, without the times, so libm rounding across platforms doesn't change it).

## Added in M2 (the feel build)

New subsystems: `input`, `ui`, `audio`, `settings`, `freetable`. A played shot logs `shot.input`, then
`shot.simulated` (with `context`), then `shot.playback`, all with the same `shot_id`
(`<session>-<n>`).

| Event / field | Level | When | Fields |
| --- | --- | --- | --- |
| `shot.input` | info | the player strikes, before the simulation | `shot_id`, `mode` (`free_table`, `match`), `strike_mode` (`stroke` for Push, `pull` for Pull and release, `harness`, `demo` for a practice shot shown as filmed, `opponent` for the opponent's stroke in a match, `autoplay` for the person's part played by the stand-in), `aim_rad`, `wander_rad`, `tip_x`, `tip_y`, `elevation_requested_rad`, `elevation_min_rad`, `speed_m_s`, `power`, `guide_level`, `stroke_samples` (`[t_s, x, y]` in the power cue's canvas units: the latest press on it, the push stroke in `stroke` mode and the pull in `pull` mode; there is no separate `pull_samples`), `fine_tune_samples` (`[t_s, delta_rad]`); in a match also `match_id` and `frame` |
| `shot.simulated` + | | | `context` (`shot_id`, `mode`, `strike_mode`) when the game played it |
| `shot.rejected` | warn | the binding refused a shot (not configured, malformed balls or strike, a ball listed twice, the core threw) | `shot_id`, `reason`, and to reproduce it `balls`, `cue_ball`, `strike` as the caller passed them (non-finite numbers and other types as text; at most 32 items per Array or Dictionary) |
| `shot.playback` | info/warn | a shot finished playing (warn when frames dropped) | `shot_id`, `t_end`, `wall_s`, `frames`, `dropped_frames`, `frame_ms_max`, `events`, `speed`, `skipped` |
| `shot.camera` | info | at the strike, the camera sequence chosen for the shot | `shot_id`, `sequence` (`pocket`: the view cuts to a camera at the pocket to watch the first object ball drop, or the struck ball stay out of the pocket it was played at, then to the full-table view; `broadcast`: it cuts straight to the full-table view), `ball` and `pocket` (index into `OfsPhysics.table_shape()["pockets"]`) of that drop or attempt, `drop_t` (playback seconds) or for an attempt that stays out `near_t` (when the ball is nearest the pocket), `reason` (`near miss` for the pocket camera on such an attempt; else why not `pocket`: `no pot`, `cue ball only`, `late pot`, `setting off`, `cue ball's journey` (it goes on into another ball or round three cushions, so the shot is watched whole), `practice` (a practice shot, always watched whole), `position` (a shot at a pocket that leaves the striker an excellent position, watched whole for the cue ball's run), `watching` (the person is watching the opponent from above or walking round, so the view stays theirs), or `no pocket` when the drop's pocket index isn't in the table shape, with an `app.error` warn), `pose` (the pocket camera, `[x, y, z, pitch_rad, yaw_rad]` in Godot units) |
| `shot.side_allowed` | info | at the strike of a played shot with side on, the aim turned so side doesn't move the pot (`SpinAim`) | `shot_id`, `turned_rad` (added to the aim), `target` (`ball`: the first ball hit leaves as without side; `cushion`: the cue ball reaches the same point; empty when nothing was done), `residual_rad` (error left), `reason` (why nothing was done, else empty; `played as given` for a practice shot shown as filmed, whose `turned_rad` is the filmed aim less the setup's), `sims` (predictions run), `us` |
| `shot.result` | info | after the balls settle, before the camera returns behind the cue ball | `shot_id`, `potted` (ball labels), `points` (their values: red 1, yellow 2 up to black 7), `break` (running total; 0 after a shot that pots nothing or the cue ball), `next_ball` (the ball the aim turned to, `""` when it stayed), `next_pocket`, `next_aim_rad`, `next_cut_deg`, `next_chance` (the pot's chance as `NextTarget` works it out, 0 to 0.9), `next_by` (`reader` when the `person` reader chose it, `geometry` for the fallback), `position` (what the position left is worth in points as the `person` reader sees it, written when the person plays next), `next_reason` (`setting off`, `no object ball`, `no clear pot` when it aimed at the nearest ball, `no usable aim` when the chooser gave no finite aim, with an `app.error` warn); in a match also `match_id` and `frame` |
| `physics.configured` | info/error | the binding took (or refused) parameters and table shape | `ok`, `params_hash`, `table_hash`, `problems` |
| `physics.query_refused` | warn | `OfsPhysics.predict`, `min_cue_elevation` or `OfsShotRecord.path` refused its input (not configured, malformed, out of range); once per distinct `what` and `reason` in a session, at most 64 | `what` (`predict`, `min_cue_elevation`, `path`), `reason` |
| `params.pending` | info | new parameters arrived while a shot was playing or being set up; they apply at the next shot | `hash`, `current_hash` |
| `params.applied` + | | | `shot_id`: the first shot that used them |
| `guide.predict_slow` | warn | a guide level 3 prediction took longer than its budget | `sim_us`, `budget_us`, `level` |
| `freetable.action` | info | retake, undo, redo, a setup (a practice shot's is `setup` with `detail.position`) or edit mode toggled | `action`, `history_index`, `history_size`, `shot_id`, `detail` |
| `freetable.edit` | info/warn | a ball moved, added or removed in edit mode (warn when refused) | `action` (`move`, `add`, `remove`), `ball`, `index`, `from`, `to` (core `[x, y]`), `legal`, `reason` |
| `input.cancel` | info | a stroke or gesture was cancelled | `control` (`stroke`, `pull`, `spin`, `elevation`, `fine_tune`, `gestures`), `reason` (for `pull`: `no pull`, `slid back to the start`, `left the cue strip`, `touch cancelled`, or why the game ended it) |
| `aim.lift` | info | an aim drag ended with the finger having moved in its last 0.06 s (`AimControl.LIFT_S`) | `moved` (canvas units in that time), `turned_rad` (what the aim turned in it), `taken_back` (true when that turn was undone as the finger's roll on lifting: `moved` at most `LIFT_MAX_UNITS`) |
| `settings.loaded` | info/warn | settings read at start (warn when a value was refused or migrated, or the file unreadable) | `path`, `version`, `problems` (a saved `strike_mode` of `power_bar` reads as `pull` and is listed as `strike_mode "power_bar" migrated to "pull"`; the file is saved at once, so it happens once) |
| `settings.changed` | info/warn | a setting changed (warn when the value was refused) | `key`, `old`, `new`, `ok`, `reason` |
| `screen.changed` | info | the screen changed | `from`, `to` |
| `ui.layout` | info | the free table's controls were laid out for a new sensor housing side: at start, and when the phone turns round | `housing` (`left`, `right`, or `""` when unknown or not a phone), `safe` (the safe area), `cue` and `spin` (their rects), all `[x, y, w, h]` in canvas units |
| `overlay.toggled` | info | the debug overlay opened or closed | `visible`, `via` (`three_finger_tap`, `key`, `harness`) |
| `audio.error` | warn | a sound failed to load or play | `sound`, `reason` |
| `haptics.unavailable` | info | once per session when vibration can't be used | `reason` |
| `harness.channel` | info/warn | the debug command channel listened, took a client, closed or failed | `state`, `port`, `error` |
| `params.refused` | warn | at a shot boundary, the binding refused the waiting parameters (`physics.configured` says why); they are dropped and the shot runs on the parameters in use, which `Link.applied_params_hash` keeps reporting | `hash` (the refused set), `current_hash` (still in use), `shot_id` |
| `perf.hitch` + | | | `activity` is now `startup` (the frame began in the first 3 s), `resume` (the first frame after a resume or regained focus), `idle`, or what kept the game active: the sorted `Perf.set_active` reasons, then `held` for a touch or button down (e.g. `camera, shot, held`), or `input` for recent input alone; `active` (the `set_active` reasons), `held` (`touch <i>`, `mouse <button>`), `idle`, `frame` (frames since start, the first is 1), `first_frame` (`startup`, `resume` or `""`), `since_start_s` (when the frame began), `input_age_s` (since the last input when the frame began, -1 before any) |
| `settings.loaded` + | | | `values`: every setting in effect after loading, defaults included |

## Added in M3 (rules and matches)

`pre`, `before`, `judgement`, `facts`, `input`, calls and summaries are the JSON forms of
`core/include/ofs/rules/serialize.hpp`. The game's lines carry `match_id` (`<session>-m<n>`),
`frame` (its number) and, for a stroke, `shot_id`, which ties them to `shot.input` and
`shot.simulated`. `tools/sim_cli referee <log>` re-judges every `rules.decision` from its own line.
`tools/check_logs.py` fails a line with a data field its event does not declare.

| Event | Level | When | Fields |
| --- | --- | --- | --- |
| `app.error` + | | | `match_id`, `frame` when it happened in a match (`kind` `exception` from `OfsMatch` or the stand-in); from the opponent's search also `shot_id`, `level`, `seed`, and `more` (how many more errors like it the same decision met: only the first is logged in full) |
| `rules.configured` | info/error | the match engine was given its referee and stand-in values (error when refused) | `ok`, `values` (the referee's), `standin`, `params_hash` (the physics values' hash, as `params.loaded` has it; `""` without physics), `problems`; from M4 `levels` (each level's `id`, values and `hash`, the level file's content hash) and `exhibitions` (the tables' names) |
| `rules.decision` | info | the referee judged a stroke | `pre` (the frame before it), `input` (the nomination), `facts` (what was seen in the shot record), `judgement` (fouls with clauses and values, penalty, miss and why, points, calls; no `post`), `after` (`striker`, `scores`, `on`, `free_ball`, `cue_in_hand`, `pending`, `decider`, `over`, `winner`, `end_reason`), `post_hash` (the hash of the frame after it), `ref` (the referee's values) |
| `rules.choice` | info/warn | a player chose after a foul, answered a re-rack or the toss, claimed or conceded (warn when refused) | `before` (the frame), `player`, `kind`, `ok`, `error`, `options` (what was offered), `calls`, `after`, `post_hash`; no `shot_id` |
| `rules.cue_placed` | info/warn | the in-hand cue ball was placed (warn when refused) | `player`, `x`, `y`, `ok`, `error` |
| `rules.invalid` | warn/error | the engine refused or could not make sense of an input | `what`, `detail`, and `match_id`, `frame` and `shot_id` of the call it came in (`ofs::rules::ScopedLogContext`), when there was one |
| `match.started` | info | a match began | `config` (`best_of`, `first_breaker`: -1 for lots drawn from the seed, 0 or 1 for that player; `seed`, a decimal string; `concede_any_time`), `breaker`, `person` (which player is the person) |
| `match.ended` | info | a match ended | `winner` (-1 when abandoned), `frames`, `frames_played`, `reason` (`frames`, `conceded`, `abandoned`) |
| `frame.started` | info | a frame was racked, a re-rack included | `number`, `breaker`, `seed`, `reracks`, `params_hash` (the physics values the rack was laid with; they may have changed over the link since `rules.configured`) |
| `frame.ended` | info | a frame ended | `summary` (`number`, `breaker`, `winner`, `reason`, `scores`, `stats`, `strokes`, `reracks`) |
| `match.call` | info | the referee line showed calls | `calls` (kinds), `text` (the words shown), `match_id`, `frame`, and `shot_id` when a stroke brought them |
| `match.nominated` | info | the declared colour changed | `ball`, `how` (`aim`, `tap`, `harness`, `opponent`) |
| `match.refused` | info | the person asked for a decision that is not open now (a concession or claim while a stroke plays or the other player is choosing); nothing changed | `kind` (the decision), `why`, `match_id`, `frame` |
| `match.turn` | info | play waits for someone | `player`, `person`, `waiting` (`shot`, `choice`, `opponent`, `frame_summary`, `match_summary`, `stalled` when the stand-in could not play and is no longer asked) |
| `opponent.decision` | info/warn | the stand-in chose a stroke or a choice (warn when it could not) | `kind` (`shot`, `choice`), `player`, `seed` (a decimal string: the stand-in's `Rng` seed, which makes the same choice again from the same frame), `why`, `us` (time taken), and for a stroke `shot_id`, `strike`, `nominated`, `cue` (when it places the cue ball); for a choice `choice`, `options` and no `shot_id`, as nothing is struck; `error` |

`opponent.decision`'s `why` (`core/include/ofs/opponent/standin.hpp`):

- a stroke: `kind` (`pot`, `safety`, `escape`, `break`), `target` (`red 3`, `blue`), `pocket` (index,
  -1 for none), `cut_rad`, `candidates` (strokes listed), `sims` (simulated to check them),
  `penalty` (what the stroke chosen cost as aimed, -1 when nothing was simulated), `aim_error_rad`
  and `speed_error` (the error then added); `{kind: safety, error}` when it could not work one out
  and played the plainest stroke instead (with an `app.error`).
- a claim or concession in place of a stroke: `kind` `claim`, or `concede` with `required` (the
  penalty points it needs).
- a choice after a foul, a re-rack or the toss: `rule` (`replace`, `play_again`, `play`,
  `accept_rerack`, `refuse_rerack`, `play_first`, `first_option`), `options`, `scores`,
  `points_remaining`, and what the rule looked at: `clear_path_before` (the position before a miss
  had a straight stroke at a ball on), `pot` and `clear_path` (in the position left), `score_gap`
  and `rerack_within`; `error` when it threw and took the first option.

## Added in M4 (the computer opponent)

| Event | Level | When | Data |
| --- | --- | --- | --- |
| `opponent.think` | info | a level was asked for a stroke and started thinking, off the main thread | `level`, `player`, `seed` (a decimal string), `shot_id`, `budget` (`max_sims`, `max_steps`, `max_ms`), `params_hash` (the physics values it plans with) |
| `opponent.decision` | info/warn | as in M3, now for a level too | also `level` (absent for the stand-in), `meant` (the strike as planned; `strike` is the one played, error included), `think` (`candidates`, `sims`, `steps`, `depth`, `stopped`: `done`, `sims`, `steps`, `time` or `cancelled`; `ms`; `errors`: trial simulations that failed, when any did), `pre_hash` (`frame_hash` of the frame it decided from), `params_changed` (true when the physics values changed while it thought: the stroke was planned on the old ones). A stroke the game refused (warn, with `error`) also carries `pre` (that frame), and `now_hash` when the frame had moved on |
| `opponent.maximum` | info | the 147's maximum came on at the start of its visit, or went | `state` (`on`, `gone`), `why` (when gone, logged with the stroke that lost it: `the break ended`, `a colour other than the black`, `two reds in one stroke`; or at the start of a visit: `a red is off the table`, `a free ball`), `level`, `player`, `break_points`, `reds` (left on the table), `shot_id` |
| `opponent.fallback` | warn | a level could not work a stroke out and played the stand-in's plainest | `level`, `why`, `player`, `seed`, `shot_id` |
| `opponent.cancelled` | info | thinking was stopped before it answered (the match was left, the pause menu quit) | `level`, `why` (`left`, `conceded`, `claimed`, `replaced` by a new request; pausing holds the thinking and cancels nothing), `ms`, `player`, `seed`, `shot_id` |
| `exhibition.started` | info | Watch a 147 began | `layout` (its name in `exhibitions.json`), `level`, `match_id` |
| `exhibition.ended` | info | the exhibition's break ended | `break_points`, `maximum` (147 made), `why` (`maximum`, `missed`, `fouled`, `left`: the person left; from the tournament also `claimed`, `conceded` and `failed`), `strokes`, `match_id` |
| `camera.watch` | info | the view of the opponent's turn changed | `view` (`cue`, `above`, `round`), `how` (`button`, `harness`, `turn`: back to the cue view at the person's turn), `match_id` |

`match.started` also carries `level` (the opponent's level id) and, for Watch a 147, `exhibition`
(the layout's name).

A level's decision is made again from its line: `seed` and `think.steps` with the frame it was
made from (`ofs_sim_cli opponent <log.jsonl>`). `why` for a level's stroke
(`core/include/ofs/opponent/opponent.hpp`, in full in `docs/opponent/README.md`): `level`, `kind`
(`pot`, `safety`, `escape`, `break`), `target`, `pocket`, `band` (`short`, `medium`, `long`),
`cut_rad`, `chance` (as it read it), `worth`, `leave` (where it meant the cue ball to stop and what
for), `plan` (the strokes it planned after this one), `others` (the next best three), `drawn` (the
error added: `aim_rad`, `speed`, `tip_x`, `tip_y`), `maximum` (`on`, `off`, `gone`).
