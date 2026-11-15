extends Node3D
## The free table (spec 12.1, 8), where the app opens: the table, balls and cue, one ShotSetup
## every control writes, the shot from strike to playback and its cycle (spec 8.5: the pocket
## camera, the points, the break, the aim turned to the next ball), edit mode, and the harness's
## hooks (spec 16.5, docs/harness.md).

const HudScript := preload("res://scripts/hud.gd")
const MODE := "free_table"
const LAYOUT := "full_rack"
const SCREEN := "table"
const HARNESS_STRIKE := "harness"
const NOTICE_SECONDS := 2.5
# iOS can report the safe area late and changes it on rotation without a resize.
const SAFE_AREA_POLL_SECONDS := 1.0
## The shot controls while a shot plays: dimmed, and their touches go to the table (a tap skips).
const PLAYING_ALPHA := 0.35
const SELECTION_COLOUR := Color(1.0, 0.82, 0.3, 0.9)
## Edit mode: where a new ball goes when its own spot is taken, rings of this many ball widths out.
const FREE_SPOT_RINGS := 40
const OVERVIEW_HINT := "Drag to walk round the table"
const BREAK_TEXT := "Break %d"

var setup := ShotSetup.new()
## Where the aim turns after a shot: NextTarget.choose(balls, cue_ball, prefer); tests put a fake in.
var choose_next: Callable

var _backswing_m := 0.0
var _record: OfsShotRecord
var _shot_id := ""
## From the strike until the balls have stopped and every drop has finished.
var _playing := false
var _skip_requested := false
var _settling := false
var _last_shot := {}
var _refresh_queued := false
var _overview_hint_shown := false
# The sensor housing's side the controls were last laid out for; "unset" logs the first layout.
var _housing := "unset"
## Edit mode: list positions of the ball under the finger and of the selected ball, or -1.
var _dragged := -1
var _selected := -1
var _selection := MeshInstance3D.new()
var _notice_timer := Timer.new()
var _pockets: Array = []
## The shot cycle (spec 8.5): the camera from the strike to the swoop back, and the break. _cycle_t
## is its clock: the shot's playback time, then real time once the balls have stopped.
var _cycle: ShotCycle
var _cycle_t := 0.0
var _plan := {}
var _popups := PotPopups.new()
var _break_pill := Label.new()
var _break_band := Rect2()
var _break_centre_x := 0.0
var _pot_warned := false

@onready var _stage: Stage = $Stage
@onready var _table: TableView = $Table
@onready var _balls: BallsView = $Balls
@onready var _cue: CueView = $Cue
@onready var _guides: Guides = $Guides
@onready var _rig: CameraRig = $CameraRig
@onready var _gestures: Gestures = $Gestures
@onready var _aim: AimControl = $AimControl
@onready var _player: ShotPlayer = $ShotPlayer
@onready var _audio: TableAudio = $TableAudio
@onready var _haptics: Haptics = $Haptics
@onready var _free: FreeTable = $FreeTable
@onready var _elevation: ElevationControl = $Hud/ElevationControl
@onready var _spin: SpinWidget = $Hud/SpinWidget
@onready var _wheel: FineTuneWheel = $Hud/FineTuneWheel
@onready var _stroke: StrokeControl = $Hud/StrokeCue
@onready var _pull: PullControl = $Hud/PullCue
@onready var _palette: EditPalette = $Hud/EditPalette
@onready var _notice: Label = $Hud/Notice
@onready var _bar: FreeTableBar = $Hud/FreeTableBar
@onready var _settings_panel: SettingsPanel = $Hud/SettingsPanel
@onready var _overlay: DebugOverlay = $Hud/Overlay


