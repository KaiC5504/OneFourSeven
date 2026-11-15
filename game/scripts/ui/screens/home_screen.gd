class_name HomeScreen
extends Control
## Home (spec 11.1), where the app opens: Play, Free table and Settings, over the table seen from
## above. Covers the canvas and takes every touch while it shows.
##
## The name sits at the top of the safe area and the buttons along its bottom, with the canvas
## darkened most at those two edges, so the bed of the table stays clear between them (spec 8.9).

signal play_pressed
signal free_table_pressed
signal settings_pressed

const Kit := preload("res://scripts/ui/screens/screen_kit.gd")
const TITLE := "One Four Seven"
const TITLE_FONT_SIZE := 64
const BUTTON_SIZE := Vector2(220.0, 96.0)
const ROW_WIDTH := 932.0
const GAP := 16
# The free table bar's dark, a little denser: these buttons lie on the table with no card under them.
const BUTTON_FILL := Color(0.05, 0.06, 0.065, 0.7)
const EDGE_MARGIN := 32.0
const SHADE_EDGE := 0.72
const SHADE_MIDDLE := 0.3

## When true the screen fits itself to the safe area; set false to call place_in.
var auto_place := true

var _buttons := {}
var _title: Label
var _row: HBoxContainer


func _init() -> void:
	visible = false
	set_anchors_preset(PRESET_FULL_RECT)
	_build()


func _ready() -> void:
	Kit.follow_safe_area(self, _auto_place)


func open() -> void:
	visible = true
	_auto_place()


func close() -> void:
	visible = false


## "play", "free_table" or "settings".
func button(id: String) -> Button:
	return _buttons.get(id)


func place_in(safe_rect: Rect2) -> void:
	var title_size := _title.get_combined_minimum_size()
	Kit.put(_title, Vector2(safe_rect.get_center().x - title_size.x / 2.0, safe_rect.position.y + EDGE_MARGIN), title_size)
	var row_size := Vector2(minf(ROW_WIDTH, safe_rect.size.x), _row.get_combined_minimum_size().y)
	Kit.put(_row, Vector2(safe_rect.get_center().x - row_size.x / 2.0, safe_rect.end.y - EDGE_MARGIN - row_size.y), row_size)


func _build() -> void:
	theme = SettingsPanel.make_theme()
	add_child(_shade())
	_title = Kit.label(TITLE, TITLE_FONT_SIZE)
	_title.name = "Title"
	_title.mouse_filter = MOUSE_FILTER_IGNORE
	add_child(_title)
	_row = HBoxContainer.new()
	_row.name = "Buttons"
	_row.add_theme_constant_override("separation", GAP)
	add_child(_row)
	_add("play", "Play", true).pressed.connect(play_pressed.emit)
	_add("free_table", "Free table", false).pressed.connect(free_table_pressed.emit)
	_add("settings", "Settings", false).pressed.connect(settings_pressed.emit)


func _add(id: String, text: String, primary: bool) -> Button:
	var b := Kit.button(text, BUTTON_SIZE, primary)
	b.size_flags_horizontal = SIZE_EXPAND_FILL
	if not primary:
		b.add_theme_stylebox_override("normal", Kit.fill(BUTTON_FILL))
		b.add_theme_stylebox_override("hover", Kit.fill(BUTTON_FILL.lightened(0.08)))
	_buttons[id] = b
	_row.add_child(b)
	return b


func _shade() -> TextureRect:
	var gradient := Gradient.new()
	gradient.offsets = PackedFloat32Array([0.0, 0.3, 0.7, 1.0])
	gradient.colors = PackedColorArray([Color(0.0, 0.0, 0.0, SHADE_EDGE), Color(0.0, 0.0, 0.0, SHADE_MIDDLE),
			Color(0.0, 0.0, 0.0, SHADE_MIDDLE), Color(0.0, 0.0, 0.0, SHADE_EDGE)])
	var texture := GradientTexture2D.new()
	texture.gradient = gradient
	texture.fill_from = Vector2(0.5, 0.0)
	texture.fill_to = Vector2(0.5, 1.0)
	texture.width = 4
	texture.height = 256
	var shade := TextureRect.new()
	shade.name = "Backdrop"
	shade.texture = texture
	shade.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	shade.stretch_mode = TextureRect.STRETCH_SCALE
	shade.set_anchors_preset(PRESET_FULL_RECT)
	shade.mouse_filter = MOUSE_FILTER_STOP
	return shade


func _auto_place() -> void:
	if auto_place and is_inside_tree() and visible:
		place_in(SettingsPanel.canvas_safe_rect(self))
