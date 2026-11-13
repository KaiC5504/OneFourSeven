class_name CameraRig
extends Node3D
## The camera behind the cue ball, looking along the aim (spec 8.5). Pinch zooms; a two-finger
## vertical drag tilts it from cue level up to a nearly overhead planning view. While a shot plays
## it holds still. Child: Camera3D.

signal moved

const PITCH_MIN_DEG := 4.0
const PITCH_MAX_DEG := 82.0


## Puts the rig behind cue_xy (core metres) along aim_rad.
func follow(_cue_xy: Vector2, _aim_rad: float) -> void:
	pass


func pinch(_factor: float) -> void:
	pass


## relative_px: the two-finger drag's vertical movement in canvas units, down positive.
func tilt(_relative_px: float) -> void:
	pass


## 1 at the default zoom, smaller when zoomed in; aim sensitivity is multiplied by it (spec 8.1).
func zoom_scale() -> float:
	return 1.0


func pitch_deg() -> float:
	return 0.0


func hold(_on: bool) -> void:
	pass


func camera() -> Camera3D:
	return $Camera3D