func _ready() -> void:
	set_process(false)
	_stage.setup()
	var shape := Physics.table_shape()
	if shape.is_empty():
		# Physics has logged why it isn't configured; the table is still drawn from the same file.
		shape = TableView.shape_from_json(FileAccess.get_file_as_string(Physics.TABLE_SHAPE_PATH))
	_table.build(shape, OfsTable.dimensions())
	_stage.set_table_bounds(_table.bounds())
	_stage.dress_camera(_rig.camera())
	_rig.set_table_bounds(_table.bounds())
	_stage.capture_reflections()
	_pockets = shape.get("pockets", [])
	_balls.set_pockets(_pockets)
	_rig.moved.connect(_on_camera_moved)
	_cycle = ShotCycle.new(_rig)
	choose_next = func(balls: Array[Dictionary], cue_ball: int, prefer: String) -> Dictionary: return NextTarget.choose(balls, cue_ball, prefer)
	_popups.name = "PotPopups"
	add_child(_popups)
	# Kept out of the table's reflection probe, which would show them where they were captured.
	_set_layers(_guides, Stage.MOVING_LAYER)

	setup.strike_mode = String(Settings.get_value("strike_mode"))
	_aim.bind(setup, _gestures, _rig)
	_aim.on_ball = func() -> bool: return _guides.aims_at_ball(_free.balls, _free.cue_ball, setup.aim_rad)
	_guides.allowance = _allowing_for_side
	_wheel.bind(setup)
	_spin.bind(setup)
	_elevation.bind(setup)
	_stroke.bind(setup)
	_pull.bind(setup)
	setup.changed.connect(_on_setup_changed)
	_stroke.backswing_changed.connect(_on_backswing_changed)
	_pull.backswing_changed.connect(_on_backswing_changed)
	_stroke.struck.connect(func(_speed: float) -> void: _strike(ShotSetup.STROKE))
	_pull.struck.connect(func(_speed: float) -> void: _strike(ShotSetup.PULL))
	_wheel.stepped.connect(func(_steps: int) -> void: _haptics.tick())

	_gestures.pinched.connect(func(factor: float, _centre: Vector2) -> void: _rig.pinch(factor))
	_gestures.two_finger_dragged.connect(func(relative: Vector2) -> void: _rig.tilt(relative.y))
	_gestures.three_finger_tapped.connect(func() -> void: _overlay.toggle("three_finger_tap"))
	_gestures.tapped.connect(_on_tapped)
	_gestures.drag_started.connect(_on_drag_started)
	_gestures.drag_moved.connect(_on_drag_moved)
	_gestures.drag_ended.connect(_on_drag_ended)

	_player.frame.connect(_on_frame)
	_player.event_reached.connect(_on_event)
	_player.finished.connect(_on_shot_finished)

	_free.state_changed.connect(_on_table_changed)
	_free.history_changed.connect(_on_history_changed)
	_free.edit_mode_changed.connect(_on_edit_mode_changed)
	_bar.retake_pressed.connect(retake)
	_bar.undo_pressed.connect(undo)
	_bar.redo_pressed.connect(redo)
	_bar.edit_toggled.connect(_free.set_edit_mode)
	_bar.setup_chosen.connect(_choose_setup)
	_bar.settings_pressed.connect(_settings_panel.open)
	_bar.overview_toggled.connect(_on_overview_toggled)
	_rig.overview_changed.connect(_on_overview_changed)
	_palette.add_pressed.connect(_add_ball)
	_palette.remove_pressed.connect(_remove_selected)
	Settings.changed.connect(_on_setting_changed)

	_build_selection()
	_build_notice()
	_build_break_pill()
	get_viewport().size_changed.connect(_layout)
	var poll := Timer.new()
	poll.wait_time = SAFE_AREA_POLL_SECONDS
	poll.timeout.connect(_layout)
	add_child(poll)
	poll.start()
	_show_controls()
	_layout()

	_free.load_setup(LAYOUT)
	_refresh()
	Log.info("table", "table.loaded", {
		"balls": _free.balls.size(),
		"layout": LAYOUT,
		"params_hash": Params.current_hash(),
	})


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_RESUMED or what == NOTIFICATION_APPLICATION_FOCUS_IN:
		_layout()


func _process(delta: float) -> void:
	if _playing and not _player.is_playing() and not _skip_requested:
		_cycle_t += delta
		_cycle.update(_cycle_t)
	if _settling:
		_settle()


# Keyboard test aids (spec 8.8): R retake, Ctrl+Z undo, Ctrl+Y redo, Space shoots at the last power
# in pull mode. F3 is the overlay's own.
func _unhandled_input(event: InputEvent) -> void:
	if not (event is InputEventKey or event is InputEventAction) or _settings_panel.visible:
		return
	if event.is_action_pressed("retake", false, true):
		retake()
	elif event.is_action_pressed("undo", false, true):
		undo()
	elif event.is_action_pressed("redo", false, true):
		redo()
	elif event.is_action_pressed("shoot", false, true):
		if _pull.is_visible_in_tree() and not _playing:
			_pull.shoot_last_power()
	else:
		return
	get_viewport().set_input_as_handled()


## Undoes the last shot: its table and its setup again.
func retake() -> void:
	_history("retake")


func undo() -> void:
	_history("undo")


func redo() -> void:
	_history("redo")


func is_playing() -> bool:
	return _playing


## Ends the playing shot at once, as a tap on the table does: no pocket camera, straight to the
## settle and the swoop back, also while the camera is still at the pocket or holding the
## full-table view after the balls have stopped.
func skip_shot() -> void:
	if not _playing:
		return
	_skip_requested = true
	if _player.is_playing():
		_player.skip_to_end()
	elif _settling:
		_balls.finish_drops()
		_cycle.cancel()
		_settle()


