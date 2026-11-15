class_name MatchSummary
extends Control
## At the end of a match (spec 11.1): who won, the frames, each frame's score in a short list, the
## highest break of the match for each player, and one button back to Home. A card in the middle
## of the safe area over the table.
##
## Contract skeleton (M3): the signatures and signal are fixed; the look and the body are the
## builder's.

signal home_pressed

var auto_place := true


## match: the finished match (serialize.hpp); person: which player (0 or 1) is "You".
func show_summary(_match: Dictionary, _person: int = 0) -> void:
	visible = true


func close() -> void:
	visible = false


## "home".
func button(_id: String) -> Button:
	return null


## What the card says, for tests: {title ("You won" | "You lost"), frames_you, frames_opponent,
## frames: ["63-41", ...] one per frame played with the person's score first, break_you,
## break_opponent}.
func texts() -> Dictionary:
	return {}


func place_in(_safe_rect: Rect2) -> void:
	pass
