extends TestCase

const HarnessScript := preload("res://scripts/autoload/harness.gd")


func test_parses_user_args() -> void:
	var p := HarnessScript.parse_args(
		PackedStringArray(["--screenshot=shots/a.png", "--dump-state=C:/tmp/s.json", "--quit-after=4", "--demo-screen=table"]),
		PackedStringArray())
	check_eq(p["screenshot"], "shots/a.png", "screenshot")
	check_eq(p["dump_state"], "C:/tmp/s.json", "dump-state")
	check_eq(p["quit_after"], 4.0, "quit-after")
	check_eq(p["demo_screen"], "table", "demo-screen")
	check_eq(p["invalid"].size(), 0, "no invalid args")


func test_defaults_when_no_args() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(), PackedStringArray())
	check_eq(p["screenshot"], "", "screenshot")
	check_eq(p["quit_after"], -1.0, "quit-after off")
	check_eq(p["demo_screen"], "", "demo-screen")


func test_ios_style_demo_screen() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(), PackedStringArray(["--some-engine-flag", "-DemoScreen", "table"]))
	check_eq(p["demo_screen"], "table", "from launch args")
	p = HarnessScript.parse_args(PackedStringArray(["-DemoScreen", "table"]), PackedStringArray())
	check_eq(p["demo_screen"], "table", "from user args")
	check_eq(p["invalid"].size(), 0, "the value is not an invalid arg")


func test_flags_bad_and_unknown_args() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(["--quit-after=soon", "--quit-after=-2", "--screenshot=", "--frobnicate=1"]), PackedStringArray())
	check_eq(p["quit_after"], -1.0, "bad quit-after ignored")
	check_eq(p["invalid"], PackedStringArray(["--quit-after=soon", "--quit-after=-2", "--screenshot=", "--frobnicate=1"]), "invalid")


func test_quit_after_must_be_finite() -> void:
	var args := PackedStringArray(["--quit-after=1e400", "--quit-after=inf", "--quit-after=nan", "--quit-after=-0.5"])
	var p := HarnessScript.parse_args(args, PackedStringArray())
	check_eq(p["quit_after"], -1.0, "rejected")
	check_eq(p["invalid"], args, "all reported")
	check_eq(HarnessScript.parse_args(PackedStringArray(["--quit-after=0"]), PackedStringArray())["quit_after"], 0.0, "zero is allowed")


func test_demo_screen_without_a_value_keeps_the_next_flag() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(["-DemoScreen", "--quit-after=3"]), PackedStringArray())
	check_eq(p["demo_screen"], "", "no demo screen")
	check_eq(p["quit_after"], 3.0, "--quit-after still read")
	check_eq(p["invalid"], PackedStringArray(["-DemoScreen"]), "-DemoScreen reported")
	p = HarnessScript.parse_args(PackedStringArray(["-DemoScreen"]), PackedStringArray(["-DemoScreen", "-v"]))
	check_eq(p["demo_screen"], "", "launch args: a flag is not a screen name")
	check_eq(p["invalid"], PackedStringArray(["-DemoScreen"]), "trailing -DemoScreen reported")


func test_repeated_flags_keep_the_last_value() -> void:
	var p := HarnessScript.parse_args(
		PackedStringArray(["--quit-after=3", "--screenshot=a.png", "--quit-after=5", "--quit-after=7", "-DemoScreen", "table", "--demo-screen=other"]),
		PackedStringArray())
	check_eq(p["quit_after"], 7.0, "last quit-after")
	check_eq(p["demo_screen"], "other", "last demo screen")
	check_eq(p["screenshot"], "a.png", "screenshot")
	check_eq(p["repeated"], PackedStringArray(["--quit-after", "--demo-screen"]), "repeated, each once")
	check_eq(p["invalid"].size(), 0, "nothing invalid")


