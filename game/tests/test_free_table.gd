extends TestCase
## FreeTable: setups, shot history (undo, redo, retake with exact doubles and snapshots), edit
## sessions, legality and the add and remove rules.

const R := 0.02625
const SNAP_A := {"aim_rad": 0.123456789012345678, "tip_x": -0.3, "tip_y": 0.25, "elevation_rad": 0.0872664625997165,
		"speed_m_s": 2.718281828459045, "power": 0.4, "strike_mode": "power_bar"}
const SNAP_B := {"aim_rad": -2.9, "tip_x": 0.1, "tip_y": -0.45, "elevation_rad": 0.0, "speed_m_s": 1.1,
		"power": 0.0, "strike_mode": "stroke"}

var _log_start := 0
var _states: Array = []
var _histories: Array = []


func test_full_rack_setup() -> void:
	var table := _table()
	_mark_log()
	check(table.load_setup("full_rack", 7), "full rack loads")
	check_eq(table.balls.size(), 22, "22 balls")
	check_eq(table.cue_ball, 0, "cue ball first")
	if not table.balls.is_empty():
		check_eq(table.balls[0]["ball"], "cue", "the first ball is the cue ball")
	var reds := 0
	for b in table.balls:
		reds += 1 if b["ball"] == "red" else 0
	check_eq(reds, 15, "15 reds")
	check_eq(_states.size(), 1, "state_changed once")
	check_eq(table.history_size(), 1, "one history state")
	check(not table.can_undo() and not table.can_redo() and not table.can_retake(), "nothing to undo")
	var actions := _logged("freetable.action")
	check_eq(actions.size(), 1, "freetable.action once")
	if not actions.is_empty():
		check_eq(actions[0]["action"], "setup", "action")
		check_eq(actions[0]["detail"]["setup"], "full_rack", "detail.setup")
		check_eq(actions[0]["detail"]["seed"], "7", "detail.seed")
		# Physics.opening_rack is empty until the binding is configured; the fallback is logged.
		if actions[0]["detail"]["source"] == "opening_layout":
			check_eq(_logged("app.error").size(), 1, "the fallback logs app.error")
	table.free()


func test_empty_setup_is_the_cue_ball_on_its_opening_spot() -> void:
	var table := _table()
	check(table.load_setup("empty"), "empty loads")
	var home: Dictionary = OfsTable.opening_layout(0.0005)[0]
	check_eq(table.balls.size(), 1, "one ball")
	check_eq(table.balls[0]["ball"], "cue", "the cue ball")
	check_eq(table.balls[0]["x"], home["x"], "x")
	check_eq(table.balls[0]["y"], home["y"], "y")
	check(not table.load_setup("triangle"), "an unknown setup is refused")
	check_eq(table.history_size(), 1, "and changes nothing")
	table.free()


func test_setup_is_undoable() -> void:
	var table := _table()
	table.load_setup("full_rack", 1)
	table.load_setup("empty")
	check(table.can_undo(), "a setup can be undone")
	check_eq(table.undo(), {}, "no shot setup to restore")
	check_eq(table.balls.size(), 22, "the rack is back")
	table.free()


