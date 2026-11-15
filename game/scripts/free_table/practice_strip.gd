class_name PracticeStrip
extends PanelContainer
## The practice shot on the table (spec 12.1): its name, what the cue ball should do and the
## filmed cue in the controls' own words, with Show me (the shot as filmed), Again (the position
## back) and a button that ends the practice. Sits at the top, right of the free table's bar.
## Only its buttons take touches.

signal show_pressed
signal again_pressed
signal close_pressed

const TITLE_FONT_SIZE := 24
const TEXT_FONT_SIZE := 20
const CLOSE_FONT_SIZE := 40
const SHOW_SIZE := Vector2(140.0, 88.0)
const AGAIN_SIZE := Vector2(116.0, 88.0)
const CLOSE_SIZE := Vector2(72.0, 88.0)
const GAP := 6
const TEXT_GAP := 14
const MARGIN := 6.0
## The words keep at least this much width; past that the strip runs under the power cue.
const MIN_TEXT_WIDTH := 240.0

var _labels: Array[Label] = []
var _text: VBoxContainer
var _buttons := {}


func _init() -> void:
	theme = SettingsPanel.make_theme()
	mouse_filter = MOUSE_FILTER_IGNORE
	visible = false
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.05, 0.06, 0.065, 0.55)
	style.set_corner_radius_all(22)
	style.set_content_margin_all(MARGIN)
	style.content_margin_left = MARGIN + TEXT_GAP
	add_theme_stylebox_override("panel", style)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", GAP)
	row.mouse_filter = MOUSE_FILTER_IGNORE
	add_child(row)
	_text = VBoxContainer.new()
	_text.add_theme_constant_override("separation", 0)
	_text.alignment = BoxContainer.ALIGNMENT_CENTER
	_text.mouse_filter = MOUSE_FILTER_IGNORE
	row.add_child(_text)
	for i in 3:
		var label := Label.new()
		label.add_theme_font_size_override("font_size", TITLE_FONT_SIZE if i == 0 else TEXT_FONT_SIZE)
		label.add_theme_color_override("font_color", SettingsPanel.TEXT if i == 0 else SettingsPanel.MUTED)
		# Trimmed, a label has no minimum width of its own: place_in gives the column one.
		label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
		label.mouse_filter = MOUSE_FILTER_IGNORE
		_text.add_child(label)
		_labels.append(label)
	var spacer := Control.new()
	spacer.custom_minimum_size.x = TEXT_GAP - GAP
	spacer.mouse_filter = MOUSE_FILTER_IGNORE
	row.add_child(spacer)
	_button(row, "show", "Show me", SHOW_SIZE).pressed.connect(show_pressed.emit)
	_button(row, "again", "Again", AGAIN_SIZE).pressed.connect(again_pressed.emit)
	var close := _button(row, "close", "×", CLOSE_SIZE)
	close.add_theme_font_size_override("font_size", CLOSE_FONT_SIZE)
	close.tooltip_text = "End practice"
	close.pressed.connect(close_pressed.emit)


## "show", "again" or "close".
func button(id: String) -> Button:
	return _buttons.get(id)


func texts() -> Array[String]:
	var out: Array[String] = []
	for label in _labels:
		out.append(label.text)
	return out


## title: the shot's name; hint: what the cue ball should do; cue: the filmed cue (Practice.cue_text).
func set_shot(title: String, hint: String, cue: String) -> void:
	var lines := [title, hint, "As filmed: " + cue]
	for i in _labels.size():
		_labels[i].text = lines[i]


## While a shot plays, Show me and Again wait.
func set_busy(on: bool) -> void:
	(_buttons["show"] as Button).disabled = on
	(_buttons["again"] as Button).disabled = on


## Puts the strip at area's top-left (canvas units), no wider than area unless the words would
## drop under MIN_TEXT_WIDTH. Writes only what changed, so a steady poll never makes
## low-processor mode redraw.
func place_in(area: Rect2) -> void:
	var words := 0.0
	for label in _labels:
		var font := label.get_theme_font("font")
		words = maxf(words, font.get_string_size(label.text, HORIZONTAL_ALIGNMENT_LEFT, -1.0,
				label.get_theme_font_size("font_size")).x)
	var fixed := get_combined_minimum_size().x - _text.custom_minimum_size.x
	var width := ceilf(minf(words + 2.0, maxf(area.size.x - fixed, MIN_TEXT_WIDTH)))
	if _text.custom_minimum_size.x != width:
		_text.custom_minimum_size.x = width
	var want := get_combined_minimum_size()
	if size != want:
		size = want
	var at := area.position.round()
	if position != at:
		position = at


func _button(parent: Container, id: String, text: String, min_size: Vector2) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = min_size
	# Without focus a keyboard shortcut (Space shoots) can never press it.
	b.focus_mode = FOCUS_NONE
	b.add_theme_font_size_override("font_size", FreeTableBar.FONT_SIZE)
	_buttons[id] = b
	parent.add_child(b)
	return b
