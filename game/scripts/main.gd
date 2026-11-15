extends Node3D
## The table, in one of three modes (spec 11.1): Home, where the app opens; the free table (spec
## 12.1, 8); and a match (spec 9, 11). All three share the table, balls and cue, one ShotSetup
## every control writes, and the shot from strike to playback and its cycle (spec 8.5: the pocket
## camera, the points, the break, the aim turned to the next ball). The free table adds edit mode
## and the practice shots (spec 12.1). A match plays as MatchController says: whose stroke it is,
## ball in hand (spec 8.6), nomination (spec 8.7), the opponent's strokes (spec 10.5), the
## referee's calls and choices, pause and the summaries; no rule is worked out here. Also the
## harness's hooks (spec 16.5, docs/harness.md).

const HudScript := preload("res://scripts/hud.gd")
## The modes, as a shot's log context names them.
const HOME := "home"
const FREE_TABLE := "free_table"
const MATCH := "match"
## The screens that are not a mode's own, as screen.changed names them; the free table's is TABLE.
const TABLE := "table"
const MATCH_SETUP := "match_setup"
const PAUSE := "pause"
const FRAME_SUMMARY := "frame_summary"
const MATCH_SUMMARY := "match_summary"
const LAYOUT := "full_rack"
const HARNESS_STRIKE := "harness"
## A practice shot shown as filmed.
const DEMO_STRIKE := "demo"
## A stroke the stand-in chose: the opponent's, or the person's while it plays for them.
const OPPONENT_STRIKE := "opponent"
const AUTOPLAY_STRIKE := "autoplay"
const PERSON := MatchController.PERSON
## MatchController.strike_blocked() while the stroke is the person's to set up.
const SETTING_UP: Array[String] = ["", "nominate"]
## And while the shot controls show at all: also through the opponent's turn, dimmed.
const STROKE_ON: Array[String] = ["", "nominate", "opponent", "playing"]
## Seconds the opponent's cue stands on its line before it strikes (spec 10.5), at pace 1.
const LINEUP_SECONDS := 0.5
## Ball in hand: how far back from the baulk line the camera's place is looked for, and in what steps.
const PLACING_STEP_M := 0.05
const PLACING_STEPS := 40
## Home: the steps, in canvas units, in which room is taken from the sides of its full-table view,
## and the most taken from each side as a share of the screen's width.
const HOME_MARGIN_STEP := 16.0
const HOME_MARGIN_MAX := 0.4
const NOMINATE_NOTICE := "Tap a colour to nominate it"
const NO_MATCH_NOTICE := "The match couldn't be started"
## What the person is told of the opponent's choice, where the table alone doesn't show it.
const OPPONENT_CHOSE := {
	"play_again": "Computer puts you back in",
	"replace": "Computer has the balls replaced",
	"refuse_rerack": "Computer plays on",
	"play_first": "Computer plays first",
	"play_second": "You play first",
}
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
## The same in a match, among the balls on: NextTarget.choose(balls, cue_ball, "any", places).
var choose_among: Callable

## HOME, FREE_TABLE or MATCH: whose balls are on the table and what shows over it.
var _mode := FREE_TABLE
## The screen showing, as screen.changed logged it.
var _screen := ""
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
var _practices: Array[Dictionary] = []
## The practice shot the strip is showing: its place in _practices, or -1.
var _practice := -1
var _strip := PracticeStrip.new()
## The stand-in's stroke while its cue stands on the line, and the timer of that wait.
var _opponent := {}
var _lining_up := false
var _lineup := Timer.new()
## The harness's whole-match run: every stroke's playback is skipped and nothing waits.
var _fast := false
## The person's cue while the stand-in's stroke is on the controls, and the free table's while a
## match is on the table: each is put back as it was.
var _person_setup := {}
var _free_setup := {}
## The finished match, from match_ended until its summary is left.
var _finished := {}
var _offered: Array[String] = []
## Ball in hand: the cue ball follows a drag, as far from the finger as when it went down.
var _cue_dragging := false
var _cue_drag_offset := Vector2.ZERO
## Where the camera looks from while the cue ball is placed, and whether it is there.
var _placing_focus := Vector2.ZERO
var _placing_view := false
## The place of the ball a tapped nomination looked past on the aim line, or -1.
var _tap_over := -1
var _starting := false
var _home_clear_key: Array = []
var _home_clear_rects: Array[Rect2] = []
## A match's shot is ending because the match is being left: there is nothing to judge.
var _unjudged := false
var _harness_why := ""

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
@onready var _match: MatchController = $Match
@onready var _nomination: Nomination = $Nomination
@onready var _elevation: ElevationControl = $Hud/ElevationControl
@onready var _spin: SpinWidget = $Hud/SpinWidget
@onready var _wheel: FineTuneWheel = $Hud/FineTuneWheel
@onready var _stroke: StrokeControl = $Hud/StrokeCue
@onready var _pull: PullControl = $Hud/PullCue
@onready var _palette: EditPalette = $Hud/EditPalette
@onready var _notice: Label = $Hud/Notice
@onready var _bar: FreeTableBar = $Hud/FreeTableBar
@onready var _scoreboard: Scoreboard = $Hud/Scoreboard
@onready var _referee: RefereeLine = $Hud/RefereeLine
@onready var _choices: ChoiceStrip = $Hud/ChoiceStrip
@onready var _placer: CuePlacer = $Hud/CuePlacer
@onready var _home: HomeScreen = $Hud/HomeScreen
@onready var _match_setup: MatchSetupScreen = $Hud/MatchSetupScreen
@onready var _pause: PauseMenu = $Hud/PauseMenu
@onready var _frame_summary: FrameSummary = $Hud/FrameSummary
@onready var _match_summary: MatchSummary = $Hud/MatchSummary
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
	choose_among = func(balls: Array[Dictionary], cue_ball: int, places: Array) -> Dictionary:
		return NextTarget.choose(balls, cue_ball, "any", places)
	_popups.name = "PotPopups"
	add_child(_popups)
	# Kept out of the table's reflection probe, which would show them where they were captured.
	_set_layers(_guides, Stage.MOVING_LAYER)

	setup.strike_mode = String(Settings.get_value("strike_mode"))
	_aim.bind(setup, _gestures, _rig)
	_aim.on_ball = func() -> bool: return _guides.aims_at_ball(_table_balls(), _table_cue(), setup.aim_rad)
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
	_bar.home_pressed.connect(_go_home)
	_bar.overview_toggled.connect(_on_overview_toggled)
	_rig.overview_changed.connect(_on_overview_changed)
	_palette.add_pressed.connect(_add_ball)
	_palette.remove_pressed.connect(_remove_selected)
	Settings.changed.connect(_on_setting_changed)

	_home.play_pressed.connect(_open_match_setup)
	_home.free_table_pressed.connect(_enter_free_table)
	_home.settings_pressed.connect(_settings_panel.open)
	_match_setup.start_pressed.connect(_start_match)
	_match_setup.back_pressed.connect(_go_home)
	_match.table_changed.connect(_on_match_table_changed)
	_match.frame_changed.connect(_on_frame_changed)
	_match.calls_made.connect(_on_calls_made)
	_match.turn_started.connect(_on_turn_started)
	_match.choice_needed.connect(_on_choice_needed)
	_match.opponent_ready.connect(_on_opponent_ready)
	_match.opponent_chose.connect(_on_opponent_chose)
	_match.frame_ended.connect(_on_frame_ended)
	_match.match_ended.connect(func(match_state: Dictionary) -> void: _finished = match_state)
	_scoreboard.auto_place = false
	_scoreboard.pause_pressed.connect(_open_pause)
	_choices.chosen.connect(_on_chosen)
	_placer.moved.connect(func(xy: Vector2) -> void: _balls.place_ball(_match.cue_ball, xy))
	_placer.dropped.connect(_on_cue_dropped)
	_placer.confirmed.connect(_on_cue_confirmed)
	_placer.move_requested.connect(_on_cue_move_requested)
	_pause.resume_pressed.connect(_close_pause)
	_pause.concede_frame_confirmed.connect(_concede.bind(false))
	_pause.concede_match_confirmed.connect(_concede.bind(true))
	_pause.quit_confirmed.connect(_quit_match)
	_pause.settings_pressed.connect(_settings_panel.open)
	_frame_summary.continue_pressed.connect(_on_continue)
	_match_summary.home_pressed.connect(_on_summary_home)
	_lineup.name = "Lineup"
	_lineup.one_shot = true
	_lineup.timeout.connect(_play_opponent)
	add_child(_lineup)

	_build_selection()
	_build_notice()
	_build_break_pill()
	_build_practice()
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
	_go_home()


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
# in pull mode. F3 is the overlay's own. The history keys are the free table's; none acts under a
# screen.
func _unhandled_input(event: InputEvent) -> void:
	if not (event is InputEventKey or event is InputEventAction) or _settings_panel.visible:
		return
	if _screen != TABLE and _screen != MATCH:
		return
	var free := _mode == FREE_TABLE
	if free and event.is_action_pressed("retake", false, true):
		retake()
	elif free and event.is_action_pressed("undo", false, true):
		undo()
	elif free and event.is_action_pressed("redo", false, true):
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


