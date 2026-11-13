class_name SettingsPanel
extends Control
## The feel build's settings (spec 15, the keys in Settings.DEFAULTS): strike mode, guide level,
## sensitivities, stroke wander, spin reset, volume and haptics. Every change goes through
## Settings.set_value, and the controls follow Settings.changed. A modal card in the middle of
## the safe area; tapping outside it closes it, like Close.

signal closed

const SettingsStore := preload("res://scripts/autoload/settings.gd")

## Canvas units: the canvas is 880 tall, 1320 px on the phone, so 2 units are 1 pt and 88 units
## the 44 pt minimum touch target.
const TOUCH := 88.0
## Narrow enough that the card, centred in the phone's safe area, clears FreeTableBar's column.
const COLUMN_WIDTH := 580.0
const TEXT := Color(0.88, 0.9, 0.88)
const MUTED := Color(0.62, 0.66, 0.64)
const ACCENT := Color(0.42, 0.75, 0.55)
const CARD := Color(0.07, 0.08, 0.085, 0.97)
const GUIDE_TEXT := ["Cue only", "Aim line and ghost ball", "Plus the object ball's line", "Predicted paths"]

## When true the panel centres itself in the display's safe area; set false to call place_in.
var auto_place := true

var _store: SettingsStore
var _controls := {}
var _values := {}
var _dragging := {}
var _card: PanelContainer


func _init() -> void:
	visible = false
	set_anchors_preset(PRESET_FULL_RECT)
	_build()


func _ready() -> void:
	if _store == null:
		bind_settings(Settings)
	get_viewport().size_changed.connect(_auto_place)


## The settings this panel edits; the Settings autoload unless a test binds another.
func bind_settings(store: SettingsStore) -> void:
	if _store != null and _store.changed.is_connected(_on_settings_changed):
		_store.changed.disconnect(_on_settings_changed)
	_store = store
	_store.changed.connect(_on_settings_changed)
	_refresh_all()


func open() -> void:
	_refresh_all()
	visible = true
	_auto_place()


func close() -> void:
	if not visible:
		return
	visible = false
	closed.emit()


## The control for a setting: "<key>:<value>" for a choice button ("strike_mode:power_bar",
## "guide_level:2"), "<key>" for a slider or switch, "close" for Close.
func control(id: String) -> Control:
	return _controls.get(id)


## Centres the card in safe_rect (canvas units), shrinking it to fit if it must.
func place_in(safe_rect: Rect2) -> void:
	var wanted := _card.get_combined_minimum_size()
	var card_size := wanted.min(safe_rect.size)
	var card_position := (safe_rect.position + (safe_rect.size - card_size) / 2.0).round()
	if _card.size != card_size:
		_card.size = card_size
	if _card.position != card_position:
		_card.position = card_position


## The display's safe area in this control's canvas units, clipped to the window.
static func canvas_safe_rect(node: Control) -> Rect2:
	var canvas := node.get_viewport_rect().size
	var window := node.get_window()
	var window_rect := Rect2(Vector2(window.position), Vector2(window.size))
	if not window_rect.has_area():
		return Rect2(Vector2.ZERO, canvas)
	var inside := Rect2(DisplayServer.get_display_safe_area()).intersection(window_rect)
	if not inside.has_area():
		inside = window_rect
	var scale := canvas / window_rect.size
	return Rect2((inside.position - window_rect.position) * scale, inside.size * scale)


## The look the free table's controls share: dark, translucent, rounded, no focus rings.
static func make_theme() -> Theme:
	var theme := Theme.new()
	theme.default_font_size = 30
	theme.set_color("font_color", "Label", TEXT)
	theme.set_stylebox("normal", "Button", _box(Color(1.0, 1.0, 1.0, 0.07), 16))
	theme.set_stylebox("hover", "Button", _box(Color(1.0, 1.0, 1.0, 0.12), 16))
	theme.set_stylebox("pressed", "Button", _box(ACCENT, 16))
	theme.set_stylebox("hover_pressed", "Button", _box(ACCENT.lightened(0.1), 16))
	theme.set_stylebox("disabled", "Button", _box(Color(1.0, 1.0, 1.0, 0.025), 16))
	theme.set_stylebox("focus", "Button", StyleBoxEmpty.new())
	theme.set_color("font_color", "Button", TEXT)
	theme.set_color("font_hover_color", "Button", TEXT)
	theme.set_color("font_focus_color", "Button", TEXT)
	theme.set_color("font_pressed_color", "Button", Color(0.04, 0.06, 0.05))
	theme.set_color("font_hover_pressed_color", "Button", Color(0.04, 0.06, 0.05))
	theme.set_color("font_disabled_color", "Button", Color(TEXT, 0.3))
	var track := _box(Color(1.0, 1.0, 1.0, 0.14), 4)
	track.content_margin_top = 4.0
	track.content_margin_bottom = 4.0
	var filled := _box(ACCENT, 4)
	filled.content_margin_top = 4.0
	filled.content_margin_bottom = 4.0
	theme.set_stylebox("slider", "HSlider", track)
	theme.set_stylebox("grabber_area", "HSlider", filled)
	theme.set_stylebox("grabber_area_highlight", "HSlider", filled)
	theme.set_stylebox("focus", "HSlider", StyleBoxEmpty.new())
	var grabber := _disc(44, TEXT)
	theme.set_icon("grabber", "HSlider", grabber)
	theme.set_icon("grabber_highlight", "HSlider", _disc(44, Color.WHITE))
	theme.set_icon("grabber_disabled", "HSlider", _disc(44, MUTED))
	return theme


