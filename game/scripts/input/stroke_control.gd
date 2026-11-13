class_name StrokeControl
extends Control
## The finger stroke (spec 8.3) in its stroke area: pull back (drag down), push forward (drag up)
## and release. The push speed over the last WINDOW_S before release sets the cue speed through
## speed_for_push, which gives fine control at low power. The cue follows the backswing. Dragging
## out of the area cancels. With Settings "stroke_wander" on, sideways wander during the push adds
## a small aim error (ShotSetup.wander_rad).

signal backswing_changed(distance_m: float)
signal struck(speed_m_s: float)
signal cancelled(reason: String)

const WINDOW_S := 0.08
## The finger speed that gives max_cue_speed_m_s: about 0.5 m/s on the phone (1 canvas unit is
## about 0.088 mm), a hard but controlled push over a few centimetres.
const FASTEST_PUSH_PX_S := 6000.0
## Relative pace error grows by this factor, so it stays gentle: a slow push still maps to a
## usable range of soft shots without doubling the spread of a medium one.
const PUSH_EXPONENT := 1.5
## Slower than this over the window (under 8 units of travel) is a finger lifted, not a push.
const MIN_PUSH_PX_S := 100.0
## A pull the full height of the canvas is about 0.25 m of backswing, whatever the area's size.
const BACKSWING_M_PER_UNIT := 0.25 / 880.0
const LEAVE_MARGIN := 40.0
## Aim error per unit of sideways-to-forward finger speed, and its cap.
const WANDER_GAIN := 0.1
const MAX_WANDER_RAD := 1.5 * PI / 180.0
const MIN_SIZE := Vector2(120.0, 300.0)

const LEFT_AREA := "left the stroke area"
const NO_PUSH := "no push"

const _NO_POINTER := -100
const _MOUSE := -2

## Seconds; tests replace it.
var clock: Callable = func() -> float: return Time.get_ticks_usec() / 1.0e6
## Tests replace it rather than change the saved setting.
var wander_enabled: Callable = func() -> bool: return Settings.get_value("stroke_wander") == true

var _setup: ShotSetup
var _pointer := _NO_POINTER
var _start := Vector2.ZERO
var _finger := Vector2.ZERO
var _backswing_m := 0.0
# Doubles: clock() keeps growing for the life of the app, beyond float32 precision.
var _t := PackedFloat64Array()
var _x := PackedFloat64Array()
var _y := PackedFloat64Array()


func _ready() -> void:
	mouse_filter = MOUSE_FILTER_STOP
	custom_minimum_size = MIN_SIZE


func bind(setup: ShotSetup) -> void:
	_setup = setup


func is_stroking() -> bool:
	return _pointer != _NO_POINTER


## Abandons a stroke in progress, as when the integrator takes the finger for something else.
func cancel_stroke(reason: String) -> void:
	if is_stroking():
		_cancel(reason)


## Cue speed for a push of push_px_per_s canvas units per second: 0 at 0, rising slowly at first,
## max_speed_m_s at and beyond the fastest push.
static func speed_for_push(push_px_per_s: float, max_speed_m_s: float) -> float:
	if push_px_per_s <= 0.0:
		return 0.0
	return max_speed_m_s * pow(minf(push_px_per_s / FASTEST_PUSH_PX_S, 1.0), PUSH_EXPONENT)


static func backswing_for_pull(pull_units: float) -> float:
	return maxf(pull_units, 0.0) * BACKSWING_M_PER_UNIT


## Finger velocity over the last `window` seconds of samples, in canvas units per second:
## x rightward, y upward. The position at the window's start is interpolated; before the first
## sample the finger is taken to be resting where it touched down.
static func finger_velocity(t: PackedFloat64Array, x: PackedFloat64Array, y: PackedFloat64Array, window: float) -> Vector2:
	var n := t.size()
	if n == 0 or window <= 0.0:
		return Vector2.ZERO
	var t_from := t[n - 1] - window
	var i := n - 1
	while i > 0 and t[i] > t_from:
		i -= 1
	var from_x := x[i]
	var from_y := y[i]
	if t[i] <= t_from and i < n - 1:
		var f := (t_from - t[i]) / (t[i + 1] - t[i])
		from_x = lerpf(x[i], x[i + 1], f)
		from_y = lerpf(y[i], y[i + 1], f)
	return Vector2((x[n - 1] - from_x) / window, (from_y - y[n - 1]) / window)


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
	if what == NOTIFICATION_VISIBILITY_CHANGED and not is_visible_in_tree():
		cancel_stroke("hidden")