## The balls on the table, the cue ball's place among them and a number that changes whenever they
## do: the match's while one is on the table, else the free table's.
func _table_balls() -> Array[Dictionary]:
	return _match.balls if _mode == MATCH else _free.balls


func _table_cue() -> int:
	return _match.cue_ball if _mode == MATCH else _free.cue_ball


# The guides tell one table from another by this number alone, and the two tables count apart: odd
# for a match, even for the free table.
func _table_version() -> int:
	return 2 * _match.version() + 1 if _mode == MATCH else 2 * _free.version()


## The person is setting up a stroke: the cue is theirs to aim and the controls are live.
func _setting_up() -> bool:
	if _playing:
		return false
	match _mode:
		FREE_TABLE:
			return not _free.edit_mode
		MATCH:
			return SETTING_UP.has(_match.strike_blocked())
	return false


## Whether a stroke struck this way may go now. In a match MatchController says; a stroke of the
## person's with a colour still to nominate tells them so.
func _may_strike(strike_mode: String) -> bool:
	match _mode:
		FREE_TABLE:
			return not _free.edit_mode
		MATCH:
			var blocked := _match.strike_blocked()
			if strike_mode == OPPONENT_STRIKE or strike_mode == AUTOPLAY_STRIKE:
				return blocked == "opponent"
			if blocked == "nominate":
				setup.stroke_samples.clear()
				setup.fine_tune_samples.clear()
				setup.wander_rad = 0.0
				_show_notice(NOMINATE_NOTICE)
				_queue_refresh()
			return blocked.is_empty()
	return false


## Logs, simulates and starts playing the shot set up; its id, or "" when none could start.
## strike_mode is what the logs call the way it was struck: stroke (Push), pull, harness, demo, or
## in a match opponent and autoplay. as_given, when not empty, is the strike to play in place of
## the setup's own; given_id the id the shot was already logged under (the stand-in's).
func _strike(strike_mode: String, as_given: Dictionary = {}, given_id: String = "") -> String:
	if _playing or not _may_strike(strike_mode):
		return ""
	var shot_id := given_id if not given_id.is_empty() else Physics.new_shot_id()
	Params.commit_pending(shot_id)
	_update_elevation_min()
	Log.info("shot", "shot.input", _shot_input_fields(shot_id, strike_mode))
	var strike := setup.to_strike()
	if not as_given.is_empty():
		# The setup holds the aim before the allowance for side; the log gets the difference.
		Log.info("shot", "shot.side_allowed", {"shot_id": shot_id,
				"turned_rad": angle_difference(float(strike["aim_rad"]), float(as_given["aim_rad"])), "target": "",
				"residual_rad": 0.0, "reason": "played as given", "sims": 0, "us": 0})
		strike = as_given.duplicate()
	# A harness shot plays exactly as given.
	elif strike_mode != HARNESS_STRIKE:
		strike = _allowing_for_side(strike, shot_id)
	var record := Physics.simulate(_table_balls(), _table_cue(), strike,
			{"shot_id": shot_id, "mode": _mode, "strike_mode": strike_mode})
	if record == null:
		# The binding has logged shot.rejected with the reason.
		setup.stroke_samples.clear()
		setup.fine_tune_samples.clear()
		setup.wander_rad = 0.0
		_show_notice("That shot couldn't be played")
		_queue_refresh()
		return ""
	if _mode == MATCH:
		_match.shot_started(shot_id)
		# The cue ball is no longer in hand.
		if _placer.covered_rect().has_area():
			_placer.end()
			_layout()
	else:
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
	_plan = ShotCycle.plan(playback, _table_balls(), _table_cue(), bool(Settings.get_value("pocket_camera")), _pockets)
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
	var solved := SpinAim.solve(Physics.predict, _table_balls(), _table_cue(), strike, setup.elevation_min_rad)
	if int(solved["sims"]) > 0 and not shot_id.is_empty():
		Log.info("shot", "shot.side_allowed", {"shot_id": shot_id, "turned_rad": solved["turned_rad"],
				"target": solved["target"], "residual_rad": solved["residual_rad"], "reason": solved["reason"],
				"sims": solved["sims"], "us": Time.get_ticks_usec() - began})
	var allowed := strike.duplicate()
	allowed["aim_rad"] = solved["aim_rad"]
	return allowed


func _shot_input_fields(shot_id: String, strike_mode: String) -> Dictionary:
	var fields := _match_fields({"shot_id": shot_id, "mode": _mode, "guide_level": _guide_level(),
			"strike_mode": strike_mode})
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


## An object ball dropped: its value floats up out of the pocket and the break counts it. Not in a
## match, where only the referee knows what a pot is worth: the scoreboard has it once the stroke
## is judged.
func _on_pot(event: Dictionary) -> void:
	var index := int(event.get("a", -1))
	if index == _table_cue() or _mode == MATCH:
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
## result to the free table, or in a match to the referee, turns the aim to the next ball and
## swoops back behind the cue ball.
func _settle() -> void:
	if _balls.is_dropping() or not _cycle.settle_ready(_cycle_t):
		set_process(true)
		return
	set_process(false)
	_settling = false
	if _mode == MATCH:
		_settle_match()
		return
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


