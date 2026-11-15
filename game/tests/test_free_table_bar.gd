extends TestCase
## FreeTableBar: the icon row and the menu each emit their signals, history and edit state show,
## the link status shows as a dot and in words, touch targets are big enough and the row sits at
## the safe area's top-left.


func test_buttons_emit_their_signals() -> void:
	var bar := FreeTableBar.new()
	var got: Array = []
	bar.retake_pressed.connect(func() -> void: got.append("retake"))
	bar.undo_pressed.connect(func() -> void: got.append("undo"))
	bar.redo_pressed.connect(func() -> void: got.append("redo"))
	bar.edit_toggled.connect(func(on: bool) -> void: got.append("edit %s" % on))
	bar.setup_chosen.connect(func(setup: String) -> void: got.append(setup))
	bar.settings_pressed.connect(func() -> void: got.append("settings"))
	bar.home_pressed.connect(func() -> void: got.append("home"))
	bar.overview_toggled.connect(func(on: bool) -> void: got.append("overview %s" % on))
	bar.set_history(true, true, true)
	for id: String in ["retake", "undo", "redo", "rack", "empty", "settings", "home"]:
		bar.button(id).pressed.emit()
	bar.button("edit").button_pressed = true
	bar.button("done").pressed.emit()
	bar.button("overview").button_pressed = true
	bar.button("overview").button_pressed = false
	check_eq(got, ["retake", "undo", "redo", "full_rack", "empty", "settings", "home", "edit true", "edit false",
			"overview true", "overview false"], "signals")
	bar.free()


func test_the_menu_opens_closes_and_closes_after_a_choice() -> void:
	var bar := FreeTableBar.new()
	check(not bar.is_menu_open(), "starts closed")
	bar.button("menu").button_pressed = true
	check(bar.is_menu_open(), "the menu button opens it")
	bar.button("menu").button_pressed = false
	check(not bar.is_menu_open(), "and closes it")
	for id: String in ["rack", "empty", "settings", "home", "edit"]:
		bar.open_menu()
		if id == "edit":
			bar.button(id).button_pressed = not bar.button(id).button_pressed
		else:
			bar.button(id).pressed.emit()
		check(not bar.is_menu_open(), "%s closes the menu" % id)
		check(not bar.button("menu").button_pressed, "%s: the menu button shows it closed" % id)
	bar.free()


func test_a_press_outside_the_menu_closes_it_and_does_nothing_else() -> void:
	var bar := FreeTableBar.new()
	tree.root.add_child(bar)
	await tree.process_frame
	bar.open_menu()
	var backdrop: Control = bar.get_node("Backdrop")
	check(backdrop.visible and backdrop.mouse_filter == Control.MOUSE_FILTER_STOP, "a backdrop takes the press")
	var touch := InputEventScreenTouch.new()
	touch.pressed = true
	touch.position = Vector2(900.0, 400.0)
	backdrop.gui_input.emit(touch)
	check(not bar.is_menu_open(), "closed")
	check(not backdrop.visible, "the backdrop goes with it")
	bar.queue_free()
	await tree.process_frame


func test_history_shows_on_the_icons() -> void:
	var bar := FreeTableBar.new()
	check(bar.button("undo").disabled and bar.button("retake").disabled, "dimmed before any history")
	check(not bar.button("redo").visible, "no Redo with nothing to redo")
	bar.set_history(true, false, true)
	check(not bar.button("undo").disabled, "undo on")
	check(not bar.button("retake").disabled, "retake on")
	check(not bar.button("redo").visible, "still no Redo")
	bar.set_history(true, true, true)
	check(bar.button("redo").visible and not bar.button("redo").disabled, "Redo when available")
	check(not bar.button("rack").disabled and not bar.button("empty").disabled, "setups always on")
	bar.free()


func test_set_edit_mode_shows_without_emitting() -> void:
	var bar := FreeTableBar.new()
	var got: Array = []
	bar.edit_toggled.connect(func(on: bool) -> void: got.append(on))
	check(not bar.button("done").visible, "no Done outside edit mode")
	bar.set_edit_mode(true)
	check(bar.button("edit").button_pressed, "shown pressed")
	check(bar.button("done").visible, "Done in the row")
	bar.set_edit_mode(false)
	check(not bar.button("edit").button_pressed, "shown released")
	check(not bar.button("done").visible, "Done gone")
	check_eq(got, [], "no edit_toggled")
	bar.free()


