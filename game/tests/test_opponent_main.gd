extends TestCase
## The computer opponent on the real table scene (spec 10.5, 10.6, 11) with a scripted engine: the
## level chosen at the setup, its name on every surface, its cue standing, feathering and striking
## on OpponentShow's clock, its strike on the dimmed controls, the views to watch it from, Watch a
## 147 from Home to its summary, the overlay's line and the harness's commands for all of it.

const MainScript := preload("res://scripts/main.gd")
const HarnessScript := preload("res://scripts/autoload/harness.gd")
const FRAME_VIEW := "res://data/rules/example_frame_view.json"
const MATCH := "res://data/rules/example_match.json"
const CUE := 0
const STRIKE := {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.0}
const THEIRS := {"aim_rad": 0.35, "tip_x": 0.2, "tip_y": -0.3, "elevation_rad": 0.05, "speed_m_s": 2.5}
const THINK := {"candidates": 12, "sims": 40, "steps": 120, "depth": 0, "stopped": "done", "ms": 412.0}

var _engine: FakeMatchEngine
var _log_start := 0


func test_the_setup_offers_the_levels_and_the_one_chosen_plays() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var hud := main.get_node("Hud")
	(hud.get_node("HomeScreen") as HomeScreen).button("play").pressed.emit()
	var setup_screen: MatchSetupScreen = hud.get_node("MatchSetupScreen")
	check(setup_screen.control("level_beginner") != null and setup_screen.control("level_maximum") != null,
			"the engine's levels are offered")
	check_eq((setup_screen.control("level_beginner") as Button).text, "Beginner", "by name")
	(setup_screen.control("level_maximum") as Button).button_pressed = true
	(setup_screen.control("start") as Button).pressed.emit()
	var m: MatchController = main.get_node("Match")
	check_eq(_last_call("start")[1].get("level"), "maximum", "the level chosen goes to the engine")
	check_eq([m.level(), m.level_name()], ["maximum", "The 147"], "and is the match's")
	check_eq(main.harness_match_state()["level"], "maximum", "match_state says so")

	# Opened again, the list is asked for again and the choice kept.
	main.harness_open_screen("match_setup")
	_engine.level_list = [{"id": "beginner", "name": "Beginner", "tier": 1, "maximum": false}]
	main.harness_open_screen("home")
	(hud.get_node("HomeScreen") as HomeScreen).button("play").pressed.emit()
	check(setup_screen.control("level_maximum") == null and setup_screen.config()["level"] == "beginner",
			"each opening lists the levels as they are now: %s" % [setup_screen.config()])
	await _finish(main)


func test_the_opponents_name_is_on_every_surface() -> void:
	var main: MainScript = await _spawn(_frame(0))
	_start(main, "beginner")
	var hud := main.get_node("Hud")
	var board: Scoreboard = hud.get_node("Scoreboard")
	check_eq(board.get_node("Strip").get("opponent"), "Beginner", "the scoreboard")
	check(_shows(hud.get_node("FrameSummary"), "Beginner"), "the frame summary's column")
	check(_shows(hud.get_node("MatchSummary"), "Beginner"), "the match summary's")
	var strip: ChoiceStrip = hud.get_node("ChoiceStrip")
	var fouled := _frame(0, {"last_penalty": 4})
	strip.offer(["play", "play_again"] as Array[String], fouled)
	check_eq(strip.prompt(), "Beginner fouled: 4 to you", "the choice strip's question")
	check_eq(strip.button("play_again").text, "Beginner plays again", "and its button")
	strip.dismiss()
	var referee: RefereeLine = hud.get_node("RefereeLine")
	referee.announce([{"kind": "frame_over", "detail": "awarded", "value": 1}], 0)
	check_eq(referee.text(), "Frame awarded to Beginner", "the referee's line")
	(main.get_node("Match") as MatchController).opponent_chose.emit("play_again")
	check_eq((hud.get_node("Notice") as Label).text, "Beginner puts you back in", "the notice of its choice")
	var pause: PauseMenu = hud.get_node("PauseMenu")
	board.pause_button().pressed.emit()
	pause.button("concede_frame").pressed.emit()
	check(_shows(pause, "Beginner wins the frame"), "the pause menu's question")
	pause.button("cancel").pressed.emit()
	pause.button("resume").pressed.emit()
	await _finish(main)


