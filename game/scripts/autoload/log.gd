extends Node
## Log autoload: GDScript's logging API over OfsLog (spec 16.10).
##
## Every other script logs through these four functions. Event names and their fields
## must be in core/data/log_schema.json.
##
## OfsLog and OfsEngineLogger are reached through ClassDB rather than by name, so this
## script still compiles when the extension fails to load; Log then writes the same JSONL
## itself, starting with an app.error that says so.

const BUILD_INFO_PATH := "res://data/build_info.json"
const LOG_DIR := "user://logs"
## Where test runs log instead, so the next linked run doesn't upload them as real sessions.
const TEST_LOG_DIR := "user://test-logs"
const MAX_FILE_BYTES := 50 * 1024 * 1024
## The user argument that starts the phone link switched off (link.gd reads it too).
const NO_LINK_ARG := "--no-link"
const ENVELOPE := '{"ts":%s,"session":%s,"build":%s,"platform":%s,"subsystem":%s,"level":%s,"event":%s,"data":%s}'

var _engine_logger: Logger
var _quit_logged := false
var _native := true
var _fallback: FileAccess
var _session := ""
var _build := ""


# _enter_tree rather than _ready: Log is the first autoload, so this runs before any other
# autoload can log or raise an engine error.
static func log_dir() -> String:
	return TEST_LOG_DIR if OS.get_cmdline_args().has("res://tests/run_tests.gd") else LOG_DIR


func _enter_tree() -> void:
	var build_info := _read_build_info()
	var build := str(build_info["build_number"])
	if ClassDB.class_exists("OfsLog") and ClassDB.class_exists("OfsEngineLogger"):
		ClassDB.class_call_static("OfsLog", "start", build, platform_name(),
				ProjectSettings.globalize_path(log_dir()), MAX_FILE_BYTES)
		_engine_logger = ClassDB.instantiate("OfsEngineLogger") as Logger
		OS.add_logger(_engine_logger)
	else:
		_start_fallback(log_dir(), build,
				"the onefourseven GDExtension did not load (no OfsLog class); Log is writing JSONL itself")
	info("app", "app.start", _start_data(build_info))
	if build_info.has("missing_reason"):
		warn("app", "app.error", {"kind": "message", "message": build_info["missing_reason"],
				"file": BUILD_INFO_PATH})


func _notification(what: int) -> void:
	match what:
		NOTIFICATION_APPLICATION_PAUSED:
			_lifecycle("background")
		NOTIFICATION_APPLICATION_RESUMED:
			_lifecycle("foreground")
		NOTIFICATION_APPLICATION_FOCUS_OUT:
			_lifecycle("focus_lost")
		NOTIFICATION_APPLICATION_FOCUS_IN:
			_lifecycle("focus_gained")
		NOTIFICATION_WM_CLOSE_REQUEST, NOTIFICATION_PREDELETE:
			# Both arrive on a desktop quit; iOS may only ever send PREDELETE.
			if not _quit_logged:
				_quit_logged = true
				_lifecycle("quit")
			flush()
			if what == NOTIFICATION_PREDELETE and _engine_logger != null:
				OS.remove_logger(_engine_logger)
				_engine_logger = null


func debug(subsystem: String, event: String, data: Dictionary = {}) -> void:
	_emit(subsystem, "debug", event, data)


func info(subsystem: String, event: String, data: Dictionary = {}) -> void:
	_emit(subsystem, "info", event, data)


func warn(subsystem: String, event: String, data: Dictionary = {}) -> void:
	_emit(subsystem, "warn", event, data)


func error(subsystem: String, event: String, data: Dictionary = {}) -> void:
	_emit(subsystem, "error", event, data)


func session() -> String:
	if not _native:
		return _session
	return ClassDB.class_call_static("OfsLog", "session")


func flush() -> void:
	if not _native:
		if _fallback != null:
			_fallback.flush()
		return
	ClassDB.class_call_static("OfsLog", "flush")


