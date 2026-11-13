class_name ElevationControl
extends Control
## Cue elevation (spec 8.2): from the obstruction minimum (spec 7.3) up to max_elevation_rad,
## near vertical for masse. Shows when the minimum has forced the cue well above what was asked
## for (shows_forced).
##
## A slim vertical track above the spin ball, 0 at the bottom, its readout on top. The whole
## control takes touches; only the track is drawn. The scale is a power curve whose lower half covers
## 0-HALF_TRAVEL_DEG, so the usual cueing angles get half the travel. The player can ask for less
## than the obstruction minimum; the track then shows the minimum and the raised elevation.

signal elevation_changed(rad: float)

const HALF_TRAVEL_DEG := 20.0
const SHOWN_FORCED_DEG := 3.0
const PAD_TOP := 64.0
const PAD_BOTTOM := 18.0
const GRAB_RADIUS := 44.0
const MIN_SIZE := Vector2(88.0, 280.0)
const FONT_SIZE := 28
const SMALL_FONT_SIZE := 24
const TRACK_PANEL_WIDTH := 40.0
const TICKS_DEG: Array[float] = [0.0, 10.0, 20.0, 40.0, 60.0, 80.0]

const PANEL_COLOUR := Color(0.05, 0.06, 0.07, 0.5)
const SHADOW_COLOUR := Color(0.0, 0.0, 0.0, 0.7)
const TRACK_COLOUR := Color(1.0, 1.0, 1.0, 0.22)
const TEXT_COLOUR := Color(0.9, 0.92, 0.9, 0.92)
const FORCED_COLOUR := Color(0.96, 0.66, 0.24)

const _NO_POINTER := -100
const _MOUSE := -2

var _setup: ShotSetup
var _elevation := 0.0
var _pointer := _NO_POINTER
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
	queue_redraw()


## Ends a press in progress as if the finger had lifted, without acting on it: a shot
## starting, or the app losing focus, may swallow the real release.
func cancel_press(reason: String = "cancelled by the game") -> void:
	if _pointer == _NO_POINTER:
		return
	_pointer = _NO_POINTER
	Log.info("input", "input.cancel", {"control": "elevation", "reason": reason})


## ShotSetup.elevation_forced(): the minimum is above the request by any amount.
func is_forced() -> bool:
	return _setup != null and _setup.elevation_forced()


## Drawn as forced (amber, "raised to") only when the minimum is SHOWN_FORCED_DEG or more above
## the request: the cue clearing the cushion rail lifts every baulk-line shot about 2°, which is
## not worth a warning.
func shows_forced() -> bool:
	return is_forced() and _setup.elevation_min_rad - _setup.elevation_rad >= deg_to_rad(SHOWN_FORCED_DEG) - 1e-12


## What the player asked for.
func requested() -> float:
	return _setup.elevation_rad if _setup != null else _elevation


## What the cue will be at: the request, or the obstruction minimum when that is higher.
func effective() -> float:
	return _setup.effective_elevation() if _setup != null else _elevation


## Elevation for a fraction 0-1 of the travel from the bottom.
static func elevation_for_travel(travel: float, max_rad: float) -> float:
	return max_rad * pow(clampf(travel, 0.0, 1.0), _exponent(max_rad))


static func travel_for_elevation(rad: float, max_rad: float) -> float:
	if max_rad <= 0.0:
		return 0.0
	return pow(clampf(rad / max_rad, 0.0, 1.0), 1.0 / _exponent(max_rad))


# max_rad * 0.5^p = HALF_TRAVEL_DEG; p is 2 for the 80° default.
static func _exponent(max_rad: float) -> float:
	var half := deg_to_rad(HALF_TRAVEL_DEG)
	if max_rad <= half:
		return 1.0
	return log(max_rad / half) / log(2.0)


func elevation_for_local(local: Vector2) -> float:
	var span := _track_bottom() - _track_top()
	if span <= 0.0:
		return 0.0
	return elevation_for_travel((_track_bottom() - local.y) / span, _max_rad())


func local_y_for_elevation(rad: float) -> float:
	return _track_bottom() - travel_for_elevation(rad, _max_rad()) * (_track_bottom() - _track_top())


## The readout: "12°", or "raised to 34°" while shows_forced().
func readout_text() -> String:
	var value := _degrees_text(effective())
	return "raised to " + value if shows_forced() else value


