extends TestCase
## The pause menu on the real table scene against what the engine offers (spec 11.1,
## docs/rules/README.md S4.2, S2.1(b)), with a scripted engine on its MatchController. Every
## expectation follows the options the test gives the frame, never the pending state.

const MainScript := preload("res://scripts/main.gd")
const HarnessScript := preload("res://scripts/autoload/harness.gd")
const FRAME_VIEW := "res://data/rules/example_frame_view.json"
const MATCH := "res://data/rules/example_match.json"
const AFTER_FOUL: Array[String] = ["play", "play_again", "replace"]
const CONCESSIONS: Array[String] = ["concede_frame", "concede_match"]
const STRIKE := {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.0}

var _engine: FakeMatchEngine
var _log_start := 0


func test_gap_pause_offers_a_concession_only_when_the_engine_does() -> void:
	for concessions: Array in [[], CONCESSIONS]:
		var options: Array[String] = AFTER_FOUL.duplicate()
		for kind: String in concessions:
			options.append(kind)
		var main: MainScript = await _spawn(_fouled(options))
		var hud := main.get_node("Hud")
		_start(main)
		var pause: PauseMenu = hud.get_node("PauseMenu")
		(hud.get_node("Scoreboard") as Scoreboard).pause_button().pressed.emit()
		var offered := not concessions.is_empty()
		check(pause.visible, "the pause menu is up (offered %s)" % [options])
		check_eq(not pause.button("concede_frame").disabled, offered, "Concede frame as offered: %s" % [options])
		check_eq(not pause.button("concede_match").disabled, offered, "Concede match as offered: %s" % [options])
		check(not pause.button("claim_frame").visible, "no claim when none is offered")
		main.free()


# A concession that is not open never reaches the engine: it is logged as refused and said.
func test_gap_a_concession_not_open_is_refused_without_an_error() -> void:
	var main: MainScript = await _spawn(_fouled(AFTER_FOUL))
	_start(main)
	var pause: PauseMenu = main.get_node("Hud/PauseMenu")
	(main.get_node("Hud/Scoreboard") as Scoreboard).pause_button().pressed.emit()
	_mark_log()
	pause.concede_match_confirmed.emit()
	pause.concede_frame_confirmed.emit()
	pause.claim_frame_confirmed.emit()
	check(not _engine.called().has("decide"), "the engine is not asked: %s" % [_engine.called()])
	var refused := _logged("match.refused")
	check_eq(refused.size(), 3, "each is logged as refused: %s" % [refused])
	for line in refused:
		check(str(line.get("why", "")).contains("not offered"), "saying why: %s" % [line])
		check_eq(line.get("match_id"), (main.get_node("Match") as MatchController).match_id(), "with the match")
		_check_schema("match.refused", line)
	check(_logged("app.error").is_empty() and _logged("rules.invalid").is_empty(), "and no error: %s" % [_logged("app.error")])
	check((main.get_node("Hud/Notice") as Label).visible, "the person is told")
	check(pause.visible, "and the menu stays")
	main.free()


# The menu follows the frame while it is open: a stroke playing closes the concessions, its end
# opens them again.
func test_gap_the_open_menu_follows_the_frame() -> void:
	var main: MainScript = await _spawn(_frame(0, CONCESSIONS))
	var m := _start(main)
	var pause: PauseMenu = main.get_node("Hud/PauseMenu")
	_engine.judgements.append(_judged(_frame(0, CONCESSIONS)))
	check(main.harness_match_shot(STRIKE, "", null).has("shot_id"), "the person strikes")
	(main.get_node("Hud/Scoreboard") as Scoreboard).pause_button().pressed.emit()
	check(pause.visible and pause.button("concede_frame").disabled and pause.button("concede_match").disabled,
			"the menu opens over the stroke with nothing to concede while it plays")
	main.skip_shot()
	await _until_idle(main)
	check(pause.visible, "the menu is still up")
	check(not pause.button("concede_frame").disabled and not pause.button("concede_match").disabled,
			"and offers both once the stroke is judged")
	check(m.is_active(), "the match goes on")
	main.free()