func test_flags_for_other_autoloads_are_accepted() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(["--no-link", "--quit-after=2"]), PackedStringArray())
	check_eq(p["invalid"].size(), 0, "--no-link is not reported")
	check_eq(p["quit_after"], 2.0, "quit-after")


func test_relative_paths_go_under_user() -> void:
	check_eq(HarnessScript.resolve_path("shots/a.png"), "user://shots/a.png", "relative")
	check_eq(HarnessScript.resolve_path("C:/tmp/a.png"), "C:/tmp/a.png", "windows absolute")
	check_eq(HarnessScript.resolve_path("user://a.png"), "user://a.png", "user://")


func test_state_lists_balls_in_core_coordinates() -> void:
	var scene := Node3D.new()
	var balls := Node3D.new()
	balls.name = "Balls"
	scene.add_child(balls)
	var pink := Node3D.new()
	pink.name = "pink"
	pink.set_meta("ball", "pink")
	pink.set_meta("index", 0)
	pink.position = TableSpace.core_to_godot(0.89225, 0.0, 0.02625)
	balls.add_child(pink)
	var state := HarnessScript.build_state(scene, "table", "s1", "searching", "abc")
	scene.free()
	check_eq(state["screen"], "table", "screen")
	check_eq(state["session"], "s1", "session")
	check_eq(state["link_status"], "searching", "link_status")
	check_eq(state["params_hash"], "abc", "params_hash")
	check_eq(state["balls"].size(), 1, "ball count")
	var ball: Dictionary = state["balls"][0]
	check_eq(ball["name"], "pink", "name")
	check_eq(ball["ball"], "pink", "ball")
	check_eq(ball["index"], 0, "index")
	check_near(ball["position"][0], 0.89225, 1e-6, "core x")
	check_near(ball["position"][1], 0.0, 1e-6, "core y")
	check_near(ball["position"][2], 0.02625, 1e-6, "height")
	check(JSON.parse_string(JSON.stringify(state)) is Dictionary, "state serialises to JSON")


func test_state_prefers_double_precision_core_xy() -> void:
	var scene := Node3D.new()
	var balls := Node3D.new()
	balls.name = "Balls"
	scene.add_child(balls)
	var cue := Node3D.new()
	cue.name = "cue"
	cue.set_meta("core_xy", [-1.0475, -0.146])
	cue.position = TableSpace.core_to_godot(-1.0475, -0.146, 0.02625)
	balls.add_child(cue)
	var state := HarnessScript.build_state(scene, "table", "", "off", "")
	scene.free()
	check_eq(state["balls"][0]["position"][0], -1.0475, "exact core x")
	check_eq(state["balls"][0]["position"][1], -0.146, "exact core y")


func test_parses_m2_flags() -> void:
	var p := HarnessScript.parse_args(
		PackedStringArray(["--mute", "--harness-port=0", "--demo-position=straight_1m"]), PackedStringArray())
	check(p["mute"], "mute")
	check_eq(p["harness_port"], 0, "port 0 picks a free port")
	check_eq(p["demo_position"], "straight_1m", "demo-position")
	check_eq(p["invalid"].size(), 0, "nothing invalid")
	p = HarnessScript.parse_args(PackedStringArray(), PackedStringArray())
	check(not p["mute"], "not muted by default")
	check_eq(p["harness_port"], -1, "no channel by default")
	check_eq(p["demo_position"], "", "no demo position")


func test_flags_bad_ports_and_valued_mute() -> void:
	var args := PackedStringArray(["--harness-port=70000", "--harness-port=-1", "--harness-port=abc", "--harness-port=", "--mute=1"])
	var p := HarnessScript.parse_args(args, PackedStringArray())
	check_eq(p["harness_port"], -1, "no channel")
	check(not p["mute"], "--mute=1 is not --mute")
	check_eq(p["invalid"], args, "all reported")


