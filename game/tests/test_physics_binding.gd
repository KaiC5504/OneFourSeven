extends TestCase
## OfsPhysics and the Physics autoload: configuration, simulation, record queries and refusals.

const PARAMS_PATH := "res://data/table_params.json"
const TABLE_PATH := "res://data/table_shape.json"
const POSITIONS := "res://data/positions/"
const R := 0.02625

var _log_start := 0


func test_configure_with_the_real_files() -> void:
	var sim := _configured()
	check(sim.is_ready(), "ready after configure")
	check_eq(sim.params_hash(), Params.current_hash(), "params hash as given")
	check_eq(sim.table_hash().length(), 16, "table hash: " + sim.table_hash())
	check_eq(sim.table_hash(), OfsLog.content_hash(FileAccess.get_file_as_string(TABLE_PATH)),
			"table hash is the file's content hash, as sim_cli takes it")
	var shape := sim.table_shape()
	check_eq((shape.get("lines", []) as Array).size(), 18, "lines")
	check_eq((shape.get("arcs", []) as Array).size(), 12, "arcs")
	check_eq((shape.get("pockets", []) as Array).size(), 6, "pockets")
	var file: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(TABLE_PATH))
	for i in (file["lines"] as Array).size():
		var want: Dictionary = file["lines"][i]
		var got: Dictionary = shape["lines"][i]
		check_eq(got["name"], want["name"], "line %d name" % i)
		check_eq(got["kind"], want["kind"], "line %d kind" % i)
		check_eq(got["p1"], want["p1"], "line %d p1" % i)
	for i in (file["arcs"] as Array).size():
		check_eq(shape["arcs"][i]["name"], file["arcs"][i]["name"], "arc %d name" % i)
		var pocket: int = shape["arcs"][i]["pocket"]
		check_eq(shape["pockets"][pocket]["name"], file["arcs"][i]["pocket"], "arc %d pocket index" % i)
	check_near(shape.get("rail_height", 0.0), float(file["rail_height_m"]), 1e-12, "rail_height")
	check(Physics.is_ready(), "the autoload configured itself at start")
	check_eq(Physics.table_shape(), shape, "the autoload's outline")


func test_straight_pot_events_and_speeds() -> void:
	var sim := _configured()
	var balls := _straight_pot_balls()
	var record := sim.simulate(balls, 0, _strike(PI / 2.0, 1.5), 7, _context("test-1"))
	if record == null:
		check(false, "simulated")
		return
	check_eq(record.shot_id(), "test-1", "shot_id from the context")
	check_eq(record.ball_count(), 2, "ball_count")
	var events := record.events()
	check(events.size() >= 3, "strike, contact and pot at least")
	check(events.is_read_only(), "events are read-only")
	check(events == record.events(), "events cached")
	var strike: Dictionary = events[0]
	check_eq(strike["kind"], "strike", "the strike comes first")
	var v: Array = record.strike_outcome()["v"]
	check_near(strike["speed"], sqrt(float(v[0]) * v[0] + float(v[1]) * v[1]), 1e-12, "strike speed is |strike_outcome.v|")
	var hit := _first(events, "ball_ball")
	if hit.is_empty():
		check(false, "a ball_ball event")
		return
	var before := record.state_at(float(hit["t"]) - 1e-6)
	var cue_speed := sqrt(before[2] * before[2] + before[3] * before[3])
	check_near(hit["speed"], cue_speed, 1e-3, "contact speed is the cue ball's just before")
	var pot := _first(events, "pocket")
	check(not pot.is_empty() and pot["a"] == 1, "the red is potted")
	if not pot.is_empty():
		check(float(pot["speed"]) > 0.0, "pot speed")
		check_eq(record.final_balls()[1]["pocket"], pot["body"], "final pocket is the event's")
	for key: String in ["t", "kind", "a", "b", "body", "from", "to", "speed"]:
		check(strike.has(key), "event has " + key)
	check_eq(record.anomalies(), [], "no anomalies")
	var path := record.path(0, 0.01)
	check(path.size() > 2 and path.size() <= 2048, "cue ball path: %d points" % path.size())


