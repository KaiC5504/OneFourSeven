extends TestCase
## A match on the real table scene (spec 11, 8.6, 8.7, 10.5) with a scripted engine on its
## MatchController: Home to the setup to a match and back, the person's stroke held up by ball in
## hand and by nomination, the opponent's stroke played as given, choices, the referee's line,
## pause, concessions, the summaries, every screen.changed, the free table left as it was, and the
## harness's match commands. No rule is tested here: every answer of the engine is scripted.

const MainScript := preload("res://scripts/main.gd")
const HarnessScript := preload("res://scripts/autoload/harness.gd")
const HudScript := preload("res://scripts/hud.gd")
const FRAME_VIEW := "res://data/rules/example_frame_view.json"
const MATCH := "res://data/rules/example_match.json"
const COLOURS: Array[String] = ["yellow", "green", "brown", "blue", "pink", "black"]
## Places of the example frame's balls.
const CUE := 0
const RED := 1
const BLUE := 6
const PINK := 7
const BLACK := 8
const STRIKE := {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.0}
const RADIUS := 0.02625
const COLOUR_OF_CHOICE := {"on": {"kind": "any_colour"}, "nominations": COLOURS, "on_places": [3, 4, 5, 6, 7, 8]}


## FakeMatchEngine whose next_frame racks the frames queued, as OfsMatch racks the next one.
class Scripted extends FakeMatchEngine:
	var racks: Array[Dictionary] = []

	func next_frame() -> Dictionary:
		calls.append(["next_frame"])
		if racks.is_empty():
			return {"ok": false, "error": "no frame to rack"}
		frame_view = racks.pop_front()
		ball_list = (frame_view["balls"] as Array).duplicate(true)
		return {"ok": true, "error": "", "frame": frame()}


var _engine: Scripted
var _log_start := 0


func test_the_opening_screen_is_logged() -> void:
	_mark_log()
	var main := load("res://scenes/main.tscn").instantiate() as MainScript
	tree.root.add_child(main)
	await tree.process_frame
	check_eq(_screens(), [["", "home"]], "the app opens at Home")
	main.free()


func test_home_leads_through_the_setup_to_a_match_and_quit_leads_back() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var hud := main.get_node("Hud")
	var home: HomeScreen = hud.get_node("HomeScreen")
	var setup_screen: MatchSetupScreen = hud.get_node("MatchSetupScreen")
	var scoreboard: Scoreboard = hud.get_node("Scoreboard")
	var pause: PauseMenu = hud.get_node("PauseMenu")
	var m: MatchController = main.get_node("Match")
	var rig: CameraRig = main.get_node("CameraRig")
	var free_balls: Array[Dictionary] = (main.get_node("FreeTable") as FreeTable).balls.duplicate(true)

	home.button("play").pressed.emit()
	check(setup_screen.visible and not home.visible, "Play opens the match setup")
	check_eq(_engine.called().count("start"), 0, "nothing starts yet")
	check(rig.is_held(), "the full-table view stays behind it")
	(setup_screen.control("back") as Button).pressed.emit()
	check(home.visible and not setup_screen.visible, "Back returns Home")

	home.button("play").pressed.emit()
	(setup_screen.control("best_of:5") as Button).button_pressed = true
	(setup_screen.control("first_breaker:you") as Button).button_pressed = true
	(setup_screen.control("start") as Button).pressed.emit()
	var asked := _last_call("start")
	check_eq([asked[1]["best_of"], asked[1]["first_breaker"]], [5, 0], "Start sends the setup's choices to the engine")
	check(m.is_active(), "a match is on")
	check(not setup_screen.visible and not home.visible, "the screens are gone")
	check_eq([main.harness_state()["mode"], main.harness_state()["screen"]], ["match", "match"], "mode and screen")
	check(scoreboard.visible and scoreboard.texts()["you"] == 24 and scoreboard.texts()["opponent"] == 31,
			"the scoreboard shows the frame: %s" % [scoreboard.texts()])
	check_eq(scoreboard.texts()["best_of"], 3, "and the match")
	_check_nodes_match(main, _engine.ball_list, "the match's balls are on the table")
	check_eq(main.harness_state()["balls"], m.balls, "and are the state's balls")
	for hidden: String in ["FreeTableBar", "EditPalette", "BreakPill", "PracticeStrip"]:
		check(not (hud.get_node(hidden) as Control).visible, "the free table's %s is off" % hidden)
	check(not rig.is_held() and rig.is_easing(), "the camera comes down behind the cue ball")
	_check_controls(main, true, true, "the person's turn")
	check((main.get_node("Cue") as Node3D).visible, "with the cue")

	# The free table's history keys do nothing in a match.
	var key := InputEventKey.new()
	key.keycode = KEY_R
	key.physical_keycode = KEY_R
	key.pressed = true
	tree.root.push_input(key, true)
	check(not _engine.called().has("judge") and m.is_active(), "R retakes nothing")

	scoreboard.pause_button().pressed.emit()
	check(pause.visible, "the scoreboard's button opens the pause menu")
	pause.button("quit").pressed.emit()
	check(m.is_active(), "Quit asks first")
	pause.button("confirm").pressed.emit()
	check(not m.is_active() and _engine.called().has("abandon"), "and then the match is abandoned")
	check(home.visible and not pause.visible and not scoreboard.visible, "back at Home")
	check_eq([main.harness_state()["mode"], main.harness_state()["screen"]], ["home", "home"], "mode and screen")
	check(rig.is_held(), "the full-table view again")
	_check_nodes_match(main, free_balls, "the free table's balls are behind Home")
	check_eq(_screens(), [["home", "match_setup"], ["match_setup", "home"], ["home", "match_setup"],
			["match_setup", "match"], ["match", "pause"], ["pause", "home"]], "every change of screen is logged")
	main.free()


func test_a_match_that_cannot_start_stays_at_the_setup_and_says_so() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var hud := main.get_node("Hud")
	_engine.ready = false
	(hud.get_node("HomeScreen") as HomeScreen).button("play").pressed.emit()
	((hud.get_node("MatchSetupScreen") as MatchSetupScreen).control("start") as Button).pressed.emit()
	var notice: Label = hud.get_node("Notice")
	check(notice.visible and notice.text == MainScript.NO_MATCH_NOTICE, "the person is told")
	check((hud.get_node("MatchSetupScreen") as Control).visible, "the setup stays")
	check_eq([main.harness_state()["mode"], main.harness_state()["screen"]], ["home", "match_setup"], "mode and screen")
	check(not (hud.get_node("Scoreboard") as Control).visible, "no scoreboard")
	check(notice.get_index() > hud.get_node("MatchSetupScreen").get_index(), "the notice shows over the setup")
	check_eq(_errors_with("engine.start(").size(), 1, "MatchController logged why")
	main.free()