## Logs, simulates and starts playing the shot set up; its id, or "" when none could start.
## strike_mode is what the logs call the way it was struck: stroke (Push), pull or harness.
func _strike(strike_mode: String) -> String:
	if _playing or _free.edit_mode:
		return ""
	var shot_id := Physics.new_shot_id()
	Params.commit_pending(shot_id)
	_update_elevation_min()
	Log.info("shot", "shot.input", _shot_input_fields(shot_id, strike_mode))
	var strike := setup.to_strike()
	# A harness shot plays exactly as given.
	if strike_mode != HARNESS_STRIKE:
		strike = _allowing_for_side(strike, shot_id)
	var record := Physics.simulate(_free.balls, _free.cue_ball, strike,
			{"shot_id": shot_id, "mode": MODE, "strike_mode": strike_mode})
	if record == null:
		# The binding has logged shot.rejected with the reason.
		setup.stroke_samples.clear()
		setup.fine_tune_samples.clear()
		setup.wander_rad = 0.0
		_show_notice("That shot couldn't be played")
		_queue_refresh()
		return ""
	_free.shot_started(setup.snapshot(), shot_id)
	# An aim set this frame (a harness shot) would otherwise leave the camera held where it was, or
	# where an ease to it had got to.
	_flush_refresh()
	_rig.finish_ease()
	_record = record
	_shot_id = shot_id
	_skip_requested = false
	_backswing_m = 0.0
	_last_shot = {"shot_id": shot_id, "sim_us": record.sim_us(), "events": record.events().size(),
			"potted": _potted(record)}
	_cue.play_strike(setup.speed_m_s)
	_audio.begin_shot(record.strike_outcome())
	_guides.hide_all()
	var playback := ShotPlayback.from_record(record)
	_start_cycle(playback, shot_id)
	_bar.close_menu()
	_stroke.cancel_stroke("shot started")
	# Their touches are ignored while it plays, so a thumb still down would never lift.
	_spin.cancel_press("shot started")
	_wheel.cancel_press("shot started")
	_elevation.cancel_press("shot started")
	_pull.cancel_press("shot started")
	_set_playing(true)
	_overlay.set_shot_times(record.sim_us(), _guides.last_predict_us())
	# A shot with no motion finishes inside play(), so everything above is in place first.
	_player.play(playback, shot_id)
	return shot_id


## Chooses the camera sequence for the shot about to play (spec 8.5), logs it and starts it.
func _start_cycle(playback: ShotPlayback, shot_id: String) -> void:
	_plan = ShotCycle.plan(playback, _free.balls, _free.cue_ball, bool(Settings.get_value("pocket_camera")), _pockets)
	var fields := {"shot_id": shot_id, "sequence": _plan["sequence"]}
	for key: String in ["ball", "pocket", "drop_t", "near_t"]:
		if _plan.has(key):
			fields[key] = _plan[key]
	if not String(_plan["reason"]).is_empty():
		fields["reason"] = _plan["reason"]
	var pose := Transform3D.IDENTITY
	if _plan["sequence"] == "pocket":
		pose = _rig.pocket_pose(_plan["drop_centre"], _plan["from"], _plan["middle"], _plan["track"])
		fields["pose"] = ShotCycle.pose_fields(pose)
	if _plan.has("problem"):
		Log.warn("app", "app.error", {"kind": "message", "message": "main: shot %s has no pocket camera: %s (pockets %s)"
				% [shot_id, _plan["problem"], _pockets]})
	Log.info("shot", "shot.camera", fields)
	_cycle_t = 0.0
	_cycle.start(_plan, pose)


## The logger truncates a long line from its last values, so the identity fields go first and the
## gesture samples last.
## The strike with its aim turned for the side on it (SpinAim); shot_id "" for a guide's
## prediction, which logs nothing.
func _allowing_for_side(strike: Dictionary, shot_id: String = "") -> Dictionary:
	if not bool(Settings.get_value("allow_for_side")):
		return strike
	var began := Time.get_ticks_usec()
	var solved := SpinAim.solve(Physics.predict, _free.balls, _free.cue_ball, strike, setup.elevation_min_rad)
	if int(solved["sims"]) > 0 and not shot_id.is_empty():
		Log.info("shot", "shot.side_allowed", {"shot_id": shot_id, "turned_rad": solved["turned_rad"],
				"target": solved["target"], "residual_rad": solved["residual_rad"], "reason": solved["reason"],
				"sims": solved["sims"], "us": Time.get_ticks_usec() - began})
	var allowed := strike.duplicate()
	allowed["aim_rad"] = solved["aim_rad"]
	return allowed


func _shot_input_fields(shot_id: String, strike_mode: String) -> Dictionary:
	var fields := {"shot_id": shot_id, "mode": MODE, "guide_level": _guide_level(), "strike_mode": strike_mode}
	var known := setup.log_fields()
	for key: String in known:
		if not fields.has(key) and not key.ends_with("_samples"):
			fields[key] = known[key]
	fields["fine_tune_samples"] = known["fine_tune_samples"]
	fields["stroke_samples"] = known["stroke_samples"]
	return fields


