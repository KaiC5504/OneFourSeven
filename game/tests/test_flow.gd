extends TestCase
## The free table end to end on the real scene (spec 16.3): scripted touches and keys pushed into
## the viewport, the shot logged, simulated, played and kept in the history; edit mode, live
## parameters, Push and Pull and release, a refused shot, the camera through a shot and the
## overview, and the harness's hooks.

const MainScript := preload("res://scripts/main.gd")
const SCHEMA_PATH := "res://data/log_schema.json"
const STROKE_DT := 1.0 / 120.0
const TARGET_SPEED_M_S := 2.5
## Canvas units of one-finger drag on the table, leftwards: the aim turns anticlockwise.
const AIM_DRAG := 100
const FINE_TUNE_STEPS := 30
## A point on the table clear of every control at the default window size.
const TABLE_POINT := Vector2(960.0, 300.0)
const PULL_UNITS := 312.0

var _log_start := 0
var _t := [0.0]


func _spawn() -> MainScript:
	_reset_settings()
	var main := load("res://scenes/main.tscn").instantiate() as MainScript
	tree.root.add_child(main)
	await tree.process_frame
	await tree.process_frame
	_mark_log()
	return main


func _reset_settings() -> void:
	for key: String in Settings.DEFAULTS:
		Settings.set_value(key, Settings.DEFAULTS[key])


func _finish(main: MainScript) -> void:
	main.free()
	_reset_settings()


func test_drag_fine_tune_and_stroke_play_a_shot_that_retake_undo_and_redo_walk_back() -> void:
	var main: MainScript = await _spawn()
	var setup: ShotSetup = main.setup
	var table: FreeTable = main.get_node("FreeTable")
	var aim: AimControl = main.get_node("AimControl")
	var rig: CameraRig = main.get_node("CameraRig")
	var guides: Node3D = main.get_node("Guides")
	var before: Array[Dictionary] = table.balls.duplicate(true)
	var start_aim := setup.aim_rad

	_table_drag(TABLE_POINT, Vector2(-AIM_DRAG, 0.0), 10)
	var expected_aim := start_aim + AIM_DRAG * aim.rad_per_px()
	check_near(setup.aim_rad, expected_aim, 1e-9, "the drag turned the aim")
	var wheel: Control = main.get_node("Hud/FineTuneWheel")
	_vertical_drag(wheel.get_global_rect().get_center(), -FineTuneWheel.PX_PER_STEP, FINE_TUNE_STEPS)
	expected_aim += deg_to_rad(FINE_TUNE_STEPS * FineTuneWheel.STEP_DEG)
	check_near(setup.aim_rad, expected_aim, 1e-9, "fine-tune +0.30°")
	await tree.process_frame
	var cue: CueView = main.get_node("Cue")
	check(cue.axis().dot(TableSpace.core_to_godot(cos(setup.aim_rad), sin(setup.aim_rad))) > 0.99, "the cue swung with the aim")
	setup.set_tip(0.2, -0.3)
	await tree.process_frame

	var push := StrokeControl.FASTEST_PUSH_PX_S * pow(TARGET_SPEED_M_S / Physics.param("max_cue_speed_m_s", 10.0), 1.0 / StrokeControl.PUSH_EXPONENT)
	_stroke(main, push)
	var struck := setup.snapshot()
	check(main.is_playing(), "the stroke struck")
	check(rig.is_held(), "the camera shows the shot")
	check(not aim.enabled, "no aiming while it plays")
	check(not guides.get_node("AimLine").visible, "guides hidden while it plays")
	var inputs := _logged("shot.input")
	check_eq(inputs.size(), 1, "one shot.input")
	var simulated := _logged("shot.simulated")
	check_eq(simulated.size(), 1, "one shot.simulated")
	if inputs.size() != 1 or simulated.size() != 1:
		_finish(main)
		return
	var input: Dictionary = inputs[0]
	_check_schema("shot.input", input)
	check_eq(input["mode"], "free_table", "mode")
	check_eq(input["strike_mode"], "stroke", "strike_mode")
	check_eq(int(input["guide_level"]), 1, "guide_level")
	check_near(input["aim_rad"], expected_aim, 1e-9, "logged aim = start + drag x rad_per_px + 0.30°")
	check_near(input["speed_m_s"], TARGET_SPEED_M_S, TARGET_SPEED_M_S * 0.01, "stroke speed within 1%")
	# Event positions are single precision, so the measured push is a hair off the one sent.
	check_near(input["speed_m_s"], StrokeControl.speed_for_push(push, Physics.param("max_cue_speed_m_s", 10.0)), 1e-4,
			"the speed for that push")
	check_near(input["tip_x"], 0.2, 1e-12, "tip x")
	check_near(input["tip_y"], -0.3, 1e-12, "tip y")
	check_eq((input["fine_tune_samples"] as Array).size(), FINE_TUNE_STEPS, "every fine-tune step recorded")
	check(not (input["stroke_samples"] as Array).is_empty(), "stroke samples recorded")
	var bundle: Dictionary = simulated[0]["bundle"]
	var strike: Dictionary = bundle["strike"]
	check_near(strike["aim_rad"], expected_aim, 1e-9, "simulated aim")
	check_eq(strike["speed_m_s"], input["speed_m_s"], "simulated speed is the logged one")
	check_eq(simulated[0]["context"], {"shot_id": input["shot_id"], "mode": "free_table", "strike_mode": "stroke"}, "context")
	check_eq((bundle["start"]["balls"] as Array).size(), before.size(), "every ball in the repro bundle")

	var player: ShotPlayer = main.get_node("ShotPlayer")
	for i in 4:
		player.advance(0.05)
	# A tap on the table skips to the end.
	_touch(TABLE_POINT, true)
	_touch(TABLE_POINT, false)
	await _until_idle(main)
	check(not main.is_playing(), "the shot ended")
	var playback := _logged("shot.playback")
	check(playback.size() == 1 and playback[0]["skipped"] == true, "played and skipped: %s" % [playback])
	check_eq(table.history_size(), 2, "the shot is in the history")
	check_eq(table.history_index(), 1, "and current")
	check(not rig.is_held(), "the camera follows again")
	check(aim.enabled, "aiming again")
	check(cue.visible, "the cue is back")
	check(guides.get_node("AimLine").visible or guides.get_node("GhostRing").visible or table.balls.size() == 1,
			"guides back")
	check_eq([setup.tip_x, setup.tip_y], [0.0, 0.0], "spin reset after the shot")
	var after: Array[Dictionary] = table.balls.duplicate(true)
	_check_nodes_match(main, after, "after the shot")
	var cue_node := main.get_node("Balls/cue") as Node3D
	check(not cue_node.basis.is_equal_approx(Basis.IDENTITY), "the cue ball turned as it rolled")
	var turned := cue_node.basis

	_key(KEY_R)
	check_eq(table.balls, before, "retake restores the balls bit for bit")
	_check_nodes_match(main, before, "after retake")
	check_eq(setup.snapshot(), struck, "and the setup as struck")
	check_eq(table.history_index(), 0, "back before the shot")
	check(main.get_node("Balls/cue") == cue_node, "the cue ball node is kept")
	check(cue_node.basis.is_equal_approx(turned), "its dots stay as they were")

	_key(KEY_Y, true)
	check_eq(table.history_index(), 1, "Ctrl+Y redoes")
	check_eq(table.balls, after, "redo brings the shot's result back")
	_check_nodes_match(main, after, "after redo")
	_key(KEY_Z, true)
	check_eq(table.history_index(), 0, "Ctrl+Z undoes")
	check_eq(table.balls, before, "undo goes back to the rack")
	_finish(main)