## A match's stroke has stopped: the referee judges it, and what that brings (the next turn, a
## choice, the frame's end) is on screen as the camera swoops back. The points and the break in
## shot.result are the referee's.
func _settle_match() -> void:
	var record := _record
	_record = null
	# Before the judgement: the turn it starts takes the person's own cue back off the controls.
	setup.after_shot(bool(Settings.get_value("reset_spin_after_shot")))
	var result := _match_fields({"shot_id": _shot_id})
	result.merge(_cycle.end_shot())
	# The counter is the free table's; a match's break is on the scoreboard.
	_cycle.reset_break()
	if _unjudged:
		_set_playing(false)
		return
	var judgement := _match.shot_finished(record)
	result["points"] = int(judgement.get("points", 0))
	result["break"] = int(_match.frame().get("break_points", 0))
	result.merge(_turn_to_next())
	Log.info("shot", "shot.result", result)
	_set_playing(false)
	_rig.reset_view()
	_sync_match_ui()
	_preselect()
	_refresh()
	# A frame that ended keeps the whole table behind its summary.
	if not _frame_summary.visible:
		_rig.hold(false)


## Turns the aim to the next ball (spec 8.5, the return) unless "aim_next_ball" is off; shot.result's
## next_ fields. In a match the next ball is one of the balls on.
func _turn_to_next() -> Dictionary:
	if not bool(Settings.get_value("aim_next_ball")):
		return {"next_ball": "", "next_reason": "setting off"}
	var prefer := "any" if _mode == MATCH else _cycle.prefer(_free.balls)
	var chosen: Variant
	if _mode == MATCH:
		chosen = choose_among.call(_match.balls, _match.cue_ball, _on_places())
	else:
		chosen = choose_next.call(_free.balls, _free.cue_ball, prefer)
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
	_show_controls()
	_mark_nominated()
	for id: String in ["edit", "rack", "empty", "practice", "overview", "home"]:
		_bar.button(id).disabled = on
	_strip.set_busy(on)
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


func _on_setup_changed(what: StringName) -> void:
	if what == &"aim":
		_preselect()
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
	var balls := _table_balls()
	var cue_ball := _table_cue()
	if cue_ball < 0 or cue_ball >= balls.size():
		_refresh_queued = false
		return
	var cue_xy := _cue_xy()
	if not _playing:
		_update_elevation_min()
	# A cue shows for the person setting up a stroke and for the opponent's on its line.
	var cued := _setting_up() or _lining_up
	_rig.set_cue_elevation(setup.effective_elevation() if cued or _playing else 0.0)
	if _mode == MATCH and _placer.is_placing():
		# Still while the cue ball is dragged about under it.
		_rig.follow(_placing_focus, 0.0)
	else:
		_rig.follow(cue_xy, setup.aim_rad)
	if not _playing:
		if not cued:
			_cue.show_cue(false)
			_guides.hide_all()
		else:
			_cue.show_cue(true)
			_cue.set_pose(cue_xy, setup.aim_rad, setup.effective_elevation(), Vector2(setup.tip_x, setup.tip_y),
					_backswing_m)
			_cue.clear_of(_rig.camera().global_position)
			if _lining_up:
				_guides.hide_all()
			else:
				_guides.show_for(balls, cue_ball, setup, _guide_level(), _table_version())
	_mark_nominated()
	# Last, so the elevation minimum's own change signal doesn't queue another pass.
	_refresh_queued = false


func _update_elevation_min() -> void:
	setup.set_elevation_min(Physics.min_cue_elevation(_table_balls(), _table_cue(), setup.to_strike()))


func _cue_xy() -> Vector2:
	var cue: Dictionary = _table_balls()[_table_cue()]
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
	_aim.enabled = _setting_up() and not _rig.is_overview()


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
	if _mode != MATCH:
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
		# Redo coming or going changes the bar's width, which the practice strip sits against.
		_layout()


func _history(action: String) -> void:
	if _playing or _mode != FREE_TABLE:
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
	if not _playing and _mode == FREE_TABLE:
		end_practice()
		_free.load_setup(setup_name)


## Puts practice shot `index` (its place in the menu) on the table with the aim on its line and
## shows its strip, entering the free table from Home. False when a shot is playing, a match is on
## or the position can't be set up.
func start_practice(index: int) -> bool:
	if _playing or _mode == MATCH or index < 0 or index >= _practices.size():
		return false
	_enter_free_table()
	if not _set_up_practice(_practices[index]):
		return false
	_practice = index
	var shot := _practices[index]
	_strip.set_shot(shot["title"], shot["hint"], Practice.cue_text(shot["strike"],
			Physics.param("max_cue_speed_m_s", 10.0), Physics.param("miscue_limit", 0.55)))
	_strip.visible = true
	_layout()
	return true


## The practice position back on the table and the aim on its line; spin and power stay.
func practice_again() -> bool:
	if _playing or _practice < 0 or _mode != FREE_TABLE:
		return false
	return _set_up_practice(_practices[_practice])


## Plays the practice shot as filmed; its shot id, or "" when it couldn't start. The setup is left
## as the player would set it for the same shot, so a retake hands them the filmed cue.
func show_practice() -> String:
	if not practice_again():
		return ""
	var strike: Dictionary = _practices[_practice]["strike"]
	var speed := float(strike["speed_m_s"])
	setup.set_tip(float(strike["tip_x"]), float(strike["tip_y"]))
	setup.set_elevation(float(strike["elevation_rad"]))
	setup.set_speed(speed, PullControl.power_for_speed(speed, Physics.param("max_cue_speed_m_s", 10.0)))
	setup.wander_rad = 0.0
	return _strike(DEMO_STRIKE, strike)


func end_practice() -> void:
	if _practice < 0:
		return
	_practice = -1
	_strip.visible = false
	_layout()


func _set_up_practice(shot: Dictionary) -> bool:
	if _free.edit_mode:
		_free.set_edit_mode(false)
	var balls: Array[Dictionary] = shot["balls"]
	var cue_ball := int(shot["cue_ball"])
	if not (_free.cue_ball == cue_ball and Practice.same_balls(_free.balls, balls)) 			and not _free.load_position(String(shot["position"]), balls, cue_ball):
		# FreeTable has logged why.
		_show_notice("That practice shot couldn't be set up")
		return false
	var strike: Dictionary = shot["strike"]
	var aim := float(strike["aim_rad"])
	if bool(Settings.get_value("allow_for_side")):
		# What the player points at: the allowance for side turns it into the filmed aim.
		aim = Practice.player_aim(Physics.predict, _free.balls, _free.cue_ball, strike,
				Physics.min_cue_elevation(_free.balls, _free.cue_ball, strike))
	setup.set_aim(aim)
	return true


