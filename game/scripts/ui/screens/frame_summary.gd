class_name FrameSummary
extends Control
## After each frame (spec 11.1): who won it and why, the frame score, each player's highest break
## and pot success, the match score in frames, and one button on to the next frame. A card in the
## middle of the safe area over the table.
##
## The numbers stand as on a broadcast's comparison: the person's on the left, the opponent's on
## the right, what they are between them. A summary or match that lacks a key shows a dash there
## and logs one app.error naming what was missing.

signal continue_pressed

const Kit := preload("res://scripts/ui/screens/screen_kit.gd")
const HudKit := preload("res://scripts/match/hud/hud_kit.gd")
const SCORE_FONT_SIZE := 56
const VALUE_FONT_SIZE := 30
## Past the two that end on a pot, the referee's words for each (RefereeLine.FRAME_OVER).
const REASON_TEXT := {
	"black": "On the black",
	"respotted_black": "On the re-spotted black",
	"claimed": "Frame claimed",
	"awarded": "Frame awarded",
	"conceded": "Frame conceded",
	"three_misses": "Three misses: frame awarded",
	"match_conceded": "Match conceded",
}
const NO_REASON := "Frame over"

var auto_place := true

var _texts := {}
var _title: Label
var _reason: Label
var _score: Array[Label]
var _break: Array[Label]
var _pots: Array[Label]
var _frames: Array[Label]
var _continue: Button
var _card: PanelContainer


func _init() -> void:
	visible = false
	set_anchors_preset(PRESET_FULL_RECT)
	_build()


func _ready() -> void:
	Kit.follow_safe_area(self, _auto_place)


## summary: the finished frame's summary; match_state: the match after it (serialize.hpp); person:
## which player (0 or 1) is "You".
func show_summary(summary: Dictionary, match_state: Dictionary, person: int = 0) -> void:
	var you := clampi(person, 0, 1)
	var them := 1 - you
	var missing: Array[String] = []
	var winner := Kit.number(summary, "summary", ["winner"], missing)
	var title := "Frame over"
	if winner == 0 or winner == 1:
		title = "You won the frame" if winner == you else "You lost the frame"
	var reason: Variant = summary.get("reason")
	var why := ""
	if reason is not String:
		missing.append("summary.reason")
	else:
		why = reason_text(reason)
		if why.is_empty():
			HudKit.problem("FrameSummary: no words for the end reason \"%s\"" % reason)
			why = NO_REASON
	_texts = {
		"title": title,
		"reason": why,
		"score_you": Kit.number(summary, "summary", ["scores", you], missing),
		"score_opponent": Kit.number(summary, "summary", ["scores", them], missing),
		"break_you": Kit.number(summary, "summary", ["stats", you, "highest_break"], missing),
		"break_opponent": Kit.number(summary, "summary", ["stats", them, "highest_break"], missing),
		"pots_you": _pots_text(summary, you, missing),
		"pots_opponent": _pots_text(summary, them, missing),
		"frames_you": Kit.number(match_state, "match", ["frames", you], missing),
		"frames_opponent": Kit.number(match_state, "match", ["frames", them], missing),
	}
	Kit.report_missing("FrameSummary", missing)
	_title.text = _texts["title"]
	_reason.text = _texts["reason"]
	_reason.visible = not _reason.text.is_empty()
	_score[0].text = Kit.number_text(_texts["score_you"])
	_score[1].text = Kit.number_text(_texts["score_opponent"])
	_break[0].text = Kit.number_text(_texts["break_you"])
	_break[1].text = Kit.number_text(_texts["break_opponent"])
	_pots[0].text = _texts["pots_you"]
	_pots[1].text = _texts["pots_opponent"]
	_frames[0].text = Kit.number_text(_texts["frames_you"])
	_frames[1].text = Kit.number_text(_texts["frames_opponent"])
	visible = true
	_auto_place()


func close() -> void:
	visible = false


## "continue".
func button(id: String) -> Button:
	return _continue if id == "continue" else null


## What the card says, for tests: {title, reason, score_you, score_opponent, break_you,
## break_opponent, pots_you, pots_opponent (as "7 of 12"), frames_you, frames_opponent}. The
## scores, breaks and frames are ints, -1 where the Dictionary had no such key (the card shows a
## dash); the rest are Strings.
func texts() -> Dictionary:
	return _texts.duplicate()


func place_in(safe_rect: Rect2) -> void:
	Kit.centre(_card, safe_rect)


## Why a frame ended, in words, for an end reason name; "" for one with no words.
static func reason_text(reason: String) -> String:
	return String(REASON_TEXT.get(reason, ""))


func _build() -> void:
	theme = SettingsPanel.make_theme()
	Kit.backdrop(self)
	var body := Kit.card(self)
	_card = body.get_parent()
	var heading := VBoxContainer.new()
	heading.add_theme_constant_override("separation", 0)
	body.add_child(heading)
	_title = Kit.label("", Kit.TITLE_FONT_SIZE)
	heading.add_child(_title)
	_reason = Kit.label("", Kit.SMALL_FONT_SIZE, SettingsPanel.MUTED)
	heading.add_child(_reason)

	var frame := Kit.table()
	body.add_child(frame)
	Kit.heading(frame)
	_score = Kit.row(frame, "Score", SCORE_FONT_SIZE)
	_break = Kit.row(frame, "Highest break", VALUE_FONT_SIZE)
	_pots = Kit.row(frame, "Pot success", VALUE_FONT_SIZE)
	body.add_child(Kit.divider())
	var match_table := Kit.table()
	body.add_child(match_table)
	_frames = Kit.row(match_table, "Frames", VALUE_FONT_SIZE)

	_continue = Kit.button("Continue", Vector2(0.0, Kit.TOUCH), true)
	_continue.pressed.connect(continue_pressed.emit)
	body.add_child(_continue)


## "7 of 12": the strokes that scored of the strokes that were attempts to pot.
func _pots_text(summary: Dictionary, player: int, missing: Array[String]) -> String:
	var pots := Kit.number(summary, "summary", ["stats", player, "pots"], missing)
	var attempts := Kit.number(summary, "summary", ["stats", player, "pot_attempts"], missing)
	return "%s of %s" % [Kit.number_text(pots), Kit.number_text(attempts)]


func _auto_place() -> void:
	if auto_place and is_inside_tree() and visible:
		place_in(SettingsPanel.canvas_safe_rect(self))
