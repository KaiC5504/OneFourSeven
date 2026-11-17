extends TestCase
## The table scene through the opponent's turn and Watch a 147, with a scripted engine: what the
## scoreboard and the stroke controls show while the opponent thinks, lines up and plays, the
## break shot.result logs as an exhibition ends, that an exhibition has nothing to concede, the
## level named in app.error, and the safe area handed to the camera.

const MainScript := preload("res://scripts/main.gd")
const HarnessScript := preload("res://scripts/autoload/harness.gd")
const FRAME_VIEW := "res://data/rules/example_frame_view.json"
const MATCH := "res://data/rules/example_match.json"
const STRIKE := {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.0}
const THEIRS := {"aim_rad": 0.35, "tip_x": 0.2, "tip_y": -0.3, "elevation_rad": 0.05, "speed_m_s": 2.5}
const THINK := {"candidates": 12, "sims": 40, "steps": 120, "depth": 0, "stopped": "done", "ms": 412.0}

var _engine: FakeMatchEngine
var _log_start := 0


func test_the_scoreboard_in_an_exhibition_is_the_147s_side_alone() -> void:
	var board := Scoreboard.new()
	board.auto_place = false
	tree.root.add_child(board)
	var strip: Control = board.get_node("Strip")
	var frame := _frame(1, {"break_points": 0})
	var state := _json(MATCH)
	board.set_opponent_name("The 147")
	board.show_frame(frame, state)
	var match_width := strip.size.x
	board.set_exhibition(true)
	check(board.is_exhibition(), "on")
	check_eq(board.texts(), {}, "what the match's strip said is dropped")
	board.show_frame(frame, state)
	var view: Dictionary = frame["view"]
	check_eq(board.texts(), {"name": "The 147", "break": 0, "on": Scoreboard.on_text(frame),
			"remaining": int(view["points_remaining"])}, "the name, 0 before the first pot, the ball on and Remaining")
	check(strip.size.x < match_width, "one side only, narrower: %s < %s" % [strip.size.x, match_width])
	board.place_in(Rect2(0.0, 0.0, 1912.0, 880.0))
	check_near(strip.position.x + strip.size.x / 2.0, 956.0, 1.0, "centred on the screen")
	board.show_frame(_frame(1, {"break_points": 56}), state)
	check_eq(board.texts()["break"], 56, "its break as it grows")
	var width := strip.size.x
	board.show_frame(_frame(1, {"break_points": 140}), state)
	check_eq(strip.size.x, width, "one width through the break")
	board.set_exhibition(false)
	board.show_frame(frame, state)
	check(board.texts().has("you") and board.texts().has("best_of"), "a match's strip again: %s" % [board.texts()])
	check_near(strip.size.x, match_width, 0.5, "as wide as before")
	board.free()


func test_watching_a_147_the_scoreboard_follows_its_break_and_a_match_clears_it() -> void:
	var main: MainScript = await _spawn(_frame(1, {"break_points": 0}))
	var board: Scoreboard = main.get_node("Hud/Scoreboard")
	_engine.opponent_shots.append(_level_shot(THEIRS, "maximum"))
	(main.get_node("Hud/HomeScreen") as HomeScreen).button("watch").pressed.emit()
	var m: MatchController = main.get_node("Match")
	m.pace = 0.0
	check(board.is_exhibition(), "the scoreboard is the exhibition's")
	check_eq(board.texts().get("name"), "The 147", "named")
	check_eq(board.texts().get("break"), 0, "0 before the first pot")
	check(not board.texts().has("you"), "nothing of the person's: %s" % [board.texts()])
	await _until(main.is_playing, "the 147's stroke")
	_engine.opponent_shots.append(_level_shot(THEIRS, "maximum"))
	_engine.judgements.append(_judged(_frame(1, {"break_points": 8})))
	main.skip_shot()
	await _until_idle(main)
	check_eq(board.texts().get("break"), 8, "its break")
	m.abandon()

	_engine.frame_view = _frame(0)
	_engine.ball_list = (_engine.frame_view["balls"] as Array).duplicate(true)
	main.harness_start_match({"best_of": 3, "first_breaker": "you", "level": "beginner"}, false, false)
	check(not board.is_exhibition(), "a match clears it")
	check_eq(board.texts().get("you"), 24, "the person's score is back")
	await _finish(main)