func test_ball_in_hand_is_dragged_about_the_d_and_confirmed_before_the_stroke() -> void:
	var frame := _frame(0, {"cue_in_hand": true})
	frame["balls"][CUE]["x"] = -1.2
	frame["balls"][CUE]["y"] = 0.05
	var main: MainScript = await _spawn(frame)
	var m := _start(main)
	var hud := main.get_node("Hud")
	var placer: CuePlacer = hud.get_node("CuePlacer")
	var stroke: StrokeControl = hud.get_node("StrokeCue")
	var camera: Camera3D = main.get_node("CameraRig/Camera3D")
	var cue_node: Node3D = main.get_node("Balls/cue")
	check(m.placing() and placer.is_placing(), "the cue ball is in hand")
	check(placer.button("confirm") != null and placer.button("move") == null, "with Confirm")
	_check_controls(main, false, false, "placing")
	check(not (main.get_node("Cue") as Node3D).visible, "and no cue")

	# The whole D shows, above the placer and under the scoreboard.
	var dims := OfsTable.dimensions()
	var baulk_x := float(dims["baulk_line_x"])
	var d := float(dims["d_radius"])
	var top := (hud.get_node("Scoreboard") as Scoreboard).covered_rect().end.y
	var bottom := placer.covered_rect().position.y
	var canvas := main.get_viewport().get_visible_rect().size
	for point: Vector2 in [Vector2(baulk_x, d + RADIUS), Vector2(baulk_x, -d - RADIUS), Vector2(baulk_x - d - RADIUS, 0.0),
			Vector2(baulk_x - d * 0.7, d * 0.7), Vector2(baulk_x - d * 0.7, -d * 0.7)]:
		var at := camera.unproject_position(TableSpace.core_to_godot(point.x, point.y))
		check(at.y > top and at.y < bottom and at.x > 0.0 and at.x < canvas.x,
				"the D's %s shows clear of the placer: %s (between %s and %s)" % [point, at, top, bottom])
	var centre: Rect2 = HudScript.layout(SettingsPanel.canvas_safe_rect(placer), Vector2.ZERO, canvas, "", false)["centre"]
	check(centre.grow(1.0).encloses(placer.covered_rect()), "the placer stands between the spin ball and the power cue")
	check_near(placer.covered_rect().get_center().x, canvas.x / 2.0, 1.0, "about the screen's middle")

	main.setup.set_speed(1.5)
	stroke.struck.emit(1.5)
	check(not main.is_playing() and m.strike_blocked() == "placing", "no stroke before the cue ball is confirmed")

	var target := Vector2(-1.25, -0.1)
	var from := camera.unproject_position(cue_node.global_position)
	var to := camera.unproject_position(TableSpace.core_to_godot(target.x, target.y, RADIUS))
	_touch(from, true)
	_drag_to(from, from.lerp(to, 0.5))
	_drag_to(from.lerp(to, 0.5), to)
	var shown := TableSpace.godot_to_core(cue_node.position)
	check(shown.distance_to(target) < 2e-3, "the cue ball follows the finger: %s" % shown)
	check_eq(_engine.called().count("place_cue"), 0, "and is placed only when the finger lifts")
	check_eq(main.setup.aim_rad, 0.0, "the drag turns no aim")
	_touch(to, false)
	var placed := _last_call("place_cue")
	check(Vector2(placed[1], placed[2]).distance_to(target) < 2e-3, "the engine places it there: %s" % [placed])
	check(Vector2(float(m.balls[CUE]["x"]), float(m.balls[CUE]["y"])).distance_to(target) < 2e-3, "the match has it")

	_engine.cue_legal = false
	var away := camera.unproject_position(TableSpace.core_to_godot(-1.1, 0.2, RADIUS))
	_touch(to, true)
	_drag_to(to, away)
	_touch(away, false)
	check(TableSpace.godot_to_core(cue_node.position).distance_to(target) < 2e-3, "a place the engine refuses puts it back")
	check((hud.get_node("Notice") as Label).visible, "and says so")
	_engine.cue_legal = true

	placer.button("confirm").pressed.emit()
	check(not m.placing() and m.strike_blocked() == "", "Confirm frees the stroke")
	check(placer.button("move") != null, "and leaves Move")
	_check_controls(main, true, true, "confirmed")
	placer.button("move").pressed.emit()
	check(m.placing() and placer.is_placing(), "Move goes back to placing")
	_check_controls(main, false, false, "placing again")
	placer.button("confirm").pressed.emit()
	stroke.struck.emit(1.5)
	check(main.is_playing(), "the stroke goes")
	check(not placer.covered_rect().has_area(), "and the placer with it")
	_engine.judgements.append(_judged(_frame(0)))
	main.skip_shot()
	await _until_idle(main)
	main.free()


func test_a_colour_of_choice_is_declared_by_the_aim_or_a_tap() -> void:
	var main: MainScript = await _spawn(_frame(0, COLOUR_OF_CHOICE))
	var m := _start(main)
	var hud := main.get_node("Hud")
	var ring: Nomination = main.get_node("Nomination")
	var stroke: StrokeControl = hud.get_node("StrokeCue")
	var gestures: Gestures = main.get_node("Gestures")
	var notice: Label = hud.get_node("Notice")
	check_eq(m.nominated(), "", "the opening aim is on no ball")
	check(not ring.is_marking(), "no ring")
	main.setup.set_speed(1.5)
	stroke.struck.emit(1.5)
	check(not main.is_playing() and _engine.called().count("judge") == 0, "the stroke waits for a colour")
	check(notice.visible and notice.text == MainScript.NOMINATE_NOTICE, "and says how: %s" % notice.text)

	main.setup.set_aim(_aim_at(m, BLUE))
	check_eq(m.nominated(), "blue", "the first ball on the aim line is declared")
	check(ring.is_marking() and (ring.get_node("Ring") as Node3D).position.distance_to(
			TableSpace.core_to_godot(0.0, 0.0, Nomination.HEIGHT)) < 1e-6, "and ringed")
	check_eq(m.strike_blocked(), "", "the stroke may go")
	main.setup.set_aim(_aim_at(m, RED))
	check_eq(m.nominated(), "blue", "a line on a red leaves the declaration")
	main.setup.set_aim(PI / 2.0)
	check_eq(m.nominated(), "blue", "so does a line on nothing")

	# The black lies behind the pink on the line from the cue ball.
	var black_aim := _aim_at(m, BLACK)
	check_eq(Nomination.first_on_line(m.balls, CUE, black_aim, RADIUS), PINK, "the pink is in the way of the black")
	_land(main)
	gestures.tapped.emit(_screen_of(main, BLACK))
	check_eq(m.nominated(), "black", "a tap declares the ball tapped")
	check_near(main.setup.aim_rad, black_aim, 1e-9, "and turns the aim to it")
	main.setup.add_aim(0.0005)
	check_eq(m.nominated(), "black", "the tap stands while the line still meets the pink first")
	_land(main)
	gestures.tapped.emit(_screen_of(main, RED))
	check_eq(m.nominated(), "black", "a tap on a red declares nothing")
	main.setup.set_aim(_aim_at(m, BLUE))
	check_eq(m.nominated(), "blue", "the aim on another colour declares that one")
	var declared: Array[Array] = []
	for line in _logged("match.nominated"):
		declared.append([line["ball"], line["how"]])
	check_eq(declared, [["blue", "aim"], ["black", "tap"], ["blue", "aim"]], "each change is logged with how")

	stroke.struck.emit(1.5)
	check(main.is_playing(), "the stroke goes")
	check(not ring.is_marking(), "the ring is off while it plays")
	_engine.judgements.append(_judged(_frame(0)))
	main.skip_shot()
	await _until_idle(main)
	check_eq(_last_call("judge")[2], {"nominated": "blue"}, "the referee is given the declared colour")
	main.free()


func test_with_a_free_ball_a_ball_on_on_the_line_withdraws_the_declaration() -> void:
	var main: MainScript = await _spawn(_frame(0, {"free_ball": true, "nominations": COLOURS}))
	var m := _start(main)
	main.setup.set_aim(_aim_at(m, BLUE))
	check_eq(m.nominated(), "blue", "the blue as the free ball")
	main.setup.set_aim(_aim_at(m, RED))
	check_eq(m.nominated(), "", "a red on the line: it is played as itself")
	check_eq(m.strike_blocked(), "", "and the stroke may go")
	main.setup.set_aim(PI / 2.0)
	main.setup.set_aim(_aim_at(m, PINK))
	check_eq(m.nominated(), "pink", "another colour is declared again")
	main.free()


