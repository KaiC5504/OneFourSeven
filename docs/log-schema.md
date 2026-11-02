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
| `app.error` | warn/error | every engine error and warning, GDScript error, C++ assertion | `kind` (`error`, `warning`, `script`, `shader`, `message`, `assert`, `log` for the logger's own failures), `message`, `file`, `line`, `function`, `stack` (array of `file:line function` strings) |
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

`physics.anomaly` kinds: `event_guard` (max_events reached), `zero_time_chain` (too many
simultaneous events in a row), `time_cap` (still moving at time_cap_s), `solver_fallback`,
`energy_gain`, `overlap`, `nan`. The first three end the shot with every ball at rest where it is.

**Repro bundle** (`core/include/ofs/physics/repro.hpp`): `v` (1), `seed` (decimal string),
`params_hash`, `params` (every physics value by name), `cue_ball`, `has_strike`, `strike`
(`aim_rad`, `tip_x`, `tip_y`, `elevation_rad`, `speed_m_s`), `start` (every ball: `ball`, `index`,
`r`, `v`, `w`, `motion`, `pocket`, doubles at full precision). `tools/sim_cli replay` re-runs it.

**Result:** `events`, `t_end`, `potted` (`ball`, `pocket`, `t`), `final` (as `start`), `miscue`,
`elevation_used_rad`, `elevation_raised`, `anomalies`, `sim_us`, `event_list` (the first 400 events as
`[t, kind, a, b, body]`), `event_digest` (FNV-1a 64 over all of them).