## "windows", "ios" or "ios-simulator"; any other desktop reports its own name.
static func platform_name() -> String:
	if OS.has_feature("ios"):
		# Godot has no simulator feature tag; its iOS get_processor_name() returns
		# "Simulator" in simulator builds.
		return "ios-simulator" if OS.get_processor_name() == "Simulator" else "ios"
	if OS.has_feature("windows"):
		return "windows"
	return OS.get_name().to_lower()


func _emit(subsystem: String, level: String, event: String, data: Dictionary) -> void:
	if _native:
		ClassDB.class_call_static("OfsLog", "emit", subsystem, level, event, data)
	elif _fallback != null:
		_fallback.store_line(ENVELOPE % [JSON.stringify(Time.get_unix_time_from_system()), JSON.stringify(_session),
				JSON.stringify(_build), JSON.stringify(platform_name()), JSON.stringify(subsystem),
				JSON.stringify(level), JSON.stringify(event), JSON.stringify(data)])
		_fallback.flush()


## The writer for when the extension is missing; dir is a Godot path such as user://logs.
func _start_fallback(dir: String, build: String, reason: String) -> void:
	_native = false
	_build = build
	var now := Time.get_datetime_dict_from_system(true)
	_session = "%04d%02d%02d-%02d%02d%02d-%06x" % [now["year"], now["month"], now["day"], now["hour"],
			now["minute"], now["second"], randi() & 0xffffff]
	DirAccess.make_dir_recursive_absolute(dir)
	_fallback = FileAccess.open(dir.path_join(_session + ".jsonl"), FileAccess.WRITE)
	if _fallback == null:
		printerr("Log: %s, and %s can't be written (%s)" % [reason, dir, error_string(FileAccess.get_open_error())])
		return
	error("app", "app.error", {"kind": "message", "message": reason})


func _lifecycle(state: String) -> void:
	info("app", "app.lifecycle", {"state": state})


func _read_build_info() -> Dictionary:
	var reason := ""
	if FileAccess.file_exists(BUILD_INFO_PATH):
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(BUILD_INFO_PATH))
		if parsed is Dictionary and (parsed as Dictionary).has_all(["version", "build_number", "git_sha"]):
			return parsed
		reason = "build_info.json is not an object with version, build_number and git_sha"
	else:
		reason = "build_info.json missing; run scripts/sync_data.py"
	return {
		"version": str(ProjectSettings.get_setting("application/config/version", "")),
		"build_number": "dev",
		"git_sha": "unknown",
		"missing_reason": reason,
	}


func _start_data(build_info: Dictionary) -> Dictionary:
	# Godot keeps the engine options it consumed (--headless, --path, ...) out of
	# get_cmdline_args; display_server and renderer below say what they chose.
	var args: Array = Array(OS.get_cmdline_args())
	var user_args := OS.get_cmdline_user_args()
	if not user_args.is_empty():
		args.append("--")
		args.append_array(Array(user_args))
	var screen := DisplayServer.screen_get_size()
	var safe := DisplayServer.get_display_safe_area()
	var core_version := "none"
	if _native:
		core_version = ClassDB.class_call_static("OfsLog", "core_version")
	return {
		"version": str(build_info["version"]),
		"build_number": str(build_info["build_number"]),
		"git_sha": str(build_info["git_sha"]),
		"git_dirty": build_info.get("git_dirty", false) == true,
		"core_version": core_version,
		"godot_version": str(Engine.get_version_info()["string"]),
		"os": OS.get_name(),
		"os_version": OS.get_version(),
		"device_model": OS.get_model_name(),
		"renderer": "%s / %s" % [RenderingServer.get_current_rendering_method(),
				RenderingServer.get_video_adapter_name()],
		"display_server": DisplayServer.get_name(),
		"max_fps": Engine.max_fps,
		"screen_size": [screen.x, screen.y],
		"safe_area": [safe.position.x, safe.position.y, safe.size.x, safe.size.y],
		"refresh_rate": DisplayServer.screen_get_refresh_rate(),
		"locale": OS.get_locale(),
		"debug_build": OS.is_debug_build(),
		"launch_args": args,
		"settings": {"link_enabled": not user_args.has(NO_LINK_ARG)},
	}