func test_settings_keep_spin_when_reset_is_off() -> void:
	var main: MainScript = await _spawn()
	Settings.set_value("reset_spin_after_shot", false)
	var setup: ShotSetup = main.setup
	setup.set_tip(-0.1, 0.25)
	setup.set_elevation(0.2)
	var shot_id: String = main.harness_play_shot({"aim_rad": 0.0, "tip_x": -0.1, "tip_y": 0.25,
			"elevation_rad": 0.2, "speed_m_s": 1.0})
	check(not shot_id.is_empty(), "played")
	main.skip_shot()
	await _until_idle(main)
	check_eq([setup.tip_x, setup.tip_y, setup.elevation_rad], [-0.1, 0.25, 0.2], "spin and elevation kept")
	_finish(main)


func test_edit_mode_moves_adds_and_removes_balls() -> void:
	var main: MainScript = await _spawn()
	var table: FreeTable = main.get_node("FreeTable")
	var aim: AimControl = main.get_node("AimControl")
	var bar: FreeTableBar = main.get_node("Hud/FreeTableBar")
	var palette: EditPalette = main.get_node("Hud/EditPalette")
	var camera: Camera3D = main.get_node("CameraRig/Camera3D")
	bar.button("edit").button_pressed = true
	await tree.process_frame
	check(table.edit_mode, "Edit turns edit mode on")
	check(not aim.enabled, "no aiming while editing")
	check(palette.visible, "the palette shows")
	check(not (main.get_node("Hud/StrokeCue") as Control).visible, "the shot controls step aside")
	check(not (main.get_node("Cue") as Node3D).visible, "and the cue")

	var blue := _find(table, "blue")
	var start_aim: float = main.setup.aim_rad
	var target := Vector2(0.3, 0.2)
	_table_drag(_screen_of_ball(main, "blue"), _screen_at(camera, target) - _screen_of_ball(main, "blue"), 12)
	await tree.process_frame
	check_near(table.balls[blue]["x"], target.x, 2e-3, "blue moved with the finger (x)")
	check_near(table.balls[blue]["y"], target.y, 2e-3, "blue moved with the finger (y)")
	check_eq(main.setup.aim_rad, start_aim, "the drag didn't aim")
	_check_nodes_match(main, table.balls, "after the move")
	var edits := _logged("freetable.edit")
	check(edits.size() == 1 and edits[0]["legal"] == true and edits[0]["ball"] == "blue", "move logged: %s" % [edits])
	var history := table.history_size()

	var moved: Dictionary = table.balls[blue].duplicate()
	_mark_log()
	_table_drag(_screen_of_ball(main, "blue"), _screen_of_ball(main, "pink") - _screen_of_ball(main, "blue"), 12)
	await tree.process_frame
	check_eq(table.balls[blue], moved, "a move onto the pink is refused")
	_check_nodes_match(main, table.balls, "the blue snaps back")
	edits = _logged("freetable.edit")
	check(edits.size() == 1 and edits[0]["legal"] == false, "refusal logged: %s" % [edits])
	check((main.get_node("Hud/Notice") as Label).visible, "and said briefly")

	var pink_at := _screen_of_ball(main, "pink")
	_touch(pink_at, true)
	_touch(pink_at, false)
	var remove := palette.button("remove")
	check_eq(remove.text, "Remove pink", "a tap selects the pink")
	check(not remove.disabled, "which can be removed")
	check(not palette.button("pink").visible, "no adding a second pink")
	check(not palette.button("red").visible, "nor a sixteenth red")
	remove.pressed.emit()
	check_eq(_find(table, "pink"), -1, "the pink is gone")
	check(main.get_node_or_null("Balls/pink") == null, "and its node")
	check(palette.button("pink").visible, "and can be added back")
	palette.button("pink").pressed.emit()
	var pink := _find(table, "pink")
	var spot := OfsTable.spot("pink")
	check(pink >= 0, "the pink is back")
	if pink >= 0:
		# FreeTable takes edit positions as Vector2, single precision.
		check_near(table.balls[pink]["x"], spot["x"], 1e-6, "on its spot (x)")
		check_near(table.balls[pink]["y"], spot["y"], 1e-6, "on its spot (y)")
	check(main.get_node_or_null("Balls/pink") != null, "with a node")
	check_eq(table.history_size(), history, "one edit session, one history entry")

	var cue_at := _screen_of_ball(main, "cue")
	_touch(cue_at, true)
	_touch(cue_at, false)
	check(remove.disabled, "the cue ball can't be removed")

	bar.button("edit").button_pressed = false
	await tree.process_frame
	check(not table.edit_mode, "Edit again turns it off")
	check(aim.enabled, "aiming again")
	check(not palette.visible, "the palette goes")
	check((main.get_node("Hud/StrokeCue") as Control).visible, "the shot controls come back")
	_finish(main)