func test_the_opponents_cue_feathers_on_the_shows_clock_and_strikes_when_it_is_due() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main, "beginner")
	var show: OpponentShow = main.get_node("OpponentShow")
	var cue: CueView = main.get_node("Cue")
	_engine.opponent_shots.append(_level_shot(THEIRS))
	m.pace = 0.5
	_mark_log()
	await _person_stroke(main, _frame(1))
	await _until(show.is_showing, "the show")
	var began := Time.get_ticks_msec()
	check(not main.is_playing() and main.is_processing(), "the cue stands on the line, posed each frame")
	check(Perf._active.has(&"opponent_show"), "drawing while it moves")
	var pulled := 0.0
	var followed := true
	while show.is_showing() and Time.get_ticks_msec() - began < 3000:
		await tree.process_frame
		if show.is_showing() and not main.is_playing():
			# The scene poses the cue before the show's clock moves on in the same frame.
			main._process(0.0)
			followed = followed and is_equal_approx(float(cue.get("_backswing")), show.backswing_m())
			pulled = maxf(pulled, float(cue.get("_backswing")))
	check(followed, "the cue's backswing follows OpponentShow")
	check(pulled > OpponentShow.FEATHER_PULL_M * 0.5, "and is drawn back: %f m" % pulled)
	await _until(main.is_playing, "the strike")
	var waited := Time.get_ticks_msec() - began
	var show_ms := _show_seconds() * m.pace * 1000.0
	check(waited >= show_ms - 60.0, "struck once the show is over (%d ms of %d)" % [waited, show_ms])
	var inputs := _logged("shot.input")
	check(inputs.size() == 2 and inputs[1]["strike_mode"] == "opponent", "struck as the opponent's: %s" % [inputs])
	var simulated := _logged("shot.simulated")
	if simulated.size() == 2:
		for key: String in THEIRS:
			check_eq(simulated[1]["bundle"]["strike"][key], THEIRS[key], "exactly as given: %s" % key)
	_engine.judgements.append(_judged(_frame(0)))
	main.skip_shot()
	await _until_idle(main)
	await _finish(main)


func test_the_wait_before_a_levels_stroke_is_its_thinking_pause_and_the_show() -> void:
	check_near(MatchController.LEVEL_THINK_SECONDS + _show_seconds(), 1.6, 0.1,
			"about 1.6 s at pace 1")
	check(OpponentShow.FEATHERS == 2, "two feathers")
	check(MatchController.THINK_SECONDS == 0.9, "the stand-in's pause as it was")


func test_pause_holds_the_show_leaving_cancels_it_and_fast_skips_it() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main, "beginner")
	var show: OpponentShow = main.get_node("OpponentShow")
	var board: Scoreboard = main.get_node("Hud/Scoreboard")
	_engine.opponent_shots.append(_level_shot(THEIRS))
	m.pace = 0.5
	await _person_stroke(main, _frame(1))
	await _until(show.is_showing, "the show")
	board.pause_button().pressed.emit()
	var held := show.backswing_m()
	for i in 40:
		await tree.process_frame
	check(show.is_showing() and not main.is_playing(), "the pause menu holds it")
	check_eq(show.backswing_m(), held, "where it was")
	check(not Perf._active.has(&"opponent_show"), "and nothing is drawn for it behind the menu")
	(main.get_node("Hud/PauseMenu") as PauseMenu).button("resume").pressed.emit()
	await _until(main.is_playing, "the strike once resumed")
	_engine.judgements.append(_judged(_frame(1)))
	_engine.opponent_shots.append(_level_shot(THEIRS))
	main.skip_shot()
	await _until_idle(main)
	await _until(show.is_showing, "the next show")
	main.harness_open_screen("home")
	check(not show.is_showing() and not show.is_processing(), "leaving the match cancels it")
	check_eq(_engine.called().count("judge"), 2, "with nothing struck")
	for i in 3:
		await tree.process_frame
	check(not main.is_processing() and not Perf._active.has(&"opponent_show"), "and nothing keeps running")

	# A fast run has no show.
	_engine.frame_view = _frame(0)
	_engine.opponent_shots.append(_level_shot(THEIRS))
	_engine.judgements.append(_judged(_frame(1)))
	main.harness_start_match({"best_of": 1, "first_breaker": "you", "level": "beginner"}, false, true)
	main.harness_match_shot(STRIKE, "", null)
	await _until(func() -> bool: return _engine.called().count("judge") >= 4 or main.is_playing(), "the fast stroke")
	check(not show.is_showing(), "fast strikes with no show")
	await _finish(main)