func test_ios_style_demo_position() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(), PackedStringArray(["-DemoScreen", "table", "-DemoPosition", "lag"]))
	check_eq(p["demo_position"], "lag", "from launch args")
	check_eq(p["demo_screen"], "table", "with -DemoScreen")
	p = HarnessScript.parse_args(PackedStringArray(["-DemoPosition", "lag"]), PackedStringArray())
	check_eq(p["demo_position"], "lag", "from user args")
	p = HarnessScript.parse_args(PackedStringArray(["-DemoPosition", "--mute"]), PackedStringArray())
	check_eq(p["demo_position"], "", "a flag is not a position name")
	check(p["mute"], "and is still read")
	check_eq(p["invalid"], PackedStringArray(["-DemoPosition"]), "reported")


func test_named_position_becomes_the_game_ball_list() -> void:
	var p := HarnessScript.load_position_file("straight_1m")
	check(not p.has("error"), "loads: %s" % p.get("error", ""))
	if p.has("error"):
		return
	check_eq(p["cue_ball"], 0, "cue ball")
	check_eq(p["balls"], [{"ball": "cue", "index": 0, "x": -0.5, "y": 0.0},
			{"ball": "red", "index": 0, "x": 0.5, "y": 0.0}] as Array[Dictionary], "balls")
	check(p["balls"][0]["index"] is int, "index is an integer")


func test_every_synced_position_loads() -> void:
	var count := 0
	for file in DirAccess.get_files_at(HarnessScript.POSITIONS_DIR):
		if not file.ends_with(".json"):
			continue
		count += 1
		var p := HarnessScript.load_position_file(file.get_basename())
		check(not p.has("error"), "%s: %s" % [file, p.get("error", "")])
	check(count >= 10, "positions synced into game/data/positions (%d)" % count)


func test_bad_position_names_and_files() -> void:
	for bad: String in ["", "../table_params", "a.b", "a/b", "no_such_position"]:
		check(HarnessScript.load_position_file(bad).has("error"), "refused: %s" % bad)
	check(HarnessScript.position_from_fixture([]).has("error"), "not an object")
	check(HarnessScript.position_from_fixture({"cue_ball": 0}).has("error"), "no start")
	check(HarnessScript.position_from_fixture({"cue_ball": 0.5, "start": {"balls": [{"ball": "cue", "r": [0, 0]}]}}).has("error"),
			"cue_ball not an integer")
	var bad := HarnessScript.game_balls([{"ball": "cue", "x": 0, "y": 0}, {"ball": "red", "r": [1.0]}])
	check(str(bad.get("error", "")).begins_with("balls[1]"), "names the bad entry: %s" % bad)
	check(HarnessScript.game_balls([{"ball": "red", "x": INF, "y": 0}]).has("error"), "non-finite")
	check(HarnessScript.game_balls([]).has("error"), "empty")
	check_eq(HarnessScript.game_balls([{"ball": "red", "index": 3.0, "x": 1, "y": 2}])["balls"],
			[{"ball": "red", "index": 3, "x": 1.0, "y": 2.0}] as Array[Dictionary], "x, y form")


