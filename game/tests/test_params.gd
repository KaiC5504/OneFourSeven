extends TestCase
## The Params autoload: embedded parameters at start, live ones from the phone link.

const REMOTE := '{"table_params":{"schema_version":1,"values":{"test_value":2}}}'
const REMOTE_HASH := "feedfacefeedface"

var _changes: Array[String] = []
var _log_start := 0


func test_embedded_params_loaded() -> void:
	var text := FileAccess.get_file_as_string("res://data/table_params.json")
	check(not text.is_empty(), "res://data/table_params.json exists")
	check_eq(Params.current(), JSON.parse_string(text), "current() is the embedded file")
	check_eq(Params.current_hash(), OfsLog.content_hash('{"table_params":%s}' % text), "hash covers {table_params: file}")
	check_eq(Link.applied_params_hash, Params.current_hash(), "Link knows the applied hash")
	check_eq(Params.current_hash().length(), 16, "a 16-digit hash: " + Params.current_hash())


func test_embedded_hash_matches_server_hash() -> void:
	# devlink.py hashes the parsed file under "table_params"; re-ordered keys and whitespace
	# must not change it.
	var text := FileAccess.get_file_as_string("res://data/table_params.json")
	var server_form := '{ "table_params" : %s }' % text
	check_eq(Params.current_hash(), OfsLog.content_hash(server_form), "hash ignores formatting")


func test_embedded_hash_is_taken_from_the_file_text() -> void:
	# Godot reads every JSON number as a float, so hashing the re-serialised Dictionary
	# would turn schema_version 1 into 1.0 and miss devlink.py's hash.
	var text := FileAccess.get_file_as_string("res://data/table_params.json")
	check(text.contains('"schema_version": 1') or text.contains('"schema_version":1'), "schema_version is an integer")
	var reserialised := JSON.stringify({"table_params": JSON.parse_string(text)})
	check(Params.current_hash() != OfsLog.content_hash(reserialised), "integers in the file stay integers")


func test_apply_remote_replaces_logs_and_emits() -> void:
	var old: Dictionary = Params.current()
	var old_hash := Params.current_hash()
	Params.changed.connect(_on_changed)
	_mark_log()
	Params.apply_remote(REMOTE, REMOTE_HASH)
	check_eq((Params.current()["values"] as Dictionary).get("test_value"), 2.0, "current() replaced")
	check_eq(Params.current_hash(), REMOTE_HASH, "hash")
	check_eq(Link.applied_params_hash, REMOTE_HASH, "Link.applied_params_hash")
	check_eq(_changes, [REMOTE_HASH] as Array[String], "changed emitted once")
	var applied := _logged("params.applied")
	check_eq(applied.size(), 1, "params.applied logged")
	if not applied.is_empty():
		check_eq(applied[0], {"source": "devlink", "old_hash": old_hash, "new_hash": REMOTE_HASH}, "fields")
	Params.changed.disconnect(_on_changed)
	Params.apply_remote(JSON.stringify({"table_params": old}), old_hash)


func test_apply_remote_rejects_invalid_input() -> void:
	var before: Dictionary = Params.current()
	var before_hash := Params.current_hash()
	Params.changed.connect(_on_changed)
	_mark_log()
	Params.apply_remote("not json", "1111111111111111")
	Params.apply_remote('{"other": {}}', "2222222222222222")
	Params.apply_remote('{"table_params": 3}', "3333333333333333")
	check_eq(Params.current(), before, "current() unchanged")
	check_eq(Params.current_hash(), before_hash, "hash unchanged")
	check_eq(Link.applied_params_hash, before_hash, "Link.applied_params_hash unchanged")
	check_eq(_changes.size(), 0, "changed not emitted")
	var errors := _logged("app.error")
	check_eq(errors.size(), 3, "one app.error per rejected set")
	for error in errors:
		check_eq(error.get("kind"), "message", "kind")
	Params.changed.disconnect(_on_changed)


func _on_changed(hash: String) -> void:
	_changes.append(hash)


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


## The events logged since _mark_log, read from the session file.
func _logged(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append((parsed as Dictionary).get("data", {}))
	return found
