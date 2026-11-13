extends TestCase
## The Params autoload: embedded parameters at start, live ones from the phone link applied at
## the next shot.

const REMOTE := '{"table_params":{"schema_version":1,"values":{"test_value":2}}}'
const REMOTE_HASH := "feedfacefeedface"
const REMOTE_TABLE := '{"schema_version":1,"values":{"test_value":2}}'

var _changes: Array[String] = []
var _log_start := 0
var _offered: Array = []
var _physics_takes := true


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
	var integer_version := RegEx.create_from_string('"schema_version":\\s*\\d+\\s*[,}]')
	check(integer_version.search(text) != null, "schema_version is an integer")
	var reserialised := JSON.stringify({"table_params": JSON.parse_string(text)})
	check(Params.current_hash() != OfsLog.content_hash(reserialised), "integers in the file stay integers")


# What OfsPhysics.member_json does, keeping REMOTE's integers as the binding would.
func _extract(json_text: String, key: String) -> String:
	if json_text == REMOTE and key == "table_params":
		return REMOTE_TABLE
	var json := JSON.new()
	if json.parse(json_text) != OK or not json.data is Dictionary or not (json.data as Dictionary).has(key):
		return ""
	return JSON.stringify(json.data[key])


func _swap_in() -> Array:
	var saved := [Params.extract_member, Params.current(), Params.current_text(), Params.current_hash(),
			Params.get("_pending_params"), Params.get("_pending_text"), Params.pending_hash(), Link.applied_params_hash,
			Params.configure_physics]
	Params.extract_member = _extract
	Params.configure_physics = _configure_physics
	_offered.clear()
	_physics_takes = true
	Params.changed.connect(_on_changed)
	_mark_log()
	return saved


func _restore(saved: Array) -> void:
	Params.changed.disconnect(_on_changed)
	Params.extract_member = saved[0]
	Params.set("_table_params", saved[1])
	Params.set("_text", saved[2])
	Params.set("_hash", saved[3])
	Params.set("_pending_params", saved[4])
	Params.set("_pending_text", saved[5])
	Params.set("_pending_hash", saved[6])
	Link.applied_params_hash = saved[7]
	Params.configure_physics = saved[8]


# Stands in for Physics.try_params, which would refuse REMOTE_TABLE's made-up values.
func _configure_physics(text: String, hash: String) -> bool:
	_offered.append([text, hash])
	return _physics_takes


func test_apply_remote_waits_for_the_next_shot() -> void:
	var saved := _swap_in()
	var old: Dictionary = Params.current()
	var old_hash := Params.current_hash()
	Params.apply_remote(REMOTE, REMOTE_HASH)
	check_eq(Params.current(), old, "current() unchanged until a shot")
	check_eq(Params.current_hash(), old_hash, "hash unchanged")
	check_eq(Link.applied_params_hash, old_hash, "Link still has the applied hash")
	check_eq(Params.pending_hash(), REMOTE_HASH, "pending_hash")
	check_eq(_changes.size(), 0, "changed not emitted")
	check_eq(_logged("params.applied").size(), 0, "nothing applied")
	check_eq(_logged("params.pending"), [{"hash": REMOTE_HASH, "current_hash": old_hash}] as Array[Dictionary], "params.pending")
	_restore(saved)


func test_commit_pending_applies_at_the_shot_boundary() -> void:
	var saved := _swap_in()
	var old_hash := Params.current_hash()
	Params.apply_remote(REMOTE, REMOTE_HASH)
	check(Params.commit_pending("s-7"), "committed")
	check_eq(_offered, [[REMOTE_TABLE, REMOTE_HASH]], "physics was offered the text and hash first")
	check_eq((Params.current()["values"] as Dictionary).get("test_value"), 2.0, "current() replaced")
	check_eq(Params.current_text(), REMOTE_TABLE, "current_text is the binding's text, integers intact")
	check_eq(Params.current_hash(), REMOTE_HASH, "hash")
	check_eq(Params.pending_hash(), "", "nothing pending")
	check_eq(Link.applied_params_hash, REMOTE_HASH, "Link.applied_params_hash")
	check_eq(_changes, [REMOTE_HASH] as Array[String], "changed emitted once")
	check_eq(_logged("params.applied"),
			[{"source": "devlink", "old_hash": old_hash, "new_hash": REMOTE_HASH, "shot_id": "s-7"}] as Array[Dictionary],
			"params.applied names the first shot that used them")
	check(not Params.commit_pending("s-8"), "a second commit has nothing to apply")
	check_eq(_logged("params.applied").size(), 1, "and logs nothing")
	check_eq(_changes.size(), 1, "nor emits")
	_restore(saved)


