extends Node3D
## The free table (spec 12.1, 8), where the app opens: the table, balls and cue, one ShotSetup
## every control writes, the shot from strike to playback, edit mode, and the harness's hooks
## (spec 16.5, docs/harness.md).

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

var setup := ShotSetup.new()

var _backswing_m := 0.0
var _record: OfsShotRecord
var _shot_id := ""
## From the strike until the balls have stopped and every drop has finished.
var _playing := false
var _skip_requested := false
var _settling := false
var _last_shot := {}
var _refresh_queued := false
## Edit mode: list positions of the ball under the finger and of the selected ball, or -1.
var _dragged := -1
var _selected := -1
var _selection := MeshInstance3D.new()
var _notice_timer := Timer.new()

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
@onready var _stroke: StrokeControl = $Hud/StrokeArea
@onready var _power: PowerBar = $Hud/PowerBar
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
	_stage.capture_reflections()
	_balls.set_pockets(shape.get("pockets", []))
	_rig.moved.connect(_on_camera_moved)
	# Kept out of the table's reflection probe, which would show them where they were captured.
	_set_layers(_guides, Stage.MOVING_LAYER)

	setup.strike_mode = String(Settings.get_value("strike_mode"))
	_aim.bind(setup, _gestures, _rig)
	_wheel.bind(setup)
	_spin.bind(setup)
	_elevation.bind(setup)
	_stroke.bind(setup)
	_power.bind(setup)
	setup.changed.connect(_on_setup_changed)
	_stroke.backswing_changed.connect(_on_backswing_changed)
	_stroke.struck.connect(func(_speed: float) -> void: _strike(ShotSetup.STROKE))
	_power.shoot_pressed.connect(func() -> void: _strike(ShotSetup.POWER_BAR))
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
	_palette.add_pressed.connect(_add_ball)
	_palette.remove_pressed.connect(_remove_selected)
	Settings.changed.connect(_on_setting_changed)

	_build_selection()
	_build_notice()
	_spin.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	_spin.grow_horizontal = Control.GROW_DIRECTION_END
	_spin.grow_vertical = Control.GROW_DIRECTION_BEGIN
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


func _process(_delta: float) -> void:
	if _settling:
		_settle()


# Keyboard test aids (spec 8.8): R retake, Ctrl+Z undo, Ctrl+Y redo, Space shoots in power-bar
# mode. F3 is the overlay's own.
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
		if _power.is_visible_in_tree() and _power.can_shoot():
			_strike(ShotSetup.POWER_BAR)
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


## Ends the playing shot at once, as a tap on the table does.
func skip_shot() -> void:
	if _playing and _player.is_playing():
		_skip_requested = true
		_player.skip_to_end()


# The shot (spec 8.3, 16.10).

## Logs, simulates and starts playing the shot set up; its id, or "" when none could start.
## strike_mode is what the logs call the way it was struck: stroke, power_bar or harness.
func _strike(strike_mode: String) -> String:
	if _playing or _free.edit_mode:
		return ""
	var shot_id := Physics.new_shot_id()
	Params.commit_pending(shot_id)
	_update_elevation_min()
	var fields := setup.log_fields()
	fields["shot_id"] = shot_id
	fields["mode"] = MODE
	fields["strike_mode"] = strike_mode
	fields["guide_level"] = _guide_level()
	Log.info("shot", "shot.input", fields)
	_free.shot_started(setup.snapshot(), shot_id)
	var record := Physics.simulate(_free.balls, _free.cue_ball, setup.to_strike(),
			{"shot_id": shot_id, "mode": MODE, "strike_mode": strike_mode})
	if record == null:
		# The binding has logged shot.rejected with the reason.
		setup.stroke_samples.clear()
		setup.wander_rad = 0.0
		_show_notice("That shot couldn't be played")
		_queue_refresh()
		return ""
	_record = record
	_shot_id = shot_id
	_skip_requested = false
	_backswing_m = 0.0
	_last_shot = {"shot_id": shot_id, "sim_us": record.sim_us(), "events": record.events().size(),
			"potted": _potted(record)}
	_cue.play_strike(setup.speed_m_s)
	_audio.begin_shot(record.strike_outcome())
	_guides.hide_all()
	_rig.hold(true)
	_spin.collapse()
	_stroke.cancel_stroke("shot started")
	_set_playing(true)
	_overlay.set_shot_times(record.sim_us(), _guides.last_predict_us())
	# A shot with no motion finishes inside play(), so everything above is in place first.
	_player.play(ShotPlayback.from_record(record), shot_id)
	return shot_id


func _on_frame(state: PackedFloat64Array, t: float, dt: float) -> void:
	_balls.apply_state(state, dt)
	_audio.on_frame(state, t, dt)


func _on_event(event: Dictionary, state: PackedFloat64Array) -> void:
	if _player.is_skipping():
		return
	_audio.on_event(event, state)
	_haptics.on_event(event)


func _on_shot_finished(shot_id: String) -> void:
	if not _playing or shot_id != _shot_id:
		return
	if _skip_requested:
		_balls.finish_drops()
	_settling = true
	_settle()