func test_parameters_from_the_pc_apply_at_the_next_shot() -> void:
	var main: MainScript = await _spawn()
	var old_text := Params.current_text()
	var old_hash := Params.current_hash()
	var new_text := old_text.replace('"roll_friction": 0.01,', '"roll_friction": 0.0105,')
	check(new_text != old_text, "the test changes a value")
	var files := '{"table_params":%s}' % new_text
	var new_hash := OfsLog.content_hash(files)
	Params.apply_remote(files, new_hash)
	check_eq(Params.pending_hash(), new_hash, "pending")
	check_eq(Params.current_hash(), old_hash, "not applied while aiming")
	var shot_id: String = main.harness_play_shot({"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0,
			"elevation_rad": 0.0, "speed_m_s": 3.0})
	check(not shot_id.is_empty(), "played")
	var applied := _logged("params.applied")
	check(applied.size() == 1 and applied[0]["shot_id"] == shot_id and applied[0]["new_hash"] == new_hash,
			"params.applied names the shot: %s" % [applied])
	var configured := _logged("physics.configured")
	check(configured.size() == 1 and configured[0]["ok"] == true and configured[0]["params_hash"] == new_hash,
			"physics took the new parameters: %s" % [configured])
	var simulated := _logged("shot.simulated")
	check(simulated.size() == 1 and simulated[0]["bundle"]["params_hash"] == new_hash, "the shot ran on them")
	check_near(Physics.param("roll_friction", 0.0), 0.0105, 1e-12, "the new value in use")
	main.skip_shot()
	await _until_idle(main)
	# Back to the embedded parameters for the tests after this one.
	var old_files := '{"table_params":%s}' % old_text
	Params.apply_remote(old_files, old_hash)
	Params.commit_pending("test-restore")
	check_eq(Params.current_hash(), old_hash, "restored")
	check_near(Physics.param("roll_friction", 0.0), 0.01, 1e-12, "restored value")
	_finish(main)


func test_pull_and_release_plays_a_shot_and_space_repeats_it() -> void:
	# spec 16.3: a scripted pull of PULL_UNITS and release strikes at its speed within 1%.
	var main: MainScript = await _spawn()
	Settings.set_value("strike_mode", "pull")
	await tree.process_frame
	var pull: PullControl = main.get_node("Hud/PullCue")
	var setup: ShotSetup = main.setup
	check(pull.visible and not (main.get_node("Hud/StrokeCue") as Control).visible, "the pull cue replaces the push cue")
	check_eq(setup.strike_mode, ShotSetup.PULL, "the setup follows the setting")
	check_eq(pull.get_global_rect(), (main.get_node("Hud/StrokeCue") as Control).get_global_rect(), "in the same place")
	var cue: CueView = main.get_node("Cue")
	var rest := cue.tip_position()
	var at := _pull(main, PULL_UNITS)
	await tree.process_frame
	check(setup.pulling, "pulling")
	check(cue.tip_position().distance_to(rest) > 0.05, "the cue on the table drew back")
	_touch(at, false)
	check(main.is_playing(), "the release struck")
	var power := PULL_UNITS / PullControl.FULL_PULL
	var want := PullControl.speed_for_power(power, Physics.param("max_cue_speed_m_s", 10.0))
	var inputs := _logged("shot.input")
	check(inputs.size() == 1 and inputs[0]["strike_mode"] == "pull", "pull shot logged: %s" % [inputs])
	if inputs.size() == 1:
		_check_schema("shot.input", inputs[0])
		check_near(inputs[0]["speed_m_s"], want, want * 0.01, "the pull's speed within 1%")
		check_near(inputs[0]["power"], power, 1e-9, "power logged")
		check(not (inputs[0]["stroke_samples"] as Array).is_empty(), "the pull's samples logged as stroke_samples")
	main.skip_shot()
	await _until_idle(main)
	_mark_log()
	_key(KEY_SPACE)
	check(main.is_playing(), "Space shoots in pull mode")
	inputs = _logged("shot.input")
	check(inputs.size() == 1 and absf(float(inputs[0]["speed_m_s"]) - want) < 1e-9, "at the last power: %s" % [inputs])
	main.skip_shot()
	await _until_idle(main)
	_finish(main)


func test_space_does_nothing_in_stroke_mode() -> void:
	var main: MainScript = await _spawn()
	_key(KEY_SPACE)
	check(not main.is_playing(), "no shot")
	check_eq(_logged("shot.input").size(), 0, "nothing logged")
	_finish(main)


func test_a_refused_shot_returns_to_aiming() -> void:
	var main: MainScript = await _spawn()
	var table: FreeTable = main.get_node("FreeTable")
	var aim: AimControl = main.get_node("AimControl")
	var history := table.history_size()
	main.setup.add_fine_tune_sample(1.0, 0.0002)
	main.setup.add_stroke_sample(1.0, Vector2(10.0, 20.0))
	table.balls[1]["ball"] = "purple"
	var shot_id: String = main.harness_play_shot({"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0,
			"elevation_rad": 0.0, "speed_m_s": 2.0})
	check_eq(shot_id, "", "no shot")
	var rejected := _logged("shot.rejected")
	check_eq(rejected.size(), 1, "the binding said why")
	check(not main.is_playing(), "not playing")
	check(not main.harness_busy(), "not busy")
	check(aim.enabled, "still aiming")
	var notice: Label = main.get_node("Hud/Notice")
	check(notice.visible and not notice.text.is_empty(), "the player is told: %s" % notice.text)
	check_eq(table.history_size(), history, "nothing added to the history")
	check(main.setup.fine_tune_samples.is_empty() and main.setup.stroke_samples.is_empty(), "the gesture samples start over")
	check(table._pending_shot.is_empty(), "no shot left waiting for its result")
	check(not (main.get_node("Hud/FreeTableBar") as FreeTableBar).button("rack").disabled, "the bar still works")
	table.balls[1]["ball"] = "red"
	_finish(main)


