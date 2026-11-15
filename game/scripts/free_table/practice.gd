class_name Practice
extends RefCounted
## The free table's practice shots (spec 12.1): positions measured from broadcasts, each with the
## strike that plays it as filmed on this table (core/tests/fixtures/positions, which the
## calibration suite holds to the film). data/practice.json lists them in menu order with the
## words the player sees.

const HarnessScript := preload("res://scripts/autoload/harness.gd")
const INDEX_PATH := "res://data/practice.json"
const POSITIONS_DIR := "res://data/positions"
const STRIKE_KEYS: Array[String] = ["aim_rad", "tip_x", "tip_y", "elevation_rad", "speed_m_s"]
const AIM_PASSES := 4
const AIM_TOL_RAD := 1.0e-6


## [{"position", "title", "hint", "balls" (the game's ball list), "cue_ball", "strike"}] in the
## index's order. A shot that can't be loaded is left out with app.error saying why.
static func load_all(index_path: String = INDEX_PATH, positions_dir: String = POSITIONS_DIR) -> Array[Dictionary]:
	var shots: Array[Dictionary] = []
	var index: Variant = _read_json(index_path)
	var listed: Variant = (index as Dictionary).get("shots") if index is Dictionary else null
	if not listed is Array:
		_warn("%s has no shots list; the practice menu is empty" % index_path)
		return shots
	for entry: Variant in listed:
		var shot := _load(entry, positions_dir)
		if shot.has("error"):
			_warn("practice shot %s left out: %s" % [JSON.stringify(entry), shot["error"]])
		else:
			shots.append(shot)
	return shots


## The filmed cue in the controls' own words, such as "top 0.17 · right 0.15 · power 98%".
static func cue_text(strike: Dictionary, max_speed_m_s: float, miscue_limit: float) -> String:
	var spin := SpinWidget.describe(Vector2(float(strike["tip_x"]), float(strike["tip_y"])), miscue_limit)
	var power := PullControl.power_for_speed(float(strike["speed_m_s"]), max_speed_m_s)
	return "%s · power %d%%" % [spin, roundi(power * 100.0)]


## The aim the player sets to play the strike as filmed: the one the cue action's allowance for
## side (SpinAim) turns into the strike's own. predictor is Physics.predict's shape.
static func player_aim(predictor: Callable, balls: Array[Dictionary], cue_ball: int, strike: Dictionary,
		elevation_min_rad: float) -> float:
	var want := float(strike["aim_rad"])
	var aim := want
	for i in AIM_PASSES:
		var tried := strike.duplicate()
		tried["aim_rad"] = aim
		var solved := SpinAim.solve(predictor, balls, cue_ball, tried, elevation_min_rad)
		var error := angle_difference(want, float(solved["aim_rad"]))
		if absf(error) < AIM_TOL_RAD:
			break
		aim = wrapf(aim - error, -PI, PI)
	return aim


## True when two ball lists are the same balls in the same places, to the last bit.
static func same_balls(a: Array[Dictionary], b: Array[Dictionary]) -> bool:
	if a.size() != b.size():
		return false
	for i in a.size():
		if String(a[i]["ball"]) != String(b[i]["ball"]) or int(a[i]["index"]) != int(b[i]["index"]) 				or float(a[i]["x"]) != float(b[i]["x"]) or float(a[i]["y"]) != float(b[i]["y"]):
			return false
	return true


static func _load(entry: Variant, positions_dir: String) -> Dictionary:
	if not entry is Dictionary:
		return {"error": "not an object"}
	var e: Dictionary = entry
	for key: String in ["position", "title", "hint"]:
		if not e.get(key) is String or (e[key] as String).is_empty():
			return {"error": "no %s" % key}
	var position_name: String = e["position"]
	if not position_name.is_valid_filename() or position_name.contains("."):
		return {"error": "%s is not a position name" % JSON.stringify(position_name)}
	var path := positions_dir.path_join(position_name + ".json")
	var fixture: Variant = _read_json(path)
	if fixture == null:
		return {"error": "%s is missing or not JSON (scripts/sync_data.py copies it from core/tests/fixtures/positions)" % path}
	var position: Dictionary = HarnessScript.position_from_fixture(fixture)
	if position.has("error"):
		return {"error": "%s: %s" % [path, position["error"]]}
	var strike := _strike((fixture as Dictionary).get("strike"))
	if strike.is_empty():
		return {"error": "%s has no strike with finite %s" % [path, ", ".join(STRIKE_KEYS)]}
	var balls: Array[Dictionary] = []
	balls.assign(position["balls"])
	return {"position": position_name, "title": e["title"], "hint": e["hint"], "balls": balls,
			"cue_ball": int(position["cue_ball"]), "strike": strike}


static func _strike(value: Variant) -> Dictionary:
	if not value is Dictionary:
		return {}
	var strike := {}
	for key in STRIKE_KEYS:
		var number: Variant = (value as Dictionary).get(key)
		if not (number is float or number is int) or not is_finite(float(number)):
			return {}
		strike[key] = float(number)
	return strike


static func _read_json(path: String) -> Variant:
	if not FileAccess.file_exists(path):
		return null
	var json := JSON.new()
	if json.parse(FileAccess.get_file_as_string(path)) != OK:
		return null
	return json.data


static func _warn(message: String) -> void:
	Log.warn("app", "app.error", {"kind": "message", "message": "Practice: " + message})
