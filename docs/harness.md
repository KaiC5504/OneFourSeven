# Debug harness

How agents drive and inspect the real game (spec 16.5). Godot draws its own UI, which desktop
automation can't read, so the debug build takes launch arguments and, on Windows, a command
channel on 127.0.0.1. Release builds (TestFlight included) ignore all of it. Every flag and
command logs `harness.command`; the channel logs `harness.channel` (`docs/log-schema.md`).

## Quick start

```
python scripts/run_debug.py -- --quit-after=0      # builds the Debug extension once; the game quits at once
python scripts/background.py python tools/harness_client.py run --resolution 1912x880 -- --no-link < commands.jsonl
```

`scripts/background.py` keeps the game's window off the screen; every agent run goes through it.

with `commands.jsonl` holding one request per line:

```
{"cmd": "ping"}
{"cmd": "open_screen", "args": {"name": "table"}}
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
| `--demo-screen=<name>` | Opens a screen one frame after the scene is ready: `home` (where the app opens anyway), `match_setup` or `table` as `open_screen` does; `match` starts a best of 3 with seed 147 and the person to break; `pause`, `frame_summary` and `match_summary` show that screen for a picture with no match behind it, the summaries filled from `res://data/rules/example_match.json` (the match summary with its frame in play counted as the last). |
| `--screenshot=<path>` | Saves a PNG after one second, and once the camera has landed where it is going. Not headless: that renders nothing. |
| `--dump-state=<path>` | Writes the state (as `dump_state` below) at the same moment. |
| `--quit-after=<seconds>` | Quits, after any capture above has finished. |
| `--no-link` | Read by the phone link: starts it switched off. |

iOS passes launch arguments as `-Name value` pairs, which the CI simulator run uses:
`-DemoScreen table -DemoPosition straight_1m`.

The dev preview scenes' own arguments (`--screen=`, `--state=`, `--frames=`, `--person=`) are
passed over without a warning.

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
| `open_screen` | `name`: `home`, `match_setup` or `table` | `{"screen"}`, once the camera has landed there. A match on the table is left (abandoned) for it |
| `load_position` | `{"name"}`, or `{"balls", "cue_ball"}` | `{"balls": n, "cue_ball"}`; an error when the scene refuses the position. The free table's: from Home it enters the free table, and during a match it is refused, saying so |
| `play_shot` | `aim_rad`, `tip_x`, `tip_y`, `elevation_rad`, `speed_m_s` (all required, finite) | `{"shot_id"}`; an error when the scene refuses the shot. The strike plays exactly as given: the aim is not turned for side as a player's shot is. The free table's, as `load_position` |
| `wait_idle` | `timeout_s` (default 30) | `{"waited_s"}` once no shot is playing; an error at the timeout |
| `set_setting` | `key`, `value` | `{"key", "value"}` as stored; an error when Settings refuses it (`settings.changed` says why) |
| `dump_state` | `path` (optional) | the state, also written to `path` when given (the result then has `path`) |
| `screenshot` | `path` | the PNG's absolute path |
| `overlay` | `visible` (true or false) | `{"visible"}`: the debug overlay, as F3 or a three-finger tap toggles it |
| `input` | `events`, `frames_between` (default 1) | `{"sent": n}` after the last event has reached the scene |
| `start_match` | `best_of` (1, 3, 5, 7, 9 or 11; default 3), `first_breaker` (`random`, `you`, `computer`; default `random`), `seed` (optional), `autoplay`, `fast` (default false) | `{"match_id"}`; an error when the engine refuses (`app.error` says why) |
| `match_state` | | `MatchController.harness_state()`: `active`, `match_id`, `frame` (the frame view), `match`, `nominated`, `placing`, `blocked`, `autoplay`, `waiting` |
| `match_shot` | a strike as `play_shot`'s, `nominated` (a colour, optional), `cue` `[x, y]` (optional, when the cue ball is in hand) | `{"shot_id"}`: the person's stroke exactly as given. The in-hand cue ball is put at `cue`, or left where it is, and confirmed; without `nominated` the aim line declares as it does for a player |
| `match_choose` | `kind` (a decision name offered: `play`, `play_again`, `replace`, ...) | `{"kind"}` |
| `match_wait` | `timeout_s` (default 30) | `{"waiting", "waited_s"}` once the match waits for the person: `shot`, `choice`, `frame_summary` or `match_summary`; an error at the timeout or with no match on the table |
| `match_autoplay` | `on`, `fast` (default false) | `{"on", "fast"}`: the stand-in plays the person's strokes and choices too; `fast` skips each stroke's playback and every wait |
| `match_pause` | `on` | `{"on", "can_concede"}`: opens the pause menu as the scoreboard's button does, or resumes from it. While it is up the opponent waits; `match_choose` with `concede_frame` or `concede_match` concedes from it. An error unless the match's own screen (to open) or the menu (to resume) is showing |
| `match_continue` | | `{"screen"}`: from a frame summary to the next frame (after the last, to the match summary), from the match summary to Home |
| `quit` | | `"quitting"`, then the game exits |