func test_the_aim_turns_to_a_ball_on() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var asked: Array[Array] = []
	main.choose_among = func(_balls: Array[Dictionary], _cue_ball: int, places: Array) -> Dictionary:
		asked.append(places)
		return {"index": 2, "ball": "red 7", "pocket": 1, "aim_rad": 0.4, "cut_deg": 12.0, "score": 0.5, "reason": ""}
	var m := _start(main)
	check_eq(asked, [[1.0, 2.0]], "the balls on are what the aim may turn to")
	check_near(main.setup.aim_rad, 0.4, 1e-12, "the first turn starts aimed at one")
	main.setup.set_aim(-1.0)
	_mark_log()
	var after := _frame(0, {"on_places": [6], "scores": [25, 31], "break_points": 1})
	_engine.judgements.append(_judged(after, {"points": 1}))
	var shot: Dictionary = main.harness_match_shot(STRIKE, "", null)
	main.skip_shot()
	await _until_idle(main)
	check_eq(asked.back(), [6], "after a stroke, the balls on then")
	check_near(main.setup.aim_rad, 0.4, 1e-12, "and the aim turns again")
	var results := _logged("shot.result")
	check_eq(results.size(), 1, "one shot.result")
	if results.size() == 1:
		var result := results[0]
		_check_schema("shot.result", result)
		check_eq([result["shot_id"], result["match_id"], int(result["frame"])], [shot["shot_id"], m.match_id(), 2], "whose it is")
		check_eq([int(result["points"]), int(result["break"])], [1, 1], "points and break are the referee's")
		check_eq(result["next_ball"], "red 7", "and where the aim turned")
	check_eq((main.get_node("Hud/Scoreboard") as Scoreboard).texts()["break"], 1, "the scoreboard has the break")
	check_eq(main.harness_state()["break"], 1, "and the state")
	check(not (main.get_node("Hud/BreakPill") as Control).visible, "the free table's break counter stays off")
	main.free()


func test_a_ball_not_on_shows_red_and_turns_at_full_speed() -> void:
	# A red is on (places 1 and 2). On the blue the line is red and the aim swings as it does off a
	# ball; on a red it is white and fine.
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main)
	var guides: Guides = main.get_node("Guides")
	var aim: AimControl = main.get_node("AimControl")
	Settings.set_value("guide_level", 2)
	main.setup.set_aim(_aim_at(m, BLUE))
	await tree.process_frame
	check_eq(guides.aimed_ball(m.balls, m.cue_ball, main.setup.aim_rad), BLUE, "the line meets the blue")
	check(guides.shows_wrong_ball(), "the blue is not on: red")
	var coarse := aim.rad_per_px()
	main.setup.set_aim(_aim_at(m, RED))
	await tree.process_frame
	check_eq(guides.aimed_ball(m.balls, m.cue_ball, main.setup.aim_rad), RED, "the line meets a red")
	check(not guides.shows_wrong_ball(), "a red is on: white")
	check(aim.rad_per_px() < coarse, "and the turn is the fine one: %s against %s" % [aim.rad_per_px(), coarse])
	main.free()
	# A colour of choice: every colour may be declared, so the blue is white and a red is not.
	main = await _spawn(_frame(0, COLOUR_OF_CHOICE))
	m = _start(main)
	guides = main.get_node("Guides")
	Settings.set_value("guide_level", 2)
	main.setup.set_aim(_aim_at(m, BLUE))
	await tree.process_frame
	check(not guides.shows_wrong_ball(), "a colour is on: the blue is white")
	main.setup.set_aim(_aim_at(m, RED))
	await tree.process_frame
	check_eq(guides.aimed_ball(m.balls, m.cue_ball, main.setup.aim_rad), RED, "the line meets a red")
	check(guides.shows_wrong_ball(), "and a red is red")
	Settings.set_value("guide_level", Settings.DEFAULTS["guide_level"])
	main.free()


func test_the_persons_stroke_is_logged_and_judged_as_a_match_stroke() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main)
	var stroke: StrokeControl = main.get_node("Hud/StrokeCue")
	var moved: Array = _engine.ball_list.duplicate(true)
	moved[CUE]["x"] = 0.3
	moved.remove_at(RED)
	var calls := [{"kind": "foul", "value": 4, "detail": "no_hit"}]
	_engine.judgements.append(_judged(_frame(0), {"calls": calls}, {"balls": moved}))
	main.setup.set_speed(1.5)
	_mark_log()
	stroke.struck.emit(1.5)
	check(main.is_playing(), "the stroke goes")
	check_eq(m.strike_blocked(), "playing", "the match knows a stroke is in play")
	_check_controls(main, true, false, "while it plays")
	var inputs := _logged("shot.input")
	var simulated := _logged("shot.simulated")
	check(inputs.size() == 1 and simulated.size() == 1, "logged and simulated once")
	if inputs.size() == 1 and simulated.size() == 1:
		_check_schema("shot.input", inputs[0])
		check_eq([inputs[0]["mode"], inputs[0]["strike_mode"], inputs[0]["match_id"], int(inputs[0]["frame"])],
				["match", "stroke", m.match_id(), 2], "shot.input says whose match and frame")
		check_eq(simulated[0]["context"], {"shot_id": inputs[0]["shot_id"], "mode": "match", "strike_mode": "stroke"},
				"the simulation's context")
		check_eq((simulated[0]["bundle"]["start"]["balls"] as Array).size(), 9, "simulated from the match's balls")
	check_eq((main.get_node("FreeTable") as FreeTable).history_size(), 1, "the free table's history is not touched")
	main.skip_shot()
	await _until_idle(main)
	var judged := _last_call("judge")
	check(judged[1] is OfsShotRecord, "the referee is given the shot's record")
	if inputs.size() == 1:
		check_eq(judged[3], {"shot_id": inputs[0]["shot_id"]}, "under the stroke's id")
	_check_nodes_match(main, moved, "the table shows the balls as judged")
	var referee: RefereeLine = main.get_node("Hud/RefereeLine")
	check_eq(referee.text(), "FOUL 4", "the referee's call shows")
	var line := (referee.get_node("Line") as Control).get_rect()
	var board := (main.get_node("Hud/Scoreboard") as Scoreboard).covered_rect()
	check(line.position.y >= board.end.y and line.position.y < board.end.y + 2.0 * HudScript.LINE_GAP,
			"right under the scoreboard: %s under %s" % [line, board])
	check_near(line.get_center().x, main.get_viewport().get_visible_rect().size.x / 2.0, 1.0, "centred")
	main._show_notice("x")
	check((main.get_node("Hud/Notice") as Control).position.y >= line.end.y, "a notice goes under the referee's line")
	_check_controls(main, true, true, "the person's turn again")
	main.free()


