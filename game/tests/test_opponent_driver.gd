extends TestCase
## OpponentDriver against FakeMatchEngine: when a level is asked, when its stroke is handed over
## against the natural pause, and that the stand-in's way is unchanged.

var _shots: Array[Array] = []
var _choices: Array[Array] = []
var _thinking: Array[bool] = []


func test_a_level_thinks_at_once_and_waits_out_the_pause() -> void:
	var e := _engine()
	e.opponent_shots.append(_level_shot())
	var driver := _driver()
	driver.request_shot(e, 1, 0.3, "beginner")
	var think := _last_call(e, "opponent_think")
	check(think.size() == 5 and think[1] == "beginner" and think[2] is int,
			"opponent_think(level, seed, context, budget) in the same call: %s" % [think])
	check_eq(think[4], {"max_ms": 800.0}, "a clock of 800 ms, spec 10.6's 1.5 s with room for the cue's show")
	check_eq(think[2], driver.last_seed, "with the seed kept for the logs")
	check(String((think[3] as Dictionary).get("shot_id", "")).begins_with(Log.session()), "and a shot id of this session")
	check(driver.is_thinking() and driver.is_waiting(), "thinking")
	check_eq(_thinking, [true] as Array[bool], "thinking_changed(true)")
	# The driver counts the frames' process time, which headless runs report ahead of the clock;
	# this counts every frame since the request, at least as much as the driver does.
	var counted := 0.0
	for i in 3:
		await tree.process_frame
		counted += driver.get_process_delta_time()
	check_eq(e.called().count("opponent_take"), 1, "the answer is taken as soon as it is there")
	if counted < 0.25:
		check_eq(_shots.size(), 0, "but not handed over before the pause is out")
		check(driver.is_thinking(), "still thinking as far as the game knows")
	var frames := 0
	while _shots.is_empty() and frames < 600:
		await tree.process_frame
		counted += driver.get_process_delta_time()
		frames += 1
	check_eq(_shots.size(), 1, "handed over")
	check(counted >= 0.3 - 0.001, "after the pause (%.3f s counted)" % counted)
	if _shots.size() == 1:
		check_eq(_shots[0][0], 1, "for the player")
		var shot: Dictionary = _shots[0][1]
		check_eq(shot["shot_id"], think[3]["shot_id"], "under the id it thought with")
		for key: String in ["ok", "strike", "level", "meant", "think"]:
			check_eq(shot.get(key), _level_shot()[key], "as the engine gave %s" % key)
	check(not driver.is_thinking() and not driver.is_waiting(), "done")
	check_eq(_thinking, [true, false] as Array[bool], "thinking_changed(false)")
	check_eq(e.called().count("standin_shot"), 0, "the stand-in is not asked")
	check(not driver.is_processing(), "and it sleeps")
	driver.free()


func test_a_slow_think_takes_longer_than_the_pause() -> void:
	var e := _engine()
	e.opponent_shots.append(_level_shot())
	e.think_polls = 6
	var driver := _driver()
	driver.request_shot(e, 1, 0.0, "maximum")
	await _frames(4)
	check_eq(_shots.size(), 0, "nothing while it thinks")
	check(driver.is_thinking(), "thinking")
	await _frames(5)
	check_eq(_shots.size(), 1, "handed over once the answer is there")
	check_eq(e.called().count("opponent_take"), 1, "taken once")
	check_eq(e.called().count("opponent_think"), 1, "thought once")
	driver.free()


func test_a_refused_think_comes_back_after_the_pause() -> void:
	var e := _engine()
	var driver := _driver()
	driver.request_shot(e, 0, 0.2, "beginner")
	check(not driver.is_thinking(), "not thinking")
	check(driver.is_waiting(), "but waiting")
	check_eq(_thinking, [] as Array[bool], "no thinking_changed")
	var counted := 0.0
	var frames := 0
	while _shots.is_empty() and frames < 600:
		await tree.process_frame
		counted += driver.get_process_delta_time()
		frames += 1
	check(counted >= 0.2 - 0.001, "the pause stands (%.3f s counted)" % counted)
	check_eq(_shots.size(), 1, "a refusal")
	if _shots.size() == 1:
		var shot: Dictionary = _shots[0][1]
		check_eq(shot["ok"], false, "not ok")
		check_eq(shot["error"], "no level stroke queued", "with the engine's last_error")
		check_eq(shot["shot_id"], _last_call(e, "opponent_think")[3]["shot_id"], "and the id it was asked under")
	check_eq(driver.last_call, "opponent_think", "named")
	check_eq(e.called().count("opponent_take"), 0, "nothing is taken")
	driver.free()