func test_shot_undo_redo_retake_restore_exact_doubles() -> void:
	var table := _table()
	var start := _two_balls(0.111111111111111111, 0.333333333333333333, 0.7071067811865476, -0.1234567890123456)
	table.set_balls(start, 0)
	var after_a := _final(start, [[0.5000000000000001, 0.2499999999999999, -1], [0.81234567890123456, 0.4, -1]])
	table.shot_started(SNAP_A, "s-1")
	table.shot_finished(after_a)
	var state_1: Array[Dictionary] = table.balls.duplicate(true)
	var after_b := _final(state_1, [[-0.9876543210987654, 0.0000000000000123, -1], [1.2, -0.6, -1]])
	table.shot_started(SNAP_B, "s-2")
	table.shot_finished(after_b)
	check_eq(table.history_index(), 2, "two shots")
	check(table.can_retake(), "retake after a shot")
	check(not table.can_redo(), "nothing to redo")

	_mark_log()
	var snap := table.undo()
	check_eq(snap, SNAP_B, "undo gives shot B's setup again")
	check(_same_balls(table.balls, state_1), "undo restores the exact doubles of state 1")
	snap = table.undo()
	check_eq(snap, SNAP_A, "undo again gives shot A's setup")
	check(_same_balls(table.balls, start), "start restored exactly")
	check(not table.can_undo(), "at the start")
	check_eq(table.undo(), {}, "nothing more to undo")

	snap = table.redo()
	check_eq(snap, SNAP_B, "redo to state 1 sets up shot B, played next from there")
	check(_same_balls(table.balls, state_1), "redo restores state 1 exactly")
	snap = table.redo()
	check_eq(snap, {}, "no shot after the last state")
	check_eq(table.balls[1]["x"], 1.2, "state 2")
	check_eq(table.redo(), {}, "nothing more to redo")

	snap = table.retake()
	check_eq(snap, SNAP_B, "retake gives shot B's setup")
	check(_same_balls(table.balls, state_1), "retake restores the pre-shot table exactly")
	check(table.can_redo(), "retake can be redone, like undo")
	var actions := _logged("freetable.action")
	var names: Array[String] = []
	for a in actions:
		names.append(String(a["action"]))
	check_eq(names, ["undo", "undo", "undo", "redo", "redo", "redo", "retake"] as Array[String], "actions logged")
	if actions.size() == 7:
		check_eq(actions[0]["shot_id"], "s-2", "undo names the shot undone")
		check_eq(int(actions[0]["history_index"]), 1, "history_index")
		check_eq(int(actions[0]["history_size"]), 3, "history_size")
		check(actions[2]["detail"].has("refused"), "a refused undo says so")
	table.free()


func test_snapshot_is_copied() -> void:
	var table := _table()
	var start := _two_balls(0.0, 0.0, 0.5, 0.0)
	table.set_balls(start, 0)
	var snap := SNAP_A.duplicate(true)
	table.shot_started(snap, "s")
	snap["aim_rad"] = 9.0
	table.shot_finished(_final(start, [[0.1, 0.1, -1], [0.6, 0.0, -1]]))
	var back := table.retake()
	check_eq(back, SNAP_A, "history keeps the snapshot as struck")
	back["tip_x"] = 5.0
	table.redo()
	check_eq(table.retake(), SNAP_A, "a returned snapshot can't change history")
	table.free()


func test_new_shot_clears_redo() -> void:
	var table := _table()
	var start := _two_balls(0.0, 0.0, 0.5, 0.0)
	table.set_balls(start, 0)
	table.shot_started(SNAP_A, "a")
	table.shot_finished(_final(start, [[0.1, 0.0, -1], [0.6, 0.0, -1]]))
	table.undo()
	check(table.can_redo(), "redo available")
	table.shot_started(SNAP_B, "b")
	table.shot_finished(_final(start, [[0.2, 0.0, -1], [0.7, 0.0, -1]]))
	check(not table.can_redo(), "a new shot drops the redo state")
	check_eq(table.history_size(), 2, "start and the new shot")
	check_eq(table.retake(), SNAP_B, "retake is the new shot")
	table.free()


func test_potted_balls_leave_and_the_cue_ball_returns() -> void:
	var table := _table()
	var start := _two_balls(0.0, 0.0, 0.5, 0.2)
	start.append({"ball": "black", "index": 0, "x": 1.4625, "y": 0.0})
	table.set_balls(start, 0)
	_mark_log()
	table.shot_started(SNAP_A, "pot")
	table.shot_finished(_final(start, [[1.8, 0.9, 2], [0.3, 0.3, -1], [1.8, -0.9, 3]]))
	check_eq(table.balls.size(), 2, "the black left the table, the cue ball came back")
	check_eq(table.cue_ball, 0, "cue ball index")
	var home: Dictionary = OfsTable.opening_layout(0.0005)[0]
	check_eq(table.balls[0]["x"], home["x"], "back on the opening cue position x")
	check_eq(table.balls[0]["y"], home["y"], "back on the opening cue position y")
	var actions := _logged("freetable.action")
	if not actions.is_empty():
		var detail: Dictionary = actions[0]["detail"]
		check_eq(detail["potted"], ["cue", "black"], "potted named")
		check(String(detail.get("cue_ball", "")).contains("opening cue position"), "detail says where it went: " + str(detail))
	table.free()


