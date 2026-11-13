class_name EditPalette
extends Control
## Edit mode's add and remove (spec 12.1): a row along the bottom edge with a button for a red and
## one for each colour not on the table, then Remove for the selected ball. In FreeTableBar's look.

## ball: "red" or a colour.
signal add_pressed(ball: String)
signal remove_pressed

const BALLS: Array[String] = ["red", "yellow", "green", "brown", "blue", "pink", "black"]
## 88 canvas units is 44 pt on the phone, the minimum touch target.
const BALL_BUTTON := Vector2(104.0, 88.0)
## Wide enough for "Remove yellow", so the row doesn't change width as the selection does.
const REMOVE_BUTTON := Vector2(250.0, 88.0)
const GAP := 6
const FONT_SIZE := 24
const SWATCH := 40

var _buttons := {}
var _box: HBoxContainer
var _panel: PanelContainer
var _area := Rect2()


func _init() -> void:
	mouse_filter = MOUSE_FILTER_IGNORE
	_build()


## "red", a colour, or "remove".
func button(id: String) -> Button:
	return _buttons.get(id)


## A red while any are off the table, and each colour that isn't on it.
func set_available(red: bool, colours: Array[String]) -> void:
	for ball in BALLS:
		(_buttons[ball] as Button).visible = red if ball == "red" else colours.has(ball)
	_fit()


## label names the selected ball ("red 3", "pink"); Remove is off for none and for the cue ball.
func set_selected(label: String, removable: bool) -> void:
	var remove: Button = _buttons["remove"]
	remove.text = "Remove " + label if removable else "Remove"
	remove.disabled = not removable


## Centres the row at the bottom of area (canvas units).
func place_in(area: Rect2) -> void:
	_area = area
	var target := Vector2(area.get_center().x - size.x / 2.0, area.end.y - size.y).round()
	if position != target:
		position = target


func _build() -> void:
	theme = SettingsPanel.make_theme()
	_panel = PanelContainer.new()
	var style := StyleBoxFlat.new()
	# Nearly opaque: the row sits over the D and baulk line, which showed through.
	style.bg_color = Color(0.05, 0.06, 0.065, 0.94)
	style.set_corner_radius_all(22)
	style.set_content_margin_all(8.0)
	_panel.add_theme_stylebox_override("panel", style)
	_panel.mouse_filter = MOUSE_FILTER_IGNORE
	add_child(_panel)
	_box = HBoxContainer.new()
	_box.add_theme_constant_override("separation", GAP)
	_panel.add_child(_box)
	for ball in BALLS:
		var b := _button("+", BALL_BUTTON)
		b.icon = _swatch(BallsView.BALL_COLOURS[ball])
		b.tooltip_text = "Add a " + ball
		b.pressed.connect(add_pressed.emit.bind(ball))
		_buttons[ball] = b
	var remove := _button("Remove", REMOVE_BUTTON)
	remove.pressed.connect(remove_pressed.emit)
	_buttons["remove"] = remove
	set_selected("", false)
	_fit()


func _button(text: String, min_size: Vector2) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = min_size
	b.focus_mode = FOCUS_NONE
	b.add_theme_font_size_override("font_size", FONT_SIZE)
	_box.add_child(b)
	return b


func _fit() -> void:
	size = _panel.get_combined_minimum_size()
	_panel.size = size
	if _area.has_area():
		place_in(_area)


static func _swatch(colour: Color) -> GradientTexture2D:
	# A light rim, so the black reads on the dark panel.
	var rim := Color(0.85, 0.87, 0.86, 0.8)
	var gradient := Gradient.new()
	gradient.offsets = PackedFloat32Array([0.0, 0.78, 0.82, 0.92, 1.0])
	gradient.colors = PackedColorArray([colour.lightened(0.15), colour, rim, rim, Color(rim, 0.0)])
	var texture := GradientTexture2D.new()
	texture.gradient = gradient
	texture.fill = GradientTexture2D.FILL_RADIAL
	texture.fill_from = Vector2(0.5, 0.5)
	texture.fill_to = Vector2(1.0, 0.5)
	texture.width = SWATCH
	texture.height = SWATCH
	return texture
