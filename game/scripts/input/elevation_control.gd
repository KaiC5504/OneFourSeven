class_name ElevationControl
extends Control
## Cue elevation (spec 8.2): from the obstruction minimum (spec 7.3) up to max_elevation_rad,
## near vertical for masse. Shows when the minimum has forced the cue above what was asked for.

signal elevation_changed(rad: float)


func bind(_setup: ShotSetup) -> void:
	pass


## Shown as forced while ShotSetup.elevation_forced().
func is_forced() -> bool:
	return false
