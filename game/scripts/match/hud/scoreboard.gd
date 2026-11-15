class_name Scoreboard
extends Control
## The match's minimal scoreboard (spec 11.1): both scores, frames, the current break, points
## remaining, "snookers required" when relevant, the ball on, and whose turn it is; with it, the
## pause button. Sits at the top of the safe area and takes no touches except on that button.
##
## Contract skeleton (M3): the signatures and signal are fixed; the look and the body are the
## builder's.

signal pause_pressed

## When true the scoreboard keeps itself at the safe area's top; set false to call place_in.
var auto_place := true


## frame: the frame view; match: the match (serialize.hpp). person: which player (0 or 1) is "You".
func show_frame(_frame: Dictionary, _match: Dictionary, _person: int = 0) -> void:
	pass


## Puts the scoreboard along the top of `area` (canvas units), writing only what changed.
func place_in(_area: Rect2) -> void:
	pass


## The canvas rect it covers, for the camera to keep the table clear of.
func covered_rect() -> Rect2:
	return Rect2()


## The pause button, for tests and the harness.
func pause_button() -> Button:
	return null


## What the scoreboard says, for tests: {you, opponent, frames_you, frames_opponent, best_of,
## break, remaining, required (the words shown, "" when nobody requires penalty points), on (the
## ball on in words: "Red", "Colour", "Blue", "Free ball"), turn ("you" | "opponent")}.
func texts() -> Dictionary:
	return {}


## The ball on in words for a frame view: "Red", "Colour" (a colour of choice), the colour's name
## capitalised, or "Free ball" when one was called.
static func on_text(_frame: Dictionary) -> String:
	return ""
