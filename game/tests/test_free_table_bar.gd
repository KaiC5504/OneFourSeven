extends TestCase
## FreeTableBar: each button emits its signal, history and edit state show, touch targets are
## big enough and the bar sits at the safe area's edge.


func test_buttons_emit_their_signals() -> void:
	var bar := FreeTableBar.new()
	var got: Array = []
	bar.retake_pressed.connect(func() -> void: got.append("retake"))
	bar.undo_pressed.connect(func() -> void: got.append("undo"))
	bar.redo_pressed.connect(func() -> void: got.append("redo"))
	bar.edit_toggled.connect(func(on: bool) -> void: got.append("edit %s" % on))
	bar.setup_chosen.connect(func(setup: String) -> void: got.append(setup))
	bar.settings_pressed.connect(func() -> void: got.append("settings"))
	bar.set_history(true, true, true)
	for id: String in ["retake", "undo", "redo", "rack", "empty", "settings"]:
		bar.button(id).pressed.emit()
	bar.button("edit").button_pressed = true
	bar.button("edit").button_pressed = false
	check_eq(got, ["retake", "undo", "redo", "full_rack", "empty", "settings", "edit true", "edit false"], "signals")
	bar.free()


func test_history_disables_buttons() -> void:
	var bar := FreeTableBar.new()
	check(bar.button("undo").disabled and bar.button("redo").disabled and bar.button("retake").disabled,
			"all off before any history")
	bar.set_history(true, false, true)
	check(not bar.button("undo").disabled, "undo on")
	check(bar.button("redo").disabled, "redo off")
	check(not bar.button("retake").disabled, "retake on")
	check(not bar.button("rack").disabled and not bar.button("empty").disabled, "setups always on")
	bar.free()


func test_set_edit_mode_shows_without_emitting() -> void:
	var bar := FreeTableBar.new()
	var got: Array = []
	bar.edit_toggled.connect(func(on: bool) -> void: got.append(on))
	bar.set_edit_mode(true)
	check(bar.button("edit").button_pressed, "shown pressed")
	bar.set_edit_mode(false)
	check(not bar.button("edit").button_pressed, "shown released")
	check_eq(got, [], "no edit_toggled")
	bar.free()


func test_touch_targets_and_placement() -> void:
	var bar := FreeTableBar.new()
	bar.auto_place = false
	tree.root.add_child(bar)
	await tree.process_frame
	for id: String in ["retake", "undo", "redo", "edit", "rack", "empty", "settings"]:
		var size := bar.button(id).size
		check(size.x >= 88.0 and size.y >= 88.0, "%s is at least 88 units square: %s" % [id, size])
		check(bar.button(id).focus_mode == Control.FOCUS_NONE, "%s takes no keyboard focus" % id)
	var safe := Rect2(124.0, 0.0, 1664.0, 838.0)
	bar.place_in(safe)
	check_eq(bar.position.x, 140.0, "left edge of the safe area plus the margin")
	check(bar.position.y >= 0.0 and bar.position.y + bar.size.y <= 838.0, "inside the safe area vertically")
	check_near(bar.position.y + bar.size.y / 2.0, 419.0, 1.0, "centred")
	bar.set_vertical(false)
	bar.place_in(safe)
	check(bar.size.x > bar.size.y, "a row")
	check_eq(bar.position.y, 16.0, "at the top edge")
	bar.queue_free()
	await tree.process_frame
