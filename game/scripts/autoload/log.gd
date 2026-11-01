extends Node
## Log autoload: GDScript's logging API over OfsLog (spec 16.10). Agent C owns this file.
##
## Every other script logs through these four functions. Event names and their fields
## must be in core/data/log_schema.json.

const BUILD_INFO_PATH := "res://data/build_info.json"
const LOG_DIR := "user://logs"
const MAX_FILE_BYTES := 50 * 1024 * 1024
const MAX_RING_BYTES := 4 * 1024 * 1024

var _engine_logger: OfsEngineLogger
var _quit_logged := false


# _enter_tree rather than _ready: Log is the first autoload, so this runs before any other
# autoload can log or raise an engine error.
func _enter_tree() -> void:
	var build_info := _read_build_info()
	OfsLog.start(str(build_info["build_number"]), platform_name(), ProjectSettings.globalize_path(LOG_DIR),
			MAX_FILE_BYTES, MAX_RING_BYTES)
	_engine_logger = OfsEngineLogger.new()
	OS.add_logger(_engine_logger)
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
			OfsLog.flush()
			if what == NOTIFICATION_PREDELETE and _engine_logger != null:
				OS.remove_logger(_engine_logger)
				_engine_logger = null


func debug(subsystem: String, event: String, data: Dictionary = {}) -> void:
	OfsLog.emit(subsystem, "debug", event, data)


func info(subsystem: String, event: String, data: Dictionary = {}) -> void:
	OfsLog.emit(subsystem, "info", event, data)


func warn(subsystem: String, event: String, data: Dictionary = {}) -> void:
	OfsLog.emit(subsystem, "warn", event, data)


func error(subsystem: String, event: String, data: Dictionary = {}) -> void:
	OfsLog.emit(subsystem, "error", event, data)


func session() -> String:
	return OfsLog.session()


## "windows", "ios" or "ios-simulator"; any other desktop reports its own name.
static func platform_name() -> String:
	if OS.has_feature("ios"):
		# Godot has no simulator feature tag; its iOS get_processor_name() returns
		# "Simulator" in simulator builds.
		return "ios-simulator" if OS.get_processor_name() == "Simulator" else "ios"
	if OS.has_feature("windows"):
		return "windows"
	return OS.get_name().to_lower()


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
	var args: Array = Array(OS.get_cmdline_args()) + Array(OS.get_cmdline_user_args())
	return {
		"version": str(build_info["version"]),
		"build_number": str(build_info["build_number"]),
		"git_sha": str(build_info["git_sha"]),
		"core_version": OfsLog.core_version(),
		"godot_version": str(Engine.get_version_info()["string"]),
		"os": OS.get_name(),
		"os_version": OS.get_version(),
		"device_model": OS.get_model_name(),
		"renderer": "%s / %s" % [RenderingServer.get_current_rendering_method(),
				RenderingServer.get_video_adapter_name()],
		"screen_size": DisplayServer.screen_get_size(),
		"safe_area": DisplayServer.get_display_safe_area(),
		"refresh_rate": DisplayServer.screen_get_refresh_rate(),
		"locale": OS.get_locale(),
		"debug_build": OS.is_debug_build(),
		"launch_args": args,
		"settings": {},
	}