func _on_press(pointer: int, pos: Vector2) -> void:
	if is_stroking():
		return
	_pointer = pointer
	_start = pos
	_t.clear()
	_x.clear()
	_y.clear()
	_sample(pos)
	_set_backswing(0.0)
	queue_redraw()


func _on_move(pointer: int, pos: Vector2) -> void:
	if pointer != _pointer:
		return
	_sample(pos)
	if not Rect2(Vector2.ZERO, size).grow(LEAVE_MARGIN).has_point(pos):
		_cancel(LEFT_AREA)
		return
	_set_backswing(backswing_for_pull(pos.y - _start.y))
	queue_redraw()


func _on_release(pointer: int, pos: Vector2, canceled: bool) -> void:
	if pointer != _pointer:
		return
	_sample(pos)
	if canceled:
		_cancel("touch cancelled")
		return
	var velocity := finger_velocity(_t, _x, _y, WINDOW_S)
	if velocity.y < MIN_PUSH_PX_S:
		_cancel(NO_PUSH)
		return
	_pointer = _NO_POINTER
	var speed := speed_for_push(velocity.y, Physics.param("max_cue_speed_m_s", 10.0))
	if _setup != null:
		_setup.wander_rad = _wander(velocity) if wander_enabled.call() else 0.0
		_setup.set_speed(speed)
	queue_redraw()
	struck.emit(speed)


# Finger drifting right sends the cue to the player's right, which is clockwise: -aim_rad.
func _wander(velocity: Vector2) -> float:
	return -clampf(WANDER_GAIN * velocity.x / velocity.y, -MAX_WANDER_RAD, MAX_WANDER_RAD)


func _cancel(reason: String) -> void:
	_pointer = _NO_POINTER
	Log.info("input", "input.cancel", {"control": "stroke", "reason": reason})
	_set_backswing(0.0)
	queue_redraw()
	cancelled.emit(reason)


func _sample(pos: Vector2) -> void:
	var now: float = clock.call()
	_t.append(now)
	_x.append(pos.x)
	_y.append(pos.y)
	_finger = pos
	if _setup != null:
		_setup.add_stroke_sample(now, pos)


func _set_backswing(distance_m: float) -> void:
	if is_equal_approx(distance_m, _backswing_m):
		return
	_backswing_m = distance_m
	backswing_changed.emit(distance_m)


func _draw() -> void:
	var rect := Rect2(Vector2.ZERO, size)
	var strip := StyleBoxFlat.new()
	strip.bg_color = Color(1.0, 1.0, 1.0, 0.045)
	strip.border_color = Color(1.0, 1.0, 1.0, 0.09)
	strip.set_border_width_all(2)
	strip.set_corner_radius_all(24)
	draw_style_box(strip, rect)
	var x := size.x / 2.0
	var hint := Color(1.0, 1.0, 1.0, 0.22)
	for end: float in [1.0, -1.0]:
		# A chevron at each end: push up at the top, pull back at the bottom.
		var tip_y := 30.0 if end > 0.0 else size.y - 30.0
		var back := tip_y + 14.0 * end
		draw_line(Vector2(x - 16.0, back), Vector2(x, tip_y), hint, 3.0, true)
		draw_line(Vector2(x + 16.0, back), Vector2(x, tip_y), hint, 3.0, true)
	draw_line(Vector2(x, 60.0), Vector2(x, size.y - 60.0), Color(1.0, 1.0, 1.0, 0.07), 2.0, true)
	if not is_stroking():
		return
	if _finger.y > _start.y:
		draw_rect(Rect2(0.0, _start.y, size.x, _finger.y - _start.y), Color(1.0, 1.0, 1.0, 0.07))
	draw_line(Vector2(0.0, _start.y), Vector2(size.x, _start.y), Color(1.0, 1.0, 1.0, 0.4), 2.0, true)
	draw_circle(_finger, 30.0, Color(1.0, 1.0, 1.0, 0.16))
	draw_arc(_finger, 30.0, 0.0, TAU, 48, Color(1.0, 1.0, 1.0, 0.45), 2.0, true)
