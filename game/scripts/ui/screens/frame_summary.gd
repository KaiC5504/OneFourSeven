class_name FrameSummary
extends Control
## After each frame (spec 11.1): who won it and why, the frame score, each player's highest break
## and pot success, the match score in frames, and one button on to the next frame. A card in the
## middle of the safe area over the table.
##
## Contract skeleton (M3): the signatures and signal are fixed; the look and the body are the
## builder's.

signal continue_pressed

var auto_place := true


## summary: the finished frame's summary; match: the match after it (serialize.hpp); person: which
## player (0 or 1) is "You".
func show_summary(_summary: Dictionary, _match: Dictionary, _person: int = 0) -> void:
	visible = true


func close() -> void:
	visible = false


## "continue".
func button(_id: String) -> Button:
	return null


## What the card says, for tests: {title, reason, score_you, score_opponent, break_you,
## break_opponent, pots_you, pots_opponent (as "7 of 12"), frames_you, frames_opponent}.
func texts() -> Dictionary:
	return {}


func place_in(_safe_rect: Rect2) -> void:
	pass


## Why a frame ended, in words, for an end reason name: black "On the black", respotted_black "On
## the re-spotted black", claimed and awarded "Frame claimed", conceded "Conceded", three_misses
## "Three misses", match_conceded "Match conceded".
static func reason_text(_reason: String) -> String:
	return ""
