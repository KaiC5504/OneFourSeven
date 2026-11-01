extends Node
## Harness autoload: debug-build launch arguments for agents and CI (spec 16.5). Inert in
## release builds, where _ready returns before reading any argument.
##
## User arguments (after `--`): --screenshot=<path>, --dump-state=<path>,
## --quit-after=<seconds>, --demo-screen=<name>. iOS launch arguments: -DemoScreen <name>.
## Relative paths land under user://. A flag given twice keeps its last value. Every command
## logs harness.command.

signal captures_done

const SCREENS: Array[String] = ["table"]
const STABLE_SECONDS := 1.0
const CAPTURE_TIMEOUT_SECONDS := 10.0
const FLAGS := {
	"--screenshot": "screenshot",
	"--dump-state": "dump_state",
	"--quit-after": "quit_after",
	"--demo-screen": "demo_screen",
}
# Read by other autoloads (Link reads --no-link), so the harness accepts them silently.
const OTHER_FLAGS: Array[String] = ["--no-link"]

var _args := {}
var _screen := "table"
var _captures_pending := false


static func parse_args(user_args: PackedStringArray, launch_args: PackedStringArray) -> Dictionary:
	var parsed := {
		"screenshot": "",
		"dump_state": "",
		"quit_after": -1.0,
		"demo_screen": "",
		"invalid": PackedStringArray(),
		"repeated": PackedStringArray(),
	}
	var seen := {}
	var i := 0
	while i < user_args.size():
		var arg := user_args[i]
		i += 1
		if arg == "-DemoScreen":
			# A missing value must not swallow the next flag.
			if i < user_args.size() and not user_args[i].begins_with("-"):
				_store(parsed, seen, "--demo-screen", user_args[i])
				i += 1
			else:
				parsed["invalid"].append(arg)
			continue
		if OTHER_FLAGS.has(arg):
			continue
		var eq := arg.find("=")
		var key := arg.substr(0, eq) if eq >= 0 else arg
		var value := arg.substr(eq + 1) if eq >= 0 else ""
		var ok := FLAGS.has(key) and not value.is_empty()
		if ok and key == "--quit-after":
			ok = value.is_valid_float() and is_finite(value.to_float()) and value.to_float() >= 0.0
		if ok:
			_store(parsed, seen, key, value)
		else:
			parsed["invalid"].append(arg)
	var demo := launch_args.find("-DemoScreen")
	if demo >= 0 and demo + 1 < launch_args.size() and not launch_args[demo + 1].begins_with("-"):
		_store(parsed, seen, "--demo-screen", launch_args[demo + 1])
	return parsed


static func _store(parsed: Dictionary, seen: Dictionary, flag: String, value: String) -> void:
	if seen.has(flag) and not (parsed["repeated"] as PackedStringArray).has(flag):
		parsed["repeated"].append(flag)
	seen[flag] = true
	var field: String = FLAGS[flag]
	parsed[field] = value.to_float() if field == "quit_after" else value


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


func _ready() -> void:
	if not OS.is_debug_build():
		return
	process_mode = Node.PROCESS_MODE_ALWAYS
	_args = parse_args(OS.get_cmdline_user_args(), OS.get_cmdline_args())
	_run.call_deferred()


func _run() -> void:
	for arg: String in _args["invalid"]:
		_report("unknown", [arg], false, "unrecognised or malformed argument")
	for flag: String in _args["repeated"]:
		_report("repeated", [flag], false, "given more than once; the last value is used")
	var demo: String = _args["demo_screen"]
	if not demo.is_empty():
		var known := SCREENS.has(demo)
		_report("demo-screen", [demo], known, demo if known else "unknown screen; known: %s" % ", ".join(SCREENS))
		if known:
			_screen = demo
	var quit_after: float = _args["quit_after"]
	if quit_after >= 0.0:
		_quit_after(quit_after)
	var screenshot: String = _args["screenshot"]
	var dump: String = _args["dump_state"]
	if screenshot.is_empty() and dump.is_empty():
		return
	_captures_pending = true
	await get_tree().create_timer(STABLE_SECONDS, true, false, true).timeout
	# The state needs no rendering, so it doesn't wait for a frame a headless run never draws.
	if not dump.is_empty():
		_dump_state(resolve_path(dump))
	if not screenshot.is_empty():
		await _screenshot(resolve_path(screenshot))
	_captures_pending = false
	captures_done.emit()


func _screenshot(path: String) -> void:
	if DisplayServer.get_name() == "headless":
		_report("screenshot", [path], false, "the headless display server renders nothing")
		return
	# In low-processor mode an unchanged scene is never redrawn, so ask for frames instead of
	# waiting for one.
	Perf.set_active(&"harness_capture", true)
	var drawn: bool = await _frame_drawn_within(CAPTURE_TIMEOUT_SECONDS)
	Perf.set_active(&"harness_capture", false)
	if not drawn:
		_report("screenshot", [path], false, "no frame drawn within %d s" % int(CAPTURE_TIMEOUT_SECONDS))
		return
	var texture := get_viewport().get_texture()
	var image := texture.get_image() if texture else null
	if image == null or image.is_empty():
		_report("screenshot", [path], false, "no image from the viewport")
		return
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var err := image.save_png(path)
	_report("screenshot", [path], err == OK, ProjectSettings.globalize_path(path) if err == OK else error_string(err))


func _dump_state(path: String) -> void:
	var state := build_state(get_tree().current_scene, _screen, Log.session(), Link.get_status(), Params.current_hash())
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		_report("dump-state", [path], false, error_string(FileAccess.get_open_error()))
		return
	file.store_string(JSON.stringify(state, "  "))
	file.close()
	_report("dump-state", [path], true, ProjectSettings.globalize_path(path))


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
