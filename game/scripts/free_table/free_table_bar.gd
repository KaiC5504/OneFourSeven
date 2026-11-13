class_name FreeTableBar
extends Control
## The free table's buttons: Retake, Undo, Redo, Edit, Rack, Empty, Settings. A compact column at
## the left edge of the safe area (a row when vertical is false), in SettingsPanel's look.

signal retake_pressed
signal undo_pressed
signal redo_pressed
signal edit_toggled(on: bool)
## "full_rack" or "empty".
signal setup_chosen(name: String)
signal settings_pressed

## 88 canvas units is 44 pt on the phone, the minimum touch target.
const BUTTON_SIZE := Vector2(112.0, 88.0)
const GAP := 6
const GROUP_GAP := 14.0
const EDGE_MARGIN := 16.0
const FONT_SIZE := 24
# iOS can report the safe area late and changes it on rotation without a resize.
const SAFE_AREA_POLL_SECONDS := 1.0

## When true the bar keeps itself at the safe area's left edge (top edge for a row), centred;
## set false to call place_in.
var auto_place := true

var _buttons := {}
var _box: BoxContainer
var _panel: PanelContainer


## The gear on the settings button, drawn so no icon font or asset is needed.
class GearIcon extends Control:
	const TEETH := 8
	var colour := Color.WHITE

	func _draw() -> void:
		var centre := size / 2.0
		var outer := minf(size.x, size.y) * 0.36
		var inner := outer * 0.74
		var points := PackedVector2Array()
		for i in TEETH * 4:
			var angle := TAU * (float(i) - 0.5) / float(TEETH * 4)
			var r := outer if (i % 4) < 2 else inner
			points.append(centre + Vector2(cos(angle), sin(angle)) * r)
		points.append(points[0])
		draw_polyline(points, colour, 3.0, true)
		draw_arc(centre, outer * 0.36, 0.0, TAU, 32, colour, 3.0, true)


func _init() -> void:
	mouse_filter = MOUSE_FILTER_IGNORE
	_build()


func _ready() -> void:
	get_viewport().size_changed.connect(_auto_place)
	var poll := Timer.new()
	poll.wait_time = SAFE_AREA_POLL_SECONDS
	poll.timeout.connect(_auto_place)
	add_child(poll)
	poll.start()
	_auto_place()


## "retake", "undo", "redo", "edit", "rack", "empty" or "settings".
func button(id: String) -> Button:
	return _buttons.get(id)


func set_history(can_undo: bool, can_redo: bool, can_retake: bool) -> void:
	(_buttons["undo"] as Button).disabled = not can_undo
	(_buttons["redo"] as Button).disabled = not can_redo
	(_buttons["retake"] as Button).disabled = not can_retake


func set_edit_mode(on: bool) -> void:
	(_buttons["edit"] as Button).set_pressed_no_signal(on)


func set_vertical(on: bool) -> void:
	_box.vertical = on
	_fit()
	_auto_place()


## Puts the bar at the left edge of safe_rect (canvas units), centred vertically; a row goes at
## the top edge, centred horizontally. Writes only what changed, so a steady poll never makes
## low-processor mode redraw.
func place_in(safe_rect: Rect2) -> void:
	var target: Vector2
	if _box.vertical:
		target = Vector2(safe_rect.position.x + EDGE_MARGIN, safe_rect.get_center().y - size.y / 2.0)
	else:
		target = Vector2(safe_rect.get_center().x - size.x / 2.0, safe_rect.position.y + EDGE_MARGIN)
	target = target.round()
	if position != target:
		position = target


func _build() -> void:
	theme = SettingsPanel.make_theme()
	_panel = PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.05, 0.06, 0.065, 0.62)
	style.set_corner_radius_all(22)
	style.set_content_margin_all(8.0)
	_panel.add_theme_stylebox_override("panel", style)
	_panel.mouse_filter = MOUSE_FILTER_IGNORE
	add_child(_panel)
	_box = BoxContainer.new()
	_box.vertical = true
	_box.add_theme_constant_override("separation", GAP)
	_panel.add_child(_box)

	_add("retake", "Retake").pressed.connect(retake_pressed.emit)
	_add("undo", "Undo").pressed.connect(undo_pressed.emit)
	_add("redo", "Redo").pressed.connect(redo_pressed.emit)
	_spacer()
	var edit := _add("edit", "Edit")
	edit.toggle_mode = true
	edit.toggled.connect(edit_toggled.emit)
	_spacer()
	_add("rack", "Rack").pressed.connect(setup_chosen.emit.bind("full_rack"))
	_add("empty", "Empty").pressed.connect(setup_chosen.emit.bind("empty"))
	_spacer()
	var settings := _add("settings", "")
	settings.tooltip_text = "Settings"
	settings.pressed.connect(settings_pressed.emit)
	var gear := GearIcon.new()
	gear.colour = SettingsPanel.TEXT
	gear.set_anchors_preset(PRESET_FULL_RECT)
	gear.mouse_filter = MOUSE_FILTER_IGNORE
	settings.add_child(gear)
	set_history(false, false, false)
	_fit()


func _add(id: String, text: String) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = BUTTON_SIZE
	# Without focus a keyboard shortcut (Space shoots) can never press a bar button.
	b.focus_mode = FOCUS_NONE
	b.add_theme_font_size_override("font_size", FONT_SIZE)
	_buttons[id] = b
	_box.add_child(b)
	return b


func _spacer() -> void:
	var spacer := Control.new()
	spacer.custom_minimum_size = Vector2(GROUP_GAP, GROUP_GAP)
	spacer.mouse_filter = MOUSE_FILTER_IGNORE
	_box.add_child(spacer)


func _fit() -> void:
	size = _panel.get_combined_minimum_size()
	_panel.size = size


func _auto_place() -> void:
	if auto_place and is_inside_tree():
		place_in(SettingsPanel.canvas_safe_rect(self))
