class_name Gestures
extends Node
## The input layer (spec 8.1, 8.5, 8.8): touches and the mouse turned into one set of gestures.
## It reads _unhandled_input, so HUD controls take their own touches first. On Windows a left-drag
## is a one-finger drag, the wheel is pinch and a right-drag is a two-finger drag. Positions are
## in the viewport's canvas coordinates.
##
## A gesture runs from the first finger down to the last finger up. A one-finger drag that a
## second finger joins ends at once and the pair becomes a two-finger gesture; once a two- or
## three-finger gesture has started, the fingers left after one lifts do nothing until all are up,
## so the aim never jumps to wherever a remaining finger happens to be.

signal drag_started(position: Vector2)
signal drag_moved(position: Vector2, relative: Vector2)
signal drag_ended(position: Vector2)
signal tapped(position: Vector2)
## factor > 1 zooms in.
signal pinched(factor: float, centre: Vector2)
signal two_finger_dragged(relative: Vector2)
signal three_finger_tapped

## Canvas units a finger moves before a touch becomes a drag: about 4 pt on the phone, tight so
## the aim answers small movements.
const DRAG_SLOP := 8.0
## A touch held still longer than this is not a tap (iOS's long-press threshold).
const TAP_MAX_S := 0.5
## Two fingers: the centroid has to move this far, and PAN_DOMINANCE times further than the
## separation changes, to start a two-finger drag; the separation has to change this far, and
## more than the centroid moves, to start a pinch. A one-finger pinch moves the centroid half as
## far as the separation changes, so it reads as a pinch.
const PAN_SLOP := 12.0
const PINCH_SLOP := 16.0
const PAN_DOMINANCE := 2.0
## Hysteresis: once decided, the other movement has to beat the current one by this much,
## counted from the last moment the current one led, before the gesture switches.
const SWITCH_SLOP := 40.0
const THREE_FINGER_WINDOW_S := 0.25
const THREE_FINGER_SLOP := 16.0
const THREE_FINGER_MAX_S := 0.6
const WHEEL_FACTOR := 1.1
## The left mouse button's slot among the touch indices.
const MOUSE_INDEX := -100

enum Mode { NONE, PENDING, DRAG, TWO_PENDING, PINCH, TWO_DRAG, THREE, SPENT }


class Touch:
	var start: Vector2
	var position: Vector2
	var start_s: float

	func _init(at: Vector2, now_s: float) -> void:
		start = at
		position = at
		start_s = now_s


## Setting it false ends any gesture in progress (a drag gets its drag_ended) and forgets the
## fingers down, so turning it back on mid-touch starts nothing until a new touch.
var enabled := true:
	set = set_enabled
## Seconds; tests replace it.
var clock: Callable = func() -> float: return Time.get_ticks_usec() / 1.0e6

var _touches := {}
var _mode := Mode.NONE
var _first_down_s := 0.0
var _anchor_centre := Vector2.ZERO
var _anchor_separation := 0.0
var _last_centre := Vector2.ZERO
var _last_separation := 0.0
var _right_held := false


func set_enabled(on: bool) -> void:
	if not on and enabled:
		_reset("disabled")
	enabled = on


## What _unhandled_input does with an event; tests call it directly.
func feed(event: InputEvent) -> void:
	_handle(event)


## The gesture in progress, for the debug overlay and tests.
func mode() -> Mode:
	return _mode


func _unhandled_input(event: InputEvent) -> void:
	if _handle(event):
		get_viewport().set_input_as_handled()


# A touch lifted while the app is away never reports its release.
func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT or what == NOTIFICATION_APPLICATION_PAUSED:
		_reset("focus lost")


