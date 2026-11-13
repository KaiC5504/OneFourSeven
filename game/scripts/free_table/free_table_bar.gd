class_name FreeTableBar
extends Control
## The free table's buttons (KaiC's M2 phone session: less clutter). A row of small icon buttons
## at the safe area's top-left: the menu, the overview, then Retake and Undo, with Redo only while
## there is something to redo and Done only in edit mode. The menu opens a compact list below it:
## Rack, Empty table, Edit balls, Settings, and the phone link's status in words; the menu button
## carries the link status as a dot. A press outside the open list closes it and does nothing else.
##
## The bar covers the whole canvas but takes no touches itself; only its buttons, and the backdrop
## while the list is open, do.

signal retake_pressed
signal undo_pressed
signal redo_pressed
signal edit_toggled(on: bool)
## "full_rack" or "empty".
signal setup_chosen(name: String)
signal settings_pressed
signal overview_toggled(on: bool)

## 88 canvas units is 44 pt on the phone, the minimum touch target.
const BUTTON_SIZE := Vector2(88.0, 88.0)
const ITEM_SIZE := Vector2(300.0, 88.0)
const GAP := 6
const GROUP_GAP := 14.0
const EDGE_MARGIN := 16.0
const LIST_GAP := 8.0
const FONT_SIZE := 26
const LINK_FONT_SIZE := 22
# iOS can report the safe area late and changes it on rotation without a resize.
const SAFE_AREA_POLL_SECONDS := 1.0
const LINK_STATUS_TEXT := {
	"off": "off",
	"searching": "searching…",
	"connected": "connected",
	"lost": "lost",
}
const LINK_COLOURS := {
	"off": Color(0.62, 0.66, 0.64, 0.55),
	"searching": Color(0.96, 0.72, 0.3),
	"connected": Color(0.42, 0.8, 0.5),
	"lost": Color(0.9, 0.3, 0.25),
}

## When true the bar keeps its row at the safe area's top-left; set false to call place_in.
var auto_place := true

var _buttons := {}
var _icons := {}
var _row: PanelContainer
var _row_box: HBoxContainer
var _list: PanelContainer
var _backdrop: Control
var _link_dot: Icon
var _link_label: Label
var _link_status := "off"


## A button's icon, drawn so no icon font or asset is needed: "menu", "overview", "retake",
## "undo", "redo", "settings" or "dot". `badge`, when not transparent, is a dot at the top right.
class Icon extends Control:
	const TEETH := 8
	var kind := ""
	var colour := Color.WHITE
	var badge := Color.TRANSPARENT

	func _draw() -> void:
		var button := get_parent() as BaseButton
		var c := colour
		if button != null and button.disabled:
			c = Color(c, c.a * 0.3)
		elif button != null and button.button_pressed and button.toggle_mode:
			c = Color(0.04, 0.06, 0.05)
		var centre := size / 2.0
		var r := minf(size.x, size.y) * 0.22
		match kind:
			"menu":
				for i in 3:
					var y := centre.y + (i - 1) * r * 0.75
					draw_line(Vector2(centre.x - r, y), Vector2(centre.x + r, y), c, 3.5, true)
			"overview":
				var upper := PackedVector2Array()
				var lower := PackedVector2Array()
				for i in 17:
					var x := lerpf(-1.2, 1.2, i / 16.0)
					var y := r * 0.7 * (1.0 - pow(x / 1.2, 2.0))
					upper.append(centre + Vector2(x * r, -y))
					lower.append(centre + Vector2(x * r, y))
				draw_polyline(upper, c, 3.0, true)
				draw_polyline(lower, c, 3.0, true)
				draw_circle(centre, r * 0.32, c)
			"retake":
				# A circle round the cue ball with the arrow at its end: the shot again.
				draw_arc(centre, r, deg_to_rad(-60.0), deg_to_rad(240.0), 32, c, 3.0, true)
				_arrow_head(centre + Vector2.from_angle(deg_to_rad(240.0)) * r, deg_to_rad(-30.0), c)
				draw_circle(centre, r * 0.28, c)
			"undo", "redo":
				var side := -1.0 if kind == "undo" else 1.0
				var arc_centre := centre + Vector2(0.0, r * 0.3)
				var radius := r * 0.9
				# Over the top from one side to the other, the arrow coming down at the far end.
				var start := deg_to_rad(20.0) if side < 0.0 else deg_to_rad(160.0)
				var stop := -PI if side < 0.0 else TAU
				draw_arc(arc_centre, radius, start, stop, 24, c, 3.0, true)
				_arrow_head(arc_centre + Vector2(side * radius, 4.0), PI / 2.0, c)
			"dot":
				draw_circle(centre, minf(size.x, size.y) * 0.32, c)
		if badge.a > 0.0:
			var at := Vector2(size.x - 18.0, 18.0)
			draw_circle(at, 7.0, Color(0.05, 0.06, 0.065, 0.9))
			draw_circle(at, 5.0, badge)

	func _arrow_head(at: Vector2, angle: float, c: Color) -> void:
		var back := Vector2.from_angle(angle + PI)
		var side := Vector2(-back.y, back.x)
		draw_colored_polygon(PackedVector2Array([at + Vector2.from_angle(angle) * 6.0,
				at + back * 6.0 + side * 7.0, at + back * 6.0 - side * 7.0]), c)


