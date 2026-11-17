class_name PullControl
extends Control
## Pull and release (KaiC's M2 phone session; spec 8.3 lists the modes): on the power cue at the
## right edge, press and drag down to pull the cue back. The pull sets the power, power_for_pull,
## and the power the speed through speed_for_power, finer at the low end. Releasing fires at the
## power last shown; no push is needed. The cue here and on the table follows the pull, and a
## readout beside the thumb shows the power and pace.
##
## Cancels, logged as input.cancel: a release under MIN_PULL; sliding back to where the press
## started once past MIN_PULL; sliding out sideways past LEAVE_MARGIN; a cancelled touch or a
## release reported outside the strip. Each press's events are kept as the setup's stroke_samples.

signal backswing_changed(distance_m: float)
signal power_changed(power: float)
signal struck(speed_m_s: float)
signal cancelled(reason: String)

## With the 6.5 m/s top, 1.5 keeps the paces KaiC plays most (1.4 to 3.6 m/s) within 0.07 of where
## they sat on the bar under the first curve (10 m/s, squared).
const POWER_EXPONENT := 1.5
## The pull for full power: about 42 mm on the phone (1 canvas unit is about 0.088 mm), reachable
## from a press anywhere in the strip's upper half.
const FULL_PULL := 480.0
## About 2 mm: a touch that hardly moves is not a shot, and below it the guides keep the last pace.
const MIN_PULL := 24.0
const LEAVE_MARGIN := 40.0
const BACKSWING_AT_FULL_M := 0.25
const MIN_SIZE := Vector2(120.0, 300.0)
const FONT_SIZE := 28
const SMALL_FONT_SIZE := 22
const READOUT_SIZE := Vector2(132.0, 74.0)
const READOUT_GAP := 14.0

const PANEL_COLOUR := Color(0.05, 0.06, 0.07, 0.78)
const TEXT_COLOUR := Color(0.93, 0.94, 0.93)
const FILL_COLOUR := Color(0.96, 0.72, 0.3)

const NO_PULL := "no pull"
const SLID_BACK := "slid back to the start"
const LEFT_STRIP := "left the cue strip"

const _NO_POINTER := -100
const _MOUSE := -2

## Seconds; tests replace it.
var clock: Callable = func() -> float: return Time.get_ticks_usec() / 1.0e6

var _setup: ShotSetup
var _pointer := _NO_POINTER
var _start := Vector2.ZERO
var _finger := Vector2.ZERO
var _pull := 0.0
var _armed := false
## The setup's power and speed before this press, put back when it is cancelled.
var _before_power := 0.0
var _before_speed := 0.0
## The power show_power puts on the strip, or -1 for none.
var _shown := -1.0


func _ready() -> void:
	mouse_filter = MOUSE_FILTER_STOP
	custom_minimum_size = MIN_SIZE


func bind(setup: ShotSetup) -> void:
	if _setup != null and _setup.changed.is_connected(_on_setup_changed):
		_setup.changed.disconnect(_on_setup_changed)
	_setup = setup
	if _setup != null:
		_setup.changed.connect(_on_setup_changed)
		if _setup.strike_mode == ShotSetup.PULL:
			_apply_mode()
	queue_redraw()


## Cue speed for a power 0-1: 0 at 0, max_speed_m_s at 1, finer at the low end.
static func speed_for_power(power: float, max_speed_m_s: float) -> float:
	return max_speed_m_s * pow(clampf(power, 0.0, 1.0), POWER_EXPONENT)


static func power_for_speed(speed_m_s: float, max_speed_m_s: float) -> float:
	if max_speed_m_s <= 0.0:
		return 0.0
	return pow(clampf(speed_m_s / max_speed_m_s, 0.0, 1.0), 1.0 / POWER_EXPONENT)


## Power for a pull of `units` canvas units: linear, full at FULL_PULL.
static func power_for_pull(units: float) -> float:
	return clampf(units / FULL_PULL, 0.0, 1.0)


