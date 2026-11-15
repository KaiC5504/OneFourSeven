class_name MatchSetupScreen
extends Control
## Match setup (spec 11.1): match length (best of 1, 3, 5, 7, 9 or 11 frames) and who breaks first
## (random, you or the computer; random by default), then Start. The opponent's level arrives with
## the opponent in M4 and has no row until then. The last choices are remembered for the session:
## the screen keeps them from one opening to the next.
##
## A modal card in the middle of the safe area, in the settings panel's look.

## config: {best_of: int, first_breaker: "random" | "you" | "computer"}.
signal start_pressed(config: Dictionary)
signal back_pressed

const Kit := preload("res://scripts/ui/screens/screen_kit.gd")
const LENGTHS: Array[int] = [1, 3, 5, 7, 9, 11]
const BREAKERS: Array[String] = ["random", "you", "computer"]
const BREAKER_TEXT := {"random": "Random", "you": "You", "computer": "Computer"}
const CARD_WIDTH := 640.0
const BACK_WIDTH := 190.0

var auto_place := true

var _controls := {}
var _best_of := 3
var _first_breaker := "random"
var _length: Label
var _card: PanelContainer


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


## What Start would send now.
func config() -> Dictionary:
	return {"best_of": _best_of, "first_breaker": _first_breaker}


## "start", "back", "best_of:<n>" or "first_breaker:<name>".
func control(id: String) -> Control:
	return _controls.get(id)


func place_in(safe_rect: Rect2) -> void:
	Kit.centre(_card, safe_rect)


func _build() -> void:
	theme = SettingsPanel.make_theme()
	Kit.backdrop(self)
	var body := Kit.card(self)
	_card = body.get_parent()
	body.custom_minimum_size.x = CARD_WIDTH
	var title := Kit.label("New match", Kit.TITLE_FONT_SIZE)
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	body.add_child(title)

	var lengths: Array = []
	for n in LENGTHS:
		lengths.append([n, str(n)])
	_length = _choice_row(body, "Match length", "best_of", lengths, func(value: Variant) -> void:
		_best_of = value
		_show_length())
	var breakers: Array = []
	for who in BREAKERS:
		breakers.append([who, BREAKER_TEXT[who]])
	_choice_row(body, "Who breaks first", "first_breaker", breakers, func(value: Variant) -> void:
		_first_breaker = value)
	(_controls["best_of:%d" % _best_of] as Button).set_pressed_no_signal(true)
	(_controls["first_breaker:" + _first_breaker] as Button).set_pressed_no_signal(true)
	_show_length()

	var footer := HBoxContainer.new()
	footer.add_theme_constant_override("separation", 12)
	body.add_child(footer)
	var back := Kit.button("Back", Vector2(BACK_WIDTH, Kit.TOUCH))
	back.pressed.connect(back_pressed.emit)
	_controls["back"] = back
	footer.add_child(back)
	var start := Kit.button("Start", Vector2(0.0, Kit.TOUCH), true)
	start.size_flags_horizontal = SIZE_EXPAND_FILL
	start.pressed.connect(func() -> void: start_pressed.emit(config()))
	_controls["start"] = start
	footer.add_child(start)


## A titled row of buttons of which one is chosen, as in the settings panel; returns the muted
## label at the title's right.
func _choice_row(parent: Container, title: String, key: String, options: Array, chosen: Callable) -> Label:
	var row := VBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	var header := HBoxContainer.new()
	var words := Kit.label(title, 30)
	words.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	words.size_flags_horizontal = SIZE_EXPAND_FILL
	header.add_child(words)
	var value := Kit.label("", Kit.SMALL_FONT_SIZE, SettingsPanel.MUTED)
	header.add_child(value)
	row.add_child(header)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 10)
	var group := ButtonGroup.new()
	for option: Array in options:
		var b := Kit.button(String(option[1]), Vector2(0.0, Kit.TOUCH))
		b.toggle_mode = true
		b.button_group = group
		b.size_flags_horizontal = SIZE_EXPAND_FILL
		var choice: Variant = option[0]
		b.toggled.connect(func(on: bool) -> void:
			if on:
				chosen.call(choice))
		_controls["%s:%s" % [key, str(choice)]] = b
		buttons.add_child(b)
	row.add_child(buttons)
	parent.add_child(row)
	return value


func _show_length() -> void:
	_length.text = "1 frame" if _best_of == 1 else "Best of %d frames" % _best_of


func _auto_place() -> void:
	if auto_place and is_inside_tree() and visible:
		place_in(SettingsPanel.canvas_safe_rect(self))
