class_name FineTuneWheel
extends Control
## The fine-tune thumb wheel on the right screen edge (spec 8.1). Every PX_PER_STEP canvas units
## of vertical drag, divided by Settings "fine_tune_sensitivity", turn the aim one STEP_DEG step:
## dragging up turns the aim anticlockwise seen from above (to the player's left, +aim_rad). Each
## change is recorded with ShotSetup.add_fine_tune_sample.
##
## Touch, a left mouse drag, or the mouse wheel over it (one step per notch). The ridges move with
## the finger, not by whole steps, so it turns like a wheel; the readout above shows the aim to
## 0.01 degrees. It redraws only when it turns, the aim changes or it is pressed or released.

## steps: signed, + is up; for haptic ticks.
signal stepped(steps: int)

const STEP_DEG := 0.01
const PX_PER_STEP := 6.0

## 60 x 190 pt on the phone: wider than the 44 pt touch target, tall enough for long turns.
const MIN_SIZE := Vector2(120.0, 380.0)
const READOUT_HEIGHT := 46.0
const READOUT_FONT_SIZE := 22
const WHEEL_MARGIN := 14.0
## Surface distance between ridges at the wheel's centre line.
const RIDGE_SPACING := 12.0
const SHADING_BANDS := 24

const PANEL_COLOUR := Color(0.04, 0.05, 0.055, 0.55)
const WHEEL_DARK := Color(0.09, 0.1, 0.11)
const WHEEL_LIGHT := Color(0.42, 0.44, 0.46)
const RIDGE_COLOUR := Color(0.02, 0.02, 0.025, 0.85)
const RIDGE_HIGHLIGHT := Color(1.0, 1.0, 1.0, 0.22)
const MARKER_IDLE := Color(0.85, 0.87, 0.88, 0.8)
const MARKER_PRESSED := Color(1.0, 0.75, 0.25)
const TEXT_COLOUR := Color(0.93, 0.94, 0.95)

const NO_POINTER := -1
const MOUSE_POINTER := -2

## Seconds; tests replace it.
var clock: Callable = func() -> float: return Time.get_ticks_usec() / 1.0e6

var _setup: ShotSetup
var _pointer := NO_POINTER
var _last_y := 0.0
var _remainder := 0.0
var _wheel_notches := 0.0
## How far the drawn surface has turned, in canvas units at the centre line; down is positive.
var _phase := 0.0
var _panel := StyleBoxFlat.new()


func _init() -> void:
	_panel.bg_color = PANEL_COLOUR
	_panel.set_corner_radius_all(22)


func bind(setup: ShotSetup) -> void:
	if _setup != null and _setup.changed.is_connected(_on_setup_changed):
		_setup.changed.disconnect(_on_setup_changed)
	_setup = setup
	if _setup != null:
		_setup.changed.connect(_on_setup_changed)
	queue_redraw()


## Canvas units of drag per step at the current sensitivity.
func units_per_step() -> float:
	return PX_PER_STEP / maxf(float(Settings.get_value("fine_tune_sensitivity")), 0.001)


## Ends a press in progress as if the finger had lifted, without acting on it: a shot
## starting, or the app losing focus, may swallow the real release.
func cancel_press() -> void:
	pass


func is_pressed() -> bool:
	return _pointer != NO_POINTER


func _get_minimum_size() -> Vector2:
	return MIN_SIZE


func _gui_input(event: InputEvent) -> void:
	if _handle(event):
		accept_event()


func _notification(what: int) -> void:
	match what:
		NOTIFICATION_RESIZED:
			queue_redraw()
		NOTIFICATION_APPLICATION_FOCUS_OUT, NOTIFICATION_APPLICATION_PAUSED:
			if _pointer != NO_POINTER:
				_release("focus lost")


func _handle(event: InputEvent) -> bool:
	var touch := event as InputEventScreenTouch
	if touch != null:
		if touch.pressed:
			if _pointer == NO_POINTER:
				_press(touch.index, touch.position.y)
		elif touch.index == _pointer:
			_release("touch cancelled by the system" if touch.canceled else "")
		return true
	var screen_drag := event as InputEventScreenDrag
	if screen_drag != null:
		if screen_drag.index == _pointer:
			_drag_to(screen_drag.position.y)
		return true
	# iOS emulates the mouse from touches; those copies would turn the wheel twice.
	if event is InputEventMouse and event.device == InputEvent.DEVICE_ID_EMULATION:
		return false
	var button := event as InputEventMouseButton
	if button != null:
		match button.button_index:
			MOUSE_BUTTON_LEFT:
				if button.pressed and _pointer == NO_POINTER:
					_press(MOUSE_POINTER, button.position.y)
				elif not button.pressed and _pointer == MOUSE_POINTER:
					_release("")
				return true
			MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN:
				if button.pressed:
					var notches := button.factor if button.factor > 0.0 else 1.0
					_turn_by_notches(notches if button.button_index == MOUSE_BUTTON_WHEEL_UP else -notches)
				return true
		return false
	var motion := event as InputEventMouseMotion
	if motion != null and _pointer == MOUSE_POINTER:
		_drag_to(motion.position.y)
		return true
	return false


