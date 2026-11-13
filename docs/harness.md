# Debug harness

How agents drive and inspect the real game (spec 16.5). Godot draws its own UI, which desktop
automation can't read, so the debug build takes launch arguments and, on Windows, a command
channel on 127.0.0.1. Release builds (TestFlight included) ignore all of it. Every flag and
command logs `harness.command`; the channel logs `harness.channel` (`docs/log-schema.md`).

## Quick start

```
python scripts/run_debug.py -- --quit-after=0      # builds the Debug extension once; the game quits at once
python tools/harness_client.py run --resolution 1912x880 -- --no-link < commands.jsonl
```

with `commands.jsonl` holding one request per line:

```
{"cmd": "ping"}
{"cmd": "load_position", "args": {"name": "straight_1m"}}
{"cmd": "play_shot", "args": {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": -0.3, "elevation_rad": 0.0, "speed_m_s": 2.0}}
{"cmd": "wait_idle", "args": {"timeout_s": 30}}
{"cmd": "screenshot", "args": {"path": "C:/tmp/after.png"}}
{"cmd": "dump_state", "args": {"path": "C:/tmp/after.json"}}
```

`run` prints one response per line and quits the game at the end of stdin. Pass `--no-link`
(after `--`) unless the run should upload its session log to `devlink.py`. To keep a run's logs
out of your own user folder, set `APPDATA` to a scratch folder first, as `scripts/gate.py` does.

## Launch arguments

User arguments, after `--` on Godot's command line. Relative paths land under `user://`. A
flag given twice keeps its last value and logs a warning; an unknown or malformed one logs a
warning and is ignored.

| Flag | What it does |
| --- | --- |
| `--mute` | Mutes the master audio bus before anything plays. `run_debug.py` and `harness_client.py` always pass it (`run_debug.py --sound` leaves it out): agent runs make no sound. |
| `--harness-port=<port>` | Opens the command channel on 127.0.0.1 (Windows only). `0` picks a free port; the game prints `harness listening on 127.0.0.1:<port>` to stdout. |
| `--demo-position=<name>` | Loads `res://data/positions/<name>.json` one frame after the scene is ready. |
| `--demo-screen=<name>` | Opens a screen (M2: `table`). |
| `--screenshot=<path>` | Saves a PNG after one second. Not headless: that renders nothing. |
| `--dump-state=<path>` | Writes the state (as `dump_state` below) after one second. |
| `--quit-after=<seconds>` | Quits, after any capture above has finished. |
| `--no-link` | Read by the phone link: starts it switched off. |

iOS passes launch arguments as `-Name value` pairs, which the CI simulator run uses:
`-DemoScreen table -DemoPosition straight_1m`.

Named positions are the physics fixtures (`core/tests/fixtures/positions/`, listed in their
README), copied into `game/data/positions/` by `scripts/sync_data.py`.

## The channel

Newline-delimited JSON over TCP on 127.0.0.1, debug builds on Windows only. Each request is
one line, `{"id": <any>, "cmd": "<name>", "args": {...}}`; `args` may be left out. Each answer
is one line, `{"id": <the request's>, "ok": true, "result": ...}` or `{"id", "ok": false,
"error": "<why>"}`. A client's requests run one at a time, in order, each answered before the
next starts (`wait_idle` and `input` take frames); several clients may connect at once. A line
that is not a JSON object is answered with `"id": null`. A line over 1 MiB closes the client.

`tools/harness_client.py send --port N '{"cmd": "ping"}'` sends one request to a game already
listening; from Python, `Harness` starts the game and `Connection` talks to a port:

```python
from harness_client import Harness

with Harness(resolution="1912x880", extra_args=["--no-link"]) as game:
    game.call("load_position", name="cut_half_ball")
    shot_id = game.call("play_shot", aim_rad=0.0, tip_x=0.0, tip_y=0.0, elevation_rad=0.0, speed_m_s=1.5)["shot_id"]
    game.call("wait_idle", timeout_s=30)
    state = game.call("dump_state")
```

`call` raises `HarnessError` with the game's error; `request` returns the raw answer.