func _on_frame(state: PackedFloat64Array, t: float, dt: float) -> void:
	_balls.apply_state(state, dt)
	_audio.on_frame(state, t, dt)
	# A skip jumps to the end, past the cuts.
	if _playing and not _skip_requested:
		_cycle_t = t
		_cycle.update(t)


func _on_event(event: Dictionary, state: PackedFloat64Array) -> void:
	if _player.is_skipping():
		return
	_audio.on_event(event, state)
	_haptics.on_event(event)
	if String(event.get("kind", "")) == "pocket":
		_on_pot(event)


## An object ball dropped: its value floats up out of the pocket and the break counts it.
func _on_pot(event: Dictionary) -> void:
	var index := int(event.get("a", -1))
	if index == _free.cue_ball:
		return
	var pocket := int(event.get("body", -1))
	var ball: Dictionary = _free.balls[index] if index >= 0 and index < _free.balls.size() else {}
	var value := int(ShotCycle.VALUES.get(String(ball.get("ball", "")), 0))
	if value <= 0 or pocket < 0 or pocket >= _pockets.size():
		if not _pot_warned:
			_pot_warned = true
			Log.warn("app", "app.error", {"kind": "message", "message": "main: shot %s potted ball %d (%s) in pocket %d of %d; no points shown"
					% [_shot_id, index, ball, pocket, _pockets.size()]})
		return
	var centre: Array = (_pockets[pocket] as Dictionary)["drop_centre"]
	_popups.pop(value, Vector2(float(centre[0]), float(centre[1])))
	_cycle.count(value)
	_show_break()


func _on_shot_finished(shot_id: String) -> void:
	if not _playing or shot_id != _shot_id:
		return
	if _skip_requested:
		_balls.finish_drops()
		_cycle.cancel()
	_settling = true
	_settle()


## Waits for the last drops into the pockets and the shot cycle's full-table view, then hands the
## result to the free table, turns the aim to the next ball and swoops back behind the cue ball.
func _settle() -> void:
	if _balls.is_dropping() or not _cycle.settle_ready(_cycle_t):
		set_process(true)
		return
	set_process(false)
	_settling = false
	var final_balls := _record.final_balls()
	_record = null
	var index := _free.history_index()
	_free.shot_finished(final_balls)
	if _free.history_index() == index:
		# FreeTable refused the result and logged why; show the table it kept.
		_balls.sync(_free.balls)
	setup.after_shot(bool(Settings.get_value("reset_spin_after_shot")))
	var result := {"shot_id": _shot_id}
	result.merge(_cycle.end_shot())
	# Before the camera moves, so the swoop lands on the new aim.
	result.merge(_turn_to_next())
	Log.info("shot", "shot.result", result)
	_show_break()
	_set_playing(false)
	_rig.reset_view()
	_refresh()
	_rig.hold(false)


## Turns the aim to the next ball (spec 8.5, the return) unless "aim_next_ball" is off; shot.result's
## next_ fields.
func _turn_to_next() -> Dictionary:
	if not bool(Settings.get_value("aim_next_ball")):
		return {"next_ball": "", "next_reason": "setting off"}
	var prefer := _cycle.prefer(_free.balls)
	var chosen: Variant = choose_next.call(_free.balls, _free.cue_ball, prefer)
	if not chosen is Dictionary or (chosen as Dictionary).is_empty():
		if not chosen is Dictionary:
			Log.warn("app", "app.error", {"kind": "message", "message": "main: NextTarget.choose(prefer %s) returned %s, not a Dictionary; the aim stays"
					% [prefer, type_string(typeof(chosen))]})
		return {"next_ball": "", "next_reason": "no object ball"}
	var target: Dictionary = chosen
	var aim: Variant = target.get("aim_rad")
	if not (aim is float or aim is int) or not is_finite(float(aim)):
		Log.warn("app", "app.error", {"kind": "message", "message": "main: NextTarget.choose(prefer %s) gave no usable aim_rad: %s; the aim stays"
				% [prefer, target]})
		return {"next_ball": "", "next_reason": "no usable aim"}
	setup.set_aim(float(aim))
	# NextTarget's "ball" is already the label ("red 3").
	return {"next_ball": String(target.get("ball", "")), "next_pocket": int(target.get("pocket", -1)),
			"next_aim_rad": setup.aim_rad, "next_cut_deg": float(target.get("cut_deg", 0.0)),
			"next_reason": String(target.get("reason", ""))}


func _set_playing(on: bool) -> void:
	_playing = on
	_update_aim_enabled()
	for control: Control in [_spin, _elevation, _wheel, _stroke, _pull]:
		control.mouse_filter = Control.MOUSE_FILTER_IGNORE if on else Control.MOUSE_FILTER_STOP
		control.modulate.a = PLAYING_ALPHA if on else 1.0
	for id: String in ["edit", "rack", "empty", "overview"]:
		_bar.button(id).disabled = on
	if on:
		_bar.set_history(false, false, false)
	else:
		_bar.set_history(_free.can_undo(), _free.can_redo(), _free.can_retake())