func test_potted_cue_ball_finds_the_nearest_legal_spot_on_the_baulk_line() -> void:
	var table := _table()
	var home: Dictionary = OfsTable.opening_layout(0.0005)[0]
	var hx := float(home["x"])
	var hy := float(home["y"])
	# A red on the opening cue position, offset slightly towards the yellow side.
	var start: Array[Dictionary] = [{"ball": "cue", "index": 0, "x": 1.0, "y": 0.0},
			{"ball": "red", "index": 0, "x": hx, "y": hy - 0.01}]
	table.set_balls(start, 0)
	table.shot_started(SNAP_A, "in-off")
	table.shot_finished(_final(start, [[1.8, 0.9, 2], [hx, hy - 0.01, -1]]))
	var cue := table.balls[table.cue_ball]
	check_near(float(cue["x"]), hx, 1e-12, "on the baulk line")
	check_near(float(cue["y"]), hy - 0.01 + 2.0 * R, 1e-5, "just clear of the red, on the nearer side")
	var dx := float(cue["x"]) - hx
	var dy := float(cue["y"]) - (hy - 0.01)
	check(sqrt(dx * dx + dy * dy) >= 2.0 * R + 1e-6, "legal clearance")
	check(table.is_legal(Vector2(cue["x"], cue["y"]), table.cue_ball), "legal by is_legal")
	table.free()


func test_edit_session_pushes_once_then_edits_in_place() -> void:
	var table := _table()
	table.set_balls(_two_balls(0.0, 0.0, 0.5, 0.0), 0)
	check(not table.move_ball(1, Vector2(0.6, 0.1)), "no edits outside edit mode")
	table.set_edit_mode(true)
	check(table.edit_mode, "edit mode on")
	check(table.move_ball(1, Vector2(0.6, 0.1)), "move a red")
	check_eq(table.history_size(), 2, "the first edit pushed a state")
	check(table.move_ball(0, Vector2(-1.0, 0.2)), "move the cue ball anywhere legal")
	check(table.add_ball("pink", Vector2(0.9, 0.0)), "add the pink")
	check_eq(table.history_size(), 2, "later edits change it in place")
	table.set_edit_mode(false)
	table.set_edit_mode(true)
	check(table.move_ball(2, Vector2(0.95, 0.0)), "a new session")
	check_eq(table.history_size(), 3, "a new session's first edit pushes again")
	table.undo()
	check_eq(table.balls.size(), 3, "undo goes back to the end of the first session")
	check_near(float(table.balls[2]["x"]), 0.9, 1e-6, "pink where the first session left it")
	check(table.move_ball(2, Vector2(1.0, 0.0)), "an edit after undo")
	check_eq(table.history_size(), 3, "pushes a new state rather than changing the undone-to one")
	check(not table.can_redo(), "and drops the redo state")
	table.undo()
	check_near(float(table.balls[2]["x"]), 0.9, 1e-6, "the first session's state is untouched")
	table.free()