func test_an_exhibitions_spin_and_power_show_as_it_lines_up_and_plays_and_nothing_else() -> void:
	var main: MainScript = await _spawn(_frame(1))
	var hud := main.get_node("Hud")
	var spin: SpinWidget = hud.get_node("SpinWidget")
	var pull: PullControl = hud.get_node("PullCue")
	_engine.opponent_shots.append(_level_shot(THEIRS, "maximum"))
	_engine.think_polls = 4
	main.harness_start_exhibition(0, false)
	var m: MatchController = main.get_node("Match")
	m.pace = 0.3
	_check_shown(main, [], "while the 147 thinks")
	await _until(func() -> bool: return main._lining_up, "the 147's cue on its line")
	_check_shown(main, ["SpinWidget", "PullCue"], "as it lines up")
	check_eq(Vector2(spin.get("_tip")), Vector2(0.2, -0.3), "the spin ball shows its tip")
	check_near(pull.shown_power(), PullControl.power_for_speed(2.5, Physics.param("max_cue_speed_m_s", 10.0)), 1e-9,
			"the power bar its power")
	for control: Control in [spin, pull]:
		check(control.mouse_filter == Control.MOUSE_FILTER_IGNORE and control.modulate.a < 1.0,
				"%s dimmed, taking no touch" % control.name)
	check(not (hud.get_node("CuePlacer") as CuePlacer).is_placing(), "no placer")
	check(not (main.get_node("AimControl") as AimControl).enabled, "no aiming")
	check_eq(m.nominations(), [] as Array[String], "nothing to nominate")
	await _until(main.is_playing, "its strike")
	_check_shown(main, ["SpinWidget", "PullCue"], "as it plays")
	_engine.judgements.append(_judged(_frame(1, {"break_points": 1})))
	_engine.opponent_shots.append(_level_shot(THEIRS, "maximum"))
	main.skip_shot()
	await _until_idle(main)
	_check_shown(main, [], "while it thinks about the next")
	m.abandon()
	await _finish(main)


func test_while_the_opponent_thinks_or_chooses_nothing_stale_shows_and_yours_comes_back() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main, "beginner")
	Settings.set_value("reset_spin_after_shot", false)
	main.setup.set_tip(-0.4, 0.1)
	main.setup.set_speed(1.2)
	var mine := main.setup.snapshot()
	var hud := main.get_node("Hud")
	var spin: SpinWidget = hud.get_node("SpinWidget")
	var pull: PullControl = hud.get_node("PullCue")
	var elevation: ElevationControl = hud.get_node("ElevationControl")
	_engine.opponent_shots.append(_level_shot(THEIRS))
	_engine.think_polls = 1000
	m.pace = 0.3
	_engine.judgements.append(_judged(_frame(1)))
	main.harness_match_shot(STRIKE, "", null)
	main.setup.restore(mine)
	main.skip_shot()
	await _until_idle(main)
	check(m.harness_state()["thinking"], "the level thinks")
	_check_shown(main, [], "while it thinks")
	check(not elevation.is_visible_in_tree(), "no elevation readout (\"%s\") left over" % elevation.readout_text())

	_engine.set("_polls_left", 0)
	await _until(func() -> bool: return main._lining_up, "its cue on the line")
	_check_shown(main, ["SpinWidget", "ElevationControl", "PullCue"], "its stroke lined up")
	check_eq(Vector2(spin.get("_tip")), Vector2(0.2, -0.3), "its tip")
	await _until(main.is_playing, "its strike")
	# It plays on: the power bar would otherwise keep this stroke's power while it thinks again.
	_engine.opponent_shots.append(_level_shot(THEIRS))
	_engine.think_polls = 1000
	_engine.judgements.append(_judged(_frame(1, {"break_points": 1})))
	main.skip_shot()
	await _until_idle(main)
	check(m.harness_state()["thinking"], "thinking about its next")
	_check_shown(main, [], "nothing of the last stroke")
	m.abandon()
	await _finish(main)

	# Choosing after the person's foul.
	main = await _spawn(_frame(0))
	m = _start(main, "beginner")
	m.pace = 1.0
	_engine.judgements.append(_judged(_frame(1, {"pending": "after_foul", "decider": 1, "offender": 0,
			"options": [[], ["play", "play_again"]]})))
	main.harness_match_shot(STRIKE, "", null)
	main.skip_shot()
	await _until_idle(main)
	check_eq(m.strike_blocked(), "opponent", "the opponent chooses")
	_check_shown(main, [], "while it chooses")
	m.abandon()
	await _finish(main)

	# And the person's own cue at their turn.
	main = await _spawn(_frame(0))
	m = _start(main, "beginner")
	Settings.set_value("reset_spin_after_shot", false)
	main.setup.set_tip(-0.4, 0.1)
	main.setup.set_speed(1.2)
	mine = main.setup.snapshot()
	_engine.opponent_shots.append(_level_shot(THEIRS))
	m.pace = 0.2
	_engine.judgements.append(_judged(_frame(1)))
	main.harness_match_shot(STRIKE, "", null)
	main.setup.restore(mine)
	main.skip_shot()
	await _until_idle(main)
	await _until(main.is_playing, "its strike")
	_engine.judgements.append(_judged(_frame(0)))
	main.skip_shot()
	await _until_idle(main)
	_check_shown(main, ["SpinWidget", "ElevationControl", "StrokeCue"], "the person's turn")
	check_eq(Vector2((main.get_node("Hud/SpinWidget") as SpinWidget).get("_tip")), Vector2(-0.4, 0.1), "their spin back")
	check_near(main.setup.speed_m_s, float(mine["speed_m_s"]), 1e-9, "their pace back")
	check_eq(main.setup.strike_mode, mine.get("strike_mode", ShotSetup.STROKE), "their strike mode")
	await _finish(main)