func test_events_are_made_in_window_coordinates() -> void:
	var to_window := Transform2D(0.0, Vector2(1.5, 1.5), 0.0, Vector2(10, 0))
	var touch: Variant = HarnessScript.make_event({"type": "touch", "index": 1, "pressed": true, "x": 100, "y": 40}, to_window)
	check(touch is InputEventScreenTouch, "touch")
	if touch is InputEventScreenTouch:
		check_eq((touch as InputEventScreenTouch).position, Vector2(160, 60), "scaled and offset")
		check_eq((touch as InputEventScreenTouch).index, 1, "index")
	var drag: Variant = HarnessScript.make_event({"type": "drag", "x": 0, "y": 0, "rx": 10, "ry": -4}, to_window)
	check(drag is InputEventScreenDrag and (drag as InputEventScreenDrag).relative == Vector2(15, -6), "drag relative scaled, not offset")
	var click: Variant = HarnessScript.make_event({"type": "mouse_button", "button": 2, "pressed": true, "x": 0, "y": 0}, to_window)
	check(click is InputEventMouseButton and (click as InputEventMouseButton).button_index == MOUSE_BUTTON_RIGHT, "right button")
	if click is InputEventMouseButton:
		check_eq((click as InputEventMouseButton).global_position, Vector2(10, 0), "global position")
		check_eq(int((click as InputEventMouseButton).button_mask), 2, "mask")
	var motion: Variant = HarnessScript.make_event({"type": "mouse_motion", "x": 2, "y": 2, "button_mask": 1}, to_window)
	check(motion is InputEventMouseMotion and int((motion as InputEventMouseMotion).button_mask) == 1, "motion")
	var action: Variant = HarnessScript.make_event({"type": "key", "action": "overlay_toggle", "pressed": true}, to_window)
	check(action is InputEventAction and (action as InputEventAction).action == &"overlay_toggle", "action")
	var key: Variant = HarnessScript.make_event({"type": "key", "keycode": "F3", "pressed": true}, to_window)
	check(key is InputEventKey and (key as InputEventKey).physical_keycode == KEY_F3, "named key")
	check(key is InputEventKey and (key as InputEventKey).is_action_pressed("overlay_toggle"), "matches the F3 action")
	check(key is InputEventKey and (key as InputEventKey).get_modifiers_mask() == 0, "no modifiers unless asked")
	var undo: Variant = HarnessScript.make_event({"type": "key", "keycode": "Z", "ctrl": true, "pressed": true}, to_window)
	check(undo is InputEventKey and (undo as InputEventKey).is_action_pressed("undo", false, true), "Ctrl+Z is the undo action")
	var plain_z: Variant = HarnessScript.make_event({"type": "key", "keycode": "Z", "pressed": true}, to_window)
	check(plain_z is InputEventKey and not (plain_z as InputEventKey).is_action_pressed("undo"), "Z alone is not")
	var all: Variant = HarnessScript.make_event({"type": "key", "keycode": "Y", "ctrl": true, "shift": true, "alt": true,
			"meta": true, "pressed": true}, to_window)
	check(all is InputEventKey and (all as InputEventKey).get_modifiers_mask()
			== KEY_MASK_CTRL | KEY_MASK_SHIFT | KEY_MASK_ALT | KEY_MASK_META, "every modifier")
	check(HarnessScript.make_event({"type": "key", "keycode": "Z", "ctrl": 1, "pressed": true}, to_window) is String,
			"a modifier must be a boolean")
	for bad: Variant in [{"type": "touch", "x": 1, "y": 2}, {"type": "touch", "pressed": true, "x": "a", "y": 2},
			{"type": "mouse_button", "button": 0, "pressed": true, "x": 0, "y": 0}, {"type": "key", "action": "no_such", "pressed": true},
			{"type": "key", "keycode": "NotAKey", "pressed": true}, {"type": "wave"}, 3]:
		check(HarnessScript.make_event(bad, to_window) is String, "refused: %s" % JSON.stringify(bad))


func test_release_builds_stay_inert() -> void:
	var harness: Node = HarnessScript.new()
	tree.root.add_child(harness)
	harness.begin(false, "Windows", PackedStringArray(["--mute", "--harness-port=0"]), PackedStringArray())
	await tree.process_frame
	await tree.process_frame
	check_eq(harness.channel_port(), -1, "no channel")
	check(not AudioServer.is_bus_mute(0), "not muted")
	check(not harness.is_processing(), "no per-frame work")
	harness.free()


