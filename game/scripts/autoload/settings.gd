extends Node
## Settings autoload (spec 15): every player setting, saved to user://settings.json.

signal changed(key: String, value: Variant)

const PATH := "user://settings.json"
const VERSION := 1
## "stroke" is Push, "pull" is Pull and release.
const STRIKE_MODES: Array[String] = ["stroke", "pull"]
## Values earlier builds saved, read as their replacement: the power bar gave way to the pull.
const MIGRATED := {"strike_mode": {"power_bar": "pull"}}
const DEFAULTS := {
	"strike_mode": "stroke",
	"guide_level": 1,
	"aim_sensitivity": 1.0,
	"fine_tune_sensitivity": 1.0,
	"stroke_wander": false,
	"reset_spin_after_shot": true,
	"volume": 0.8,
	"haptics": true,
	# The shot cycle (spec 8.5): the camera at the pocket for a pot, and the aim turned to the next
	# ball after each shot.
	"pocket_camera": true,
	"aim_next_ball": true,
}
## The values a string setting may take; every other key is checked by its default's type.
const CHOICES := {"strike_mode": STRIKE_MODES}
## Inclusive [min, max] for the numeric settings.
const RANGES := {
	"guide_level": [0, 3],
	"aim_sensitivity": [0.25, 4.0],
	"fine_tune_sensitivity": [0.25, 4.0],
	"volume": [0.0, 1.0],
}

## The file read at start and written on every change; tests point it at a scratch file and
## call load_settings.
var path := PATH

var _values := DEFAULTS.duplicate()


func _ready() -> void:
	load_settings()


func get_value(key: String) -> Variant:
	return _values.get(key, DEFAULTS.get(key))


## Refuses an unknown key or a value of the wrong type or range (logged); otherwise stores it,
## saves, logs settings.changed and emits changed. Setting the value already held does nothing.
func set_value(key: String, value: Variant) -> bool:
	var old: Variant = _values.get(key)
	var reason := refusal(key, value)
	if not reason.is_empty():
		Log.warn("settings", "settings.changed", {"key": key, "old": _loggable(old), "new": _loggable(value),
				"ok": false, "reason": reason})
		return false
	var stored: Variant = normalised(key, value)
	if typeof(stored) == typeof(old) and stored == old:
		return true
	_values[key] = stored
	_save()
	Log.info("settings", "settings.changed", {"key": key, "old": old, "new": stored, "ok": true})
	changed.emit(key, stored)
	return true


func all() -> Dictionary:
	return _values.duplicate()


## Replaces every value with what `path` holds. Unknown keys and refused values fall back to
## their defaults and are reported; a file that can't be read or parsed is kept as <path>.bad
## and replaced by the defaults.
func load_settings() -> void:
	_values = DEFAULTS.duplicate()
	if not FileAccess.file_exists(path):
		_log_loaded(VERSION, [])
		return
	var text := FileAccess.get_file_as_string(path)
	var open_error := FileAccess.get_open_error()
	if open_error != OK:
		_set_aside("unreadable: %s" % error_string(open_error))
		return
	var json := JSON.new()
	if json.parse(text) != OK:
		_set_aside("not JSON (line %d: %s)" % [json.get_error_line(), json.get_error_message()])
		return
	var root: Variant = json.data
	if not root is Dictionary or not (root as Dictionary).get("values") is Dictionary:
		_set_aside("not an object with a \"values\" object")
		return
	var problems: Array[String] = []
	var version := VERSION
	var file_version: Variant = (root as Dictionary).get("version")
	if _is_whole_number(file_version):
		version = int(file_version)
		if version != VERSION:
			problems.append("version %d is not %d; values read as version %d" % [version, VERSION, VERSION])
	else:
		problems.append("version missing or not a whole number; read as version %d" % VERSION)
	var values: Dictionary = (root as Dictionary)["values"]
	var migrated := false
	for raw_key: Variant in values:
		var key := str(raw_key)
		if not DEFAULTS.has(key):
			problems.append("unknown key %s dropped" % key)
			continue
		var value: Variant = values[raw_key]
		var renames: Dictionary = MIGRATED.get(key, {})
		if (value is String or value is StringName) and renames.has(String(value)):
			problems.append("%s %s migrated to %s" % [key, JSON.stringify(String(value)), JSON.stringify(renames[String(value)])])
			value = renames[String(value)]
			migrated = true
		var reason := refusal(key, value)
		if not reason.is_empty():
			problems.append("%s: %s; using the default %s" % [key, reason, JSON.stringify(DEFAULTS[key])])
			continue
		_values[key] = normalised(key, value)
	_log_loaded(version, problems)
	# Saved at once, so the migration happens, and is logged, only once.
	if migrated:
		_save()


