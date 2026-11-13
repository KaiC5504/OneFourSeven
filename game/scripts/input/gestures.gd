class_name Gestures
extends Node
## The input layer (spec 8.1, 8.5, 8.8): touches and the mouse turned into one set of gestures.
## It reads _unhandled_input, so HUD controls take their own touches first. On Windows a left-drag
## is a one-finger drag, the wheel is pinch and a right-drag is a two-finger drag. Positions are
## in the viewport's canvas coordinates.

signal drag_started(position: Vector2)
signal drag_moved(position: Vector2, relative: Vector2)
signal drag_ended(position: Vector2)
signal tapped(position: Vector2)
## factor > 1 zooms in.
signal pinched(factor: float, centre: Vector2)
signal two_finger_dragged(relative: Vector2)
signal three_finger_tapped

var enabled := true


## What _unhandled_input does with an event; tests call it directly.
func feed(_event: InputEvent) -> void:
	pass
