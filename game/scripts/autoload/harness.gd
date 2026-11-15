extends Node
## Harness autoload: debug-build launch arguments and a local command channel for agents and CI
## (spec 16.5; docs/harness.md). Inert in release builds, where begin returns before acting on
## any argument.
##
## User arguments (after `--`): --screenshot=<path>, --dump-state=<path>,
## --quit-after=<seconds>, --demo-screen=<name>, --demo-position=<name>, --mute,
## --harness-port=<port>. iOS launch arguments: -DemoScreen <name>, -DemoPosition <name>.
## Relative paths land under user://. A flag given twice keeps its last value. Every command
## logs harness.command. The scene's part is its harness_* hooks (main.gd): the screens, the free
## table's positions and shots, a match's start, strokes, choices and summaries.
##
## The channel (Windows only) listens on 127.0.0.1: newline-delimited JSON requests
## {"id", "cmd", "args"}, each answered {"id", "ok", "result" | "error"} in order, one at a time
## per client.

signal captures_done

## What --demo-screen takes. The first three open as open_screen does; "match" starts DEMO_MATCH,
## the person to break; the last three are shown for pictures, the summaries filled from
## EXAMPLE_MATCH_PATH, with no match behind them.
const SCREENS: Array[String] = ["home", "match_setup", "table", "match", "pause", "frame_summary", "match_summary"]
const OPEN_SCREENS: Array[String] = ["home", "match_setup", "table"]
const DEMO_SCREENS: Array[String] = ["pause", "frame_summary", "match_summary"]
const DEMO_MATCH := {"best_of": 3, "first_breaker": "you", "seed": 147}
const EXAMPLE_MATCH_PATH := "res://data/rules/example_match.json"
const BREAKERS: Array[String] = ["random", "you", "computer"]
const MATCH_LENGTHS: Array[int] = [1, 3, 5, 7, 9, 11]
const STABLE_SECONDS := 1.0
## The longest a capture or open_screen waits for the camera to land.
const SETTLE_TIMEOUT_SECONDS := 5.0
const CAPTURE_TIMEOUT_SECONDS := 10.0
const FLAGS := {
	"--screenshot": "screenshot",
	"--dump-state": "dump_state",
	"--quit-after": "quit_after",
	"--demo-screen": "demo_screen",
	"--demo-position": "demo_position",
	"--harness-port": "harness_port",
}
const SWITCHES := {"--mute": "mute"}
# iOS launch arguments come as `-Name value` pairs, which Godot does not treat as user arguments.
const LAUNCH_FLAGS := {"-DemoScreen": "--demo-screen", "-DemoPosition": "--demo-position"}
# Read by other autoloads (Link reads --no-link), so the harness accepts them silently.
const OTHER_FLAGS: Array[String] = ["--no-link"]
# The dev preview scenes' own (scripts/dev/screens_preview.gd, match_hud_preview.gd), with a value.
const PREVIEW_FLAGS: Array[String] = ["--screen", "--state", "--frames", "--person"]
const POSITIONS_DIR := "res://data/positions"
const MAX_LINE_BYTES := 1 << 20
const WAIT_IDLE_DEFAULT_SECONDS := 30.0
const MAX_FRAMES_BETWEEN := 600
const KEY_MODIFIERS: Array[String] = ["ctrl", "shift", "alt", "meta"]
const STRIKE_KEYS: Array[String] = ["aim_rad", "tip_x", "tip_y", "elevation_rad", "speed_m_s"]
const COMMANDS: Array[String] = ["ping", "load_position", "play_shot", "wait_idle", "set_setting", "dump_state",
		"screenshot", "overlay", "input", "open_screen", "start_match", "match_state", "match_shot", "match_choose",
		"match_wait", "match_autoplay", "match_continue", "quit"]
const OVERLAY_PATH := "Hud/Overlay"

## The scene commands act on; tests swap in a fake one.
var scene_provider: Callable = func() -> Node: return get_tree().current_scene
## Ends the game for the quit command; tests swap it out.
var quitter: Callable = func() -> void: get_tree().quit()

var _screen := "table"
var _captures_pending := false
var _server: TCPServer
var _port := -1
# Each {"peer", "buffer", "queue", "busy"}.
var _clients: Array[Dictionary] = []
var _quitting := false