func test_state_at_ends_match_inputs_and_final_balls() -> void:
	var sim := _configured()
	var balls := _straight_pot_balls()
	var record := sim.simulate(balls, 0, _strike(PI / 2.0, 1.5), 3, _context("test-2"))
	if record == null:
		check(false, "simulated")
		return
	var stride := OfsShotRecord.kStride
	var start := record.state_at(0.0)
	check_eq(start.size(), balls.size() * stride, "kStride doubles per ball")
	for i in balls.size():
		check_eq(start[i * stride], balls[i]["x"], "ball %d x at 0" % i)
		check_eq(start[i * stride + 1], balls[i]["y"], "ball %d y at 0" % i)
	check_eq(record.state_at(-5.0), start, "clamped below")
	var end := record.state_at(record.t_end())
	check_eq(record.state_at(record.t_end() + 100.0), end, "clamped above")
	var final := record.final_balls()
	check_eq(final.size(), balls.size(), "final_balls size")
	var potted := 0
	for i in final.size():
		var f: Dictionary = final[i]
		check_eq(f["ball"], balls[i]["ball"], "final ball %d name" % i)
		check_near(end[i * stride], f["x"], 1e-9, "ball %d x at t_end" % i)
		check_near(end[i * stride + 1], f["y"], 1e-9, "ball %d y at t_end" % i)
		check_eq(int(end[i * stride + 8]), f["pocket"], "ball %d pocket at t_end" % i)
		if int(f["pocket"]) >= 0:
			potted += 1
			check_eq(int(end[i * stride + 7]), 4, "a potted ball's motion is pocketed")
	check_eq(potted, 1, "one ball potted")
	check(int(final[1]["pocket"]) >= 0, "the red has a pocket")


func test_final_balls_feed_the_next_shot() -> void:
	var sim := _configured()
	var record := sim.simulate(_straight_pot_balls(), 0, _strike(PI / 2.0, 1.5), 3, _context("test-4"))
	if record == null:
		check(false, "simulated")
		return
	var after: Array[Dictionary] = []
	for b: Dictionary in record.final_balls():
		after.append(b)
	var pocket: int = after[1]["pocket"]
	var next := sim.predict(after, 0, _strike(PI / 2.0, 3.0))
	if next == null:
		check(false, "final_balls accepted as a ball list")
		return
	check_eq(int(next.state_at(0.0)[OfsShotRecord.kStride + 8]), pocket, "the potted red stays in its pocket")
	check_eq(next.final_balls()[1]["pocket"], pocket, "and is still potted after")
	check(_first(next.events(), "ball_ball").is_empty(), "nothing hits a potted ball")
	var cue_potted := _with(after, 0, "pocket", 2)
	_mark_log()
	check(sim.simulate(cue_potted, 0, _strike(0.0, 1.0), 1, _context("potted-cue")) == null, "potted cue ball")
	check(sim.simulate(_with(after, 1, "pocket", 6), 0, _strike(0.0, 1.0), 1, _context("bad-pocket")) == null,
			"no pocket 6")
	check_eq(_logged("shot.rejected").size(), 2, "both rejected")