`load_position` with `balls` takes the game's ball list, `[{"ball": "red", "index": 3, "x":
0.1, "y": 0.2}, ...]` in core metres (`x` towards the black, `y` to the left seen from baulk),
or the fixtures' form with `"r": [x, y]`; `cue_ball` is the cue ball's position in that list.

`dump_state` holds `screen`, `session`, `link_status`, `params_hash`, `params_pending_hash`,
`perf` (the last second's fps, frame times, dropped frames, thermal state, memory footprint and
idle) and `balls` (`name`, `ball`, `index`, `position` [x, y, height] in core metres), overlaid
by the game's own state (`harness_state()` in `main.gd`: screen, mode, ball list, shot setup,
settings, last shot, camera, break, `practice`, the practice shot's position name or "", and
`match`, what `match_state` answers).

| Field | What |
| --- | --- |
| `camera.pitch_deg`, `camera.zoom_scale` | the aiming camera's tilt and zoom (spec 8.5) |
| `camera.view` | `aim` (behind the cue ball, including the swoop back to it), `shot` (a shot holds the camera: still behind the cue ball for the stroke, then cut to the full-table view), `pocket` (the shot cycle's camera behind the pocket of the first object ball to drop) or `overview` |
| `break` | the running break as the counter shows it: the shot's pots count up as they drop, and it is 0 after a shot that pots nothing or the cue ball, a retake, undo, redo, setup, edit mode or `load_position`. In a match, the frame's break as the scoreboard shows it |
| `screen` | `home`, `match_setup`, `table`, `match`, `pause`, `frame_summary` or `match_summary`, as `screen.changed` logged it |
| `mode` | `home`, `free_table` or `match`: whose balls are on the table (`balls`, `cue_ball`) |

`wait_idle` returns once the shot has settled, as the camera starts swooping back behind the cue
ball; the swoop takes about 1.1 s more (`CameraRig.RETURN_S`).

### A match

The app opens at Home. `start_match` starts a match from any screen; with `autoplay` the stand-in
plays both sides, and with `fast` no stroke is watched. A match played by hand through the
channel:

```
{"cmd": "start_match", "args": {"best_of": 1, "first_breaker": "you", "seed": 147}}
{"cmd": "match_shot", "args": {"cue": [-1.2, 0.05], "aim_rad": 0.02, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 3.0}}
{"cmd": "match_wait", "args": {"timeout_s": 60}}
{"cmd": "match_state"}
```

`match_wait` answers when the person must act: `shot` (their stroke; `match_state`'s `placing`
and `blocked` say what it still needs), `choice` (`frame.view.options[0]` lists what
`match_choose` takes), or a summary for `match_continue`. The in-hand cue ball is placed and
confirmed by `match_shot`'s `cue`; `match_pause` opens the pause menu, and `open_screen` with
`home` leaves the match as Quit does. A place outside the D is refused, with `rules.invalid` and
an `app.error` in the log.

`python tools/play_match.py` plays a whole match this way with the stand-in on both sides
(`--best-of`, `--seed`, `--first-breaker`, `--slow`, `--out`), in a user folder of its own under
`build/play-match`, saves the state and a picture at the match summary, and then has
`ofs_sim_cli referee` (the Debug build's, or `$OFS_SIM_CLI`) re-judge every `rules.decision` of
the session log. It prints the strokes, each frame and the result, and exits 0 only when the
match finished, the log fits the schema with no `app.error` or `rules.invalid`, and the referee
agrees. It hides its own window. A best of 3 takes under ten seconds. The seed is the match's
(the rack, who breaks): the stand-in's choices are fresh each run, so one seed gives different
matches.

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
| `key` | `pressed`, and `action` (an input action such as `shoot` or `overlay_toggle`) or `keycode` (a name such as `"F3"`, `"Space"`, or a number); with `keycode`, `ctrl`, `shift`, `alt`, `meta` (default false) hold modifiers down |

A one-finger aim drag, then the overlay:

```
{"cmd": "input", "args": {"events": [
  {"type": "touch", "pressed": true, "x": 900, "y": 500},
  {"type": "drag", "x": 940, "y": 505, "rx": 40, "ry": 5},
  {"type": "touch", "pressed": false, "x": 940, "y": 505},
  {"type": "key", "keycode": "F3", "pressed": true},
  {"type": "key", "keycode": "F3", "pressed": false}]}}
```

Undo then redo in the free table, as Ctrl+Z and Ctrl+Y (the `undo` and `redo` actions):

```
{"cmd": "input", "args": {"events": [
  {"type": "key", "keycode": "Z", "ctrl": true, "pressed": true},
  {"type": "key", "keycode": "Z", "ctrl": true, "pressed": false},
  {"type": "key", "keycode": "Y", "ctrl": true, "pressed": true},
  {"type": "key", "keycode": "Y", "ctrl": true, "pressed": false}]}}
```

A first touch also arrives as a left mouse button (Godot's mouse-from-touch emulation).

## The debug overlay

Hidden by default, in every build: F3, a three-finger tap or the `overlay` command shows it
top-right inside the safe area. It shows the build, fps, frame time average, p95 and max,
dropped frames, the last shot's simulation time and the last guide prediction's time, thermal
state, memory footprint, the parameter hash in use and any waiting for the next shot, and the
link status. It refreshes at most twice a second and only while shown, so it never keeps an
idle game drawing. Its numbers start once the first second is logged (`perf.second`).