func _on_edit_mode_changed(on: bool) -> void:
	_abort_drag()
	_reset_break()
	_bar.set_edit_mode(on)
	_layout()
	_update_aim_enabled()
	_selected = -1
	_spin.cancel_press("edit mode")
	_show_controls()
	_update_palette()
	_place_selection()
	_queue_refresh()


## The shot controls for the strike mode, or the edit palette in edit mode. In a match they show
## while a stroke is to be played or is playing, and are the person's only while the stroke is:
## through the opponent's turn they stay dimmed and take no touch, as while any shot plays.
func _show_controls() -> void:
	var editing := _mode == FREE_TABLE and _free.edit_mode
	var shown := false
	match _mode:
		FREE_TABLE:
			shown = not editing
		MATCH:
			shown = _playing or STROKE_ON.has(_match.strike_blocked())
	var pull := setup.strike_mode == ShotSetup.PULL
	_spin.visible = shown
	_elevation.visible = shown
	_wheel.visible = shown and bool(Settings.get_value("fine_tune_wheel"))
	_stroke.visible = shown and not pull
	_pull.visible = shown and pull
	_palette.visible = editing
	var live := _setting_up()
	var filter := Control.MOUSE_FILTER_STOP if live else Control.MOUSE_FILTER_IGNORE
	var alpha := 1.0 if live else PLAYING_ALPHA
	for control: Control in [_spin, _elevation, _wheel, _stroke, _pull]:
		if control.mouse_filter != filter:
			control.mouse_filter = filter
		if control.modulate.a != alpha:
			control.modulate.a = alpha


func _on_tapped(at: Vector2) -> void:
	if _playing:
		skip_shot()
	elif _mode == FREE_TABLE and _free.edit_mode:
		_select(_balls.ball_at_screen(_rig.camera(), at))
	elif _mode == MATCH and _setting_up():
		_tap_to_nominate(at)


func _on_drag_started(at: Vector2) -> void:
	if _mode == MATCH and _placer.is_placing() and not _playing:
		var down: Variant = TableSpace.screen_to_core(_rig.camera(), at, _balls.radius)
		if down != null:
			_cue_dragging = true
			_cue_drag_offset = _cue_xy() - (down as Vector2)
		return
	if not _free.edit_mode or _playing:
		return
	_dragged = _balls.ball_at_screen(_rig.camera(), at)
	if _dragged >= 0:
		_select(_dragged)


func _on_drag_moved(at: Vector2, relative: Vector2) -> void:
	if _cue_dragging:
		var under: Variant = TableSpace.screen_to_core(_rig.camera(), at, _balls.radius)
		if under != null:
			_placer.drag((under as Vector2) + _cue_drag_offset)
		return
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
	if _cue_dragging:
		_cue_dragging = false
		var lifted: Variant = TableSpace.screen_to_core(_rig.camera(), at, _balls.radius)
		var cue := _balls.get_child(_match.cue_ball) as Node3D
		# A finger lifted off the table leaves the cue ball where the drag last showed it.
		_placer.drop((lifted as Vector2) + _cue_drag_offset if lifted != null else TableSpace.godot_to_core(cue.position))
		return
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


func _build_practice() -> void:
	_strip.name = "PracticeStrip"
	$Hud.add_child(_strip)
	# Under the bar, so its open menu covers the strip.
	$Hud.move_child(_strip, _bar.get_index())
	_strip.show_pressed.connect(show_practice)
	_strip.again_pressed.connect(practice_again)
	_strip.close_pressed.connect(end_practice)
	_practices = Practice.load_all()
	var titles: Array[String] = []
	for shot in _practices:
		titles.append(String(shot["title"]))
	_bar.set_practices(titles)
	_bar.practice_chosen.connect(start_practice)


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


func _hide_notice() -> void:
	_notice_timer.stop()
	_notice.visible = false


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
	# A match's pieces: the scoreboard at the top centre, the referee's line under it, the choice
	# strip and the cue placer along the bottom between the spin ball and the power cue.
	_scoreboard.place_in(safe)
	var under := _scoreboard.covered_rect().end.y + HudScript.LINE_GAP
	_referee.place_in(Rect2(safe.position.x, under, safe.size.x, HudScript.LINE_HEIGHT))
	_choices.place_in(rects["centre"])
	_placer.place_in(rects["centre"])
	# What the full-table view keeps the pockets clear of: the controls that show while a shot
	# plays. A match has its scoreboard where the free table has its bar, with the referee's line
	# under it whether one shows or not, so the view is the same shot after shot. Home has its name.
	var controls: Array[Rect2] = []
	if _mode == HOME:
		controls = _home_clear(canvas)
	else:
		for key: String in ["bar", "spin", "elevation", "cue", "wheel"]:
			if key == "bar" and _mode == MATCH:
				var board := _scoreboard.covered_rect()
				board.size.y += HudScript.LINE_GAP + HudScript.LINE_HEIGHT
				controls.append(board)
			elif key != "wheel" or with_wheel:
				controls.append(rects[key])
	var notice_y := safe.position.y + HudScript.EDGE
	if _mode == MATCH:
		notice_y = under + HudScript.LINE_HEIGHT + HudScript.GAP
	if _strip.visible:
		var bar: Rect2 = rects["bar"]
		var left := bar.end.x + HudScript.GAP
		_strip.place_in(Rect2(left, bar.position.y, (rects["cue"] as Rect2).position.x - HudScript.GAP - left, bar.size.y))
		controls.append(Rect2(_strip.position, _strip.size))
		notice_y = _strip.position.y + _strip.size.y + HudScript.GAP
	_rig.keep_clear(controls)
	var notice_at := Vector2(safe.get_center().x - _notice.size.x / 2.0, notice_y).round()
	if _notice.position != notice_at:
		_notice.position = notice_at


## What Home's full-table view is kept clear of, so the whole table stands between Home's name
## and its buttons. The rig fits the table to rectangles the pockets must stay out of and has
## none for the bottom of the screen, so room is taken from both sides, in steps, until the
## table's near end is above the buttons. Worked out again only when the screen's layout changes.
func _home_clear(canvas: Vector2) -> Array[Rect2]:
	var title := _home.get_node("Title") as Control
	var row := _home.get_node("Buttons") as Control
	var key := [canvas, title.position, title.size, row.position, _table.bounds()]
	if key == _home_clear_key:
		return _home_clear_rects
	var band := Rect2(0.0, 0.0, canvas.x, title.position.y + title.size.y)
	var rects: Array[Rect2] = [band]
	var corners: Array[Vector3] = []
	var bounds := _table.bounds()
	for x: float in [bounds.position.x, bounds.end.x]:
		for z: float in [bounds.position.z, bounds.end.z]:
			corners.append(Vector3(x, bounds.end.y, z))
	var margin := 0.0
	while margin < canvas.x * HOME_MARGIN_MAX:
		if margin > 0.0:
			rects = [band, Rect2(0.0, 0.0, margin, canvas.y), Rect2(canvas.x - margin, 0.0, margin, canvas.y)]
		_rig.keep_clear(rects)
		var pose := _rig.broadcast_pose(CameraRig.FULL_TABLE_YAW)
		var lowest := 0.0
		for corner in corners:
			lowest = maxf(lowest, _rig.project(pose, corner, CameraRig.BROADCAST_FOV_DEG).y)
		if lowest <= row.position.y - HudScript.GAP:
			break
		margin += HOME_MARGIN_STEP
	_home_clear_key = key
	_home_clear_rects = rects
	return rects


