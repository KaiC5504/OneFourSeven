extends TestCase
## NextTarget (spec 8.5, the return): the easiest clear pot by ghost-ball geometry, the preference
## and its fallback, the nearest ball when nothing is clear, refusals of bad input, and its time.

const R := 0.02625
const HALF_LENGTH := 1.7845
const HALF_WIDTH := 0.889
## Each component of the top-left pocket's axis, (DIAG, DIAG) from the table into the pocket. Not a
## Vector2, which is single precision.
const DIAG := 0.70710678118654752

var _log_start := 0


func test_black_off_its_spot_is_a_straight_pot_into_the_top_left() -> void:
	# The black's spot is (1.4605, 0). The top-left target starts at the drop circle's near edge,
	# (1.815613, 0.920113) - 0.042 * (DIAG, DIAG) = (1.785914, 0.890414). The line to it is 24.9 degrees
	# off the pocket's axis (sine 0.4214), so the target moves 0.025 * 0.4214 = 10.5 mm towards the
	# left cushion's jaw, to (1.778464, 0.897864): u = (0.333820, 0.942637). With the cue ball 0.5 m
	# straight behind the black on that line the pot is straight: cut 0, aim atan2(u) = 1.230443.
	var black := [1.4605, 0.0]
	var target := [1.7784644346106204, 0.89786399251412596]
	var u := _unit(float(target[0]) - float(black[0]), float(target[1]) - float(black[1]))
	var cue := [float(black[0]) - 0.5 * u[0], float(black[1]) - 0.5 * u[1]]
	_mark_log()
	var balls := _balls([["cue", cue], ["black", black]])
	var result := NextTarget.choose(balls, 0, "colour")
	check_eq(result.get("index"), 1, "the black")
	check_eq(result.get("ball"), "black", "label")
	check_eq(result.get("pocket"), _pocket("top_left"), "top left")
	check_near(float(result.get("cut_deg", NAN)), 0.0, 1e-6, "straight")
	check_near(float(result.get("aim_rad", NAN)), 1.2304431495750543, 1e-9, "along the line")
	check_near(float(result.get("aim_rad", NAN)), atan2(u[1], u[0]), 1e-12, "the line's own angle")
	check_eq(result.get("reason"), "", "no reason")
	check(float(result.get("score", 0.0)) > 0.0, "a score")
	check_eq(_logged("app.error").size(), 0, "nothing logged")
	# Struck plain ball along that aim, the simulator drops it there too.
	var record: OfsShotRecord = Physics.predict(balls, 0, {"aim_rad": float(result.get("aim_rad", 0.0)), "tip_x": 0.0,
			"tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.5})
	check(record != null, "predicted")
	if record != null:
		check_eq(int(record.final_balls()[1].get("pocket", -1)), _pocket("top_left"), "potted in the top left")


func test_thirty_degree_cut_aims_at_the_ghost_ball() -> void:
	# A red 0.4 m out from the top-left corner on the pocket's axis: no shift, so the target is
	# the drop circle's near edge T, the object ball runs along the axis and the ghost ball is
	# G = O - 2R * (DIAG, DIAG). The cue ball sits 0.6 m back from G along 45 +- 30 degrees, so the
	# aim is atan2(G - C), 75 or 15 degrees, and the cut 30.
	var pocket: Dictionary = Physics.table_shape()["pockets"][_pocket("top_left")]
	var drop: Array = pocket["drop_centre"]
	var t := [float(drop[0]) - DIAG * float(pocket["drop_radius"]), float(drop[1]) - DIAG * float(pocket["drop_radius"])]
	var o := [HALF_LENGTH - 0.4 * DIAG, HALF_WIDTH - 0.4 * DIAG]
	var u := _unit(float(t[0]) - float(o[0]), float(t[1]) - float(o[1]))
	var g := [float(o[0]) - 2.0 * R * u[0], float(o[1]) - 2.0 * R * u[1]]
	for side: float in [1.0, -1.0]:
		var line := deg_to_rad(45.0 + 30.0 * side)
		var cue := [float(g[0]) - 0.6 * cos(line), float(g[1]) - 0.6 * sin(line)]
		var result := NextTarget.choose(_balls([["cue", cue], ["red", o]]), 0, "red")
		check_eq(result.get("pocket"), _pocket("top_left"), "top left, side %d" % side)
		check_near(float(result.get("aim_rad", NAN)), atan2(float(g[1]) - float(cue[1]), float(g[0]) - float(cue[0])), 1e-9,
				"aim at the ghost ball, side %d" % side)
		check_near(float(result.get("aim_rad", NAN)), line, 1e-9, "45 + 30 side %d" % side)
		check_near(float(result.get("cut_deg", NAN)), 30.0, 1e-9, "cut, side %d" % side)


func test_a_ball_on_a_path_sends_it_to_another_pot() -> void:
	# A red at (0.65, -0.04) with the cue ball at (1.15, 0.34): an 18.4 degree cut into the right
	# middle beats a 19.8 degree cut into the bottom right, whose object ball runs 1.5 m further.
	var open := [["cue", [1.15, 0.34]], ["red", [0.65, -0.04]]]
	var result := NextTarget.choose(_balls(open), 0, "red")
	check_eq(result.get("pocket"), _pocket("middle_right"), "unblocked: the middle")
	check_near(float(result.get("cut_deg", NAN)), 18.393, 1e-3, "middle cut")
	# A blue halfway along the red's line to the middle (target (-0.0116, -0.9545)).
	var object_blocked := NextTarget.choose(_balls(open + [["blue", [0.3192, -0.4973]]]), 0, "red")
	check_eq(object_blocked.get("pocket"), _pocket("bottom_right"), "object path blocked: the bottom right")
	check_near(float(object_blocked.get("cut_deg", NAN)), 19.832, 1e-3, "bottom right cut")
	# A blue 1.5R off the cue ball's line to the middle's ghost ball, 3R short of it, on the side
	# away from the bottom right's ghost ball: it blocks the one path but not the other.
	var cue_blocked := NextTarget.choose(_balls(open + [["blue", [0.7217, 0.0805]]]), 0, "red")
	check_eq(cue_blocked.get("pocket"), _pocket("bottom_right"), "cue path blocked: the bottom right")
	check_eq(cue_blocked.get("index"), 1, "still the red")


func test_prefer_colour_takes_a_colour_over_an_easier_red() -> void:
	# A red 0.3 m out on the top-left axis with the cue ball 0.4 m straight behind it (cut 0), and a
	# pink at (1.2, -0.2) that goes in the top right at a 49.7 degree cut.
	var red := [HALF_LENGTH - 0.3 * DIAG, HALF_WIDTH - 0.3 * DIAG]
	var cue := [float(red[0]) - (0.4 + 2.0 * R) * DIAG, float(red[1]) - (0.4 + 2.0 * R) * DIAG]
	var balls := _balls([["cue", cue], ["red", red], ["pink", [1.2, -0.2]]])
	var colour := NextTarget.choose(balls, 0, "colour")
	check_eq(colour.get("ball"), "pink", "colour: the pink")
	check_eq(colour.get("pocket"), _pocket("top_right"), "colour: top right")
	check_near(float(colour.get("cut_deg", NAN)), 49.669, 1e-3, "colour: cut")
	var any := NextTarget.choose(balls, 0, "any")
	check_eq(any.get("ball"), "red 0", "any: the easier red")
	check_eq(any.get("pocket"), _pocket("top_left"), "any: top left")
	check_near(float(any.get("cut_deg", NAN)), 0.0, 1e-6, "any: straight")
	check(float(any.get("score", 0.0)) > float(colour.get("score", 1.0)), "the red is easier")
	check_eq(NextTarget.choose(balls, 0, "red").get("ball"), "red 0", "red: the red")
	var no_reds := NextTarget.choose(_balls([["cue", cue], ["pink", [1.2, -0.2]]]), 0, "red")
	check_eq(no_reds.get("ball"), "pink", "no reds: falls back to any")
	check_eq(no_reds.get("pocket"), _pocket("top_right"), "no reds: the pink's pot")
	var no_colours := NextTarget.choose(_balls([["cue", cue], ["red", red]]), 0, "colour")
	check_eq(no_colours.get("ball"), "red 0", "no colours: falls back to any")


func test_equal_pots_take_the_higher_value() -> void:
	# Mirror images across the long axis: the pink into the top left and the black into the top
	# right are exactly as easy, and a player on a colour takes the black.
	var pink_first := NextTarget.choose(_balls([["cue", [0.5, 0.0]], ["pink", [1.2, 0.3]], ["black", [1.2, -0.3]]]), 0, "colour")
	check_eq(pink_first.get("ball"), "black", "black listed second")
	check_eq(pink_first.get("pocket"), _pocket("top_right"), "black's pocket")
	var black_first := NextTarget.choose(_balls([["cue", [0.5, 0.0]], ["black", [1.2, -0.3]], ["pink", [1.2, 0.3]]]), 0, "colour")
	check_eq(black_first.get("ball"), "black", "black listed first")


func test_no_clear_pot_aims_at_the_nearest_preferred_ball() -> void:
	# The cue ball, a blue and two reds in one line: the blue is within a ball's width of the cue
	# ball's path to every ghost ball. The nearer red, (1.0, 0.2), is listed second; the aim goes
	# straight at it, atan2(0.4, 2.0). The blue is nearer still but not preferred.
	var balls := _balls([["cue", [-1.0, -0.2]], ["blue", [0.0, 0.0]], ["red", [1.5, 0.3]], ["red", [1.0, 0.2]]])
	_mark_log()
	var result := NextTarget.choose(balls, 0, "red")
	check_eq(result.get("index"), 3, "the nearer red")
	check_eq(result.get("ball"), "red 1", "label")
	check_eq(result.get("pocket"), -1, "no pocket")
	check_eq(result.get("cut_deg"), 0.0, "no cut")
	check_eq(result.get("score"), 0.0, "no score")
	check_near(float(result.get("aim_rad", NAN)), atan2(0.4, 2.0), 1e-12, "straight at it")
	check_eq(result.get("reason"), "no clear pot", "reason")
	check_eq(_logged("app.error").size(), 0, "not an error")


func test_only_the_cue_ball_gives_nothing() -> void:
	_mark_log()
	check_eq(NextTarget.choose(_balls([["cue", [0.0, 0.0]]]), 0, "red"), {}, "no object ball")
	var potted := _balls([["cue", [0.0, 0.0]], ["black", [1.4605, 0.0]]])
	potted[1]["pocket"] = 4
	check_eq(NextTarget.choose(potted, 0, "any"), {}, "a potted ball is not on the table")
	check_eq(_logged("app.error").size(), 0, "not an error")


func test_a_cut_over_75_degrees_is_never_chosen() -> void:
	# A red 0.15 m out on the top-left axis, the cue ball 0.3 m back from its ghost ball along
	# 45 - cut degrees. At 80 degrees nothing else is clear either; at 70 the top left is taken.
	var o := [HALF_LENGTH - 0.15 * DIAG, HALF_WIDTH - 0.15 * DIAG]
	var g := [float(o[0]) - 2.0 * R * DIAG, float(o[1]) - 2.0 * R * DIAG]
	for cut: float in [80.0, 70.0]:
		var line := deg_to_rad(45.0 - cut)
		var cue := [float(g[0]) - 0.3 * cos(line), float(g[1]) - 0.3 * sin(line)]
		var result := NextTarget.choose(_balls([["cue", cue], ["red", o]]), 0, "red")
		if cut > 75.0:
			check(result.get("pocket") != _pocket("top_left"), "an 80 degree cut is not taken")
			check(int(result.get("pocket", -2)) < 0 or float(result.get("cut_deg", 90.0)) <= 75.0, "no cut over 75")
		else:
			check_eq(result.get("pocket"), _pocket("top_left"), "a 70 degree cut is taken")
			check_near(float(result.get("cut_deg", NAN)), 70.0, 1e-9, "70 degrees")


func test_along_the_cushion_into_a_middle_is_rejected() -> void:
	# A red 8.9 cm from the left cushion at x = -0.6 with the cue ball straight behind it on the line
	# to the left middle's mouth (0, 0.9545): 76 degrees off the pocket's axis. Some other pot wins.
	var mouth := [0.0, 0.9545]
	var along := [-0.6, 0.8]
	var u := _unit(float(mouth[0]) - float(along[0]), float(mouth[1]) - float(along[1]))
	var cue := [float(along[0]) - 0.4 * u[0], float(along[1]) - 0.4 * u[1]]
	var result := NextTarget.choose(_balls([["cue", cue], ["red", along]]), 0, "red")
	check(result.get("pocket") != _pocket("middle_left"), "not along the cushion into the middle")
	# 23 degrees off the axis, the same middle takes it.
	var square := [-0.15, 0.6]
	u = _unit(float(mouth[0]) - float(square[0]), float(mouth[1]) - float(square[1]))
	cue = [float(square[0]) - 0.4 * u[0], float(square[1]) - 0.4 * u[1]]
	result = NextTarget.choose(_balls([["cue", cue], ["red", square]]), 0, "red")
	check_eq(result.get("pocket"), _pocket("middle_left"), "a fuller angle into the middle")


func test_opening_break_position() -> void:
	# From the opening cue position every red is in the pack: no clear pot, so the aim goes at the
	# apex red. The blue off its spot into the top left (a 19.3 degree cut) is the easiest colour.
	var layout := FreeTable._opening_layout()
	check_eq(layout.size(), 22, "full layout")
	_mark_log()
	var red := NextTarget.choose(layout, 0, "red")
	check_eq(red.get("ball"), "red 0", "the apex red")
	check_eq(red.get("pocket"), -1, "nothing clear")
	check_eq(red.get("reason"), "no clear pot", "reason")
	check_near(float(red.get("aim_rad", NAN)), atan2(float(layout[1]["y"]) - float(layout[0]["y"]),
			float(layout[1]["x"]) - float(layout[0]["x"])), 1e-12, "straight at the apex")
	var colour := NextTarget.choose(layout, 0, "colour")
	check_eq(colour.get("ball"), "blue", "the blue")
	check_eq(colour.get("pocket"), _pocket("top_left"), "top left")
	check_near(float(colour.get("cut_deg", NAN)), 19.289, 1e-3, "cut")
	check(is_finite(float(colour.get("aim_rad", NAN))) and float(colour.get("score", 0.0)) > 0.0, "finite aim and a score")
	check_eq(_logged("app.error").size(), 0, "no errors")


func test_bad_input_returns_nothing_and_logs() -> void:
	var good := [["cue", [0.0, 0.0]], ["black", [1.4605, 0.0]]]
	var not_a_number := _balls(good)
	not_a_number[1]["x"] = NAN
	var cases := [
		["cue_ball out of range", _balls(good), 5, "red", Physics.table_shape()],
		["cue_ball not the cue ball", _balls(good), 1, "red", Physics.table_shape()],
		["NaN position", not_a_number, 0, "red", Physics.table_shape()],
		["unknown preference", _balls(good), 0, "pink", Physics.table_shape()],
		["no table shape", _balls(good), 0, "red", {}],
	]
	for case: Array in cases:
		_mark_log()
		var balls: Array[Dictionary] = case[1]
		check_eq(NextTarget.choose_on(case[4], OfsTable.dimensions(), balls, case[2], case[3]), {}, "%s: {}" % case[0])
		var lines := _logged_lines("app.error")
		check_eq(lines.size(), 1, "%s: one app.error" % case[0])
		if lines.size() == 1:
			check_eq(lines[0].get("level"), "warn", "%s: a warning" % case[0])
			var data: Dictionary = lines[0].get("data", {})
			check_eq(data.get("kind"), "message", "%s: kind" % case[0])
			check(String(data.get("message", "")).begins_with("NextTarget: "), "%s: says who" % case[0])
	check_eq(NextTarget.choose_on({}, {}, _balls(good), 0, "red"), {}, "no dimensions either")


func test_full_rack_takes_well_under_2_ms() -> void:
	# Reds preferred from the D is the slowest case: every candidate is checked and fails.
	var tables: Array = [Physics.opening_rack(20261115), _scattered(7)]
	var report := PackedStringArray()
	for t in tables.size():
		var balls: Array[Dictionary] = tables[t]
		check_eq(balls.size(), 22, "table %d has every ball" % t)
		for prefer: String in ["red", "colour", "any"]:
			var total := 0
			var worst := 0
			var runs := 50
			for run in runs:
				var start := Time.get_ticks_usec()
				var result := NextTarget.choose(balls, 0, prefer)
				var took := Time.get_ticks_usec() - start
				total += took
				worst = maxi(worst, took)
				if run == 0:
					check(not result.is_empty(), "table %d %s: a result" % [t, prefer])
			var mean := float(total) / runs
			report.append("%s %s mean %.0f us max %d us" % [["rack", "scattered"][t], prefer, mean, worst])
			check(mean < 2000.0, "table %d %s: mean %.0f us" % [t, prefer, mean])
	print("      next_target: " + "; ".join(report))


## 21 object balls and the cue ball at seeded places on the cloth, none touching.
func _scattered(seed: int) -> Array[Dictionary]:
	var rng := RandomNumberGenerator.new()
	rng.seed = seed
	var names: Array = ["cue"]
	for i in 15:
		names.append("red")
	names.append_array(FreeTable.COLOURS)
	var balls: Array[Dictionary] = []
	var reds := 0
	for name: String in names:
		while true:
			var x := rng.randf_range(-HALF_LENGTH + R, HALF_LENGTH - R)
			var y := rng.randf_range(-HALF_WIDTH + R, HALF_WIDTH - R)
			var clear := true
			for b in balls:
				if Vector2(x, y).distance_to(Vector2(float(b["x"]), float(b["y"]))) < 2.5 * R:
					clear = false
					break
			if clear:
				balls.append({"ball": name, "index": reds if name == "red" else 0, "x": x, "y": y})
				reds += 1 if name == "red" else 0
				break
	return balls


## [[name, [x, y]], ...] as FreeTable balls; reds are numbered in order.
func _balls(entries: Array) -> Array[Dictionary]:
	var balls: Array[Dictionary] = []
	var reds := 0
	for entry: Array in entries:
		var at: Array = entry[1]
		var name: String = entry[0]
		balls.append({"ball": name, "index": reds if name == "red" else 0, "x": float(at[0]), "y": float(at[1])})
		reds += 1 if name == "red" else 0
	return balls


func _unit(x: float, y: float) -> Array:
	var length := sqrt(x * x + y * y)
	return [x / length, y / length]


func _pocket(name: String) -> int:
	var pockets: Array = Physics.table_shape().get("pockets", [])
	for i in pockets.size():
		if pockets[i]["name"] == name:
			return i
	failures.append("no pocket named " + name)
	return -99


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