func test_shot_result_keeps_the_break_an_exhibition_ended_on() -> void:
	var main: MainScript = await _spawn(_frame(1, {"break_points": 140}))
	_engine.opponent_shots.append(_level_shot(THEIRS, "maximum"))
	main.harness_start_exhibition(0, false)
	var m: MatchController = main.get_node("Match")
	m.pace = 0.0
	await _until(main.is_playing, "the last black")
	var over := _frame(1, {"over": true, "winner": 1, "end_reason": "black", "break_points": 147,
			"stats": [{"highest_break": 0}, {"highest_break": 147}]})
	_engine.match_view["over"] = true
	var answer := _judged(over)
	answer["event"] = "match_ended"
	answer["judgement"]["points"] = 7
	_engine.judgements.append(answer)
	_mark_log()
	main.skip_shot()
	await _until_idle(main)
	check((main.get_node("Hud/ExhibitionSummary") as Control).visible, "its summary")
	var results := _logged("shot.result")
	check(results.size() == 1 and int(results[0]["break"]) == 147 and int(results[0]["points"]) == 7,
			"shot.result has the break it ended on: %s" % [results])
	main.harness_match_continue()
	await _finish(main)


func test_an_exhibition_has_nothing_to_concede_or_claim() -> void:
	var main: MainScript = await _spawn(_frame(1, {"view_options": [["concede_frame", "concede_match", "claim_frame"], []]}))
	_engine.opponent_shots.append(_level_shot(THEIRS, "maximum"))
	_engine.think_polls = 1000
	main.harness_start_exhibition(0, false)
	_mark_log()
	var paused := main.harness_match_pause(true)
	check_eq(paused, {"on": true, "can_concede": false, "can_concede_match": false, "can_claim": false},
			"the pause menu offers nothing to concede or claim")
	main.harness_match_pause(false)
	for kind: String in ["concede_frame", "concede_match", "claim_frame"]:
		var r := main.harness_match_choose(kind)
		check(r.has("error"), "%s refused: %s" % [kind, r])
	check_eq(_engine.called().count("decide"), 0, "the engine is not asked")
	check_eq(_errors(), [] as Array[Dictionary], "with no app.error")
	check((main.get_node("Match") as MatchController).is_exhibition(), "the 147 plays on")
	(main.get_node("Match") as MatchController).abandon()
	await _finish(main)


func test_a_levels_stroke_that_cant_be_played_names_the_level() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main, "beginner")
	m.pace = 0.0
	var broken := _level_shot(THEIRS)
	(broken["strike"] as Dictionary).erase("speed_m_s")
	_engine.opponent_shots.append(broken)
	_engine.judgements.append(_judged(_frame(1)))
	_mark_log()
	main.harness_match_shot(STRIKE, "", null)
	main.skip_shot()
	await _until_idle(main)
	await _until(func() -> bool: return _engine.called().count("opponent_think") >= 2, "the level asked again")
	var said: Array = _errors().map(func(line: Dictionary) -> String: return str(line.get("message", "")))
	var named := said.filter(func(text: String) -> bool: return text.contains("the level \"beginner\"'s stroke can't be played"))
	check_eq(named.size(), 1, "the level named: %s" % [said])
	check(said.all(func(text: String) -> bool: return not text.begins_with("main: the stand-in")), "not the stand-in: %s" % [said])
	m.abandon()
	await _finish(main)


