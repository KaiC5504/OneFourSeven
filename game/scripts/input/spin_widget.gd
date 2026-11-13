class_name SpinWidget
extends Control
## The cue-ball widget (spec 8.2): tap it to enlarge, then drag the tip marker for top, screw
## and side. The miscue limit (Physics.param("miscue_limit")) is drawn as a ring; the marker can go
## past it, up to MAX_TIP, and such a shot miscues.
##
## It sizes itself through custom_minimum_size: COLLAPSED_SIZE square, or EXPANDED_SIZE wide and
## READOUT_HEIGHT taller when expanded. Anchor it to its corner and set grow_horizontal and
## grow_vertical away from that corner so it expands onto the screen. While expanded, a press
## anywhere outside it collapses it and still goes on to whatever it landed on.

signal tip_changed(tip: Vector2)
signal expanded_changed(on: bool)

## Furthest tip offset the marker reaches, in ball radii.
const MAX_TIP := 0.8
const COLLAPSED_SIZE := 150.0
const EXPANDED_SIZE := 420.0
const READOUT_HEIGHT := 60.0
const COLLAPSED_MARGIN := 8.0
const EXPANDED_MARGIN := 34.0
# 44 pt touch targets.
const GRAB_RADIUS := 44.0
const CLOSE_RADIUS := 44.0
const TAP_SLOP := 20.0
const FONT_SIZE := 26

const BALL_COLOUR := Color(0.93, 0.92, 0.86)
const PANEL_COLOUR := Color(0.05, 0.06, 0.07, 0.82)
const CROSSHAIR_COLOUR := Color(0.0, 0.0, 0.0, 0.14)
const RING_COLOUR := Color(0.78, 0.2, 0.15, 0.6)
const MARKER_COLOUR := Color(0.16, 0.36, 0.78)
const MISCUE_COLOUR := Color(0.86, 0.22, 0.16)
const TEXT_COLOUR := Color(0.9, 0.92, 0.9, 0.92)

enum Press { NONE, EXPAND, MARKER, CLOSE }

const _NO_POINTER := -100
const _MOUSE := -2

var expanded := false

var _setup: ShotSetup
var _tip := Vector2.ZERO
var _pointer := _NO_POINTER
var _press := Press.NONE
var _press_at := Vector2.ZERO
var _grab_offset := Vector2.ZERO


func _ready() -> void:
	mouse_filter = MOUSE_FILTER_STOP
	_apply_size()
	set_process_input(expanded)


func bind(setup: ShotSetup) -> void:
	if _setup != null and _setup.changed.is_connected(_on_setup_changed):
		_setup.changed.disconnect(_on_setup_changed)
	_setup = setup
	if _setup != null:
		_setup.changed.connect(_on_setup_changed)
		_tip = Vector2(_setup.tip_x, _setup.tip_y)
	queue_redraw()


func expand() -> void:
	_set_expanded(true)


func collapse() -> void:
	_set_expanded(false)


func current_tip() -> Vector2:
	return Vector2(_setup.tip_x, _setup.tip_y) if _setup != null else _tip


## A point in this control's local coordinates to tip offsets (x right, y up, ball radii),
## clamped to MAX_TIP, for the current size and expanded state.
func tip_for_local(local: Vector2) -> Vector2:
	var radius := ball_radius()
	if radius <= 0.0:
		return Vector2.ZERO
	var tip := (local - ball_centre()) / radius
	tip.y = -tip.y
	return tip.limit_length(MAX_TIP)


func local_for_tip(tip: Vector2) -> Vector2:
	return ball_centre() + Vector2(tip.x, -tip.y) * ball_radius()


func ball_centre() -> Vector2:
	if expanded:
		var side := _ball_side()
		return Vector2(size.x / 2.0, side / 2.0)
	return size / 2.0


func ball_radius() -> float:
	if expanded:
		return maxf(_ball_side() / 2.0 - EXPANDED_MARGIN, 0.0)
	return maxf(minf(size.x, size.y) / 2.0 - COLLAPSED_MARGIN, 0.0)


func miscue_ring_radius() -> float:
	return _miscue_limit() * ball_radius()


func close_centre() -> Vector2:
	var side := _ball_side()
	return Vector2(size.x / 2.0 + side / 2.0 - CLOSE_RADIUS, CLOSE_RADIUS)


## The readout, such as "screw 0.40 · left 0.20".
static func describe(tip: Vector2, miscue_limit: float) -> String:
	var parts := PackedStringArray()
	if absf(tip.y) >= 0.005:
		parts.append("%s %.2f" % ["top" if tip.y > 0.0 else "screw", absf(tip.y)])
	if absf(tip.x) >= 0.005:
		parts.append("%s %.2f" % ["right" if tip.x > 0.0 else "left", absf(tip.x)])
	if parts.is_empty():
		return "centre ball"
	if tip.length() > miscue_limit:
		parts.append("miscue")
	return " · ".join(parts)


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouse and event.device == InputEvent.DEVICE_ID_EMULATION:
		return
	if event is InputEventScreenTouch:
		var touch := event as InputEventScreenTouch
		if touch.pressed:
			_on_press(touch.index, touch.position, touch.double_tap)
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
			_on_press(_MOUSE, button.position, button.double_click)
		else:
			_on_release(_MOUSE, button.position, button.canceled)
	elif event is InputEventMouseMotion:
		_on_move(_MOUSE, (event as InputEventMouseMotion).position)