static func _degrees_text(rad: float) -> String:
	var deg := rad_to_deg(rad)
	return "%.1f°" % deg if deg < 9.95 else "%d°" % roundi(deg)


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouse and event.device == InputEvent.DEVICE_ID_EMULATION:
		return
	if event is InputEventScreenTouch:
		var touch := event as InputEventScreenTouch
		if touch.pressed:
			_on_press(touch.index, touch.position)
		elif touch.index == _pointer:
			_pointer = _NO_POINTER
	elif event is InputEventScreenDrag:
		var drag := event as InputEventScreenDrag
		_on_move(drag.index, drag.position)
	elif event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		if button.button_index != MOUSE_BUTTON_LEFT:
			return
		if button.pressed:
			_on_press(_MOUSE, button.position)
		elif _pointer == _MOUSE:
			_pointer = _NO_POINTER
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
	if _pointer != _NO_POINTER:
		return
	_pointer = pointer
	var thumb := local_y_for_elevation(requested())
	# Grabbing the thumb keeps it under the finger; anywhere else on the track jumps there.
	_grab_offset = thumb - pos.y if absf(pos.y - thumb) <= GRAB_RADIUS else 0.0
	_apply(elevation_for_local(Vector2(pos.x, pos.y + _grab_offset)))


func _on_move(pointer: int, pos: Vector2) -> void:
	if pointer != _pointer:
		return
	_apply(elevation_for_local(Vector2(pos.x, pos.y + _grab_offset)))


func _apply(rad: float) -> void:
	if is_equal_approx(rad, requested()):
		return
	_elevation = rad
	if _setup != null:
		_setup.set_elevation(rad)
	elevation_changed.emit(rad)
	queue_redraw()


func _max_rad() -> float:
	return Physics.param("max_elevation_rad", deg_to_rad(80.0))


func _track_top() -> float:
	return PAD_TOP


func _track_bottom() -> float:
	return size.y - PAD_BOTTOM


func _on_setup_changed(what: StringName) -> void:
	if what == &"elevation" or what == &"elevation_min" or what == &"reset":
		queue_redraw()


func _draw() -> void:
	var x := size.x / 2.0
	var top := _track_top()
	var bottom := _track_bottom()
	if bottom <= top:
		return
	var panel := StyleBoxFlat.new()
	panel.bg_color = PANEL_COLOUR
	panel.set_corner_radius_all(int(TRACK_PANEL_WIDTH / 2.0))
	draw_style_box(panel, Rect2(x - TRACK_PANEL_WIDTH / 2.0, top - 14.0, TRACK_PANEL_WIDTH, bottom - top + 28.0))
	draw_line(Vector2(x, top), Vector2(x, bottom), TRACK_COLOUR, 4.0, true)
	for deg in TICKS_DEG:
		var y := local_y_for_elevation(deg_to_rad(deg))
		var half := 10.0 if is_equal_approx(deg, HALF_TRAVEL_DEG) else 6.0
		draw_line(Vector2(x - half, y), Vector2(x + half, y), TRACK_COLOUR, 2.0, true)
	var forced := shows_forced()
	var thumb_colour := TEXT_COLOUR
	if forced:
		var min_y := local_y_for_elevation(_setup.elevation_min_rad)
		draw_line(Vector2(x, bottom), Vector2(x, min_y), Color(FORCED_COLOUR, 0.35), 8.0, true)
		draw_line(Vector2(x - 22.0, min_y), Vector2(x + 22.0, min_y), FORCED_COLOUR, 3.0, true)
		var asked := local_y_for_elevation(requested())
		draw_arc(Vector2(x, asked), 8.0, 0.0, TAU, 32, Color(1.0, 1.0, 1.0, 0.45), 2.0, true)
		thumb_colour = FORCED_COLOUR
	var thumb_y := local_y_for_elevation(effective())
	draw_rect(Rect2(x - 20.0, thumb_y - 6.0, 40.0, 12.0), thumb_colour)

	var font := get_theme_default_font()
	var line := readout_text()
	var font_size := SMALL_FONT_SIZE if forced else FONT_SIZE
	var colour := FORCED_COLOUR if forced else TEXT_COLOUR
	# Centred over the track; a forced readout may run wider than a slim control.
	var width := font.get_string_size(line, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
	var at := Vector2(x - width / 2.0, 38.0)
	draw_string(font, at + Vector2(1.5, 1.5), line, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, SHADOW_COLOUR)
	draw_string(font, at, line, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, colour)