func test_the_opponents_stroke_is_lined_up_then_struck_exactly_as_given_under_its_id() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main)
	var cue: CueView = main.get_node("Cue")
	var setup: ShotSetup = main.setup
	var given := {"aim_rad": 0.35, "tip_x": 0.1, "tip_y": -0.2, "elevation_rad": 0.05, "speed_m_s": 2.0}
	_engine.standin_shots.append(_standin(given))
	# Short waits, long enough to see the cue stand on its line before it strikes.
	m.pace = 0.3
	await _person_stroke(main, _frame(1))
	var mine := [setup.speed_m_s, setup.power, setup.last_stroke_speed_m_s]
	check_eq(m.strike_blocked(), "opponent", "the opponent's turn")
	_check_controls(main, false, false, "while the opponent thinks, no stroke to show")
	check(not cue.visible, "no cue while it thinks")
	_mark_log()
	await _until(func() -> bool: return setup.aim_rad == 0.35 or main.is_playing(), "the opponent's stroke")
	check(not main.is_playing(), "its cue stands on the line first")
	check(cue.visible and cue.axis().dot(TableSpace.core_to_godot(cos(0.35), sin(0.35))) > 0.99, "the cue shows on its line")
	check_eq([setup.tip_x, setup.tip_y, setup.speed_m_s], [0.1, -0.2, 2.0], "its strike is on the controls")
	check(not (main.get_node("Guides/AimLine") as Node3D).visible, "with no guides")
	var pull: PullControl = main.get_node("Hud/PullCue")
	check(pull.visible and not (main.get_node("Hud/StrokeCue") as Control).visible, "its power on the power bar")
	check_near(pull.shown_power(), PullControl.power_for_speed(2.0, Physics.param("max_cue_speed_m_s", 10.0)), 1e-9,
			"at its speed")
	check(pull.mouse_filter == Control.MOUSE_FILTER_IGNORE and pull.modulate.a < 1.0, "dimmed, taking no touch")
	var rig: CameraRig = main.get_node("CameraRig")
	_land(main)
	check(rig.transform.is_equal_approx(rig.pose_for(main._cue_xy(), 0.35, setup.effective_elevation())),
			"the camera is behind its cue")
	await _until(main.is_playing, "the opponent's strike")
	var shot_id: String = _last_call("standin_shot")[2]["shot_id"]
	var inputs := _logged("shot.input")
	var simulated := _logged("shot.simulated")
	check(inputs.size() == 1 and simulated.size() == 1, "logged and simulated once")
	if inputs.size() == 1 and simulated.size() == 1:
		check_eq([inputs[0]["shot_id"], inputs[0]["mode"], inputs[0]["strike_mode"]], [shot_id, "match", "opponent"],
				"under the id the stand-in logged its decision with")
		var played: Dictionary = simulated[0]["bundle"]["strike"]
		for key: String in given:
			check_eq(played[key], given[key], "struck exactly as given: %s" % key)
		check_eq(simulated[0]["context"], {"shot_id": shot_id, "mode": "match", "strike_mode": "opponent"}, "context")
	_engine.judgements.append(_judged(_frame(0)))
	(main.get_node("Gestures") as Gestures).tapped.emit(Vector2(900.0, 400.0))
	await _until_idle(main)
	check_eq(_last_call("judge")[3], {"shot_id": shot_id}, "a tap skips it, and it is judged under that id")
	check_eq([setup.speed_m_s, setup.power, setup.last_stroke_speed_m_s], mine, "the person's own cue is back on the controls")
	check(pull.shown_power() < 0.0 and not pull.visible, "and the opponent's power is off the bar")
	_check_controls(main, true, true, "the person's turn")
	main.free()


func test_the_opponent_places_its_cue_ball_and_declares_its_colour() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main)
	var shot := _standin({"aim_rad": 0.1, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.5})
	shot["places_cue"] = true
	shot["cue"] = [-1.2, 0.1]
	shot["nominated"] = "black"
	_engine.standin_shots.append(shot)
	var in_hand := COLOUR_OF_CHOICE.duplicate(true)
	in_hand["cue_in_hand"] = true
	await _person_stroke(main, _frame(1, in_hand))
	check(not m.placing() and not (main.get_node("Hud/CuePlacer") as CuePlacer).is_placing(),
			"the person places nothing for the opponent")
	_mark_log()
	await _until(main.is_playing, "the opponent's strike")
	var placed := _last_call("place_cue")
	check(Vector2(placed[1], placed[2]).distance_to(Vector2(-1.2, 0.1)) < 1e-6, "its cue ball is placed where it says")
	var simulated := _logged("shot.simulated")
	if simulated.size() == 1:
		var start: Array = simulated[0]["bundle"]["start"]["balls"][CUE]["r"]
		check(Vector2(start[0], start[1]).distance_to(Vector2(-1.2, 0.1)) < 1e-6, "and the stroke is played from there")
	else:
		check(false, "the stroke was simulated once: %d" % simulated.size())
	_engine.judgements.append(_judged(_frame(0)))
	main.skip_shot()
	await _until_idle(main)
	check_eq(_last_call("judge")[2], {"nominated": "black"}, "the referee is given its colour")
	var how := _logged("match.nominated")
	check(how.size() >= 1 and how[0]["ball"] == "black" and how[0]["how"] == "opponent", "declared as the opponent's: %s" % [how])
	main.free()


func test_the_stand_in_plays_for_the_person_under_autoplay() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var given := {"aim_rad": -0.2, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.0}
	_engine.standin_shots.append(_standin(given))
	_engine.judgements.append(_judged(_frame(0)))
	_mark_log()
	var started: Dictionary = main.harness_start_match({"best_of": 1, "first_breaker": "you", "seed": 7}, true, true)
	check_eq(started.get("match_id"), (main.get_node("Match") as MatchController).match_id(), "started")
	_check_controls(main, false, false, "the person's controls while the stand-in thinks for them")
	await _until(func() -> bool: return _engine.called().has("judge"), "the stand-in's stroke")
	var inputs := _logged("shot.input")
	check(inputs.size() == 1 and inputs[0]["strike_mode"] == "autoplay", "struck as autoplay: %s" % [inputs])
	var playback := _logged("shot.playback")
	check(playback.size() == 1 and playback[0]["skipped"] == true, "fast skips the playback")
	main.free()


func test_a_choice_is_offered_low_and_applied_when_chosen() -> void:
	var fouled := _json(FRAME_VIEW)
	fouled["balls"] = HarnessScript.game_balls(fouled["balls"])["balls"]
	var main: MainScript = await _spawn(fouled)
	var m := _start(main)
	var hud := main.get_node("Hud")
	var strip: ChoiceStrip = hud.get_node("ChoiceStrip")
	check(strip.is_offering(), "the strip asks")
	for kind: String in ["play", "play_again", "replace"]:
		check(strip.button(kind) != null, "with %s" % kind)
	check_eq(strip.button("play").text, ChoiceStrip.FREE_BALL_PLAY, "the free ball is in its words")
	check_eq(m.strike_blocked(), "choice", "no stroke meanwhile")
	_check_controls(main, false, false, "at a choice")
	var canvas := main.get_viewport().get_visible_rect().size
	var centre: Rect2 = HudScript.layout(SettingsPanel.canvas_safe_rect(strip), Vector2.ZERO, canvas, "", false)["centre"]
	check(centre.grow(1.0).encloses(strip.covered_rect()), "between the spin ball and the power cue: %s in %s"
			% [strip.covered_rect(), centre])
	check_near(strip.covered_rect().get_center().x, canvas.x / 2.0, 1.0, "about the screen's middle")
	check(strip.covered_rect().end.y > canvas.y * 0.9, "along the bottom")

	strip.button("replace").pressed.emit()
	check(strip.is_offering() and (hud.get_node("Notice") as Label).visible, "a choice the engine refuses is asked again")
	var replaced: Array = (fouled["original"]["balls"] as Array).duplicate(true)
	replaced = HarnessScript.game_balls(replaced)["balls"]
	replaced[RED]["x"] = 0.7
	_engine.decisions.append({"ok": true, "error": "", "calls": [], "event": "none", "frame": _frame(0), "balls": replaced})
	strip.button("replace").pressed.emit()
	check_eq(_last_call("decide"), ["decide", "replace", 0], "the choice goes to the engine")
	check(not strip.is_offering(), "and the strip goes")
	_check_nodes_match(main, replaced, "the balls put back are on the table")
	_check_controls(main, true, true, "the person's turn")
	main.free()


