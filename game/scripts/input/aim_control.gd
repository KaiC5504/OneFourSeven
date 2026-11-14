class_name AimControl
extends Node
## A one-finger drag on the table swings the cue round the cue ball (spec 8.1): horizontal
## movement turns the aim, finer when zoomed in (CameraRig.zoom_scale) and scaled by Settings
## "aim_sensitivity". While the aim line meets a ball (on_ball) it turns ON_BALL_SCALE as fast, for
## the last adjustments of a pot, and Settings "fine_tune_sensitivity" scales it instead (KaiC's
## phone sessions); elsewhere it swings at full speed. It never snaps to a ball or pocket.
##
## The movement that turned a touch into a drag (Gestures.DRAG_SLOP, handed over in one piece)
## turns nothing: the aim starts from rest under a new touch, so a correction can be as small as
## the finger can make it.
##
## Dragging right turns the aim clockwise seen from above (towards the player's right, -aim_rad),
## so the far end of the aim line follows the finger.

## Aim change per canvas unit of horizontal drag at the default zoom and sensitivity 1.
const BASE_RAD_PER_PX := 0.0015
const ON_BALL_SCALE := 0.25

var enabled := true
## () -> bool: whether the aim line's first contact is a ball; main.gd sets it.
var on_ball: Callable = func() -> bool: return false

var _setup: ShotSetup
var _gestures: Gestures
var _camera_rig: CameraRig
var _drag_fresh := false


func bind(setup: ShotSetup, gestures: Gestures, camera_rig: CameraRig) -> void:
	if _gestures != null and _gestures.drag_moved.is_connected(_on_drag_moved):
		_gestures.drag_moved.disconnect(_on_drag_moved)
		_gestures.drag_started.disconnect(_on_drag_started)
	_setup = setup
	_gestures = gestures
	_camera_rig = camera_rig
	if _gestures != null:
		_gestures.drag_moved.connect(_on_drag_moved)
		_gestures.drag_started.connect(_on_drag_started)


## The aim change one canvas unit of drag makes right now.
func rad_per_px() -> float:
	var zoom := _camera_rig.zoom_scale() if _camera_rig != null else 1.0
	if on_ball.call():
		return BASE_RAD_PER_PX * ON_BALL_SCALE * float(Settings.get_value("fine_tune_sensitivity")) * zoom
	return BASE_RAD_PER_PX * float(Settings.get_value("aim_sensitivity")) * zoom


func _on_drag_started(_position: Vector2) -> void:
	_drag_fresh = true


func _on_drag_moved(_position: Vector2, relative: Vector2) -> void:
	var across := relative.x
	if _drag_fresh:
		_drag_fresh = false
		across *= maxf(1.0 - Gestures.DRAG_SLOP / maxf(relative.length(), Gestures.DRAG_SLOP), 0.0)
	if not enabled or _setup == null:
		return
	_setup.add_aim(-across * rad_per_px())