func test_its_strike_shows_on_the_dimmed_spin_ball_and_power_bar_and_yours_comes_back() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main, "beginner")
	# The person's spin kept after their stroke, so it is theirs to see come back.
	Settings.set_value("reset_spin_after_shot", false)
	main.setup.set_tip(-0.4, 0.1)
	main.setup.set_speed(1.2)
	var mine := main.setup.snapshot()
	var hud := main.get_node("Hud")
	var spin: SpinWidget = hud.get_node("SpinWidget")
	var pull: PullControl = hud.get_node("PullCue")
	var stroke: StrokeControl = hud.get_node("StrokeCue")
	check_eq(main.setup.strike_mode, ShotSetup.STROKE, "the person strikes with the finger stroke")
	_engine.opponent_shots.append(_level_shot(THEIRS))
	m.pace = 0.5
	_engine.judgements.append(_judged(_frame(1)))
	main.harness_match_shot(STRIKE, "", null)
	main.setup.restore(mine)
	main.skip_shot()
	await _until_idle(main)
	await _until(func() -> bool: return main._lining_up, "the opponent's cue on its line")
	check_eq(Vector2(spin.get("_tip")), Vector2(0.2, -0.3), "the spin ball shows its tip")
	check(pull.visible and not stroke.visible, "the power bar shows in place of the finger stroke")
	var max_speed := Physics.param("max_cue_speed_m_s", 10.0)
	check_near(pull.shown_power(), PullControl.power_for_speed(2.5, max_speed), 1e-9, "at its power")
	for control: Control in [spin, pull]:
		check(control.mouse_filter == Control.MOUSE_FILTER_IGNORE and control.modulate.a < 1.0,
				"%s is dimmed and takes no touch" % control.name)
	await _until(main.is_playing, "its strike")
	check(pull.visible and pull.shown_power() >= 0.0 and Vector2(spin.get("_tip")) == Vector2(0.2, -0.3), "both stay while it plays")
	_engine.judgements.append(_judged(_frame(0)))
	main.skip_shot()
	await _until_idle(main)
	check(pull.shown_power() < 0.0 and not pull.visible and stroke.visible, "at the person's turn the power bar goes")
	check_eq(Vector2(spin.get("_tip")), Vector2(-0.4, 0.1), "and their own spin is back")
	check_eq(main.setup.strike_mode, ShotSetup.STROKE, "with their strike mode")
	check_near(main.setup.speed_m_s, mine["speed_m_s"], 1e-9, "and their pace")
	await _finish(main)