func test_channel_is_windows_only_and_mute_mutes() -> void:
	var harness: Node = HarnessScript.new()
	tree.root.add_child(harness)
	harness.begin(true, "iOS", PackedStringArray(["--mute", "--harness-port=0"]), PackedStringArray())
	await tree.process_frame
	await tree.process_frame
	check_eq(harness.channel_port(), -1, "no channel on iOS")
	check(AudioServer.is_bus_mute(0), "--mute mutes the master bus")
	AudioServer.set_bus_mute(0, false)
	harness.begin(true, "Windows", PackedStringArray(["--harness-port=0"]), PackedStringArray())
	await tree.process_frame
	await tree.process_frame
	check(harness.channel_port() > 0, "a free port on Windows: %d" % harness.channel_port())
	harness.free()


# The channel, against a scene that stands in for main.gd's hooks.

class FakeScene extends Node3D:
	var loaded: Array = []
	var cue_ball := -1
	var strikes: Array[Dictionary] = []
	var busy_frames := 0

	func harness_load_position(balls: Array, cue: int) -> bool:
		if balls.size() > 20:
			return false
		loaded = balls
		cue_ball = cue
		return true

	func harness_play_shot(strike: Dictionary) -> String:
		strikes.append(strike)
		busy_frames = 3
		return "fake-%d" % strikes.size()

	func harness_busy() -> bool:
		if busy_frames > 0:
			busy_frames -= 1
			return true
		return false

	func harness_state() -> Dictionary:
		return {"screen": "fake", "shots": strikes.size()}


class Recorder extends Control:
	var events: Array[InputEvent] = []

	func _gui_input(event: InputEvent) -> void:
		events.append(event)


class Client:
	var peer := StreamPeerTCP.new()
	var buffer := PackedByteArray()
	var tree: SceneTree

	func open(scene_tree: SceneTree, port: int) -> bool:
		tree = scene_tree
		peer.connect_to_host("127.0.0.1", port)
		for i in 300:
			peer.poll()
			if peer.get_status() == StreamPeerTCP.STATUS_CONNECTED:
				return true
			await tree.process_frame
		return false

	func write(line: String) -> void:
		peer.put_data((line + "\n").to_utf8_buffer())

	func read() -> Dictionary:
		var deadline := Time.get_ticks_msec() + 10000
		while Time.get_ticks_msec() < deadline:
			peer.poll()
			var available := peer.get_available_bytes()
			if available > 0:
				buffer.append_array(peer.get_data(available)[1])
			var newline := buffer.find(10)
			if newline >= 0:
				var line := buffer.slice(0, newline).get_string_from_utf8()
				buffer = buffer.slice(newline + 1)
				var parsed: Variant = JSON.parse_string(line)
				return parsed if parsed is Dictionary else {"unparsed": line}
			await tree.process_frame
		return {"timeout": true}

	func ask(id: Variant, cmd: String, args: Dictionary = {}) -> Dictionary:
		write(JSON.stringify({"id": id, "cmd": cmd, "args": args}))
		return await read()


var _quits := [0]
var _log_start := 0


func _channel(scene: Node) -> Node:
	var harness: Node = HarnessScript.new()
	harness.scene_provider = func() -> Node: return scene
	harness.quitter = func() -> void: _quits[0] += 1
	tree.root.add_child(harness)
	check(harness.start_channel(0), "listening")
	return harness


func _fake_scene() -> FakeScene:
	var scene := FakeScene.new()
	scene.name = "Fake"
	var hud := CanvasLayer.new()
	hud.name = "Hud"
	scene.add_child(hud)
	var overlay: DebugOverlay = preload("res://scripts/ui/debug_overlay.gd").new()
	overlay.name = "Overlay"
	hud.add_child(overlay)
	tree.root.add_child(scene)
	return scene


func _client(port: int) -> Client:
	var client := Client.new()
	check(await client.open(tree, port), "connected to 127.0.0.1:%d" % port)
	return client