static func _box(colour: Color, radius: int) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = colour
	box.set_corner_radius_all(radius)
	box.anti_aliasing = true
	box.content_margin_left = 16.0
	box.content_margin_right = 16.0
	return box


static func _disc(diameter: int, colour: Color) -> GradientTexture2D:
	var gradient := Gradient.new()
	gradient.offsets = PackedFloat32Array([0.0, 0.9, 1.0])
	gradient.colors = PackedColorArray([colour, colour, Color(colour, 0.0)])
	var texture := GradientTexture2D.new()
	texture.gradient = gradient
	texture.fill = GradientTexture2D.FILL_RADIAL
	texture.fill_from = Vector2(0.5, 0.5)
	texture.fill_to = Vector2(1.0, 0.5)
	texture.width = diameter
	texture.height = diameter
	return texture


func _build() -> void:
	theme = make_theme()
	var backdrop := ColorRect.new()
	backdrop.color = Color(0.0, 0.0, 0.0, 0.5)
	backdrop.set_anchors_preset(PRESET_FULL_RECT)
	backdrop.mouse_filter = MOUSE_FILTER_STOP
	backdrop.gui_input.connect(_on_backdrop_input)
	add_child(backdrop)

	_card = PanelContainer.new()
	var card_style := _box(CARD, 28)
	card_style.content_margin_left = 44.0
	card_style.content_margin_right = 44.0
	card_style.content_margin_top = 24.0
	card_style.content_margin_bottom = 32.0
	_card.add_theme_stylebox_override("panel", card_style)
	add_child(_card)
	var body := VBoxContainer.new()
	body.add_theme_constant_override("separation", 20)
	_card.add_child(body)

	var header := HBoxContainer.new()
	var title := Label.new()
	title.text = "Settings"
	title.add_theme_font_size_override("font_size", 40)
	title.size_flags_horizontal = SIZE_EXPAND_FILL
	title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	header.add_child(title)
	var close_button := _button("Close", Vector2(170.0, TOUCH))
	close_button.pressed.connect(close)
	_controls["close"] = close_button
	header.add_child(close_button)
	body.add_child(header)

	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 56)
	body.add_child(columns)
	var left := _column(columns)
	var right := _column(columns)
	_choice_row(left, "Strike", "strike_mode", [["stroke", "Finger stroke"], ["power_bar", "Power bar"]])
	_choice_row(left, "Guide lines", "guide_level", [[0, "0"], [1, "1"], [2, "2"], [3, "3"]])
	_slider_row(left, "Aim sensitivity", "aim_sensitivity", 0.05, true)
	_slider_row(left, "Fine-tune sensitivity", "fine_tune_sensitivity", 0.05, true)
	_switch_row(right, "Stroke wander", "A crooked push adds aim error", "stroke_wander")
	_switch_row(right, "Reset spin and elevation", "After each shot", "reset_spin_after_shot")
	_switch_row(right, "Haptics", "", "haptics")
	_slider_row(right, "Volume", "volume", 0.05, false)


func _column(parent: Container) -> VBoxContainer:
	var column := VBoxContainer.new()
	column.custom_minimum_size.x = COLUMN_WIDTH
	column.add_theme_constant_override("separation", 22)
	parent.add_child(column)
	return column


## A row title on the left and, for sliders and guide levels, the current value on the right.
func _row_header(parent: Container, title: String, key: String) -> void:
	var header := HBoxContainer.new()
	var label := Label.new()
	label.text = title
	label.size_flags_horizontal = SIZE_EXPAND_FILL
	header.add_child(label)
	var value := Label.new()
	value.add_theme_color_override("font_color", MUTED)
	value.add_theme_font_size_override("font_size", 26)
	value.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	header.add_child(value)
	_values[key] = value
	parent.add_child(header)


