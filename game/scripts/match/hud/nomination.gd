class_name Nomination
extends Node3D
## Nomination (spec 8.7): which colour the striker declares. The game pre-selects the ball the aim
## line hits first; a tap on any eligible ball changes it; the declared ball wears a ring on the
## cloth. This node is the ring; the static functions are the choosing, with no state.
##
## Contract skeleton (M3): the signatures are fixed; the look and the body are the builder's.

## Rings the ball at `xy` (core metres); null hides the ring.
func mark(_xy: Variant) -> void:
	pass


func is_marking() -> bool:
	return false


## The place in `balls` of the first ball the cue ball would hit travelling along `aim_rad` from
## its place, taking both as circles of `radius`; -1 when it would hit none.
static func first_on_line(_balls: Array[Dictionary], _cue_ball: int, _aim_rad: float, _radius: float) -> int:
	return -1


## The colour to pre-select: the first ball on the aim line when it is one of `eligible` (colour
## names), else "" (the line points at no eligible ball).
static func preselect(_balls: Array[Dictionary], _cue_ball: int, _aim_rad: float, _eligible: Array[String],
		_radius: float) -> String:
	return ""


## The colour a tap chose: the ball at place `tapped` when it is one of `eligible`, else "".
static func tapped(_balls: Array[Dictionary], _tapped: int, _eligible: Array[String]) -> String:
	return ""


## The place of the colour `ball` in `balls`; -1 when it is not on the table.
static func place_of(_balls: Array[Dictionary], _ball: String) -> int:
	return -1
