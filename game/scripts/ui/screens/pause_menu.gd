class_name PauseMenu
extends Control
## The match's pause menu (spec 11.1): Resume, Concede frame, Concede match, Settings and Quit.
## Quit asks to confirm in place, on the same card, since the match is discarded (spec 11.2); so do
## the two concessions, which cannot be withdrawn (Section 4 Rule 2(e)). A modal card in the middle
## of the safe area; tapping outside it resumes.
##
## The card keeps its size while it asks, with Confirm at the bottom left and Cancel at the bottom
## right, where Quit was: a second tap on any of the three buttons that ask never confirms.

signal resume_pressed
## Each is emitted only after the confirming press.
signal concede_frame_confirmed
signal concede_match_confirmed
signal quit_confirmed
signal settings_pressed

const Kit := preload("res://scripts/ui/screens/screen_kit.gd")
const BUTTON_WIDTH := 280.0
const GAP := 12
const TITLES := {
	"": "Paused",
	"concede_frame": "Concede frame",
	"concede_match": "Concede match",
	"quit": "Quit",
}
const CONSEQUENCES := {
	"concede_frame": "Your opponent wins the frame",
	"concede_match": "Your opponent wins the match",
	"quit": "The match will be discarded",
}

var auto_place := true

var _buttons := {}
var _confirming := ""
var _title: Label
var _menu: VBoxContainer
var _question: VBoxContainer
var _consequence: Label
var _card: PanelContainer
var _backdrop_down := false


func _init() -> void:
	visible = false
	set_anchors_preset(PRESET_FULL_RECT)
	_build()


func _ready() -> void:
	Kit.follow_safe_area(self, _auto_place)


## can_concede_frame: whether the engine offers the person concede_frame now (the frame view's
## options); the button is disabled otherwise.
func open(can_concede_frame: bool) -> void:
	(_buttons["concede_frame"] as Button).disabled = not can_concede_frame
	_ask("")
	_backdrop_down = false
	visible = true
	_auto_place()


func close() -> void:
	visible = false


## "resume", "concede_frame", "concede_match", "settings", "quit", and while a confirmation
## shows, "confirm" and "cancel".
func button(id: String) -> Button:
	if _confirming.is_empty() and (id == "confirm" or id == "cancel"):
		return null
	return _buttons.get(id)


## Which action is waiting for its confirming press: "concede_frame", "concede_match", "quit" or "".
func confirming() -> String:
	return _confirming


func place_in(safe_rect: Rect2) -> void:
	# The question takes the menu's room, so the card is one size whichever shows.
	_question.custom_minimum_size = _menu.get_combined_minimum_size()
	Kit.centre(_card, safe_rect)


func _build() -> void:
	theme = SettingsPanel.make_theme()
	Kit.backdrop(self).gui_input.connect(_on_backdrop_input)
	var body := Kit.card(self)
	_card = body.get_parent()
	_title = Kit.label("", Kit.TITLE_FONT_SIZE)
	_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	body.add_child(_title)

	_menu = VBoxContainer.new()
	_menu.add_theme_constant_override("separation", GAP)
	body.add_child(_menu)
	var resume := _add(_menu, "resume", "Resume", true)
	resume.pressed.connect(resume_pressed.emit)
	var concessions := _pair(_menu)
	_add(concessions, "concede_frame", "Concede frame").pressed.connect(_ask.bind("concede_frame"))
	_add(concessions, "concede_match", "Concede match").pressed.connect(_ask.bind("concede_match"))
	var rest := _pair(_menu)
	_add(rest, "settings", "Settings").pressed.connect(settings_pressed.emit)
	_add(rest, "quit", "Quit").pressed.connect(_ask.bind("quit"))

	_question = VBoxContainer.new()
	_question.add_theme_constant_override("separation", GAP)
	_question.size_flags_vertical = SIZE_EXPAND_FILL
	body.add_child(_question)
	_consequence = Kit.label("", 30)
	_consequence.size_flags_vertical = SIZE_EXPAND_FILL
	_question.add_child(_consequence)
	var answers := _pair(_question)
	_add(answers, "confirm", "Confirm").pressed.connect(_on_confirm)
	_add(answers, "cancel", "Cancel").pressed.connect(_ask.bind(""))
	_ask("")


func _pair(parent: Container) -> HBoxContainer:
	var pair := HBoxContainer.new()
	pair.add_theme_constant_override("separation", GAP)
	parent.add_child(pair)
	return pair


func _add(parent: Container, id: String, text: String, primary: bool = false) -> Button:
	var b := Kit.button(text, Vector2(BUTTON_WIDTH, Kit.TOUCH), primary)
	b.size_flags_horizontal = SIZE_EXPAND_FILL
	_buttons[id] = b
	parent.add_child(b)
	return b


## Shows the question for an action, or the menu for "".
func _ask(action: String) -> void:
	_confirming = action
	_title.text = TITLES[action]
	_consequence.text = String(CONSEQUENCES.get(action, ""))
	_menu.visible = action.is_empty()
	_question.visible = not action.is_empty()


func _on_confirm() -> void:
	var action := _confirming
	_ask("")
	match action:
		"concede_frame":
			concede_frame_confirmed.emit()
		"concede_match":
			concede_match_confirmed.emit()
		"quit":
			quit_confirmed.emit()


## A tap is a press and its release both on the backdrop: a finger that went down on something
## else before the menu opened and lifts here resumes nothing.
func _on_backdrop_input(event: InputEvent) -> void:
	var tap := event as InputEventMouseButton
	if tap == null or tap.button_index != MOUSE_BUTTON_LEFT:
		return
	if tap.pressed:
		_backdrop_down = true
	elif _backdrop_down:
		_backdrop_down = false
		_ask("")
		resume_pressed.emit()


func _auto_place() -> void:
	if auto_place and is_inside_tree() and visible:
		place_in(SettingsPanel.canvas_safe_rect(self))
