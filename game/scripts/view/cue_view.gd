class_name CueView
extends Node3D
## The cue (spec 7.3: a tapered cylinder, tip 9.5 mm, butt 29 mm, 1450 mm long) behind the cue
## ball along the aim, raised to the elevation, its tip at the chosen offset, pulled back by the
## stroke's backswing.


## cue_xy in core metres; tip in ball radii (x right, y up).
func set_pose(_cue_xy: Vector2, _aim_rad: float, _elevation_rad: float, _tip: Vector2, _backswing_m: float) -> void:
	pass


## The stroke going through to the ball, then the cue hides for the shot.
func play_strike(_speed_m_s: float) -> void:
	pass


func show_cue(_on: bool) -> void:
	pass