func _init() -> void:
	mouse_filter = MOUSE_FILTER_IGNORE
	set_anchors_preset(PRESET_FULL_RECT)
	_build()


func _ready() -> void:
	# Before the bar is in the tree its panels have no theme, so _init measured them without their
	# margins and separations.
	_fit()
	Link.status_changed.connect(set_link_status)
	set_link_status(Link.get_status())
	get_viewport().size_changed.connect(_auto_place)
	var poll := Timer.new()
	poll.wait_time = SAFE_AREA_POLL_SECONDS
	poll.timeout.connect(_auto_place)
	add_child(poll)
	poll.start()
	_auto_place()


## "menu", "overview", "retake", "undo", "redo", "done", or a menu item: "rack", "empty", "edit",
## "settings".
func button(id: String) -> Button:
	return _buttons.get(id)


## The row of icon buttons, in canvas units.
func row_rect() -> Rect2:
	return Rect2(_row.position, _row.size)


## The open menu's list, in canvas units.
func list_rect() -> Rect2:
	return Rect2(_list.position, _list.size)


static func link_text(status: String) -> String:
	return "PC link: " + String(LINK_STATUS_TEXT.get(status, status))


static func link_colour(status: String) -> Color:
	return LINK_COLOURS.get(status, LINK_COLOURS["off"])


func set_link_status(status: String) -> void:
	_link_status = status
	(_icons["menu"] as Icon).badge = link_colour(status)
	(_icons["menu"] as Icon).queue_redraw()
	_link_dot.colour = link_colour(status)
	_link_dot.queue_redraw()
	_link_label.text = link_text(status)


func link_status() -> String:
	return _link_status


## Retake and Undo show dimmed when there is nothing to do; Redo only shows when there is.
func set_history(can_undo: bool, can_redo: bool, can_retake: bool) -> void:
	(_buttons["undo"] as Button).disabled = not can_undo
	(_buttons["redo"] as Button).disabled = not can_redo
	(_buttons["retake"] as Button).disabled = not can_retake
	(_buttons["redo"] as Button).visible = can_redo
	for id: String in ["undo", "redo", "retake"]:
		(_icons[id] as Icon).queue_redraw()
	_fit()


func set_edit_mode(on: bool) -> void:
	var edit: Button = _buttons["edit"]
	edit.set_pressed_no_signal(on)
	edit.text = "Stop editing" if on else "Edit balls"
	(_buttons["done"] as Button).visible = on
	_fit()


func set_overview(on: bool) -> void:
	(_buttons["overview"] as Button).set_pressed_no_signal(on)
	(_icons["overview"] as Icon).queue_redraw()


func open_menu() -> void:
	_list.visible = true
	_backdrop.visible = true
	(_buttons["menu"] as Button).set_pressed_no_signal(true)
	(_icons["menu"] as Icon).queue_redraw()


func close_menu() -> void:
	_list.visible = false
	_backdrop.visible = false
	(_buttons["menu"] as Button).set_pressed_no_signal(false)
	(_icons["menu"] as Icon).queue_redraw()


func is_menu_open() -> bool:
	return _list.visible


## Puts the row at the top-left of safe_rect (canvas units) and the list below the menu button.
## Writes only what changed, so a steady poll never makes low-processor mode redraw.
func place_in(safe_rect: Rect2) -> void:
	var at := (safe_rect.position + Vector2(EDGE_MARGIN, EDGE_MARGIN)).round()
	if _row.position != at:
		_row.position = at
	var below := Vector2(at.x, at.y + _row.size.y + LIST_GAP).round()
	if _list.position != below:
		_list.position = below


