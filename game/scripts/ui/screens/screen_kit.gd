extends Object
## What the match screens share: the settings panel's look (its theme, colours and card), a
## backdrop that takes every touch, a card centred in the safe area, the summaries' table of You
## against Computer, and reading a summary or match Dictionary that may be malformed.

const TOUCH := SettingsPanel.TOUCH
const TITLE_FONT_SIZE := 40
const SMALL_FONT_SIZE := 28
const CARD_SEPARATION := 24
const DARK_TEXT := Color(0.04, 0.06, 0.05)
const BACKDROP := Color(0.0, 0.0, 0.0, 0.5)
const DIVIDER := Color(1.0, 1.0, 1.0, 0.1)
const YOU := "You"
const OPPONENT := "Computer"
## Shown where a summary has no number to show.
const NO_VALUE := "–"
const VALUE_WIDTH := 190.0
const CAPTION_WIDTH := 220.0
# iOS can report the safe area late and changes it on rotation without a resize.
const SAFE_AREA_POLL_SECONDS := 1.0


## Covers the canvas under a screen's controls, so every touch that misses them stops here.
static func backdrop(screen: Control, colour: Color = BACKDROP) -> ColorRect:
	var rect := ColorRect.new()
	rect.name = "Backdrop"
	rect.color = colour
	rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	rect.mouse_filter = Control.MOUSE_FILTER_STOP
	screen.add_child(rect)
	return rect


## The settings panel's card, with a column for its rows.
static func card(screen: Control) -> VBoxContainer:
	var panel := PanelContainer.new()
	panel.name = "Card"
	var style := StyleBoxFlat.new()
	style.bg_color = SettingsPanel.CARD
	style.set_corner_radius_all(28)
	style.anti_aliasing = true
	style.content_margin_left = 44.0
	style.content_margin_right = 44.0
	style.content_margin_top = 32.0
	style.content_margin_bottom = 36.0
	panel.add_theme_stylebox_override("panel", style)
	screen.add_child(panel)
	var body := VBoxContainer.new()
	body.add_theme_constant_override("separation", CARD_SEPARATION)
	panel.add_child(body)
	return body


## `primary` marks a screen's one way on: filled light, so the accent keeps meaning "chosen".
static func button(text: String, min_size: Vector2, primary: bool = false) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = min_size
	b.focus_mode = Control.FOCUS_NONE
	if primary:
		b.add_theme_stylebox_override("normal", fill(SettingsPanel.TEXT))
		b.add_theme_stylebox_override("hover", fill(Color.WHITE))
		for colour: String in ["font_color", "font_hover_color", "font_focus_color"]:
			b.add_theme_color_override(colour, DARK_TEXT)
	return b


static func label(text: String, font_size: int, colour: Color = SettingsPanel.TEXT) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", font_size)
	l.add_theme_color_override("font_color", colour)
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	return l


## The summaries' table: the person's value, what it is, the opponent's value. Its columns are
## fixed, so two tables on one card line up.
static func table() -> GridContainer:
	var grid := GridContainer.new()
	grid.columns = 3
	grid.add_theme_constant_override("h_separation", 0)
	grid.add_theme_constant_override("v_separation", 10)
	grid.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	return grid


## Adds a row to a table and returns its two value labels, the person's first.
static func row(grid: GridContainer, caption: String, font_size: int, colour: Color = SettingsPanel.TEXT,
		value_width: float = VALUE_WIDTH, caption_width: float = CAPTION_WIDTH) -> Array[Label]:
	var mine := label("", font_size, colour)
	mine.custom_minimum_size.x = value_width
	var words := label(caption, SMALL_FONT_SIZE, SettingsPanel.MUTED)
	words.custom_minimum_size.x = caption_width
	var theirs := label("", font_size, colour)
	theirs.custom_minimum_size.x = value_width
	for cell: Label in [mine, words, theirs]:
		grid.add_child(cell)
	return [mine, theirs]


## The table's first row: whose column is whose.
static func heading(grid: GridContainer, value_width: float = VALUE_WIDTH,
		caption_width: float = CAPTION_WIDTH) -> void:
	var names := row(grid, "", SMALL_FONT_SIZE, SettingsPanel.MUTED, value_width, caption_width)
	names[0].text = YOU
	names[1].text = OPPONENT


static func divider() -> ColorRect:
	var line := ColorRect.new()
	line.color = DIVIDER
	line.custom_minimum_size.y = 2.0
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return line


## Moves and sizes a control, writing only what changed, so a steady poll never makes
## low-processor mode redraw.
static func put(control: Control, at: Vector2, extent: Vector2) -> void:
	if control.size != extent:
		control.size = extent
	if control.position != at.round():
		control.position = at.round()


## Centres a card in safe_rect (canvas units), shrinking it to fit if it must.
static func centre(panel: Control, safe_rect: Rect2) -> void:
	var extent := panel.get_combined_minimum_size().min(safe_rect.size)
	put(panel, safe_rect.position + (safe_rect.size - extent) / 2.0, extent)


## For a screen's _ready: calls place now (a screen opened before it was in the tree had no theme
## to measure with), when the canvas changes size and, since the safe area can change without
## that, once a second.
static func follow_safe_area(screen: Control, place: Callable) -> void:
	screen.get_viewport().size_changed.connect(place)
	var poll := Timer.new()
	poll.wait_time = SAFE_AREA_POLL_SECONDS
	poll.timeout.connect(place)
	screen.add_child(poll)
	poll.start()
	place.call()


## The whole number at source[path[0]][path[1]]..., or -1 with "<source_name>.<path>" added to
## `missing` when a step isn't there or the value is no number.
static func number(source: Dictionary, source_name: String, path: Array, missing: Array[String]) -> int:
	var at: Variant = source
	for step: Variant in path:
		if step is String and at is Dictionary and (at as Dictionary).has(step):
			at = (at as Dictionary)[step]
		elif step is int and at is Array and int(step) >= 0 and int(step) < (at as Array).size():
			at = (at as Array)[step]
		else:
			at = null
			break
	# JSON read by Godot has every number as a float.
	if at is int or (at is float and is_finite(at)):
		return int(at)
	var key := source_name
	for step: Variant in path:
		key += ".%s" % str(step)
	if not missing.has(key):
		missing.append(key)
	return -1


static func number_text(value: int) -> String:
	return NO_VALUE if value < 0 else str(value)


## One app.error naming every key a summary lacked.
static func report_missing(screen_name: String, missing: Array[String]) -> void:
	if not missing.is_empty():
		Log.warn("app", "app.error", {"kind": "message",
				"message": "%s: missing or malformed %s" % [screen_name, ", ".join(missing)]})


## A button's box in the theme's shape.
static func fill(colour: Color) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = colour
	box.set_corner_radius_all(16)
	box.anti_aliasing = true
	box.content_margin_left = 16.0
	box.content_margin_right = 16.0
	return box
