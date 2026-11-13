class_name PowerBar
extends Control
## The power bar (spec 8.3): set power on a slider with a non-linear scale for fine low-power
## control, then tap Shoot.
##
## Vertical: the readout at the top, the slider (0 at the bottom) and the Shoot button below it.
## Shoot fires on release over the button, and is inert at zero power. When the strike mode
## switches to the power bar, the setup's speed is set from the bar, starting at
## ShotSetup.DEFAULT_PACE_M_S if no power has been set yet.

signal power_changed(power: float)
signal shoot_pressed

const POWER_EXPONENT := 2.0
const PAD := 18.0
const READOUT_HEIGHT := 86.0
const SHOOT_HEIGHT := 96.0
const GRAB_RADIUS := 44.0
const MIN_SIZE := Vector2(140.0, 420.0)
const FONT_SIZE := 30
const SMALL_FONT_SIZE := 22

const PANEL_COLOUR := Color(0.05, 0.06, 0.07, 0.55)
const TRACK_COLOUR := Color(1.0, 1.0, 1.0, 0.16)
const FILL_COLOUR := Color(0.96, 0.72, 0.3, 0.85)
const TEXT_COLOUR := Color(0.9, 0.92, 0.9, 0.92)

enum Press { NONE, SLIDER, SHOOT }

const _NO_POINTER := -100
const _MOUSE := -2

var _setup: ShotSetup
var _power := 0.0
var _pointer := _NO_POINTER
var _press := Press.NONE
var _grab_offset := 0.0


func _ready() -> void:
	mouse_filter = MOUSE_FILTER_STOP
	custom_minimum_size = MIN_SIZE


func bind(setup: ShotSetup) -> void:
	if _setup != null and _setup.changed.is_connected(_on_setup_changed):
		_setup.changed.disconnect(_on_setup_changed)
	_setup = setup
	if _setup != null:
		_setup.changed.connect(_on_setup_changed)
		_power = _setup.power
		if _setup.strike_mode == ShotSetup.POWER_BAR:
			_apply_mode()
	queue_redraw()


## Cue speed for a bar position 0-1: 0 at 0, max_speed_m_s at 1, finer at the low end.
static func speed_for_power(power: float, max_speed_m_s: float) -> float:
	return max_speed_m_s * pow(clampf(power, 0.0, 1.0), POWER_EXPONENT)


static func power_for_speed(speed_m_s: float, max_speed_m_s: float) -> float:
	if max_speed_m_s <= 0.0:
		return 0.0
	return pow(clampf(speed_m_s / max_speed_m_s, 0.0, 1.0), 1.0 / POWER_EXPONENT)


func power() -> float:
	return _setup.power if _setup != null else _power


func speed() -> float:
	return speed_for_power(power(), _max_speed())


## Moves the bar and writes the setup's speed, as a drag would.
func set_power(value: float) -> void:
	value = clampf(value, 0.0, 1.0)
	var moved := not is_equal_approx(value, power())
	_power = value
	if _setup != null:
		_setup.set_speed(speed_for_power(value, _max_speed()), value)
	queue_redraw()
	if moved:
		power_changed.emit(value)


func can_shoot() -> bool:
	return power() > 0.0


func power_for_local(local: Vector2) -> float:
	var span := _track_bottom() - _track_top()
	if span <= 0.0:
		return 0.0
	return clampf((_track_bottom() - local.y) / span, 0.0, 1.0)


func local_y_for_power(value: float) -> float:
	return _track_bottom() - clampf(value, 0.0, 1.0) * (_track_bottom() - _track_top())


func shoot_rect() -> Rect2:
	return Rect2(PAD, size.y - SHOOT_HEIGHT - PAD / 2.0, size.x - PAD * 2.0, SHOOT_HEIGHT - PAD / 2.0)


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
	if what == NOTIFICATION_VISIBILITY_CHANGED and not is_visible_in_tree():
		_end_press()