static func pull_for_power(power: float) -> float:
	return clampf(power, 0.0, 1.0) * FULL_PULL


static func backswing_for_pull(units: float) -> float:
	return power_for_pull(units) * BACKSWING_AT_FULL_M


## Shows the strip drawn back to `power` (0 to 1) with its fill line and no readout, as another
## player's stroke is shown on the dimmed controls; -1 clears it. The setup is not touched, and a
## pull of the person's own draws over it.
func show_power(power: float) -> void:
	var shown := clampf(power, 0.0, 1.0) if power >= 0.0 else -1.0
	if shown == _shown:
		return
	_shown = shown
	queue_redraw()


## The power show_power is showing, or -1.
func shown_power() -> float:
	return _shown


func is_pulling() -> bool:
	return _pointer != _NO_POINTER


## The power shown: the pull's while pulling, else the last one set.
func power() -> float:
	if is_pulling():
		return power_for_pull(_pull)
	return _setup.power if _setup != null else 0.0


func speed() -> float:
	return speed_for_power(power(), _max_speed())


## Ends a press in progress without striking, as when a shot starts or the app loses focus.
func cancel_press(reason: String = "cancelled by the game") -> void:
	if is_pulling():
		_cancel(reason)


## Space's test aid (spec 8.8): strikes at the last power set, as a release would. False when
## there is none or a pull is in progress.
func shoot_last_power() -> bool:
	if _setup == null or is_pulling() or _setup.power <= 0.0:
		return false
	var s := speed_for_power(_setup.power, _max_speed())
	_setup.wander_rad = 0.0
	_setup.set_speed(s, _setup.power)
	struck.emit(s)
	return true


## The readout: "42%" and the pace, such as "1.84 m/s".
func readout_text() -> String:
	var s := speed()
	return "%d%%\n%s m/s" % [roundi(power() * 100.0), ("%.2f" if s < 1.0 else "%.1f") % s]


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouse and event.device == InputEvent.DEVICE_ID_EMULATION:
		return
	if event is InputEventScreenTouch:
		var touch := event as InputEventScreenTouch
		if touch.pressed:
			_on_press(touch.index, touch.position)
		else:
			_on_release(touch.index, touch.position, touch.canceled)
	elif event is InputEventScreenDrag:
		var drag := event as InputEventScreenDrag
		_on_move(drag.index, drag.position)
	elif event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		if button.button_index != MOUSE_BUTTON_LEFT:
			return
		if button.pressed:
			_on_press(_MOUSE, button.position)
		else:
			_on_release(_MOUSE, button.position, button.canceled)
	elif event is InputEventMouseMotion:
		_on_move(_MOUSE, (event as InputEventMouseMotion).position)


func _notification(what: int) -> void:
	match what:
		NOTIFICATION_VISIBILITY_CHANGED:
			if not is_visible_in_tree():
				cancel_press("hidden")
		# A touch lifted while the app is away never reports its release.
		NOTIFICATION_APPLICATION_FOCUS_OUT, NOTIFICATION_APPLICATION_PAUSED:
			cancel_press("focus lost")


func _on_press(pointer: int, pos: Vector2) -> void:
	if is_pulling():
		return
	_pointer = pointer
	_start = pos
	_finger = pos
	_pull = 0.0
	_armed = false
	if _setup != null:
		_before_power = _setup.power
		_before_speed = _setup.speed_m_s
		_setup.clear_stroke_samples()
	_sample(pos)
	queue_redraw()


func _on_move(pointer: int, pos: Vector2) -> void:
	if pointer != _pointer:
		return
	_sample(pos)
	if _outside(pos):
		_cancel(LEFT_STRIP)
		return
	_finger = pos
	var pull := maxf(pos.y - _start.y, 0.0)
	if _armed and pull <= 0.0:
		_cancel(SLID_BACK)
		return
	if pull >= MIN_PULL:
		_armed = true
	if pull == _pull:
		return
	_pull = pull
	var p := power_for_pull(pull)
	if _armed and _setup != null:
		_setup.set_pull(p, speed_for_power(p, _max_speed()))
	backswing_changed.emit(backswing_for_pull(pull))
	power_changed.emit(p)
	queue_redraw()


