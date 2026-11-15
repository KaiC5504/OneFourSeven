extends TestCase
## The free table's practice shots (spec 12.1): the list loads, every filmed strike pots its ball,
## the aim the player is given turns into the filmed one, the strip, and the whole thing on the
## real scene: chosen from the menu, shown as filmed, set up again, ended.

const MainScript := preload("res://scripts/main.gd")
const TEMP_INDEX := "user://test_practice_index.json"
const TEMP_POSITIONS := "user://test_practice_positions"
const TRUMP := "film_trump_screw_left"

var _log_start := 0


func test_every_listed_shot_loads() -> void:
	var index: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(Practice.INDEX_PATH))
	var listed: Array = index["shots"]
	check(listed.size() >= 9, "the nine filmed shots are listed: %d" % listed.size())
	_mark_log()
	var shots := Practice.load_all()
	check_eq(shots.size(), listed.size(), "every listed shot loads")
	check_eq(_logged("app.error"), [], "nothing left out")
	var seen := {}
	for i in shots.size():
		var shot := shots[i]
		check_eq(shot["position"], listed[i]["position"], "menu order kept")
		check(not seen.has(shot["position"]), "%s listed once" % shot["position"])
		seen[shot["position"]] = true
		check(not String(shot["title"]).is_empty() and not String(shot["hint"]).is_empty(), "%s has its words" % shot["position"])
		check_eq(FreeTable.ball_list_problems(shot["balls"], shot["cue_ball"]), [], "%s is a table" % shot["position"])
		check_eq(FreeTable.off_table_problems(shot["balls"]), [], "%s on the cloth" % shot["position"])
		check_eq((shot["strike"] as Dictionary).keys(), Practice.STRIKE_KEYS, "%s strike" % shot["position"])


func test_a_bad_entry_is_left_out_and_logged() -> void:
	DirAccess.make_dir_recursive_absolute(TEMP_POSITIONS)
	var good: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(Practice.POSITIONS_DIR.path_join(TRUMP + ".json")))
	var no_strike := good.duplicate(true)
	no_strike.erase("strike")
	var files := {"good": good, "no_strike": no_strike, "no_balls": {"cue_ball": 0}}
	for file_name: String in files:
		_write(TEMP_POSITIONS.path_join(file_name + ".json"), JSON.stringify(files[file_name]))
	var entries := [
		{"position": "good", "title": "Good", "hint": "Plays"},
		{"position": "no_such_position", "title": "Missing", "hint": "x"},
		{"position": "../good", "title": "Not a name", "hint": "x"},
		{"position": "good", "hint": "no title"},
		{"position": "no_strike", "title": "No strike", "hint": "x"},
		{"position": "no_balls", "title": "No balls", "hint": "x"},
		"not an object",
	]
	_write(TEMP_INDEX, JSON.stringify({"shots": entries}))
	_mark_log()
	var shots := Practice.load_all(TEMP_INDEX, TEMP_POSITIONS)
	check_eq(shots.size(), 1, "only the good one")
	if shots.size() == 1:
		check_eq(shots[0]["title"], "Good", "the good one")
	check_eq(_logged("app.error").size(), entries.size() - 1, "each bad entry logged")
	_mark_log()
	check_eq(Practice.load_all("user://no_such_index.json"), [], "no index, no shots")
	check_eq(_logged("app.error").size(), 1, "and that is logged")
	for file_name: String in files:
		DirAccess.remove_absolute(TEMP_POSITIONS.path_join(file_name + ".json"))
	DirAccess.remove_absolute(TEMP_POSITIONS)
	DirAccess.remove_absolute(TEMP_INDEX)


func test_the_filmed_cue_in_the_controls_words() -> void:
	var strike := {"aim_rad": 0.0, "tip_x": -0.18, "tip_y": -0.35, "elevation_rad": 0.0, "speed_m_s": 6.5}
	check_eq(Practice.cue_text(strike, 6.5, 0.55), "screw 0.35 · left 0.18 · power 100%", "screw and side")
	strike["tip_x"] = 0.0
	strike["tip_y"] = 0.0
	strike["speed_m_s"] = PullControl.speed_for_power(0.5, 6.5)
	check_eq(Practice.cue_text(strike, 6.5, 0.55), "centre ball · power 50%", "centre ball")