static func _rect_list(rect: Rect2) -> Array:
	return [rect.position.x, rect.position.y, rect.size.x, rect.size.y]


# Writes only what changed, so the safe-area poll never makes low-processor mode redraw.
static func _put(control: Control, rect: Rect2) -> void:
	if control.position != rect.position:
		control.position = rect.position
	if control.size != rect.size:
		control.size = rect.size


func _set_screen(to: String) -> void:
	if to == _screen:
		return
	Log.info("ui", "screen.changed", {"from": _screen, "to": to})
	_screen = to


## The screen a mode shows with nothing over it.
func _mode_screen() -> String:
	match _mode:
		FREE_TABLE:
			return TABLE
		MATCH:
			return MATCH
	return HOME


## Shows what the mode has on the table and hides the rest.
func _apply_mode() -> void:
	var free := _mode == FREE_TABLE
	_bar.visible = free
	_strip.visible = free and _practice >= 0
	_scoreboard.visible = _mode == MATCH
	# Behind Home nothing is tappable.
	_gestures.set_enabled(_mode != HOME)
	_show_controls()
	_update_aim_enabled()
	_place_selection()
	_layout()
	_refresh()


## Home (spec 11.1): the free table's balls from the full-table view, with nothing over them but
## the Home screen.
func _go_home() -> void:
	_end_shot_now()
	_abort_drag()
	if _free.edit_mode:
		_free.set_edit_mode(false)
	_bar.close_menu()
	_match_setup.close()
	_mode = HOME
	_close_match()
	_reset_break()
	_audio.stop_all()
	_balls.sync(_free.balls)
	_home.open()
	_set_screen(HOME)
	_apply_mode()
	_rig.hold(true)
	_rig.show_broadcast()


## The free table as it was left (spec 12.1); the camera comes down behind the cue ball.
func _enter_free_table() -> void:
	if _mode == FREE_TABLE:
		return
	if _mode == MATCH:
		_leave_match()
	_close_match()
	_home.close()
	_match_setup.close()
	_mode = FREE_TABLE
	_set_screen(TABLE)
	_apply_mode()
	_rig.reset_view()
	_refresh()
	_rig.hold(false)


func _open_match_setup() -> void:
	if _mode == MATCH:
		_leave_match()
	elif _mode == FREE_TABLE:
		_go_home()
	_close_match()
	_home.close()
	_match_setup.open()
	_set_screen(MATCH_SETUP)


## Starts a match (MatchController.start's config) and puts it on the table; autoplay and fast are
## the harness's. False, with the person told and the screen as it was, when it can't start.
func _start_match(config: Dictionary, autoplay: bool = false, fast: bool = false) -> bool:
	var from := _mode
	_end_shot_now()
	_cancel_lineup()
	_starting = true
	# One still on is left here, so that autoplay is not handed its turn.
	_match.abandon()
	_take_back_cue()
	_referee.clear()
	_choices.dismiss()
	if _free_setup.is_empty():
		_free_setup = setup.snapshot()
		_free_setup.erase("strike_mode")
		_free_setup["pace"] = setup.last_stroke_speed_m_s
	_match.autoplay = autoplay
	_match.pace = 0.0 if fast else 1.0
	_fast = fast
	# The match's first signals find its balls on the table.
	_mode = MATCH
	var started := _match.start(config)
	_starting = false
	if not started:
		# MatchController has logged why.
		_mode = from
		if from == MATCH:
			_go_home()
		else:
			_close_match()
		_show_notice(NO_MATCH_NOTICE)
		return false
	if _free.edit_mode:
		_free.set_edit_mode(false)
	_bar.close_menu()
	_reset_break()
	_home.close()
	_match_setup.close()
	_pause.close()
	_frame_summary.close()
	_match_summary.close()
	_hide_notice()
	_finished = {}
	_set_screen(MATCH)
	_apply_mode()
	_rig.reset_view()
	_sync_match_ui()
	_preselect()
	_rig.hold(false)
	return true


## Leaves the match, finished or not (spec 11.2: nothing is kept), for Home.
func _leave_match() -> void:
	_end_shot_now()
	# With a match to leave, its &"ended" brings Home.
	_match.abandon()
	if _mode == MATCH:
		_go_home()
	else:
		# A summary or the pause menu the harness showed, with no match behind it.
		_close_match()
		_set_screen(_mode_screen())


## Takes a match's pieces off the screen and, once the match is off the table, puts the free
## table's cue back on the controls.
func _close_match() -> void:
	_cancel_lineup()
	_hold_match(false)
	_hide_notice()
	_pause.close()
	_frame_summary.close()
	_match_summary.close()
	_referee.clear()
	_choices.dismiss()
	if _placer.covered_rect().has_area():
		_placer.end()
	_placing_view = false
	_nomination.mark(null)
	_cue_dragging = false
	_tap_over = -1
	_finished = {}
	_fast = false
	_match.autoplay = false
	_match.pace = 1.0
	_person_setup = {}
	if not _free_setup.is_empty() and _mode != MATCH:
		setup.last_stroke_speed_m_s = float(_free_setup["pace"])
		setup.restore(_free_setup)
		_free_setup = {}


## Ends a shot that is playing, at once. The free table keeps its result; a match's is dropped
## unjudged, for the match is being left.
func _end_shot_now() -> void:
	if not _playing:
		return
	_unjudged = _mode == MATCH
	skip_shot()
	_unjudged = false


## A match's lines carry its id and the frame's number.
func _match_fields(fields: Dictionary) -> Dictionary:
	if _mode == MATCH:
		fields["match_id"] = _match.match_id()
		fields["frame"] = _match.frame_number()
	return fields


## A list of the frame view (serialize.hpp: "on_places", "options"); empty when it has none.
func _view_list(key: String) -> Array:
	var view: Variant = _match.frame().get("view")
	var list: Variant = (view as Dictionary).get(key) if view is Dictionary else null
	return list if list is Array else []


## The places in the match's balls of the balls on.
func _on_places() -> Array:
	return _view_list("on_places")


## The decisions the engine offers the person now.
func _person_options() -> Array:
	var all := _view_list("options")
	return all[PERSON] if PERSON < all.size() and all[PERSON] is Array else []


