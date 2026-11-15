extends RefCounted
## What the match's HUD pieces share: the engine's Dictionaries (serialize.hpp) read without
## trusting them, and the look of a panel over the table.
##
## A key that is missing or of the wrong type gives the fallback and logs app.error once for that
## piece and key, so a contract that drifted shows in the log from one occurrence and not once
## for every frame shown.

const PANEL := Color(0.05, 0.06, 0.065)
const PANEL_ALPHA := 0.62
const CORNER := 22

static var _reported := {}


static func integer(data: Dictionary, key: String, who: String, fallback: int = -1) -> int:
	var value: Variant = data.get(key)
	if _is_number(value):
		return int(value)
	missing(who, key)
	return fallback


## The whole number at `index` of the list under `key`.
static func integer_at(data: Dictionary, key: String, index: int, who: String, fallback: int = -1) -> int:
	var list: Variant = data.get(key)
	if list is Array and index >= 0 and index < (list as Array).size() and _is_number((list as Array)[index]):
		return int((list as Array)[index])
	missing(who, key)
	return fallback


static func text(data: Dictionary, key: String, who: String, fallback: String = "") -> String:
	var value: Variant = data.get(key)
	if value is String or value is StringName:
		return str(value)
	missing(who, key)
	return fallback


static func flag(data: Dictionary, key: String, who: String, fallback: bool = false) -> bool:
	var value: Variant = data.get(key)
	if value is bool:
		return value
	missing(who, key)
	return fallback


static func dictionary(data: Dictionary, key: String, who: String) -> Dictionary:
	var value: Variant = data.get(key)
	if value is Dictionary:
		return value
	missing(who, key)
	return {}


static func missing(who: String, key: String) -> void:
	problem("%s has no usable \"%s\"" % [who, key])


## Logs `message` as app.error the first time it is given.
static func problem(message: String) -> void:
	if _reported.has(message):
		return
	_reported[message] = true
	Log.warn("app", "app.error", {"kind": "message", "message": message})


## Tests: lets a problem already reported be reported again.
static func forget_reported() -> void:
	_reported.clear()


static func panel_style(alpha: float = PANEL_ALPHA, radius: int = CORNER) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = Color(PANEL, alpha)
	style.set_corner_radius_all(radius)
	style.anti_aliasing = true
	return style


static func button(label: String, min_size: Vector2, font_size: int) -> Button:
	var b := Button.new()
	b.text = label
	b.custom_minimum_size = min_size
	# Without focus a keyboard shortcut (Space shoots) can never press it.
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_font_size_override("font_size", font_size)
	return b


## Writes only what changed, so a steady safe-area poll never makes low-processor mode redraw.
static func put(control: Control, at: Vector2, extent: Vector2) -> void:
	if control.size != extent:
		control.size = extent
	if control.position != at:
		control.position = at


static func _is_number(value: Variant) -> bool:
	return value is int or (value is float and is_finite(value))
