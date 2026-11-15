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
## A finger rolls as it lifts, and the screen reports that as a last small movement: the line
## moved by itself after the player let go (KaiC, 2026-11-15). So what the aim turned in the last
## LIFT_S before the release is taken back, unless the finger was still travelling (further than
## LIFT_MAX_UNITS in that time), which is a drag let go on the move.
##
## Dragging right turns the aim clockwise seen from above (towards the player's right, -aim_rad),
## so the far end of the aim line follows the finger.

## Aim change per canvas unit of horizontal drag at the default zoom and sensitivity 1.
const BASE_RAD_PER_PX := 0.0015
const ON_BALL_SCALE := 0.25
const LIFT_S := 0.06
## Canvas units: 5 pt on the phone.
const LIFT_MAX_UNITS := 10.0

var enabled := true
## () -> bool: whether the aim line's first contact is a ball; main.gd sets it.
var on_ball: Callable = func() -> bool: return false
## Seconds; tests replace it.
var clock: Callable = func() -> float: return Time.get_ticks_usec() / 1.0e6

var _setup: ShotSetup
var _gestures: Gestures
var _camera_rig: CameraRig
var _drag_fresh := false
## This drag's latest moves, oldest first: [seconds, canvas units moved, radians the aim turned].
var _trail: Array[PackedFloat64Array] = []


func bind(setup: ShotSetup, gestures: Gestures, camera_rig: CameraRig) -> void:
	if _gestures != null and _gestures.drag_moved.is_connected(_on_drag_moved):
		_gestures.drag_moved.disconnect(_on_drag_moved)
		_gestures.drag_started.disconnect(_on_drag_started)
		_gestures.drag_ended.disconnect(_on_drag_ended)
	_setup = setup
	_gestures = gestures
	_camera_rig = camera_rig
	if _gestures != null:
		_gestures.drag_moved.connect(_on_drag_moved)
		_gestures.drag_started.connect(_on_drag_started)
		_gestures.drag_ended.connect(_on_drag_ended)


## The aim change one canvas unit of drag makes right now.
func rad_per_px() -> float:
	var zoom := _camera_rig.zoom_scale() if _camera_rig != null else 1.0
	if on_ball.call():
		return BASE_RAD_PER_PX * ON_BALL_SCALE * float(Settings.get_value("fine_tune_sensitivity")) * zoom
	return BASE_RAD_PER_PX * float(Settings.get_value("aim_sensitivity")) * zoom


func _on_drag_started(_position: Vector2) -> void:
	_drag_fresh = true
	_trail.clear()


func _on_drag_moved(_position: Vector2, relative: Vector2) -> void:
	var across := relative.x
	if _drag_fresh:
		_drag_fresh = false
		across *= maxf(1.0 - Gestures.DRAG_SLOP / maxf(relative.length(), Gestures.DRAG_SLOP), 0.0)
	if not enabled or _setup == null:
		return
	var turned := -across * rad_per_px()
	var now := float(clock.call())
	while not _trail.is_empty() and now - _trail[0][0] > LIFT_S:
		_trail.pop_front()
	_trail.append(PackedFloat64Array([now, relative.length(), turned]))
	_setup.add_aim(turned)


func _on_drag_ended(_position: Vector2) -> void:
	var now := float(clock.call())
	var moved := 0.0
	var turned := 0.0
	for move in _trail:
		if now - move[0] <= LIFT_S:
			moved += move[1]
			turned += move[2]
	_trail.clear()
	if moved <= 0.0 or not enabled or _setup == null:
		return
	var taken_back := moved <= LIFT_MAX_UNITS
	if taken_back:
		_setup.add_aim(-turned)
	Log.info("input", "aim.lift", {"moved": moved, "turned_rad": turned, "taken_back": taken_back})
