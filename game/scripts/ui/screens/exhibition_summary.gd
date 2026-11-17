class_name ExhibitionSummary
extends Control
## After Watch a 147 (spec 10.1): the break the 147 made, and Again or Home. Built like the frame
## summary (ScreenKit).

signal again_pressed
signal home_pressed

const Kit := preload("res://scripts/ui/screens/screen_kit.gd")
const HEADLINE_FONT_SIZE := 72
const CARD_WIDTH := 480.0
const HOME_WIDTH := 190.0
const MAXIMUM := "Maximum break"

var auto_place := true

var _headline: Label
var _maximum: Label
var _controls := {}
var _card: PanelContainer


func _init() -> void:
	visible = false
	set_anchors_preset(PRESET_FULL_RECT)
	_build()


func _ready() -> void:
	Kit.follow_safe_area(self, _auto_place)


## result: {break_points: int, maximum: bool, why: "maximum" | "missed" | "fouled" | "left"}.
func open(result: Dictionary) -> void:
	var missing: Array[String] = []
	var points := Kit.number(result, "result", ["break_points"], missing)
	var maximum: bool = result.get("maximum") == true
	if result.get("maximum") is not bool:
		missing.append("result.maximum")
	Kit.report_missing("ExhibitionSummary", missing)
	_headline.text = Kit.number_text(points) if maximum else "Break %s" % Kit.number_text(points)
	_maximum.visible = maximum
	visible = true
	_auto_place()


func close() -> void:
	visible = false


## "again" or "home".
func control(id: String) -> Control:
	return _controls.get(id)


## Every line it shows, top to bottom, for tests and the harness.
func texts() -> PackedStringArray:
	var lines := PackedStringArray([_headline.text])
	if _maximum.visible:
		lines.append(_maximum.text)
	return lines


func place_in(safe_rect: Rect2) -> void:
	Kit.centre(_card, safe_rect)


func _build() -> void:
	theme = SettingsPanel.make_theme()
	Kit.backdrop(self)
	var body := Kit.card(self)
	_card = body.get_parent()
	body.custom_minimum_size.x = CARD_WIDTH
	var heading := VBoxContainer.new()
	heading.add_theme_constant_override("separation", 0)
	body.add_child(heading)
	_headline = Kit.label("", HEADLINE_FONT_SIZE)
	heading.add_child(_headline)
	_maximum = Kit.label(MAXIMUM, Kit.SMALL_FONT_SIZE, SettingsPanel.MUTED)
	_maximum.visible = false
	heading.add_child(_maximum)

	var footer := HBoxContainer.new()
	footer.add_theme_constant_override("separation", 12)
	body.add_child(footer)
	var home := Kit.button("Home", Vector2(HOME_WIDTH, Kit.TOUCH))
	home.pressed.connect(home_pressed.emit)
	_controls["home"] = home
	footer.add_child(home)
	var again := Kit.button("Again", Vector2(0.0, Kit.TOUCH), true)
	again.size_flags_horizontal = SIZE_EXPAND_FILL
	again.pressed.connect(again_pressed.emit)
	_controls["again"] = again
	footer.add_child(again)


func _auto_place() -> void:
	if auto_place and is_inside_tree() and visible:
		place_in(SettingsPanel.canvas_safe_rect(self))
