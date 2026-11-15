extends TestCase
## NextTarget (spec 8.5, the return): the best clear pot by ghost-ball geometry (its chance against
## the aim error a player makes, weighed by the ball's worth), the preference and when the other
## kind is taken, the nearest ball when nothing is clear, refusals of bad input, and its time.
##
## Where a ball must go to drop is taken from the table shape, never from numbers written here: the
## expected line is the middle of the directions that Guides' straight-line geometry sends cleanly
## into the pocket (_clear_window).

const R := 0.02625
const HALF_LENGTH := 1.7845
const HALF_WIDTH := 0.889
## Each component of the top-left pocket's axis, (DIAG, DIAG) from the table into the pocket. Not a
## Vector2, which is single precision.
const DIAG := 0.70710678118654752

var _log_start := 0


func test_black_off_its_spot_is_a_straight_pot_into_the_top_left() -> void:
	# The black's spot is (1.4605, 0), some 24 degrees off the top-left pocket's axis: its line is
	# the middle of what goes in, nearer the left cushion's jaw than the line to the drop centre.
	# With the cue ball 0.5 m straight behind the black on that line the pot is straight.
	var black := [1.4605, 0.0]
	var window := _clear_window(black, _pocket("top_left"))
	check_eq(window.size(), 2, "the black can be potted in the top left")
	if window.size() != 2:
		return
	var line := 0.5 * (float(window[0]) + float(window[1]))
	var drop: Array = Physics.table_shape()["pockets"][_pocket("top_left")]["drop_centre"]
	check(line > atan2(float(drop[1]) - float(black[1]), float(drop[0]) - float(black[0])), "left of the line to the drop centre")
	var cue := [float(black[0]) - 0.5 * cos(line), float(black[1]) - 0.5 * sin(line)]
	_mark_log()
	var balls := _balls([["cue", cue], ["black", black]])
	var result := NextTarget.choose(balls, 0, "colour")
	check_eq(result.get("index"), 1, "the black")
	check_eq(result.get("ball"), "black", "label")
	check_eq(result.get("pocket"), _pocket("top_left"), "top left")
	check_near(float(result.get("cut_deg", NAN)), 0.0, 1e-6, "straight")
	check_near(float(result.get("aim_rad", NAN)), line, 1e-9, "along the middle of what goes in")
	check_eq(result.get("reason"), "", "no reason")
	check(float(result.get("score", 0.0)) > 0.0, "a score")
	check_eq(_logged("app.error").size(), 0, "nothing logged")
	# Struck plain ball along that aim, the simulator drops it there too.
	var record: OfsShotRecord = Physics.predict(balls, 0, {"aim_rad": float(result.get("aim_rad", 0.0)), "tip_x": 0.0,
			"tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.5})
	check(record != null, "predicted")
	if record != null:
		check_eq(int(record.final_balls()[1].get("pocket", -1)), _pocket("top_left"), "potted in the top left")


func test_the_line_into_a_pocket_is_the_middle_of_what_goes_in() -> void:
	# From either side of each kind of pocket and from near and far: the cue ball straight behind
	# the red on the middle of its clear directions is a straight pot along exactly that line.
	var cases := [
		["top_left", [1.2, 0.75]], ["top_left", [1.6, 0.3]], ["top_left", [0.0, 0.0]], ["bottom_right", [-1.0, -0.2]],
		["middle_left", [0.3, 0.5]], ["middle_left", [-0.5, 0.45]], ["middle_right", [0.1, 0.3]],
	]
	for case: Array in cases:
		var red: Array = case[1]
		var label := "%s from %s" % [case[0], red]
		var p := _pocket(case[0])
		var window := _clear_window(red, p)
		check_eq(window.size(), 2, label + ": can be potted")
		if window.size() != 2:
			continue
		var line := 0.5 * (float(window[0]) + float(window[1]))
		var cue := [float(red[0]) - 0.3 * cos(line), float(red[1]) - 0.3 * sin(line)]
		var result := NextTarget.choose(_balls([["cue", cue], ["red", red]]), 0, "red")
		check_eq(result.get("pocket"), p, label + ": that pocket")
		check_near(float(result.get("aim_rad", NAN)), line, 1e-9, label + ": the middle of what goes in")
		check_near(float(result.get("cut_deg", NAN)), 0.0, 1e-6, label + ": straight")


func test_no_pot_where_the_jaws_leave_no_way_in() -> void:
	# A red 3 mm off the left cushion, 0.5 m short of the top-left pocket: along the cushion the
	# near jaw's curve is in the way, and nothing goes cleanly in. The cue ball is straight behind
	# it along the cushion, so that pocket would otherwise be the easiest by far.
	var red := [HALF_LENGTH - 0.5, HALF_WIDTH - R - 0.003]
	check_eq(_clear_window(red, _pocket("top_left")).size(), 0, "nothing goes in")
	var result := NextTarget.choose(_balls([["cue", [float(red[0]) - 0.4, float(red[1])]], ["red", red]]), 0, "red")
	check(result.get("pocket") != _pocket("top_left"), "the top left is not offered")


func test_thirty_degree_cut_aims_at_the_ghost_ball() -> void:
	# A red 0.4 m out from the top-left corner on the pocket's axis, where the jaws are the same
	# either side: the object ball runs along the axis and the ghost ball is G = O - 2R * (DIAG, DIAG).
	# The cue ball sits 0.6 m back from G along 45 +- 30 degrees, so the aim is atan2(G - C), 75 or
	# 15 degrees, and the cut 30.
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
	# A red at (0.65, -0.04) with the cue ball at (1.15, 0.34): a cut of some 17 degrees into the
	# right middle beats one of some 20 into the bottom right, whose object ball runs 1.5 m further.
	var red := [0.65, -0.04]
	var cue := [1.15, 0.34]
	var open := [["cue", cue], ["red", red]]
	var middle := _pot(red, cue, _pocket("middle_right"))
	var corner := _pot(red, cue, _pocket("bottom_right"))
	check(not middle.is_empty() and not corner.is_empty(), "both pots are on")
	if middle.is_empty() or corner.is_empty():
		return
	check(float(middle["cut_deg"]) < float(corner["cut_deg"]), "the middle is the fuller ball")
	var result := NextTarget.choose(_balls(open), 0, "red")
	check_eq(result.get("pocket"), _pocket("middle_right"), "unblocked: the middle")
	check_near(float(result.get("cut_deg", NAN)), float(middle["cut_deg"]), 1e-6, "middle cut")
	check_near(float(result.get("aim_rad", NAN)), float(middle["aim_rad"]), 1e-9, "middle aim")
	# A blue 0.5 m along the red's line to the middle. The red is the only ball on, as in a match:
	# with no rules the blue itself would be the pot.
	var along: Array = middle["line"]
	var on_object_path := [float(red[0]) + 0.5 * float(along[0]), float(red[1]) + 0.5 * float(along[1])]
	var object_blocked := NextTarget.choose(_balls(open + [["blue", on_object_path]]), 0, "red", [1])
	check_eq(object_blocked.get("pocket"), _pocket("bottom_right"), "object path blocked: the bottom right")
	check_near(float(object_blocked.get("cut_deg", NAN)), float(corner["cut_deg"]), 1e-6, "bottom right cut")
	# A blue 1.5R off the cue ball's line to the middle's ghost ball, 3R short of it, on the side
	# away from the bottom right's ghost ball: it blocks the one path but not the other.
	var ghost: Array = middle["ghost"]
	var other: Array = corner["ghost"]
	var to_ghost := _unit(float(ghost[0]) - float(cue[0]), float(ghost[1]) - float(cue[1]))
	var side := [-float(to_ghost[1]), float(to_ghost[0])]
	if (float(other[0]) - float(ghost[0])) * float(side[0]) + (float(other[1]) - float(ghost[1])) * float(side[1]) > 0.0:
		side = [-float(side[0]), -float(side[1])]
	var on_cue_path := [float(ghost[0]) - 3.0 * R * float(to_ghost[0]) + 1.5 * R * float(side[0]),
			float(ghost[1]) - 3.0 * R * float(to_ghost[1]) + 1.5 * R * float(side[1])]
	var cue_blocked := NextTarget.choose(_balls(open + [["blue", on_cue_path]]), 0, "red", [1])
	check_eq(cue_blocked.get("pocket"), _pocket("bottom_right"), "cue path blocked: the bottom right")
	check_eq(cue_blocked.get("index"), 1, "still the red")


func test_prefer_colour_takes_a_colour_over_an_easier_red() -> void:
	# A red 0.3 m out on the top-left axis with the cue ball 0.4 m straight behind it (cut 0), and a
	# pink at (1.2, -0.2) that goes in the top right at a cut of some 50 degrees.
	var red := [HALF_LENGTH - 0.3 * DIAG, HALF_WIDTH - 0.3 * DIAG]
	var cue := [float(red[0]) - (0.4 + 2.0 * R) * DIAG, float(red[1]) - (0.4 + 2.0 * R) * DIAG]
	var balls := _balls([["cue", cue], ["red", red], ["pink", [1.2, -0.2]]])
	var pink := _pot([1.2, -0.2], cue, _pocket("top_right"))
	check(not pink.is_empty(), "the pink can be potted in the top right")
	var colour := NextTarget.choose(balls, 0, "colour")
	check_eq(colour.get("ball"), "pink", "colour: the pink")
	check_eq(colour.get("pocket"), _pocket("top_right"), "colour: top right")
	check_near(float(colour.get("cut_deg", NAN)), float(pink.get("cut_deg", NAN)), 1e-6, "colour: cut")
	check(float(pink.get("cut_deg", 0.0)) > 40.0 and float(pink.get("cut_deg", 90.0)) < 60.0, "a thin cut: %s" % pink.get("cut_deg"))
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


func test_among_keeps_the_choice_to_the_places_given() -> void:
	# The same table: an easy red on the top-left axis and a pink that cuts into the top right.
	var red := [HALF_LENGTH - 0.3 * DIAG, HALF_WIDTH - 0.3 * DIAG]
	var cue := [float(red[0]) - (0.4 + 2.0 * R) * DIAG, float(red[1]) - (0.4 + 2.0 * R) * DIAG]
	var balls := _balls([["cue", cue], ["red", red], ["pink", [1.2, -0.2]]])
	check_eq(NextTarget.choose(balls, 0, "any").get("ball"), "red 0", "with no places given, the easier red")
	check_eq(NextTarget.choose(balls, 0, "any", []).get("ball"), "red 0", "an empty list is no list")
	var pink := NextTarget.choose(balls, 0, "any", [2])
	check_eq(pink.get("ball"), "pink", "only the pink's place: the pink")
	check_eq(pink.get("pocket"), _pocket("top_right"), "and its pot")
	check_eq(NextTarget.choose(balls, 0, "any", [2.0]).get("ball"), "pink", "a place read from JSON is a float")
	check_eq(NextTarget.choose(balls, 0, "any", [2, 1]).get("ball"), "red 0", "both places: the easier again, in any order")
	check_eq(NextTarget.choose(balls, 0, "colour", [1, 2]).get("ball"), "pink", "the preference holds among them")
	check_eq(NextTarget.choose(balls, 0, "colour", [1]).get("ball"), "red 0", "and gives way when none of them is preferred")
	_mark_log()
	check_eq(NextTarget.choose(balls, 0, "any", [0, 7, -1, "red", null]).get("ball"), "red 0",
			"places with no object ball are passed over, and with none left it chooses as without them")
	check_eq(_logged("app.error").size(), 0, "without an error")
	# Nothing clear among the places: straight at the nearest of them.
	var blocked := _balls([["cue", [-1.0, -0.2]], ["blue", [0.0, 0.0]], ["red", [1.5, 0.3]], ["red", [1.0, 0.2]]])
	var nearest := NextTarget.choose(blocked, 0, "any", [2])
	check_eq([nearest.get("index"), nearest.get("reason")], [2, "no clear pot"], "the one place given, though another ball is nearer")


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
	# ball's path to every red's ghost ball. With the reds the balls on, the aim goes straight at the
	# nearer, (1.0, 0.2), listed second: atan2(0.4, 2.0). The blue is nearer still but not on.
	var balls := _balls([["cue", [-1.0, -0.2]], ["blue", [0.0, 0.0]], ["red", [1.5, 0.3]], ["red", [1.0, 0.2]]])
	_mark_log()
	var result := NextTarget.choose(balls, 0, "red", [2, 3])
	check_eq(result.get("index"), 3, "the nearer red")
	check_eq(result.get("ball"), "red 1", "label")
	check_eq(result.get("pocket"), -1, "no pocket")
	check_eq(result.get("cut_deg"), 0.0, "no cut")
	check_eq(result.get("score"), 0.0, "no score")
	check_near(float(result.get("aim_rad", NAN)), atan2(0.4, 2.0), 1e-12, "straight at it")
	check_eq(result.get("reason"), "no clear pot", "reason")
	check_eq(result.get("chance"), 0.0, "no chance")
	check_eq(_logged("app.error").size(), 0, "not an error")
	# With no rules the blue's pot is the pick: a pot of the other kind beats no pot at all.
	var free := NextTarget.choose(balls, 0, "red")
	check_eq([free.get("ball"), free.get("reason")], ["blue", ""], "no rules: the blue")
	check(int(free.get("pocket", -1)) >= 0 and float(free.get("chance", 0.0)) > 0.0, "with a pocket and a chance")
	# And with only colours no better off, straight at the nearest red again.
	var none := NextTarget.choose(_balls([["cue", [-1.0, -0.2]], ["red", [0.0, 0.0]], ["red", [1.5, 0.3]], ["red", [1.0, 0.2]]]),
			0, "colour", [2, 3])
	check_eq([none.get("index"), none.get("reason")], [3, "no clear pot"], "nothing clear at all")


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
	# The left middle's mouth is the point of its drop circle nearest the table. A red 0.4 m from
	# it with the cue ball straight behind on the red's own line: at 40 degrees off the pocket's
	# axis the middle takes it; at 60 the line is still clear but past MAX_MIDDLE_APPROACH_DEG, and
	# at 80, along the cushion, the near jaw leaves no way in. Some other pot wins those.
	var pocket: Dictionary = Physics.table_shape()["pockets"][_pocket("middle_left")]
	var mouth := [float(pocket["drop_centre"][0]), float(pocket["drop_centre"][1]) - float(pocket["drop_radius"])]
	for case: Array in [[40.0, true], [60.0, true], [80.0, false]]:
		var off_axis: float = case[0]
		var label := "%d degrees off the axis" % off_axis
		var red := [float(mouth[0]) - 0.4 * sin(deg_to_rad(off_axis)), float(mouth[1]) - 0.4 * cos(deg_to_rad(off_axis))]
		var window := _clear_window(red, _pocket("middle_left"))
		check_eq(window.size() == 2, case[1], label + ": whether anything goes in")
		var line := deg_to_rad(90.0 - off_axis) if window.is_empty() else 0.5 * (float(window[0]) + float(window[1]))
		var cue := [float(red[0]) - 0.4 * cos(line), float(red[1]) - 0.4 * sin(line)]
		var result := NextTarget.choose(_balls([["cue", cue], ["red", red]]), 0, "red")
		if off_axis < NextTarget.MAX_MIDDLE_APPROACH_DEG:
			check_eq(result.get("pocket"), _pocket("middle_left"), label + ": the middle takes it")
			check_near(float(result.get("cut_deg", NAN)), 0.0, 1e-6, label + ": straight")
		else:
			check(result.get("pocket") != _pocket("middle_left"), label + ": not into the middle")


func test_opening_break_position() -> void:
	# From the opening cue position every red is in the pack. With the reds the balls on there is no
	# clear pot, so the aim goes at the apex red. With no rules the brown beside the cue ball, into
	# the bottom left, is the pick whichever kind is preferred: a short cut beats the long blue.
	var layout := FreeTable._opening_layout()
	check_eq(layout.size(), 22, "full layout")
	var reds: Array = []
	for i in layout.size():
		if layout[i]["ball"] == "red":
			reds.append(i)
	_mark_log()
	var red := NextTarget.choose(layout, 0, "red", reds)
	check_eq(red.get("ball"), "red 0", "the apex red")
	check_eq(red.get("pocket"), -1, "nothing clear")
	check_eq(red.get("reason"), "no clear pot", "reason")
	check_near(float(red.get("aim_rad", NAN)), atan2(float(layout[1]["y"]) - float(layout[0]["y"]),
			float(layout[1]["x"]) - float(layout[0]["x"])), 1e-12, "straight at the apex")
	for prefer: String in ["red", "colour"]:
		var colour := NextTarget.choose(layout, 0, prefer)
		check_eq(colour.get("ball"), "brown", prefer + ": the brown")
		check_eq(colour.get("pocket"), _pocket("bottom_left"), prefer + ": bottom left")
		var brown := _pot([float(layout[colour.get("index", 0)]["x"]), float(layout[colour.get("index", 0)]["y"])],
				[float(layout[0]["x"]), float(layout[0]["y"])], _pocket("bottom_left"))
		check_near(float(colour.get("cut_deg", NAN)), float(brown.get("cut_deg", NAN)), 1e-6, prefer + ": cut")
		check(is_finite(float(colour.get("aim_rad", NAN))) and float(colour.get("score", 0.0)) > 0.0, prefer + ": finite aim and a score")
		check(float(colour.get("chance", 0.0)) > 0.5 and float(colour.get("chance", 1.0)) <= NextTarget.MAX_CHANCE, prefer + ": a fair chance")
	check_eq(_logged("app.error").size(), 0, "no errors")


func test_a_far_colour_gives_way_to_an_easy_red() -> void:
	# KaiC's sessions of 2026-11-15: after a red the pick was a colour however far, and he played
	# the red beside the cue ball instead. A blue on its spot with the cue ball 1.6 m off at a
	# 30 degree cut, and a red straight into the top left from 0.3 m.
	var red := [HALF_LENGTH - 0.3 * DIAG, HALF_WIDTH - 0.3 * DIAG]
	var cue := [float(red[0]) - (0.3 + 2.0 * R) * DIAG, float(red[1]) - (0.3 + 2.0 * R) * DIAG]
	var balls := _balls([["cue", cue], ["red", red], ["blue", [0.0, 0.0]]])
	var ranked := NextTarget.rank_on(Physics.table_shape(), OfsTable.dimensions(), balls, 0, "colour", [], 8)
	check(ranked.size() >= 2, "both are pots: %d" % ranked.size())
	check_eq(ranked[0].get("ball"), "red 0", "the red first, though a colour is preferred")
	var blue := {}
	for entry in ranked:
		if entry["ball"] == "blue" and blue.is_empty():
			blue = entry
	check(not blue.is_empty(), "the blue is ranked")
	if not blue.is_empty():
		check(float(blue["chance"]) < 0.6 * float(ranked[0]["chance"]), "the blue is the far harder pot: %s against %s"
				% [blue["chance"], ranked[0]["chance"]])
	for i in range(1, ranked.size()):
		check(float(ranked[i - 1]["score"]) >= float(ranked[i]["score"]), "best first")
	# The blue is the only ball on in a match: it is the pick however hard.
	check_eq(NextTarget.choose(balls, 0, "colour", [2]).get("ball"), "blue", "a match keeps to the ball on")
	# A colour as easy as the red is the pick whichever kind is preferred: mirror images across the
	# long axis, the red into the top left and the black into the top right.
	var level := _balls([["cue", [0.5, 0.0]], ["red", [1.2, 0.3]], ["black", [1.2, -0.3]]])
	check_eq(NextTarget.choose(level, 0, "colour").get("ball"), "black", "an easy colour after a red")
	check_eq(NextTarget.choose(level, 0, "red").get("ball"), "red 0", "and the red when a red is on")


func test_chance_falls_with_distance_cut_and_a_squeeze() -> void:
	# A red on the top-left axis 0.4 m out, the cue ball straight behind it.
	var o := [HALF_LENGTH - 0.4 * DIAG, HALF_WIDTH - 0.4 * DIAG]
	var chances: Array[float] = []
	for back: float in [0.3, 1.0, 2.0]:
		var cue := [float(o[0]) - (back + 2.0 * R) * DIAG, float(o[1]) - (back + 2.0 * R) * DIAG]
		chances.append(float(NextTarget.choose(_balls([["cue", cue], ["red", o]]), 0, "red").get("chance", 0.0)))
	check(chances[0] > chances[2] and chances[0] >= chances[1] and chances[1] >= chances[2], "further is harder: %s" % [chances])
	check(chances[0] <= NextTarget.MAX_CHANCE and chances[0] > 0.85, "a short straight pot is as good as it gets: %s" % chances[0])
	# The same distance at a cut.
	var g := [float(o[0]) - 2.0 * R * DIAG, float(o[1]) - 2.0 * R * DIAG]
	var cut_chances: Array[float] = []
	for cut: float in [0.0, 30.0, 60.0]:
		var line := deg_to_rad(45.0 - cut)
		var cue := [float(g[0]) - 1.0 * cos(line), float(g[1]) - 1.0 * sin(line)]
		var result := NextTarget.choose(_balls([["cue", cue], ["red", o]]), 0, "red")
		check_eq(result.get("pocket"), _pocket("top_left"), "%d degrees: the top left" % cut)
		cut_chances.append(float(result.get("chance", 0.0)))
	check(cut_chances[0] > cut_chances[1] and cut_chances[1] > cut_chances[2], "a thinner cut is harder: %s" % [cut_chances])
	# A ball just clear of the cue ball's path leaves less room than an open table.
	var cue := [float(o[0]) - (1.0 + 2.0 * R) * DIAG, float(o[1]) - (1.0 + 2.0 * R) * DIAG]
	var open := NextTarget.choose(_balls([["cue", cue], ["red", o]]), 0, "red", [1])
	var beside := [float(cue[0]) + 0.5 * DIAG + (2.0 * R + 0.0002) * DIAG, float(cue[1]) + 0.5 * DIAG - (2.0 * R + 0.0002) * DIAG]
	var squeezed := NextTarget.choose(_balls([["cue", cue], ["red", o], ["blue", beside]]), 0, "red", [1])
	check_eq(squeezed.get("pocket"), _pocket("top_left"), "the squeeze is still a pot")
	check(float(squeezed.get("margin_rad", 1.0)) < float(open.get("margin_rad", 0.0)), "with less margin: %s against %s"
			% [squeezed.get("margin_rad"), open.get("margin_rad")])
	check_near(float(squeezed.get("margin_rad", 0.0)), 0.0002 / 0.5, 2.0e-5, "the gap over the distance to it")


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


## The directions, in radians from +x, along which a ball at `from` runs cleanly into pocket p:
## [lowest, highest], or [] when there are none. Found with Guides' straight-line geometry, by a
## scan across the drop circle and a bisection at each end, not with anything of NextTarget's.
func _clear_window(from: Array, p: int) -> Array:
	var shape := Physics.table_shape()
	var pocket: Dictionary = shape["pockets"][p]
	var drop := [float(pocket["drop_centre"][0]), float(pocket["drop_centre"][1])]
	var drop_r := float(pocket["drop_radius"])
	var away := sqrt(pow(float(drop[0]) - float(from[0]), 2.0) + pow(float(drop[1]) - float(from[1]), 2.0))
	var towards := atan2(float(drop[1]) - float(from[1]), float(drop[0]) - float(from[0]))
	var reach := asin(minf(drop_r / away, 1.0))
	var balls := _balls([["cue", from]])
	var goes_in := func(angle: float) -> bool:
		var c := Guides.first_contact(balls, 0, angle, shape, R)
		var ghost: Vector2 = c["ghost"]
		return c["kind"] == "pocket" and absf(ghost.distance_to(Vector2(float(drop[0]), float(drop[1]))) - drop_r) < 1.0e-5
	var steps := 1000
	var step := 2.0 * reach / steps
	var first := -1
	var last := -1
	for i in steps + 1:
		if goes_in.call(towards - reach + i * step):
			first = i if first < 0 else first
			last = i
	if first < 0:
		return []
	var ends: Array = []
	for end: Array in [[first, -1.0], [last, 1.0]]:
		var inside := towards - reach + int(end[0]) * step
		var outside := inside + float(end[1]) * step
		for i in 60:
			var mid := 0.5 * (inside + outside)
			if goes_in.call(mid):
				inside = mid
			else:
				outside = mid
		ends.append(inside)
	return ends


## The pot of a ball at `object` into pocket p with the cue ball at `cue`, from _clear_window:
## {"line": the object ball's unit direction, "ghost", "aim_rad", "cut_deg"}, or {} without one.
func _pot(object: Array, cue: Array, p: int) -> Dictionary:
	var window := _clear_window(object, p)
	if window.size() != 2:
		return {}
	var line := 0.5 * (float(window[0]) + float(window[1]))
	var ghost := [float(object[0]) - 2.0 * R * cos(line), float(object[1]) - 2.0 * R * sin(line)]
	var aim := atan2(float(ghost[1]) - float(cue[1]), float(ghost[0]) - float(cue[0]))
	return {"line": [cos(line), sin(line)], "ghost": ghost, "aim_rad": aim, "cut_deg": rad_to_deg(absf(angle_difference(aim, line)))}


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