static func parse_args(user_args: PackedStringArray, launch_args: PackedStringArray) -> Dictionary:
	var parsed := {
		"screenshot": "",
		"dump_state": "",
		"quit_after": -1.0,
		"demo_screen": "",
		"demo_position": "",
		"harness_port": -1,
		"mute": false,
		"invalid": PackedStringArray(),
		"repeated": PackedStringArray(),
	}
	var seen := {}
	var i := 0
	while i < user_args.size():
		var arg := user_args[i]
		i += 1
		if LAUNCH_FLAGS.has(arg):
			# A missing value must not swallow the next flag.
			if i < user_args.size() and not user_args[i].begins_with("-"):
				_store(parsed, seen, LAUNCH_FLAGS[arg], user_args[i])
				i += 1
			else:
				parsed["invalid"].append(arg)
			continue
		if OTHER_FLAGS.has(arg):
			continue
		if SWITCHES.has(arg):
			parsed[SWITCHES[arg]] = true
			continue
		var eq := arg.find("=")
		var key := arg.substr(0, eq) if eq >= 0 else arg
		var value := arg.substr(eq + 1) if eq >= 0 else ""
		if PREVIEW_FLAGS.has(key) and eq >= 0:
			continue
		var ok := FLAGS.has(key) and not value.is_empty()
		if ok and key == "--quit-after":
			ok = value.is_valid_float() and is_finite(value.to_float()) and value.to_float() >= 0.0
		elif ok and key == "--harness-port":
			ok = value.is_valid_int() and value.to_int() >= 0 and value.to_int() <= 65535
		if ok:
			_store(parsed, seen, key, value)
		else:
			parsed["invalid"].append(arg)
	for flag: String in LAUNCH_FLAGS:
		var at := launch_args.find(flag)
		if at >= 0 and at + 1 < launch_args.size() and not launch_args[at + 1].begins_with("-"):
			_store(parsed, seen, LAUNCH_FLAGS[flag], launch_args[at + 1])
	return parsed


static func _store(parsed: Dictionary, seen: Dictionary, flag: String, value: String) -> void:
	if seen.has(flag) and not (parsed["repeated"] as PackedStringArray).has(flag):
		parsed["repeated"].append(flag)
	seen[flag] = true
	var field: String = FLAGS[flag]
	if field == "quit_after":
		parsed[field] = value.to_float()
	elif field == "harness_port":
		parsed[field] = value.to_int()
	else:
		parsed[field] = value


static func resolve_path(path: String) -> String:
	return path if path.is_absolute_path() else "user://" + path


## Positions are core coordinates (x, y on the cloth, z the height of the centre above
## it), the same frame as the logs and the physics. x and y come from a ball's "core_xy"
## meta when it has one, so they keep full double precision.
static func build_state(scene: Node, screen: String, session: String, link_status: String, params_hash: String) -> Dictionary:
	var balls: Array[Dictionary] = []
	var parent := scene.get_node_or_null("Balls") if scene else null
	if parent:
		for ball: Node in parent.get_children():
			if not ball is Node3D:
				continue
			var p := (ball as Node3D).position
			var core := TableSpace.godot_to_core(p)
			var xy: Array = ball.get_meta("core_xy", [core.x, core.y])
			balls.append({
				"name": String(ball.name),
				"ball": String(ball.get_meta("ball", "")),
				"index": int(ball.get_meta("index", 0)),
				"position": [xy[0], xy[1], p.y],
			})
	return {
		"screen": screen,
		"session": session,
		"link_status": link_status,
		"params_hash": params_hash,
		"balls": balls,
	}


## A named position file (core/tests/fixtures/positions: "cue_ball" and "start": {"balls":
## [{ball, index, r: [x, y]}]}) as {"balls": the game's ball list, "cue_ball"}, or {"error"}.
static func position_from_fixture(fixture: Variant) -> Dictionary:
	if not fixture is Dictionary:
		return {"error": "not a JSON object"}
	var start: Variant = (fixture as Dictionary).get("start")
	if not start is Dictionary:
		return {"error": "no start object"}
	var converted := game_balls((start as Dictionary).get("balls"))
	if converted.has("error"):
		return converted
	var cue_ball: Variant = _integer((fixture as Dictionary).get("cue_ball", 0))
	if cue_ball == null:
		return {"error": "cue_ball is not an integer"}
	return {"balls": converted["balls"], "cue_ball": cue_ball}


## Ball entries {ball, index, x, y} or {ball, index, r: [x, y]} as the game's ball list
## [{ball, index, x, y}] (OfsPhysics), or {"error"} naming the first bad entry.
static func game_balls(entries: Variant) -> Dictionary:
	if not entries is Array or (entries as Array).is_empty():
		return {"error": "balls must be a non-empty array"}
	var balls: Array[Dictionary] = []
	var list: Array = entries
	for i: int in list.size():
		var entry: Variant = list[i]
		if not entry is Dictionary or not (entry as Dictionary).get("ball") is String:
			return {"error": "balls[%d] has no ball name" % i}
		var e: Dictionary = entry
		var xy: Variant = e.get("r", [e.get("x"), e.get("y")])
		var x: Variant = _finite((xy as Array)[0]) if xy is Array and (xy as Array).size() == 2 else null
		var y: Variant = _finite((xy as Array)[1]) if xy is Array and (xy as Array).size() == 2 else null
		var index: Variant = _integer(e.get("index", 0))
		if x == null or y == null or index == null:
			return {"error": "balls[%d] (%s) needs finite x and y (or r) and an integer index" % [i, e["ball"]]}
		balls.append({"ball": e["ball"], "index": index, "x": x, "y": y})
	return {"balls": balls}


