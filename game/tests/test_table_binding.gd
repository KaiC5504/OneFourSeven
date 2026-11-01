extends TestCase
## OfsTable: the binding's keys and shapes, and the geometry it passes through from
## ofs::table (values need core/src/table_geometry.cpp to be the real implementation).

const COLOURS: PackedStringArray = ["yellow", "green", "brown", "blue", "pink", "black"]
const HALF_LENGTH := 3.569 / 2.0
const EPS := 1e-9


func test_dimensions_keys() -> void:
	var keys: Array = OfsTable.dimensions().keys()
	keys.sort()
	check_eq(keys, ["ball_radius", "baulk_cushion_x", "baulk_line_x", "d_radius", "playing_length",
			"playing_width", "top_cushion_x"], "dimension keys")
	for key: String in keys:
		check(OfsTable.dimensions()[key] is float, key + " is a float")


func test_dimensions_values() -> void:
	var d := OfsTable.dimensions()
	check_near(d["playing_length"], 3.569, EPS, "playing_length")
	check_near(d["playing_width"], 1.778, EPS, "playing_width")
	check_near(d["ball_radius"], 0.02625, EPS, "ball_radius")
	check_near(d["d_radius"], 0.292, EPS, "d_radius")
	check_near(d["baulk_cushion_x"], -HALF_LENGTH, EPS, "baulk_cushion_x")
	check_near(d["top_cushion_x"], HALF_LENGTH, EPS, "top_cushion_x")
	check_near(d["baulk_line_x"], -HALF_LENGTH + 0.737, EPS, "baulk_line_x")


func test_opening_layout_shape() -> void:
	var layout := OfsTable.opening_layout(0.0)
	check_eq(layout.size(), 22, "placements")
	if layout.size() != 22:
		return
	var expected_balls: Array[String] = ["cue"]
	for i in 15:
		expected_balls.append("red")
	expected_balls.append_array(Array(COLOURS))
	for i in layout.size():
		var p: Dictionary = layout[i]
		check_eq(p.keys().size(), 4, "placement %d has ball, index, x, y" % i)
		check_eq(p["ball"], expected_balls[i], "ball %d" % i)
		check(p["index"] is int, "index %d is an int" % i)
		check(p["x"] is float and p["y"] is float, "position %d is floats" % i)
		check_eq(p["index"], i - 1 if p["ball"] == "red" else 0, "index %d" % i)


func test_opening_layout_colours_on_their_spots() -> void:
	var layout := OfsTable.opening_layout(0.0)
	if layout.size() != 22:
		check(false, "22 placements needed")
		return
	for i in COLOURS.size():
		var p: Dictionary = layout[16 + i]
		var s := OfsTable.spot(COLOURS[i])
		check_near(p["x"], s.get("x", INF), EPS, COLOURS[i] + " x on its spot")
		check_near(p["y"], s.get("y", INF), EPS, COLOURS[i] + " y on its spot")


func test_spots() -> void:
	var baulk_line := -HALF_LENGTH + 0.737
	# Arrays, not Vector2: Vector2 is single precision and would fail a 1e-9 tolerance.
	var expected := {
		"yellow": [baulk_line, -0.292],
		"green": [baulk_line, 0.292],
		"brown": [baulk_line, 0.0],
		"blue": [0.0, 0.0],
		"pink": [HALF_LENGTH / 2.0, 0.0],
		"black": [HALF_LENGTH - 0.324, 0.0],
	}
	for colour: String in expected:
		var s := OfsTable.spot(colour)
		check_eq(s.keys(), ["x", "y"], colour + " spot keys")
		var want: Array = expected[colour]
		check_near(s.get("x", INF), want[0], EPS, colour + " x")
		check_near(s.get("y", INF), want[1], EPS, colour + " y")


func test_spot_of_non_colour_is_empty() -> void:
	for name in ["cue", "red", "purple", ""]:
		check_eq(OfsTable.spot(name), {}, "spot(%s)" % name)