static func _potted(record: OfsShotRecord) -> Array:
	var potted: Array = []
	for entry: Variant in record.final_balls():
		var ball: Dictionary = entry
		if int(ball.get("pocket", -1)) >= 0:
			potted.append({"ball": ball["ball"], "index": ball["index"], "pocket": ball["pocket"]})
	return potted


func _on_setup_changed(_what: StringName) -> void:
	_queue_refresh()


func _on_backswing_changed(distance_m: float) -> void:
	_backswing_m = distance_m
	_queue_refresh()


# Once per frame at most: a fine-tune turn changes the aim many times in one event.
func _queue_refresh() -> void:
	if _refresh_queued:
		return
	_refresh_queued = true
	_flush_refresh.call_deferred()


func _flush_refresh() -> void:
	if _refresh_queued:
		_refresh()


func _refresh() -> void:
	if _free.cue_ball < 0 or _free.cue_ball >= _free.balls.size():
		_refresh_queued = false
		return
	var cue_xy := _cue_xy()
	if not _playing:
		_update_elevation_min()
	_rig.set_cue_elevation(0.0 if _free.edit_mode else setup.effective_elevation())
	_rig.follow(cue_xy, setup.aim_rad)
	if not _playing:
		if _free.edit_mode:
			_cue.show_cue(false)
			_guides.hide_all()
		else:
			_cue.show_cue(true)
			_cue.set_pose(cue_xy, setup.aim_rad, setup.effective_elevation(), Vector2(setup.tip_x, setup.tip_y),
					_backswing_m)
			_cue.clear_of(_rig.camera().global_position)
			_guides.show_for(_free.balls, _free.cue_ball, setup, _guide_level(), _free.version())
	# Last, so the elevation minimum's own change signal doesn't queue another pass.
	_refresh_queued = false


func _update_elevation_min() -> void:
	setup.set_elevation_min(Physics.min_cue_elevation(_free.balls, _free.cue_ball, setup.to_strike()))


func _cue_xy() -> Vector2:
	var cue: Dictionary = _free.balls[_free.cue_ball]
	return Vector2(float(cue["x"]), float(cue["y"]))


func _guide_level() -> int:
	return int(Settings.get_value("guide_level"))


func _on_overview_toggled(on: bool) -> void:
	if _playing:
		_bar.set_overview(_rig.is_overview())
		return
	_rig.set_overview(on)
	if on and not _overview_hint_shown:
		_overview_hint_shown = true
		_show_notice(OVERVIEW_HINT)


func _on_overview_changed(on: bool) -> void:
	_bar.set_overview(on)
	_update_aim_enabled()


# In the overview a drag walks round the table instead of turning the aim.
func _update_aim_enabled() -> void:
	_aim.enabled = not _playing and not _free.edit_mode and not _rig.is_overview()


func _on_camera_moved() -> void:
	_stage.fit_shadows(_rig.camera())
	_cue.clear_of(_rig.camera().global_position)
	_guides.place_pace_label()


func _on_setting_changed(key: String, value: Variant) -> void:
	match key:
		"strike_mode":
			setup.set_strike_mode(String(value))
			_show_controls()
		"guide_level":
			_queue_refresh()
		"fine_tune_wheel":
			if not bool(value):
				_wheel.cancel_press("wheel turned off")
			_show_controls()
			_layout()


func _on_table_changed(balls: Array[Dictionary], _cue_ball: int, reason: StringName) -> void:
	_abort_drag()
	_balls.sync(balls)
	# Only a shot carries the break on; a retake, undo, redo, setup or edit starts again.
	if reason != &"shot":
		_reset_break()
	if reason != &"edit" or _selected >= balls.size():
		_selected = -1
	_update_palette()
	_place_selection()
	_queue_refresh()


func _on_history_changed(can_undo: bool, can_redo: bool, can_retake: bool) -> void:
	if not _playing:
		_bar.set_history(can_undo, can_redo, can_retake)


func _history(action: String) -> void:
	if _playing:
		return
	var snapshot: Dictionary
	match action:
		"retake":
			snapshot = _free.retake()
		"undo":
			snapshot = _free.undo()
		"redo":
			snapshot = _free.redo()
	_audio.stop_all()
	if snapshot.is_empty():
		if action == "redo" and _free.can_retake():
			# Redone to the latest shot's result: spin as that shot left it.
			setup.after_shot(bool(Settings.get_value("reset_spin_after_shot")))
		return
	setup.restore(snapshot)
	# The strike mode is a setting, not part of the shot.
	var mode := String(Settings.get_value("strike_mode"))
	if setup.strike_mode != mode:
		setup.set_strike_mode(mode)