func test_channel_ping_and_logs() -> void:
	_mark_log()
	var scene := _fake_scene()
	var harness := _channel(scene)
	var port: int = harness.channel_port()
	var client := await _client(port)
	check_eq(await client.ask(1, "ping"), {"id": 1.0, "ok": true, "result": "pong"}, "ping")
	check_eq(await client.ask("two", "ping"), {"id": "two", "ok": true, "result": "pong"}, "a string id")
	harness.free()
	var states: Array[String] = []
	for data: Dictionary in _logged("harness.channel"):
		check_eq(data["port"], float(port), "the channel's port")
		states.append(data["state"])
	check_eq(states, ["listening", "client", "closed"] as Array[String], "listening, client, closed")
	var commands := _logged("harness.command")
	check(commands.size() >= 2 and commands[0]["command"] == "ping" and commands[0]["ok"], "ping logged: %s" % [commands])
	scene.free()


func test_channel_load_position() -> void:
	var scene := _fake_scene()
	var harness := _channel(scene)
	var client := await _client(harness.channel_port())
	var r := await client.ask(1, "load_position", {"name": "straight_1m"})
	check(r.get("ok", false), "by name: %s" % r)
	check_eq(scene.loaded, [{"ball": "cue", "index": 0, "x": -0.5, "y": 0.0}, {"ball": "red", "index": 0, "x": 0.5, "y": 0.0}],
			"the scene got the game ball list")
	check_eq(scene.cue_ball, 0, "cue ball")
	r = await client.ask(2, "load_position", {"balls": [{"ball": "red", "index": 4, "x": 0.1, "y": 0.2},
			{"ball": "cue", "x": -1.0, "y": 0.0}], "cue_ball": 1})
	check(r.get("ok", false), "explicit balls: %s" % r)
	check_eq(scene.cue_ball, 1, "explicit cue ball")
	check_eq(scene.loaded[0]["index"], 4, "an integer index")
	var many := []
	for i in 21:
		many.append({"ball": "red", "index": i % 15, "x": 0.0, "y": 0.0})
	r = await client.ask(3, "load_position", {"balls": many, "cue_ball": 0})
	check(not r.get("ok", true) and str(r.get("error")).contains("refused"), "the scene's refusal: %s" % r)
	r = await client.ask(4, "load_position", {"name": "no_such_position"})
	check(not r.get("ok", true) and str(r.get("error")).contains("no position"), "unknown name: %s" % r)
	r = await client.ask(5, "load_position", {"balls": [{"ball": "cue", "x": 0, "y": 0}], "cue_ball": 3})
	check(not r.get("ok", true), "cue ball out of range: %s" % r)
	harness.free()
	scene.free()


func test_channel_play_shot_and_wait_idle() -> void:
	var scene := _fake_scene()
	var harness := _channel(scene)
	var client := await _client(harness.channel_port())
	var strike := {"aim_rad": 0.25, "tip_x": 0.1, "tip_y": -0.2, "elevation_rad": 0.05, "speed_m_s": 2}
	var r := await client.ask(1, "play_shot", strike)
	check_eq(r, {"id": 1.0, "ok": true, "result": {"shot_id": "fake-1"}}, "shot id")
	check_eq(scene.strikes[0], {"aim_rad": 0.25, "tip_x": 0.1, "tip_y": -0.2, "elevation_rad": 0.05, "speed_m_s": 2.0}, "exact strike")
	r = await client.ask(2, "wait_idle", {"timeout_s": 5})
	check(r.get("ok", false), "idle again: %s" % r)
	check_eq(scene.busy_frames, 0, "waited the shot out")
	scene.busy_frames = 100000
	r = await client.ask(3, "wait_idle", {"timeout_s": 0.05})
	check(not r.get("ok", true) and str(r.get("error")).contains("still busy"), "timeout: %s" % r)
	strike.erase("speed_m_s")
	r = await client.ask(4, "play_shot", strike)
	check(not r.get("ok", true) and str(r.get("error")).contains("speed_m_s"), "missing field named: %s" % r)
	harness.free()
	scene.free()


