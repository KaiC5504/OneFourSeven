extends TestCase
## ShotCycle.excellent() at the edges of its tests: the run, the cushion, the share and the plain
## stroke to beat.


class ScriptedPlayback extends ShotPlayback:
	var scripted_events: Array[Dictionary] = []
	var runs := {}
	var balls := 3

	func duration() -> float:
		return 5.0

	func ball_count() -> int:
		return balls

	func events() -> Array[Dictionary]:
		return scripted_events

	func state_at(t: float) -> PackedFloat64Array:
		var state := PackedFloat64Array()
		state.resize(balls * STRIDE)
		for i in balls:
			var run: Array = runs.get(i, [Vector2.ZERO, Vector2.ZERO, 0.0, 1.0])
			var f := clampf((t - float(run[2])) / (float(run[3]) - float(run[2])), 0.0, 1.0)
			var xy := (run[0] as Vector2).lerp(run[1], f)
			state[i * STRIDE + X] = xy.x
			state[i * STRIDE + Y] = xy.y
			state[i * STRIDE + POCKET] = -1.0
		return state


func _balls() -> Array[Dictionary]:
	return [{"ball": "cue", "index": 0, "x": -1.0, "y": 0.0}, {"ball": "red", "index": 3, "x": 1.0, "y": 0.3},
			{"ball": "black", "index": 0, "x": 1.4605, "y": 0.0}]


func _top_left() -> int:
	var pockets: Array = Physics.table_shape()["pockets"]
	for i in pockets.size():
		if pockets[i]["name"] == "top_left":
			return i
	return 0


## The red into the top left; the cue ball from (0.9, 0.3) at 0.4 s to `to` at 2.5 s, meeting
## a cushion at `cushion_t` (none when negative).
func _shot(to: Vector2, cushion_t: float) -> ScriptedPlayback:
	var pocket := _top_left()
	var c: Array = Physics.table_shape()["pockets"][pocket]["drop_centre"]
	var p := ScriptedPlayback.new()
	p.scripted_events = [{"t": 0.0, "kind": "strike", "a": 0, "b": -1, "body": -1},
			{"t": 0.4, "kind": "ball_ball", "a": 0, "b": 1, "body": -1},
			{"t": 1.6, "kind": "pocket", "a": 1, "b": -1, "body": pocket}]
	if cushion_t >= 0.0:
		p.scripted_events.append({"t": cushion_t, "kind": "ball_line", "a": 0, "b": -1, "body": 2})
		p.scripted_events.sort_custom(func(x: Dictionary, y: Dictionary) -> bool: return float(x["t"]) < float(y["t"]))
	p.runs[1] = [Vector2(1.0, 0.3), Vector2(float(c[0]), float(c[1])), 0.4, 1.6]
	p.runs[0] = [Vector2(0.9, 0.3), to, 0.4, 2.5]
	return p


func _reading(worth: float) -> Callable:
	return func(_balls: Array[Dictionary], _cue: int, _on: Dictionary) -> float: return worth


func test_excellent_at_the_edges() -> void:
	var most := ShotCycle.POSITION_CHANCE_CAP * (7.0 + NextTarget.BREAK_POINTS)
	var high := ShotCycle.POSITION_SHARE * most + 0.5
	var far := Vector2(-0.5, -0.4)
	check(ShotCycle.excellent(_shot(far, 1.0), _balls(), 0, 1, _reading(high)), "the plain case")
	# A cushion before the first ball is not one met on the run after it.
	check(not ShotCycle.excellent(_shot(far, 0.2), _balls(), 0, 1, _reading(high)),
			"a cushion only before the first ball")
	check(not ShotCycle.excellent(_shot(Vector2(0.9 - 0.98, 0.3), 1.0), _balls(), 0, 1, _reading(high)),
			"a run of 0.98 m")
	check(ShotCycle.excellent(_shot(Vector2(0.9 - 1.02, 0.3), 1.0), _balls(), 0, 1, _reading(high)),
			"a run of 1.02 m")
	check(ShotCycle.excellent(_shot(far, 1.0), _balls(), 0, 1, _reading(ShotCycle.POSITION_SHARE * most)),
			"a worth of exactly the share")
	check(not ShotCycle.excellent(_shot(far, 1.0), _balls(), 0, 1, _reading(ShotCycle.POSITION_SHARE * most - 1e-9)),
			"just under it")
	# The cue ball in a pocket leaves nothing to read.
	var in_off := _shot(far, 1.0)
	in_off.scripted_events.append({"t": 2.6, "kind": "pocket", "a": 0, "b": -1, "body": 0})
	check(not ShotCycle.excellent(in_off, _balls(), 0, 1, _reading(high)), "an in-off")