func _press(pointer: int, y: float) -> void:
	_pointer = pointer
	_last_y = y
	queue_redraw()


## reason is empty for an ordinary release. The steps already taken stay: each one was applied
## as it happened.
func _release(reason: String) -> void:
	_pointer = NO_POINTER
	if not reason.is_empty():
		Log.info("input", "input.cancel", {"control": "fine_tune", "reason": reason})
	queue_redraw()


func _drag_to(y: float) -> void:
	var dy := y - _last_y
	_last_y = y
	if dy == 0.0:
		return
	_phase += dy
	_remainder -= dy
	var per_step := units_per_step()
	# Rounding in the summed moves must not leave a whole step a hair short.
	var steps := int(_remainder / per_step + signf(_remainder) * 1e-9)
	_remainder -= steps * per_step
	_apply_steps(steps)
	queue_redraw()


func _turn_by_notches(notches: float) -> void:
	_wheel_notches += notches
	var steps := int(_wheel_notches)
	_wheel_notches -= steps
	_phase -= steps * units_per_step()
	_apply_steps(steps)
	queue_redraw()


func _apply_steps(steps: int) -> void:
	if steps == 0:
		return
	if _setup != null:
		var step_rad := deg_to_rad(STEP_DEG) * signi(steps)
		for i in absi(steps):
			_setup.add_aim(step_rad)
			_setup.add_fine_tune_sample(clock.call(), step_rad)
	stepped.emit(steps)


func _on_setup_changed(what: StringName) -> void:
	if what == &"aim" or what == &"reset":
		queue_redraw()


func _draw() -> void:
	draw_style_box(_panel, Rect2(Vector2.ZERO, size))
	_draw_readout()
	var body := Rect2(WHEEL_MARGIN, READOUT_HEIGHT, size.x - WHEEL_MARGIN * 2.0,
			size.y - READOUT_HEIGHT - WHEEL_MARGIN)
	if body.size.x <= 0.0 or body.size.y <= 0.0:
		return
	_draw_wheel(body)


func _draw_readout() -> void:
	var font := get_theme_default_font()
	var aim_deg := rad_to_deg(_setup.aim_rad) if _setup != null else 0.0
	var text := "%.2f°" % aim_deg
	var width := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, READOUT_FONT_SIZE).x
	var baseline := (READOUT_HEIGHT + font.get_ascent(READOUT_FONT_SIZE) - font.get_descent(READOUT_FONT_SIZE)) / 2.0
	draw_string(font, Vector2((size.x - width) / 2.0, baseline), text, HORIZONTAL_ALIGNMENT_LEFT,
			-1, READOUT_FONT_SIZE, TEXT_COLOUR)


## A cylinder seen side-on: shading and ridge spacing follow the cosine of the angle round it.
func _draw_wheel(body: Rect2) -> void:
	var radius := body.size.y / 2.0
	var centre_y := body.position.y + radius
	var left := body.position.x
	var right := body.end.x
	for i in SHADING_BANDS:
		var a0 := -PI / 2.0 + PI * i / SHADING_BANDS
		var a1 := -PI / 2.0 + PI * (i + 1) / SHADING_BANDS
		var y0 := centre_y + radius * sin(a0)
		var y1 := centre_y + radius * sin(a1)
		var light := cos((a0 + a1) / 2.0)
		draw_rect(Rect2(left, y0, right - left, y1 - y0 + 0.5), WHEEL_DARK.lerp(WHEEL_LIGHT, light))
	var spacing := RIDGE_SPACING / radius
	var angle := -PI / 2.0 + fposmod(_phase / radius, spacing)
	while angle < PI / 2.0:
		var y := centre_y + radius * sin(angle)
		var facing := cos(angle)
		var width := maxf(1.0, 3.0 * facing)
		draw_line(Vector2(left + 3.0, y), Vector2(right - 3.0, y), RIDGE_COLOUR, width)
		draw_line(Vector2(left + 3.0, y + width), Vector2(right - 3.0, y + width),
				RIDGE_HIGHLIGHT * Color(1.0, 1.0, 1.0, facing), 1.0)
		angle += spacing
	var marker := MARKER_PRESSED if is_pressed() else MARKER_IDLE
	var half := 9.0
	draw_colored_polygon(PackedVector2Array([Vector2(left - 2.0, centre_y - half),
			Vector2(left + half, centre_y), Vector2(left - 2.0, centre_y + half)]), marker)
	draw_colored_polygon(PackedVector2Array([Vector2(right + 2.0, centre_y - half),
			Vector2(right - half, centre_y), Vector2(right + 2.0, centre_y + half)]), marker)