func _choose_setup(setup_name: String) -> void:
	if not _playing:
		_free.load_setup(setup_name)


func _on_edit_mode_changed(on: bool) -> void:
	_abort_drag()
	_reset_break()
	_bar.set_edit_mode(on)
	_update_aim_enabled()
	_selected = -1
	_spin.cancel_press("edit mode")
	_show_controls()
	_update_palette()
	_place_selection()
	_queue_refresh()


## The shot controls for the strike mode, or the edit palette in edit mode.
func _show_controls() -> void:
	var editing := _free.edit_mode
	var pull := setup.strike_mode == ShotSetup.PULL
	_spin.visible = not editing
	_elevation.visible = not editing
	_wheel.visible = not editing and bool(Settings.get_value("fine_tune_wheel"))
	_stroke.visible = not editing and not pull
	_pull.visible = not editing and pull
	_palette.visible = editing


func _on_tapped(at: Vector2) -> void:
	if _playing:
		skip_shot()
	elif _free.edit_mode:
		_select(_balls.ball_at_screen(_rig.camera(), at))


func _on_drag_started(at: Vector2) -> void:
	if not _free.edit_mode or _playing:
		return
	_dragged = _balls.ball_at_screen(_rig.camera(), at)
	if _dragged >= 0:
		_select(_dragged)


func _on_drag_moved(at: Vector2, relative: Vector2) -> void:
	if _dragged < 0:
		if _playing:
			# At the pocket (or before the first cut) a drag cuts to the full-table view to walk round.
			_cycle.walk_round(_cycle_t)
		_rig.orbit(relative.x)
		return
	var xy: Variant = TableSpace.screen_to_core(_rig.camera(), at, _balls.radius)
	if xy != null:
		_balls.place_ball(_dragged, xy)
		_place_selection()


func _on_drag_ended(at: Vector2) -> void:
	if _dragged < 0:
		return
	var index := _dragged
	_dragged = -1
	var xy: Variant = TableSpace.screen_to_core(_rig.camera(), at, _balls.radius)
	# FreeTable logs a refused move; the ball goes back where it was.
	if xy == null or not _free.move_ball(index, xy):
		_balls.sync(_free.balls)
		_place_selection()
		_show_notice("A ball can't go there")


## Puts a ball being dragged back where FreeTable has it and ends the gesture, so a drag never
## outlives edit mode or the table it started on.
func _abort_drag() -> void:
	if _dragged < 0:
		return
	_dragged = -1
	_balls.sync(_free.balls)
	_place_selection()
	_gestures.set_enabled(false)
	_gestures.set_enabled(true)


func _select(index: int) -> void:
	_selected = index if index >= 0 and index < _free.balls.size() else -1
	_update_palette()
	_place_selection()


func _add_ball(ball: String) -> void:
	var spot: Variant = _free_spot(ball)
	if spot == null:
		_show_notice("No room for another %s" % ball)
		return
	if _free.add_ball(ball, spot):
		_select(_free.balls.size() - 1)


func _remove_selected() -> void:
	if _selected >= 0 and _free.remove_ball(_selected):
		_select(-1)


## Where an added ball goes: its spot (a red: the first free place in the pack), else the nearest
## legal place round it. null when the table has no room.
func _free_spot(ball: String) -> Variant:
	var tries: Array[Vector2] = []
	if ball == "red":
		for entry: Variant in OfsTable.opening_layout(FreeTable.OPENING_GAP):
			if (entry as Dictionary)["ball"] == "red":
				tries.append(Vector2(float(entry["x"]), float(entry["y"])))
	else:
		var spot := OfsTable.spot(ball)
		if not spot.is_empty():
			tries.append(Vector2(float(spot["x"]), float(spot["y"])))
	for xy in tries:
		if _free.is_legal(xy):
			return xy
	var centre := tries[0] if not tries.is_empty() else Vector2.ZERO
	var step := 2.0 * _balls.radius + 0.002
	for ring in range(1, FREE_SPOT_RINGS + 1):
		var count := 6 * ring
		for k in count:
			var xy := centre + Vector2.from_angle(TAU * k / count) * step * ring
			if _free.is_legal(xy):
				return xy
	return null


func _update_palette() -> void:
	var reds := 0
	var colours: Array[String] = FreeTable.COLOURS.duplicate()
	for b: Dictionary in _free.balls:
		if b["ball"] == "red":
			reds += 1
		colours.erase(String(b["ball"]))
	_palette.set_available(reds < FreeTable.RED_COUNT, colours)
	if _selected < 0:
		_palette.set_selected("", false)
		return
	var ball: Dictionary = _free.balls[_selected]
	var label := "red %d" % int(ball["index"]) if ball["ball"] == "red" else String(ball["ball"])
	_palette.set_selected(label, _selected != _free.cue_ball)


