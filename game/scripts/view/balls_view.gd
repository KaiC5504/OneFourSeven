class_name BallsView
extends Node3D
## The balls (spec 13.1). Children are built from a ball list in its order, so state index i is
## child i. Each is named "cue", "red_<index>" or the colour, with metas "ball", "index" and
## "core_xy" ([x, y] at full precision; the harness's build_state reads them). Spin shows by
## turning each ball by its angular velocity; markings make it visible on the cue ball.


## Replaces every ball with the list's (the free table after a shot, an edit, a retake).
func build(_balls: Array[Dictionary]) -> void:
	pass


## Moves every ball to `state` (ShotPlayback layout) and turns it by its spin over dt seconds of
## shot time. A ball that has become pocketed plays its drop into the pocket and then hides.
func apply_state(_state: PackedFloat64Array, _dt: float) -> void:
	pass


## The index of the ball under a screen point, or -1.
func ball_at_screen(_camera: Camera3D, _screen_pos: Vector2) -> int:
	return -1