func _choice_row(parent: Container, title: String, key: String, options: Array) -> void:
	var row := VBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	_row_header(row, title, key)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 10)
	var group := ButtonGroup.new()
	for option: Array in options:
		var button := _button(String(option[1]), Vector2(0.0, TOUCH))
		button.toggle_mode = true
		button.button_group = group
		button.size_flags_horizontal = SIZE_EXPAND_FILL
		var value: Variant = option[0]
		button.toggled.connect(func(on: bool) -> void:
			if on:
				_store.set_value(key, value))
		_controls["%s:%s" % [key, str(value)]] = button
		buttons.add_child(button)
	row.add_child(buttons)
	parent.add_child(row)


func _slider_row(parent: Container, title: String, key: String, step: float, logarithmic: bool) -> void:
	var row := VBoxContainer.new()
	row.add_theme_constant_override("separation", 0)
	_row_header(row, title, key)
	var bounds: Array = SettingsStore.RANGES[key]
	var slider := HSlider.new()
	slider.min_value = float(bounds[0])
	slider.max_value = float(bounds[1])
	slider.step = step
	# On a 0.25-4 range a log scale puts 1 in the middle, with as much room below as above.
	slider.exp_edit = logarithmic
	slider.custom_minimum_size.y = TOUCH
	slider.focus_mode = FOCUS_NONE
	_dragging[key] = false
	# A drag commits once, on release, rather than saving and logging every step.
	slider.drag_started.connect(func() -> void: _dragging[key] = true)
	slider.drag_ended.connect(func(_changed: bool) -> void:
		_dragging[key] = false
		_commit_slider(key))
	slider.value_changed.connect(func(value: float) -> void:
		_show_value(key, value)
		if not _dragging[key]:
			_commit_slider(key))
	_controls[key] = slider
	row.add_child(slider)
	parent.add_child(row)


func _switch_row(parent: Container, title: String, caption: String, key: String) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 24)
	var words := VBoxContainer.new()
	words.size_flags_horizontal = SIZE_EXPAND_FILL
	words.alignment = BoxContainer.ALIGNMENT_CENTER
	words.add_theme_constant_override("separation", 0)
	var label := Label.new()
	label.text = title
	words.add_child(label)
	if not caption.is_empty():
		var small := Label.new()
		small.text = caption
		small.add_theme_color_override("font_color", MUTED)
		small.add_theme_font_size_override("font_size", 24)
		words.add_child(small)
	row.add_child(words)
	var switch := _button("", Vector2(150.0, TOUCH))
	switch.toggle_mode = true
	switch.toggled.connect(func(on: bool) -> void:
		switch.text = "On" if on else "Off"
		_store.set_value(key, on))
	_controls[key] = switch
	row.add_child(switch)
	parent.add_child(row)


func _button(text: String, min_size: Vector2) -> Button:
	var button := Button.new()
	button.text = text
	button.custom_minimum_size = min_size
	button.focus_mode = FOCUS_NONE
	return button


func _commit_slider(key: String) -> void:
	var slider: HSlider = _controls[key]
	if _store != null and not _store.set_value(key, slider.value):
		_refresh(key)


func _on_settings_changed(key: String, _value: Variant) -> void:
	_refresh(key)


func _refresh_all() -> void:
	for key: String in SettingsStore.DEFAULTS:
		_refresh(key)


## Puts the control for key back to the stored value without emitting anything.
func _refresh(key: String) -> void:
	if _store == null:
		return
	var value: Variant = _store.get_value(key)
	var slider := _controls.get(key) as HSlider
	if slider != null:
		slider.set_value_no_signal(float(value))
		_show_value(key, float(value))
		return
	var switch := _controls.get(key) as Button
	if switch != null:
		switch.set_pressed_no_signal(bool(value))
		switch.text = "On" if bool(value) else "Off"
		return
	for id: String in _controls:
		if id.begins_with(key + ":"):
			(_controls[id] as Button).set_pressed_no_signal(id == "%s:%s" % [key, str(value)])
	if key == "guide_level":
		(_values[key] as Label).text = GUIDE_TEXT[clampi(int(value), 0, GUIDE_TEXT.size() - 1)]


func _show_value(key: String, value: float) -> void:
	var label := _values.get(key) as Label
	if label == null:
		return
	label.text = "%d%%" % roundi(value * 100.0) if key == "volume" else "×%.2f" % value


func _on_backdrop_input(event: InputEvent) -> void:
	var tap := event as InputEventMouseButton
	if tap != null and tap.button_index == MOUSE_BUTTON_LEFT and not tap.pressed:
		close()


func _auto_place() -> void:
	if auto_place and is_inside_tree() and visible:
		place_in(canvas_safe_rect(self))
