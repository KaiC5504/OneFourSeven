class_name ChoiceStrip
extends Control
## The rule decisions the game requires (spec 11.1, 11.2): after the computer's foul (play, have it
## play again, replace the balls), a re-rack offer, the toss for a re-spotted black. A row of
## buttons low in the safe area, one per option, with one line saying what is being asked. It is
## not modal: the table stays visible, and nothing else is tappable through the buttons.
##
## A choice dismisses the strip before `chosen` is emitted, so a second tap can't choose twice.

## A decision name from the options offered.
signal chosen(kind: String)

const Kit := preload("res://scripts/match/hud/hud_kit.gd")
const LABELS := {
	"play": "Play",
	"play_again": "Computer plays again",
	"replace": "Replace the balls",
	"accept_rerack": "Re-rack",
	"refuse_rerack": "Play on",
	"play_first": "Play first",
	"play_second": "Computer first",
	"claim_frame": "Claim the frame",
}
const FREE_BALL_PLAY := "Take the free ball"
const OPPONENT := "Computer"
const BUTTON_SIZE := Vector2(210.0, 88.0)
const FONT_SIZE := 28
const GAP := 8
const MARGIN := 10.0
## Darker than the scoreboard: a ball may lie under the words.
const PANEL_ALPHA := 0.78
const WHO := "ChoiceStrip: the frame view"

var _panel := PanelContainer.new()
var _prompt := Label.new()
var _row := HBoxContainer.new()
var _buttons := {}
var _area := Rect2()
var _offering := false
var _opponent := OPPONENT


func _init() -> void:
	mouse_filter = MOUSE_FILTER_IGNORE
	set_anchors_preset(PRESET_FULL_RECT)
	theme = SettingsPanel.make_theme()
	var style := Kit.panel_style(PANEL_ALPHA)
	style.set_content_margin_all(MARGIN)
	_panel.name = "Panel"
	_panel.add_theme_stylebox_override("panel", style)
	_panel.mouse_filter = MOUSE_FILTER_IGNORE
	_panel.visible = false
	add_child(_panel)
	var lines := VBoxContainer.new()
	lines.add_theme_constant_override("separation", GAP)
	lines.mouse_filter = MOUSE_FILTER_IGNORE
	_panel.add_child(lines)
	_prompt.name = "Prompt"
	_prompt.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_prompt.add_theme_font_size_override("font_size", FONT_SIZE)
	_prompt.mouse_filter = MOUSE_FILTER_IGNORE
	lines.add_child(_prompt)
	_row.alignment = BoxContainer.ALIGNMENT_CENTER
	_row.add_theme_constant_override("separation", GAP)
	_row.mouse_filter = MOUSE_FILTER_IGNORE
	lines.add_child(_row)


func _ready() -> void:
	# Before the strip is in the tree its panel has no theme, so an offer made then was measured
	# without its margins and separations.
	_fit()


## Offers `options` (decision names, in order); `frame` is the frame view, for the words (a free
## ball called, the penalty). Replaces any offer showing.
func offer(options: Array[String], frame: Dictionary) -> void:
	_drop_buttons()
	for kind in options:
		var label := label_for(kind, frame, _opponent)
		if label.is_empty():
			Kit.problem("ChoiceStrip: no button for the decision \"%s\"" % kind)
			continue
		if _buttons.has(kind):
			continue
		var b := Kit.button(label, BUTTON_SIZE, FONT_SIZE)
		b.name = kind
		b.pressed.connect(_choose.bind(kind))
		_row.add_child(b)
		_buttons[kind] = b
	_offering = not _buttons.is_empty()
	_panel.visible = _offering
	if _offering:
		if _buttons.has("play") and not frame.get("free_ball") is bool:
			Kit.missing(WHO, "free_ball")
		_prompt.text = _question(_buttons.keys(), frame, _opponent)
		_fit()


## The name the other player is called by: the opponent's level's, OPPONENT until given one. An
## offer already showing keeps its words.
func set_opponent_name(opponent: String) -> void:
	_opponent = opponent


func dismiss() -> void:
	_drop_buttons()
	_offering = false
	_panel.visible = false


func is_offering() -> bool:
	return _offering


## The button for a decision name; null when it isn't offered.
func button(kind: String) -> Button:
	return _buttons.get(kind)


## The question shown above the buttons, "" when nothing is offered.
func prompt() -> String:
	return _prompt.text if _offering else ""


## Puts the strip centred along the bottom of `area` (canvas units).
func place_in(area: Rect2) -> void:
	_area = area
	_fit()


## The canvas rect it covers while offering, else an empty rect.
func covered_rect() -> Rect2:
	return Rect2(_panel.position, _panel.size) if _offering else Rect2()


## A decision's button text (LABELS, the other player called `opponent`), or "Take the free ball"
## for play when `frame` called one; "" for an unknown kind.
static func label_for(kind: String, frame: Dictionary = {}, opponent: String = OPPONENT) -> String:
	if kind == "play" and frame.get("free_ball") == true:
		return FREE_BALL_PLAY
	return String(LABELS.get(kind, "")).replace(OPPONENT, opponent)


# The strip speaks to whoever decides, so after a foul the offender is always the other player.
static func _question(kinds: Array, frame: Dictionary, opponent: String) -> String:
	if kinds.has("play") or kinds.has("play_again") or kinds.has("replace"):
		var penalty := Kit.integer(frame, "last_penalty", WHO, 0)
		return "%s fouled: %d to you" % [opponent, penalty] if penalty > 0 else "%s fouled" % opponent
	if kinds.has("accept_rerack") or kinds.has("refuse_rerack"):
		return "Re-rack?"
	if kinds.has("play_first") or kinds.has("play_second"):
		return "You won the toss"
	return "You can't be caught"


func _choose(kind: String) -> void:
	dismiss()
	chosen.emit(kind)


func _drop_buttons() -> void:
	for kind: String in _buttons:
		var b: Button = _buttons[kind]
		_row.remove_child(b)
		b.queue_free()
	_buttons.clear()


func _fit() -> void:
	var extent := _panel.get_combined_minimum_size()
	Kit.put(_panel, Vector2(_area.get_center().x - extent.x / 2.0, _area.end.y - extent.y).round(), extent)
