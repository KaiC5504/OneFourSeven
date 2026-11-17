class_name RefereeLine
extends Control
## The referee's calls as one line under the scoreboard (spec 11.2: no pop-ups): FOUL with its
## value, FOUL AND A MISS, FREE BALL, TOUCHING BALL, the warning before a frame is lost on misses,
## the re-spotted black, a re-rack. Each shows for a few seconds; nothing to dismiss, no touches.
##
## The line holds for HOLD_SECONDS, fades for FADE_SECONDS and is gone; a new announcement
## replaces the one showing. Each announcement logs match.call.

const Kit := preload("res://scripts/match/hud/hud_kit.gd")
const HOLD_SECONDS := 3.5
const FADE_SECONDS := 0.3
const SEPARATOR := " · "
const FONT_SIZE := 28
const OPPONENT := "Computer"
const WHO := "RefereeLine: a call"
const WORDS := {
	"miss": "FOUL AND A MISS",
	"free_ball": "FREE BALL",
	"touching_ball": "TOUCHING BALL",
	"warning": "Warning: miss again and the frame is lost",
	"respotted_black": "Black re-spotted",
	"rerack_offer": "Re-rack offered",
	"rerack": "Re-rack",
}
## A frame's end by its reason (end_reason_name); any other reason is "Frame over".
const FRAME_OVER := {
	"claimed": "Frame claimed",
	"awarded": "Frame awarded",
	"conceded": "Frame conceded",
	"three_misses": "Three misses: frame awarded",
	"match_conceded": "Match conceded",
}
## The reasons that say who the frame went to, since no stroke showed it.
const AWARDS: Array[String] = ["awarded", "three_misses"]
## The engine's other end reasons (names.cpp), said as "Frame over".
const PLAIN_ENDS: Array[String] = ["none", "black", "respotted_black"]

var _label := Label.new()
var _hold := Timer.new()
var _fade: Tween
var _text := ""
var _area := Rect2()
var _opponent := OPPONENT


func _init() -> void:
	mouse_filter = MOUSE_FILTER_IGNORE
	set_anchors_preset(PRESET_FULL_RECT)
	theme = SettingsPanel.make_theme()
	var panel := Kit.panel_style(0.78, 18)
	panel.content_margin_left = 24.0
	panel.content_margin_right = 24.0
	panel.content_margin_top = 8.0
	panel.content_margin_bottom = 8.0
	_label.name = "Line"
	_label.add_theme_stylebox_override("normal", panel)
	_label.add_theme_color_override("font_color", SettingsPanel.TEXT)
	_label.add_theme_font_size_override("font_size", FONT_SIZE)
	_label.mouse_filter = MOUSE_FILTER_IGNORE
	_label.visible = false
	add_child(_label)
	_hold.name = "Hold"
	_hold.one_shot = true
	_hold.wait_time = HOLD_SECONDS
	_hold.timeout.connect(_on_hold_over)
	add_child(_hold)


func _ready() -> void:
	# A line announced before this was in the tree was measured without its theme, and its hold
	# could not start.
	if _label.visible:
		_label.reset_size()
		_place()
		_hold.start()


