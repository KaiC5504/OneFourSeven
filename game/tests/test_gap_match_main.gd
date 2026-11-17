extends TestCase
## The table scene's part of a match where the check round found gaps, with a scripted engine: a
## stand-in stroke that can't be played, the cue a match starts with, what match.call and
## match.started carry, and the harness's answers when the engine refuses.

const MainScript := preload("res://scripts/main.gd")
const HarnessScript := preload("res://scripts/autoload/harness.gd")
const FRAME_VIEW := "res://data/rules/example_frame_view.json"
const MATCH := "res://data/rules/example_match.json"
const STRIKE := {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.0}


## Refuses every next frame, saying why through last_error as OfsMatch does.
class Refusing extends FakeMatchEngine:
	func next_frame() -> Dictionary:
		calls.append(["next_frame"])
		error = "the match is over"
		return {"ok": false, "error": error}


var _engine: FakeMatchEngine
var _log_start := 0


# A stand-in stroke that can't be played counts as a refusal and the stand-in is asked again; past
# MatchController.MAX_REFUSALS the person is told and offered the pause menu, and nothing is asked.
func test_gap_a_stand_in_stroke_that_cannot_be_played_is_asked_again_then_stalls() -> void:
	var main: MainScript = await _spawn(_frame(1))
	var m := _start(main)
	var broken := STRIKE.duplicate()
	broken.erase("speed_m_s")
	for i in MatchController.MAX_REFUSALS + 2:
		_engine.standin_shots.append(_standin(broken))
	await _until(func() -> bool: return _engine.called().count("standin_shot") >= MatchController.MAX_REFUSALS,
			"the stand-in asked %d times" % MatchController.MAX_REFUSALS)
	for i in 10:
		await tree.process_frame
	check_eq(_engine.called().count("standin_shot"), MatchController.MAX_REFUSALS, "and no more")
	var pause: PauseMenu = main.get_node("Hud/PauseMenu")
	var notice: Label = main.get_node("Hud/Notice")
	check(pause.visible and notice.visible and notice.text == MainScript.STALLED_NOTICE,
			"the person is told the match can't go on, with the menu up: %s" % notice.text)
	check(not main.is_playing(), "nothing was struck")
	pause.button("quit").pressed.emit()
	pause.button("confirm").pressed.emit()
	check(not m.is_active() and main.harness_state()["screen"] == "home", "Quit leaves the match")
	main.free()


# A good stroke after a bad one plays, and the count starts again.
func test_gap_a_stand_in_stroke_after_a_bad_one_plays() -> void:
	var main: MainScript = await _spawn(_frame(1))
	_start(main)
	var broken := STRIKE.duplicate()
	broken["aim_rad"] = NAN
	_engine.standin_shots.append(_standin(broken))
	_engine.standin_shots.append(_standin(STRIKE))
	await _until(main.is_playing, "the second stroke")
	check_eq(_engine.called().count("standin_shot"), 2, "asked twice")
	main.skip_shot()
	main.free()


func test_gap_a_match_starts_with_its_own_cue_and_gives_the_free_tables_back() -> void:
	var main: MainScript = await _spawn(_frame(0))
	main.setup.set_tip(0.118, -0.195)
	main.setup.set_elevation(0.4)
	main.setup.set_speed(2.5, 0.6)
	main.setup.last_stroke_speed_m_s = 2.5
	var free := main.setup.snapshot()
	_start(main)
	check_eq([main.setup.tip_x, main.setup.tip_y, main.setup.elevation_rad, main.setup.speed_m_s, main.setup.power],
			[0.0, 0.0, 0.0, 0.0, 0.0], "centre ball, the cue as low as it goes, the opening power")
	check_eq(main.setup.last_stroke_speed_m_s, 0.0, "and no pace carried over")
	main.harness_open_screen("table")
	check_eq(main.setup.snapshot(), free, "the free table has its own back")
	main.free()


func test_gap_the_match_lines_say_whose_match_and_stroke() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main)
	check_eq(_last_call("start")[1].get("person"), MatchController.PERSON, "the engine is told who the person is")
	var fouled := _frame(1)
	_mark_log()
	_engine.judgements.append({"ok": true, "error": "", "event": "none", "frame": fouled, "judgement": {"fouls": [],
			"penalty": 4, "miss": false, "miss_reason": "", "points": 0, "nominated": "", "free_ball_used": false,
			"spotted": [], "calls": [{"kind": "foul", "value": 4, "detail": ""}]}})
	var shot_id := str(main.harness_match_shot(STRIKE, "", null).get("shot_id", ""))
	main.skip_shot()
	await _until_idle(main)
	var said := _logged("match.call")
	check_eq(said.size(), 1, "the call is logged")
	if said.size() == 1:
		check_eq([said[0].get("match_id"), int(said[0].get("frame", -1)), said[0].get("shot_id")], [m.match_id(), m.frame_number(),
				shot_id], "with the match, the frame and the stroke")
	main.free()