func test_the_opponents_choice_is_said() -> void:
	var main: MainScript = await _spawn(_frame(0))
	_start(main)
	var fouled := _frame(1, {"pending": "after_foul", "decider": 1, "offender": 0, "options": [[], ["play", "play_again"]]})
	_engine.standin_choices.append("play_again")
	_engine.decisions.append({"ok": true, "error": "", "calls": [], "event": "none", "frame": _frame(0)})
	await _person_stroke(main, fouled)
	await _until(func() -> bool: return _engine.called().has("decide"), "the opponent's choice")
	var notice: Label = main.get_node("Hud/Notice")
	check(notice.visible and notice.text == MainScript.OPPONENT_CHOSE["play_again"], "the person is told: %s" % notice.text)
	_check_controls(main, true, true, "and plays again")
	main.free()


func test_pause_resumes_concedes_and_holds_the_opponent() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var m := _start(main)
	var hud := main.get_node("Hud")
	var pause: PauseMenu = hud.get_node("PauseMenu")
	var board: Scoreboard = hud.get_node("Scoreboard")
	var summary: FrameSummary = hud.get_node("FrameSummary")
	_mark_log()
	main._show_notice("said before the menu opened")
	board.pause_button().pressed.emit()
	check(pause.visible and not pause.button("concede_frame").disabled, "the pause menu, with Concede frame as the engine offers it")
	check(not (hud.get_node("Notice") as Control).visible, "with no earlier notice left over it")
	pause.button("resume").pressed.emit()
	check(not pause.visible and m.is_active(), "Resume goes back to the match")
	board.pause_button().pressed.emit()
	pause.button("settings").pressed.emit()
	var settings: SettingsPanel = hud.get_node("SettingsPanel")
	check(settings.visible and pause.visible, "Settings opens over the menu")
	settings.close()

	# The opponent's turn waits while the menu is up.
	pause.button("resume").pressed.emit()
	_engine.standin_shots.append(_standin(STRIKE))
	await _person_stroke(main, _frame(1))
	board.pause_button().pressed.emit()
	check(pause.button("concede_frame").disabled == false, "still offered")
	for i in 5:
		await tree.process_frame
	check_eq(_engine.called().count("standin_shot"), 0, "the opponent waits behind the menu")
	pause.button("resume").pressed.emit()
	await _until(main.is_playing, "the opponent's stroke once resumed")
	board.pause_button().pressed.emit()
	check(pause.button("concede_frame").disabled, "no conceding while a stroke is in play")
	pause.button("resume").pressed.emit()
	_engine.judgements.append(_judged(_frame(0)))
	main.skip_shot()
	await _until_idle(main)

	board.pause_button().pressed.emit()
	pause.button("concede_frame").pressed.emit()
	check_eq(_engine.called().count("decide"), 0, "Concede frame asks first")
	pause.button("confirm").pressed.emit()
	check(pause.visible and (hud.get_node("Notice") as Label).visible, "a concession the engine refuses leaves the menu up")
	var over := _frame(0, {"over": true, "winner": 1, "end_reason": "conceded"})
	_engine.match_view["history"].append({"number": 2, "breaker": 1, "winner": 1, "reason": "conceded", "scores": [24, 31],
			"stats": over["stats"], "strokes": 43, "reracks": 0})
	_engine.match_view["frames"] = [0, 2]
	_engine.decisions.append({"ok": true, "error": "", "calls": [], "event": "frame_ended", "frame": over})
	pause.button("concede_frame").pressed.emit()
	pause.button("confirm").pressed.emit()
	check_eq(_last_call("decide"), ["decide", "concede_frame", 0], "the concession goes to the engine")
	check(not pause.visible and summary.visible, "the frame's summary takes the menu's place")
	check_eq(summary.texts()["reason"], FrameSummary.reason_text("conceded"), "saying why it ended")
	_check_controls(main, false, false, "at the summary")
	var screens := _screens()
	check_eq(screens.back(), ["pause", "frame_summary"], "the last change of screen: %s" % [screens])
	check(screens.has(["match", "pause"]) and screens.has(["pause", "match"]), "pause and back are logged")
	main.free()


func test_the_frames_end_leads_to_the_next_frame_and_the_last_to_the_match_summary_and_home() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var free: FreeTable = main.get_node("FreeTable")
	var free_balls: Array[Dictionary] = free.balls.duplicate(true)
	main.setup.set_aim(0.7)
	main.setup.set_tip(0.2, 0.3)
	main.setup.set_speed(2.5, 0.4)
	var mine := main.setup.snapshot()
	var m := _start(main)
	var hud := main.get_node("Hud")
	var frame_summary: FrameSummary = hud.get_node("FrameSummary")
	var match_summary: MatchSummary = hud.get_node("MatchSummary")
	var home: HomeScreen = hud.get_node("HomeScreen")

	var over := _frame(0, {"over": true, "winner": 0, "end_reason": "black", "scores": [70, 31]})
	var won := {"number": 2, "breaker": 1, "winner": 0, "reason": "black", "scores": [70, 31], "stats": over["stats"],
			"strokes": 60, "reracks": 0}
	_engine.match_view["history"].append(won)
	_engine.match_view["frames"] = [1, 1]
	_engine.judgements.append(_judged(over, {}, {"event": "frame_ended"}))
	main.harness_match_shot(STRIKE, "", null)
	main._show_notice("said of the last stroke")
	main.skip_shot()
	await _until_idle(main)
	check(frame_summary.visible, "the frame's summary shows")
	check(not (hud.get_node("Notice") as Control).visible, "with no earlier notice left over it")
	var rig: CameraRig = main.get_node("CameraRig")
	check(rig.is_held() and rig.transform.is_equal_approx(rig.broadcast_pose(CameraRig.FULL_TABLE_YAW)),
			"over the whole table, from the full-table view")
	check_eq([frame_summary.texts()["title"], frame_summary.texts()["score_you"], frame_summary.texts()["frames_you"]],
			["You won the frame", 70, 1], "with the frame and the match: %s" % [frame_summary.texts()])
	_check_controls(main, false, false, "at the summary")
	check(not (main.get_node("Cue") as Node3D).visible, "and no cue")
	# Between frames the engine offers no concession, so the pause menu is out of reach.
	var board: Scoreboard = hud.get_node("Scoreboard")
	check(frame_summary.get_index() > board.get_index() and (frame_summary.get_node("Backdrop") as Control).mouse_filter
			== Control.MOUSE_FILTER_STOP, "the summary covers the pause button")
	board.pause_button().pressed.emit()
	check(not (hud.get_node("PauseMenu") as Control).visible, "which opens no menu behind it")
	var decided := _engine.called().count("decide")
	check(not m.concede_match() and not m.concede_frame(), "and nothing can be conceded")
	check_eq(_engine.called().count("decide"), decided, "without the engine being asked")

	var next := _frame(1, {"scores": [0, 0]})
	next["balls"][CUE]["x"] = -1.2
	_engine.racks.append(next)
	_engine.match_view["frame_number"] = 3
	_engine.standin_shots.append(_standin(STRIKE))
	frame_summary.button("continue").pressed.emit()
	check(not frame_summary.visible and _engine.called().has("next_frame"), "Continue racks the next frame")
	check(not rig.is_held() and rig.is_easing(), "and the camera comes down to it")
	_check_nodes_match(main, next["balls"], "its balls are on the table")
	check_eq((hud.get_node("Scoreboard") as Scoreboard).texts()["you"], 0, "and its scores on the scoreboard")

	# The opponent wins the decider with its first stroke.
	var lost := _frame(1, {"over": true, "winner": 1, "end_reason": "black", "scores": [0, 80]})
	_engine.match_view["history"].append({"number": 3, "breaker": 0, "winner": 1, "reason": "black", "scores": [0, 80],
			"stats": lost["stats"], "strokes": 1, "reracks": 0})
	_engine.match_view["frames"] = [1, 2]
	_engine.match_view["over"] = true
	_engine.match_view["winner"] = 1
	_engine.judgements.append(_judged(lost, {}, {"event": "match_ended"}))
	await _until(main.is_playing, "the opponent's stroke")
	main.skip_shot()
	await _until_idle(main)
	check(frame_summary.visible and not match_summary.visible, "the last frame's summary comes first")
	check(not m.is_active(), "the match is over")
	frame_summary.button("continue").pressed.emit()
	check(match_summary.visible and not frame_summary.visible, "then the match's")
	check_eq([match_summary.texts()["title"], match_summary.texts()["frames_you"], match_summary.texts()["frames_opponent"]],
			["You lost", 1, 2], "with the result: %s" % [match_summary.texts()])
	check_eq(_engine.called().count("next_frame"), 1, "no frame is racked after the last")
	match_summary.button("home").pressed.emit()
	check(home.visible and not match_summary.visible, "its button returns Home")
	check_eq(_screens(), [["home", "match_setup"], ["match_setup", "match"], ["match", "frame_summary"],
			["frame_summary", "match"], ["match", "frame_summary"], ["frame_summary", "match_summary"],
			["match_summary", "home"]], "every change of screen is logged")

	# The free table is as it was left, and reachable.
	home.button("free_table").pressed.emit()
	check_eq(main.harness_state()["screen"], "table", "the free table opens")
	check_eq(free.balls, free_balls, "its balls as they were")
	check_eq(free.history_size(), 1, "its history untouched")
	_check_nodes_match(main, free_balls, "and on the table")
	check_eq(main.setup.snapshot(), mine, "with the cue as it was left on the controls")
	check((hud.get_node("FreeTableBar") as Control).visible and not (hud.get_node("Scoreboard") as Control).visible,
			"its bar, and no scoreboard")
	check(not main.harness_play_shot(STRIKE).is_empty(), "and a shot plays on it")
	main.skip_shot()
	await _until_idle(main)
	check_eq(free.history_size(), 2, "into its history")
	main.free()


