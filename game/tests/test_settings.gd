extends TestCase
## The Settings autoload's store: defaults, validation, saving and loading, a corrupt file. Each
## test uses its own instance on a scratch path, never the player's settings.

const SettingsStore := preload("res://scripts/autoload/settings.gd")

var _log_start := 0


func test_defaults_without_a_file() -> void:
	var store := _store()
	_mark_log()
	store.load_settings()
	check_eq(store.all(), SettingsStore.DEFAULTS, "every default")
	check_eq(store.get_value("strike_mode"), "stroke", "finger stroke by default")
	check_eq(store.get_value("reset_spin_after_shot"), true, "spin reset on by default")
	check_eq(store.get_value("pocket_camera"), true, "the pocket camera on by default")
	check_eq(store.get_value("aim_next_ball"), true, "aiming at the next ball on by default")
	var loaded := _logged("settings.loaded")
	check_eq(loaded.size(), 1, "settings.loaded once")
	if not loaded.is_empty():
		check_eq(loaded[0]["level"], "info", "level")
		check_eq(loaded[0]["data"]["problems"], [], "no problems")
		check_eq(loaded[0]["data"]["path"], store.path, "path")
		_check_values(loaded[0]["data"], SettingsStore.DEFAULTS, "every default logged")
	_cleanup(store)


func test_set_value_validates_each_key() -> void:
	var store := _store()
	var good := {
		"strike_mode": "pull", "guide_level": 3, "aim_sensitivity": 0.25,
		"fine_tune_sensitivity": 4.0, "stroke_wander": true, "reset_spin_after_shot": false,
		"volume": 0.0, "haptics": false, "pocket_camera": false, "aim_next_ball": false,
	}
	for key: String in good:
		check(store.set_value(key, good[key]), "%s accepts %s" % [key, str(good[key])])
		check_eq(store.get_value(key), good[key], key)
	var bad := [
		["strike_mode", "mouse"], ["strike_mode", "power_bar"], ["strike_mode", 1], ["guide_level", 4], ["guide_level", -1],
		["guide_level", 1.5], ["guide_level", "2"], ["aim_sensitivity", 0.2], ["aim_sensitivity", 4.01],
		["fine_tune_sensitivity", NAN], ["fine_tune_sensitivity", "1"], ["stroke_wander", 1],
		["reset_spin_after_shot", "true"], ["volume", 1.5], ["volume", -0.1], ["haptics", null],
		["pocket_camera", 1], ["aim_next_ball", "on"], ["no_such_key", true],
	]
	_mark_log()
	for pair: Array in bad:
		var before: Variant = store.get_value(pair[0])
		check(not store.set_value(pair[0], pair[1]), "%s refuses %s" % [pair[0], str(pair[1])])
		check_eq(store.get_value(pair[0]), before, "%s unchanged" % pair[0])
	var refused := _logged("settings.changed")
	check_eq(refused.size(), bad.size(), "one settings.changed per refusal")
	for line in refused:
		check_eq(line["level"], "warn", "refusal at warn")
		check_eq(line["data"]["ok"], false, "ok false")
		check(not String(line["data"].get("reason", "")).is_empty(), "a reason")
	_cleanup(store)


func test_set_value_normalises_logs_emits_and_saves() -> void:
	var store := _store()
	var emitted: Array = []
	store.changed.connect(func(key: String, value: Variant) -> void: emitted.append([key, value]))
	_mark_log()
	check(store.set_value("guide_level", 2.0), "a whole float is a guide level")
	check_eq(typeof(store.get_value("guide_level")), TYPE_INT, "stored as an int")
	check(store.set_value("volume", 1), "an int volume")
	check_eq(typeof(store.get_value("volume")), TYPE_FLOAT, "stored as a float")
	check_eq(emitted, [["guide_level", 2], ["volume", 1.0]], "changed emitted with stored values")
	var changed := _logged("settings.changed")
	check_eq(changed.size(), 2, "two settings.changed")
	if changed.size() == 2:
		var data: Dictionary = changed[0]["data"]
		check_eq(data["key"], "guide_level", "key")
		check_eq(float(data["old"]), 1.0, "old")
		check_eq(float(data["new"]), 2.0, "new")
		check_eq(data["ok"], true, "ok")
	check(store.set_value("volume", 1.0), "the same value again is fine")
	check_eq(emitted.size(), 2, "but changes nothing")
	check(FileAccess.file_exists(store.path), "saved")
	check(not FileAccess.file_exists(store.path + ".tmp"), "no temporary file left")
	_cleanup(store)


func test_round_trip_through_the_file() -> void:
	var store := _store()
	store.set_value("strike_mode", "pull")
	store.set_value("guide_level", 3)
	store.set_value("aim_sensitivity", 1.75)
	store.set_value("haptics", false)
	var saved: Variant = JSON.parse_string(FileAccess.get_file_as_string(store.path))
	check(saved is Dictionary, "saved JSON")
	if saved is Dictionary:
		check_eq((saved as Dictionary).get("version"), 1.0, "version")
	var again := SettingsStore.new()
	again.path = store.path
	_mark_log()
	again.load_settings()
	check_eq(again.all(), store.all(), "read back the same")
	check_eq(typeof(again.get_value("guide_level")), TYPE_INT, "guide_level an int again")
	var loaded := _logged("settings.loaded")
	if not loaded.is_empty():
		check_eq(loaded[0]["data"]["problems"], [], "no problems")
		_check_values(loaded[0]["data"], store.all(), "the values read logged")
	again.free()
	_cleanup(store)