func test_predict_is_deterministic_and_logs_nothing() -> void:
	var sim := _configured()
	var balls := _straight_pot_balls()
	_mark_log()
	var a := sim.predict(balls, 0, _strike(PI / 2.0 + 0.01, 3.0))
	var b := sim.predict(balls, 0, _strike(PI / 2.0 + 0.01, 3.0))
	if a == null or b == null:
		check(false, "predicted")
		return
	check_eq(a.state_at(a.t_end()), b.state_at(b.t_end()), "same final state")
	check_eq(a.events(), b.events(), "same events")
	check_eq(a.shot_id(), "", "no shot_id")
	check_eq(sim.last_error(), "", "no error after a prediction")
	check_eq(_logged_count(), 0, "nothing logged")
	for i in 3:
		check(sim.predict(balls, 5147, _strike(0.0, 1.0)) == null, "refused")
	var reason := "cue_ball 5147 is out of range (2 balls)"
	check_eq(sim.last_error(), reason, "last_error says why")
	var refused := _logged("physics.query_refused")
	check_eq(refused, [{"what": "predict", "reason": reason}], "logged once however often it's asked")
	check(sim.predict(balls, 0, _strike(PI / 2.0, 1.0)) != null, "predicted again")
	check_eq(sim.last_error(), "", "cleared by the next prediction")


func test_simulate_logs_shot_simulated_with_context() -> void:
	var sim := _configured()
	_mark_log()
	var record := sim.simulate(_straight_pot_balls(), 0, _strike(PI / 2.0, 1.5), -42, _context("test-3"))
	check(record != null, "simulated")
	var lines := _logged("shot.simulated")
	check_eq(lines.size(), 1, "one shot.simulated")
	if lines.is_empty():
		return
	check_eq(lines[0]["context"], _context("test-3"), "context")
	var bundle: Dictionary = lines[0]["bundle"]
	check_eq(bundle["seed"], "18446744073709551574", "seed is the int64's bits as uint64")
	check_eq(bundle["params_hash"], sim.params_hash(), "params_hash")
	check_eq(bundle["table_hash"], sim.table_hash(), "table_hash")
	check_eq(bundle["has_strike"], true, "has_strike")


func test_min_cue_elevation_over_the_baulk_cushion() -> void:
	var sim := _configured()
	var position := _position("cue_ball_on_baulk_cushion")
	var degrees := rad_to_deg(sim.min_cue_elevation(position["balls"], position["cue_ball"], _strike(0.0, 2.0)))
	# The bought table's rail stands 44.5 mm above the cloth (it was 40 mm): about 6 degrees steeper.
	check(degrees >= 44.0 and degrees <= 50.0, "about 47 degrees for tip_y 0, got %f" % degrees)
	var high := _strike(0.0, 2.0)
	high["tip_y"] = 0.4
	check(sim.min_cue_elevation(position["balls"], position["cue_ball"], high) < deg_to_rad(degrees) - 0.1,
			"lower with the tip high")
	check_eq(sim.min_cue_elevation(_straight_pot_balls(), 0, {"aim_rad": PI / 2.0, "tip_x": 0.0, "tip_y": 0.0}),
			0.0, "nothing in the way; only aim and tip are needed")
	var fresh := OfsPhysics.new()
	check_eq(fresh.min_cue_elevation(position["balls"], 0, _strike(0.0, 2.0)), 0.0, "0 unconfigured")
	check_eq(fresh.last_error(), "physics is not configured", "unconfigured, says last_error")
	_mark_log()
	var bad := {"aim_rad": 0.0, "tip_x": "tip-147", "tip_y": 0.0}
	check_eq(sim.min_cue_elevation(position["balls"], position["cue_ball"], bad), 0.0, "0 for a malformed strike")
	check_eq(sim.min_cue_elevation(position["balls"], position["cue_ball"], bad), 0.0, "and again")
	var reason := 'strike: "tip_x" is not a number (tip-147)'
	check_eq(sim.last_error(), reason, "last_error says why")
	check_eq(_logged("physics.query_refused"), [{"what": "min_cue_elevation", "reason": reason}], "logged once")
	sim.min_cue_elevation(position["balls"], position["cue_ball"], high)
	check_eq(sim.last_error(), "", "cleared by an answer")


