extends TestCase
## The Log autoload and OfsLog: the session file, app.start, engine errors, the upload
## queue and content hashes. Most checks read back what the real FileSink wrote, so they
## need ofs::log (core/src/log.cpp) to be the real implementation.

const SCHEMA_PATHS: PackedStringArray = ["res://data/log_schema.json", "res://../core/data/log_schema.json"]
const ENVELOPE_KEYS: PackedStringArray = ["ts", "session", "build", "platform", "subsystem", "level", "event", "data"]


func test_emit_writes_one_canonical_line() -> void:
	var marker := _marker()
	Log.info("harness", "harness.command", {"command": marker, "args": [1, 2.5, true, "s", null], "ok": true})
	var line := _find_line(marker)
	check(not line.is_empty(), "no line in %s contains %s" % [OfsLog.file_path(), marker])
	if line.is_empty():
		return
	var at := -1
	for key in ENVELOPE_KEYS:
		var found := line.find("\"%s\":" % key)
		check(found > at, "key %s out of order in %s" % [key, line])
		at = found
	check(line.begins_with("{\"ts\":"), "line starts with ts: " + line)
	check(line.contains("\"args\":[1,2.5,true,\"s\",null]"), "int stays integer, float stays number: " + line)
	var parsed: Dictionary = JSON.parse_string(line)
	check(parsed["ts"] is float and float(parsed["ts"]) > 1.7e9, "ts is epoch seconds")
	check_eq(parsed["session"], Log.session(), "session")
	check_eq(parsed["platform"], Log.platform_name(), "platform")
	check_eq(parsed["subsystem"], "harness", "subsystem")
	check_eq(parsed["level"], "info", "level")
	check_eq(parsed["event"], "harness.command", "event")


func test_levels_map_to_their_names() -> void:
	var marker := _marker()
	Log.debug("harness", "harness.command", {"command": marker + "-debug"})
	Log.warn("harness", "harness.command", {"command": marker + "-warn"})
	Log.error("harness", "harness.command", {"command": marker + "-error"})
	for level in ["debug", "warn", "error"]:
		check(_find_line("%s-%s\"" % [marker, level]).contains("\"level\":\"%s\"" % level), "level " + level)


func test_unknown_level_is_kept_as_error() -> void:
	var marker := _marker()
	OfsLog.emit("harness", "loud", "harness.command", {"command": marker})
	var line := _find_line(marker)
	check(line.contains("\"level\":\"error\""), "unknown level becomes error: " + line)
	check(line.contains("\"invalid_level\":\"loud\""), "the asked-for level is kept: " + line)


func test_variants_convert_to_json() -> void:
	var marker := _marker()
	Log.info("harness", "harness.command", {
		"command": marker,
		"v2": Vector2(1, 2.5),
		"v2i": Vector2i(3, 4),
		"v3": Vector3(1, 2, 3),
		"rect": Rect2(1, 2, 3, 4),
		"recti": Rect2i(5, 6, 7, 8),
		"ints": PackedInt32Array([1, 2]),
		"floats": PackedFloat64Array([0.5]),
		"strings": PackedStringArray(["a", "b"]),
		"nested": {"inner": [{"deep": 1}]},
		7: "int key",
		"colour": Color(1, 0, 0),
	})
	var line := _find_line(marker)
	for expected in ["\"v2\":[1.0,2.5]", "\"v2i\":[3,4]", "\"v3\":[1.0,2.0,3.0]", "\"rect\":[1.0,2.0,3.0,4.0]",
			"\"recti\":[5,6,7,8]", "\"ints\":[1,2]", "\"floats\":[0.5]", "\"strings\":[\"a\",\"b\"]",
			"\"nested\":{\"inner\":[{\"deep\":1}]}", "\"7\":\"int key\"", "\"colour\":\"(1.0, 0.0, 0.0, 1.0)\""]:
		check(line.contains(expected), "%s in %s" % [expected, line])


func test_app_start_has_every_schema_field() -> void:
	var start := {}
	for line in _lines():
		if line.contains("\"event\":\"app.start\""):
			start = JSON.parse_string(line)
			break
	check(not start.is_empty(), "app.start in the session file")
	var schema := _schema()
	check(not schema.is_empty(), "log schema readable from %s" % [SCHEMA_PATHS])
	if start.is_empty() or schema.is_empty():
		return
	check_eq(start["subsystem"], "app", "app.start subsystem")
	check_eq(start["level"], "info", "app.start level")
	var data: Dictionary = start["data"]
	var fields: Dictionary = schema["events"]["app.start"]["fields"]
	for field: String in fields:
		check(data.has(field), "app.start has " + field)
		if data.has(field):
			check(_has_type(data[field], fields[field]["type"]), "app.start %s is %s: %s" % [field, fields[field]["type"], data[field]])
	if data.has("screen_size"):
		check_eq((data["screen_size"] as Array).size(), 2, "screen_size is [w, h]")
	if data.has("safe_area"):
		check_eq((data["safe_area"] as Array).size(), 4, "safe_area is [x, y, w, h]")
	if data.has("version"):
		check_eq(data["version"], ProjectSettings.get_setting("application/config/version"), "version")


