class_name SpinWidget
extends Control
## The cue-ball widget (spec 8.2, KaiC's phone sessions): a big ball at the bottom-left with a red
## crosshair where the tip meets the cue ball. The ball is see-through and plain, and all of it can
## be struck (KaiC, 2026-11-15): its edge is the furthest a tip can reach (reach()), just inside
## the miscue limit. A finger anywhere on the ball moves the crosshair by the finger's own movement
## (DRAG_GAIN), never jumping to it, so the finger never hides the point it sets; a double tap
## returns to centre ball. A readout beside the ball, such as "screw 0.40" over "left 0.20", shows
## while dragging or while the tip is off centre.

signal tip_changed(tip: Vector2)

## Canvas units, square: 120 pt on the phone.
const SIZE := 240.0
const MARGIN := 4.0
## The drawn edge as a fraction of the miscue limit: short of it, so rounding never miscues.
const EDGE := 0.98
## Crosshair movement per unit of finger movement: below 1, so the finger travels further than
## the point and sets it finely.
const DRAG_GAIN := 0.75
const CROSS_ARM := 20.0
const CROSS_WIDTH := 5.0
const FONT_SIZE := 24
const READOUT_GAP := 10.0
const READOUT_PAD := Vector2(14.0, 8.0)
const LINE_HEIGHT := 30.0

const BALL_COLOUR := Color(1.0, 1.0, 1.0, 0.3)
const EDGE_COLOUR := Color(1.0, 1.0, 1.0, 0.55)
const PANEL_COLOUR := Color(0.05, 0.06, 0.07, 0.78)
const GUIDE_COLOUR := Color(1.0, 1.0, 1.0, 0.22)
const CROSS_COLOUR := Color(0.88, 0.1, 0.08)
const CROSS_OUTLINE := Color(0.0, 0.0, 0.0, 0.4)
const HALO_COLOUR := Color(1.0, 1.0, 1.0, 0.55)
const MISCUE_COLOUR := Color(0.86, 0.22, 0.16)
const TEXT_COLOUR := Color(0.9, 0.92, 0.9, 0.92)

const _NO_POINTER := -100
const _MOUSE := -2

var _setup: ShotSetup
var _tip := Vector2.ZERO
var _pointer := _NO_POINTER
var _last_finger := Vector2.ZERO


func _ready() -> void:
	mouse_filter = MOUSE_FILTER_STOP
	custom_minimum_size = Vector2(SIZE, SIZE)
	size = custom_minimum_size


func bind(setup: ShotSetup) -> void:
	if _setup != null and _setup.changed.is_connected(_on_setup_changed):
		_setup.changed.disconnect(_on_setup_changed)
	_setup = setup
	if _setup != null:
		_setup.changed.connect(_on_setup_changed)
		_tip = Vector2(_setup.tip_x, _setup.tip_y)
	queue_redraw()


## Ends a press in progress without acting on it: a shot starting, or the app losing focus, may
## swallow the real release.
func cancel_press(reason: String = "cancelled by the game") -> void:
	if _pointer == _NO_POINTER:
		return
	Log.info("input", "input.cancel", {"control": "spin", "reason": reason})
	_end_press()


func is_pressed() -> bool:
	return _pointer != _NO_POINTER


func current_tip() -> Vector2:
	return Vector2(_setup.tip_x, _setup.tip_y) if _setup != null else _tip


## The tip offset at the drawn ball's edge, in ball radii.
func reach() -> float:
	return _miscue_limit() * EDGE


## A point in this control's local coordinates to tip offsets (x right, y up, ball radii),
## clamped to reach().
func tip_for_local(local: Vector2) -> Vector2:
	var radius := ball_radius()
	if radius <= 0.0:
		return Vector2.ZERO
	var tip := (local - ball_centre()) / radius * reach()
	tip.y = -tip.y
	return tip.limit_length(reach())


func local_for_tip(tip: Vector2) -> Vector2:
	return ball_centre() + Vector2(tip.x, -tip.y) / reach() * ball_radius()


func ball_centre() -> Vector2:
	return size / 2.0


func ball_radius() -> float:
	return maxf(minf(size.x, size.y) / 2.0 - MARGIN, 0.0)


## The readout, such as "screw 0.40 · left 0.20"; drawn a part to a line.
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