func test_the_camera_is_given_the_hud_safe_area() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var rig: CameraRig = main.get_node("CameraRig")
	var safe := SettingsPanel.canvas_safe_rect(main.get_node("Hud/Scoreboard") as Control)
	check(Rect2(rig.get("_safe")).is_equal_approx(safe), "the rig has the HUD's safe area: %s, %s" % [rig.get("_safe"), safe])
	await _finish(main)


## Which stroke controls show (the rest hidden).
func _check_shown(main: MainScript, shown: Array, when: String) -> void:
	for control_name: String in ["SpinWidget", "ElevationControl", "FineTuneWheel", "StrokeCue", "PullCue"]:
		check_eq((main.get_node("Hud/" + control_name) as Control).visible, shown.has(control_name),
				"%s: %s shown" % [when, control_name])


func _spawn(frame: Dictionary) -> MainScript:
	for key: String in Settings.DEFAULTS:
		Settings.set_value(key, Settings.DEFAULTS[key])
	var main := load("res://scenes/main.tscn").instantiate() as MainScript
	tree.root.add_child(main)
	await tree.process_frame
	_engine = FakeMatchEngine.new()
	_engine.frame_view = frame
	_engine.match_view = _json(MATCH)
	_engine.match_view.erase("frame")
	_engine.ball_list = (frame["balls"] as Array).duplicate(true)
	(main.get_node("Match") as MatchController).engine = _engine
	main.choose_among = func(_balls: Array[Dictionary], _cue_ball: int, _places: Array) -> Dictionary: return {}
	_mark_log()
	return main


func _start(main: MainScript, level: String) -> MatchController:
	var hud := main.get_node("Hud")
	(hud.get_node("HomeScreen") as HomeScreen).button("play").pressed.emit()
	var screen: MatchSetupScreen = hud.get_node("MatchSetupScreen")
	(screen.control("level_" + level) as Button).button_pressed = true
	(screen.control("start") as Button).pressed.emit()
	var m: MatchController = main.get_node("Match")
	check(m.is_active() and m.level() == level, "the match started against %s" % level)
	main._flush_refresh()
	(main.get_node("CameraRig") as CameraRig)._process(CameraRig.RETURN_S + CameraRig.BLEND_S)
	return m


func _level_shot(strike: Dictionary, level: String = "beginner") -> Dictionary:
	return {"ok": true, "error": "", "places_cue": false, "cue": [], "nominated": "", "strike": strike.duplicate(),
			"why": {"kind": "pot", "level": level}, "us": 120, "level": level, "meant": strike.duplicate(), "think": THINK.duplicate()}


## The example frame view with nothing pending and `striker` to play; `changes` replaces frame keys,
## "options" the view's and "view_options" the view's too.
func _frame(striker: int, changes: Dictionary = {}) -> Dictionary:
	var frame := _json(FRAME_VIEW)
	frame["balls"] = HarnessScript.game_balls(frame["balls"])["balls"]
	frame["pending"] = "none"
	frame["striker"] = striker
	frame["free_ball"] = false
	frame["view"]["nominations"] = []
	frame["view"]["options"] = [["concede_frame", "concede_match"], []]
	for key: String in changes:
		if key == "options" or key == "view_options":
			frame["view"]["options"] = changes[key]
		else:
			frame[key] = changes[key]
	return frame


func _judged(frame: Dictionary) -> Dictionary:
	var clean := {"fouls": [], "penalty": 0, "miss": false, "miss_reason": "", "points": 0, "nominated": "",
			"free_ball_used": false, "spotted": [], "calls": []}
	return {"ok": true, "error": "", "judgement": clean, "event": "none", "frame": frame}


func _finish(main: MainScript) -> void:
	await _until(func() -> bool: return not Perf._active.has(&"cue_strike"), "the cue's strike to end")
	main.free()


func _until_idle(main: MainScript) -> void:
	for i in 120:
		if not main.harness_busy():
			return
		await tree.process_frame
	failures.append("still busy after 120 frames")


func _until(done: Callable, what: String) -> void:
	var deadline := Time.get_ticks_msec() + 3000
	while not done.call():
		if Time.get_ticks_msec() > deadline:
			failures.append("no %s within 3 s" % what)
			return
		await tree.process_frame


func _json(path: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not (parsed is Dictionary):
		check(false, "%s is missing or not an object; run scripts/sync_data.py" % path)
		return {}
	return parsed


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


func _logged(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append((parsed as Dictionary).get("data", {}))
	return found


func _errors() -> Array[Dictionary]:
	return _logged("app.error")