## A synthesized InputEvent from a channel input spec (docs/harness.md), or the reason it can't
## be made. x, y, rx, ry are canvas units; to_window maps them to the window, where the
## viewport expects events to come from.
static func make_event(spec: Variant, to_window: Transform2D) -> Variant:
	if not spec is Dictionary:
		return "not an object"
	var e: Dictionary = spec
	var type: Variant = e.get("type")
	var at: Variant = _point(e, "x", "y")
	var moved: Variant = _point(e, "rx", "ry")
	var pressed: Variant = e.get("pressed")
	match type:
		"touch":
			var index: Variant = _integer(e.get("index", 0))
			if at == null or not pressed is bool or index == null:
				return "touch needs x, y, pressed and an integer index"
			var touch := InputEventScreenTouch.new()
			touch.index = index
			touch.pressed = pressed
			touch.position = to_window * (at as Vector2)
			return touch
		"drag":
			var index: Variant = _integer(e.get("index", 0))
			if at == null or index == null:
				return "drag needs x, y and an integer index"
			var drag := InputEventScreenDrag.new()
			drag.index = index
			drag.position = to_window * (at as Vector2)
			drag.relative = to_window.basis_xform(moved if moved != null else Vector2.ZERO)
			drag.screen_relative = drag.relative
			return drag
		"mouse_button":
			var button: Variant = _integer(e.get("button", MOUSE_BUTTON_LEFT))
			if at == null or not pressed is bool or button == null or button < 1 or button > 9:
				return "mouse_button needs x, y, pressed and a button 1-9"
			var click := InputEventMouseButton.new()
			click.button_index = button
			click.pressed = pressed
			click.position = to_window * (at as Vector2)
			click.global_position = click.position
			click.button_mask = (1 << (button - 1)) if pressed else 0
			click.factor = 1.0
			return click
		"mouse_motion":
			var mask: Variant = _integer(e.get("button_mask", 0))
			if at == null or mask == null:
				return "mouse_motion needs x, y and an integer button_mask"
			var motion := InputEventMouseMotion.new()
			motion.position = to_window * (at as Vector2)
			motion.global_position = motion.position
			motion.relative = to_window.basis_xform(moved if moved != null else Vector2.ZERO)
			motion.screen_relative = motion.relative
			motion.button_mask = mask
			return motion
		"key":
			if not pressed is bool:
				return "key needs pressed"
			if e.get("action") is String:
				var action: String = e["action"]
				if not InputMap.has_action(action):
					return "no input action %s" % action
				var event := InputEventAction.new()
				event.action = action
				event.pressed = pressed
				event.strength = 1.0 if pressed else 0.0
				return event
			var code: Variant = e.get("keycode")
			var key: int = KEY_NONE
			if code is String:
				key = OS.find_keycode_from_string(code)
			elif _integer(code) != null:
				key = _integer(code)
			if key == KEY_NONE:
				return "key needs an action or a keycode (a name such as \"F3\" or a number), got %s" % JSON.stringify(code)
			for modifier: String in KEY_MODIFIERS:
				if e.has(modifier) and not e[modifier] is bool:
					return "key's %s must be true or false, got %s" % [modifier, JSON.stringify(e[modifier])]
			var press := InputEventKey.new()
			press.keycode = key as Key
			press.physical_keycode = key as Key
			press.pressed = pressed
			press.ctrl_pressed = e.get("ctrl", false)
			press.shift_pressed = e.get("shift", false)
			press.alt_pressed = e.get("alt", false)
			press.meta_pressed = e.get("meta", false)
			return press
	return "unknown type %s (touch, drag, mouse_button, mouse_motion, key)" % JSON.stringify(type)


static func _finite(value: Variant) -> Variant:
	if (value is float or value is int) and is_finite(float(value)):
		return float(value)
	return null


# JSON numbers arrive as floats; a whole one counts as an integer.
static func _integer(value: Variant) -> Variant:
	if value is int:
		return value
	if value is float and is_finite(value) and value == floorf(value) and absf(value) < 9.0e15:
		return int(value)
	return null


static func _point(e: Dictionary, x_key: String, y_key: String) -> Variant:
	var x: Variant = _finite(e.get(x_key))
	var y: Variant = _finite(e.get(y_key))
	return Vector2(x, y) if x != null and y != null else null


func _ready() -> void:
	set_process(false)
	begin(OS.is_debug_build(), OS.get_name(), OS.get_cmdline_user_args(), OS.get_cmdline_args())


func begin(debug_build: bool, os_name: String, user_args: PackedStringArray, launch_args: PackedStringArray) -> void:
	if not debug_build:
		return
	process_mode = Node.PROCESS_MODE_ALWAYS
	var args := parse_args(user_args, launch_args)
	if args["mute"]:
		# Before any scene can make a sound: agent runs stay silent.
		AudioServer.set_bus_mute(0, true)
		_report("mute", [], true, "bus %s muted" % AudioServer.get_bus_name(0))
	_run.call_deferred(os_name, args)


