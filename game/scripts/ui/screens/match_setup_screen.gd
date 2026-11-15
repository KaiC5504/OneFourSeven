class_name MatchSetupScreen
extends Control
## Match setup (spec 11.1): match length (best of 1, 3, 5, 7, 9 or 11 frames) and who breaks first
## (random, you or the computer; random by default), then Start. The opponent's level arrives with
## the opponent in M4 and has no row until then. The last choices are remembered for the session.
##
## Contract skeleton (M3): the signatures and signals are fixed; the look and the body are the
## builder's.

## config: {best_of: int, first_breaker: "random" | "you" | "computer"}.
signal start_pressed(config: Dictionary)
signal back_pressed

const LENGTHS: Array[int] = [1, 3, 5, 7, 9, 11]
const BREAKERS: Array[String] = ["random", "you", "computer"]

var auto_place := true


func open() -> void:
	visible = true


func close() -> void:
	visible = false


## What Start would send now.
func config() -> Dictionary:
	return {"best_of": 3, "first_breaker": "random"}


## "start", "back", "best_of:<n>" or "first_breaker:<name>".
func control(_id: String) -> Control:
	return null


func place_in(_safe_rect: Rect2) -> void:
	pass
