class_name FreeTable
extends Node
## Free table (spec 12.1): a full rack or an empty table, an edit mode that moves, adds and
## removes balls and places the cue ball anywhere legal, retake (the exact pre-shot state and
## shot setup again) and undo and redo across the session's shots. No rules or fouls.
##
## History is a list of table states and an index. A shot, a setup or the first edit of an
## edit-mode session pushes a state (dropping any redo states); later edits in the same session
## change that state in place. Being at state i, the ShotSetup snapshot that goes with it is the
## one of the shot played from i, if any: undo, retake and redo return it.

signal state_changed(balls: Array[Dictionary], cue_ball: int, reason: StringName)
signal history_changed(can_undo: bool, can_redo: bool, can_retake: bool)
signal edit_mode_changed(on: bool)

const OPENING_GAP := 0.0005
const COLOURS: Array[String] = ["yellow", "green", "brown", "blue", "pink", "black"]
const RED_COUNT := 15
## Clearance beyond touching that a placed ball needs from every other ball.
const CLEARANCE := 1e-6

var balls: Array[Dictionary] = []
var cue_ball := 0
var edit_mode := false

## Entries {balls: Array[Dictionary], cue_ball: int, kind: "setup" | "shot" | "edit" | "harness",
## snapshot: Dictionary (shots only), shot_id: String (shots only)}.
var _history: Array[Dictionary] = []
var _index := -1
## The history entry this edit-mode session owns, or -1 until its first edit.
var _edit_entry := -1
## {snapshot, shot_id} between shot_started and shot_finished.
var _pending_shot := {}


## "full_rack" (the physics rack with seeded micro-gaps, spec 7.7) or "empty" (the cue ball
## alone, in the D). rack_seed -1 takes a fresh one from Physics. A new history state: undo returns
## to the table before it.
func load_setup(setup_name: String, rack_seed: int = -1) -> bool:
	var layout: Array[Dictionary] = []
	var detail := {"setup": setup_name}
	match setup_name:
		"full_rack":
			if rack_seed < 0:
				rack_seed = Physics.new_seed()
			detail["seed"] = str(rack_seed)
			layout = Physics.opening_rack(rack_seed)
			detail["source"] = "physics"
			if layout.is_empty():
				Log.warn("app", "app.error", {"kind": "message", "message":
						"FreeTable: Physics.opening_rack(%d) returned no balls (physics not configured?); using OfsTable.opening_layout(%s) without micro-gaps" % [rack_seed, OPENING_GAP]})
				layout = _opening_layout()
				detail["source"] = "opening_layout"
		"empty":
			var opening := _opening_layout()
			if not opening.is_empty():
				layout.append(opening[0])
		_:
			_log_action("setup", {"setup": setup_name, "refused": "unknown setup; known: full_rack, empty"})
			return false
	var problems := ball_list_problems(layout, 0)
	if not problems.is_empty():
		Log.warn("app", "app.error", {"kind": "message", "message":
				"FreeTable: setup %s has a malformed ball list: %s" % [setup_name, "; ".join(problems)]})
		return false
	_push({"balls": _copy_balls(layout), "cue_ball": 0, "kind": "setup"})
	detail["balls"] = layout.size()
	_log_action("setup", detail)
	_show(&"setup")
	return true


## A position from elsewhere (the harness): replaces the table and resets history to that one
## state. False, with the table unchanged, when the list is malformed (ball_list_problems).
## Touching or overlapping balls are taken as given.
func set_balls(new_balls: Array[Dictionary], new_cue_ball: int) -> bool:
	var problems := ball_list_problems(new_balls, new_cue_ball)
	if not problems.is_empty():
		Log.warn("app", "app.error", {"kind": "message", "message":
				"FreeTable.set_balls refused: " + "; ".join(problems)})
		return false
	_history.clear()
	_index = -1
	_push({"balls": _copy_balls(new_balls), "cue_ball": new_cue_ball, "kind": "harness"})
	_log_action("set_balls", {"balls": new_balls.size()})
	_show(&"set_balls")
	return true