func test_malformed_input_is_rejected_and_logged() -> void:
	var sim := _configured()
	var good := _straight_pot_balls()
	var twice := _with(good, 0, "x", 0.0)
	twice.append(good[1].duplicate())
	var cases := {
		"unknown ball": [_with(good, 1, "ball", "purple"), 0, _strike(0.0, 1.0)],
		"missing x": [_without(good, 1, "x"), 0, _strike(0.0, 1.0)],
		"nan y": [_with(good, 0, "y", NAN), 0, _strike(0.0, 1.0)],
		"red index": [_with(good, 1, "index", 15), 0, _strike(0.0, 1.0)],
		"cue_ball out of range": [good, 2, _strike(0.0, 1.0)],
		"cue_ball not the cue": [good, 1, _strike(0.0, 1.0)],
		"missing speed": [good, 0, {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0}],
		"infinite aim": [good, 0, _strike(INF, 1.0)],
		"red 0 twice": [twice, 0, _strike(0.0, 1.0)],
		"cue_ball past 32 bits": [good, 1 << 32, _strike(0.0, 1.0)],
		"cue_ball below 32 bits": [good, -(1 << 32), _strike(0.0, 1.0)],
	}
	_mark_log()
	for name: String in cases:
		var c: Array = cases[name]
		check(sim.simulate(c[0], c[1], c[2], 1, _context("bad-" + name)) == null, name + " returns null")
	var rejected := _logged("shot.rejected")
	check_eq(rejected.size(), cases.size(), "one shot.rejected each")
	for i in mini(rejected.size(), cases.size()):
		var c: Array = cases.values()[i]
		check_eq(rejected[i]["shot_id"], "bad-" + str(cases.keys()[i]), "shot_id")
		check(not str(rejected[i]["reason"]).is_empty(), "a reason")
		check_eq(rejected[i]["cue_ball"], c[1], "cue_ball as passed")
		check_eq((rejected[i]["balls"] as Array).size(), (c[0] as Array).size(), "balls as passed")
		check_eq((rejected[i]["strike"] as Dictionary).keys().size(), (c[2] as Dictionary).size(), "strike as passed")
	if rejected.size() == cases.size():
		var by_name := {}
		for i in cases.size():
			by_name[cases.keys()[i]] = rejected[i]
		check_eq(by_name["unknown ball"]["balls"][1]["ball"], "purple", "the bad name is logged")
		check_eq(by_name["nan y"]["balls"][0]["y"], "nan", "a non-finite number as text")
		check_eq(by_name["infinite aim"]["strike"]["aim_rad"], "inf", "in the strike too")
		check_eq(by_name["missing x"]["balls"][1].has("x"), false, "a missing key stays missing")
		check_eq(by_name["red 0 twice"]["reason"], "balls[2]: red 0 is already in the list", "duplicate named")
		check_eq(by_name["cue_ball past 32 bits"]["reason"], "cue_ball 4294967296 is out of range (2 balls)",
				"not truncated to 0")
		check_eq(by_name["cue_ball past 32 bits"]["cue_ball"], 4294967296, "logged whole")
	check_eq(_logged("shot.simulated").size(), 0, "nothing simulated")
	_mark_log()
	check(OfsPhysics.new().simulate(good, 0, _strike(0.0, 1.0), 1, _context("early")) == null, "unconfigured")
	check_eq(_logged("shot.rejected").size(), 1, "unconfigured logs shot.rejected")


func test_a_full_table_is_the_most_a_ball_list_holds() -> void:
	var sim := _configured()
	var rack: Array[Dictionary] = []
	for b: Dictionary in sim.opening_rack(5):
		rack.append(b)
	check(sim.predict(rack, 0, _strike(0.0, 1.0)) != null, "all 22 balls")
	var extra := rack.duplicate()
	extra.append(rack[21].duplicate())
	check(sim.predict(extra, 0, _strike(0.0, 1.0)) == null, "a 23rd is a ball listed twice")
	check_eq(sim.last_error(), "balls[22]: black 0 is already in the list", "named")