func test_gap_the_harness_answers_with_the_engines_reason() -> void:
	var main: MainScript = await _spawn(_frame(0), Refusing.new())
	var harness: Node = HarnessScript.new()
	harness.scene_provider = func() -> Node: return main
	tree.root.add_child(harness)
	var r: Dictionary = await harness.run_command("start_match", {"best_of": 3, "first_breaker": "you", "seed": 1})
	check(r["ok"], "the match starts: %s" % r)
	_engine.error = "not in the D"
	r = await harness.run_command("match_choose", {"kind": "replace"})
	check(not r["ok"] and str(r["error"]).contains("engine: not in the D"), "match_choose: %s" % r)

	var over := _frame(0, {"over": true, "winner": 0, "end_reason": "black"})
	_engine.match_view["history"].append({"number": 2, "breaker": 1, "winner": 0, "reason": "black", "scores": [70, 31],
			"stats": over["stats"], "strokes": 60, "reracks": 0})
	_engine.judgements.append({"ok": true, "error": "", "event": "frame_ended", "frame": over, "judgement": {}})
	r = await harness.run_command("match_shot", STRIKE)
	check(r["ok"], "a stroke ends the frame: %s" % r)
	main.skip_shot()
	await _until_idle(main)
	r = await harness.run_command("match_wait", {"timeout_s": 1})
	check(r["ok"] and r["result"]["waiting"] == "frame_summary", "at the frame's summary: %s" % r)
	r = await harness.run_command("match_continue", {})
	check(not r["ok"] and str(r["error"]) == "next frame refused: the match is over", "match_continue: %s" % r)
	harness.free()
	main.free()


func test_gap_an_end_reason_with_no_words_is_logged_once() -> void:
	var line := RefereeLine.new()
	tree.root.add_child(line)
	RefereeLine.Kit.forget_reported()
	_mark_log()
	for reason: String in ["black", "respotted_black", "conceded"]:
		line.announce([{"kind": "frame_over", "value": 0, "detail": reason}], 0)
	check(_logged("app.error").is_empty(), "every end reason the engine names is known: %s" % [_logged("app.error")])
	line.announce([{"kind": "frame_over", "value": 0, "detail": "walkover"}], 0)
	line.announce([{"kind": "frame_over", "value": 0, "detail": "walkover"}], 0)
	check_eq(_logged("app.error").size(), 1, "an unknown one is logged once")
	check_eq(line.text(), "Frame over", "and said plainly")
	line.free()


func _frame(striker: int, changes: Dictionary = {}) -> Dictionary:
	var frame := _json(FRAME_VIEW)
	frame["balls"] = HarnessScript.game_balls(frame["balls"])["balls"]
	frame["pending"] = "none"
	frame["striker"] = striker
	frame["free_ball"] = false
	frame["view"]["nominations"] = []
	frame["view"]["options"] = [["concede_frame", "concede_match"], []]
	frame.merge(changes, true)
	return frame


func _standin(strike: Dictionary) -> Dictionary:
	return {"ok": true, "error": "", "places_cue": false, "cue": [], "nominated": "", "strike": strike.duplicate(),
			"why": {"kind": "pot"}, "us": 120}


func _spawn(frame: Dictionary, engine: FakeMatchEngine = null) -> MainScript:
	for key: String in Settings.DEFAULTS:
		Settings.set_value(key, Settings.DEFAULTS[key])
	var main := load("res://scenes/main.tscn").instantiate() as MainScript
	tree.root.add_child(main)
	await tree.process_frame
	_engine = engine if engine != null else FakeMatchEngine.new()
	# The stand-in plays these, as when no level could be configured: the setup then offers none.
	_engine.level_list = []
	_engine.frame_view = frame
	_engine.match_view = _json(MATCH)
	_engine.match_view.erase("frame")
	_engine.ball_list = (frame["balls"] as Array).duplicate(true)
	(main.get_node("Match") as MatchController).engine = _engine
	main.choose_among = func(_balls: Array[Dictionary], _cue_ball: int, _places: Array) -> Dictionary: return {}
	return main


func _start(main: MainScript) -> MatchController:
	var hud := main.get_node("Hud")
	(hud.get_node("HomeScreen") as HomeScreen).button("play").pressed.emit()
	((hud.get_node("MatchSetupScreen") as MatchSetupScreen).control("start") as Button).pressed.emit()
	var m: MatchController = main.get_node("Match")
	check(m.is_active(), "the match is on")
	m.pace = 0.0
	main._flush_refresh()
	(main.get_node("CameraRig") as CameraRig)._process(CameraRig.RETURN_S + CameraRig.BLEND_S)
	return m


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


func _last_call(method: String) -> Array:
	for i in range(_engine.calls.size() - 1, -1, -1):
		if _engine.calls[i][0] == method:
			return _engine.calls[i]
	check(false, "%s was never called" % method)
	return [method, {}, null]


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