func test_legality() -> void:
	var table := _table()
	var dims := OfsTable.dimensions()
	var half_l := float(dims["playing_length"]) / 2.0
	var half_w := float(dims["playing_width"]) / 2.0
	table.set_balls(_two_balls(0.0, 0.0, 0.5, 0.0), 0)
	check(table.is_legal(Vector2(1.0, 0.5)), "open cloth")
	check(not table.is_legal(Vector2(half_l - R + 0.001, 0.0)), "past the top cushion nose")
	check(not table.is_legal(Vector2(0.0, -(half_w - R + 0.001))), "past a side cushion nose")
	check(table.is_legal(Vector2(half_l - R - 0.0001, half_w - R - 0.0001)), "inside the corner")
	check(not table.is_legal(Vector2(0.5 + 2.0 * R - 0.0001, 0.0)), "overlapping the red")
	check(table.is_legal(Vector2(0.5 + 2.0 * R + 0.0001, 0.0)), "just clear of the red")
	check(table.is_legal(Vector2(0.5, 0.0), 1), "a ball doesn't block itself")
	check(not table.is_legal(Vector2(0.5, 0.0), 0), "but blocks every other ball")
	table.set_edit_mode(true)
	_mark_log()
	check(not table.move_ball(1, Vector2(0.01, 0.0)), "can't move onto the cue ball")
	check(not table.move_ball(1, Vector2(2.0, 0.0)), "can't move off the table")
	check_eq(table.history_size(), 1, "refused edits push nothing")
	var edits := _logged_lines("freetable.edit")
	check_eq(edits.size(), 2, "both refusals logged")
	for line in edits:
		check_eq(line["level"], "warn", "at warn")
		check_eq(line["data"]["legal"], false, "legal false")
		check_eq(line["data"]["ball"], "red", "ball")
	if edits.size() == 2:
		check_eq(edits[0]["data"]["reason"], "overlaps cue", "overlap reason")
		check_eq(edits[1]["data"]["reason"], "outside the cushions", "cushion reason")
		check_eq(edits[0]["data"]["from"], [0.5, 0.0], "from")
	table.free()


func test_add_and_remove_rules() -> void:
	var table := _table()
	table.load_setup("full_rack", 3)
	table.set_edit_mode(true)
	check(not table.add_ball("red", Vector2(-1.0, 0.5)), "all 15 reds are on")
	check(not table.add_ball("blue", Vector2(-1.0, 0.5)), "the blue is on")
	check(not table.add_ball("cue", Vector2(-1.0, 0.5)), "never a second cue ball")
	check(not table.add_ball("purple", Vector2(-1.0, 0.5)), "unknown ball")
	check(not table.remove_ball(table.cue_ball), "the cue ball can't be removed")
	check(not table.remove_ball(99), "no such ball")
	var red_4 := _find(table, "red", 4)
	check(table.remove_ball(red_4), "remove red 4")
	var red_9 := _find(table, "red", 9)
	check(table.remove_ball(red_9), "remove red 9")
	var blue := _find(table, "blue", 0)
	check(table.remove_ball(blue), "remove the blue")
	check_eq(table.balls.size(), 19, "three gone")
	_mark_log()
	check(table.add_ball("blue", Vector2(-1.2, 0.0)), "the blue again")
	check(not table.add_ball("red", Vector2(-1.2, 0.0)), "not on top of the blue")
	check(table.add_ball("red", Vector2(-1.0, 0.5)), "a red")
	check_eq(table.balls[table.balls.size() - 1]["index"], 4, "takes the lowest free index")
	check(table.add_ball("red", Vector2(-1.0, -0.5)), "another red")
	check_eq(table.balls[table.balls.size() - 1]["index"], 9, "then the next")
	check_eq(table.balls[table.cue_ball]["ball"], "cue", "cue_ball still points at the cue ball")
	var edits := _logged_lines("freetable.edit")
	check_eq(edits.size(), 4, "every add logged")
	if edits.size() == 4:
		check_eq(edits[0]["level"], "info", "accepted at info")
		check_eq(edits[0]["data"]["action"], "add", "action")
		check_eq(edits[1]["level"], "warn", "refused at warn")
		check_eq(edits[1]["data"]["reason"], "overlaps blue", "reason")
		check_eq(int(edits[2]["data"]["index"]), 4, "index")
	table.free()


func test_remove_before_the_cue_ball_keeps_cue_ball_pointing_at_it() -> void:
	var table := _table()
	var list: Array[Dictionary] = [{"ball": "red", "index": 0, "x": 0.5, "y": 0.0},
			{"ball": "cue", "index": 0, "x": 0.0, "y": 0.0}]
	table.set_balls(list, 1)
	table.set_edit_mode(true)
	check(table.remove_ball(0), "remove the red")
	check_eq(table.cue_ball, 0, "cue ball index follows")
	check_eq(table.balls[0]["ball"], "cue", "still the cue ball")
	table.free()