## Puts on screen what the match's state asks for: the cue placer, the choice strip, the shot
## controls, who may aim. Called after anything that changes that state.
func _sync_match_ui() -> void:
	if _mode != MATCH:
		return
	var in_hand := _match.person_to_play() and bool(_match.frame().get("cue_in_hand", false))
	if _match.placing() and not _playing:
		if not _placer.is_placing():
			_placer.begin(_match.nearest_cue_placement)
	elif _placer.covered_rect().has_area() and (_placer.is_placing() or not in_hand):
		# Confirmed by other hands, or no longer in hand. After Confirm its Move button stays.
		_placer.end()
	if not _placer.is_placing():
		_cue_dragging = false
	elif not _placing_view:
		_rig.reset_view()
	_placing_view = _placer.is_placing()
	if _choices.is_offering() and _match.strike_blocked() != "choice":
		_choices.dismiss()
	_show_controls()
	_update_aim_enabled()
	_layout()
	if _placing_view:
		_find_placing_view()
	_refresh()


func _on_match_table_changed(balls: Array[Dictionary], _cue_ball: int, reason: StringName) -> void:
	if reason == &"ended":
		if _mode == MATCH and not _starting:
			_go_home()
		return
	if _mode != MATCH:
		return
	_cue_dragging = false
	_tap_over = -1
	_balls.sync(balls)
	_queue_refresh()


func _on_frame_changed(frame: Dictionary) -> void:
	_scoreboard.show_frame(frame, _match.match_state(), PERSON)


func _on_calls_made(calls: Array, _shot_id: String) -> void:
	_referee.announce(calls, PERSON)


func _on_turn_started(_player: int, person: bool) -> void:
	_tap_over = -1
	if person:
		_take_back_cue()
		# After a stroke the settle turns the aim, to log where; any other turn starts here.
		if not _playing and not _match.placing():
			_turn_to_next()
	_sync_match_ui()
	_preselect()


func _on_choice_needed(options: Array[String]) -> void:
	_take_back_cue()
	_offered = options
	_choices.offer(options, _match.frame())
	_sync_match_ui()


func _on_chosen(kind: String) -> void:
	if _match.choose(kind):
		return
	# MatchController has logged why; the question stands.
	_show_notice("That choice couldn't be made")
	_choices.offer(_offered, _match.frame())
	_layout()


func _on_opponent_chose(kind: String) -> void:
	var words := String(OPPONENT_CHOSE.get(kind, ""))
	if not words.is_empty():
		_show_notice(words)


## The opponent's stroke (spec 10.5): its cue ball placed and its colour declared as it says, its
## strike put on the controls so the cue shows on the line with the camera behind it, and after a
## moment struck exactly as given under the id the stand-in logged it with.
func _on_opponent_ready(shot: Dictionary) -> void:
	var strike: Variant = shot.get("strike")
	var problem := ""
	if _mode != MATCH or _playing:
		problem = "it came with %s" % ("a shot playing (%s)" % _shot_id if _playing else "no match on the table")
	elif not strike is Dictionary:
		problem = "it has no strike"
	else:
		for key: String in ["aim_rad", "tip_x", "tip_y", "elevation_rad", "speed_m_s"]:
			var value: Variant = (strike as Dictionary).get(key)
			if not (value is float or value is int) or not is_finite(float(value)):
				problem = "its %s is %s" % [key, JSON.stringify(value)]
				break
	if not problem.is_empty():
		Log.error("app", "app.error", _match_fields({"kind": "message",
				"message": "main: the stand-in's stroke can't be played: %s; the match waits for it: %s" % [problem,
				JSON.stringify(shot)]}))
		return
	if bool(shot.get("places_cue", false)):
		var cue: Variant = shot.get("cue")
		if cue is Array and (cue as Array).size() == 2 and (cue[0] is float or cue[0] is int) and (cue[1] is float or cue[1] is int):
			# MatchController logs a refusal; the stroke is then played from where the cue ball is.
			_match.place_cue(Vector2(float(cue[0]), float(cue[1])))
		else:
			Log.warn("app", "app.error", _match_fields({"kind": "message",
					"message": "main: the stand-in places the cue ball at %s, not [x, y]; played from where it is (shot %s)"
					% [JSON.stringify(cue), shot.get("shot_id", "")]}))
	var nominated := str(shot.get("nominated", ""))
	if not nominated.is_empty() and not _match.nominate(nominated, OPPONENT_STRIKE):
		Log.warn("app", "app.error", _match_fields({"kind": "message",
				"message": "main: the stand-in nominated \"%s\", not one of %s; played with nothing declared (shot %s)"
				% [nominated, _match.nominations(), shot.get("shot_id", "")]}))
	if _person_setup.is_empty():
		_person_setup = setup.snapshot()
		_person_setup.erase("strike_mode")
		_person_setup["pace"] = setup.last_stroke_speed_m_s
	_opponent = shot
	_lining_up = true
	_set_up(strike)
	_sync_match_ui()
	var wait := 0.0 if _fast else LINEUP_SECONDS * _match.pace
	if wait > 0.0:
		_lineup.start(wait)
	else:
		_play_opponent()


func _play_opponent() -> void:
	var shot := _opponent
	_cancel_lineup()
	if shot.is_empty() or _mode != MATCH:
		return
	var by := AUTOPLAY_STRIKE if int(shot.get("player", MatchController.OPPONENT)) == PERSON else OPPONENT_STRIKE
	var shot_id := _strike(by, shot["strike"], str(shot.get("shot_id", "")))
	if shot_id.is_empty():
		Log.error("app", "app.error", _match_fields({"kind": "message",
				"message": "main: the stand-in's stroke was not struck (strike_blocked \"%s\"; shot.rejected says if the physics refused it); the match waits for it: %s"
				% [_match.strike_blocked(), JSON.stringify(shot)]}))
		_sync_match_ui()
	elif _fast:
		skip_shot()


func _cancel_lineup() -> void:
	_lineup.stop()
	_opponent = {}
	_lining_up = false


## The person's own cue back on the controls, as it stood before the stand-in's stroke went on them.
func _take_back_cue() -> void:
	if _person_setup.is_empty():
		return
	var mine := _person_setup
	_person_setup = {}
	setup.last_stroke_speed_m_s = float(mine["pace"])
	setup.restore(mine)


## Nomination by the aim (spec 8.7): the first ball on the aim line is declared when it is one the
## striker may nominate; with a free ball called, a ball on there withdraws the declaration, since
## the ball on is then played as itself. A line on nothing eligible leaves the declaration alone.
func _preselect() -> void:
	if _mode != MATCH or _playing or not _match.person_to_play() or _match.placing():
		return
	var eligible := _match.nominations()
	if eligible.is_empty():
		return
	var first := Nomination.first_on_line(_match.balls, _match.cue_ball, setup.aim_rad, _balls.radius)
	# A tap chose a ball beyond this one: it stands while the line still meets this one first.
	if first == _tap_over:
		return
	_tap_over = -1
	var ball := Nomination.tapped(_match.balls, first, eligible)
	if not ball.is_empty():
		_match.nominate(ball, "aim")
	elif first >= 0 and bool(_match.frame().get("free_ball", false)) and _is_on(first):
		_match.nominate_none("aim")
	_mark_nominated()


func _is_on(place: int) -> bool:
	for on: Variant in _on_places():
		if (on is int or on is float) and int(on) == place:
			return true
	return false