func _run(os_name: String, args: Dictionary) -> void:
	for arg: String in args["invalid"]:
		_report("unknown", [arg], false, "unrecognised or malformed argument")
	for flag: String in args["repeated"]:
		_report("repeated", [flag], false, "given more than once; the last value is used")
	var port: int = args["harness_port"]
	if port >= 0:
		if os_name == "Windows":
			start_channel(port)
		else:
			_report("harness-port", [port], false, "the command channel runs only on Windows, not %s" % os_name)
	var demo: String = args["demo_screen"]
	if not demo.is_empty():
		# One frame, so the scene's own setup has run and Home is up before the screen replaces it.
		await get_tree().process_frame
		var shown: Dictionary = await _show_demo_screen(demo)
		_report("demo-screen", [demo], shown["ok"], shown.get("result", shown.get("error")))
	var position: String = args["demo_position"]
	if not position.is_empty():
		# One frame, so the scene's own setup has run before the position replaces its balls.
		await get_tree().process_frame
		var loaded := _load_position({"name": position})
		_report("demo-position", [position], loaded["ok"], loaded.get("result", loaded.get("error")))
	var quit_after: float = args["quit_after"]
	if quit_after >= 0.0:
		_quit_after(quit_after)
	var screenshot: String = args["screenshot"]
	var dump: String = args["dump_state"]
	if screenshot.is_empty() and dump.is_empty():
		return
	_captures_pending = true
	await get_tree().create_timer(STABLE_SECONDS, true, false, true).timeout
	await _settled(_scene())
	# The state needs no rendering, so it doesn't wait for a frame a headless run never draws.
	if not dump.is_empty():
		var written := _write_state(resolve_path(dump))
		_report("dump-state", [resolve_path(dump)], written["ok"], written.get("result", written.get("error")))
	if not screenshot.is_empty():
		var captured: Dictionary = await _capture(resolve_path(screenshot))
		_report("screenshot", [resolve_path(screenshot)], captured["ok"], captured.get("result", captured.get("error")))
	_captures_pending = false
	captures_done.emit()


## {"ok": true, "result": the file's absolute path} or {"ok": false, "error"}.
func _capture(path: String) -> Dictionary:
	if DisplayServer.get_name() == "headless":
		return _fail("the headless display server renders nothing")
	# In low-processor mode an unchanged scene is never redrawn, so ask for frames instead of
	# waiting for one.
	Perf.set_active(&"harness_capture", true)
	var drawn: bool = await _frame_drawn_within(CAPTURE_TIMEOUT_SECONDS)
	Perf.set_active(&"harness_capture", false)
	if not drawn:
		return _fail("no frame drawn within %d s" % int(CAPTURE_TIMEOUT_SECONDS))
	var texture := get_viewport().get_texture()
	var image := texture.get_image() if texture else null
	if image == null or image.is_empty():
		return _fail("no image from the viewport")
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var err := image.save_png(path)
	if err != OK:
		return _fail("saving %s failed: %s" % [ProjectSettings.globalize_path(path), error_string(err)])
	return _ok(ProjectSettings.globalize_path(path))


## build_state with the parameters waiting, the last second's perf numbers and, over them, the
## scene's own harness_state().
func full_state() -> Dictionary:
	var scene := _scene()
	var state := build_state(scene, _screen, Log.session(), Link.get_status(), Params.current_hash())
	state["params_pending_hash"] = Params.pending_hash()
	state["perf"] = Perf.last_summary()
	if scene and scene.has_method("harness_state"):
		var game: Variant = scene.call("harness_state")
		if game is Dictionary:
			state.merge(game, true)
	return state


func _write_state(path: String) -> Dictionary:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return _fail("opening %s failed: %s" % [ProjectSettings.globalize_path(path), error_string(FileAccess.get_open_error())])
	file.store_string(JSON.stringify(full_state(), "  ", true, true))
	file.close()
	return _ok(ProjectSettings.globalize_path(path))


func _frame_drawn_within(seconds: float) -> bool:
	var drawn := [false]
	var mark := func() -> void: drawn[0] = true
	RenderingServer.frame_post_draw.connect(mark)
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	while not drawn[0] and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	RenderingServer.frame_post_draw.disconnect(mark)
	return drawn[0]


func _quit_after(seconds: float) -> void:
	await get_tree().create_timer(seconds, true, false, true).timeout
	if _captures_pending:
		await captures_done
	_report("quit-after", [seconds], true, "")
	OfsLog.flush()
	get_tree().quit()


func _report(command: String, args: Array, ok: bool, result: Variant) -> void:
	var data := {"command": command, "args": args, "ok": ok, "result": result}
	if ok:
		Log.info("harness", "harness.command", data)
	else:
		Log.warn("harness", "harness.command", data)


## The port the channel listens on; -1 when it isn't open.
func channel_port() -> int:
	return _port


func start_channel(port: int) -> bool:
	stop_channel()
	var server := TCPServer.new()
	var err := server.listen(port, "127.0.0.1")
	if err != OK:
		_channel("error", port, "listening on 127.0.0.1:%d failed: %s" % [port, error_string(err)])
		return false
	_server = server
	_port = server.get_local_port()
	# harness_client.py reads this line to find a port picked with 0.
	print("harness listening on 127.0.0.1:%d" % _port)
	_channel("listening", _port)
	set_process(true)
	return true


func stop_channel() -> void:
	if _server == null:
		return
	for client: Dictionary in _clients:
		(client["peer"] as StreamPeerTCP).disconnect_from_host()
	_clients.clear()
	_server.stop()
	_server = null
	_channel("closed", _port)
	_port = -1
	set_process(false)


func _exit_tree() -> void:
	stop_channel()


func _process(_delta: float) -> void:
	if _server == null:
		return
	while _server.is_connection_available():
		var peer := _server.take_connection()
		_clients.append({"peer": peer, "buffer": PackedByteArray(), "queue": [], "busy": false})
		_channel("client", _port)
	for client: Dictionary in _clients.duplicate():
		_poll(client)