## Waits for the last drops into the pockets, then hands the result to the free table.
func _settle() -> void:
	if _balls.is_dropping():
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
	_set_playing(false)
	_refresh()
	_rig.hold(false)


func _set_playing(on: bool) -> void:
	_playing = on
	_aim.enabled = not on and not _free.edit_mode
	for control: Control in [_spin, _elevation, _wheel, _stroke, _power]:
		control.mouse_filter = Control.MOUSE_FILTER_IGNORE if on else Control.MOUSE_FILTER_STOP
		control.modulate.a = PLAYING_ALPHA if on else 1.0
	for id: String in ["edit", "rack", "empty"]:
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


# Aiming: the cue, camera, guides and obstruction follow the setup and the table.

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
	_rig.follow(cue_xy, setup.aim_rad)
	if not _playing:
		if _free.edit_mode:
			_cue.show_cue(false)
			_guides.hide_all()
		else:
			_cue.show_cue(true)
			_cue.set_pose(cue_xy, setup.aim_rad, setup.effective_elevation(), Vector2(setup.tip_x, setup.tip_y),
					_backswing_m)
			_guides.show_for(_free.balls, _free.cue_ball, setup, _guide_level())
	# Last, so the elevation minimum's own change signal doesn't queue another pass.
	_refresh_queued = false


func _update_elevation_min() -> void:
	setup.set_elevation_min(Physics.min_cue_elevation(_free.balls, _free.cue_ball, setup.to_strike()))


func _cue_xy() -> Vector2:
	var cue: Dictionary = _free.balls[_free.cue_ball]
	return Vector2(float(cue["x"]), float(cue["y"]))


func _guide_level() -> int:
	return int(Settings.get_value("guide_level"))


func _on_camera_moved() -> void:
	_stage.fit_shadows(_rig.camera())


func _on_setting_changed(key: String, value: Variant) -> void:
	match key:
		"strike_mode":
			setup.set_strike_mode(String(value))
			_show_controls()
		"guide_level":
			_queue_refresh()


# The free table: history, setups and edit mode.

func _on_table_changed(balls: Array[Dictionary], _cue_ball: int, reason: StringName) -> void:
	_balls.sync(balls)
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
	_bar.set_edit_mode(on)
	_aim.enabled = not on and not _playing
	_dragged = -1
	_selected = -1
	_spin.collapse()
	_show_controls()
	_update_palette()
	_place_selection()
	_queue_refresh()


## The shot controls for the strike mode, or the edit palette in edit mode.
func _show_controls() -> void:
	var editing := _free.edit_mode
	var power_bar := setup.strike_mode == ShotSetup.POWER_BAR
	_spin.visible = not editing
	_elevation.visible = not editing
	_wheel.visible = not editing
	_stroke.visible = not editing and not power_bar
	_power.visible = not editing and power_bar
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


func _on_drag_moved(at: Vector2, _relative: Vector2) -> void:
	if _dragged < 0:
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


# The HUD.

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
	var rects := HudScript.layout(safe, _bar.size)
	_put(_wheel, rects["wheel"])
	_put(_stroke, rects["stroke"])
	_put(_power, rects["power"])
	_put(_elevation, rects["elevation"])
	var corner: Vector2 = rects["spin_corner"]
	var bottom := corner.y - canvas.y
	if _spin.offset_left != corner.x or _spin.offset_bottom != bottom:
		_spin.offset_left = corner.x
		_spin.offset_right = corner.x
		_spin.offset_top = bottom
		_spin.offset_bottom = bottom
	_palette.place_in(rects["bottom"])
	var notice_at := Vector2(safe.get_center().x - _notice.size.x / 2.0, safe.position.y + HudScript.EDGE).round()
	if _notice.position != notice_at:
		_notice.position = notice_at


# Writes only what changed, so the safe-area poll never makes low-processor mode redraw.
static func _put(control: Control, rect: Rect2) -> void:
	if control.position != rect.position:
		control.position = rect.position
	if control.size != rect.size:
		control.size = rect.size


# The harness's hooks (spec 16.5; docs/harness.md). Debug builds only: Harness calls them through
# get_tree().current_scene.

## balls: a ball list (OfsPhysics); false when the position isn't legal.
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
	setup.set_aim(float(strike["aim_rad"]))
	setup.set_tip(float(strike["tip_x"]), float(strike["tip_y"]))
	setup.set_elevation(float(strike["elevation_rad"]))
	setup.set_speed(float(strike["speed_m_s"]))
	setup.wander_rad = 0.0
	return _strike(HARNESS_STRIKE)


## True while a shot is playing or a ball is still dropping into a pocket.
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
		"camera": {"pitch_deg": _rig.pitch_deg(), "zoom_scale": _rig.zoom_scale()},
	}


func _harness_refused(command: String, why: String) -> void:
	Log.warn("app", "app.error", {"kind": "message", "message": "main: harness %s refused: %s" % [command, why]})
