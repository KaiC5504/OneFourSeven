# Log schema

The machine-readable schema is `core/data/log_schema.json`; this is the readable version.
Every line is one JSON object:

| Key | Meaning |
| --- | --- |
| `ts` | seconds since the Unix epoch, UTC, as a number with millisecond precision or better |
| `session` | `YYYYMMDD-HHMMSS-xxxxxx`, one per app launch |
| `build` | the build number (TestFlight build on iOS, `dev` for local builds) |
| `platform` | `windows` or `ios` (`ios-simulator` in CI) |
| `subsystem` | one of `app`, `perf`, `table`, `params`, `devlink`, `log`, `harness`, `physics`, `shot` |
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
| `shot.input` | info | the player strikes, before the simulation | `shot_id`, `mode` (`free_table`), `strike_mode` (`stroke` for Push, `pull` for Pull and release, `harness`), `aim_rad`, `wander_rad`, `tip_x`, `tip_y`, `elevation_requested_rad`, `elevation_min_rad`, `speed_m_s`, `power`, `guide_level`, `stroke_samples` (`[t_s, x, y]` in the power cue's canvas units: the latest press on it, the push stroke in `stroke` mode and the pull in `pull` mode; there is no separate `pull_samples`), `fine_tune_samples` (`[t_s, delta_rad]`) |
| `shot.simulated` + | | | `context` (`shot_id`, `mode`, `strike_mode`) when the game played it |
| `shot.rejected` | warn | the binding refused a shot (not configured, malformed balls or strike, a ball listed twice, the core threw) | `shot_id`, `reason`, and to reproduce it `balls`, `cue_ball`, `strike` as the caller passed them (non-finite numbers and other types as text; at most 32 items per Array or Dictionary) |
| `shot.playback` | info/warn | a shot finished playing (warn when frames dropped) | `shot_id`, `t_end`, `wall_s`, `frames`, `dropped_frames`, `frame_ms_max`, `events`, `speed`, `skipped` |
| `shot.camera` | info | at the strike, the camera sequence chosen for the shot | `shot_id`, `sequence` (`pocket`: the view cuts to a camera at the pocket to watch the first object ball drop, then to the full-table view; `broadcast`: it cuts straight to the full-table view), `ball` and `pocket` (index into `OfsPhysics.table_shape()["pockets"]`) of that drop, `drop_t` (playback seconds), `reason` (why not `pocket`: `no pot`, `cue ball only`, `late pot`, `setting off`, or `no pocket` when the drop's pocket index isn't in the table shape, with an `app.error` warn), `pose` (the pocket camera, `[x, y, z, pitch_rad, yaw_rad]` in Godot units) |
| `shot.result` | info | after the balls settle, before the camera returns behind the cue ball | `shot_id`, `potted` (ball labels), `points` (their values: red 1, yellow 2 up to black 7), `break` (running total; 0 after a shot that pots nothing or the cue ball), `next_ball` (the ball the aim turned to, `""` when it stayed), `next_pocket`, `next_aim_rad`, `next_cut_deg`, `next_reason` (`setting off`, `no object ball`, `no clear pot` when it aimed at the nearest ball, `no usable aim` when the chooser gave no finite aim, with an `app.error` warn) |
| `physics.configured` | info/error | the binding took (or refused) parameters and table shape | `ok`, `params_hash`, `table_hash`, `problems` |
| `physics.query_refused` | warn | `OfsPhysics.predict`, `min_cue_elevation` or `OfsShotRecord.path` refused its input (not configured, malformed, out of range); once per distinct `what` and `reason` in a session, at most 64 | `what` (`predict`, `min_cue_elevation`, `path`), `reason` |
| `params.pending` | info | new parameters arrived while a shot was playing or being set up; they apply at the next shot | `hash`, `current_hash` |
| `params.applied` + | | | `shot_id`: the first shot that used them |
| `guide.predict_slow` | warn | a guide level 3 prediction took longer than its budget | `sim_us`, `budget_us`, `level` |
| `freetable.action` | info | retake, undo, redo, a setup or edit mode toggled | `action`, `history_index`, `history_size`, `shot_id`, `detail` |
| `freetable.edit` | info/warn | a ball moved, added or removed in edit mode (warn when refused) | `action` (`move`, `add`, `remove`), `ball`, `index`, `from`, `to` (core `[x, y]`), `legal`, `reason` |
| `input.cancel` | info | a stroke or gesture was cancelled | `control` (`stroke`, `pull`, `spin`, `elevation`, `fine_tune`, `gestures`), `reason` (for `pull`: `no pull`, `slid back to the start`, `left the cue strip`, `touch cancelled`, or why the game ended it) |
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