func _poll(client: Dictionary) -> void:
	var peer: StreamPeerTCP = client["peer"]
	peer.poll()
	if peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		_clients.erase(client)
		_channel("closed", _port, "" if peer.get_status() == StreamPeerTCP.STATUS_NONE else "client connection status %d" % peer.get_status())
		return
	var available := peer.get_available_bytes()
	if available > 0:
		var got: Array = peer.get_partial_data(available)
		if got[0] != OK:
			_drop(client, "reading from the client failed: %s" % error_string(got[0]))
			return
		var buffer: PackedByteArray = client["buffer"]
		buffer.append_array(got[1])
		var newline := buffer.find(10)
		while newline >= 0:
			var line := buffer.slice(0, newline).get_string_from_utf8().strip_edges()
			if not line.is_empty():
				(client["queue"] as Array).append(line)
			buffer = buffer.slice(newline + 1)
			newline = buffer.find(10)
		client["buffer"] = buffer
		if buffer.size() > MAX_LINE_BYTES:
			_send(client, {"id": null, "ok": false, "error": "a request line over %d bytes; closing" % MAX_LINE_BYTES})
			_drop(client, "a request line over %d bytes" % MAX_LINE_BYTES)
			return
	var queue: Array = client["queue"]
	if not client["busy"] and not queue.is_empty():
		_serve(client, queue.pop_front())


func _serve(client: Dictionary, line: String) -> void:
	client["busy"] = true
	var response: Dictionary = await handle_line(line)
	_send(client, response)
	client["busy"] = false
	if _quitting:
		OfsLog.flush()
		# Disconnecting sends what was written before the process goes.
		stop_channel()
		quitter.call()


func _send(client: Dictionary, response: Dictionary) -> void:
	var peer: StreamPeerTCP = client["peer"]
	if peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		return
	var err := peer.put_data((JSON.stringify(response, "", true, true) + "\n").to_utf8_buffer())
	if err != OK:
		_channel("error", _port, "answering request %s failed: %s" % [JSON.stringify(response.get("id")), error_string(err)])


func _drop(client: Dictionary, reason: String) -> void:
	(client["peer"] as StreamPeerTCP).disconnect_from_host()
	_clients.erase(client)
	_channel("error", _port, reason)


func _channel(state: String, port: int, error: String = "") -> void:
	var data := {"state": state, "port": port}
	if not error.is_empty():
		data["error"] = error
		Log.warn("harness", "harness.channel", data)
	else:
		Log.info("harness", "harness.channel", data)


func handle_line(line: String) -> Dictionary:
	var json := JSON.new()
	var parsed := json.parse(line) == OK
	var request: Variant = json.data if parsed else null
	if not request is Dictionary:
		var why := "a request is a JSON object" if parsed else "not JSON: %s" % json.get_error_message()
		_report("request", [line.left(200)], false, why)
		return {"id": null, "ok": false, "error": why}
	var id: Variant = (request as Dictionary).get("id")
	# Godot reads every JSON number as a float; a client's 1 should come back as 1, not 1.0.
	if _integer(id) != null:
		id = _integer(id)
	var cmd: Variant = (request as Dictionary).get("cmd")
	var args: Variant = (request as Dictionary).get("args", {})
	var outcome: Dictionary
	if not cmd is String:
		outcome = _fail("cmd must be a string")
	elif not args is Dictionary:
		outcome = _fail("args must be an object")
	else:
		outcome = await run_command(cmd, args)
	if not cmd is String or not args is Dictionary:
		_report(str(cmd), [args], false, outcome["error"])
	var response := {"id": id, "ok": outcome["ok"]}
	if outcome["ok"]:
		response["result"] = outcome["result"]
	else:
		response["error"] = outcome["error"]
	return response


## {"ok": true, "result"} or {"ok": false, "error"}; logged as harness.command.
func run_command(cmd: String, args: Dictionary) -> Dictionary:
	var outcome: Dictionary
	match cmd:
		"ping":
			outcome = _ok("pong")
		"load_position":
			outcome = _load_position(args)
		"play_shot":
			outcome = _play_shot(args)
		"wait_idle":
			outcome = await _wait_idle(args)
		"set_setting":
			outcome = _set_setting(args)
		"dump_state":
			outcome = _dump_state(args)
		"screenshot":
			if args.get("path") is String and not (args["path"] as String).is_empty():
				outcome = await _capture(resolve_path(args["path"]))
			else:
				outcome = _fail("path must be a non-empty string")
		"overlay":
			outcome = _overlay(args)
		"input":
			outcome = await _send_input(args)
		"open_screen":
			outcome = await _open_screen(args)
		"start_match":
			outcome = _start_match(args)
		"match_state":
			outcome = _ask_scene("harness_match_state", [])
		"match_shot":
			outcome = _match_shot(args)
		"match_choose":
			if args.get("kind") is String:
				outcome = _ask_scene("harness_match_choose", [args["kind"]])
			else:
				outcome = _fail("kind must be a decision name, got %s" % JSON.stringify(args.get("kind")))
		"match_wait":
			outcome = await _match_wait(args)
		"match_autoplay":
			if args.get("on") is bool and args.get("fast", false) is bool:
				outcome = _ask_scene("harness_match_autoplay", [args["on"], args.get("fast", false)])
			else:
				outcome = _fail("on must be true or false, and fast when given")
		"match_continue":
			outcome = _ask_scene("harness_match_continue", [])
		"quit":
			_quitting = true
			outcome = _ok("quitting")
		_:
			outcome = _fail("unknown command %s; known: %s" % [cmd, ", ".join(COMMANDS)])
	var logged: Variant = outcome.get("result", outcome.get("error"))
	if cmd == "dump_state" and outcome["ok"]:
		logged = "state of %d keys" % (outcome["result"] as Dictionary).size()
	elif cmd == "match_state" and outcome["ok"]:
		# The whole frame and match would make every poll an enormous line.
		var state: Dictionary = outcome["result"]
		logged = {"active": state.get("active"), "waiting": state.get("waiting"), "blocked": state.get("blocked")}
	_report(cmd, [_logged_args(cmd, args)], outcome["ok"], logged)
	return outcome