## A tap on a ball the striker may nominate declares it and turns the aim to it (spec 8.7).
func _tap_to_nominate(at: Vector2) -> void:
	var place := _balls.ball_at_screen(_rig.camera(), at)
	var ball := Nomination.tapped(_match.balls, place, _match.nominations())
	if ball.is_empty():
		return
	var target: Dictionary = _match.balls[place]
	var aim := (Vector2(float(target["x"]), float(target["y"])) - _cue_xy()).angle()
	var first := Nomination.first_on_line(_match.balls, _match.cue_ball, aim, _balls.radius)
	_tap_over = first if first != place else -1
	_match.nominate(ball, "tap")
	setup.set_aim(aim)
	_mark_nominated()


## The ring round the declared ball (spec 8.7), the opponent's too; off while a stroke plays, when
## the ball leaves it.
func _mark_nominated() -> void:
	var at: Variant = null
	if _mode == MATCH and not _playing:
		var place := Nomination.place_of(_match.balls, _match.nominated())
		if place >= 0:
			at = Vector2(float(_match.balls[place]["x"]), float(_match.balls[place]["y"]))
	_nomination.mark(at)


## Ball in hand (spec 8.6): where the camera looks from while the cue ball is placed. Up the table
## from behind the D, back until the whole D shows above the placer.
func _find_placing_view() -> void:
	var dims := OfsTable.dimensions()
	var baulk_x := float(dims.get("baulk_line_x", -1.0475))
	var back := TableSpace.core_to_godot(baulk_x - float(dims.get("d_radius", 0.292)) - _balls.radius, 0.0)
	var top := _placer.covered_rect().position.y - HudScript.GAP
	var focus := Vector2(baulk_x, 0.0)
	for step in PLACING_STEPS:
		if _rig.project(_rig.pose_for(focus, 0.0), back).y <= top:
			break
		focus.x -= PLACING_STEP_M
	_placing_focus = focus


func _on_cue_dropped(xy: Vector2) -> void:
	# MatchController logs a refusal; the cue ball goes back where the referee has it.
	if not _match.place_cue(xy):
		_balls.sync(_match.balls)
		_show_notice("The cue ball can't go there")


func _on_cue_confirmed() -> void:
	_match.confirm_cue()
	_turn_to_next()
	_sync_match_ui()
	_preselect()


func _on_cue_move_requested() -> void:
	_match.move_cue_again()
	_sync_match_ui()


func _open_pause() -> void:
	if _screen != MATCH:
		return
	_hide_notice()
	_pause.open(_person_options().has("concede_frame") and not _playing)
	_hold_match(true)
	_set_screen(PAUSE)


func _close_pause() -> void:
	_pause.close()
	_hold_match(false)
	_set_screen(_mode_screen())


## While the pause menu is up the opponent waits: neither its thinking nor its cue's stand on the
## line runs on behind the menu.
func _hold_match(on: bool) -> void:
	_match.process_mode = Node.PROCESS_MODE_DISABLED if on else Node.PROCESS_MODE_INHERIT
	_lineup.paused = on


func _concede(whole_match: bool) -> void:
	# A concession made ends the frame, whose summary takes the menu's place.
	var conceded := _match.concede_match() if whole_match else _match.concede_frame()
	if not conceded:
		# MatchController has logged why.
		_show_notice("That can't be conceded now")


func _quit_match() -> void:
	_pause.close()
	_leave_match()


func _on_frame_ended(summary: Dictionary, match_state: Dictionary) -> void:
	_cancel_lineup()
	_pause.close()
	_hold_match(false)
	# What was said of the frame's last moments has no place round its summary, which says how
	# the frame ended itself.
	_hide_notice()
	_referee.clear()
	_frame_summary.show_summary(summary, match_state, PERSON)
	_set_screen(FRAME_SUMMARY)
	# The whole table stands behind the summary until the next frame is racked.
	_rig.hold(true)
	_rig.show_broadcast()
	_sync_match_ui()


## The frame summary's Continue: the next frame, or after the last one the match's summary.
func _on_continue() -> void:
	_frame_summary.close()
	if _mode != MATCH:
		# Shown by the harness, with no match behind it.
		_set_screen(_mode_screen())
	elif not _finished.is_empty():
		_match_summary.show_summary(_finished, PERSON)
		_set_screen(MATCH_SUMMARY)
	elif _match.next_frame():
		_referee.clear()
		_set_screen(MATCH)
		_rig.reset_view()
		_refresh()
		_rig.hold(false)
	else:
		# MatchController has logged why. With no frame to play, the match can only be left.
		Log.warn("app", "app.error", {"kind": "message", "message":
				"main: no next frame after frame %d of match %s (waiting \"%s\"); the match is left"
				% [_match.frame_number(), _match.match_id(), _match.waiting()]})
		_leave_match()
		_show_notice("The next frame couldn't be started")


func _on_summary_home() -> void:
	_match_summary.close()
	_leave_match()


## Harness hook; debug builds only, called through get_tree().current_scene. The free table's:
## from Home it enters the free table, and during a match it is refused.
func harness_load_position(balls: Array, cue_ball: int) -> bool:
	if not _harness_free_table("load_position"):
		return false
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


## Plays a shot with exact parameters (a strike Dictionary) on the free table, entered from Home;
## its shot id, or "" when refused.
func harness_play_shot(strike: Dictionary) -> String:
	if not _harness_free_table("play_shot"):
		return ""
	if _playing or _free.edit_mode:
		_harness_refused("play_shot", "a shot is playing (%s)" % _shot_id if _playing else "edit mode is on")
		return ""
	_warn_out_of_range(strike)
	_set_up(strike)
	return _strike(HARNESS_STRIKE)


func _harness_free_table(command: String) -> bool:
	if _mode == MATCH:
		_harness_refused(command, "a match is on the table (%s); open_screen leaves it" % _match.match_id())
		return false
	_enter_free_table()
	return true


## Puts a strike on the controls as it is, with no wander.
func _set_up(strike: Dictionary) -> void:
	var speed := float(strike["speed_m_s"])
	setup.set_aim(float(strike["aim_rad"]))
	setup.set_tip(float(strike["tip_x"]), float(strike["tip_y"]))
	setup.set_elevation(float(strike["elevation_rad"]))
	setup.set_speed(speed, PullControl.power_for_speed(speed, Physics.param("max_cue_speed_m_s", 10.0)))
	setup.wander_rad = 0.0


## A strike the controls couldn't set still plays as given, with a warning.
func _warn_out_of_range(strike: Dictionary, command: String = "play_shot") -> void:
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
		Log.warn("harness", "harness.command", {"command": command, "args": [strike], "ok": true,
				"result": {"warnings": problems}})


## True while a shot is playing, a ball is still dropping into a pocket, or the shot cycle's camera
## has yet to hold the full-table view (spec 8.5).
func harness_busy() -> bool:
	return _playing


## True while the camera is on its way somewhere: down from Home, back from a shot.
func harness_settling() -> bool:
	return _rig.is_easing()


