class_name MatchSummary
extends Control
## At the end of a match (spec 11.1): who won, the frames, each frame's score in a short list, the
## highest break of the match for each player, and one button back to Home. A card in the middle
## of the safe area over the table.
##
## Laid out as the frame summary is: the person's numbers on the left, the opponent's on the
## right, the frames under the totals with each frame's winning score the brighter. A match that
## lacks a key shows a dash there and logs one app.error naming what was missing.

signal home_pressed

const Kit := preload("res://scripts/ui/screens/screen_kit.gd")
const TITLE_FONT_SIZE := 48
const FRAMES_FONT_SIZE := 56
const VALUE_FONT_SIZE := 30
## The longest match is eleven frames: past six the list runs in two narrower columns, so the
## card always fits the canvas's height.
const LIST_ROWS := 6
const LIST_GAP := 40
const NARROW_VALUE_WIDTH := 70.0
const NARROW_CAPTION_WIDTH := 140.0

var auto_place := true

var _texts := {}
var _title: Label
var _frames: Array[Label]
var _break: Array[Label]
var _divider: ColorRect
var _list: HBoxContainer
var _home: Button
var _card: PanelContainer


func _init() -> void:
	visible = false
	set_anchors_preset(PRESET_FULL_RECT)
	_build()


func _ready() -> void:
	Kit.follow_safe_area(self, _auto_place)


## match_state: the finished match (serialize.hpp); person: which player (0 or 1) is "You".
func show_summary(match_state: Dictionary, person: int = 0) -> void:
	var you := clampi(person, 0, 1)
	var them := 1 - you
	var missing: Array[String] = []
	var winner := Kit.number(match_state, "match", ["winner"], missing)
	var title := "Match over"
	if winner == 0 or winner == 1:
		title = "You won" if winner == you else "You lost"
	# Each frame: the person's score, the opponent's, and which of the two lost it (-1: unknown).
	var frames: Array[Array] = []
	var scores: Array[String] = []
	var break_you := -1
	var break_them := -1
	var history: Variant = match_state.get("history")
	if history is Array:
		for i in (history as Array).size():
			var source := "match.history.%d" % i
			var mine := -1
			var theirs := -1
			var loser := -1
			if history[i] is Dictionary:
				var frame: Dictionary = history[i]
				mine = Kit.number(frame, source, ["scores", you], missing)
				theirs = Kit.number(frame, source, ["scores", them], missing)
				var won := Kit.number(frame, source, ["winner"], missing)
				if won == 0 or won == 1:
					loser = 1 if won == you else 0
				break_you = maxi(break_you, Kit.number(frame, source, ["stats", you, "highest_break"], missing))
				break_them = maxi(break_them, Kit.number(frame, source, ["stats", them, "highest_break"], missing))
			else:
				missing.append(source)
			frames.append([Kit.number_text(mine), Kit.number_text(theirs), loser])
			scores.append("%s-%s" % [Kit.number_text(mine), Kit.number_text(theirs)])
	else:
		missing.append("match.history")
	_texts = {
		"title": title,
		"frames_you": Kit.number(match_state, "match", ["frames", you], missing),
		"frames_opponent": Kit.number(match_state, "match", ["frames", them], missing),
		"frames": scores,
		"break_you": break_you,
		"break_opponent": break_them,
	}
	Kit.report_missing("MatchSummary", missing)
	_title.text = title
	_frames[0].text = Kit.number_text(_texts["frames_you"])
	_frames[1].text = Kit.number_text(_texts["frames_opponent"])
	_break[0].text = Kit.number_text(break_you)
	_break[1].text = Kit.number_text(break_them)
	_list_frames(frames)
	visible = true
	_auto_place()


func close() -> void:
	visible = false


## "home".
func button(id: String) -> Button:
	return _home if id == "home" else null


## What the card says, for tests: {title ("You won" | "You lost"), frames_you, frames_opponent,
## frames: ["63-41", ...] one per frame played with the person's score first, break_you,
## break_opponent}. The frames won and the breaks are ints, -1 where the Dictionary had no such
## key (the card shows a dash).
func texts() -> Dictionary:
	return _texts.duplicate(true)


func place_in(safe_rect: Rect2) -> void:
	Kit.centre(_card, safe_rect)


func _build() -> void:
	theme = SettingsPanel.make_theme()
	Kit.backdrop(self)
	var body := Kit.card(self)
	_card = body.get_parent()
	_title = Kit.label("", TITLE_FONT_SIZE)
	body.add_child(_title)
	var totals := Kit.table()
	body.add_child(totals)
	Kit.heading(totals)
	_frames = Kit.row(totals, "Frames", FRAMES_FONT_SIZE)
	_break = Kit.row(totals, "Highest break", VALUE_FONT_SIZE)
	_divider = Kit.divider()
	body.add_child(_divider)
	_list = HBoxContainer.new()
	_list.add_theme_constant_override("separation", LIST_GAP)
	_list.size_flags_horizontal = SIZE_SHRINK_CENTER
	body.add_child(_list)
	_home = Kit.button("Home", Vector2(0.0, Kit.TOUCH), true)
	_home.pressed.connect(home_pressed.emit)
	body.add_child(_home)


## A row for each frame, down the first column and then the second.
func _list_frames(frames: Array[Array]) -> void:
	for child in _list.get_children():
		_list.remove_child(child)
		child.queue_free()
	var narrow := frames.size() > LIST_ROWS
	var rows := ceili(frames.size() / 2.0) if narrow else frames.size()
	var column: GridContainer
	for i in frames.size():
		if i % rows == 0:
			column = Kit.table()
			column.add_theme_constant_override("v_separation", 2)
			_list.add_child(column)
		var cells := Kit.row(column, "Frame %d" % (i + 1), VALUE_FONT_SIZE, SettingsPanel.TEXT,
				NARROW_VALUE_WIDTH if narrow else Kit.VALUE_WIDTH,
				NARROW_CAPTION_WIDTH if narrow else Kit.CAPTION_WIDTH)
		cells[0].text = frames[i][0]
		cells[1].text = frames[i][1]
		if frames[i][2] >= 0:
			cells[frames[i][2]].add_theme_color_override("font_color", SettingsPanel.MUTED)
	_divider.visible = not frames.is_empty()
	_list.visible = not frames.is_empty()


func _auto_place() -> void:
	if auto_place and is_inside_tree() and visible:
		place_in(SettingsPanel.canvas_safe_rect(self))