# Only listens while expanded, for the press outside that collapses it.
func _input(event: InputEvent) -> void:
	if not expanded or not is_visible_in_tree():
		return
	var pressed := false
	if event is InputEventScreenTouch:
		pressed = (event as InputEventScreenTouch).pressed
	elif event is InputEventMouseButton and event.device != InputEvent.DEVICE_ID_EMULATION:
		var button := event as InputEventMouseButton
		pressed = button.pressed and button.button_index == MOUSE_BUTTON_LEFT
	if not pressed:
		return
	var local := make_input_local(event)
	var pos: Vector2 = local.get("position")
	if not Rect2(Vector2.ZERO, size).has_point(pos):
		collapse()


func _notification(what: int) -> void:
	if what == NOTIFICATION_VISIBILITY_CHANGED and not is_visible_in_tree():
		_end_press()


func _on_press(pointer: int, pos: Vector2, double: bool) -> void:
	if _pointer != _NO_POINTER:
		return
	_pointer = pointer
	_press_at = pos
	if not expanded:
		_press = Press.EXPAND
		return
	if pos.distance_to(close_centre()) <= CLOSE_RADIUS:
		_press = Press.CLOSE
		return
	_press = Press.MARKER
	if double:
		# Back to centre ball; the rest of this touch only ends the press.
		_press = Press.NONE
		_apply_tip(Vector2.ZERO)
		return
	var marker := local_for_tip(current_tip())
	_grab_offset = marker - pos if pos.distance_to(marker) <= GRAB_RADIUS else Vector2.ZERO
	_apply_tip(tip_for_local(pos + _grab_offset))


func _on_move(pointer: int, pos: Vector2) -> void:
	if pointer != _pointer:
		return
	if _press == Press.MARKER:
		_apply_tip(tip_for_local(pos + _grab_offset))


func _on_release(pointer: int, pos: Vector2, canceled: bool) -> void:
	if pointer != _pointer:
		return
	var press := _press
	_end_press()
	if canceled:
		return
	if press == Press.EXPAND and pos.distance_to(_press_at) <= TAP_SLOP:
		expand()
	elif press == Press.CLOSE and pos.distance_to(close_centre()) <= CLOSE_RADIUS:
		collapse()


func _end_press() -> void:
	_pointer = _NO_POINTER
	_press = Press.NONE


func _apply_tip(tip: Vector2) -> void:
	if tip.is_equal_approx(current_tip()):
		return
	_tip = tip
	if _setup != null:
		_setup.set_tip(tip.x, tip.y)
	tip_changed.emit(tip)
	queue_redraw()


func _set_expanded(on: bool) -> void:
	if expanded == on:
		return
	expanded = on
	_end_press()
	_apply_size()
	set_process_input(on)
	queue_redraw()
	expanded_changed.emit(on)


func _apply_size() -> void:
	if expanded:
		custom_minimum_size = Vector2(EXPANDED_SIZE, EXPANDED_SIZE + READOUT_HEIGHT)
	else:
		custom_minimum_size = Vector2(COLLAPSED_SIZE, COLLAPSED_SIZE)


func _ball_side() -> float:
	return minf(size.x, size.y - READOUT_HEIGHT)


func _miscue_limit() -> float:
	return Physics.param("miscue_limit", 0.55)


func _on_setup_changed(what: StringName) -> void:
	if what == &"tip" or what == &"reset":
		_tip = Vector2(_setup.tip_x, _setup.tip_y)
		queue_redraw()


func _draw() -> void:
	var centre := ball_centre()
	var radius := ball_radius()
	if radius <= 0.0:
		return
	var limit := _miscue_limit()
	var tip := current_tip()
	var miscue := tip.length() > limit
	if expanded:
		var panel := StyleBoxFlat.new()
		panel.bg_color = PANEL_COLOUR
		panel.set_corner_radius_all(24)
		draw_style_box(panel, Rect2(Vector2.ZERO, size))
	draw_circle(centre, radius + 2.0, Color(0.0, 0.0, 0.0, 0.35))
	draw_circle(centre, radius, BALL_COLOUR)
	var cross := 1.5 if expanded else 1.0
	draw_line(centre - Vector2(radius, 0.0), centre + Vector2(radius, 0.0), CROSSHAIR_COLOUR, cross, true)
	draw_line(centre - Vector2(0.0, radius), centre + Vector2(0.0, radius), CROSSHAIR_COLOUR, cross, true)
	draw_arc(centre, limit * radius, 0.0, TAU, 96, RING_COLOUR, 2.0 if expanded else 1.5, true)
	var marker := local_for_tip(tip)
	var marker_radius := 13.0 if expanded else 7.0
	draw_circle(marker, marker_radius + 2.0, Color(1.0, 1.0, 1.0, 0.9))
	draw_circle(marker, marker_radius, MISCUE_COLOUR if miscue else MARKER_COLOUR)
	if not expanded:
		return
	var close := close_centre()
	draw_circle(close, 20.0, Color(1.0, 1.0, 1.0, 0.12))
	var arm := 8.0
	draw_line(close + Vector2(-arm, -arm), close + Vector2(arm, arm), TEXT_COLOUR, 3.0, true)
	draw_line(close + Vector2(-arm, arm), close + Vector2(arm, -arm), TEXT_COLOUR, 3.0, true)
	var font := get_theme_default_font()
	var baseline := _ball_side() + (READOUT_HEIGHT + FONT_SIZE * 0.7) / 2.0 - 6.0
	draw_string(font, Vector2(0.0, baseline), describe(tip, limit), HORIZONTAL_ALIGNMENT_CENTER,
			size.x, FONT_SIZE, MISCUE_COLOUR.lightened(0.25) if miscue else TEXT_COLOUR)