func test_the_full_table_view_keeps_the_pockets_clear_of_the_scoreboard() -> void:
	var main: MainScript = await _spawn(_frame(0))
	_start(main)
	var rig: CameraRig = main.get_node("CameraRig")
	var board := (main.get_node("Hud/Scoreboard") as Scoreboard).covered_rect()
	var pose := rig.broadcast_pose(CameraRig.FULL_TABLE_YAW)
	for pocket: Dictionary in Physics.table_shape()["pockets"]:
		var centre: Array = pocket["drop_centre"]
		var at := rig.project(pose, TableSpace.core_to_godot(centre[0], centre[1]), CameraRig.BROADCAST_FOV_DEG)
		check(not board.has_point(at), "pocket %s at %s is clear of the scoreboard %s" % [pocket.get("name", "?"), at, board])
	main.free()


func test_the_harness_drives_a_match() -> void:
	var main: MainScript = await _spawn(_frame(0, COLOUR_OF_CHOICE))
	var harness: Node = HarnessScript.new()
	harness.scene_provider = func() -> Node: return main
	tree.root.add_child(harness)
	var m: MatchController = main.get_node("Match")

	var r: Dictionary = await harness.run_command("open_screen", {"name": "match_setup"})
	check(r["ok"] and r["result"] == {"screen": "match_setup"} and main.harness_state()["screen"] == "match_setup",
			"open_screen match_setup: %s" % r)
	r = await harness.run_command("open_screen", {"name": "table"})
	check(r["ok"] and main.harness_state()["screen"] == "table", "open_screen table: %s" % r)
	check(not (main.get_node("CameraRig") as CameraRig).is_easing(), "answered once the camera has landed")
	r = await harness.run_command("open_screen", {"name": "home"})
	check(r["ok"] and main.harness_state()["screen"] == "home", "open_screen home: %s" % r)
	r = await harness.run_command("open_screen", {"name": "pause"})
	check(not r["ok"] and str(r["error"]).contains("home, match_setup, table"), "only those three open: %s" % r)

	r = await harness.run_command("match_wait", {"timeout_s": 0})
	check(not r["ok"] and str(r["error"]).contains("no match"), "match_wait with no match: %s" % r)
	r = await harness.run_command("start_match", {"best_of": 4})
	check(not r["ok"] and str(r["error"]).contains("best_of"), "a length that is no match length: %s" % r)
	r = await harness.run_command("start_match", {"first_breaker": "me"})
	check(not r["ok"] and str(r["error"]).contains("first_breaker"), "an unknown breaker: %s" % r)
	r = await harness.run_command("start_match", {"best_of": 5, "first_breaker": "you", "seed": 147})
	check(r["ok"] and r["result"] == {"match_id": m.match_id()} and m.is_active(), "start_match: %s" % r)
	check_eq(_last_call("start")[1], {"best_of": 5, "first_breaker": 0, "seed": 147, "concede_any_time": [true, false], "person": 0},
			"with the config asked for")
	r = await harness.run_command("match_state", {})
	check(r["ok"] and r["result"]["waiting"] == "shot" and r["result"]["match_id"] == m.match_id(), "match_state: %s" % [r.get("error")])
	r = await harness.run_command("dump_state", {})
	check(r["ok"] and r["result"]["mode"] == "match" and r["result"]["match"]["active"] == true
			and r["result"]["screen"] == "match", "dump_state has the mode and the match")
	r = await harness.run_command("match_wait", {"timeout_s": 5})
	check(r["ok"] and r["result"]["waiting"] == "shot", "match_wait returns at the person's stroke: %s" % r)

	r = await harness.run_command("match_pause", {"on": false})
	check(not r["ok"] and str(r["error"]).contains("not open"), "nothing to resume from: %s" % r)
	r = await harness.run_command("match_pause", {"on": true})
	check(r["ok"] and r["result"] == {"on": true, "can_concede": true, "can_concede_match": true, "can_claim": false}
			and main.harness_state()["screen"] == "pause",
			"match_pause opens the pause menu: %s" % r)
	r = await harness.run_command("match_pause", {"on": true})
	check(not r["ok"], "it is open already")
	r = await harness.run_command("match_pause", {"on": false})
	check(r["ok"] and r["result"]["on"] == false and main.harness_state()["screen"] == "match", "and resumes: %s" % r)
	r = await harness.run_command("match_pause", {})
	check(not r["ok"], "on is needed")

	r = await harness.run_command("load_position", {"name": "straight_1m"})
	check(not r["ok"] and str(r["error"]).contains("a match is on"), "load_position is refused in a match, with why: %s" % r)
	r = await harness.run_command("play_shot", STRIKE)
	check(not r["ok"] and str(r["error"]).contains("a match is on"), "so is play_shot: %s" % r)
	check(m.is_active() and m.balls.size() == 9, "and the match is not disturbed")

	r = await harness.run_command("match_shot", STRIKE)
	check(not r["ok"] and str(r["error"]).contains("nominate"), "a stroke with a colour still to declare: %s" % r)
	r = await harness.run_command("match_shot", _with(STRIKE, {"nominated": "red"}))
	check(not r["ok"] and str(r["error"]).contains("nominated"), "a ball that can't be declared: %s" % r)
	r = await harness.run_command("match_shot", _with(STRIKE, {"cue": [-1.2, 0.0], "nominated": "blue"}))
	check(not r["ok"] and str(r["error"]).contains("in hand"), "a cue ball that is not in hand: %s" % r)
	var in_hand := COLOUR_OF_CHOICE.duplicate(true)
	in_hand["cue_in_hand"] = true
	_engine.judgements.append(_judged(_frame(0, in_hand)))
	r = await harness.run_command("match_shot", _with(STRIKE, {"nominated": "black"}))
	check(r["ok"] and str(r["result"].get("shot_id", "")).begins_with(Log.session()), "match_shot: %s" % r)
	r = await harness.run_command("match_wait", {"timeout_s": 0})
	check(not r["ok"] and str(r["error"]).contains("still waiting"), "match_wait times out while the stroke plays: %s" % r)
	r = await harness.run_command("wait_idle", {"timeout_s": 0.01})
	check(not r["ok"], "wait_idle sees the stroke playing")
	main.skip_shot()
	check_eq(_last_call("judge")[2], {"nominated": "black"}, "played with the colour given")
	r = await harness.run_command("match_shot", _with(STRIKE, {"cue": [-1.25, 0.05], "nominated": "pink", "speed_m_s": 1.25}))
	check(r["ok"], "match_shot with the cue ball placed: %s" % r)
	var placed := _last_call("place_cue")
	check(Vector2(placed[1], placed[2]).distance_to(Vector2(-1.25, 0.05)) < 1e-6, "at the place given")
	check(not m.placing(), "and confirmed")
	var fouled := _json(FRAME_VIEW)
	fouled["balls"] = HarnessScript.game_balls(fouled["balls"])["balls"]
	_engine.judgements.append(_judged(fouled))
	main.skip_shot()
	r = await harness.run_command("match_wait", {"timeout_s": 5})
	check(r["ok"] and r["result"]["waiting"] == "choice", "match_wait returns at a choice: %s" % r)
	r = await harness.run_command("match_choose", {"kind": "accept_rerack"})
	check(not r["ok"] and str(r["error"]).contains("play_again"), "a choice not offered, with what is: %s" % r)
	_engine.decisions.append({"ok": true, "error": "", "calls": [], "event": "none", "frame": _frame(0)})
	r = await harness.run_command("match_choose", {"kind": "play"})
	check(r["ok"] and r["result"] == {"kind": "play"}, "match_choose: %s" % r)
	check(not (main.get_node("Hud/ChoiceStrip") as ChoiceStrip).is_offering(), "the strip goes with it")

	# The stand-in plays on for both under autoplay, to the frame's end.
	var over := _frame(1, {"over": true, "winner": 1, "end_reason": "black"})
	_engine.match_view["history"].append({"number": 2, "breaker": 1, "winner": 1, "reason": "black", "scores": [24, 31],
			"stats": over["stats"], "strokes": 44, "reracks": 0})
	_engine.match_view["frames"] = [0, 2]
	_engine.match_view["over"] = true
	_engine.match_view["winner"] = 1
	_engine.standin_shots.append(_standin(STRIKE))
	_engine.standin_shots.append(_standin(STRIKE))
	_engine.judgements.append(_judged(_frame(1)))
	_engine.judgements.append(_judged(over, {}, {"event": "match_ended"}))
	r = await harness.run_command("match_autoplay", {"on": true, "fast": true})
	check(r["ok"] and r["result"] == {"on": true, "fast": true}, "match_autoplay: %s" % r)
	r = await harness.run_command("match_wait", {"timeout_s": 10})
	check(r["ok"] and r["result"]["waiting"] == "frame_summary", "match_wait returns at the last frame's summary: %s" % r)
	r = await harness.run_command("match_state", {})
	check(r["ok"] and r["result"]["waiting"] == "frame_summary", "match_state says the same: %s" % [r.get("error")])
	check_eq(_engine.called().count("standin_shot"), 2, "the stand-in played both strokes")
	r = await harness.run_command("match_continue", {})
	check(r["ok"] and r["result"] == {"screen": "match_summary"}, "match_continue from the last frame's summary: %s" % r)
	r = await harness.run_command("match_wait", {"timeout_s": 1})
	check(r["ok"] and r["result"]["waiting"] == "match_summary", "then match_wait returns at the match's: %s" % r)
	r = await harness.run_command("match_continue", {})
	check(r["ok"] and r["result"] == {"screen": "home"}, "and from the match's, Home: %s" % r)
	r = await harness.run_command("match_continue", {})
	check(not r["ok"], "nothing to continue at Home")
	r = await harness.run_command("match_autoplay", {"on": false})
	check(not r["ok"], "no match to autoplay")

	# From Home the free table's commands enter the free table.
	r = await harness.run_command("load_position", {"name": "straight_1m"})
	check(r["ok"] and main.harness_state()["screen"] == "table", "load_position enters the free table: %s" % r)
	check_eq((main.get_node("FreeTable") as FreeTable).balls.size(), 2, "with the position on it")
	r = await harness.run_command("open_screen", {"name": "home"})
	r = await harness.run_command("play_shot", STRIKE)
	check(r["ok"] and main.harness_state()["screen"] == "table" and main.is_playing(), "so does play_shot: %s" % r)
	main.skip_shot()
	harness.free()
	main.free()