# A long gesture would make one enormous log line; its length is enough there.
static func _logged_args(cmd: String, args: Dictionary) -> Dictionary:
	if cmd != "input" or not args.get("events") is Array:
		return args
	var short := args.duplicate()
	short["events"] = "%d events" % (args["events"] as Array).size()
	return short


func _scene() -> Node:
	return scene_provider.call() as Node


func _hook(scene: Node, method: String) -> String:
	if scene == null:
		return "no current scene"
	if not scene.has_method(method):
		return "the scene (%s) has no %s" % [scene.name, method]
	return ""


func _load_position(args: Dictionary) -> Dictionary:
	var position: Dictionary
	if args.has("name"):
		position = load_position_file(str(args["name"]))
	elif args.has("balls"):
		position = game_balls(args["balls"])
		if not position.has("error"):
			position["cue_ball"] = _integer(args.get("cue_ball", 0))
			if position["cue_ball"] == null:
				position = {"error": "cue_ball must be an integer"}
	else:
		position = {"error": "needs a name, or balls and cue_ball"}
	if position.has("error"):
		return _fail(position["error"])
	var balls: Array = position["balls"]
	var cue_ball: int = position["cue_ball"]
	if cue_ball < 0 or cue_ball >= balls.size():
		return _fail("cue_ball %d is not one of the %d balls" % [cue_ball, balls.size()])
	var scene := _scene()
	var missing := _hook(scene, "harness_load_position")
	if not missing.is_empty():
		return _fail(missing)
	if not scene.call("harness_load_position", balls, cue_ball):
		return _fail("the scene refused the position (%d balls, cue ball %d)%s" % [balls.size(), cue_ball, _refusal(scene)])
	return _ok({"balls": balls.size(), "cue_ball": cue_ball})


## res://data/positions/<name>.json as {"balls", "cue_ball"}, or {"error"}.
static func load_position_file(position_name: String) -> Dictionary:
	if not position_name.is_valid_filename() or position_name.is_empty() or position_name.contains("."):
		return {"error": "%s is not a position name" % JSON.stringify(position_name)}
	var path := POSITIONS_DIR.path_join(position_name + ".json")
	if not FileAccess.file_exists(path):
		return {"error": "no position %s in %s (scripts/sync_data.py copies them from core/tests/fixtures/positions)" % [position_name, POSITIONS_DIR]}
	var json := JSON.new()
	if json.parse(FileAccess.get_file_as_string(path)) != OK:
		return {"error": "%s: %s at line %d" % [path, json.get_error_message(), json.get_error_line()]}
	var position := position_from_fixture(json.data)
	if position.has("error"):
		position["error"] = "%s: %s" % [path, position["error"]]
	return position


func _play_shot(args: Dictionary) -> Dictionary:
	var strike := {}
	for key: String in STRIKE_KEYS:
		var value: Variant = _finite(args.get(key))
		if value == null:
			return _fail("%s must be a finite number, got %s" % [key, JSON.stringify(args.get(key))])
		strike[key] = value
	var scene := _scene()
	var missing := _hook(scene, "harness_play_shot")
	if not missing.is_empty():
		return _fail(missing)
	var shot_id: Variant = scene.call("harness_play_shot", strike)
	if not shot_id is String or (shot_id as String).is_empty():
		var why: Variant = scene.call("harness_refusal") if scene.has_method("harness_refusal") else ""
		if why is String and not (why as String).is_empty():
			return _fail("the scene refused the shot: %s" % why)
		return _fail("the scene refused the shot (busy, or the binding rejected it; see shot.rejected)")
	return _ok({"shot_id": shot_id})


func _wait_idle(args: Dictionary) -> Dictionary:
	var timeout: Variant = _finite(args.get("timeout_s", WAIT_IDLE_DEFAULT_SECONDS))
	if timeout == null or timeout < 0.0:
		return _fail("timeout_s must be a number of seconds, 0 or more")
	var scene := _scene()
	var missing := _hook(scene, "harness_busy")
	if not missing.is_empty():
		return _fail(missing)
	var start := Time.get_ticks_msec()
	while scene.call("harness_busy"):
		if Time.get_ticks_msec() - start >= timeout * 1000.0:
			return _fail("still busy after %.1f s" % timeout)
		await get_tree().process_frame
		if not is_instance_valid(scene):
			return _fail("the scene went away while waiting")
	return _ok({"waited_s": (Time.get_ticks_msec() - start) / 1000.0})