func test_link_status_is_a_dot_and_words_in_the_menu() -> void:
	check_eq(FreeTableBar.link_text("off"), "PC link: off", "off")
	check_eq(FreeTableBar.link_text("searching"), "PC link: searching…", "searching")
	check_eq(FreeTableBar.link_text("connected"), "PC link: connected", "connected")
	check_eq(FreeTableBar.link_text("lost"), "PC link: lost", "lost")
	var colours := {}
	for status: String in ["off", "searching", "connected", "lost"]:
		colours[FreeTableBar.link_colour(status).to_html()] = status
	check_eq(colours.size(), 4, "a colour per status")
	var bar := FreeTableBar.new()
	tree.root.add_child(bar)
	await tree.process_frame
	check_eq(bar.link_status(), Link.get_status(), "follows the link from the start")
	Link.status_changed.emit("lost")
	check_eq(bar.link_status(), "lost", "and its changes")
	var words := bar.find_children("*", "Label", true, false)
	var said := false
	for label: Node in words:
		said = said or (label as Label).text == "PC link: lost"
	check(said, "the words are in the menu")
	Link.status_changed.emit(Link.get_status())
	bar.queue_free()
	await tree.process_frame


func test_touch_targets_and_placement() -> void:
	var bar := FreeTableBar.new()
	bar.auto_place = false
	tree.root.add_child(bar)
	bar.set_history(true, true, true)
	bar.set_edit_mode(true)
	bar.open_menu()
	await tree.process_frame
	for id: String in ["menu", "overview", "retake", "undo", "redo", "done", "edit", "rack", "empty", "settings", "home"]:
		var size := bar.button(id).size
		check(size.x >= 88.0 and size.y >= 88.0, "%s is at least 88 units square: %s" % [id, size])
		check(bar.button(id).focus_mode == Control.FOCUS_NONE, "%s takes no keyboard focus" % id)
	var safe := Rect2(124.0, 0.0, 1664.0, 838.0)
	bar.place_in(safe)
	var row := bar.row_rect()
	check_eq(row.position, Vector2(140.0, 16.0), "the safe area's top-left plus the margin")
	check(row.size.y <= 110.0, "one slim row: %s" % row)
	var list := bar.list_rect()
	check(list.position.y > row.end.y and safe.encloses(list), "the list below it, in the safe area: %s" % list)
	check_eq(bar.mouse_filter, Control.MOUSE_FILTER_IGNORE, "the bar itself takes no touches")
	bar.queue_free()
	await tree.process_frame


func test_practice_swaps_the_menu_for_the_shots_and_a_choice_closes_it() -> void:
	var bar := FreeTableBar.new()
	tree.root.add_child(bar)
	await tree.process_frame
	check(not bar.button("practice").visible, "no Practice item without shots")
	var titles: Array[String] = ["One", "Two", "Three"]
	bar.set_practices(titles)
	check(bar.button("practice").visible, "Practice shows once there are shots")
	var got: Array = []
	bar.practice_chosen.connect(func(index: int) -> void: got.append(index))
	bar.open_menu()
	bar.button("practice").pressed.emit()
	check(bar.is_practice_open() and not bar.is_menu_open(), "the shots replace the menu")
	check(bar.button("menu").button_pressed, "the menu button still shows it open")
	check_eq(bar.shots_rect().position, bar.list_rect().position, "where the menu was")
	for i in titles.size():
		var b := bar.button("practice_%d" % i)
		check(b != null and b.text == titles[i], "shot %d is listed" % i)
		if b != null:
			check(b.size.x >= 88.0 and b.size.y >= 88.0, "shot %d is big enough to touch" % i)
	check(bar.shots_rect().size.x > 2.0 * FreeTableBar.SHOT_SIZE.x, "two to a row")
	bar.button("practice_1").pressed.emit()
	check_eq(got, [1], "the choice")
	check(not bar.is_practice_open() and not bar.button("menu").button_pressed, "and the list closes")
	bar.open_practice()
	bar.close_menu()
	check(not bar.is_practice_open(), "closing the menu closes the shots")
	var fewer: Array[String] = ["Only"]
	bar.set_practices(fewer)
	check(bar.button("practice_0").text == "Only" and bar.button("practice_1") == null, "a new list replaces the old")
	bar.free()