func test_a_level_that_stops_thinking_with_no_answer_is_a_refusal() -> void:
	var e := _engine()
	e.opponent_shots.append(_level_shot())
	e.think_polls = 100
	var driver := _driver()
	driver.request_shot(e, 1, 0.0, "beginner")
	# The engine drops its thinking (the frame changed under it) without an answer.
	e.opponent_cancel("test")
	await _frames(2)
	check_eq(_shots.size(), 1, "the opponent is not left waiting for ever")
	if _shots.size() == 1:
		check(_shots[0][1]["ok"] == false and String(_shots[0][1]["error"]).contains("no longer thinking"),
				"a refusal that says why: %s" % [_shots[0][1]])
	check_eq(_thinking, [true, false] as Array[bool], "thinking off")
	driver.free()


func test_cancel_stops_the_thinking() -> void:
	var e := _engine()
	e.opponent_shots.append(_level_shot())
	e.opponent_shots.append(_level_shot())
	e.think_polls = 3
	var driver := _driver()
	driver.request_shot(e, 1, 0.0, "beginner")
	await _frames(1)
	driver.cancel()
	check_eq(_last_call(e, "opponent_cancel"), ["opponent_cancel", "left"], "the engine stops thinking")
	check(not driver.is_thinking() and not driver.is_waiting(), "nothing waits")
	check_eq(_thinking, [true, false] as Array[bool], "thinking_changed(false)")
	await _frames(6)
	check_eq(_shots.size(), 0, "nothing is emitted after")
	check_eq(e.called().count("opponent_take"), 0, "nothing is taken")
	driver.cancel()
	check_eq(e.called().count("opponent_cancel"), 1, "cancelling with nothing thinking asks nothing")

	_thinking.clear()
	driver.request_shot(e, 1, 0.0, "beginner")
	driver.request_choice(e, 1, 0.0, "beginner")
	check_eq(e.called().count("opponent_cancel"), 2, "a later request drops the thinking")
	check_eq(_last_call(e, "opponent_cancel"), ["opponent_cancel", "replaced"], "as replaced")
	check_eq(_thinking, [true, false] as Array[bool], "and says so")
	await _frames(2)
	check_eq(_shots.size(), 0, "the dropped stroke never comes")
	check_eq(_choices.size(), 1, "the choice does")
	driver.free()


func test_cancel_passes_its_reason_to_the_engine() -> void:
	var e := _engine()
	var driver := _driver()
	for why: String in OpponentDriver.CANCEL_WHYS:
		e.opponent_shots.append(_level_shot())
		driver.request_shot(e, 1, 0.0, "beginner")
		driver.cancel(why)
		check_eq(_last_call(e, "opponent_cancel"), ["opponent_cancel", why], "\"%s\" passed through" % why)
	e.opponent_shots.append(_level_shot())
	driver.request_shot(e, 1, 0.0, "beginner")
	OfsLog.flush()
	var start := FileAccess.get_file_as_bytes(OfsLog.file_path()).size()
	driver.cancel("bored")
	check_eq(_last_call(e, "opponent_cancel"), ["opponent_cancel", "left"], "an unknown reason is told as left")
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(start).get_string_from_utf8()
	check(text.contains("app.error") and text.contains("cancel(\\\"bored\\\")"), "and logged: %s" % text)
	driver.free()


func test_a_stroke_taken_and_then_dropped_is_logged_as_cancelled() -> void:
	var e := _engine()
	e.opponent_shots.append(_level_shot())
	var driver := _driver()
	driver.request_shot(e, 1, 5.0, "beginner")
	await _frames(3)
	check_eq(e.called().count("opponent_take"), 1, "taken, and held for the pause")
	check_eq(_shots.size(), 0, "not handed over")
	OfsLog.flush()
	var start := FileAccess.get_file_as_bytes(OfsLog.file_path()).size()
	driver.cancel("conceded")
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(start).get_string_from_utf8()
	check(text.contains("opponent.cancelled") and text.contains("\"why\":\"conceded\"")
			and text.contains("\"level\":\"beginner\""), "logged by the driver: %s" % text)
	check(not driver.is_thinking(), "no longer thinking")
	driver.free()


