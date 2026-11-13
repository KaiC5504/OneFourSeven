class_name SpinWidget
extends Control
## The cue-ball widget (spec 8.2): tap it to enlarge, then drag the tip marker for top, screw
## and side. The miscue limit (Physics.param("miscue_limit")) is drawn as a ring; the marker can go
## past it, up to MAX_TIP, and such a shot miscues.

signal tip_changed(tip: Vector2)

## Furthest tip offset the marker reaches, in ball radii.
const MAX_TIP := 0.8

var expanded := false


func bind(_setup: ShotSetup) -> void:
	pass


## A point in this control's local coordinates to tip offsets (x right, y up, ball radii),
## clamped to MAX_TIP, for the current size and expanded state.
func tip_for_local(_local: Vector2) -> Vector2:
	return Vector2.ZERO