## setup_snapshot is ShotSetup.snapshot() as it was struck; shot_id the shot's id.
func shot_started(setup_snapshot: Dictionary, shot_id: String = "") -> void:
	_pending_shot = {"snapshot": setup_snapshot.duplicate(true), "shot_id": shot_id}


## OfsShotRecord.final_balls(): potted balls leave the table; a potted cue ball comes back at the
## opening cue position, or the nearest legal spot on the baulk line inside the D.
func shot_finished(final_balls: Array) -> void:
	if _pending_shot.is_empty():
		Log.warn("app", "app.error", {"kind": "message", "message":
				"FreeTable.shot_finished without shot_started; the shot is kept without a setup to retake"})
	var shot: Dictionary = _pending_shot if not _pending_shot.is_empty() else {"snapshot": {}, "shot_id": ""}
	_pending_shot = {}
	var after: Array[Dictionary] = []
	var potted: Array[String] = []
	var cue_potted := false
	for entry: Variant in final_balls:
		var ball: Dictionary = entry if entry is Dictionary else {}
		var copy := {"ball": String(ball.get("ball", "")), "index": int(ball.get("index", 0)),
				"x": float(ball.get("x", NAN)), "y": float(ball.get("y", NAN))}
		if int(ball.get("pocket", -1)) >= 0:
			potted.append(_ball_label(copy))
			if copy["ball"] == "cue":
				cue_potted = true
				after.append(copy)
			continue
		after.append(copy)
	var detail := {"potted": potted}
	if cue_potted:
		var at := _cue_ball_index(after)
		var spot := _return_spot(after, at)
		after[at]["x"] = spot["x"]
		after[at]["y"] = spot["y"]
		detail["cue_ball"] = spot["detail"]
	var new_cue := _cue_ball_index(after)
	var problems := ball_list_problems(after, new_cue)
	if not problems.is_empty():
		Log.warn("app", "app.error", {"kind": "message", "message":
				"FreeTable.shot_finished refused malformed final balls: " + "; ".join(problems)})
		return
	_push({"balls": after, "cue_ball": new_cue, "kind": "shot", "snapshot": shot["snapshot"],
			"shot_id": shot["shot_id"]})
	_log_action("shot", detail, String(shot["shot_id"]))
	_show(&"shot")


## Undoes the shot that led to the current state: the pre-shot table, and its ShotSetup
## snapshot. {} when the current state didn't come from a shot.
func retake() -> Dictionary:
	if not can_retake():
		_log_action("retake", {"refused": "the table didn't come from a shot"})
		return {}
	return _go_back("retake")


func undo() -> Dictionary:
	if not can_undo():
		_log_action("undo", {"refused": "nothing to undo"})
		return {}
	return _go_back("undo")


## The snapshot is the one for the shot played next from the state redone to, if any.
func redo() -> Dictionary:
	if not can_redo():
		_log_action("redo", {"refused": "nothing to redo"})
		return {}
	_index += 1
	_edit_entry = -1
	_log_action("redo", {}, String(_history[_index].get("shot_id", "")))
	_show(&"redo")
	return _next_shot_snapshot()


func can_undo() -> bool:
	return _index > 0


func can_redo() -> bool:
	return _index >= 0 and _index < _history.size() - 1


func can_retake() -> bool:
	return can_undo() and _history[_index]["kind"] == "shot"


func history_index() -> int:
	return _index


func history_size() -> int:
	return _history.size()


func set_edit_mode(on: bool) -> void:
	if on == edit_mode:
		return
	edit_mode = on
	_edit_entry = -1
	_log_action("edit_mode", {"on": on})
	edit_mode_changed.emit(on)