### Commands

| Command | Args | Result |
| --- | --- | --- |
| `ping` | | `"pong"` |
| `load_position` | `{"name"}`, or `{"balls", "cue_ball"}` | `{"balls": n, "cue_ball"}`; an error when the scene refuses the position |
| `play_shot` | `aim_rad`, `tip_x`, `tip_y`, `elevation_rad`, `speed_m_s` (all required, finite) | `{"shot_id"}`; an error when the scene refuses the shot |
| `wait_idle` | `timeout_s` (default 30) | `{"waited_s"}` once no shot is playing; an error at the timeout |
| `set_setting` | `key`, `value` | `{"key", "value"}` as stored; an error when Settings refuses it (`settings.changed` says why) |
| `dump_state` | `path` (optional) | the state, also written to `path` when given (the result then has `path`) |
| `screenshot` | `path` | the PNG's absolute path |
| `overlay` | `visible` (true or false) | `{"visible"}`: the debug overlay, as F3 or a three-finger tap toggles it |
| `input` | `events`, `frames_between` (default 1) | `{"sent": n}` after the last event has reached the scene |
| `quit` | | `"quitting"`, then the game exits |

`load_position` with `balls` takes the game's ball list, `[{"ball": "red", "index": 3, "x":
0.1, "y": 0.2}, ...]` in core metres (`x` towards the black, `y` to the left seen from baulk),
or the fixtures' form with `"r": [x, y]`; `cue_ball` is the cue ball's position in that list.

`dump_state` holds `screen`, `session`, `link_status`, `params_hash`, `params_pending_hash`,
`perf` (the last second's fps, frame times, dropped frames, thermal state, memory footprint and
idle) and `balls` (`name`, `ball`, `index`, `position` [x, y, height] in core metres), overlaid
by the game's own state (`harness_state()` in `main.gd`: screen, ball list, shot setup,
settings, last shot).

### Input events

`input` sends synthesized events through `Input.parse_input_event`, one per frame
(`frames_between` frames apart; 0 sends them all in one frame). `x`, `y`, `rx`, `ry` are canvas
units, the coordinates the game's controls use (the canvas is 1912 x 880 at the default
window size, wider on wider screens); the harness maps them to window pixels through the
viewport's final transform, so a touch at a control's canvas position reaches that control at
any window size. A bad event fails the whole command, naming its index, before anything is
sent.

| `type` | Fields |
| --- | --- |
| `touch` | `index` (default 0), `pressed`, `x`, `y` |
| `drag` | `index` (default 0), `x`, `y`, `rx`, `ry` (the movement since the last event) |
| `mouse_button` | `button` (1 left, 2 right, 3 middle, 4/5 wheel up/down; default 1), `pressed`, `x`, `y` |
| `mouse_motion` | `x`, `y`, `rx`, `ry`, `button_mask` (1 left, 2 right, 4 middle; default 0) |
| `key` | `pressed`, and `action` (an input action such as `shoot` or `overlay_toggle`) or `keycode` (a name such as `"F3"`, `"Space"`, or a number) |

A one-finger aim drag, then the overlay:

```
{"cmd": "input", "args": {"events": [
  {"type": "touch", "pressed": true, "x": 900, "y": 500},
  {"type": "drag", "x": 940, "y": 505, "rx": 40, "ry": 5},
  {"type": "touch", "pressed": false, "x": 940, "y": 505},
  {"type": "key", "keycode": "F3", "pressed": true},
  {"type": "key", "keycode": "F3", "pressed": false}]}}
```

A first touch also arrives as a left mouse button (Godot's mouse-from-touch emulation).

## The debug overlay

Hidden by default, in every build: F3, a three-finger tap or the `overlay` command shows it
top-right inside the safe area. It shows the build, fps, frame time average, p95 and max,
dropped frames, the last shot's simulation time and the last guide prediction's time, thermal
state, memory footprint, the parameter hash in use and any waiting for the next shot, and the
link status. It refreshes at most twice a second and only while shown, so it never keeps an
idle game drawing. Its numbers start once the first second is logged (`perf.second`).