func test_history_is_blocked_while_a_shot_plays() -> void:
	var main: MainScript = await _spawn()
	var table: FreeTable = main.get_node("FreeTable")
	var bar: FreeTableBar = main.get_node("Hud/FreeTableBar")
	main.harness_play_shot({"aim_rad": 0.1, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 4.0})
	check(main.is_playing(), "playing")
	for id: String in ["retake", "undo", "redo", "edit", "rack", "empty", "overview"]:
		check(bar.button(id).disabled, "%s off while it plays" % id)
	main.undo()
	_key(KEY_R)
	bar.setup_chosen.emit("empty")
	check_eq(table.history_size(), 1, "nothing changed the history")
	check(main.harness_play_shot({"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0,
			"speed_m_s": 1.0}).is_empty(), "no second shot on top")
	main.skip_shot()
	await _until_idle(main)
	check(not bar.button("retake").disabled and not bar.button("rack").disabled, "back on after it")
	_finish(main)


func test_harness_hooks() -> void:
	var main: MainScript = await _spawn()
	var saved: Callable = Harness.scene_provider
	Harness.scene_provider = func() -> Node: return main
	var r: Dictionary = await Harness.run_command("load_position", {"name": "cut_half_ball"})
	check(r["ok"], "load_position: %s" % [r])
	var table: FreeTable = main.get_node("FreeTable")
	check_eq(table.balls.size(), 2, "the position's balls")
	check_eq(main.get_node("Balls").get_child_count(), 2, "drawn")
	check_eq(table.history_size(), 1, "a fresh history")
	r = await Harness.run_command("play_shot", {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": -0.2,
			"elevation_rad": 0.05, "speed_m_s": 1.5})
	check(r["ok"], "play_shot: %s" % [r])
	var shot_id: String = r.get("result", {}).get("shot_id", "")
	check(main.harness_busy(), "busy while it plays")
	check(not main.harness_load_position(table.balls, table.cue_ball), "no loading over a shot")
	var input := _logged("shot.input")
	check(input.size() == 1 and input[0]["strike_mode"] == "harness" and input[0]["shot_id"] == shot_id,
			"logged as a harness shot: %s" % [input])
	main.skip_shot()
	r = await Harness.run_command("wait_idle", {"timeout_s": 10})
	check(r["ok"], "wait_idle: %s" % [r])
	r = await Harness.run_command("dump_state", {})
	var state: Dictionary = r.get("result", {})
	check_eq(state.get("screen"), "table", "screen")
	check_eq(state.get("balls"), table.balls, "balls at full precision")
	check_eq(state.get("cue_ball"), table.cue_ball, "cue_ball")
	var setup: Dictionary = state.get("setup", {})
	for key: String in ["aim_rad", "tip_x", "tip_y", "elevation_rad", "speed_m_s", "power", "strike_mode", "elevation_min_rad"]:
		check(setup.has(key), "setup has %s" % key)
	check_eq(state.get("settings"), Settings.all(), "settings")
	check_eq(state.get("playing"), false, "not playing")
	check_eq(state.get("edit_mode"), false, "edit_mode")
	check_eq(state.get("history"), {"index": 1, "size": 2}, "history")
	var last: Dictionary = state.get("last_shot", {})
	check_eq(last.get("shot_id"), shot_id, "last shot's id")
	check(float(last.get("sim_us", 0.0)) > 0.0 and int(last.get("events", 0)) > 0, "its sim time and events")
	check(last.get("potted") is Array, "its potted list")
	var camera: Dictionary = state.get("camera", {})
	check(camera.has("pitch_deg") and camera.has("zoom_scale"), "camera")
	var two_cues: Array = [{"ball": "cue", "index": 0, "x": 0.0, "y": 0.0}, {"ball": "cue", "index": 0, "x": 0.5, "y": 0.0}]
	check(not main.harness_load_position(two_cues, 0), "a malformed position is refused")
	Harness.scene_provider = saved
	_finish(main)


func test_edit_mode_refuses_harness_shots() -> void:
	var main: MainScript = await _spawn()
	(main.get_node("FreeTable") as FreeTable).set_edit_mode(true)
	check(main.harness_play_shot({"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0,
			"speed_m_s": 1.0}).is_empty(), "no shots while editing")
	check_eq(_logged("shot.input").size(), 0, "nothing logged")
	_finish(main)


func test_three_finger_tap_toggles_the_overlay() -> void:
	var main: MainScript = await _spawn()
	var overlay: Control = main.get_node("Hud/Overlay")
	var shown := overlay.visible
	for i in 3:
		_touch(TABLE_POINT + Vector2(60.0 * i, 0.0), true, i)
	for i in 3:
		_touch(TABLE_POINT + Vector2(60.0 * i, 0.0), false, i)
	check_eq(overlay.visible, not shown, "toggled")
	var toggles := _logged("overlay.toggled")
	check(toggles.size() == 1 and toggles[0]["via"] == "three_finger_tap", "logged: %s" % [toggles])
	_finish(main)


func test_starts_in_a_saved_pull_mode() -> void:
	_reset_settings()
	Settings.set_value("strike_mode", "pull")
	var main := load("res://scenes/main.tscn").instantiate() as MainScript
	tree.root.add_child(main)
	await tree.process_frame
	await tree.process_frame
	_mark_log()
	var pull: PullControl = main.get_node("Hud/PullCue")
	check(pull.visible and not (main.get_node("Hud/StrokeCue") as Control).visible, "pull cue shown, push cue hidden")
	check(main.setup.power > 0.0, "a power to shoot at")
	check_near(main.setup.speed_m_s, ShotSetup.DEFAULT_PACE_M_S, 1e-9, "the default pace")
	_key(KEY_SPACE)
	check(main.is_playing(), "Space shoots")
	main.skip_shot()
	await _until_idle(main)
	_finish(main)


