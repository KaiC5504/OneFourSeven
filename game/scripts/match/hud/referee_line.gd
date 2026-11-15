class_name RefereeLine
extends Control
## The referee's calls as one line under the scoreboard (spec 11.2: no pop-ups): FOUL with its
## value, FOUL AND A MISS, FREE BALL, TOUCHING BALL, the warning before a frame is lost on misses,
## the re-spotted black, a re-rack. Each shows for a few seconds; nothing to dismiss, no touches.
##
## Contract skeleton (M3): the signatures are fixed; the look and the body are the builder's.

const HOLD_SECONDS := 3.5


## Shows `calls` (call Dictionaries {kind, value, detail}) in order, joined on one line.
## `person` is the player (0 or 1) the words address as "you". An empty list changes nothing.
func announce(_calls: Array, _person: int = 0) -> void:
	pass


## Hides the line at once (a new frame, the match left).
func clear() -> void:
	pass


## What is showing, "" when nothing is.
func text() -> String:
	return ""


## Puts the line centred at the top of `area` (canvas units).
func place_in(_area: Rect2) -> void:
	pass


## One call in words: "FOUL 4", "FOUL AND A MISS", "FREE BALL", "TOUCHING BALL", "Warning: miss
## again and the frame is lost", "Black re-spotted", "Re-rack offered", "Re-rack", and for
## frame_over the reason in words ("Frame conceded", "Frame over", ...). "" for an unknown kind.
static func text_for(_call: Dictionary, _person: int = 0) -> String:
	return ""