func _on_press(pointer: int, pos: Vector2) -> void:
	if _pointer != _NO_POINTER:
		return
	_pointer = pointer
	if shoot_rect().has_point(pos):
		_press = Press.SHOOT
		queue_redraw()
		return
	_press = Press.SLIDER
	var thumb := local_y_for_power(power())
	_grab_offset = thumb - pos.y if absf(pos.y - thumb) <= GRAB_RADIUS else 0.0
	set_power(power_for_local(Vector2(pos.x, pos.y + _grab_offset)))


func _on_move(pointer: int, pos: Vector2) -> void:
	if pointer != _pointer or _press != Press.SLIDER:
		return
	set_power(power_for_local(Vector2(pos.x, pos.y + _grab_offset)))


func _on_release(pointer: int, pos: Vector2, canceled: bool) -> void:
	if pointer != _pointer:
		return
	var shoot := _press == Press.SHOOT and not canceled and shoot_rect().has_point(pos)
	_end_press()
	if shoot and can_shoot():
		shoot_pressed.emit()


func _end_press() -> void:
	if _press == Press.SHOOT:
		queue_redraw()
	_pointer = _NO_POINTER
	_press = Press.NONE


func _apply_mode() -> void:
	var value := power()
	if value <= 0.0:
		value = power_for_speed(ShotSetup.DEFAULT_PACE_M_S, _max_speed())
	# Always written: the setup's speed may still be the last stroke's.
	set_power(value)


func _max_speed() -> float:
	return Physics.param("max_cue_speed_m_s", 10.0)


func _track_top() -> float:
	return READOUT_HEIGHT + PAD


func _track_bottom() -> float:
	return size.y - SHOOT_HEIGHT - PAD * 2.0


func _on_setup_changed(what: StringName) -> void:
	if what == &"mode" and _setup.strike_mode == ShotSetup.POWER_BAR:
		_apply_mode()
	elif what == &"speed" or what == &"reset":
		_power = _setup.power
		queue_redraw()


func _draw() -> void:
	_draw_rounded(Rect2(Vector2.ZERO, size), PANEL_COLOUR)
	var font := get_theme_default_font()
	var lines := readout_text().split("\n")
	draw_string(font, Vector2(0.0, 40.0), lines[0], HORIZONTAL_ALIGNMENT_CENTER, size.x, FONT_SIZE, TEXT_COLOUR)
	draw_string(font, Vector2(0.0, 72.0), lines[1], HORIZONTAL_ALIGNMENT_CENTER, size.x, SMALL_FONT_SIZE, Color(TEXT_COLOUR, 0.7))

	var x := size.x / 2.0
	var top := _track_top()
	var bottom := _track_bottom()
	if bottom > top:
		draw_rect(Rect2(x - 7.0, top, 14.0, bottom - top), TRACK_COLOUR)
		var thumb_y := local_y_for_power(power())
		draw_rect(Rect2(x - 7.0, thumb_y, 14.0, bottom - thumb_y), FILL_COLOUR)
		draw_rect(Rect2(x - 28.0, thumb_y - 8.0, 56.0, 16.0), TEXT_COLOUR)

	var button := shoot_rect()
	var enabled := can_shoot()
	var fill := FILL_COLOUR if enabled else Color(1.0, 1.0, 1.0, 0.12)
	if _press == Press.SHOOT:
		fill = fill.darkened(0.25)
	_draw_rounded(button, fill)
	var text_colour := Color(0.08, 0.07, 0.05) if enabled else Color(TEXT_COLOUR, 0.4)
	var baseline := button.position.y + (button.size.y + FONT_SIZE * 0.7) / 2.0
	draw_string(font, Vector2(button.position.x, baseline), "Shoot", HORIZONTAL_ALIGNMENT_CENTER, button.size.x, FONT_SIZE, text_colour)


func _draw_rounded(rect: Rect2, colour: Color) -> void:
	var box := StyleBoxFlat.new()
	box.bg_color = colour
	box.set_corner_radius_all(18)
	draw_style_box(box, rect)