func test_channel_set_setting_and_dump_state() -> void:
	var scene := _fake_scene()
	var harness := _channel(scene)
	var client := await _client(harness.channel_port())
	var saved: Variant = Settings.get_value("guide_level")
	var r := await client.ask(1, "set_setting", {"key": "guide_level", "value": 2})
	check(r.get("ok", false), "set: %s" % r)
	check(Settings.get_value("guide_level") is int and Settings.get_value("guide_level") == 2, "an integer setting stays an integer")
	Settings.set_value("guide_level", saved)
	r = await client.ask(2, "set_setting", {"key": "no_such_setting", "value": 1})
	check(not r.get("ok", true), "unknown key refused: %s" % r)
	r = await client.ask(3, "dump_state")
	var state: Dictionary = r.get("result", {})
	check_eq(state.get("screen"), "fake", "the scene's own state wins")
	check_eq(state.get("shots"), 0.0, "the scene's fields")
	check_eq(state.get("session"), Log.session(), "session")
	check_eq(state.get("params_hash"), Params.current_hash(), "params hash")
	check(state.has("params_pending_hash") and state.has("perf"), "pending hash and perf")
	r = await client.ask(4, "dump_state", {"path": "harness-test/state.json"})
	check(r.get("ok", false), "written: %s" % r.get("error", ""))
	var written: Variant = JSON.parse_string(FileAccess.get_file_as_string("user://harness-test/state.json"))
	check(written is Dictionary and (written as Dictionary).get("screen") == "fake", "the file holds the state")
	DirAccess.remove_absolute("user://harness-test/state.json")
	DirAccess.remove_absolute("user://harness-test")
	harness.free()
	scene.free()


func test_channel_overlay_and_screenshot() -> void:
	var scene := _fake_scene()
	var harness := _channel(scene)
	var client := await _client(harness.channel_port())
	var r := await client.ask(1, "overlay", {"visible": true})
	check_eq(r.get("result"), {"visible": true}, "shown: %s" % r)
	check((scene.get_node("Hud/Overlay") as Control).visible, "the overlay is visible")
	r = await client.ask(2, "overlay", {"visible": "yes"})
	check(not r.get("ok", true), "visible must be a bool")
	if DisplayServer.get_name() == "headless":
		r = await client.ask(3, "screenshot", {"path": "x.png"})
		check(not r.get("ok", true) and str(r.get("error")).contains("headless"), "headless can't capture: %s" % r)
	r = await client.ask(4, "screenshot", {})
	check(not r.get("ok", true), "needs a path")
	harness.free()
	scene.free()


func test_channel_input_reaches_the_control_under_it() -> void:
	var scene := _fake_scene()
	var harness := _channel(scene)
	var layer := CanvasLayer.new()
	var target := Recorder.new()
	target.position = Vector2(300, 200)
	target.size = Vector2(100, 100)
	layer.add_child(target)
	tree.root.add_child(layer)
	var saved_scale := tree.root.content_scale_factor
	# A canvas that is not the window's pixels, as on the phone.
	tree.root.content_scale_factor = 2.0
	await tree.process_frame
	var client := await _client(harness.channel_port())
	var r := await client.ask(1, "input", {"events": [
		{"type": "touch", "index": 0, "pressed": true, "x": 350, "y": 260},
		{"type": "drag", "index": 0, "x": 360, "y": 262, "rx": 10, "ry": 2},
		{"type": "touch", "index": 0, "pressed": false, "x": 360, "y": 262},
		{"type": "mouse_button", "button": 1, "pressed": true, "x": 50, "y": 50},
		{"type": "mouse_button", "button": 1, "pressed": false, "x": 50, "y": 50},
	], "frames_between": 1})
	check_eq(r.get("result"), {"sent": 5.0}, "sent: %s" % r)
	var touches: Array[InputEventScreenTouch] = []
	for event: InputEvent in target.events:
		if event is InputEventScreenTouch:
			touches.append(event)
	check_eq(touches.size(), 2, "press and release reach the control: %s" % [target.events])
	if not touches.is_empty():
		check(touches[0].pressed, "press first")
		check(touches[0].position.distance_to(Vector2(50, 60)) < 0.01, "at the canvas point, in the control's frame: %s" % touches[0].position)
	for event: InputEvent in target.events:
		if event is InputEventMouseButton:
			check((event as InputEventMouseButton).position.x >= 0.0, "only clicks inside it: %s" % event)
	r = await client.ask(2, "input", {"events": [{"type": "touch", "x": 1, "y": 1}]})
	check(not r.get("ok", true) and str(r.get("error")).begins_with("events[0]"), "a bad event names its index: %s" % r)
	r = await client.ask(3, "input", {"events": []})
	check(not r.get("ok", true), "no events")
	tree.root.content_scale_factor = saved_scale
	harness.free()
	layer.free()
	scene.free()