func test_bad_values_in_the_file_take_defaults() -> void:
	var store := _store()
	_write(store.path, JSON.stringify({"version": 1, "values": {
		"strike_mode": "telekinesis", "guide_level": 2, "volume": 7, "haptics": "yes", "colour": "blue",
	}}))
	_mark_log()
	store.load_settings()
	check_eq(store.get_value("strike_mode"), "stroke", "bad strike mode -> default")
	check_eq(store.get_value("guide_level"), 2, "good value kept")
	check_eq(store.get_value("volume"), 0.8, "out of range -> default")
	check_eq(store.get_value("haptics"), true, "wrong type -> default")
	check(not store.all().has("colour"), "unknown key dropped")
	var loaded := _logged("settings.loaded")
	check_eq(loaded.size(), 1, "settings.loaded once")
	if not loaded.is_empty():
		check_eq(loaded[0]["level"], "warn", "warn when values were refused")
		check_eq((loaded[0]["data"]["problems"] as Array).size(), 4, "four problems: " + str(loaded[0]["data"]["problems"]))
		_check_values(loaded[0]["data"], store.all(), "the values in effect, defaults for the refused ones")
	_cleanup(store)


func test_a_saved_power_bar_becomes_the_pull() -> void:
	# The power bar gave way to Pull and release (KaiC's M2 phone session).
	var store := _store()
	_write(store.path, JSON.stringify({"version": 1, "values": {"strike_mode": "power_bar", "guide_level": 2}}))
	_mark_log()
	store.load_settings()
	check_eq(store.get_value("strike_mode"), "pull", "power_bar read as pull")
	check_eq(store.get_value("guide_level"), 2, "the rest as saved")
	var loaded := _logged("settings.loaded")
	check_eq(loaded.size(), 1, "settings.loaded once")
	if not loaded.is_empty():
		var problems: Array = loaded[0]["data"]["problems"]
		check_eq(problems.size(), 1, "one problem: %s" % [problems])
		check(problems.size() == 1 and String(problems[0]).contains("migrated") and String(problems[0]).contains("power_bar"),
				"the migration named: %s" % [problems])
		_check_values(loaded[0]["data"], store.all(), "the values in effect")
	var saved: Variant = JSON.parse_string(FileAccess.get_file_as_string(store.path))
	check(saved is Dictionary and (saved as Dictionary)["values"]["strike_mode"] == "pull", "saved as pull at once")
	_mark_log()
	store.load_settings()
	loaded = _logged("settings.loaded")
	check(loaded.size() == 1 and (loaded[0]["data"]["problems"] as Array).is_empty(), "so it migrates only once")
	_cleanup(store)


func test_corrupt_file_kept_as_bad_and_defaults_used() -> void:
	var store := _store()
	_write(store.path, "{\"version\": 1, \"values\": {\"guide_level\": 3")
	_mark_log()
	store.load_settings()
	check_eq(store.all(), SettingsStore.DEFAULTS, "defaults")
	check(FileAccess.file_exists(store.path + ".bad"), "copy kept as settings.json.bad")
	check_eq(FileAccess.get_file_as_string(store.path + ".bad"), "{\"version\": 1, \"values\": {\"guide_level\": 3", "the copy is the corrupt text")
	var rewritten: Variant = JSON.parse_string(FileAccess.get_file_as_string(store.path))
	check(rewritten is Dictionary, "settings.json is valid again")
	var loaded := _logged("settings.loaded")
	check_eq(loaded.size(), 1, "settings.loaded once")
	if not loaded.is_empty():
		check_eq(loaded[0]["level"], "warn", "warn")
		check_eq((loaded[0]["data"]["problems"] as Array).size(), 2, "the parse error and the copy")
		_check_values(loaded[0]["data"], SettingsStore.DEFAULTS, "the defaults in effect")
	_write(store.path, "[1, 2]")
	store.load_settings()
	check_eq(store.all(), SettingsStore.DEFAULTS, "an array is corrupt too")
	_cleanup(store)


func test_save_failure_logs_app_error() -> void:
	var store := _store()
	# A file where the directory should be: the temporary file can't be created.
	_write(store.path.get_base_dir().path_join("blocker"), "x")
	store.path = store.path.get_base_dir().path_join("blocker/settings.json")
	_mark_log()
	check(store.set_value("haptics", false), "the value is still taken for this session")
	var errors := _logged("app.error")
	check_eq(errors.size(), 1, "one app.error")
	if not errors.is_empty():
		check_eq(errors[0]["data"]["kind"], "message", "kind")
		check(String(errors[0]["data"]["message"]).contains("Settings not saved"), "message")
	store.path = store.path.get_base_dir().get_base_dir().path_join("settings.json")
	_cleanup(store)


# JSON reads every number as a float, so compare numbers as floats.
func _check_values(data: Dictionary, expected: Dictionary, what: String) -> void:
	var logged: Variant = data.get("values")
	check(logged is Dictionary and (logged as Dictionary).size() == expected.size(), "%s: %s" % [what, str(logged)])
	if not logged is Dictionary:
		return
	for key: String in expected:
		var want: Variant = expected[key]
		var got: Variant = (logged as Dictionary).get(key)
		check_eq(float(got) if want is int and (got is int or got is float) else got, float(want) if want is int else want, "%s: %s" % [what, key])


func _store() -> SettingsStore:
	var store := SettingsStore.new()
	var dir := "user://test_settings_%d_%d" % [Time.get_ticks_usec(), randi()]
	DirAccess.make_dir_recursive_absolute(dir)
	store.path = dir.path_join("settings.json")
	return store


func _cleanup(store: SettingsStore) -> void:
	var dir := store.path.get_base_dir()
	for file in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(dir.path_join(file))
	DirAccess.remove_absolute(dir)
	store.free()


func _write(path: String, text: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string(text)
	file.close()


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


## The lines of `event` logged since _mark_log, read from the session file.
func _logged(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append(parsed)
	return found