func _build_selection() -> void:
	var r := float(OfsTable.dimensions().get("ball_radius", BallsView.RADIUS_FALLBACK))
	var ring := TorusMesh.new()
	ring.inner_radius = r * 1.15
	ring.outer_radius = r * 1.45
	ring.rings = 48
	ring.ring_segments = 8
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_color = SELECTION_COLOUR
	ring.material = material
	_selection.name = "Selection"
	_selection.mesh = ring
	# Flattened onto the cloth, so it reads as a ring round the ball from any height.
	_selection.scale = Vector3(1.0, 0.1, 1.0)
	_selection.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_selection.layers = Stage.MOVING_LAYER
	_selection.visible = false
	add_child(_selection)


static func _set_layers(node: Node, layers: int) -> void:
	var visual := node as VisualInstance3D
	if visual != null:
		visual.layers = layers
	for child in node.get_children():
		_set_layers(child, layers)


func _place_selection() -> void:
	var shown := _free.edit_mode and _selected >= 0 and _selected < _balls.get_child_count()
	if shown:
		var ball := _balls.get_child(_selected) as Node3D
		_selection.position = Vector3(ball.position.x, 0.002, ball.position.z)
	if _selection.visible != shown:
		_selection.visible = shown


func _build_notice() -> void:
	var panel := StyleBoxFlat.new()
	panel.bg_color = Color(0.05, 0.06, 0.065, 0.78)
	panel.set_corner_radius_all(18)
	panel.content_margin_left = 24.0
	panel.content_margin_right = 24.0
	panel.content_margin_top = 10.0
	panel.content_margin_bottom = 10.0
	_notice.add_theme_stylebox_override("normal", panel)
	_notice.add_theme_color_override("font_color", SettingsPanel.TEXT)
	_notice.add_theme_font_size_override("font_size", 28)
	_notice.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_notice_timer.one_shot = true
	_notice_timer.timeout.connect(func() -> void: _notice.visible = false)
	add_child(_notice_timer)


## The running break (spec 8.5): a small pill at the bottom centre, shown while above zero.
func _build_break_pill() -> void:
	var panel := StyleBoxFlat.new()
	panel.bg_color = Color(0.05, 0.06, 0.065, 0.72)
	panel.set_corner_radius_all(26)
	panel.content_margin_left = 22.0
	panel.content_margin_right = 22.0
	panel.content_margin_top = 6.0
	panel.content_margin_bottom = 6.0
	_break_pill.name = "BreakPill"
	_break_pill.add_theme_stylebox_override("normal", panel)
	_break_pill.add_theme_color_override("font_color", SettingsPanel.TEXT)
	_break_pill.add_theme_font_size_override("font_size", 28)
	_break_pill.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_break_pill.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_break_pill.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_break_pill.visible = false
	$Hud.add_child(_break_pill)


func _show_break() -> void:
	var value := _cycle.shown_break
	if value <= 0:
		_break_pill.visible = false
		return
	var text := BREAK_TEXT % value
	if _break_pill.text != text:
		_break_pill.text = text
		_break_pill.reset_size()
	_break_pill.visible = true
	_place_break()


func _reset_break() -> void:
	_cycle.reset_break()
	_popups.clear()
	_show_break()


# Centred under the table, inside the band the layout leaves it; written only when it moves.
func _place_break() -> void:
	var pill := _break_pill.get_combined_minimum_size()
	pill.y = maxf(pill.y, HudScript.BREAK_HEIGHT)
	if _break_pill.size != pill:
		_break_pill.size = pill
	var band := _break_band
	var x := clampf(_break_centre_x - pill.x / 2.0, band.position.x, maxf(band.end.x - pill.x, band.position.x))
	var at := Vector2(x, band.end.y - pill.y).round()
	if _break_pill.position != at:
		_break_pill.position = at


## A line at the top of the screen for a moment: no pop-up, nothing to dismiss (spec 11.2).
func _show_notice(text: String) -> void:
	_notice.text = text
	_notice.reset_size()
	_notice.visible = true
	_layout()
	_notice_timer.start(NOTICE_SECONDS)


func _layout() -> void:
	if not is_inside_tree():
		return
	var window := get_window()
	var canvas := get_viewport().get_visible_rect().size
	var safe := HudScript.safe_rect(Rect2(DisplayServer.get_display_safe_area()),
			Rect2(Vector2(window.position), Vector2(window.size)), canvas)
	var housing := OfsDevice.housing_side()
	var with_wheel := bool(Settings.get_value("fine_tune_wheel"))
	var rects := HudScript.layout(safe, _bar.row_rect().size, canvas, housing, with_wheel)
	if housing != _housing:
		_housing = housing
		Log.info("ui", "ui.layout", {"housing": housing, "safe": [safe.position.x, safe.position.y, safe.size.x,
				safe.size.y], "cue": _rect_list(rects["cue"]), "spin": _rect_list(rects["spin"])})
	_put(_wheel, rects["wheel"])
	_put(_stroke, rects["cue"])
	_put(_pull, rects["cue"])
	_put(_elevation, rects["elevation"])
	_put(_spin, rects["spin"])
	_palette.place_in(rects["bottom"])
	_break_band = rects["break"]
	_break_centre_x = safe.get_center().x
	if _break_pill.visible:
		_place_break()
	_overlay.clear_right = safe.end.x - (rects["cue"] as Rect2).position.x
	var controls: Array[Rect2] = []
	for key: String in ["bar", "spin", "elevation", "cue", "wheel"]:
		if key != "wheel" or with_wheel:
			controls.append(rects[key])
	_rig.keep_clear(controls)
	var notice_at := Vector2(safe.get_center().x - _notice.size.x / 2.0, safe.position.y + HudScript.EDGE).round()
	if _notice.position != notice_at:
		_notice.position = notice_at


