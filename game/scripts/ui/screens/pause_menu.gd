class_name PauseMenu
extends Control
## The match's pause menu (spec 11.1): Resume, Concede frame, Concede match, Settings and Quit, and
## Claim frame while the person can't be caught (S2.1(b)).
## Quit asks to confirm in place, on the same card, since the match is discarded (spec 11.2); so do
## the two concessions, which cannot be withdrawn (S4.2(e)). A modal card in the middle
## of the safe area; tapping outside it resumes.
##
## The card keeps its size while it asks, with Confirm at the bottom left and Cancel at the bottom
## right, where Quit was: a second tap on any of the three buttons that ask never confirms.

signal resume_pressed
## Each is emitted only after the confirming press.
signal concede_frame_confirmed
signal concede_match_confirmed
signal claim_frame_confirmed
signal quit_confirmed
signal settings_pressed

const Kit := preload("res://scripts/ui/screens/screen_kit.gd")
const BUTTON_WIDTH := 280.0
const GAP := 12
const TITLES := {
	"": "Paused",
	"concede_frame": "Concede frame",
	"concede_match": "Concede match",
	"claim_frame": "Claim frame",
	"quit": "Quit",
}
const OPPONENT := "Computer"
const CONSEQUENCES := {
	"concede_frame": OPPONENT + " wins the frame",
	"concede_match": OPPONENT + " wins the match",
	"claim_frame": "You win the frame",
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
var _opponent := OPPONENT
var _watching := false
var _concessions: HBoxContainer


func _init() -> void:
	visible = false
	set_anchors_preset(PRESET_FULL_RECT)
	_build()


func _ready() -> void:
	Kit.follow_safe_area(self, _auto_place)


## Each flag is whether the engine offers the person that decision now (the frame view's options)
## and no stroke is playing. A concession not offered is disabled; Claim frame shows only when it is.
## watching: the person only watches (Watch a 147), so the menu is Resume and Quit alone, and
## Quit leaves at once: there is no match to lose.
func open(can_concede_frame: bool, can_concede_match: bool = true, can_claim_frame: bool = false,
		watching: bool = false) -> void:
	_watching = watching
	_concessions.visible = not watching
	(_buttons["settings"] as Button).visible = not watching
	set_available(can_concede_frame, can_concede_match, can_claim_frame)
	_ask("")
	_backdrop_down = false
	visible = true
	_auto_place()


## What is offered changed while the menu is open. A question about something no longer offered
## goes back to the menu.
func set_available(can_concede_frame: bool, can_concede_match: bool, can_claim_frame: bool) -> void:
	var offered := {"concede_frame": can_concede_frame, "concede_match": can_concede_match,
		"claim_frame": can_claim_frame}
	(_buttons["concede_frame"] as Button).disabled = not can_concede_frame
	(_buttons["concede_match"] as Button).disabled = not can_concede_match
	(_buttons["claim_frame"] as Button).visible = can_claim_frame and not _watching
	if offered.has(_confirming) and not bool(offered[_confirming]):
		_ask("")
	_auto_place()


func close() -> void:
	visible = false


## The name the concessions' questions give the winner: the opponent's level's, OPPONENT until
## given one.
func set_opponent_name(opponent: String) -> void:
	_opponent = opponent
	if not _confirming.is_empty():
		_ask(_confirming)


## "resume", "claim_frame", "concede_frame", "concede_match", "settings", "quit", and while a
## confirmation shows, "confirm" and "cancel".
func button(id: String) -> Button:
	if _confirming.is_empty() and (id == "confirm" or id == "cancel"):
		return null
	return _buttons.get(id)


## Which action is waiting for its confirming press: "claim_frame", "concede_frame",
## "concede_match", "quit" or "".
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
	var claim := _add(_menu, "claim_frame", "Claim frame")
	claim.pressed.connect(_ask.bind("claim_frame"))
	claim.visible = false
	var concessions := _pair(_menu)
	_concessions = concessions
	_add(concessions, "concede_frame", "Concede frame").pressed.connect(_ask.bind("concede_frame"))
	_add(concessions, "concede_match", "Concede match").pressed.connect(_ask.bind("concede_match"))
	var rest := _pair(_menu)
	_add(rest, "settings", "Settings").pressed.connect(settings_pressed.emit)
	_add(rest, "quit", "Quit").pressed.connect(_on_quit)

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
	_consequence.text = String(CONSEQUENCES.get(action, "")).replace(OPPONENT, _opponent)
	_menu.visible = action.is_empty()
	_question.visible = not action.is_empty()


func _on_quit() -> void:
	if _watching:
		quit_confirmed.emit()
	else:
		_ask("quit")


func _on_confirm() -> void:
	var action := _confirming
	_ask("")
	match action:
		"concede_frame":
			concede_frame_confirmed.emit()
		"concede_match":
			concede_match_confirmed.emit()
		"claim_frame":
			claim_frame_confirmed.emit()
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