func test_the_watch_button_shows_through_the_opponents_turn_and_steps_the_views() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main, "beginner")
	var hud := main.get_node("Hud")
	var button: WatchButton = hud.get_node("WatchButton")
	var rig: CameraRig = main.get_node("CameraRig")
	var board: Scoreboard = hud.get_node("Scoreboard")
	check(not button.visible, "not at the person's turn")
	_engine.opponent_shots.append(_level_shot(THEIRS))
	m.pace = 1.0
	_mark_log()
	await _person_stroke(main, _frame(1))
	check(button.visible, "the opponent's turn shows it")
	var canvas := main.get_viewport().get_visible_rect().size
	var at := Rect2(button.position, button.size)
	var safe := SettingsPanel.canvas_safe_rect(button)
	check(safe.encloses(at), "inside the safe area: %s in %s" % [at, safe])
	check(at.position.x > board.covered_rect().end.x and at.position.y < canvas.y / 4.0,
			"top right, clear of the scoreboard and its pause button: %s, board %s" % [at, board.covered_rect()])
	var cue_rect := Rect2((hud.get_node("PullCue") as Control).position, (hud.get_node("PullCue") as Control).size)
	check(at.end.x <= cue_rect.position.x, "and of the power cue")
	var cleared: Array = rig.get("_keep_clear")
	check(cleared.has(at), "the camera keeps the table clear of it")

	button.pressed.emit()
	check_eq(rig.watch(), "above", "a tap: the table from above")
	check_eq(m.waiting(), "opponent", "the opponent still at the table")
	check_eq(main.harness_match_state()["watch"], "above", "match_state says so")
	rig.pinch(2.0)
	_land(main)
	var before := rig.transform
	(main.get_node("Gestures") as Gestures).drag_moved.emit(Vector2(900.0, 400.0), Vector2(120.0, 40.0))
	_land(main)
	check(not rig.transform.is_equal_approx(before), "a drag pans it")
	button.pressed.emit()
	check_eq(rig.watch(), "round", "again: walking round")
	_land(main)
	before = rig.transform
	(main.get_node("Gestures") as Gestures).drag_moved.emit(Vector2(900.0, 400.0), Vector2(200.0, 0.0))
	_land(main)
	check(not rig.transform.is_equal_approx(before), "a drag walks round")
	var r := main.harness_watch("above")
	check_eq(r, {"view": "above"}, "the harness chooses a view")

	await _until(main.is_playing, "the opponent's stroke")
	_engine.judgements.append(_judged(_frame(0)))
	main.skip_shot()
	await _until_idle(main)
	check_eq(rig.watch(), "cue", "the person's turn brings the view back behind the cue")
	check(not button.visible, "and the button goes")
	check(main.harness_watch("above").has("error"), "the harness can't watch with nobody to watch")
	var lines := _logged("camera.watch")
	var said: Array = lines.map(func(line: Dictionary) -> Array: return [line["view"], line["how"]])
	check_eq(said, [["above", "button"], ["round", "button"], ["above", "harness"], ["cue", "turn"]],
			"camera.watch for each change, with how")
	for line in lines:
		check_eq(line.get("match_id"), m.match_id(), "with the match")
		_check_schema("camera.watch", line)
	await _finish(main)


func test_a_stroke_watched_from_above_is_watched_whole() -> void:
	var pocket: Dictionary = {}
	for each: Dictionary in Physics.table_shape()["pockets"]:
		if bool(each.get("corner", true)):
			pocket = each
			break
	var drop := Vector2(float(pocket["drop_centre"][0]), float(pocket["drop_centre"][1]))
	var cue_xy := Vector2(0.3, 0.0)
	var way := (drop - cue_xy).normalized()
	var red := drop - way * 0.45
	var frame := _frame(1)
	frame["balls"] = [{"ball": "cue", "index": 0, "x": cue_xy.x, "y": cue_xy.y}, {"ball": "red", "index": 1, "x": red.x, "y": red.y}]
	var main: MainScript = await _spawn(frame)
	var pot := {"aim_rad": way.angle(), "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.5}
	_engine.opponent_shots.append(_level_shot(pot))
	_engine.opponent_shots.append(_level_shot(pot))
	var hud := main.get_node("Hud")
	_mark_log()
	var m := _start(main, "beginner")
	m.pace = 0.2
	var button: WatchButton = hud.get_node("WatchButton")
	check(button.visible, "the opponent breaks: the button shows")
	button.pressed.emit()
	await _until(main.is_playing, "its stroke")
	var cameras := _logged("shot.camera")
	check(cameras.size() == 1 and cameras[0]["sequence"] == "broadcast" and cameras[0].get("reason") == "watching",
			"a pot watched from above stays on the whole table: %s" % [cameras])
	await _finish(main)


func test_the_person_can_idle_after_the_opponents_visit() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main, "beginner")
	_engine.opponent_shots.append(_level_shot(THEIRS))
	m.pace = 0.3
	await _person_stroke(main, _frame(1))
	(main.get_node("Hud/WatchButton") as WatchButton).pressed.emit()
	await _until(main.is_playing, "the opponent's stroke")
	_engine.judgements.append(_judged(_frame(0)))
	main.skip_shot()
	await _until_idle(main)
	var rig: CameraRig = main.get_node("CameraRig")
	var frames := 0
	while rig.is_easing() and frames < 300:
		rig._process(1.0 / 60.0)
		frames += 1
	for i in 3:
		await tree.process_frame
	var show: OpponentShow = main.get_node("OpponentShow")
	check(m.person_to_play(), "the person's turn")
	check(not main.is_processing() and not show.is_processing(), "the scene does nothing a frame")
	check(not (m.get_node("OpponentDriver") as Node).is_processing(), "nor does the opponent")
	for reason: StringName in [&"opponent_show", &"camera", &"shot", &"cue_strike"]:
		check(not Perf._active.has(reason), "nothing keeps drawing: %s" % [Perf._active.keys()])
	check(not (main.get_node("Hud/WatchButton") as Control).visible, "no view button")
	await _finish(main)