func test_every_filmed_strike_pots_its_ball() -> void:
	for shot in Practice.load_all():
		var fixture: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(
				Practice.POSITIONS_DIR.path_join(String(shot["position"]) + ".json")))
		var object := int(fixture["measured"]["object_ball"])
		var record := Physics.simulate(shot["balls"], shot["cue_ball"], shot["strike"],
				{"shot_id": "test-practice", "mode": "test", "strike_mode": "demo"})
		check(record != null, "%s plays" % shot["position"])
		if record == null:
			continue
		var final_balls := record.final_balls()
		check(int((final_balls[object] as Dictionary).get("pocket", -1)) >= 0, "%s pots its %s" % [shot["position"],
				shot["balls"][object]["ball"]])
		check_eq(int((final_balls[int(shot["cue_ball"])] as Dictionary).get("pocket", -1)), -1,
				"%s keeps the cue ball up" % shot["position"])


func test_the_players_aim_turns_into_the_filmed_one() -> void:
	for shot in Practice.load_all():
		var balls: Array[Dictionary] = shot["balls"]
		var strike: Dictionary = shot["strike"]
		var forced := Physics.min_cue_elevation(balls, shot["cue_ball"], strike)
		var aim := Practice.player_aim(Physics.predict, balls, shot["cue_ball"], strike, forced)
		var set_up := strike.duplicate()
		set_up["aim_rad"] = aim
		var solved := SpinAim.solve(Physics.predict, balls, shot["cue_ball"], set_up, forced)
		check_near(angle_difference(float(strike["aim_rad"]), float(solved["aim_rad"])), 0.0, 2.0e-5,
				"%s: the allowance gives back the filmed aim" % shot["position"])
		if absf(float(strike["tip_x"])) > 0.1:
			check(absf(angle_difference(aim, float(strike["aim_rad"]))) > 1.0e-4, "%s: side turns the aim" % shot["position"])


func test_same_balls() -> void:
	var a: Array[Dictionary] = [{"ball": "cue", "index": 0, "x": 0.1, "y": 0.2}, {"ball": "red", "index": 3, "x": 1.0, "y": 0.0}]
	var b: Array[Dictionary] = [{"ball": "cue", "index": 0, "x": 0.1, "y": 0.2}, {"ball": "red", "index": 3, "x": 1.0, "y": 0.0}]
	check(Practice.same_balls(a, b), "the same")
	b[1]["x"] = 1.0 + 1e-12
	check(not Practice.same_balls(a, b), "a ball moved")
	b.remove_at(1)
	check(not Practice.same_balls(a, b), "a ball gone")


func test_the_strip_shows_the_shot_and_emits_its_buttons() -> void:
	var strip := PracticeStrip.new()
	tree.root.add_child(strip)
	var got: Array = []
	strip.show_pressed.connect(func() -> void: got.append("show"))
	strip.again_pressed.connect(func() -> void: got.append("again"))
	strip.close_pressed.connect(func() -> void: got.append("close"))
	check(not strip.visible, "hidden until a practice starts")
	strip.set_shot("Trump: black", "Deep screw.", "screw 0.35 · power 87%")
	check_eq(strip.texts(), ["Trump: black", "Deep screw.", "As filmed: screw 0.35 · power 87%"], "the three lines")
	for id: String in ["show", "again", "close"]:
		strip.button(id).pressed.emit()
		check(strip.button(id).custom_minimum_size.x >= 72.0 and strip.button(id).custom_minimum_size.y >= 88.0,
				"%s is big enough to touch" % id)
	check_eq(got, ["show", "again", "close"], "signals")
	strip.set_busy(true)
	check(strip.button("show").disabled and strip.button("again").disabled and not strip.button("close").disabled,
			"Show me and Again wait while a shot plays")
	strip.set_busy(false)
	check(not strip.button("show").disabled, "and come back")
	strip.free()