func test_retake_in_pull_mode_is_exact() -> void:
	var main: MainScript = await _spawn()
	Settings.set_value("strike_mode", "pull")
	await tree.process_frame
	var pull: PullControl = main.get_node("Hud/PullCue")
	var setup: ShotSetup = main.setup
	setup.set_aim(0.0123456789)
	setup.set_tip(0.1, 0.2)
	var at := _pull(main, PullControl.pull_for_power(0.4))
	_touch(at, false)
	var struck := setup.snapshot()
	check(main.is_playing(), "pulled and struck")
	main.skip_shot()
	await _until_idle(main)
	_touch(_pull(main, 100.0), false)
	main.skip_shot()
	await _until_idle(main)
	_key(KEY_R)
	_key(KEY_R)
	check_eq(setup.snapshot(), struck, "the setup as struck")
	check_near(pull.power(), 0.4, 1e-9, "the power back")
	check_near(pull.speed(), setup.speed_m_s, 1e-12, "its speed the setup's")
	_finish(main)


func test_turning_edit_off_mid_drag_puts_the_ball_back() -> void:
	var main: MainScript = await _spawn()
	var table: FreeTable = main.get_node("FreeTable")
	var aim: AimControl = main.get_node("AimControl")
	table.set_edit_mode(true)
	await tree.process_frame
	var before: Array[Dictionary] = table.balls.duplicate(true)
	var at := _screen_of_ball(main, "blue")
	_touch(at, true)
	for i in 6:
		_drag_to(at + Vector2(20.0 * i, 0.0), at + Vector2(20.0 * (i + 1), 0.0))
	check(not _ball_node_at_entry(main, table, "blue"), "the blue follows the finger")
	_mark_log()
	table.set_edit_mode(false)
	check_eq(table.balls, before, "the table is as it was")
	_check_nodes_match(main, table.balls, "edit off mid-drag")
	_drag_to(at + Vector2(120.0, 0.0), at + Vector2(160.0, 0.0))
	_touch(at + Vector2(160.0, 0.0), false)
	check_eq(table.balls, before, "the finger lifting later moves nothing")
	check_eq(_logged("freetable.edit").size(), 0, "and edits nothing")
	var start_aim: float = main.setup.aim_rad
	_table_drag(TABLE_POINT, Vector2(-AIM_DRAG, 0.0), 10)
	check_near(main.setup.aim_rad, start_aim + AIM_DRAG * aim.rad_per_px(), 1e-9, "a fresh drag aims")
	_finish(main)


func test_undo_mid_drag_puts_the_ball_back() -> void:
	var main: MainScript = await _spawn()
	var table: FreeTable = main.get_node("FreeTable")
	var camera: Camera3D = main.get_node("CameraRig/Camera3D")
	table.set_edit_mode(true)
	await tree.process_frame
	check(table.move_ball(_find(table, "pink"), Vector2(0.4, -0.3)), "a first edit to undo")
	await tree.process_frame
	var at := _screen_of_ball(main, "blue")
	_touch(at, true)
	for i in 6:
		_drag_to(at + Vector2(0.0, 15.0 * i), at + Vector2(0.0, 15.0 * (i + 1)))
	main.undo()
	var undone: Array[Dictionary] = table.balls.duplicate(true)
	_check_nodes_match(main, undone, "undo mid-drag")
	_mark_log()
	_touch(at + Vector2(0.0, 90.0), false)
	check_eq(table.balls, undone, "the finger lifting later moves nothing")
	check_eq(_logged("freetable.edit").size(), 0, "and edits nothing")
	var blue_at := _screen_of_ball(main, "blue")
	_table_drag(blue_at, _screen_at(camera, Vector2(0.3, 0.2)) - blue_at, 12)
	await tree.process_frame
	check_near(table.balls[_find(table, "blue")]["x"], 0.3, 2e-3, "a fresh drag moves the blue again")
	_finish(main)


func test_the_wheel_held_through_a_shot_turns_again_after_it() -> void:
	var main: MainScript = await _spawn()
	var setup: ShotSetup = main.setup
	var wheel: FineTuneWheel = main.get_node("Hud/FineTuneWheel")
	var at := wheel.get_global_rect().get_center()
	_touch(at, true, 1)
	check(wheel.is_pressed(), "a thumb on the wheel")
	var push := StrokeControl.FASTEST_PUSH_PX_S * pow(2.0 / Physics.param("max_cue_speed_m_s", 10.0), 1.0 / StrokeControl.PUSH_EXPONENT)
	_stroke(main, push)
	check(main.is_playing(), "struck")
	check(not wheel.is_pressed(), "the strike ended the wheel's press")
	_touch(at, false, 1)
	main.skip_shot()
	await _until_idle(main)
	check(not wheel.is_pressed(), "the wheel is free")
	var before := setup.aim_rad
	_vertical_drag(at, -FineTuneWheel.PX_PER_STEP, 5)
	check_near(setup.aim_rad, before + deg_to_rad(5 * FineTuneWheel.STEP_DEG), 1e-9, "a fresh turn works")
	_finish(main)