func test_watch_a_147_from_home_to_its_summary_again_and_home() -> void:
	var main: MainScript = await _spawn(_frame(1))
	_engine.exhibition_names = ["open 1", "open 2"] as Array[String]
	var hud := main.get_node("Hud")
	var home: HomeScreen = hud.get_node("HomeScreen")
	var m: MatchController = main.get_node("Match")
	_engine.opponent_shots.append(_level_shot(THEIRS, "maximum"))
	_mark_log()
	home.button("watch").pressed.emit()
	check(m.is_active() and m.is_exhibition(), "Watch a 147 starts an exhibition")
	var asked: Dictionary = _last_call("start")[1]
	check_eq([asked.get("level"), asked.get("best_of"), asked.get("first_breaker")], ["maximum", 1, 1], "the 147, at the table")
	var first := int(asked.get("exhibition", -1))
	check(first == 0 or first == 1, "at one of the tables: %d" % first)
	m.pace = 0.2
	check_eq(main.harness_state()["screen"], "match", "the match's screen")
	check((hud.get_node("Scoreboard") as Control).visible, "with the scoreboard")
	check_eq((hud.get_node("Scoreboard") as Scoreboard).get_node("Strip").get("opponent"), "The 147", "named")
	check((hud.get_node("WatchButton") as Control).visible, "and the view button")
	for hidden: String in ["SpinWidget", "ElevationControl", "StrokeCue", "PullCue", "FineTuneWheel"]:
		check(not (hud.get_node(hidden) as Control).visible, "no %s" % hidden)
	check(not (hud.get_node("CuePlacer") as CuePlacer).is_placing(), "no cue placer")
	check(not (main.get_node("AimControl") as AimControl).enabled, "no aiming")

	var pause: PauseMenu = hud.get_node("PauseMenu")
	(hud.get_node("Scoreboard") as Scoreboard).pause_button().pressed.emit()
	for id: String in ["concede_frame", "concede_match", "claim_frame", "settings"]:
		check(not pause.button(id).is_visible_in_tree(), "the pause menu has no %s" % id)
	check(pause.button("resume").is_visible_in_tree() and pause.button("quit").is_visible_in_tree(), "only Resume and Quit")
	pause.button("resume").pressed.emit()

	await _until(main.is_playing, "the 147's stroke")
	_engine.judgements.append(_judged(_frame(0, {"break_points": 0, "stats": [{"highest_break": 0}, {"highest_break": 8}]})))
	main.skip_shot()
	await _until_idle(main)
	var summary: ExhibitionSummary = hud.get_node("ExhibitionSummary")
	check(summary.visible, "the break over, its summary shows")
	check_eq(summary.texts()[0], "Break 8", "with the break: %s" % [summary.texts()])
	check_eq(main.harness_state()["screen"], "exhibition_summary", "the screen")
	check_eq(main.harness_match_waiting(), {"waiting": "exhibition_summary", "person": true}, "the harness waits on it")
	check(not (hud.get_node("Scoreboard") as Control).visible and not (hud.get_node("WatchButton") as Control).visible,
			"over the table, with nothing else")
	var rig: CameraRig = main.get_node("CameraRig")
	check(rig.is_held() and rig.watch() == "cue", "from the full-table view")
	check_eq(main.harness_state()["mode"], "match", "the table it ended on stays")
	var ended := _logged("exhibition.ended")
	check(ended.size() == 1 and ended[0]["why"] == "missed" and int(ended[0]["break_points"]) == 8, "logged: %s" % [ended])

	_engine.frame_view = _frame(1)
	_engine.opponent_shots.append(_level_shot(THEIRS, "maximum"))
	summary.control("again").pressed.emit()
	check(m.is_exhibition() and not summary.visible, "Again starts another")
	check_eq(int(_last_call("start")[1].get("exhibition", -1)), 1 - first, "on the other table")
	(hud.get_node("Scoreboard") as Scoreboard).pause_button().pressed.emit()
	_mark_log()
	pause.button("quit").pressed.emit()
	check(home.visible and not summary.visible and not m.is_active(), "Quit goes Home at once, with no summary")
	ended = _logged("exhibition.ended")
	check(ended.size() == 1 and ended[0]["why"] == "left", "logged as left: %s" % [ended])

	_engine.frame_view = _frame(1)
	_engine.opponent_shots.append(_level_shot(THEIRS, "maximum"))
	home.button("watch").pressed.emit()
	m.pace = 0.2
	await _until(main.is_playing, "the 147's stroke")
	_engine.judgements.append(_judged(_frame(0)))
	main.skip_shot()
	await _until_idle(main)
	check(summary.visible, "the summary again")
	summary.control("home").pressed.emit()
	check(home.visible and not summary.visible and main.harness_state()["mode"] == "home", "Home goes home")
	check_eq(_errors().size(), 0, "with no app.error: %s" % [_errors()])
	await _finish(main)