func test_known_hashes_are_ignored() -> void:
	var saved := _swap_in()
	Params.apply_remote(REMOTE, REMOTE_HASH)
	Params.apply_remote(REMOTE, REMOTE_HASH)
	check_eq(_logged("params.pending").size(), 1, "the pending hash again is ignored")
	Params.commit_pending("s-1")
	Params.apply_remote(REMOTE, REMOTE_HASH)
	check_eq(Params.pending_hash(), "", "the current hash is not pending again")
	check_eq(_logged("params.pending").size(), 1, "nor logged")
	_restore(saved)


func test_a_newer_set_replaces_the_pending_one() -> void:
	var saved := _swap_in()
	var newer := '{"table_params":{"schema_version":1,"values":{"test_value":3}}}'
	Params.apply_remote(REMOTE, REMOTE_HASH)
	Params.apply_remote(newer, "0000000000000003")
	check_eq(Params.pending_hash(), "0000000000000003", "the newer set waits")
	check(Params.commit_pending("s-2"), "committed")
	check_eq((Params.current()["values"] as Dictionary).get("test_value"), 3.0, "the newer values apply")
	check_eq(_logged("params.pending").size(), 2, "both logged as pending")
	check_eq(_logged("params.applied").size(), 1, "one applied")
	_restore(saved)


func test_apply_remote_rejects_invalid_input() -> void:
	var saved := _swap_in()
	var before: Dictionary = Params.current()
	var before_hash := Params.current_hash()
	Params.apply_remote(REMOTE, REMOTE_HASH)
	_mark_log()
	Params.apply_remote("not json", "1111111111111111")
	Params.apply_remote('{"other": {}}', "2222222222222222")
	Params.apply_remote('{"table_params": 3}', "3333333333333333")
	check_eq(Params.current(), before, "current() unchanged")
	check_eq(Params.current_hash(), before_hash, "hash unchanged")
	check_eq(Params.pending_hash(), REMOTE_HASH, "the earlier pending set stays")
	check_eq(Link.applied_params_hash, before_hash, "Link.applied_params_hash unchanged")
	check_eq(_changes.size(), 0, "changed not emitted")
	check_eq(_logged("params.pending").size(), 0, "nothing more pending")
	var errors := _logged("app.error")
	check_eq(errors.size(), 3, "one app.error per rejected set")
	for error: Dictionary in errors:
		check_eq(error.get("kind"), "message", "kind")
		check(str(error.get("message")).contains("(hash "), "names the hash: %s" % error.get("message"))
	_restore(saved)


func test_a_set_physics_refuses_is_dropped() -> void:
	var saved := _swap_in()
	var old: Dictionary = Params.current()
	var old_text := Params.current_text()
	var old_hash := Params.current_hash()
	Params.apply_remote(REMOTE, REMOTE_HASH)
	_physics_takes = false
	check(not Params.commit_pending("s-3"), "nothing applied")
	check_eq(Params.current(), old, "current() unchanged")
	check_eq(Params.current_text(), old_text, "current_text unchanged")
	check_eq(Params.current_hash(), old_hash, "hash unchanged")
	check_eq(Params.pending_hash(), "", "the refused set is dropped")
	check_eq(Link.applied_params_hash, old_hash, "Link.applied_params_hash unchanged")
	check_eq(_changes.size(), 0, "changed not emitted")
	check_eq(_logged("params.applied").size(), 0, "no params.applied")
	check_eq(_logged("params.refused"), [{"hash": REMOTE_HASH, "current_hash": old_hash, "shot_id": "s-3"}] as Array[Dictionary],
			"params.refused names the set, the hash still in use and the shot")
	check(not Params.commit_pending("s-4"), "nothing left to commit")
	_restore(saved)