## Why `value` can't be stored under `key`, or "" when it can.
static func refusal(key: String, value: Variant) -> String:
	if not DEFAULTS.has(key):
		return "unknown key"
	var default: Variant = DEFAULTS[key]
	match typeof(default):
		TYPE_BOOL:
			if not value is bool:
				return "expected true or false, got %s" % type_string(typeof(value))
		TYPE_STRING:
			var choices: Array[String] = CHOICES[key]
			if not (value is String or value is StringName) or not choices.has(String(value)):
				return "expected one of %s" % ", ".join(choices)
		TYPE_INT:
			# JSON has no integers: Godot reads 2 as 2.0.
			if not _is_whole_number(value):
				return "expected a whole number, got %s" % _describe(value)
		TYPE_FLOAT:
			if not (value is int or value is float) or not is_finite(float(value)):
				return "expected a number, got %s" % _describe(value)
	if RANGES.has(key):
		var bounds: Array = RANGES[key]
		if float(value) < float(bounds[0]) or float(value) > float(bounds[1]):
			return "%s is outside %s to %s" % [_describe(value), str(bounds[0]), str(bounds[1])]
	return ""


## `value` in the type DEFAULTS[key] has; only for values refusal() accepts.
static func normalised(key: String, value: Variant) -> Variant:
	match typeof(DEFAULTS[key]):
		TYPE_INT:
			return int(value)
		TYPE_FLOAT:
			return float(value)
		TYPE_STRING:
			return String(value)
	return value


static func _is_whole_number(value: Variant) -> bool:
	if value is int:
		return true
	return value is float and is_finite(float(value)) and float(value) == floorf(float(value))


static func _describe(value: Variant) -> String:
	if value is String or value is StringName:
		return "\"%s\"" % value
	return "%s %s" % [type_string(typeof(value)), str(value)]


## Something the log can always serialise: a refused value may be NaN, an object or anything.
static func _loggable(value: Variant) -> Variant:
	if value == null or value is bool or value is int or value is String:
		return value
	if value is float and is_finite(float(value)):
		return value
	return str(value)


func _set_aside(reason: String) -> void:
	var bad := path + ".bad"
	var err := DirAccess.copy_absolute(path, bad)
	var problems: Array[String] = [reason]
	problems.append("kept as %s" % bad if err == OK else "couldn't keep a copy as %s: %s" % [bad, error_string(err)])
	_log_loaded(VERSION, problems)
	# Without the copy, overwriting would lose the only evidence of what went wrong.
	if err == OK:
		_save()


## settings.loaded with every value in effect, at warn when anything was refused.
func _log_loaded(version: int, problems: Array[String]) -> void:
	var data := {"path": path, "version": version, "problems": problems, "values": _values.duplicate()}
	if problems.is_empty():
		Log.info("settings", "settings.loaded", data)
	else:
		Log.warn("settings", "settings.loaded", data)


## Writes a temporary file and renames it over the old one, so a crash mid-write never leaves a
## half-written settings file.
func _save() -> bool:
	var temp := path + ".tmp"
	var file := FileAccess.open(temp, FileAccess.WRITE)
	if file == null:
		_save_failed("can't open %s for writing: %s" % [temp, error_string(FileAccess.get_open_error())])
		return false
	var written := file.store_string(JSON.stringify({"version": VERSION, "values": _values}, "\t"))
	file.close()
	if not written:
		_save_failed("writing %s failed: %s" % [temp, error_string(file.get_error())])
		DirAccess.remove_absolute(temp)
		return false
	var err := DirAccess.rename_absolute(temp, path)
	if err != OK:
		_save_failed("renaming %s over %s failed: %s" % [temp, path, error_string(err)])
		DirAccess.remove_absolute(temp)
		return false
	return true


func _save_failed(message: String) -> void:
	Log.error("app", "app.error", {"kind": "message", "message": "Settings not saved: " + message, "file": path})