func test_the_overlay_shows_the_opponents_last_decision() -> void:
	check_eq(DebugOverlay.think_text("beginner", THINK), "beginner 412 ms  40 sims  120 steps  done", "one line")
	check_eq(DebugOverlay.think_text("", THINK), "", "none for the stand-in")
	check_eq(DebugOverlay.think_text("maximum", null), "", "nor without a think")
	var text := DebugOverlay.format_text({}, -1.0, -1.0, "abc", "", "off", "dev", "")
	check(text.contains("think    -"), "nothing yet")
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main, "beginner")
	var overlay: DebugOverlay = main.get_node("Hud/Overlay")
	overlay.set_shown(true, "harness")
	_engine.opponent_shots.append(_level_shot(THEIRS))
	await _person_stroke(main, _frame(1))
	await _until(main.is_playing, "the opponent's stroke")
	overlay._refresh()
	check(overlay.shown_text().contains("think    beginner 412 ms  40 sims  120 steps  done"),
			"the overlay has it:\n%s" % overlay.shown_text())
	overlay.set_shown(false, "harness")
	main.skip_shot()
	m.abandon()
	await _finish(main)


func test_the_harness_drives_levels_exhibitions_and_the_views() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var harness: Node = HarnessScript.new()
	harness.scene_provider = func() -> Node: return main
	tree.root.add_child(harness)
	var m: MatchController = main.get_node("Match")

	var r: Dictionary = await harness.run_command("start_match", {"level": 3})
	check(not r["ok"] and str(r["error"]).contains("level"), "a level that is no id: %s" % r)
	r = await harness.run_command("start_match", {"best_of": 1, "first_breaker": "you", "level": "beginner", "seed": 5})
	check(r["ok"] and m.level() == "beginner", "start_match with a level: %s" % r)
	check_eq(_last_call("start")[1].get("level"), "beginner", "given to the engine")
	r = await harness.run_command("start_match", {"best_of": 1, "first_breaker": "you", "level": ""})
	check(r["ok"] and m.level() == "" and not _last_call("start")[1].has("level"), "\"\" is the stand-in: %s" % r)
	r = await harness.run_command("match_state", {})
	for key: String in ["level", "exhibition", "thinking", "watch"]:
		check(r["ok"] and (r["result"] as Dictionary).has(key), "match_state has %s" % key)
	r = await harness.run_command("watch", {"view": "above"})
	check(not r["ok"] and str(r["error"]).contains("not at the table"), "nobody to watch: %s" % r)
	r = await harness.run_command("watch", {"view": "sideways"})
	check(not r["ok"] and str(r["error"]).contains("cue"), "an unknown view: %s" % r)
	r = await harness.run_command("watch", {})
	check(not r["ok"], "view is needed")

	_engine.frame_view = _frame(1)
	r = await harness.run_command("start_exhibition", {"layout": 3})
	check(not r["ok"] and str(r["error"]).contains("exhibition tables"), "a table that isn't there: %s" % r)
	r = await harness.run_command("start_exhibition", {"layout": -1})
	check(not r["ok"], "nor a negative one")
	_engine.opponent_shots.append(_level_shot(THEIRS, "maximum"))
	r = await harness.run_command("start_exhibition", {"seed": 9, "fast": true})
	check(r["ok"] and r["result"] == {"match_id": m.match_id(), "layout": "open 1"}, "start_exhibition: %s" % r)
	check_eq([_last_call("start")[1].get("exhibition"), _last_call("start")[1].get("seed")], [0, 9], "table 0 by default, the seed")
	r = await harness.run_command("watch", {"view": "round"})
	check(r["ok"] and r["result"] == {"view": "round"}, "watch: %s" % r)
	r = await harness.run_command("match_state", {})
	check(r["ok"] and r["result"]["exhibition"] == true and r["result"]["watch"] == "round", "match_state: %s" % [r.get("error")])
	_engine.judgements.append(_judged(_frame(0)))
	r = await harness.run_command("match_wait", {"timeout_s": 5})
	check(r["ok"] and r["result"]["waiting"] == "exhibition_summary", "match_wait returns at its summary: %s" % r)
	r = await harness.run_command("match_continue", {})
	check(r["ok"] and r["result"] == {"screen": "home"}, "match_continue goes home: %s" % r)

	var shown: Dictionary = await harness._show_demo_screen("exhibition_summary")
	var summary: ExhibitionSummary = main.get_node("Hud/ExhibitionSummary")
	check(shown["ok"] and summary.visible and summary.texts()[0] == "147", "the demo screen: %s %s" % [shown, summary.texts()])
	check_eq(main.harness_state()["screen"], "exhibition_summary", "its screen")
	summary.control("home").pressed.emit()
	check(not summary.visible and main.harness_state()["screen"] == "home", "its Home closes it")
	check(HarnessScript.parse_args(PackedStringArray(["--demo-screen=exhibition_summary"]), PackedStringArray())["invalid"].is_empty(),
			"--demo-screen takes it")
	harness.free()
	await _finish(main)