# S2.1(b): the engine offers the claim; the person takes it from the menu and the frame ends.
func test_gap_the_person_claims_the_frame() -> void:
	var claim: Array[String] = ["claim_frame", "concede_frame", "concede_match"]
	var main: MainScript = await _spawn(_frame(0, claim))
	_start(main)
	var hud := main.get_node("Hud")
	var pause: PauseMenu = hud.get_node("PauseMenu")
	(hud.get_node("Scoreboard") as Scoreboard).pause_button().pressed.emit()
	check(pause.button("claim_frame").visible, "Claim frame is offered")
	var over := _frame(0, [], {"over": true, "winner": 0, "end_reason": "claimed", "scores": [70, 20]})
	_engine.match_view["history"].append({"number": 2, "breaker": 1, "winner": 0, "reason": "claimed",
			"scores": [70, 20], "stats": over["stats"], "strokes": 40, "reracks": 0})
	_engine.match_view["frames"] = [1, 1]
	_engine.decisions.append({"ok": true, "error": "", "calls": [], "event": "frame_ended", "frame": over})
	pause.button("claim_frame").pressed.emit()
	check(_engine.called().count("decide") == 0, "it asks first")
	pause.button("confirm").pressed.emit()
	check_eq(_last_call("decide"), ["decide", "claim_frame", 0], "the claim goes to the engine")
	var summary: FrameSummary = hud.get_node("FrameSummary")
	check(not pause.visible and summary.visible, "the frame's summary shows")
	check(not (hud.get_node("Scoreboard") as Control).visible, "with no scoreboard behind it")
	check_eq(main.harness_match_state()["waiting"], "frame_summary", "the harness says what shows")
	main.free()


# Conceding the match goes straight to the match's summary.
func test_gap_conceding_the_match_shows_the_match_summary_at_once() -> void:
	var main: MainScript = await _spawn(_frame(0, CONCESSIONS))
	_start(main)
	var hud := main.get_node("Hud")
	var pause: PauseMenu = hud.get_node("PauseMenu")
	var over := _frame(0, [], {"over": true, "winner": 1, "end_reason": "match_conceded"})
	_engine.match_view["history"].append({"number": 2, "breaker": 1, "winner": 1, "reason": "match_conceded",
			"scores": [24, 31], "stats": over["stats"], "strokes": 43, "reracks": 0})
	_engine.match_view["frames"] = [0, 2]
	_engine.match_view["over"] = true
	_engine.match_view["winner"] = 1
	_engine.decisions.append({"ok": true, "error": "", "calls": [], "event": "match_ended", "frame": over})
	(hud.get_node("Scoreboard") as Scoreboard).pause_button().pressed.emit()
	pause.button("concede_match").pressed.emit()
	pause.button("confirm").pressed.emit()
	check_eq(_last_call("decide"), ["decide", "concede_match", 0], "the concession goes to the engine")
	check(not (hud.get_node("FrameSummary") as Control).visible, "no frame card")
	check((hud.get_node("MatchSummary") as Control).visible, "the match's summary at once")
	check(not (hud.get_node("Scoreboard") as Control).visible, "with no scoreboard behind it")
	check_eq(main.harness_state()["screen"], "match_summary", "the screen")
	check_eq(main.harness_match_waiting()["waiting"], "match_summary", "and what the harness waits on")
	(hud.get_node("MatchSummary") as MatchSummary).button("home").pressed.emit()
	check_eq(main.harness_state()["screen"], "home", "its button goes Home")
	main.free()


func _fouled(options: Array[String]) -> Dictionary:
	var fouled := _json(FRAME_VIEW)
	fouled["balls"] = HarnessScript.game_balls(fouled["balls"])["balls"]
	fouled["pending"] = "after_foul"
	fouled["decider"] = 0
	fouled["view"]["options"] = [options, []]
	return fouled


## The example frame with nothing pending, `striker` to play and `options` the person's.
func _frame(striker: int, options: Array[String], changes: Dictionary = {}) -> Dictionary:
	var frame := _json(FRAME_VIEW)
	frame["balls"] = HarnessScript.game_balls(frame["balls"])["balls"]
	frame["pending"] = "none"
	frame["striker"] = striker
	frame["free_ball"] = false
	frame["view"]["nominations"] = []
	frame["view"]["options"] = [options, []]
	frame.merge(changes, true)
	return frame


func _judged(frame: Dictionary) -> Dictionary:
	return {"ok": true, "error": "", "event": "none", "frame": frame, "judgement": {"fouls": [], "penalty": 0,
			"miss": false, "miss_reason": "", "points": 0, "nominated": "", "free_ball_used": false, "spotted": [],
			"calls": []}}


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


func _last_call(method: String) -> Array:
	for i in range(_engine.calls.size() - 1, -1, -1):
		if _engine.calls[i][0] == method:
			return _engine.calls[i]
	check(false, "%s was never called" % method)
	return [method, null, null]


func _check_schema(event: String, data: Dictionary) -> void:
	var schema: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://data/log_schema.json"))
	var fields: Dictionary = schema["events"][event]["fields"]
	for field: String in fields:
		if fields[field].get("required", false):
			check(data.has(field), "%s has its required %s" % [event, field])


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