func test_a_harness_shot_sets_the_pull_power_too() -> void:
	var main: MainScript = await _spawn()
	Settings.set_value("strike_mode", "pull")
	await tree.process_frame
	var pull: PullControl = main.get_node("Hud/PullCue")
	var max_speed := Physics.param("max_cue_speed_m_s", 10.0)
	main.harness_play_shot({"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 7.0})
	check_near(main.setup.power, PullControl.power_for_speed(7.0, max_speed), 1e-12, "power for 7 m/s")
	main.skip_shot()
	await _until_idle(main)
	check_near(pull.speed(), 7.0, 1e-9, "the last power is the harness's")
	_mark_log()
	_key(KEY_SPACE)
	var inputs := _logged("shot.input")
	check(inputs.size() == 1 and absf(float(inputs[0]["speed_m_s"]) - 7.0) < 1e-9,
			"Space plays it: %s" % [inputs])
	main.skip_shot()
	await _until_idle(main)
	_finish(main)


func test_a_harness_shot_out_of_range_is_warned_about() -> void:
	var main: MainScript = await _spawn()
	var max_speed := Physics.param("max_cue_speed_m_s", 10.0)
	main.harness_play_shot({"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.1, "speed_m_s": 2.0})
	main.skip_shot()
	await _until_idle(main)
	check_eq(_warnings(), [] as Array[String], "nothing to warn about in range")
	_mark_log()
	main.harness_play_shot({"aim_rad": 0.0, "tip_x": 0.9, "tip_y": 0.6, "elevation_rad": -0.1,
			"speed_m_s": max_speed + 1.0})
	main.skip_shot()
	await _until_idle(main)
	var warnings := _warnings()
	check_eq(warnings.size(), 3, "tip, speed and elevation: %s" % [warnings])
	_mark_log()
	main.harness_play_shot({"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 2.0, "speed_m_s": 1.0})
	main.skip_shot()
	await _until_idle(main)
	warnings = _warnings()
	check(warnings.size() == 1 and warnings[0].contains("elevation"), "elevation above the max: %s" % [warnings])
	_finish(main)


func test_shot_input_keeps_its_identity_when_truncated() -> void:
	var main: MainScript = await _spawn()
	for i in 6000:
		main.setup.add_stroke_sample(float(i), Vector2(i, -i))
	var shot_id: String = main.harness_play_shot({"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0,
			"elevation_rad": 0.0, "speed_m_s": 1.0})
	var inputs := _logged("shot.input")
	check_eq(inputs.size(), 1, "logged")
	if inputs.size() == 1:
		var samples: Array = inputs[0].get("stroke_samples", [])
		check(not samples.is_empty() and samples[-1] == "<truncated>", "the samples were cut short")
		check_eq(inputs[0].get("shot_id"), shot_id, "shot_id kept")
		check_eq(inputs[0].get("mode"), "free_table", "mode kept")
		check_eq(inputs[0].get("strike_mode"), "harness", "strike_mode kept")
		check_eq(int(inputs[0].get("guide_level", -1)), 1, "guide_level kept")
	main.skip_shot()
	await _until_idle(main)
	_finish(main)


func test_redo_to_the_latest_shot_resets_spin_as_the_shot_did() -> void:
	var main: MainScript = await _spawn()
	var setup: ShotSetup = main.setup
	main.harness_play_shot({"aim_rad": 0.05, "tip_x": 0.2, "tip_y": -0.3, "elevation_rad": 0.1, "speed_m_s": 1.5})
	main.skip_shot()
	await _until_idle(main)
	check_eq([setup.tip_x, setup.tip_y, setup.elevation_rad], [0.0, 0.0, 0.0], "reset after the shot")
	main.undo()
	check_eq([setup.tip_x, setup.tip_y, setup.elevation_rad], [0.2, -0.3, 0.1], "undo: the shot's spin")
	main.redo()
	check_eq([setup.tip_x, setup.tip_y, setup.elevation_rad], [0.0, 0.0, 0.0], "redo: reset as after the shot")
	Settings.set_value("reset_spin_after_shot", false)
	main.undo()
	main.redo()
	check_eq([setup.tip_x, setup.tip_y, setup.elevation_rad], [0.2, -0.3, 0.1], "reset off: kept as the shot left it")
	_finish(main)


func test_guides_key_on_the_table_version() -> void:
	var main: MainScript = await _spawn()
	var table: FreeTable = main.get_node("FreeTable")
	var version := table.version()
	(main.get_node("Hud/FreeTableBar") as FreeTableBar).setup_chosen.emit("empty")
	check(table.version() > version, "a table change bumps the version")
	version = table.version()
	main.setup.set_aim(0.01)
	await tree.process_frame
	check_eq(table.version(), version, "aiming doesn't")
	_finish(main)


func test_the_broadcast_view_keeps_every_ball_of_a_long_pot_on_screen() -> void:
	# KaiC: "after the potting the ball just show this angle to see where the ball goes", the TV
	# camera at the end behind the shot, steady, eased into from behind the cue ball and back.
	var main: MainScript = await _spawn()
	var rig: CameraRig = main.get_node("CameraRig")
	var camera := rig.camera()
	var player: ShotPlayer = main.get_node("ShotPlayer")
	var dims := OfsTable.dimensions()
	var r := float(dims["ball_radius"])
	var top_left: Dictionary = (Physics.table_shape()["pockets"] as Array)[0]
	var pocket := Vector2(float(top_left["drop_centre"][0]), float(top_left["drop_centre"][1]))
	var red := Vector2(1.0, 0.45)
	var cue := Vector2(-1.0, 0.1)
	check(main.harness_load_position([{"ball": "cue", "index": 0, "x": cue.x, "y": cue.y},
			{"ball": "red", "index": 0, "x": red.x, "y": red.y}], 0), "the position")
	await tree.process_frame
	var ghost := red - (pocket - red).normalized() * 2.0 * r
	main.harness_play_shot({"aim_rad": (ghost - cue).angle(), "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0,
			"speed_m_s": 4.0})
	check(main.is_playing() and rig.view_name() == "shot", "the broadcast view for the shot")
	var broadcast := rig.broadcast_pose(0.0)
	var view := main.get_viewport().get_visible_rect()
	var dt := 1.0 / 60.0
	var off_screen: Array[String] = []
	var largest_step := 0.0
	var moved_after := 0.0
	var last_eye := camera.global_position
	var frames := 0
	var settled := int(ceil(CameraRig.BLEND_S * 60.0)) + 2
	while player.is_playing() and frames < 1200:
		player.advance(dt)
		rig._process(dt)
		frames += 1
		var step := camera.global_position.distance_to(last_eye)
		largest_step = maxf(largest_step, step)
		if frames > settled:
			moved_after = maxf(moved_after, step)
		last_eye = camera.global_position
		if main._record == null:
			break
		var state: PackedFloat64Array = main._record.state_at(player.time())
		for o in range(0, state.size(), ShotPlayback.STRIDE):
			var motion := int(state[o + ShotPlayback.MOTION])
			if motion != ShotPlayback.SLIDING and motion != ShotPlayback.ROLLING:
				continue
			var at := TableSpace.core_to_godot(state[o + ShotPlayback.X], state[o + ShotPlayback.Y], r)
			if camera.is_position_behind(at) or not view.has_point(camera.unproject_position(at)):
				off_screen.append("ball %d at t %.2f" % [o / ShotPlayback.STRIDE, player.time()])
		if frames == settled:
			check(rig.transform.is_equal_approx(broadcast), "at the broadcast view from baulk once blended")
	check(frames > settled + 30, "the shot ran: %d frames" % frames)
	check_eq(off_screen, [] as Array[String], "every moving ball stays on screen in every frame")
	check(largest_step < 0.2, "eased, no jump: %.3f m in one frame" % largest_step)
	check_eq(moved_after, 0.0, "steady once there, not chasing the balls")
	var potted := false
	for ball: Dictionary in (main._last_shot["potted"] as Array):
		potted = potted or ball["ball"] == "red"
	check(potted, "the red went in: %s" % [main._last_shot])
	await _until_idle(main)
	check(rig.view_name() == "aim" and rig.is_easing(), "on its way back behind the cue ball")
	rig._process(CameraRig.BLEND_S)
	check(not Perf._active.has(&"camera"), "the camera lets the game idle again")
	var behind := rig.pose_for(main._cue_xy(), main.setup.aim_rad, main.setup.effective_elevation())
	check(rig.transform.is_equal_approx(behind), "behind the cue ball for the next shot")
	_finish(main)


func test_the_overview_button_shows_the_broadcast_view_to_walk_round() -> void:
	var main: MainScript = await _spawn()
	var rig: CameraRig = main.get_node("CameraRig")
	var bar: FreeTableBar = main.get_node("Hud/FreeTableBar")
	var notice: Label = main.get_node("Hud/Notice")
	var aim: AimControl = main.get_node("AimControl")
	var pitch := rig.pitch_deg()
	var start_aim := main.setup.aim_rad
	bar.button("overview").button_pressed = true
	check(rig.is_overview(), "the overview")
	check(not aim.enabled, "drags walk round instead of aiming")
	check(notice.visible and notice.text == MainScript.OVERVIEW_HINT, "a one-line hint about walking round")
	rig._process(CameraRig.BLEND_S)
	check(rig.transform.is_equal_approx(rig.broadcast_pose(CameraRig.end_behind(start_aim))), "blended there")
	# The hint covered the far rail at the old, higher view.
	var hint := notice.get_global_rect()
	for pocket: Dictionary in Physics.table_shape()["pockets"]:
		var c: Array = pocket["drop_centre"]
		var at := rig.camera().unproject_position(TableSpace.core_to_godot(float(c[0]), float(c[1])))
		check(at.y > hint.end.y, "the %s pocket at %s below the hint %s" % [pocket["name"], at, hint])
	var forward := -rig.transform.basis.z
	_table_drag(TABLE_POINT, Vector2(-400.0, 0.0), 20)
	check_eq(main.setup.aim_rad, start_aim, "the aim unchanged")
	check_near(_turned(forward, -rig.transform.basis.z), 400.0 * CameraRig.ORBIT_RAD_PER_PX, 1e-4, "walked round")
	bar.button("overview").button_pressed = false
	check(not rig.is_overview() and aim.enabled, "back to aiming")
	rig._process(CameraRig.BLEND_S)
	check_eq(rig.pitch_deg(), pitch, "at the tilt before")
	check(rig.transform.is_equal_approx(rig.pose_for(main._cue_xy(), start_aim, main.setup.effective_elevation())),
			"behind the cue ball")
	notice.visible = false
	bar.button("overview").button_pressed = true
	check(not notice.visible, "the hint only once")
	rig.tilt(-50.0)
	check(not bar.button("overview").button_pressed, "a tilt leaves the overview and the button shows it")
	_finish(main)


func test_a_drag_during_a_shot_walks_round_the_broadcast_view() -> void:
	var main: MainScript = await _spawn()
	var rig: CameraRig = main.get_node("CameraRig")
	var player: ShotPlayer = main.get_node("ShotPlayer")
	check(main.harness_load_position([{"ball": "cue", "index": 0, "x": -1.0, "y": 0.0},
			{"ball": "red", "index": 0, "x": 1.2, "y": 0.6}], 0), "the position")
	await tree.process_frame
	main.harness_play_shot({"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.0})
	rig._process(CameraRig.BLEND_S)
	player.advance(0.1)
	var forward := -rig.transform.basis.z
	_table_drag(TABLE_POINT, Vector2(200.0, 0.0), 10)
	check(main.is_playing(), "a drag doesn't skip the shot")
	check_near(_turned(forward, -rig.transform.basis.z), 200.0 * CameraRig.ORBIT_RAD_PER_PX, 1e-4, "walked round")
	check_eq(main.setup.aim_rad, 0.0, "the aim unchanged")
	main.skip_shot()
	await _until_idle(main)
	rig._process(CameraRig.BLEND_S)
	check(rig.transform.is_equal_approx(rig.pose_for(main._cue_xy(), main.setup.aim_rad, main.setup.effective_elevation())),
			"behind the cue ball after it")
	_finish(main)


## How far a view direction turned round the vertical, in radians.
func _turned(from: Vector3, to: Vector3) -> float:
	return absf(Vector2(from.x, from.z).angle_to(Vector2(to.x, to.z)))


## The warnings harness_play_shot logged since _mark_log.
func _warnings() -> Array[String]:
	var found: Array[String] = []
	for data in _logged("harness.command"):
		if data.get("command") == "play_shot" and data.get("result") is Dictionary:
			for w: Variant in (data["result"] as Dictionary).get("warnings", []):
				found.append(String(w))
	return found


func _ball_node_at_entry(main: Node, table: FreeTable, ball_name: String) -> bool:
	var xy: Array = main.get_node("Balls/" + ball_name).get_meta("core_xy")
	var entry: Dictionary = table.balls[_find(table, ball_name)]
	return xy[0] == entry["x"] and xy[1] == entry["y"]


# Input, pushed straight into the viewport in canvas units, as the phone's touches arrive.

func _push(event: InputEvent) -> void:
	tree.root.push_input(event, true)


func _touch(at: Vector2, pressed: bool, index: int = 0) -> void:
	var e := InputEventScreenTouch.new()
	e.index = index
	e.pressed = pressed
	e.position = at
	_push(e)


func _drag_to(from: Vector2, to: Vector2, index: int = 0) -> void:
	var e := InputEventScreenDrag.new()
	e.index = index
	e.position = to
	e.relative = to - from
	e.screen_relative = e.relative
	_push(e)


func _key(keycode: Key, ctrl: bool = false) -> void:
	for pressed: bool in [true, false]:
		var e := InputEventKey.new()
		e.keycode = keycode
		e.physical_keycode = keycode
		e.ctrl_pressed = ctrl
		e.pressed = pressed
		_push(e)


## One finger down at `from`, moved by `by` in `steps` equal moves, lifted.
func _table_drag(from: Vector2, by: Vector2, steps: int) -> void:
	_touch(from, true)
	var at := from
	for i in steps:
		var next := from + by * float(i + 1) / float(steps)
		_drag_to(at, next)
		at = next
	_touch(at, false)


func _vertical_drag(from: Vector2, step: float, steps: int) -> void:
	_table_drag(from, Vector2(0.0, step * steps), steps)


## Pull mode: a press near the top of the pull cue, then a pull straight down `units` in steps of
## 10; the finger stays down. Returns where it is.
func _pull(main: MainScript, units: float) -> Vector2:
	var pull: PullControl = main.get_node("Hud/PullCue")
	var rect := pull.get_global_rect()
	var at := Vector2(rect.get_center().x, rect.position.y + 100.0)
	_touch(at, true)
	var moved := 0.0
	while moved < units:
		var step := minf(10.0, units - moved)
		moved += step
		_drag_to(at, at + Vector2(0.0, step))
		at += Vector2(0.0, step)
	return at


## A pull back, then a push at a constant push_px_s canvas units a second, released moving.
func _stroke(main: MainScript, push_px_s: float) -> void:
	var stroke: StrokeControl = main.get_node("Hud/StrokeCue")
	stroke.clock = func() -> float: return _t[0]
	var rect := stroke.get_global_rect()
	var at := Vector2(rect.get_center().x, rect.end.y - 200.0)
	_t[0] = 100.0
	_touch(at, true)
	for i in 6:
		_t[0] += 1.0 / 60.0
		var back := at + Vector2(0.0, 10.0)
		_drag_to(at, back)
		at = back
	for i in 24:
		_t[0] += STROKE_DT
		var forward := at - Vector2(0.0, push_px_s * STROKE_DT)
		_drag_to(at, forward)
		at = forward
	_t[0] += STROKE_DT
	at -= Vector2(0.0, push_px_s * STROKE_DT)
	_touch(at, false)


func _until_idle(main: MainScript) -> void:
	for i in 120:
		if not main.harness_busy():
			return
		await tree.process_frame
	failures.append("still busy after 120 frames")


func _screen_of_ball(main: Node, ball_name: String) -> Vector2:
	var camera: Camera3D = main.get_node("CameraRig/Camera3D")
	return camera.unproject_position((main.get_node("Balls/" + ball_name) as Node3D).global_position)


func _screen_at(camera: Camera3D, xy: Vector2) -> Vector2:
	return camera.unproject_position(TableSpace.core_to_godot(xy.x, xy.y, 0.02625))


func _find(table: FreeTable, ball: String) -> int:
	for i in table.balls.size():
		if table.balls[i]["ball"] == ball:
			return i
	return -1


func _check_nodes_match(main: Node, balls: Array, when: String) -> void:
	var nodes := main.get_node("Balls").get_children()
	check_eq(nodes.size(), balls.size(), "%s: a node per ball" % when)
	for i in mini(nodes.size(), balls.size()):
		var xy: Array = nodes[i].get_meta("core_xy")
		check_eq([xy[0], xy[1]], [balls[i]["x"], balls[i]["y"]], "%s: node %d (%s) where its entry is" % [when, i, nodes[i].name])
		check((nodes[i] as Node3D).visible, "%s: node %d shown" % [when, i])


func _check_schema(event: String, data: Dictionary) -> void:
	var schema: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(SCHEMA_PATH))
	var fields: Dictionary = schema["events"][event]["fields"]
	for field: String in fields:
		if fields[field].get("required", false):
			check(data.has(field), "%s has its required %s" % [event, field])
	for field: String in data:
		check(fields.has(field), "%s's %s is in the schema" % [event, field])


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


func test_aim_turns_finer_while_the_line_meets_a_ball() -> void:
	var main: MainScript = await _spawn()
	var aim: AimControl = main.get_node("AimControl")
	check(main.harness_load_position([{"ball": "cue", "index": 0, "x": -1.0, "y": 0.0},
			{"ball": "red", "index": 0, "x": 0.5, "y": 0.0}], 0), "the position")
	await tree.process_frame
	var full := AimControl.BASE_RAD_PER_PX * float(Settings.get_value("aim_sensitivity"))
	main.setup.set_aim(0.0)
	var on_ball := aim.rad_per_px()
	main.setup.set_aim(PI / 2.0)
	var off_ball := aim.rad_per_px()
	check(on_ball < off_ball, "finer on the red (%f) than away from it (%f)" % [on_ball, off_ball])
	check_near(on_ball / off_ball, AimControl.ON_BALL_SCALE, 1e-9, "by ON_BALL_SCALE")
	check(off_ball > 0.0 and off_ball <= full, "full speed (or finer when zoomed) away from it")
	_finish(main)