func test_64_bit_indices_are_refused_not_truncated() -> void:
	var sim := _configured()
	var balls := _straight_pot_balls()
	var past := (1 << 32) + 1
	check(sim.predict(balls, 1 << 32, _strike(PI / 2.0, 1.5)) == null, "predict: 2^32 is not ball 0")
	check_eq(sim.min_cue_elevation(balls, 1 << 32, _strike(PI / 2.0, 1.5)), 0.0, "min_cue_elevation: nor here")
	check_eq(sim.last_error(), "cue_ball 4294967296 is out of range (2 balls)", "last_error")
	var record := sim.predict(balls, 0, _strike(PI / 2.0, 1.5))
	if record == null:
		check(false, "predicted")
		return
	_mark_log()
	check_eq(record.path(past, 0.01).size(), 0, "path: 2^32 + 1 is not ball 1")
	check_eq(record.path(-1, 0.01).size(), 0, "path: no ball -1")
	check(record.path(1, 0.01).size() > 1, "ball 1's path")
	var refused := _logged("physics.query_refused")
	check_eq(refused.size(), 2, "both refusals logged")
	if refused.size() == 2:
		check_eq(refused[0], {"what": "path", "reason": "ball 4294967297 is out of range (2 balls)"}, "path refusal")


func test_bad_params_are_refused_and_the_old_ones_kept() -> void:
	var sim := _configured()
	var hash := sim.params_hash()
	var rack := sim.opening_rack(1)
	var table_text := FileAccess.get_file_as_string(TABLE_PATH)
	_mark_log()
	check(not sim.configure("{not json", "badbadbadbadbad1", table_text), "unparsable params refused")
	var values_text := FileAccess.get_file_as_string(PARAMS_PATH).replace('"max_events": 50000', '"max_events": -3')
	check(not sim.configure(values_text, "badbadbadbadbad2", table_text), "an invalid value refused")
	check(not sim.configure(FileAccess.get_file_as_string(PARAMS_PATH), "badbadbadbadbad3", "[]"), "bad table refused")
	check(sim.is_ready(), "still ready")
	check_eq(sim.params_hash(), hash, "old hash kept")
	check_eq(sim.opening_rack(1), rack, "old configuration still simulates")
	var configured := _logged("physics.configured")
	check_eq(configured.size(), 3, "physics.configured for each attempt")
	for c in configured:
		check_eq(c["ok"], false, "ok false")
		check(not (c["problems"] as Array).is_empty(), "problems listed")
	if configured.size() == 3:
		check_eq(configured[1]["params_hash"], "badbadbadbadbad2", "the refused hash is named")
	var fresh := OfsPhysics.new()
	check(not fresh.configure("", "", table_text), "empty params refused")
	check(not fresh.is_ready(), "never configured")
	check_eq(fresh.table_shape(), {}, "no outline")
	check_eq(fresh.params_values(), {}, "no values")
	check_eq(fresh.opening_rack(1), [], "no rack")


func test_member_json_keeps_integers() -> void:
	var text := FileAccess.get_file_as_string(PARAMS_PATH)
	var values := OfsPhysics.member_json(text, "values")
	check(values.contains('"max_events":50000'), "max_events stays an integer")
	check(not values.contains("50000.0"), "not a float")
	check_eq(OfsPhysics.member_json(text, "nope"), "", "missing key")
	check_eq(OfsPhysics.member_json("[1]", "values"), "", "not an object")
	check_eq(OfsPhysics.member_json("{bad", "values"), "", "not JSON")
	var sim := _configured()
	check(sim.params_values()["max_events"] is int, "params_values keeps integers")
	check_near(Physics.param("miscue_limit", -1.0), sim.params_values()["miscue_limit"], 0.0, "Physics.param")
	check_eq(Physics.param("no_such_param", 7.5), 7.5, "fallback")