func test_channel_bad_requests() -> void:
	var scene := _fake_scene()
	var harness := _channel(scene)
	var client := await _client(harness.channel_port())
	client.write("not json")
	var r := await client.read()
	check(r.has("id") and r["id"] == null and not r.get("ok", true), "not JSON: %s" % r)
	client.write("[1, 2]")
	r = await client.read()
	check(not r.get("ok", true) and str(r.get("error")).contains("object"), "not an object: %s" % r)
	client.write("null")
	r = await client.read()
	check(not r.get("ok", true) and str(r.get("error")).contains("object"), "JSON null is not a request: %s" % r)
	r = await client.ask(7, "frobnicate")
	check(r.get("id") == 7.0 and not r.get("ok", true) and str(r.get("error")).contains("unknown command"), "unknown command: %s" % r)
	client.write('{"id": 8, "cmd": "ping", "args": [1]}')
	r = await client.read()
	check(r.get("id") == 8.0 and not r.get("ok", true), "args must be an object: %s" % r)
	client.write('{"id": 9}')
	r = await client.read()
	check(r.get("id") == 9.0 and not r.get("ok", true), "no cmd: %s" % r)
	check_eq(await client.ask(10, "ping"), {"id": 10.0, "ok": true, "result": "pong"}, "still serving after errors")
	harness.free()
	scene.free()


func test_channel_answers_in_order_and_serves_a_second_client() -> void:
	var scene := _fake_scene()
	var harness := _channel(scene)
	var first := await _client(harness.channel_port())
	var second := await _client(harness.channel_port())
	# Sent together: the ping waits for the wait_idle before it, which waits on the scene.
	scene.busy_frames = 10
	first.write(JSON.stringify({"id": 1, "cmd": "wait_idle", "args": {"timeout_s": 5}}) + "\n" + JSON.stringify({"id": 2, "cmd": "ping"}))
	check_eq(await second.ask("b", "ping"), {"id": "b", "ok": true, "result": "pong"}, "the second client")
	var one := await first.read()
	var two := await first.read()
	check_eq([one.get("id"), two.get("id")], [1.0, 2.0], "in order")
	check(one.get("ok", false) and two.get("ok", false), "both ok")
	first.peer.disconnect_from_host()
	for i in 5:
		await tree.process_frame
	var third := await _client(harness.channel_port())
	check_eq(await third.ask(3, "ping"), {"id": 3.0, "ok": true, "result": "pong"}, "a client after one left")
	harness.free()
	scene.free()


func test_channel_quit_answers_then_quits() -> void:
	var scene := _fake_scene()
	var harness := _channel(scene)
	var client := await _client(harness.channel_port())
	check_eq(await client.ask(1, "quit"), {"id": 1.0, "ok": true, "result": "quitting"}, "answered first")
	check_eq(_quits[0], 1, "then quit")
	check_eq(harness.channel_port(), -1, "the channel closed")
	harness.free()
	scene.free()


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


func _logged(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append((parsed as Dictionary).get("data", {}))
	return found
