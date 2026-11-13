class_name AimControl
extends Node
## A one-finger drag on the table swings the cue round the cue ball (spec 8.1): horizontal
## movement turns the aim, finer when zoomed in (CameraRig.zoom_scale) and scaled by Settings
## "aim_sensitivity". It never snaps to a ball or pocket.

## Aim change per canvas unit of horizontal drag at the default zoom and sensitivity 1.
const BASE_RAD_PER_PX := 0.0015

var enabled := true


func bind(_setup: ShotSetup, _gestures: Gestures, _camera_rig: CameraRig) -> void:
	pass


## The aim change one canvas unit of drag makes right now.
func rad_per_px() -> float:
	return BASE_RAD_PER_PX