func test_opening_rack_has_22_balls_none_touching() -> void:
	var sim := _configured()
	var rack := sim.opening_rack(147)
	check_eq(rack.size(), 22, "22 balls")
	check_eq(rack[0]["ball"] if not rack.is_empty() else "", "cue", "cue ball first")
	for i in rack.size():
		for j in range(i + 1, rack.size()):
			var dx: float = rack[i]["x"] - rack[j]["x"]
			var dy: float = rack[i]["y"] - rack[j]["y"]
			var d := sqrt(dx * dx + dy * dy)
			check(d > 2.0 * R, "balls %d and %d touch (%.9f)" % [i, j, d])
	check(sim.opening_rack(147) == rack, "same seed, same rack")
	check(sim.opening_rack(148) != rack, "another seed, other gaps")
	var typed := Physics.opening_rack(147)
	check_eq(typed.size(), 22, "the autoload's rack")


func test_autoload_ids_seeds_and_reconfigure() -> void:
	var first := Physics.new_shot_id()
	var second := Physics.new_shot_id()
	var prefix := Log.session() + "-"
	check(first.begins_with(prefix) and second.begins_with(prefix), "session prefix: " + first)
	check_eq(int(second.trim_prefix(prefix)), int(first.trim_prefix(prefix)) + 1, "counting up")
	for i in 50:
		check(Physics.new_seed() >= 0, "non-negative seed")
	check(Physics.new_seed() != Physics.new_seed(), "seeds differ")
	_mark_log()
	check(Physics.try_params(Params.current_text(), Params.current_hash()), "try_params takes the parameters in use")
	var configured := _logged("physics.configured")
	check_eq(configured.size(), 1, "try_params reconfigures")
	if not configured.is_empty():
		check_eq(configured[0]["ok"], true, "with the same parameters")
	var record := Physics.simulate(_straight_pot_balls(), 0, _strike(PI / 2.0, 1.5), _context("autoload"))
	check(record != null and record.shot_id() == "autoload", "Physics.simulate")
	check_eq(Physics.last_error(), "", "Physics.last_error is empty after a shot")


func _configured() -> OfsPhysics:
	var sim := OfsPhysics.new()
	var ok := sim.configure(FileAccess.get_file_as_string(PARAMS_PATH), Params.current_hash(),
			FileAccess.get_file_as_string(TABLE_PATH))
	check(ok, "configure with the real files")
	return sim


## The cue ball below a red that sits 0.34 m from the left middle pocket, both on x = 0.
func _straight_pot_balls() -> Array[Dictionary]:
	return [{"ball": "cue", "index": 0, "x": 0.0, "y": 0.4}, {"ball": "red", "index": 0, "x": 0.0, "y": 0.55}]


func _strike(aim: float, speed: float) -> Dictionary:
	return {"aim_rad": aim, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": speed}


func _context(shot_id: String) -> Dictionary:
	return {"shot_id": shot_id, "mode": "free_table", "strike_mode": "pull"}


func _first(events: Array, kind: String) -> Dictionary:
	for e: Dictionary in events:
		if e["kind"] == kind:
			return e
	return {}


func _with(balls: Array[Dictionary], i: int, key: String, value: Variant) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for b in balls:
		out.append(b.duplicate())
	out[i][key] = value
	return out


func _without(balls: Array[Dictionary], i: int, key: String) -> Array[Dictionary]:
	var out := _with(balls, i, key, null)
	out[i].erase(key)
	return out


## A fixture from core/tests/fixtures/positions as a ball list.
func _position(name: String) -> Dictionary:
	var parsed: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(POSITIONS + name + ".json"))
	var balls: Array[Dictionary] = []
	for b: Dictionary in parsed["start"]["balls"]:
		balls.append({"ball": b["ball"], "index": int(b["index"]), "x": b["r"][0], "y": b["r"][1]})
	return {"balls": balls, "cue_ball": int(parsed["cue_ball"])}


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


func _lines_since_mark() -> PackedStringArray:
	OfsLog.flush()
	return FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8().split("\n", false)


func _logged_count() -> int:
	return _lines_since_mark().size()


## The events logged since _mark_log, read from the session file.
func _logged(event: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for line in _lines_since_mark():
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append((parsed as Dictionary).get("data", {}))
	return found
