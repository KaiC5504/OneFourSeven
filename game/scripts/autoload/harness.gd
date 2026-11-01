extends Node
## Harness autoload: debug-build launch arguments for agents and CI (spec 16.5). Compiled
## out of release builds by checking OS.is_debug_build(). Agent E owns this file.
##
## User arguments (after `--`): --screenshot=<path>, --dump-state=<path>,
## --quit-after=<seconds>, --demo-screen=<name>. iOS launch arguments: -DemoScreen <name>.
## Relative paths land under user://. Every command logs harness.command.

signal captures_done

const SCREENS: Array[String] = ["table"]
const STABLE_SECONDS := 1.0

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
	}
	var i := 0
	while i < user_args.size():
		var arg := user_args[i]
		i += 1
		if arg == "-DemoScreen" and i < user_args.size():
			parsed["demo_screen"] = user_args[i]
			i += 1
			continue
		var eq := arg.find("=")
		var key := arg.substr(0, eq) if eq >= 0 else arg
		var value := arg.substr(eq + 1) if eq >= 0 else ""
		var ok := not value.is_empty()
		match key:
			"--screenshot":
				parsed["screenshot"] = value
			"--dump-state":
				parsed["dump_state"] = value
			"--demo-screen":
				parsed["demo_screen"] = value
			"--quit-after":
				ok = value.is_valid_float() and value.to_float() >= 0.0
				if ok:
					parsed["quit_after"] = value.to_float()
			_:
				ok = false
		if not ok:
			parsed["invalid"].append(arg)
	var demo := launch_args.find("-DemoScreen")
	if demo >= 0 and demo + 1 < launch_args.size():
		parsed["demo_screen"] = launch_args[demo + 1]
	return parsed


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
	await RenderingServer.frame_post_draw
	if not dump.is_empty():
		_dump_state(resolve_path(dump))
	if not screenshot.is_empty():
		_screenshot(resolve_path(screenshot))
	_captures_pending = false
	captures_done.emit()


func _screenshot(path: String) -> void:
	var texture := get_viewport().get_texture()
	var image := texture.get_image() if texture else null
	if image == null or image.is_empty():
		_report("screenshot", [path], false, "no image from the viewport (headless or no renderer)")
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