func test_demo_screens_show_for_pictures() -> void:
	var main: MainScript = await _spawn(_frame(0))
	var hud := main.get_node("Hud")
	var harness: Node = HarnessScript.new()
	harness.scene_provider = func() -> Node: return main
	tree.root.add_child(harness)
	var shown: Dictionary = await harness._show_demo_screen("pause")
	check(shown["ok"] and (hud.get_node("PauseMenu") as Control).visible, "pause: %s" % shown)
	check_eq(main.harness_state()["screen"], "pause", "the screen")
	(hud.get_node("PauseMenu") as PauseMenu).button("resume").pressed.emit()
	check_eq(main.harness_state()["screen"], "home", "its Resume closes it")
	shown = await harness._show_demo_screen("frame_summary")
	var frame_summary: FrameSummary = hud.get_node("FrameSummary")
	check(shown["ok"] and frame_summary.visible and frame_summary.texts()["score_opponent"] == 63,
			"frame_summary from the example match: %s" % [frame_summary.texts()])
	frame_summary.button("continue").pressed.emit()
	check(not frame_summary.visible and main.harness_state()["screen"] == "home", "its Continue closes it")
	shown = await harness._show_demo_screen("match_summary")
	var match_summary: MatchSummary = hud.get_node("MatchSummary")
	check(shown["ok"] and match_summary.visible, "match_summary: %s" % shown)
	check_eq([match_summary.texts()["title"], match_summary.texts()["frames"]], ["You lost", ["41-63", "24-31"]],
			"a finished match made of the example: %s" % [match_summary.texts()])
	match_summary.button("home").pressed.emit()
	check(not match_summary.visible and main.harness_state()["screen"] == "home", "its Home closes it")
	shown = await harness._show_demo_screen("match")
	check(shown["ok"] and main.harness_state()["mode"] == "match", "match starts one: %s" % shown)
	check_eq(_last_call("start")[1]["first_breaker"], 0, "the person to break")
	shown = await harness._show_demo_screen("match_setup")
	check(shown["ok"] and (hud.get_node("MatchSetupScreen") as Control).visible and main.harness_state()["mode"] == "home",
			"match_setup leaves the match for the setup: %s" % shown)
	shown = await harness._show_demo_screen("lobby")
	check(not shown["ok"] and str(shown["error"]).contains("known"), "an unknown screen: %s" % shown)
	check_eq(_errors_with("FrameSummary").size() + _errors_with("MatchSummary").size(), 0, "the summaries lacked nothing")
	harness.free()
	main.free()


func test_the_preview_scenes_arguments_are_not_the_harnesss() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(["--screen=pause", "--state=placing", "--frames=5", "--person=1",
			"--demo-screen=match_summary"]), PackedStringArray())
	check_eq(p["invalid"].size(), 0, "the previews' own arguments raise no warning")
	check_eq(p["demo_screen"], "match_summary", "and the harness's are still read")
	check_eq(HarnessScript.parse_args(PackedStringArray(["--screen"]), PackedStringArray())["invalid"],
			PackedStringArray(["--screen"]), "without a value they are reported")