func test_engine_error_arrives_as_app_error() -> void:
	var marker := _marker()
	push_error(marker)
	var line := _find_line(marker)
	check(not line.is_empty(), "push_error reached the log")
	if line.is_empty():
		return
	var parsed: Dictionary = JSON.parse_string(line)
	var data: Dictionary = parsed["data"]
	check_eq(parsed["event"], "app.error", "event")
	check_eq(parsed["level"], "error", "level")
	check_eq(data["message"], marker, "message")
	check(["error", "script"].has(data["kind"]), "kind: %s" % data["kind"])
	check(not str(data["file"]).is_empty(), "file is set")
	check(float(data["line"]) > 0, "line is set")
	check(line.contains("test_log.gd:"), "stack names this script: " + line)


func test_engine_error_from_a_worker_thread() -> void:
	var marker := _marker()
	var thread := Thread.new()
	thread.start(push_error.bind(marker))
	thread.wait_to_finish()
	check(_find_line(marker).contains("\"event\":\"app.error\""), "an error raised off the main thread is logged")


func test_engine_warning_is_warn() -> void:
	var marker := _marker()
	push_warning(marker)
	var line := _find_line(marker)
	check(line.contains("\"level\":\"warn\""), "warn level: " + line)
	check(line.contains("\"kind\":\"warning\""), "warning kind: " + line)


func test_printerr_is_logged_and_print_is_not() -> void:
	var marker := _marker()
	printerr(marker + "-err")
	print(marker + "-out")
	var line := _find_line(marker + "-err")
	check(line.contains("\"kind\":\"message\""), "printerr logged as kind message: " + line)
	check(line.contains("\"message\":\"%s-err\"" % marker), "trailing newline stripped: " + line)
	check(line.contains("\"event\":\"app.error\""), "printerr is app.error: " + line)
	check(_find_line(marker + "-out").is_empty(), "plain print is not logged")


func test_lifecycle_notifications() -> void:
	var before := _lines().size()
	Log.notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	Log.notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	var tail := _lines().slice(before)
	var states: Array[String] = []
	for line in tail:
		if line.contains("\"event\":\"app.lifecycle\""):
			states.append(str((JSON.parse_string(line) as Dictionary)["data"]["state"]))
	check_eq(states, ["focus_lost", "focus_gained"] as Array[String], "lifecycle states")


func test_batch_take_and_restore_keep_order() -> void:
	var marker := _marker()
	for i in 3:
		Log.info("harness", "harness.command", {"command": "%s-%d" % [marker, i]})
	var queued := OfsLog.queued_lines()
	check(queued >= 3, "queued_lines counts the new lines: %d" % queued)
	var batch := OfsLog.take_batch(queued)
	check_eq(batch.size(), queued, "take_batch returns what was queued")
	check_eq(OfsLog.queued_lines(), 0, "the queue is empty after taking everything")
	var order: Array[int] = []
	for line in batch:
		for i in 3:
			if line.contains("\"%s-%d\"" % [marker, i]):
				order.append(i)
	check_eq(order, [0, 1, 2] as Array[int], "lines come out oldest first")
	OfsLog.restore_batch(batch)
	check_eq(OfsLog.queued_lines(), queued, "restore puts them back")
	var again := OfsLog.take_batch(queued)
	check_eq(again, batch, "restored lines come out again in the same order")
	OfsLog.restore_batch(again)
	check(OfsLog.dropped_total() >= 0, "dropped_total is a count")


func test_content_hash_invalid_json_is_empty() -> void:
	check_eq(OfsLog.content_hash("{"), "", "truncated object")
	check_eq(OfsLog.content_hash("not json"), "", "bare word")
	check_eq(OfsLog.content_hash(""), "", "empty text")


# The vectors from docs/devlink-protocol.md; they pass once core/src/params.cpp is real.
func test_content_hash_vectors_need_real_params() -> void:
	check_eq(OfsLog.content_hash("{\"b\":1,\"a\":[true,null,\"x\"]}"), "8efc364b3052045e", "mixed object")
	check_eq(OfsLog.content_hash("{\"values\":{},\"schema_version\":1}"), "b99af9e0a04c6e42", "params shape")
	check_eq(OfsLog.content_hash("{ \"values\" : {}, \"schema_version\" : 1 }"), "b99af9e0a04c6e42",
			"whitespace does not change the hash")


func test_core_version() -> void:
	check(not OfsLog.core_version().is_empty(), "core_version is set")


func test_platform_name() -> void:
	if OS.has_feature("windows"):
		check_eq(Log.platform_name(), "windows", "platform on Windows")
	check(not OS.has_feature("simulator"), "Godot 4.7 still has no simulator feature tag")


func _marker() -> String:
	return "test-log-%d-%d" % [Time.get_ticks_usec(), randi()]


func _lines() -> PackedStringArray:
	OfsLog.flush()
	var path := OfsLog.file_path()
	if path.is_empty() or not FileAccess.file_exists(path):
		return PackedStringArray()
	return FileAccess.get_file_as_string(path).split("\n", false)


func _find_line(marker: String) -> String:
	for line in _lines():
		if line.contains(marker):
			return line
	return ""


func _schema() -> Dictionary:
	for path in SCHEMA_PATHS:
		var absolute := ProjectSettings.globalize_path(path).simplify_path()
		if FileAccess.file_exists(absolute):
			var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(absolute))
			if parsed is Dictionary:
				return parsed
	return {}


func _has_type(value: Variant, type: String) -> bool:
	match type:
		"string":
			return value is String
		"number":
			return value is float or value is int
		# Godot's JSON parser reads every number as a float.
		"integer":
			return (value is float or value is int) and float(value) == floorf(float(value))
		"boolean":
			return value is bool
		"object":
			return value is Dictionary
		"array":
			return value is Array
	return true