## Shows `calls` (call Dictionaries {kind, value, detail}) in order, joined on one line.
## `person` is the player (0 or 1) the words address as "you"; `context` (match_id, frame,
## shot_id) goes on the match.call line. An empty list changes nothing.
func announce(calls: Array, person: int = 0, context: Dictionary = {}) -> void:
	var kinds: Array[String] = []
	for entry: Variant in calls:
		if not entry is Dictionary:
			Kit.problem("RefereeLine: a call is not a Dictionary")
			continue
		var call: Dictionary = entry
		var kind := Kit.text(call, "kind", WHO)
		if kind.is_empty():
			continue
		kinds.append(kind)
		if text_for(call, person, _opponent).is_empty():
			Kit.problem("RefereeLine: no words for the call \"%s\"" % kind)
		elif kind == "foul" and not _has_value(call):
			Kit.missing(WHO, "value")
		elif kind == "frame_over" and not call.get("detail") is String:
			Kit.missing(WHO, "detail")
		elif kind == "frame_over" and not FRAME_OVER.has(call["detail"]) and not PLAIN_ENDS.has(call["detail"]):
			Kit.problem("RefereeLine: no words for the end reason \"%s\"; said \"Frame over\"" % call["detail"])
	var line := line_for(calls, person, _opponent)
	if line.is_empty():
		return
	_end_fade()
	_text = line
	if _label.text != line:
		_label.text = line
		_label.reset_size()
	_label.visible = true
	_place()
	if is_inside_tree():
		_hold.start()
	var fields := {"calls": kinds, "text": line}
	for key: String in ["match_id", "frame", "shot_id"]:
		if context.has(key):
			fields[key] = context[key]
	Log.info("match", "match.call", fields)


## The name the other player is called by: the opponent's level's, OPPONENT until given one.
func set_opponent_name(opponent: String) -> void:
	_opponent = opponent


## Hides the line at once (a new frame, the match left).
func clear() -> void:
	_hold.stop()
	_end_fade()
	_text = ""
	_label.visible = false


## What is showing, "" when nothing is: also once the hold is over and the line is fading.
func text() -> String:
	return _text


## Puts the line centred at the top of `area` (canvas units).
func place_in(area: Rect2) -> void:
	_area = area
	_place()


## One call in words (WORDS, FRAME_OVER for frame_over's reason); "" for an unknown kind.
## `opponent` is the name an award to the other player is said with.
static func text_for(call: Dictionary, person: int = 0, opponent: String = OPPONENT) -> String:
	var kind := str(call.get("kind", ""))
	match kind:
		"foul":
			return "FOUL %d" % int(call["value"]) if _has_value(call) else "FOUL"
		"frame_over":
			var reason := str(call.get("detail", ""))
			var words: String = FRAME_OVER.get(reason, "Frame over")
			if AWARDS.has(reason) and _has_value(call):
				words += " to you" if int(call["value"]) == person else " to " + opponent
			return words
	return WORDS.get(kind, "")


## The whole line announce shows for `calls`: each call's words in order, joined by SEPARATOR. A
## foul followed by its miss is said once, as the referee does: "FOUL AND A MISS 4".
static func line_for(calls: Array, person: int = 0, opponent: String = OPPONENT) -> String:
	var said := PackedStringArray()
	for i in calls.size():
		var kind := _kind_at(calls, i)
		if kind.is_empty() or (kind == "foul" and _kind_at(calls, i + 1) == "miss"):
			continue
		var words := text_for(calls[i], person, opponent)
		if kind == "miss" and _kind_at(calls, i - 1) == "foul" and _has_value(calls[i - 1]):
			words += " %d" % int(calls[i - 1]["value"])
		if not words.is_empty():
			said.append(words)
	return SEPARATOR.join(said)


# The kind of the call at place `i`; "" when there is no call there.
static func _kind_at(calls: Array, i: int) -> String:
	if i < 0 or i >= calls.size() or not calls[i] is Dictionary:
		return ""
	return str((calls[i] as Dictionary).get("kind", ""))


static func _has_value(call: Dictionary) -> bool:
	var value: Variant = call.get("value")
	return value is int or (value is float and is_finite(value))


func _place() -> void:
	var at := Vector2(_area.get_center().x - _label.size.x / 2.0, _area.position.y).round()
	if _label.position != at:
		_label.position = at


func _on_hold_over() -> void:
	_text = ""
	if not is_inside_tree():
		_label.visible = false
		return
	_fade = create_tween()
	_fade.tween_property(_label, "modulate:a", 0.0, FADE_SECONDS)
	_fade.tween_callback(_end_fade)


func _end_fade() -> void:
	if _fade != null:
		_fade.kill()
		_fade = null
		_label.visible = false
	if _label.modulate.a != 1.0:
		_label.modulate.a = 1.0