## Edits; each false (and logged) when it isn't allowed or the spot isn't legal. index is the
## ball's position in `balls`.
func move_ball(index: int, xy: Vector2) -> bool:
	var to := [float(xy.x), float(xy.y)]
	if index < 0 or index >= balls.size():
		return _refuse_edit("move", "none", index, [], to, "no ball at list position %d" % index)
	var ball := balls[index]
	var from := [ball["x"], ball["y"]]
	if not edit_mode:
		return _refuse_edit("move", ball["ball"], ball["index"], from, to, "not in edit mode")
	var reason := _illegal_reason(balls, to[0], to[1], index)
	if not reason.is_empty():
		return _refuse_edit("move", ball["ball"], ball["index"], from, to, reason)
	var entry := _edit_state()
	var moved: Dictionary = (entry["balls"] as Array[Dictionary])[index]
	moved["x"] = to[0]
	moved["y"] = to[1]
	_log_edit("move", ball["ball"], ball["index"], from, to)
	_show(&"edit")
	return true


## "red" takes the lowest free red index; a colour only when it isn't on the table; never a
## second cue ball. Appended to the end of `balls`.
func add_ball(ball: String, xy: Vector2) -> bool:
	var to := [float(xy.x), float(xy.y)]
	if not edit_mode:
		return _refuse_edit("add", ball, 0, [], to, "not in edit mode")
	if ball == "cue":
		return _refuse_edit("add", ball, 0, [], to, "there is always exactly one cue ball")
	var index := 0
	if ball == "red":
		index = _free_red_index()
		if index < 0:
			return _refuse_edit("add", ball, 0, [], to, "all %d reds are on the table" % RED_COUNT)
	elif COLOURS.has(ball):
		for b in balls:
			if b["ball"] == ball:
				return _refuse_edit("add", ball, 0, [], to, "%s is already on the table" % ball)
	else:
		return _refuse_edit("add", ball, 0, [], to, "unknown ball; known: red, %s" % ", ".join(COLOURS))
	var reason := _illegal_reason(balls, to[0], to[1], -1)
	if not reason.is_empty():
		return _refuse_edit("add", ball, index, [], to, reason)
	var entry := _edit_state()
	(entry["balls"] as Array[Dictionary]).append({"ball": ball, "index": index, "x": to[0], "y": to[1]})
	_log_edit("add", ball, index, [], to)
	_show(&"edit")
	return true


func remove_ball(index: int) -> bool:
	if index < 0 or index >= balls.size():
		return _refuse_edit("remove", "none", index, [], [], "no ball at list position %d" % index)
	var ball := balls[index]
	var from := [ball["x"], ball["y"]]
	if not edit_mode:
		return _refuse_edit("remove", ball["ball"], ball["index"], from, [], "not in edit mode")
	if index == cue_ball:
		return _refuse_edit("remove", ball["ball"], ball["index"], from, [], "the cue ball can't be removed")
	var entry := _edit_state()
	(entry["balls"] as Array[Dictionary]).remove_at(index)
	if index < int(entry["cue_ball"]):
		entry["cue_ball"] = int(entry["cue_ball"]) - 1
	_log_edit("remove", ball["ball"], ball["index"], from, [])
	_show(&"edit")
	return true


## On the cloth inside the cushion noses (|x| <= L/2 - R, |y| <= W/2 - R) and at least
## 2R + CLEARANCE from every other ball (ignore: that ball's position in `balls`).
func is_legal(xy: Vector2, ignore: int = -1) -> bool:
	return _illegal_reason(balls, float(xy.x), float(xy.y), ignore).is_empty()


## Why a ball list can't be a table state: unknown names, missing or non-finite numbers, a
## duplicate red index or colour, not exactly one cue ball, or cue_ball not pointing at it.
static func ball_list_problems(list: Array, cue_index: int) -> Array[String]:
	var problems: Array[String] = []
	var seen := {}
	var cues := 0
	for i in list.size():
		if not list[i] is Dictionary:
			problems.append("entry %d is not a Dictionary" % i)
			continue
		var b: Dictionary = list[i]
		if not b.has_all(["ball", "index", "x", "y"]):
			problems.append("entry %d lacks ball, index, x or y" % i)
			continue
		var name := str(b["ball"])
		if name != "cue" and name != "red" and not COLOURS.has(name):
			problems.append("entry %d: unknown ball %s" % [i, name])
			continue
		if not (b["x"] is float or b["x"] is int) or not (b["y"] is float or b["y"] is int) \
				or not is_finite(float(b["x"])) or not is_finite(float(b["y"])):
			problems.append("entry %d (%s): x and y must be finite numbers" % [i, name])
		var id := "%s %d" % [name, int(b["index"])] if name == "red" else name
		if name == "red" and (int(b["index"]) < 0 or int(b["index"]) >= RED_COUNT):
			problems.append("entry %d: red index %d outside 0-%d" % [i, int(b["index"]), RED_COUNT - 1])
		if seen.has(id):
			problems.append("entry %d: a second %s" % [i, id])
		seen[id] = true
		if name == "cue":
			cues += 1
	if cues != 1:
		problems.append("%d cue balls; there must be exactly one" % cues)
	elif cue_index < 0 or cue_index >= list.size() or not list[cue_index] is Dictionary \
			or (list[cue_index] as Dictionary).get("ball") != "cue":
		problems.append("cue_ball %d is not the cue ball's position" % cue_index)
	return problems