func test_the_pause_menu_holds_the_answer() -> void:
	var e := _engine()
	e.opponent_shots.append(_level_shot())
	var driver := _driver()
	driver.request_shot(e, 1, 0.0, "beginner")
	driver.process_mode = Node.PROCESS_MODE_DISABLED
	await _frames(4)
	check_eq(e.called().count("opponent_take"), 0, "not taken while held")
	check_eq(_shots.size(), 0, "nor handed over")
	check(driver.is_thinking(), "thinking")
	driver.process_mode = Node.PROCESS_MODE_INHERIT
	await _frames(2)
	check_eq(_shots.size(), 1, "on resume")
	driver.free()


func test_a_levels_choice() -> void:
	var e := _engine()
	e.opponent_choices.append("play_again")
	var driver := _driver()
	driver.request_choice(e, 1, 0.0, "maximum")
	check_eq(e.called().count("opponent_choice"), 0, "never in the same call")
	check(not driver.is_thinking(), "a choice is not thought about off the main thread")
	await _frames(2)
	var asked := _last_call(e, "opponent_choice")
	check_eq(asked, ["opponent_choice", "maximum", 1, driver.last_seed], "opponent_choice(level, player, seed)")
	check_eq(_choices, [[1, "play_again"]] as Array[Array], "choice_made")
	check_eq(e.called().count("standin_choice"), 0, "not the stand-in")
	driver.request_choice(e, 1, 0.0, "maximum")
	await _frames(2)
	check_eq(_choices.back(), [1, ""], "no answer is \"\"")
	driver.free()


func test_no_level_is_the_stand_in_as_before() -> void:
	var e := _engine()
	e.standin_shots.append({"ok": true, "error": "", "strike": {}})
	e.standin_choices.append("play")
	var driver := _driver()
	driver.request_shot(e, 1, 0.0)
	check(not driver.is_thinking(), "the stand-in does not think")
	await _frames(2)
	check_eq(_shots.size(), 1, "its stroke")
	check_eq(_last_call(e, "standin_shot")[1], driver.last_seed, "standin_shot with the seed")
	driver.request_choice(e, 0, 0.0, "")
	await _frames(2)
	check_eq(_choices, [[0, "play"]] as Array[Array], "its choice")
	check_eq(e.called().count("opponent_think") + e.called().count("opponent_choice"), 0, "no level is asked")
	check_eq(_thinking, [] as Array[bool], "no thinking_changed")
	driver.free()


func _engine() -> FakeMatchEngine:
	return FakeMatchEngine.new()


func _driver() -> OpponentDriver:
	_shots.clear()
	_choices.clear()
	_thinking.clear()
	var driver := OpponentDriver.new()
	driver.shot_chosen.connect(func(player: int, shot: Dictionary) -> void: _shots.append([player, shot]))
	driver.choice_made.connect(func(player: int, kind: String) -> void: _choices.append([player, kind]))
	driver.thinking_changed.connect(func(on: bool) -> void: _thinking.append(on))
	tree.root.add_child(driver)
	return driver


func _level_shot() -> Dictionary:
	return {"ok": true, "error": "", "places_cue": false, "cue": [], "nominated": "",
			"strike": {"aim_rad": 0.5, "tip_x": 0.0, "tip_y": 0.1, "elevation_rad": 0.05, "speed_m_s": 2.5},
			"why": {"kind": "pot", "level": "beginner"}, "us": 900, "level": "beginner",
			"meant": {"aim_rad": 0.49, "tip_x": 0.0, "tip_y": 0.1, "elevation_rad": 0.05, "speed_m_s": 2.5},
			"think": {"candidates": 3, "sims": 9, "steps": 700, "depth": 1, "stopped": "done", "ms": 2.0}}


func _frames(count: int) -> void:
	for i in count:
		await tree.process_frame


func _last_call(e: FakeMatchEngine, method: String) -> Array:
	for i in range(e.calls.size() - 1, -1, -1):
		if e.calls[i][0] == method:
			return e.calls[i]
	check(false, "%s was never called" % method)
	return [method, null, null, null, null]