## Where the readout is drawn, right of the ball, in local coordinates; empty while hidden.
func readout_rect() -> Rect2:
	if not is_pressed() and current_tip().is_zero_approx():
		return Rect2()
	var font := get_theme_default_font()
	var lines := describe(current_tip(), _miscue_limit()).split(" · ")
	var width := 0.0
	for line in lines:
		width = maxf(width, font.get_string_size(line, HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE).x)
	var box := Vector2(width, lines.size() * LINE_HEIGHT) + READOUT_PAD * 2.0
	return Rect2(Vector2(size.x + READOUT_GAP, (size.y - box.y) / 2.0), box)


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouse and event.device == InputEvent.DEVICE_ID_EMULATION:
		return
	if event is InputEventScreenTouch:
		var touch := event as InputEventScreenTouch
		if touch.pressed:
			_on_press(touch.index, touch.position, touch.double_tap)
		elif touch.index == _pointer:
			_end_press()
	elif event is InputEventScreenDrag:
		var drag := event as InputEventScreenDrag
		_on_move(drag.index, drag.position)
	elif event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		if button.button_index != MOUSE_BUTTON_LEFT:
			return
		if button.pressed:
			_on_press(_MOUSE, button.position, button.double_click)
		elif _pointer == _MOUSE:
			_end_press()
	elif event is InputEventMouseMotion:
		_on_move(_MOUSE, (event as InputEventMouseMotion).position)


# Round, so the square's corners pass touches on to the table.
func _has_point(point: Vector2) -> bool:
	return point.distance_to(ball_centre()) <= minf(size.x, size.y) / 2.0


func _notification(what: int) -> void:
	match what:
		NOTIFICATION_VISIBILITY_CHANGED:
			if not is_visible_in_tree():
				cancel_press("hidden")
		# A touch lifted while the app is away never reports its release.
		NOTIFICATION_APPLICATION_FOCUS_OUT, NOTIFICATION_APPLICATION_PAUSED:
			cancel_press("focus lost")


func _on_press(pointer: int, pos: Vector2, double: bool) -> void:
	if _pointer != _NO_POINTER:
		return
	if double:
		# Back to centre ball; the rest of this touch does nothing.
		_apply_tip(Vector2.ZERO)
		return
	_pointer = pointer
	_last_finger = pos
	queue_redraw()


func _on_move(pointer: int, pos: Vector2) -> void:
	if pointer != _pointer:
		return
	var moved := pos - _last_finger
	_last_finger = pos
	_apply_tip(tip_for_local(local_for_tip(current_tip()) + moved * DRAG_GAIN))


func _end_press() -> void:
	_pointer = _NO_POINTER
	queue_redraw()


func _apply_tip(tip: Vector2) -> void:
	if tip.is_equal_approx(current_tip()):
		return
	_tip = tip
	if _setup != null:
		_setup.set_tip(tip.x, tip.y)
	tip_changed.emit(tip)
	queue_redraw()


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
	draw_circle(centre, radius, BALL_COLOUR)
	draw_arc(centre, radius - 1.5, 0.0, TAU, 128, EDGE_COLOUR, 3.0, true)
	draw_line(centre - Vector2(radius, 0.0), centre + Vector2(radius, 0.0), GUIDE_COLOUR, 1.5, true)
	draw_line(centre - Vector2(0.0, radius), centre + Vector2(0.0, radius), GUIDE_COLOUR, 1.5, true)
	var at := local_for_tip(tip)
	if is_pressed():
		draw_arc(at, CROSS_ARM + 6.0, 0.0, TAU, 48, HALO_COLOUR, 3.0, true)
	for arm: Vector2 in [Vector2(CROSS_ARM + 1.5, 0.0), Vector2(0.0, CROSS_ARM + 1.5)]:
		draw_line(at - arm, at + arm, CROSS_OUTLINE, CROSS_WIDTH + 3.0, true)
	for arm: Vector2 in [Vector2(CROSS_ARM, 0.0), Vector2(0.0, CROSS_ARM)]:
		draw_line(at - arm, at + arm, CROSS_COLOUR, CROSS_WIDTH, true)
	var box := readout_rect()
	if not box.has_area():
		return
	var panel := StyleBoxFlat.new()
	panel.bg_color = PANEL_COLOUR
	panel.set_corner_radius_all(14)
	draw_style_box(panel, box)
	var font := get_theme_default_font()
	var colour := MISCUE_COLOUR.lightened(0.25) if miscue else TEXT_COLOUR
	var lines := describe(tip, limit).split(" · ")
	for i in lines.size():
		var baseline := box.position.y + READOUT_PAD.y + LINE_HEIGHT * i + (LINE_HEIGHT + FONT_SIZE * 0.7) / 2.0
		draw_string(font, Vector2(box.position.x + READOUT_PAD.x, baseline), lines[i], HORIZONTAL_ALIGNMENT_LEFT,
				-1, FONT_SIZE, colour)