func _on_release(pointer: int, pos: Vector2, canceled: bool) -> void:
	if pointer != _pointer:
		return
	_sample(pos)
	if canceled:
		_cancel("touch cancelled")
		return
	# iOS can report a cancelled touch as a release far off the control, near the screen's (-1, -1).
	if _outside(pos):
		_cancel(LEFT_STRIP)
		return
	if _pull < MIN_PULL:
		_cancel(NO_PULL)
		return
	# The power last shown, not the release point's: lifting a thumb can move it a little.
	var p := power_for_pull(_pull)
	var s := speed_for_power(p, _max_speed())
	_end()
	if _setup != null:
		_setup.wander_rad = 0.0
		_setup.end_pull(p, s)
	struck.emit(s)


## Only sideways: below the strip the pull is already full, and above it is sliding back.
func _outside(pos: Vector2) -> bool:
	return pos.x < -LEAVE_MARGIN or pos.x > size.x + LEAVE_MARGIN or pos.y < -LEAVE_MARGIN


func _cancel(reason: String) -> void:
	var was_armed := _armed
	_end()
	Log.info("input", "input.cancel", {"control": "pull", "reason": reason})
	if was_armed and _setup != null:
		_setup.end_pull(_before_power, _before_speed)
	backswing_changed.emit(0.0)
	cancelled.emit(reason)


func _end() -> void:
	_pointer = _NO_POINTER
	_pull = 0.0
	_armed = false
	queue_redraw()


func _sample(pos: Vector2) -> void:
	if _setup != null:
		_setup.add_stroke_sample(clock.call(), pos)


func _apply_mode() -> void:
	if _setup.power <= 0.0:
		var p := power_for_speed(ShotSetup.DEFAULT_PACE_M_S, _max_speed())
		_setup.set_speed(speed_for_power(p, _max_speed()), p)


func _max_speed() -> float:
	return Physics.param("max_cue_speed_m_s", 10.0)


func _on_setup_changed(what: StringName) -> void:
	if what == &"mode" and _setup.strike_mode == ShotSetup.PULL:
		_apply_mode()


func _draw() -> void:
	var pull := _pull if is_pulling() or _shown < 0.0 else pull_for_power(_shown)
	CueStrip.draw(self, size, pull, is_pulling())
	if pull <= 0.0:
		return
	var rest := CueStrip.TIP_REST
	var tip := CueStrip.tip_y(size.y, pull)
	var x := size.x / 2.0
	draw_line(Vector2(x, rest), Vector2(x, tip), Color(FILL_COLOUR, 0.55), 4.0, true)
	if not is_pulling() or not _armed:
		return
	# Left of the strip and above the thumb, so the thumb never covers it.
	var box := Rect2(Vector2(-READOUT_GAP - READOUT_SIZE.x, clampf(_finger.y - 150.0, 0.0,
			size.y - READOUT_SIZE.y)), READOUT_SIZE)
	var panel := StyleBoxFlat.new()
	panel.bg_color = PANEL_COLOUR
	panel.set_corner_radius_all(16)
	draw_style_box(panel, box)
	var font := get_theme_default_font()
	var lines := readout_text().split("\n")
	draw_string(font, box.position + Vector2(0.0, 32.0), lines[0], HORIZONTAL_ALIGNMENT_CENTER, box.size.x,
			FONT_SIZE, FILL_COLOUR)
	draw_string(font, box.position + Vector2(0.0, 62.0), lines[1], HORIZONTAL_ALIGNMENT_CENTER, box.size.x,
			SMALL_FONT_SIZE, TEXT_COLOUR)