## The scene at Home with a scripted engine on its MatchController that will show `frame` when a
## match starts. The aim stays where a test puts it.
func _spawn(frame: Dictionary) -> MainScript:
	for key: String in Settings.DEFAULTS:
		Settings.set_value(key, Settings.DEFAULTS[key])
	var main := load("res://scenes/main.tscn").instantiate() as MainScript
	tree.root.add_child(main)
	await tree.process_frame
	_engine = Scripted.new()
	# The stand-in plays these, as when no level could be configured: the setup then offers none.
	_engine.level_list = []
	_engine.frame_view = frame
	_engine.match_view = _json(MATCH)
	_engine.match_view.erase("frame")
	_engine.ball_list = (frame["balls"] as Array).duplicate(true)
	(main.get_node("Match") as MatchController).engine = _engine
	main.choose_among = func(_balls: Array[Dictionary], _cue_ball: int, _places: Array) -> Dictionary: return {}
	_mark_log()
	return main


## Home, Play, Start: the match is on, the opponent waits no time and the camera has landed.
func _start(main: MainScript) -> MatchController:
	var hud := main.get_node("Hud")
	(hud.get_node("HomeScreen") as HomeScreen).button("play").pressed.emit()
	((hud.get_node("MatchSetupScreen") as MatchSetupScreen).control("start") as Button).pressed.emit()
	var m: MatchController = main.get_node("Match")
	check(m.is_active(), "the match started")
	m.pace = 0.0
	_land(main)
	return m


## The person's stroke, skipped to its end and judged as leaving `after`.
func _person_stroke(main: MainScript, after: Dictionary) -> void:
	_engine.judgements.append(_judged(after))
	var shot: Dictionary = main.harness_match_shot(STRIKE, "", null)
	check(shot.has("shot_id"), "the person's stroke: %s" % shot)
	main.skip_shot()
	await _until_idle(main)


## The camera where it is going, at once.
func _land(main: MainScript) -> void:
	main._flush_refresh()
	(main.get_node("CameraRig") as CameraRig)._process(CameraRig.RETURN_S + CameraRig.BLEND_S)


## The shot controls: whether they show, and whether they are the person's (full strength, taking
## touches, the aim theirs) or dimmed and dead.
func _check_controls(main: MainScript, shown: bool, live: bool, when: String) -> void:
	var hud := main.get_node("Hud")
	for control_name: String in ["SpinWidget", "ElevationControl", "StrokeCue"]:
		var control: Control = hud.get_node(control_name)
		check_eq(control.visible, shown, "%s: %s shown" % [when, control_name])
		if shown:
			check_eq(control.mouse_filter, Control.MOUSE_FILTER_STOP if live else Control.MOUSE_FILTER_IGNORE,
					"%s: %s takes touches" % [when, control_name])
			check_near(control.modulate.a, 1.0 if live else MainScript.PLAYING_ALPHA, 1e-6, "%s: %s strength" % [when, control_name])
	check_eq((main.get_node("AimControl") as AimControl).enabled, live, "%s: aiming" % when)


## The example frame view with nothing pending and `striker` to play; `changes` replaces frame
## keys, or the view's for "options", "nominations" and "on_places". Its balls are the game's
## ball list.
func _frame(striker: int, changes: Dictionary = {}) -> Dictionary:
	var frame := _json(FRAME_VIEW)
	frame["balls"] = HarnessScript.game_balls(frame["balls"])["balls"]
	frame["pending"] = "none"
	frame["striker"] = striker
	frame["free_ball"] = false
	frame["view"]["nominations"] = []
	frame["view"]["options"] = [["concede_frame", "concede_match"], []]
	for key: String in changes:
		if key == "options" or key == "nominations" or key == "on_places":
			frame["view"][key] = changes[key]
		else:
			frame[key] = changes[key]
	return frame


## What judge answers: `frame` after the stroke, a clean judgement with `judgement`'s keys over
## it, and `more` over the answer (its "event", its "balls").
func _judged(frame: Dictionary, judgement: Dictionary = {}, more: Dictionary = {}) -> Dictionary:
	var clean := {"fouls": [], "penalty": 0, "miss": false, "miss_reason": "", "points": 0, "nominated": "",
			"free_ball_used": false, "spotted": [], "calls": []}
	clean.merge(judgement, true)
	var answer := {"ok": true, "error": "", "judgement": clean, "event": "none", "frame": frame}
	answer.merge(more, true)
	return answer


func _standin(strike: Dictionary) -> Dictionary:
	return {"ok": true, "error": "", "places_cue": false, "cue": [], "nominated": "", "strike": strike.duplicate(),
			"why": {"kind": "pot"}, "us": 120}


func _with(base: Dictionary, more: Dictionary) -> Dictionary:
	var out := base.duplicate()
	out.merge(more, true)
	return out


## The aim from the cue ball to the ball at `place`.
func _aim_at(m: MatchController, place: int) -> float:
	return (Vector2(float(m.balls[place]["x"]), float(m.balls[place]["y"]))
			- Vector2(float(m.balls[m.cue_ball]["x"]), float(m.balls[m.cue_ball]["y"]))).angle()


func _screen_of(main: MainScript, place: int) -> Vector2:
	var camera: Camera3D = main.get_node("CameraRig/Camera3D")
	return camera.unproject_position((main.get_node("Balls").get_child(place) as Node3D).global_position)


func _touch(at: Vector2, pressed: bool) -> void:
	var e := InputEventScreenTouch.new()
	e.pressed = pressed
	e.position = at
	tree.root.push_input(e, true)


func _drag_to(from: Vector2, to: Vector2) -> void:
	var e := InputEventScreenDrag.new()
	e.position = to
	e.relative = to - from
	e.screen_relative = e.relative
	tree.root.push_input(e, true)


func _until_idle(main: MainScript) -> void:
	for i in 120:
		if not main.harness_busy():
			return
		await tree.process_frame
	failures.append("still busy after 120 frames")


## Waits, three seconds at most, for `done`.
func _until(done: Callable, what: String) -> void:
	var deadline := Time.get_ticks_msec() + 3000
	while not done.call():
		if Time.get_ticks_msec() > deadline:
			failures.append("no %s within 3 s" % what)
			return
		await tree.process_frame


func _check_nodes_match(main: Node, balls: Array, when: String) -> void:
	var nodes := main.get_node("Balls").get_children()
	check_eq(nodes.size(), balls.size(), "%s: a node per ball" % when)
	for i in mini(nodes.size(), balls.size()):
		var xy: Array = nodes[i].get_meta("core_xy")
		check_eq([xy[0], xy[1]], [float(balls[i]["x"]), float(balls[i]["y"])], "%s: node %d (%s) where its entry is" % [when, i, nodes[i].name])
		check((nodes[i] as Node3D).visible, "%s: node %d shown" % [when, i])


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


func _last_call(method: String) -> Array:
	for i in range(_engine.calls.size() - 1, -1, -1):
		if _engine.calls[i][0] == method:
			return _engine.calls[i]
	check(false, "%s was never called" % method)
	return [method, null, null, null]


## Each screen.changed since _mark_log, as [from, to].
func _screens() -> Array[Array]:
	var out: Array[Array] = []
	for line in _logged("screen.changed"):
		out.append([line["from"], line["to"]])
	return out


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


func _errors_with(text: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for data in _logged("app.error"):
		if String(data.get("message", "")).contains(text):
			found.append(data)
	return found