func test_set_balls_resets_history_and_refuses_malformed_lists() -> void:
	var table := _table()
	table.load_setup("full_rack", 2)
	table.load_setup("empty")
	check(table.set_balls(_two_balls(0.0, 0.0, 0.5, 0.0), 0), "a good list")
	check_eq(table.history_size(), 1, "history reset to that one state")
	check(not table.can_undo(), "nothing to undo")
	var no_cue: Array[Dictionary] = [{"ball": "red", "index": 0, "x": 0.0, "y": 0.0}]
	check(not table.set_balls(no_cue, 0), "no cue ball")
	var wrong_index := _two_balls(0.0, 0.0, 0.5, 0.0)
	check(not table.set_balls(wrong_index, 1), "cue_ball not the cue ball")
	var twin_reds := _two_balls(0.0, 0.0, 0.5, 0.0)
	twin_reds.append({"ball": "red", "index": 0, "x": 0.8, "y": 0.0})
	check(not table.set_balls(twin_reds, 0), "two red 0s")
	var nan_ball := _two_balls(0.0, 0.0, NAN, 0.0)
	check(not table.set_balls(nan_ball, 0), "non-finite position")
	check_eq(table.balls.size(), 2, "the table is unchanged")
	table.free()


func test_signals() -> void:
	var table := _table()
	var start := _two_balls(0.0, 0.0, 0.5, 0.0)
	table.set_balls(start, 0)
	table.shot_started(SNAP_A, "x")
	table.shot_finished(_final(start, [[0.1, 0.0, -1], [0.6, 0.0, -1]]))
	table.undo()
	var reasons: Array[StringName] = []
	for s: Array in _states:
		reasons.append(s[2])
	check_eq(reasons, [&"set_balls", &"shot", &"undo"] as Array[StringName], "state_changed reasons")
	check_eq(_histories[_histories.size() - 1], [false, true, false], "after undo: redo only")
	check_eq(_histories[1], [true, false, true], "after the shot: undo and retake")
	var modes: Array = []
	table.edit_mode_changed.connect(func(on: bool) -> void: modes.append(on))
	table.set_edit_mode(true)
	table.set_edit_mode(true)
	table.set_edit_mode(false)
	check_eq(modes, [true, false], "edit_mode_changed only on a change")
	table.free()


func _table() -> FreeTable:
	_states.clear()
	_histories.clear()
	var table := FreeTable.new()
	table.state_changed.connect(func(b: Array[Dictionary], cue: int, reason: StringName) -> void:
		_states.append([b, cue, reason]))
	table.history_changed.connect(func(u: bool, r: bool, t: bool) -> void: _histories.append([u, r, t]))
	return table


func _two_balls(cx: float, cy: float, rx: float, ry: float) -> Array[Dictionary]:
	return [{"ball": "cue", "index": 0, "x": cx, "y": cy}, {"ball": "red", "index": 0, "x": rx, "y": ry}]


## final_balls() for `list` with each ball's [x, y, pocket].
func _final(list: Array[Dictionary], ends: Array) -> Array:
	var out: Array = []
	for i in list.size():
		var b := list[i].duplicate()
		b["x"] = ends[i][0]
		b["y"] = ends[i][1]
		b["pocket"] = ends[i][2]
		out.append(b)
	return out


func _same_balls(a: Array[Dictionary], b: Array[Dictionary]) -> bool:
	if a.size() != b.size():
		return false
	for i in a.size():
		if a[i]["ball"] != b[i]["ball"] or a[i]["index"] != b[i]["index"]:
			return false
		# Exact equality: the doubles must come back bit for bit.
		if float(a[i]["x"]) != float(b[i]["x"]) or float(a[i]["y"]) != float(b[i]["y"]):
			return false
	return true


func _find(table: FreeTable, ball: String, index: int) -> int:
	for i in table.balls.size():
		if table.balls[i]["ball"] == ball and int(table.balls[i]["index"]) == index:
			return i
	return -1


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


func _logged(event: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for line in _logged_lines(event):
		found.append(line.get("data", {}))
	return found


## The lines of `event` logged since _mark_log, read from the session file.
func _logged_lines(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append(parsed)
	return found