static func _illegal_reason(list: Array[Dictionary], x: float, y: float, ignore: int) -> String:
	var dims := OfsTable.dimensions()
	var r := float(dims["ball_radius"])
	if absf(x) > float(dims["playing_length"]) / 2.0 - r or absf(y) > float(dims["playing_width"]) / 2.0 - r:
		return "outside the cushions"
	var min_distance := 2.0 * r + CLEARANCE
	for i in list.size():
		if i == ignore:
			continue
		var b := list[i]
		var dx := float(b["x"]) - x
		var dy := float(b["y"]) - y
		if dx * dx + dy * dy < min_distance * min_distance:
			return "overlaps %s" % _ball_label(b)
	return ""


func _go_back(action: String) -> Dictionary:
	var left := _history[_index]
	_index -= 1
	_edit_entry = -1
	_log_action(action, {}, String(left.get("shot_id", "")))
	_show(StringName(action))
	return _next_shot_snapshot()


## The snapshot of the shot played from the current state, if the next state is a shot.
func _next_shot_snapshot() -> Dictionary:
	if _index + 1 < _history.size() and _history[_index + 1]["kind"] == "shot":
		return (_history[_index + 1]["snapshot"] as Dictionary).duplicate(true)
	return {}


## The state an edit changes: the session's own entry, or a new one on the session's first edit.
func _edit_state() -> Dictionary:
	if _edit_entry >= 0 and _edit_entry == _index:
		return _history[_index]
	var current: Array[Dictionary] = []
	var current_cue := 0
	if _index >= 0:
		current = _copy_balls(_history[_index]["balls"])
		current_cue = int(_history[_index]["cue_ball"])
	_push({"balls": current, "cue_ball": current_cue, "kind": "edit"})
	_edit_entry = _index
	_log_action("edit", {"new_state": "first edit of this edit-mode session"})
	return _history[_index]


func _push(entry: Dictionary) -> void:
	_history.resize(_index + 1)
	_history.append(entry)
	_index = _history.size() - 1
	_edit_entry = -1


func _show(reason: StringName) -> void:
	var entry := _history[_index]
	balls = _copy_balls(entry["balls"])
	cue_ball = int(entry["cue_ball"])
	state_changed.emit(balls, cue_ball, reason)
	history_changed.emit(can_undo(), can_redo(), can_retake())


## Where a potted cue ball comes back: the opening cue position, else the nearest legal point
## on the baulk line inside the D, else the nearest inside the D.
static func _return_spot(after: Array[Dictionary], cue_at: int) -> Dictionary:
	var home: Dictionary = _opening_layout()[0]
	var hx := float(home["x"])
	var hy := float(home["y"])
	var result := {"x": hx, "y": hy, "detail": "potted; back on the opening cue position"}
	if not _illegal_reason(after, hx, hy, cue_at).is_empty():
		var spot := _nearest_on_baulk_line(after, hx, hy, cue_at)
		if spot.is_empty():
			spot = _nearest_in_d(after, hx, hy, cue_at)
		if spot.is_empty():
			result["detail"] = "potted; no legal spot in the D, so back on the opening cue position, overlapping"
		else:
			result["x"] = spot["x"]
			result["y"] = spot["y"]
			result["detail"] = "potted; opening cue position blocked, placed at [%.6f, %.6f] in the D" % [spot["x"], spot["y"]]
	return result


