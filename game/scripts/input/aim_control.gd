class_name AimControl
extends Node
## A one-finger drag on the table swings the cue round the cue ball (spec 8.1): horizontal
## movement turns the aim, finer when zoomed in (CameraRig.zoom_scale) and scaled by Settings
## "aim_sensitivity". While the aim line meets a ball (on_ball) it turns ON_BALL_SCALE as fast, for
## the last adjustments of a pot (KaiC's phone session); elsewhere it swings at full speed. It never
## snaps to a ball or pocket.
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


func bind(setup: ShotSetup, gestures: Gestures, camera_rig: CameraRig) -> void:
	if _gestures != null and _gestures.drag_moved.is_connected(_on_drag_moved):
		_gestures.drag_moved.disconnect(_on_drag_moved)
	_setup = setup
	_gestures = gestures
	_camera_rig = camera_rig
	if _gestures != null:
		_gestures.drag_moved.connect(_on_drag_moved)


## The aim change one canvas unit of drag makes right now.
func rad_per_px() -> float:
	var zoom := _camera_rig.zoom_scale() if _camera_rig != null else 1.0
	var fine := ON_BALL_SCALE if on_ball.call() else 1.0
	return BASE_RAD_PER_PX * float(Settings.get_value("aim_sensitivity")) * zoom * fine


func _on_drag_moved(_position: Vector2, relative: Vector2) -> void:
	if not enabled or _setup == null:
		return
	_setup.add_aim(-relative.x * rad_per_px())
