class_name Guides
extends Node3D
## Guide lines (spec 8.4). Levels 0-2 are straight-line geometry only: 0 nothing, 1 the aim line
## to the first contact and a ghost ball there, 2 also a short line along the object ball's line
## of centres. Level 3 draws the noise-free simulated paths of the cue ball and object balls at
## ShotSetup.guide_pace_m_s(), shown beside the guide, recomputed only when something changed and
## at most PREDICT_MAX_PER_S times a second. Guides never alter the shot.

const PREDICT_MAX_PER_S := 5

## (balls: Array[Dictionary], cue_ball: int, strike: Dictionary) -> OfsShotRecord; tests replace it.
var predictor: Callable = Physics.predict
## Seconds; tests replace it.
var clock: Callable = func() -> float: return Time.get_ticks_usec() / 1.0e6


func show_for(_balls: Array[Dictionary], _cue_ball: int, _setup: ShotSetup, _level: int) -> void:
	pass


func hide_all() -> void:
	pass


## The straight-line first contact along aim_rad from the cue ball (radius r in metres): {kind:
## "ball" | "cushion" | "pocket" | "none", index (the ball, else -1), ghost: Vector2 (the cue
## ball's centre at contact, core xy), distance (metres the cue ball travels), object_dir: Vector2
## (unit, along the line of centres for a ball, else Vector2.ZERO)}. shape is
## OfsPhysics.table_shape().
static func first_contact(_balls: Array[Dictionary], _cue_ball: int, _aim_rad: float, _shape: Dictionary, _r: float) -> Dictionary:
	return {"kind": "none", "index": -1, "ghost": Vector2.ZERO, "distance": 0.0, "object_dir": Vector2.ZERO}
