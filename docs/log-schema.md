# Log schema

The machine-readable schema is `core/data/log_schema.json`; this is the readable version.
Every line is one JSON object:

| Key | Meaning |
| --- | --- |
| `ts` | seconds since the Unix epoch, UTC, as a number with millisecond precision or better |
| `session` | `YYYYMMDD-HHMMSS-xxxxxx`, one per app launch |
| `build` | the build number (TestFlight build on iOS, `dev` for local builds) |
| `platform` | `windows` or `ios` (`ios-simulator` in CI) |
| `subsystem` | one of `app`, `perf`, `table`, `params`, `devlink`, `log`, `harness` |
| `level` | `debug`, `info`, `warn`, `error` |
| `event` | the event name below |
| `data` | the event's fields |

## Events (M0)

| Event | Level | When | Fields |
| --- | --- | --- | --- |
| `app.start` | info | once, as soon as the Log autoload is ready | `version`, `build_number`, `git_sha`, `core_version`, `godot_version`, `os`, `os_version`, `device_model`, `renderer`, `screen_size` [w, h] px, `safe_area` [x, y, w, h] px, `refresh_rate` Hz, `locale`, `debug_build`, `launch_args`, `settings` |
| `app.lifecycle` | info | foreground, background, focus lost/gained, quit (`state`) | `state` |
| `app.error` | warn/error | every engine error and warning, GDScript error, C++ assertion | `kind` (`error`, `warning`, `script`, `shader`, `message`, `assert`), `message`, `file`, `line`, `function`, `stack` (array of `file:line function` strings) |
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