func test_the_pause_menus_watching_form_quits_at_once() -> void:
	var pause := PauseMenu.new()
	tree.root.add_child(pause)
	var quits := [0]
	pause.quit_confirmed.connect(func() -> void: quits[0] += 1)
	pause.open(true, true, true, true)
	for id: String in ["concede_frame", "concede_match", "claim_frame", "settings"]:
		check(not pause.button(id).is_visible_in_tree(), "watching, no %s" % id)
	pause.button("quit").pressed.emit()
	check_eq(quits[0], 1, "Quit leaves at once: nothing is lost")
	pause.open(true, true, false)
	check(pause.button("concede_frame").is_visible_in_tree() and pause.button("settings").is_visible_in_tree(),
			"a match's menu has them all back")
	pause.button("quit").pressed.emit()
	check(quits[0] == 1 and pause.confirming() == "quit", "and its Quit asks first")
	pause.set_opponent_name("The 147")
	pause.button("cancel").pressed.emit()
	pause.button("concede_match").pressed.emit()
	check(_shows(pause, "The 147 wins the match"), "the question names the opponent")
	pause.free()


func test_the_power_bar_shows_a_power_it_is_given_and_nothing_else_changes() -> void:
	var pull := PullControl.new()
	var setup := ShotSetup.new()
	pull.bind(setup)
	pull.size = Vector2(120.0, 800.0)
	tree.root.add_child(pull)
	var before := [setup.power, setup.speed_m_s, setup.strike_mode]
	check_eq(pull.shown_power(), -1.0, "nothing shown at first")
	pull.show_power(0.6)
	check_eq(pull.shown_power(), 0.6, "a power shown")
	check_eq(pull.power(), setup.power, "the readout's power is still the person's")
	pull.show_power(1.7)
	check_eq(pull.shown_power(), 1.0, "at most full power")
	pull.show_power(-1.0)
	check_eq(pull.shown_power(), -1.0, "cleared")
	check_eq([setup.power, setup.speed_m_s, setup.strike_mode], before, "the setup untouched")
	pull.free()


