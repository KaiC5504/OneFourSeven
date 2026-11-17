extends TestCase
## The watching button: it names the view a tap goes to, steps through cue, above and round, and
## sits in the top-right corner of the area it is given.

const AREA := Rect2(124.0, 120.0, 1664.0, 718.0)


func test_a_tap_steps_round_the_views_and_says_where_the_next_goes() -> void:
	var button := WatchButton.new()
	var chosen: Array[String] = []
	button.view_chosen.connect(func(view: String) -> void: chosen.append(view))
	check_eq(button.view(), "cue", "starts behind the cue")
	check_eq(button.text, "From above", "and offers the view from above")
	var texts: Array[String] = []
	var views: Array[String] = []
	for i in 4:
		button.pressed.emit()
		views.append(button.view())
		texts.append(button.text)
	check_eq(views, ["above", "round", "cue", "above"] as Array[String], "a tap goes to the next, wrapping")
	check_eq(texts, ["Walk round", "Cue view", "From above", "Walk round"] as Array[String], "each time naming the one after")
	check_eq(chosen, views, "view_chosen once a tap, with the view")
	button.free()


func test_set_view_shows_a_view_without_emitting() -> void:
	var button := WatchButton.new()
	var chosen: Array[String] = []
	button.view_chosen.connect(func(view: String) -> void: chosen.append(view))
	button.set_view("round")
	check(button.view() == "round" and button.text == "Cue view", "round on, cue offered")
	button.set_view("sideways")
	check(button.view() == "round" and button.text == "Cue view", "an unknown view ignored")
	button.set_view("cue")
	check(button.view() == "cue" and button.text == "From above", "back to cue")
	check_eq(chosen, [] as Array[String], "nothing emitted")
	button.pressed.emit()
	check_eq(chosen, ["above"] as Array[String], "a tap goes on from the view set")
	button.free()


func test_place_in_puts_it_top_right_inside_the_area() -> void:
	var button := WatchButton.new()
	tree.root.add_child(button)
	button.place_in(AREA)
	var at := Rect2(button.position, button.size)
	check_eq(at.size, WatchButton.SIZE, "its size")
	check(AREA.encloses(at), "inside the area: %s in %s" % [at, AREA])
	check_near(AREA.end.x - at.end.x, WatchButton.EDGE_MARGIN, 0.5, "the margin from the right")
	check_near(at.position.y - AREA.position.y, WatchButton.EDGE_MARGIN, 0.5, "the margin from the top")
	check_eq(button.focus_mode, Control.FOCUS_NONE, "never takes the keyboard")
	var font := button.get_theme_font("font")
	for words: String in WatchButton.TEXT.values():
		var width := font.get_string_size(words, HORIZONTAL_ALIGNMENT_LEFT, -1, WatchButton.FONT_SIZE).x
		check(width < WatchButton.SIZE.x - 20.0, "\"%s\" fits: %.0f" % [words, width])
	button.free()