func _build() -> void:
	theme = SettingsPanel.make_theme()
	_backdrop = Control.new()
	_backdrop.name = "Backdrop"
	_backdrop.set_anchors_preset(PRESET_FULL_RECT)
	_backdrop.mouse_filter = MOUSE_FILTER_STOP
	_backdrop.visible = false
	_backdrop.gui_input.connect(_on_backdrop_input)
	add_child(_backdrop)

	_row = _panel("Row")
	_row_box = HBoxContainer.new()
	_row_box.add_theme_constant_override("separation", GAP)
	_row.add_child(_row_box)
	var menu := _icon_button(_row_box, "menu", "Menu")
	menu.toggle_mode = true
	menu.toggled.connect(func(on: bool) -> void:
		if on:
			open_menu()
		else:
			close_menu())
	var overview := _icon_button(_row_box, "overview", "Whole table")
	overview.toggle_mode = true
	overview.toggled.connect(func(on: bool) -> void:
		(_icons["overview"] as Icon).queue_redraw()
		overview_toggled.emit(on))
	_spacer(_row_box)
	_icon_button(_row_box, "retake", "Retake").pressed.connect(retake_pressed.emit)
	_icon_button(_row_box, "undo", "Undo").pressed.connect(undo_pressed.emit)
	_icon_button(_row_box, "redo", "Redo").pressed.connect(redo_pressed.emit)
	var done := _text_button(_row_box, "done", "Done", Vector2(130.0, BUTTON_SIZE.y))
	done.visible = false
	done.pressed.connect(edit_toggled.emit.bind(false))

	_list = _panel("List")
	_list.visible = false
	var items := VBoxContainer.new()
	items.add_theme_constant_override("separation", GAP)
	_list.add_child(items)
	_text_button(items, "rack", "Rack", ITEM_SIZE).pressed.connect(_from_menu.bind(setup_chosen.emit.bind("full_rack")))
	_text_button(items, "empty", "Empty table", ITEM_SIZE).pressed.connect(_from_menu.bind(setup_chosen.emit.bind("empty")))
	var edit := _text_button(items, "edit", "Edit balls", ITEM_SIZE)
	edit.toggle_mode = true
	edit.toggled.connect(func(on: bool) -> void: _from_menu(edit_toggled.emit.bind(on)))
	_text_button(items, "settings", "Settings", ITEM_SIZE).pressed.connect(_from_menu.bind(settings_pressed.emit))
	var link := HBoxContainer.new()
	link.add_theme_constant_override("separation", 10)
	link.custom_minimum_size.y = 52.0
	_link_dot = Icon.new()
	_link_dot.kind = "dot"
	_link_dot.custom_minimum_size = Vector2(28.0, 28.0)
	_link_dot.size_flags_vertical = SIZE_SHRINK_CENTER
	_link_dot.mouse_filter = MOUSE_FILTER_IGNORE
	link.add_child(_link_dot)
	_link_label = Label.new()
	_link_label.add_theme_font_size_override("font_size", LINK_FONT_SIZE)
	_link_label.add_theme_color_override("font_color", SettingsPanel.MUTED)
	_link_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_link_label.size_flags_vertical = SIZE_FILL
	link.add_child(_link_label)
	items.add_child(link)
	set_history(false, false, false)
	set_link_status("off")
	_fit()


func _panel(panel_name: String) -> PanelContainer:
	var panel := PanelContainer.new()
	panel.name = panel_name
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.05, 0.06, 0.065, 0.55 if panel_name == "Row" else 0.94)
	style.set_corner_radius_all(22)
	style.set_content_margin_all(6.0 if panel_name == "Row" else 10.0)
	panel.add_theme_stylebox_override("panel", style)
	panel.mouse_filter = MOUSE_FILTER_IGNORE
	add_child(panel)
	return panel


func _icon_button(parent: Container, id: String, tooltip: String) -> Button:
	var b := _text_button(parent, id, "", BUTTON_SIZE)
	b.tooltip_text = tooltip
	var icon := Icon.new()
	icon.kind = id
	icon.colour = SettingsPanel.TEXT
	icon.set_anchors_preset(PRESET_FULL_RECT)
	icon.mouse_filter = MOUSE_FILTER_IGNORE
	b.add_child(icon)
	_icons[id] = icon
	return b


func _text_button(parent: Container, id: String, text: String, min_size: Vector2) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = min_size
	# Without focus a keyboard shortcut (Space shoots) can never press a bar button.
	b.focus_mode = FOCUS_NONE
	b.add_theme_font_size_override("font_size", FONT_SIZE)
	_buttons[id] = b
	parent.add_child(b)
	return b


func _spacer(parent: Container) -> void:
	var spacer := Control.new()
	spacer.custom_minimum_size = Vector2(GROUP_GAP, GROUP_GAP)
	spacer.mouse_filter = MOUSE_FILTER_IGNORE
	parent.add_child(spacer)


func _from_menu(action: Callable) -> void:
	close_menu()
	action.call()


func _on_backdrop_input(event: InputEvent) -> void:
	var touch := event as InputEventScreenTouch
	var click := event as InputEventMouseButton
	if (touch != null and touch.pressed) or (click != null and click.pressed and click.device != InputEvent.DEVICE_ID_EMULATION):
		close_menu()
		_backdrop.accept_event()


func _fit() -> void:
	_row.size = _row.get_combined_minimum_size()
	_list.size = _list.get_combined_minimum_size()


func _auto_place() -> void:
	if auto_place and is_inside_tree():
		place_in(SettingsPanel.canvas_safe_rect(self))