func _handle(event: InputEvent) -> bool:
	if not enabled:
		return false
	var touch := event as InputEventScreenTouch
	if touch != null:
		if touch.pressed:
			_press(touch.index, touch.position)
		else:
			_release(touch.index, touch.position, touch.canceled)
		return true
	var screen_drag := event as InputEventScreenDrag
	if screen_drag != null:
		return _move(screen_drag.index, screen_drag.position)
	# iOS emulates the mouse from touches; those copies would double every gesture.
	if event is InputEventMouse and event.device == InputEvent.DEVICE_ID_EMULATION:
		return false
	var button := event as InputEventMouseButton
	if button != null:
		return _mouse_button(button)
	var motion := event as InputEventMouseMotion
	if motion != null:
		if _touches.has(MOUSE_INDEX):
			return _move(MOUSE_INDEX, motion.position)
		# The right button's release goes astray when it happens over a HUD control or outside
		# the window.
		if _right_held and (motion.button_mask & MOUSE_BUTTON_MASK_RIGHT) == 0:
			_right_held = false
			_log_cancel("right button released unseen")
		if _right_held:
			two_finger_dragged.emit(motion.relative)
			return true
	return false


func _mouse_button(button: InputEventMouseButton) -> bool:
	match button.button_index:
		MOUSE_BUTTON_LEFT:
			if button.pressed:
				_press(MOUSE_INDEX, button.position)
			else:
				_release(MOUSE_INDEX, button.position, false)
			return true
		MOUSE_BUTTON_RIGHT:
			_right_held = button.pressed
			return true
		MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN:
			# Godot sends each notch as a press and a release. Smooth-scrolling devices send
			# fractions of a notch in factor; a plain wheel leaves it at 1 (0 on some platforms).
			if button.pressed:
				var notches := button.factor if button.factor > 0.0 else 1.0
				if button.button_index == MOUSE_BUTTON_WHEEL_DOWN:
					notches = -notches
				pinched.emit(pow(WHEEL_FACTOR, notches), button.position)
			return true
	return false


func _press(index: int, at: Vector2) -> void:
	# A press for an index still down means its release was lost.
	if _touches.has(index):
		_release(index, at, true)
	var now: float = clock.call()
	_touches[index] = Touch.new(at, now)
	match _touches.size():
		1:
			_mode = Mode.PENDING
			_first_down_s = now
		2:
			if _mode == Mode.DRAG:
				drag_ended.emit(_other_touch(index).position)
				_log_cancel("second finger during a one-finger drag")
			if _mode == Mode.PENDING or _mode == Mode.DRAG:
				_mode = Mode.TWO_PENDING
				_anchor_two()
		3:
			if _mode == Mode.TWO_PENDING and now - _first_down_s <= THREE_FINGER_WINDOW_S \
					and not _any_moved(THREE_FINGER_SLOP):
				_mode = Mode.THREE
			elif _mode == Mode.PINCH or _mode == Mode.TWO_DRAG:
				_log_cancel("third finger during a two-finger gesture")
				_mode = Mode.SPENT
			else:
				_mode = Mode.SPENT
		_:
			_mode = Mode.SPENT


func _move(index: int, at: Vector2) -> bool:
	var touch: Touch = _touches.get(index)
	if touch == null:
		return false
	var previous := touch.position
	touch.position = at
	match _mode:
		Mode.PENDING:
			if touch.start.distance_to(at) > DRAG_SLOP:
				_mode = Mode.DRAG
				# From the touch-down point, so the drag adds up to the finger's whole movement.
				drag_started.emit(touch.start)
				drag_moved.emit(at, at - touch.start)
		Mode.DRAG:
			if at != previous:
				drag_moved.emit(at, at - previous)
		Mode.TWO_PENDING, Mode.PINCH, Mode.TWO_DRAG:
			_update_two()
		Mode.THREE:
			if touch.start.distance_to(at) > THREE_FINGER_SLOP:
				_mode = Mode.SPENT
	return true