# The real binding, through the link's signal: an out-of-range value is refused and the shot runs
# on the parameters in use; a later valid set still applies.
func test_out_of_range_parameters_from_the_link_are_refused() -> void:
	var old_text := Params.current_text()
	var old_hash := Params.current_hash()
	var saved_link := Link.applied_params_hash
	Params.changed.connect(_on_changed)
	_changes.clear()
	_mark_log()
	# Earlier tests may leave the text as the binding re-serialised it, without spaces.
	var roll := RegEx.create_from_string('"roll_friction":\\s*0\\.01,')
	var bad_text := roll.sub(old_text, '"roll_friction": 5.0,')
	check(bad_text != old_text, "the test sets roll_friction out of range")
	var bad_hash := OfsLog.content_hash('{"table_params":%s}' % bad_text)
	Link.params_received.emit('{"table_params":%s}' % bad_text, bad_hash)
	check_eq(Params.pending_hash(), bad_hash, "pending")
	var shot_id := Physics.new_shot_id()
	check(not Params.commit_pending(shot_id), "refused at the shot boundary")
	check_eq(_logged("params.refused"), [{"hash": bad_hash, "current_hash": old_hash, "shot_id": shot_id}] as Array[Dictionary],
			"params.refused")
	check_eq(_logged("params.applied").size(), 0, "no params.applied")
	var configured := _logged("physics.configured")
	check(configured.size() == 1 and configured[0]["ok"] == false and str(configured[0]["problems"]).contains("roll_friction"),
			"the binding says why: %s" % [configured])
	check_eq(Params.current_hash(), old_hash, "hash unchanged")
	check_eq(Params.current_text(), old_text, "text unchanged")
	check_eq(Params.pending_hash(), "", "dropped")
	check_eq(Link.applied_params_hash, saved_link, "the PC is not told the set is in use")
	check_eq(Physics.sim.params_hash(), old_hash, "physics still on the old hash")
	check_near(Physics.param("roll_friction", 0.0), 0.01, 1e-12, "and the old value")
	check_eq(_changes.size(), 0, "changed not emitted")

	var balls: Array[Dictionary] = [{"ball": "cue", "index": 0, "x": 0.0, "y": 0.4}, {"ball": "red", "index": 0, "x": 0.0, "y": 0.55}]
	var strike := {"aim_rad": PI / 2.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.5}
	var record := Physics.simulate(balls, 0, strike, {"shot_id": shot_id, "mode": "test", "strike_mode": "harness"})
	check(record != null, "the shot plays")
	var simulated := _logged("shot.simulated")
	check(simulated.size() == 1 and simulated[0]["bundle"]["params_hash"] == old_hash,
			"its bundle carries the hash in use")

	var good_text := roll.sub(old_text, '"roll_friction": 0.0105,')
	var good_hash := OfsLog.content_hash('{"table_params":%s}' % good_text)
	Link.params_received.emit('{"table_params":%s}' % good_text, good_hash)
	check(Params.commit_pending("s-good"), "a later valid set applies")
	check_eq(Params.current_hash(), good_hash, "its hash")
	check_eq(Link.applied_params_hash, good_hash, "Link has it")
	check_eq(Physics.sim.params_hash(), good_hash, "physics runs on it")
	check_near(Physics.param("roll_friction", 0.0), 0.0105, 1e-12, "the new value")
	check_eq(_changes, [good_hash] as Array[String], "changed emitted once")

	Params.apply_remote('{"table_params":%s}' % old_text, old_hash)
	check(Params.commit_pending("test-restore"), "restored")
	check_eq(Params.current_hash(), old_hash, "back on the embedded parameters")
	Link.applied_params_hash = saved_link
	Params.changed.disconnect(_on_changed)
	_changes.clear()


func test_extraction_is_the_binding_by_default() -> void:
	var fresh: Node = load("res://scripts/autoload/params.gd").new()
	var text := '{"a":{"b":1},"c":2}'
	var extracted: String = fresh.get("extract_member").call(text, "a")
	check_eq(extracted, OfsPhysics.member_json(text, "a"), "calls OfsPhysics.member_json")
	check_eq(extracted.replace(" ", ""), '{"b":1}', "the member with its integer")
	fresh.free()


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