## Opens one of OPEN_SCREENS and answers once the camera has landed there, so a screenshot next
## shows the screen as it stands.
func _open_screen(args: Dictionary) -> Dictionary:
	var screen: Variant = args.get("name")
	if not screen is String or not OPEN_SCREENS.has(screen):
		return _fail("name must be one of %s, got %s" % [", ".join(OPEN_SCREENS), JSON.stringify(screen)])
	var scene := _scene()
	var missing := _hook(scene, "harness_open_screen")
	if not missing.is_empty():
		return _fail(missing)
	if not scene.call("harness_open_screen", screen):
		return _fail("the scene did not open %s%s" % [screen, _refusal(scene)])
	_screen = screen
	await _settled(scene)
	return _ok({"screen": screen})


## A --demo-screen: {"ok": true, "result"} or {"ok": false, "error"}.
func _show_demo_screen(screen: String) -> Dictionary:
	if not SCREENS.has(screen):
		return _fail("unknown screen; known: %s" % ", ".join(SCREENS))
	if OPEN_SCREENS.has(screen):
		return await _open_screen({"name": screen})
	if screen == "match":
		return _ask_scene("harness_start_match", [DEMO_MATCH.duplicate(), false, false])
	var scene := _scene()
	var missing := _hook(scene, "harness_demo_screen")
	if not missing.is_empty():
		return _fail(missing)
	var json := JSON.new()
	if json.parse(FileAccess.get_file_as_string(EXAMPLE_MATCH_PATH)) != OK or not json.data is Dictionary:
		return _fail("%s is missing or not a JSON object (scripts/sync_data.py copies it from core/tests/fixtures/rules)"
				% EXAMPLE_MATCH_PATH)
	var example: Dictionary = json.data
	if not scene.call("harness_demo_screen", screen, finished_match(example) if screen == "match_summary" else example):
		return _fail("the scene did not show %s%s" % [screen, _refusal(scene)])
	_screen = screen
	return _ok(screen)


## A match (serialize.hpp) with its frame in play counted as finished, on the black to whoever
## leads it, and the match as over: a whole match for the match summary's picture.
static func finished_match(match_state: Dictionary) -> Dictionary:
	var out := match_state.duplicate(true)
	var frame: Variant = out.get("frame")
	var history: Variant = out.get("history")
	var frames: Variant = out.get("frames")
	if not (frame is Dictionary and history is Array and frames is Array and (frames as Array).size() == 2):
		return out
	var scores: Variant = (frame as Dictionary).get("scores")
	if not (scores is Array and (scores as Array).size() == 2):
		return out
	var winner := 0 if float(scores[0]) > float(scores[1]) else 1
	(history as Array).append({"number": (history as Array).size() + 1, "breaker": out.get("breaker", 0), "winner": winner,
			"reason": "black", "scores": scores, "stats": (frame as Dictionary).get("stats", [{}, {}]),
			"strokes": (frame as Dictionary).get("strokes", 0), "reracks": 0})
	frames[winner] = float(frames[winner]) + 1.0
	out["over"] = true
	out["winner"] = 0 if float(frames[0]) > float(frames[1]) else 1
	return out


## Waits for the scene's camera to land (its harness_settling), SETTLE_TIMEOUT_SECONDS at most.
func _settled(scene: Node) -> void:
	if scene == null or not scene.has_method("harness_settling"):
		return
	var deadline := Time.get_ticks_msec() + int(SETTLE_TIMEOUT_SECONDS * 1000.0)
	while is_instance_valid(scene) and scene.call("harness_settling") and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame


## Calls a scene hook that answers a Dictionary, with "error" when it refuses.
func _ask_scene(method: String, hook_args: Array) -> Dictionary:
	var scene := _scene()
	var missing := _hook(scene, method)
	if not missing.is_empty():
		return _fail(missing)
	var answer: Variant = scene.callv(method, hook_args)
	if not answer is Dictionary:
		return _fail("the scene's %s answered %s, not a Dictionary" % [method, type_string(typeof(answer))])
	if (answer as Dictionary).has("error"):
		return _fail(str(answer["error"]))
	return _ok(answer)


## ": <why>" when the scene says why its last hook refused, else "; see its log".
func _refusal(scene: Node) -> String:
	var why: Variant = scene.call("harness_refusal") if scene != null and scene.has_method("harness_refusal") else ""
	return ": %s" % why if why is String and not (why as String).is_empty() else "; see its log"


func _start_match(args: Dictionary) -> Dictionary:
	var best_of: Variant = _integer(args.get("best_of", 3))
	if best_of == null or not MATCH_LENGTHS.has(best_of):
		return _fail("best_of must be 1, 3, 5, 7, 9 or 11, got %s" % JSON.stringify(args.get("best_of")))
	var breaker: Variant = args.get("first_breaker", "random")
	if not breaker is String or not BREAKERS.has(breaker):
		return _fail("first_breaker must be one of %s, got %s" % [", ".join(BREAKERS), JSON.stringify(breaker)])
	var config := {"best_of": best_of, "first_breaker": breaker}
	if args.has("seed"):
		var match_seed: Variant = _integer(args["seed"])
		if match_seed == null or match_seed < 0:
			return _fail("seed must be a whole number, 0 or more, got %s" % JSON.stringify(args["seed"]))
		config["seed"] = match_seed
	for flag: String in ["autoplay", "fast"]:
		if not args.get(flag, false) is bool:
			return _fail("%s must be true or false, got %s" % [flag, JSON.stringify(args[flag])])
	return _ask_scene("harness_start_match", [config, args.get("autoplay", false), args.get("fast", false)])