## Why a harness hook just refused, for the command's answer; "" once read.
func harness_refusal() -> String:
	var why := _harness_why
	_harness_why = ""
	return why


## Opens "home", "match_setup" or "table"; a match on the table is left for it. False for any
## other name.
func harness_open_screen(screen: String) -> bool:
	match screen:
		HOME:
			if _mode == MATCH:
				_leave_match()
			else:
				_go_home()
		MATCH_SETUP:
			_open_match_setup()
		TABLE:
			_enter_free_table()
		_:
			_harness_refused("open_screen", "no screen \"%s\" to open; known: home, match_setup, table" % screen)
			return false
	return true


## Shows "pause", "frame_summary" or "match_summary" over whatever is on the table, for pictures:
## the summaries filled from match_state (serialize.hpp's match). No match need be behind them;
## their buttons close them.
func harness_demo_screen(screen: String, match_state: Dictionary) -> bool:
	match screen:
		PAUSE:
			_pause.open(true)
		FRAME_SUMMARY:
			var history: Variant = match_state.get("history")
			var last: Variant = (history as Array).back() if history is Array and not (history as Array).is_empty() else null
			_frame_summary.show_summary(last if last is Dictionary else {}, match_state, PERSON)
		MATCH_SUMMARY:
			_match_summary.show_summary(match_state, PERSON)
		_:
			_harness_refused("demo screen", "no demo of \"%s\"; known: pause, frame_summary, match_summary" % screen)
			return false
	_set_screen(screen)
	return true


## Starts a match as the setup screen's Start does; autoplay has the stand-in play the person's
## part too and fast skips every stroke's playback. {"match_id"}, or {"error"}.
func harness_start_match(config: Dictionary, autoplay: bool, fast: bool) -> Dictionary:
	if not _start_match(config, autoplay, fast):
		return {"error": "the match did not start; app.error in the log says why"}
	return {"match_id": _match.match_id()}


func harness_match_state() -> Dictionary:
	return _match.harness_state()


## The person's stroke exactly as given: the in-hand cue ball put at `cue` (a Vector2, else where
## it is) and confirmed, `nominated` declared ("" leaves it to the aim line), then the strike.
## {"shot_id"}, or {"error"}.
func harness_match_shot(strike: Dictionary, nominated: String, cue: Variant) -> Dictionary:
	if _mode != MATCH or _playing or not _match.person_to_play():
		return {"error": "it is not the person's stroke (%s)" % ("no match is on the table" if _mode != MATCH
				else "strike_blocked \"%s\"" % _match.strike_blocked())}
	if cue is Vector2:
		if not bool(_match.frame().get("cue_in_hand", false)):
			return {"error": "the cue ball is not in hand"}
		_match.move_cue_again()
		if not _match.place_cue(cue):
			_sync_match_ui()
			return {"error": "the engine refused the cue ball at [%s, %s]" % [(cue as Vector2).x, (cue as Vector2).y]}
	if _match.placing():
		_sync_match_ui()
		# As the person confirms it, so the placer and the match agree.
		if _placer.is_placing():
			_placer.button("confirm").pressed.emit()
		else:
			_on_cue_confirmed()
	_warn_out_of_range(strike, "match_shot")
	_set_up(strike)
	if not nominated.is_empty() and not _match.nominate(nominated, "harness"):
		return {"error": "\"%s\" can't be nominated; open to nominate: %s" % [nominated, _match.nominations()]}
	_mark_nominated()
	var shot_id := _strike(HARNESS_STRIKE)
	if shot_id.is_empty():
		return {"error": "the stroke was refused (strike_blocked \"%s\"; shot.rejected says if the physics refused it)"
				% _match.strike_blocked()}
	if _fast:
		skip_shot()
	return {"shot_id": shot_id}


## The person's choice among those offered. {"kind"}, or {"error"}.
func harness_match_choose(kind: String) -> Dictionary:
	if _mode != MATCH or not _match.choose(kind):
		return {"error": "\"%s\" was not chosen (offered: %s)" % [kind, _person_options() if _mode == MATCH else []]}
	return {"kind": kind}


## What a match waits for, and whether that is the person: a stroke or a choice of theirs, or a
## summary to leave. {"waiting", "person"}, or {"error"} with no match on the table.
func harness_match_waiting() -> Dictionary:
	if _mode != MATCH:
		return {"error": "no match is on the table"}
	var waiting := _match.waiting()
	return {"waiting": waiting, "person": not _playing and not _lining_up
			and ["shot", "choice", "frame_summary", "match_summary"].has(waiting)}


func harness_match_autoplay(on: bool, fast: bool) -> Dictionary:
	if _mode != MATCH:
		return {"error": "no match is on the table"}
	_fast = fast
	_match.pace = 0.0 if fast else 1.0
	_match.autoplay = on
	_sync_match_ui()
	return {"on": _match.autoplay, "fast": _fast}


## Opens the pause menu as the scoreboard's button does, or resumes from it. {"on", "can_concede"},
## or {"error"} when the match's own screen is not the one showing.
func harness_match_pause(on: bool) -> Dictionary:
	if _mode != MATCH or (on and _screen != MATCH) or (not on and _screen != PAUSE):
		return {"error": "%s (screen \"%s\")" % ["no match is on the table" if _mode != MATCH
				else "the pause menu opens from the match's own screen" if on else "the pause menu is not open", _screen]}
	if on:
		_open_pause()
	else:
		_close_pause()
	return {"on": _pause.visible, "can_concede": not _pause.button("concede_frame").disabled}


## From a frame summary on to the next frame (or the match's summary), from a match summary Home.
## {"screen"}, or {"error"} when neither shows.
func harness_match_continue() -> Dictionary:
	if _frame_summary.visible:
		_on_continue()
	elif _match_summary.visible:
		_on_summary_home()
	else:
		return {"error": "no summary is showing (screen \"%s\")" % _screen}
	return {"screen": _screen}


## The game's own part of the state dump: screen, mode, balls, shot setup, settings, last shot, and
## the match (MatchController.harness_state()).
func harness_state() -> Dictionary:
	var shot := setup.snapshot()
	shot["elevation_min_rad"] = setup.elevation_min_rad
	return {
		"screen": _screen,
		"mode": _mode,
		"balls": _table_balls().duplicate(true),
		"cue_ball": _table_cue(),
		"setup": shot,
		"settings": Settings.all(),
		"playing": _playing,
		"edit_mode": _free.edit_mode,
		"history": {"index": _free.history_index(), "size": _free.history_size()},
		"last_shot": _last_shot.duplicate(true),
		"camera": {"pitch_deg": _rig.pitch_deg(), "zoom_scale": _rig.zoom_scale(), "view": _rig.view_name()},
		"break": int(_match.frame().get("break_points", 0)) if _mode == MATCH else _cycle.shown_break,
		"practice": String(_practices[_practice]["position"]) if _practice >= 0 else "",
		"match": _match.harness_state(),
	}


func _harness_refused(command: String, why: String) -> void:
	_harness_why = why
	Log.warn("app", "app.error", {"kind": "message", "message": "main: harness %s refused: %s" % [command, why]})