## The scene at Home with a scripted engine on its MatchController that will show `frame` when a
## match starts.
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


## Home, Play, the level, Start; the camera landed.
func _start(main: MainScript, level: String) -> MatchController:
	var hud := main.get_node("Hud")
	(hud.get_node("HomeScreen") as HomeScreen).button("play").pressed.emit()
	var screen: MatchSetupScreen = hud.get_node("MatchSetupScreen")
	(screen.control("level_" + level) as Button).button_pressed = true
	(screen.control("start") as Button).pressed.emit()
	var m: MatchController = main.get_node("Match")
	check(m.is_active() and m.level() == level, "the match started against %s" % level)
	_land(main)
	return m


func _person_stroke(main: MainScript, after: Dictionary) -> void:
	_engine.judgements.append(_judged(after))
	var shot: Dictionary = main.harness_match_shot(STRIKE, "", null)
	check(shot.has("shot_id"), "the person's stroke: %s" % shot)
	main.skip_shot()
	await _until_idle(main)


func _land(main: MainScript) -> void:
	main._flush_refresh()
	(main.get_node("CameraRig") as CameraRig)._process(CameraRig.RETURN_S + CameraRig.BLEND_S)


func _level_shot(strike: Dictionary, level: String = "beginner") -> Dictionary:
	return {"ok": true, "error": "", "places_cue": false, "cue": [], "nominated": "", "strike": strike.duplicate(),
			"why": {"kind": "pot", "level": level}, "us": 120, "level": level, "meant": strike.duplicate(), "think": THINK.duplicate()}


func _frame(striker: int, changes: Dictionary = {}) -> Dictionary:
	var frame := _json(FRAME_VIEW)
	frame["balls"] = HarnessScript.game_balls(frame["balls"])["balls"]
	frame["pending"] = "none"
	frame["striker"] = striker
	frame["free_ball"] = false
	frame["view"]["nominations"] = []
	frame["view"]["options"] = [["concede_frame", "concede_match"], []]
	for key: String in changes:
		frame[key] = changes[key]
	return frame


func _judged(frame: Dictionary) -> Dictionary:
	var clean := {"fouls": [], "penalty": 0, "miss": false, "miss_reason": "", "points": 0, "nominated": "",
			"free_ball_used": false, "spotted": [], "calls": []}
	return {"ok": true, "error": "", "judgement": clean, "event": "none", "frame": frame}


func _show_seconds() -> float:
	return OpponentShow.STAND_S + OpponentShow.FEATHERS * OpponentShow.FEATHER_S + OpponentShow.BACKSWING_S + OpponentShow.HOLD_S


## Frees the scene once its cue's strike is over: CueView leaves Perf active when freed mid-strike.
func _finish(main: MainScript) -> void:
	await _until(func() -> bool: return not Perf._active.has(&"cue_strike"), "the cue's strike to end")
	main.free()


## Whether a Label under `node` says `text`.
func _shows(node: Node, text: String) -> bool:
	for label in node.find_children("*", "Label", true, false):
		if (label as Label).text == text:
			return true
	return false


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


func _check_schema(event: String, data: Dictionary) -> void:
	var schema: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://data/log_schema.json"))
	var fields: Dictionary = schema["events"][event]["fields"]
	for field: String in fields:
		if fields[field].get("required", false):
			check(data.has(field), "%s has its required %s" % [event, field])
	for field: String in data:
		check(fields.has(field), "%s's %s is in the schema" % [event, field])


func _json(path: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not (parsed is Dictionary):
		check(false, "%s is missing or not an object; run scripts/sync_data.py" % path)
		return {}
	return parsed


func _last_call(method: String) -> Array:
	for i in range(_engine.calls.size() - 1, -1, -1):
		if _engine.calls[i][0] == method:
			return _engine.calls[i]
	check(false, "%s was never called" % method)
	return [method, {}, null, null]


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