func _release(index: int, at: Vector2, cancelled: bool) -> void:
	var touch: Touch = _touches.get(index)
	if touch == null:
		return
	# iOS can report a cancelled touch as a release near (-1, -1): that is not where the finger was.
	if not cancelled and at != touch.position and _in_viewport(at):
		_move(index, at)
	_touches.erase(index)
	var now: float = clock.call()
	match _mode:
		Mode.PENDING:
			if cancelled:
				_log_cancel("touch cancelled by the system")
			elif now - touch.start_s <= TAP_MAX_S:
				tapped.emit(touch.start)
		Mode.DRAG:
			_end_drag(touch.position)
			if cancelled:
				_log_cancel("touch cancelled by the system during a drag")
		Mode.TWO_PENDING, Mode.PINCH, Mode.TWO_DRAG:
			if cancelled:
				_log_cancel("touch cancelled by the system during a two-finger gesture")
			_mode = Mode.SPENT
		Mode.THREE:
			if cancelled:
				_mode = Mode.SPENT
			elif _touches.is_empty() and now - _first_down_s <= THREE_FINGER_MAX_S:
				three_finger_tapped.emit()
	if _touches.is_empty():
		_mode = Mode.NONE


func _update_two() -> void:
	if _touches.size() != 2:
		return
	var points: Array[Vector2] = []
	for touch: Touch in _touches.values():
		points.append(touch.position)
	var centre := (points[0] + points[1]) / 2.0
	var separation := points[0].distance_to(points[1])
	var pan := centre.distance_to(_anchor_centre)
	var spread := absf(separation - _anchor_separation)
	match _mode:
		Mode.TWO_PENDING:
			# The movement that decided the gesture counts towards it.
			if pan >= PAN_SLOP and pan > spread * PAN_DOMINANCE:
				_mode = Mode.TWO_DRAG
				two_finger_dragged.emit(centre - _anchor_centre)
			elif spread >= PINCH_SLOP and spread > pan:
				_mode = Mode.PINCH
				_emit_pinch(separation, _anchor_separation, centre)
			else:
				return
			_anchor_two()
		Mode.PINCH:
			_emit_pinch(separation, _last_separation, centre)
			if spread >= pan:
				_anchor_two()
			elif pan - spread > SWITCH_SLOP:
				_mode = Mode.TWO_DRAG
				_anchor_two()
		Mode.TWO_DRAG:
			if centre != _last_centre:
				two_finger_dragged.emit(centre - _last_centre)
			if pan >= spread:
				_anchor_two()
			elif spread - pan > SWITCH_SLOP:
				_mode = Mode.PINCH
				_anchor_two()
	_last_centre = centre
	_last_separation = separation


func _emit_pinch(separation: float, from_separation: float, centre: Vector2) -> void:
	if from_separation >= 1.0 and separation != from_separation:
		pinched.emit(separation / from_separation, centre)


func _anchor_two() -> void:
	var points: Array[Vector2] = []
	for touch: Touch in _touches.values():
		points.append(touch.position)
	if points.size() < 2:
		return
	_anchor_centre = (points[0] + points[1]) / 2.0
	_anchor_separation = points[0].distance_to(points[1])
	_last_centre = _anchor_centre
	_last_separation = _anchor_separation


func _in_viewport(at: Vector2) -> bool:
	if not is_inside_tree():
		return true
	return get_viewport().get_visible_rect().has_point(at)


func _any_moved(slop: float) -> bool:
	for touch: Touch in _touches.values():
		if touch.start.distance_to(touch.position) > slop:
			return true
	return false


func _other_touch(index: int) -> Touch:
	for key: int in _touches:
		if key != index:
			return _touches[key]
	return _touches[index]


func _end_drag(at: Vector2) -> void:
	_mode = Mode.SPENT
	drag_ended.emit(at)


func _reset(reason: String) -> void:
	if _mode == Mode.DRAG:
		var touch: Touch = _touches.values()[0] if not _touches.is_empty() else null
		drag_ended.emit(touch.position if touch != null else Vector2.ZERO)
	if (_mode != Mode.NONE and _mode != Mode.SPENT) or _right_held:
		_log_cancel(reason)
	_touches.clear()
	_right_held = false
	_mode = Mode.NONE


func _log_cancel(reason: String) -> void:
	Log.info("input", "input.cancel", {"control": "gestures", "reason": reason})