func _match_shot(args: Dictionary) -> Dictionary:
	var strike := {}
	for key: String in STRIKE_KEYS:
		var value: Variant = _finite(args.get(key))
		if value == null:
			return _fail("%s must be a finite number, got %s" % [key, JSON.stringify(args.get(key))])
		strike[key] = value
	var nominated: Variant = args.get("nominated", "")
	if not nominated is String:
		return _fail("nominated must be a colour's name, got %s" % JSON.stringify(nominated))
	var cue: Variant = null
	if args.has("cue"):
		var at: Variant = args["cue"]
		var x: Variant = _finite((at as Array)[0]) if at is Array and (at as Array).size() == 2 else null
		var y: Variant = _finite((at as Array)[1]) if at is Array and (at as Array).size() == 2 else null
		if x == null or y == null:
			return _fail("cue must be [x, y] in core metres, got %s" % JSON.stringify(at))
		cue = Vector2(x, y)
	return _ask_scene("harness_match_shot", [strike, nominated, cue])


## Answers once the match waits for the person (a stroke, a choice, a summary to leave).
func _match_wait(args: Dictionary) -> Dictionary:
	var timeout: Variant = _finite(args.get("timeout_s", WAIT_IDLE_DEFAULT_SECONDS))
	if timeout == null or timeout < 0.0:
		return _fail("timeout_s must be a number of seconds, 0 or more")
	var start := Time.get_ticks_msec()
	var asked := _ask_scene("harness_match_waiting", [])
	while asked["ok"] and (asked["result"] as Dictionary).get("person") != true:
		if Time.get_ticks_msec() - start >= timeout * 1000.0:
			return _fail("still waiting on \"%s\" after %.1f s" % [(asked["result"] as Dictionary).get("waiting"), timeout])
		await get_tree().process_frame
		asked = _ask_scene("harness_match_waiting", [])
	if not asked["ok"]:
		return asked
	return _ok({"waiting": (asked["result"] as Dictionary).get("waiting"),
			"waited_s": (Time.get_ticks_msec() - start) / 1000.0})


func _set_setting(args: Dictionary) -> Dictionary:
	if not args.get("key") is String or not args.has("value"):
		return _fail("needs key (a string) and value")
	var key: String = args["key"]
	var value: Variant = args["value"]
	if Settings.get_value(key) is int and _integer(value) != null:
		value = _integer(value)
	if not Settings.set_value(key, value):
		return _fail("Settings refused %s = %s (see settings.changed)" % [key, JSON.stringify(value)])
	return _ok({"key": key, "value": Settings.get_value(key)})


func _dump_state(args: Dictionary) -> Dictionary:
	if not args.has("path"):
		return _ok(full_state())
	if not args["path"] is String or (args["path"] as String).is_empty():
		return _fail("path must be a non-empty string")
	var written := _write_state(resolve_path(args["path"]))
	if not written["ok"]:
		return written
	var state := full_state()
	state["path"] = written["result"]
	return _ok(state)


func _overlay(args: Dictionary) -> Dictionary:
	if not args.get("visible") is bool:
		return _fail("visible must be true or false")
	var scene := _scene()
	var overlay := scene.get_node_or_null(OVERLAY_PATH) if scene else null
	if overlay == null or not overlay.has_method("set_shown"):
		return _fail("the scene has no debug overlay at %s" % OVERLAY_PATH)
	overlay.call("set_shown", args["visible"], "harness")
	return _ok({"visible": overlay.get("visible")})


func _send_input(args: Dictionary) -> Dictionary:
	if not args.get("events") is Array or (args["events"] as Array).is_empty():
		return _fail("events must be a non-empty array")
	var frames: Variant = _integer(args.get("frames_between", 1))
	if frames == null or frames < 0 or frames > MAX_FRAMES_BETWEEN:
		return _fail("frames_between must be an integer from 0 to %d" % MAX_FRAMES_BETWEEN)
	var to_window := get_viewport().get_final_transform()
	var events: Array[InputEvent] = []
	var specs: Array = args["events"]
	for i: int in specs.size():
		var made: Variant = make_event(specs[i], to_window)
		if made is String:
			return _fail("events[%d]: %s" % [i, made])
		events.append(made)
	for i: int in events.size():
		Input.parse_input_event(events[i])
		if i < events.size() - 1:
			for _frame: int in frames:
				await get_tree().process_frame
	# Buffered events reach the scene at the start of the next frame.
	await get_tree().process_frame
	return _ok({"sent": events.size()})


static func _ok(result: Variant) -> Dictionary:
	return {"ok": true, "result": result}


static func _fail(error: String) -> Dictionary:
	return {"ok": false, "error": error}
