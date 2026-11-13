extends TestCase
## SettingsPanel: every control writes through Settings.set_value, follows Settings.changed, and
## opens and closes as a modal inside the safe area. The panel is bound to a scratch store.

const SettingsStore := preload("res://scripts/autoload/settings.gd")


func test_controls_call_through_to_settings() -> void:
	var store := _store()
	var panel := SettingsPanel.new()
	panel.bind_settings(store)
	# Godot 4.7's sliders only emit value_changed inside the tree.
	tree.root.add_child(panel)
	(panel.control("strike_mode:pull") as Button).button_pressed = true
	check_eq(store.get_value("strike_mode"), "pull", "strike mode")
	(panel.control("guide_level:3") as Button).button_pressed = true
	check_eq(store.get_value("guide_level"), 3, "guide level")
	check(not (panel.control("guide_level:1") as Button).button_pressed, "one guide level at a time")
	(panel.control("aim_sensitivity") as HSlider).value = 2.0
	check_eq(store.get_value("aim_sensitivity"), 2.0, "aim sensitivity")
	(panel.control("fine_tune_sensitivity") as HSlider).value = 0.5
	check_eq(store.get_value("fine_tune_sensitivity"), 0.5, "fine-tune sensitivity")
	(panel.control("volume") as HSlider).value = 0.35
	check_near(float(store.get_value("volume")), 0.35, 1e-9, "volume")
	(panel.control("stroke_wander") as Button).button_pressed = true
	check_eq(store.get_value("stroke_wander"), true, "stroke wander")
	(panel.control("reset_spin_after_shot") as Button).button_pressed = false
	check_eq(store.get_value("reset_spin_after_shot"), false, "spin reset")
	(panel.control("haptics") as Button).button_pressed = false
	check_eq(store.get_value("haptics"), false, "haptics")
	panel.queue_free()
	await tree.process_frame
	_cleanup(store)


func test_a_slider_drag_commits_once_on_release() -> void:
	var store := _store()
	var panel := SettingsPanel.new()
	panel.bind_settings(store)
	tree.root.add_child(panel)
	var changes: Array = []
	store.changed.connect(func(key: String, value: Variant) -> void: changes.append([key, value]))
	var slider := panel.control("volume") as HSlider
	slider.drag_started.emit()
	slider.value = 0.5
	slider.value = 0.6
	check_eq(changes, [], "nothing while dragging")
	slider.drag_ended.emit(true)
	check_eq(changes.size(), 1, "one change on release")
	check_near(float(store.get_value("volume")), 0.6, 1e-9, "the released value")
	panel.queue_free()
	await tree.process_frame
	_cleanup(store)


func test_controls_follow_settings_changed() -> void:
	var store := _store()
	var panel := SettingsPanel.new()
	panel.bind_settings(store)
	check((panel.control("strike_mode:stroke") as Button).button_pressed, "Push shown by default")
	check_eq((panel.control("strike_mode:stroke") as Button).text, "Push", "named Push")
	check_eq((panel.control("strike_mode:pull") as Button).text, "Pull and release", "and Pull and release")
	check(panel.control("strike_mode:power_bar") == null, "no power bar")
	check((panel.control("guide_level:1") as Button).button_pressed, "guide level 1 shown")
	check_eq((panel.control("haptics") as Button).text, "On", "switch text")
	var emitted: Array = []
	store.changed.connect(func(key: String, _v: Variant) -> void: emitted.append(key))
	store.set_value("guide_level", 0)
	store.set_value("strike_mode", "pull")
	store.set_value("aim_sensitivity", 3.0)
	store.set_value("haptics", false)
	check((panel.control("guide_level:0") as Button).button_pressed, "guide level 0 shown")
	check(not (panel.control("guide_level:1") as Button).button_pressed, "guide level 1 released")
	check((panel.control("strike_mode:pull") as Button).button_pressed, "pull shown")
	check_eq((panel._captions["strike_mode"] as Label).text, SettingsPanel.STRIKE_TEXT["pull"], "the caption says what Pull does")
	check_eq((panel.control("aim_sensitivity") as HSlider).value, 3.0, "slider moved")
	check(not (panel.control("haptics") as Button).button_pressed, "haptics off shown")
	check_eq((panel.control("haptics") as Button).text, "Off", "switch text")
	check_eq(emitted, ["guide_level", "strike_mode", "aim_sensitivity", "haptics"], "showing a change writes nothing back")
	panel.free()
	_cleanup(store)


func test_open_close_and_placement() -> void:
	var store := _store()
	var panel := SettingsPanel.new()
	panel.bind_settings(store)
	panel.auto_place = false
	tree.root.add_child(panel)
	check(not panel.visible, "starts closed")
	var closes := [0]
	panel.closed.connect(func() -> void: closes[0] += 1)
	panel.open()
	check(panel.visible, "open")
	await tree.process_frame
	var safe := Rect2(124.0, 0.0, 1664.0, 838.0)
	panel.place_in(safe)
	var card := panel.get_child(1) as Control
	check(safe.encloses(card.get_rect()), "the card is inside the safe area: %s" % card.get_rect())
	for id: String in ["close", "strike_mode:stroke", "guide_level:2", "aim_sensitivity", "volume", "haptics"]:
		check(panel.control(id).size.y >= 88.0, "%s is at least 88 units tall" % id)
	(panel.control("close") as Button).pressed.emit()
	check(not panel.visible, "Close closes")
	check_eq(closes[0], 1, "closed emitted")
	panel.close()
	check_eq(closes[0], 1, "closing a closed panel emits nothing")
	panel.queue_free()
	await tree.process_frame
	_cleanup(store)


func _store() -> SettingsStore:
	var store := SettingsStore.new()
	var dir := "user://test_settings_panel_%d_%d" % [Time.get_ticks_usec(), randi()]
	DirAccess.make_dir_recursive_absolute(dir)
	store.path = dir.path_join("settings.json")
	store.load_settings()
	return store


func _cleanup(store: SettingsStore) -> void:
	var dir := store.path.get_base_dir()
	for file in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(dir.path_join(file))
	DirAccess.remove_absolute(dir)
	store.free()