func test_the_strip_fits_the_room_it_is_given() -> void:
	var strip := PracticeStrip.new()
	tree.root.add_child(strip)
	strip.set_shot("A title", "A hint that is rather longer than the room the strip is about to be given, on purpose",
			"top 0.10 · power 50%")
	strip.place_in(Rect2(560.0, 16.0, 1100.0, 100.0))
	var wide := strip.size.x
	check_eq(strip.position, Vector2(560.0, 16.0), "at the area's corner")
	check(wide <= 1100.0, "inside a wide area: %.0f" % wide)
	strip.place_in(Rect2(560.0, 16.0, 700.0, 100.0))
	check(strip.size.x <= 700.0 and strip.size.x < wide, "trimmed to a narrow one: %.0f" % strip.size.x)
	strip.set_shot("Short", "Short", "Short")
	strip.place_in(Rect2(560.0, 16.0, 1100.0, 100.0))
	check(strip.size.x < 700.0, "and no wider than its words need: %.0f" % strip.size.x)
	strip.free()


func test_a_practice_shot_on_the_table() -> void:
	var main: MainScript = await _spawn()
	var table: FreeTable = main.get_node("FreeTable")
	var bar: FreeTableBar = main.get_node("Hud/FreeTableBar")
	var strip: PracticeStrip = main.get_node("Hud/PracticeStrip")
	var shots := Practice.load_all()
	var at := -1
	for i in shots.size():
		if shots[i]["position"] == TRUMP:
			at = i
	check(at >= 0, "Trump's shot is listed")
	check(bar.button("practice").visible, "the menu has Practice")
	check_eq(bar.button("practice_%d" % at).text, shots[at]["title"], "the list names the shot")
	check(not strip.visible and main.harness_state()["practice"] == "", "no practice yet")

	_mark_log()
	bar.button("menu").button_pressed = true
	bar.button("practice").pressed.emit()
	check(bar.is_practice_open() and not bar.is_menu_open(), "Practice swaps the menu for the shots")
	bar.button("practice_%d" % at).pressed.emit()
	check(not bar.is_practice_open(), "choosing one closes the list")
	check(Practice.same_balls(table.balls, shots[at]["balls"]), "the position is on the table")
	check_eq(main.harness_state()["practice"], TRUMP, "state names the practice")
	check(strip.visible and strip.texts()[0] == shots[at]["title"], "the strip shows it")
	check(table.can_undo(), "undo goes back to the table before")
	var actions := _logged("freetable.action")
	check(actions.size() == 1 and actions[0]["detail"].get("position") == TRUMP, "logged: %s" % [actions])
	var strike: Dictionary = shots[at]["strike"]
	var setup: ShotSetup = main.setup
	var player_aim := setup.aim_rad
	check(absf(angle_difference(player_aim, float(strike["aim_rad"]))) < 0.05, "the aim is on the shot's line")
	var bar_end := bar.row_rect().end.x
	check(strip.position.x >= bar_end and is_equal_approx(strip.position.y, bar.row_rect().position.y),
			"the strip sits right of the bar")

	_mark_log()
	strip.button("show").pressed.emit()
	check(main.is_playing(), "Show me plays it")
	check(strip.button("show").disabled and bar.button("practice").disabled, "and waits while it plays")
	var inputs := _logged("shot.input")
	check(inputs.size() == 1 and inputs[0]["strike_mode"] == "demo", "logged as a demo: %s" % [inputs])
	var camera := _logged("shot.camera")
	check(camera.size() == 1 and camera[0]["sequence"] == "broadcast" and camera[0]["reason"] == "practice",
			"watched whole, from the full table: %s" % [camera])
	var simulated := _logged("shot.simulated")
	check(simulated.size() == 1, "simulated once")
	if simulated.size() == 1:
		var played: Dictionary = simulated[0]["bundle"]["strike"]
		check_eq([played["aim_rad"], played["tip_x"], played["tip_y"], played["speed_m_s"]],
				[strike["aim_rad"], strike["tip_x"], strike["tip_y"], strike["speed_m_s"]], "the strike as filmed")
	var allowed := _logged("shot.side_allowed")
	check(allowed.size() == 1 and allowed[0]["reason"] == "played as given", "the turn from the setup's aim logged")
	if allowed.size() == 1 and inputs.size() == 1:
		check_near(float(inputs[0]["aim_rad"]) + float(allowed[0]["turned_rad"]), float(strike["aim_rad"]), 1e-9,
				"setup aim plus the turn is the played aim")
	main.skip_shot()
	await _until_idle(main)
	var potted: Array = main.harness_state()["last_shot"]["potted"]
	check(potted.size() == 1 and potted[0]["ball"] == "black", "the black goes in: %s" % [potted])
	check(not strip.button("show").disabled, "the strip is back")

	main.retake()
	check(Practice.same_balls(table.balls, shots[at]["balls"]), "retake: the position again")
	check_eq([setup.tip_x, setup.tip_y, setup.speed_m_s], [strike["tip_x"], strike["tip_y"], strike["speed_m_s"]],
			"with the filmed cue on the controls")
	check_near(setup.aim_rad, player_aim, 1e-12, "and the aim the player sets")

	setup.set_aim(1.0)
	var states := table.history_size()
	strip.button("again").pressed.emit()
	check_eq(table.history_size(), states, "Again on the position adds no state")
	check_near(setup.aim_rad, player_aim, 1e-12, "Again puts the aim back")
	check(not main.harness_play_shot({"aim_rad": 0.3, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0,
			"speed_m_s": 1.0}).is_empty(), "a shot of the player's own")
	main.skip_shot()
	await _until_idle(main)
	check(not Practice.same_balls(table.balls, shots[at]["balls"]), "the table has moved on")
	strip.button("again").pressed.emit()
	check(Practice.same_balls(table.balls, shots[at]["balls"]), "Again brings the position back")

	table.set_edit_mode(true)
	strip.button("show").pressed.emit()
	check(not table.edit_mode and main.is_playing(), "Show me leaves edit mode and plays")
	main.skip_shot()
	await _until_idle(main)

	strip.button("close").pressed.emit()
	check(not strip.visible and main.harness_state()["practice"] == "", "the close button ends the practice")
	check(not main.practice_again() and main.show_practice().is_empty(), "nothing to show after it")
	check(main.start_practice(at) and strip.visible, "started again")
	bar.button("rack").pressed.emit()
	check(not strip.visible and table.balls.size() == 22, "a rack ends it")
	check(not main.start_practice(shots.size()) and not main.start_practice(-1), "no such shot")
	main.free()


func test_without_the_allowance_the_aim_is_the_filmed_one() -> void:
	var main: MainScript = await _spawn()
	Settings.set_value("allow_for_side", false)
	var shots := Practice.load_all()
	check(main.start_practice(0), "started")
	check_near((main.setup as ShotSetup).aim_rad, float(shots[0]["strike"]["aim_rad"]), 1e-12, "the filmed aim itself")
	Settings.set_value("allow_for_side", Settings.DEFAULTS["allow_for_side"])
	main.free()


func _spawn() -> MainScript:
	for key: String in Settings.DEFAULTS:
		Settings.set_value(key, Settings.DEFAULTS[key])
	var main := load("res://scenes/main.tscn").instantiate() as MainScript
	tree.root.add_child(main)
	await tree.process_frame
	await tree.process_frame
	return main


func _write(path: String, text: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string(text)
	file.close()


func _until_idle(main: MainScript) -> void:
	for i in 120:
		if not main.harness_busy():
			return
		await tree.process_frame
	failures.append("still busy after 120 frames")


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


## The data of each `event` logged since _mark_log, read from the session file.
func _logged(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append((parsed as Dictionary).get("data", {}))
	return found