static func _rect_list(rect: Rect2) -> Array:
	return [rect.position.x, rect.position.y, rect.size.x, rect.size.y]


# Writes only what changed, so the safe-area poll never makes low-processor mode redraw.
static func _put(control: Control, rect: Rect2) -> void:
	if control.position != rect.position:
		control.position = rect.position
	if control.size != rect.size:
		control.size = rect.size


## Harness hook; debug builds only, called through get_tree().current_scene.
func harness_load_position(balls: Array, cue_ball: int) -> bool:
	if _playing:
		_harness_refused("load_position", "a shot is playing (%s)" % _shot_id)
		return false
	var list: Array[Dictionary] = []
	for entry: Variant in balls:
		if not entry is Dictionary:
			_harness_refused("load_position", "a ball entry is %s, not a Dictionary" % type_string(typeof(entry)))
			return false
		list.append(entry)
	return _free.set_balls(list, cue_ball)


## Plays a shot with exact parameters (a strike Dictionary); its shot id, or "" when refused.
func harness_play_shot(strike: Dictionary) -> String:
	if _playing or _free.edit_mode:
		_harness_refused("play_shot", "a shot is playing (%s)" % _shot_id if _playing else "edit mode is on")
		return ""
	_warn_out_of_range(strike)
	var speed := float(strike["speed_m_s"])
	setup.set_aim(float(strike["aim_rad"]))
	setup.set_tip(float(strike["tip_x"]), float(strike["tip_y"]))
	setup.set_elevation(float(strike["elevation_rad"]))
	setup.set_speed(speed, PullControl.power_for_speed(speed, Physics.param("max_cue_speed_m_s", 10.0)))
	setup.wander_rad = 0.0
	return _strike(HARNESS_STRIKE)


## A strike the controls couldn't set still plays as given, with a warning.
func _warn_out_of_range(strike: Dictionary) -> void:
	var problems: Array[String] = []
	var tip := Vector2(float(strike["tip_x"]), float(strike["tip_y"])).length()
	if tip > 1.0:
		problems.append("tip offset %.4f is beyond 1, the ball's edge" % tip)
	var speed := float(strike["speed_m_s"])
	var max_speed := Physics.param("max_cue_speed_m_s", 10.0)
	if speed > max_speed:
		problems.append("speed %.4f m/s is above max_cue_speed_m_s %.4f" % [speed, max_speed])
	var elevation := float(strike["elevation_rad"])
	var max_elevation := Physics.param("max_elevation_rad", deg_to_rad(80.0))
	if elevation < 0.0 or elevation > max_elevation:
		problems.append("elevation %.4f rad is outside 0 to max_elevation_rad %.4f" % [elevation, max_elevation])
	if not problems.is_empty():
		Log.warn("harness", "harness.command", {"command": "play_shot", "args": [strike], "ok": true,
				"result": {"warnings": problems}})


## True while a shot is playing, a ball is still dropping into a pocket, or the shot cycle's camera
## has yet to hold the full-table view (spec 8.5).
func harness_busy() -> bool:
	return _playing


## The game's own part of the state dump: screen, balls, shot setup, settings, last shot.
func harness_state() -> Dictionary:
	var shot := setup.snapshot()
	shot["elevation_min_rad"] = setup.elevation_min_rad
	return {
		"screen": SCREEN,
		"balls": _free.balls.duplicate(true),
		"cue_ball": _free.cue_ball,
		"setup": shot,
		"settings": Settings.all(),
		"playing": _playing,
		"edit_mode": _free.edit_mode,
		"history": {"index": _free.history_index(), "size": _free.history_size()},
		"last_shot": _last_shot.duplicate(true),
		"camera": {"pitch_deg": _rig.pitch_deg(), "zoom_scale": _rig.zoom_scale(), "view": _rig.view_name()},
		"break": _cycle.shown_break,
	}


func _harness_refused(command: String, why: String) -> void:
	Log.warn("app", "app.error", {"kind": "message", "message": "main: harness %s refused: %s" % [command, why]})