static func _nearest_on_baulk_line(list: Array[Dictionary], hx: float, hy: float, cue_at: int) -> Dictionary:
	var dims := OfsTable.dimensions()
	var d_radius := float(dims["d_radius"])
	# A little beyond CLEARANCE, so the endpoint of a blocked stretch passes the legality check.
	var reach := 2.0 * float(dims["ball_radius"]) + 2.0 * CLEARANCE
	var candidates: Array[float] = []
	for i in list.size():
		if i == cue_at:
			continue
		var dx := float(list[i]["x"]) - hx
		if absf(dx) < reach:
			var half := sqrt(reach * reach - dx * dx)
			candidates.append(float(list[i]["y"]) - half)
			candidates.append(float(list[i]["y"]) + half)
	candidates.sort_custom(func(a: float, b: float) -> bool: return absf(a - hy) < absf(b - hy))
	for y in candidates:
		if absf(y) <= d_radius and _illegal_reason(list, hx, y, cue_at).is_empty():
			return {"x": hx, "y": y}
	return {}


static func _nearest_in_d(list: Array[Dictionary], hx: float, hy: float, cue_at: int) -> Dictionary:
	var dims := OfsTable.dimensions()
	var baulk_x := float(dims["baulk_line_x"])
	var d_radius := float(dims["d_radius"])
	var step := 0.005
	var best := {}
	var best_d2 := INF
	var steps := int(d_radius / step)
	for i in range(0, steps + 1):
		for j in range(-steps, steps + 1):
			var x := baulk_x - i * step
			var y := j * step
			if (x - baulk_x) * (x - baulk_x) + y * y > d_radius * d_radius:
				continue
			var d2 := (x - hx) * (x - hx) + (y - hy) * (y - hy)
			if d2 < best_d2 and _illegal_reason(list, x, y, cue_at).is_empty():
				best = {"x": x, "y": y}
				best_d2 = d2
	return best


func _free_red_index() -> int:
	var used := {}
	for b in balls:
		if b["ball"] == "red":
			used[int(b["index"])] = true
	for i in RED_COUNT:
		if not used.has(i):
			return i
	return -1


func _refuse_edit(action: String, ball: String, index: int, from: Array, to: Array, reason: String) -> bool:
	var data := {"action": action, "ball": ball, "index": index, "legal": false, "reason": reason}
	if not from.is_empty():
		data["from"] = from
	if not to.is_empty():
		data["to"] = to
	Log.warn("freetable", "freetable.edit", data)
	return false


func _log_edit(action: String, ball: String, index: int, from: Array, to: Array) -> void:
	var data := {"action": action, "ball": ball, "index": index, "legal": true}
	if not from.is_empty():
		data["from"] = from
	if not to.is_empty():
		data["to"] = to
	Log.info("freetable", "freetable.edit", data)


func _log_action(action: String, detail: Dictionary, shot_id: String = "") -> void:
	var data := {"action": action, "history_index": _index, "history_size": _history.size()}
	if not shot_id.is_empty():
		data["shot_id"] = shot_id
	if not detail.is_empty():
		data["detail"] = detail
	Log.info("freetable", "freetable.action", data)


static func _ball_label(b: Dictionary) -> String:
	return "red %d" % int(b["index"]) if b["ball"] == "red" else String(b["ball"])


static func _cue_ball_index(list: Array[Dictionary]) -> int:
	for i in list.size():
		if list[i]["ball"] == "cue":
			return i
	return -1


static func _opening_layout() -> Array[Dictionary]:
	var layout: Array[Dictionary] = []
	for entry: Variant in OfsTable.opening_layout(OPENING_GAP):
		layout.append(entry)
	return layout


## Ball lists as {ball, index, x, y}, with x and y as the same doubles.
static func _copy_balls(list: Array) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for entry: Variant in list:
		var b: Dictionary = entry
		out.append({"ball": String(b["ball"]), "index": int(b["index"]), "x": float(b["x"]), "y": float(b["y"])})
	return out
