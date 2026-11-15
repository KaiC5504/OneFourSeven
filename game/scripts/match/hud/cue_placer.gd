class_name CuePlacer
extends Control
## Ball in hand (spec 8.6): the cue ball is dragged within the D, kept to legal places, and a
## Confirm button commits it. This control is the button and the hint beside it, low in the safe
## area; the table scene feeds it the drag in core metres and moves the ball it answers with.
## After Confirm it shrinks to a small "Move" button while the cue ball is still in hand.
##
## Contract skeleton (M3): the signatures and signals are fixed; the look and the body are the
## builder's.

## The cue ball should show at `xy` (already legal) while the finger moves.
signal moved(xy: Vector2)
## The finger lifted with the cue ball at `xy`: commit it to the engine.
signal dropped(xy: Vector2)
signal confirmed
## "Move" was pressed after Confirm: back to placing.
signal move_requested


## Starts placing. `nearest` maps a wanted place (core metres) to the nearest legal one
## (MatchController.nearest_cue_placement).
func begin(_nearest: Callable) -> void:
	pass


## The cue ball is no longer in hand (the stroke was played, the match left): hides everything.
func end() -> void:
	pass


## True while the cue ball follows a drag (before Confirm).
func is_placing() -> bool:
	return false


## A drag on the table at `xy` (core metres): emits moved with the nearest legal place. Nothing
## when not placing.
func drag(_xy: Vector2) -> void:
	pass


## The drag ended at `xy`: emits dropped with the nearest legal place.
func drop(_xy: Vector2) -> void:
	pass


## "confirm" or "move"; null when that button is not showing.
func button(_id: String) -> Button:
	return null


## Puts the control centred along the bottom of `area` (canvas units).
func place_in(_area: Rect2) -> void:
	pass


func covered_rect() -> Rect2:
	return Rect2()