func test_the_plain_stroke_must_be_beaten() -> void:
	var most := ShotCycle.POSITION_CHANCE_CAP * (7.0 + NextTarget.BREAK_POINTS)
	var worth := ShotCycle.POSITION_SHARE * most + 2.0
	var far := Vector2(-0.5, -0.4)
	var asked: Array = []
	var plain := func(played: int) -> float:
		asked.append(played)
		return worth - ShotCycle.POSITION_BEAT * most
	check(ShotCycle.excellent(_shot(far, 1.0), _balls(), 0, 1, _reading(worth), {}, plain), "beaten by exactly the margin")
	check_eq(asked, [1], "the plain stroke asked about the ball played")
	var close := func(_played: int) -> float: return worth - ShotCycle.POSITION_BEAT * most + 1e-6
	check(not ShotCycle.excellent(_shot(far, 1.0), _balls(), 0, 1, _reading(worth), {}, close), "not quite beaten")
	var same := func(_played: int) -> float: return worth
	check(not ShotCycle.excellent(_shot(far, 1.0), _balls(), 0, 1, _reading(worth), {}, same),
			"a stroke no better than plain: a plain stroke itself")
	var unknown := func(_played: int) -> float: return -1.0
	check(ShotCycle.excellent(_shot(far, 1.0), _balls(), 0, 1, _reading(worth), {}, unknown), "nothing to beat when it can't be said")
	asked.clear()
	check(not ShotCycle.excellent(_shot(Vector2(0.5, 0.3), 1.0), _balls(), 0, 1, _reading(worth), {}, plain),
			"a short run fails first")
	check(asked.is_empty(), "and the plain stroke is not simulated for it")


func test_plain_worth_reads_the_same_pot_struck_with_centre_ball() -> void:
	var c: Array = Physics.table_shape()["pockets"][_top_left()]["drop_centre"]
	var pocket := Vector2(float(c[0]), float(c[1]))
	var red := Vector2(1.0, 0.45)
	var cue := Vector2(-1.0, 0.1)
	var r := float(OfsTable.dimensions()["ball_radius"])
	var balls: Array[Dictionary] = [{"ball": "cue", "index": 0, "x": cue.x, "y": cue.y},
			{"ball": "red", "index": 4, "x": red.x, "y": red.y}, {"ball": "blue", "index": 0, "x": 0.0, "y": -0.2}]
	var ghost := red - (pocket - red).normalized() * 2.0 * r
	var strike := {"aim_rad": (ghost - cue).angle(), "tip_x": 0.2, "tip_y": -0.4, "elevation_rad": 0.1, "speed_m_s": 1.5}
	var reads: Array = []
	var reading := func(left: Array[Dictionary], cue_ball: int, on: Dictionary) -> float:
		reads.append([left.duplicate(true), cue_ball, on])
		return 10.0
	check_eq(ShotCycle.plain_worth(Physics.predict, Physics.min_cue_elevation, balls, 0, strike, 1, reading), 10.0, "the reading's worth")
	var plain := strike.duplicate()
	plain["tip_x"] = 0.0
	plain["tip_y"] = 0.0
	plain["elevation_rad"] = maxf(0.0, Physics.min_cue_elevation(balls, 0, plain))
	var record := Physics.predict(balls, 0, plain)
	check(record != null and reads.size() == 1, "simulated and read once")
	if record != null and reads.size() == 1:
		var at: Dictionary = record.final_balls()[0]
		var read_cue: Dictionary = (reads[0][0] as Array)[int(reads[0][1])]
		check(Vector2(float(at["x"]), float(at["y"])).distance_to(Vector2(float(read_cue["x"]), float(read_cue["y"]))) < 1e-9,
				"where the centre-ball stroke leaves the cue ball: %s, %s" % [at, read_cue])
		check_eq(reads[0][2], {"kind": "any_colour"}, "with a colour on after the red")
	var refused := func(_b: Array[Dictionary], _c: int, _s: Dictionary) -> Variant: return null
	check_eq(ShotCycle.plain_worth(refused, Callable(), balls, 0, strike, 1, reading), -1.0, "nothing simulated: nothing to beat")
