class_name CameraRig
extends Node3D
## The camera behind the cue ball, looking along the aim (spec 8.5). Pinch zooms; a two-finger
## vertical drag tilts it from cue level up to a nearly overhead planning view. While a shot plays
## it holds still. Child: Camera3D.
##
## The rig itself carries the pose; the camera sits at its origin. Up to PLAN_FROM_DEG the camera
## looks at a point LOOK_AHEAD_M along the aim line, so the cue ball sits low in view with the line
## ahead above it. From there to PITCH_MAX_DEG the framing pulls out to FAR_DISTANCE_M and the
## point it looks at slides towards the table centre, so the high angle shows the whole table for
## planning. Pinching scales the distance and the look-ahead together (a dolly towards the cue
## ball); in the planning view zooming in also slides the view back towards the cue ball.
##
## A raised cue lifts the camera to at least ABOVE_CUE_DEG above the cue's own angle, so the cue
## comes down to the ball from below the eye as a player sees it over the butt, and its butt passes
## under the camera instead of through the view. The lift changes the angle only, not the framing.
##
## Finger-driven changes (aim drags, pinch, tilt, the elevation control) apply at once. A follow()
## that moves the cue ball more than JUMP_M or the aim more than JUMP_RAD, an elevation change of
## more than ELEVATION_JUMP_RAD (the cue forced over a ball or the rail), or the first change after
## a hold, eases there over EASE_S instead.
##
## While a shot plays (held), watch() frames the moving balls (spec 8.5's auto-rise, which KaiC
## asked for): the camera rises up and out behind the shot, along the aim it was struck on, and
## glides after the action; it eases back behind the cue ball over RETURN_S when the hold ends.
## set_overview() toggles the planning view (the highest tilt, whole table) and back.

signal moved
signal overview_changed(on: bool)

const PITCH_MIN_DEG := 4.0
const PITCH_MAX_DEG := 82.0
const DEFAULT_PITCH_DEG := 16.0
## Vertical. The phone's 2.17:1 screen makes the horizontal view about twice as wide, and balls at
## its edges stretch with the angle off the centre; 40 degrees stretched them about 13 %.
const FOV_DEG := 34.0
const NEAR_M := 0.02
const FAR_M := 40.0
## Camera to the point it looks at, at the default zoom, below PLAN_FROM_DEG.
const NEAR_DISTANCE_M := 1.55
## At PITCH_MAX_DEG: the whole table, rails and pockets included, in the view aimed along its length.
const FAR_DISTANCE_M := 7.6
const LOOK_AHEAD_M := 0.5
const PLAN_FROM_DEG := 25.0
const ZOOM_MIN := 0.35
const ZOOM_MAX := 1.6
## Two-finger drag down raises the camera, as if tilting the table towards you; about 400 canvas
## units (half the screen height) cover the whole range.
const TILT_DEG_PER_PX := 0.2
const EASE_S := 0.25
const JUMP_M := 0.05
const JUMP_RAD := 0.5
const ELEVATION_JUMP_RAD := 0.15
const ABOVE_CUE_DEG := 12.0
## Watching a shot: how quickly the camera closes on its framing (about 0.6 s to settle the
## rise), the share of the half-view the balls may fill, the nearest it comes, and the pitches it
## tries, lowest first.
const WATCH_TAU_S := 0.2
const WATCH_FILL := 0.78
const WATCH_MIN_DISTANCE_M := 1.1
const WATCH_PITCHES_DEG: Array[float] = [48.0, 62.0, 74.0]
## The ease from the watching camera, or into and out of the overview, back to the aiming pose.
const RETURN_S := 0.6
## The phone's aspect, for a camera not in a viewport yet.
const DEFAULT_ASPECT := 1912.0 / 880.0
## Lowest camera height above the cloth over the playing area (a ball is 52.5 mm tall), and over
## or beyond the cushions and rails (rail tops are about 45 mm up).
const CLOTH_CLEARANCE_M := 0.06
const RAIL_CLEARANCE_M := 0.1
## Used when the extension gives no table (spec 6.1).
const FALLBACK_HALF_SIZE := Vector2(1.7845, 0.889)
const FALLBACK_BALL_RADIUS := 0.02625

var _pitch_deg := DEFAULT_PITCH_DEG
var _zoom := 1.0
var _held := false
var _has_pose := false
var _shown_cue := Vector2.ZERO
var _shown_aim := 0.0
var _shown_elevation := 0.0
var _target_cue := Vector2.ZERO
var _target_aim := 0.0
var _target_elevation := 0.0
var _from_cue := Vector2.ZERO
var _from_aim := 0.0
var _from_elevation := 0.0
## 0 to 1 through an ease; negative when not easing.
var _ease := -1.0
var _ease_s := EASE_S
## Easing from a pose the parameters don't describe (watching, the overview): the transform the
## ease blends from.
var _blend_from := Transform3D.IDENTITY
var _blending := false
var _watching := false
var _watch_aim := 0.0
var _watch := {}
var _watch_target := {}
var _overview := false
var _before_overview := Vector2(DEFAULT_PITCH_DEG, 1.0)
var _half_size := FALLBACK_HALF_SIZE
var _ball_radius := FALLBACK_BALL_RADIUS


func _init() -> void:
	var dims := OfsTable.dimensions()
	if dims.has("playing_length") and dims.has("playing_width") and dims.has("ball_radius"):
		_half_size = Vector2(float(dims["playing_length"]), float(dims["playing_width"])) / 2.0
		_ball_radius = float(dims["ball_radius"])
	else:
		Log.warn("app", "app.error", {"kind": "message",
				"message": "camera_rig: OfsTable.dimensions() gave no table; framing and clearances use the standard table"})


func _ready() -> void:
	var cam := camera()
	cam.transform = Transform3D.IDENTITY
	cam.fov = FOV_DEG
	cam.near = NEAR_M
	cam.far = FAR_M
	set_process(false)


func _exit_tree() -> void:
	if _ease >= 0.0 or _watching:
		Perf.set_active(&"camera", false)


## Puts the rig behind cue_xy (core metres) along aim_rad.
func follow(cue_xy: Vector2, aim_rad: float) -> void:
	_target_cue = cue_xy
	_target_aim = aim_rad
	if _held:
		return
	if not _has_pose:
		_has_pose = true
		_show(cue_xy, aim_rad, _target_elevation)
	elif _ease >= 0.0:
		pass
	elif cue_xy.distance_to(_shown_cue) > JUMP_M or absf(angle_difference(_shown_aim, aim_rad)) > JUMP_RAD:
		_start_ease()
	else:
		_show(cue_xy, aim_rad, _shown_elevation)


## The planning view's distance looking along aim_rad: FAR_DISTANCE_M along the table, further
## on a slant, where the table's footprint runs longer up the screen.
func far_distance(aim_rad: float) -> float:
	var along := _half_size.x * absf(cos(aim_rad)) + _half_size.y * absf(sin(aim_rad))
	return FAR_DISTANCE_M * maxf(along / _half_size.x, 1.0)


## The cue's elevation as drawn (ShotSetup.effective_elevation()), in radians: a raised cue lifts
## the camera above it.
func set_cue_elevation(elevation_rad: float) -> void:
	if not is_finite(elevation_rad):
		return
	_target_elevation = maxf(elevation_rad, 0.0)
	if _held or not _has_pose or _ease >= 0.0:
		if not _has_pose:
			_shown_elevation = _target_elevation
		return
	if absf(_target_elevation - _shown_elevation) > ELEVATION_JUMP_RAD:
		_start_ease()
	else:
		_show(_shown_cue, _shown_aim, _target_elevation)


func cue_elevation() -> float:
	return _target_elevation


func pinch(factor: float) -> void:
	if _held or not (factor > 0.0):
		return
	_leave_overview()
	_zoom = clampf(_zoom / factor, ZOOM_MIN, ZOOM_MAX)
	_apply()


## relative_px: the two-finger drag's vertical movement in canvas units, down positive.
func tilt(relative_px: float) -> void:
	if _held:
		return
	_leave_overview()
	set_pitch_deg(_pitch_deg + relative_px * TILT_DEG_PER_PX)


## The planning view (the highest tilt at the default zoom, the whole table) or back to the tilt
## and zoom before it, eased. Ignored while held.
func set_overview(on: bool) -> void:
	if on == _overview or _held:
		return
	_overview = on
	if on:
		_before_overview = Vector2(_pitch_deg, _zoom)
		_pitch_deg = PITCH_MAX_DEG
		_zoom = 1.0
	else:
		_pitch_deg = _before_overview.x
		_zoom = _before_overview.y
	if _has_pose:
		_start_ease(true)
	overview_changed.emit(on)


func is_overview() -> bool:
	return _overview


# A tilt or pinch takes the view as it is from there: no way back to the tilt before the overview.
func _leave_overview() -> void:
	if _overview:
		_overview = false
		overview_changed.emit(false)


## 1 at the default zoom, smaller when zoomed in; aim sensitivity is multiplied by it (spec 8.1).
func zoom_scale() -> float:
	return _zoom


func set_zoom_scale(scale: float) -> void:
	_zoom = clampf(scale, ZOOM_MIN, ZOOM_MAX)
	_apply()


func pitch_deg() -> float:
	return _pitch_deg


func set_pitch_deg(deg: float) -> void:
	_pitch_deg = clampf(deg, PITCH_MIN_DEG, PITCH_MAX_DEG)
	_apply()


## While held, follow() only remembers where to go and pinch and tilt are ignored; releasing eases
## to the last follow().
func hold(on: bool) -> void:
	if on == _held:
		return
	_held = on
	if on:
		_stop_ease()
	elif _watching:
		_watching = false
		_start_ease(true)
	elif _has_pose and (_target_cue != _shown_cue or _target_aim != _shown_aim
			or _target_elevation != _shown_elevation):
		_start_ease()


## While held, frames these points (core metres, on the cloth: the moving balls now and a little
## ahead) from above and behind, along the aim the shot was struck on, and glides there. The
## first call rises from where the camera is. Ignored when not held.
func watch(points: PackedVector2Array) -> void:
	if not _held or not _has_pose or points.is_empty():
		return
	if not _watching:
		_watching = true
		_watch_aim = _shown_aim
		_watch = _watch_from_transform()
		Perf.set_active(&"camera", true)
		set_process(true)
	_watch_target = watch_framing(points, _watch_aim)


func is_watching() -> bool:
	return _watching


## {focus, distance, pitch_deg}: from the lowest of WATCH_PITCHES_DEG not below the player's tilt,
## the nearest framing with every point in view looking along aim_rad; the planning view when none
## fits within FAR_DISTANCE_M.
func watch_framing(points: PackedVector2Array, aim_rad: float) -> Dictionary:
	var lo := Vector2(INF, INF)
	var hi := Vector2(-INF, -INF)
	for p in points:
		lo = lo.min(p)
		hi = hi.max(p)
	var focus := (lo + hi) / 2.0
	for pitch: float in WATCH_PITCHES_DEG:
		if pitch < minf(_pitch_deg, WATCH_PITCHES_DEG[-1]):
			continue
		var distance := _fit_distance(points, focus, pitch, aim_rad)
		if distance <= FAR_DISTANCE_M:
			return {"focus": focus, "distance": distance, "pitch_deg": pitch}
	return {"focus": Vector2.ZERO, "distance": FAR_DISTANCE_M, "pitch_deg": PITCH_MAX_DEG}


## The rig's transform looking at focus from `distance` away along aim_rad, pitched down pitch_deg.
func watch_pose(focus: Vector2, distance: float, pitch_deg: float, aim_rad: float) -> Transform3D:
	var along := Vector2(cos(aim_rad), sin(aim_rad))
	var pitch := deg_to_rad(pitch_deg)
	var eye_xy := focus - along * distance * cos(pitch)
	var height := maxf(_ball_radius + distance * sin(pitch), clearance_at(eye_xy))
	var eye := TableSpace.core_to_godot(eye_xy.x, eye_xy.y, height)
	var target := TableSpace.core_to_godot(focus.x, focus.y, _ball_radius)
	return Transform3D(Basis.looking_at(target - eye, Vector3.UP), eye)


func _fit_distance(points: PackedVector2Array, focus: Vector2, pitch_deg: float, aim_rad: float) -> float:
	var near := WATCH_MIN_DISTANCE_M
	if _fits(points, watch_pose(focus, near, pitch_deg, aim_rad)):
		return near
	var far := FAR_DISTANCE_M * 1.5
	if not _fits(points, watch_pose(focus, far, pitch_deg, aim_rad)):
		return INF
	for i in 20:
		var mid := (near + far) / 2.0
		if _fits(points, watch_pose(focus, mid, pitch_deg, aim_rad)):
			far = mid
		else:
			near = mid
	return far


func _fits(points: PackedVector2Array, pose: Transform3D) -> bool:
	var to_camera := pose.affine_inverse()
	var tan_v := tan(deg_to_rad(FOV_DEG) / 2.0) * WATCH_FILL
	var tan_h := tan(deg_to_rad(FOV_DEG) / 2.0) * _aspect() * WATCH_FILL
	for p in points:
		var local := to_camera * TableSpace.core_to_godot(p.x, p.y, _ball_radius)
		var depth := -local.z
		if depth <= NEAR_M or absf(local.x) > tan_h * depth or absf(local.y) > tan_v * depth:
			return false
	return true


func _aspect() -> float:
	if is_inside_tree():
		var view := get_viewport().get_visible_rect().size
		if view.y > 0.0:
			return view.x / view.y
	return DEFAULT_ASPECT


## The camera's present view as watch parameters: the point on the cloth it looks at.
func _watch_from_transform() -> Dictionary:
	var eye := transform.origin
	var forward := -transform.basis.z
	var down := clampf(-forward.y, 0.05, 1.0)
	var t := maxf(eye.y - _ball_radius, 0.0) / down
	var focus := TableSpace.godot_to_core(eye + forward * t)
	return {"focus": Vector2(focus.x, focus.y), "distance": t, "pitch_deg": rad_to_deg(asin(down))}


func is_held() -> bool:
	return _held


func is_easing() -> bool:
	return _ease >= 0.0


func camera() -> Camera3D:
	return $Camera3D


## The rig's transform for the cue ball at cue_xy aiming along aim_rad with the cue raised to
## elevation_rad, at the current pitch and zoom.
func pose_for(cue_xy: Vector2, aim_rad: float, elevation_rad: float = 0.0) -> Transform3D:
	var plan := smoothstep(PLAN_FROM_DEG, PITCH_MAX_DEG, _pitch_deg)
	var along := Vector2(cos(aim_rad), sin(aim_rad))
	var distance := lerpf(NEAR_DISTANCE_M, far_distance(aim_rad), plan) * _zoom
	var look_ahead := LOOK_AHEAD_M * _zoom
	var tilt := deg_to_rad(_pitch_deg)
	var pitch := deg_to_rad(pitch_for(elevation_rad))
	if pitch > tilt:
		# Lifted over the cue: look nearer the cue ball so it stays as far below the centre of view.
		var below := atan2(distance * sin(tilt), distance * cos(tilt) - look_ahead) - tilt
		look_ahead = distance * sin(below) / sin(pitch + below)
	var low_focus := cue_xy + along * look_ahead
	var plan_focus := cue_xy.lerp(Vector2.ZERO, minf(_zoom, 1.0))
	var focus := low_focus.lerp(plan_focus, plan)
	var eye_xy := focus - along * distance * cos(pitch)
	var height := maxf(_ball_radius + distance * sin(pitch), clearance_at(eye_xy))
	var eye := TableSpace.core_to_godot(eye_xy.x, eye_xy.y, height)
	var target := TableSpace.core_to_godot(focus.x, focus.y, _ball_radius)
	return Transform3D(Basis.looking_at(target - eye, Vector3.UP), eye)


## The camera's pitch in degrees with the cue raised to elevation_rad: the tilt, or higher over a
## raised cue.
func pitch_for(elevation_rad: float) -> float:
	if elevation_rad <= 0.0:
		return _pitch_deg
	return clampf(rad_to_deg(elevation_rad) + ABOVE_CUE_DEG, _pitch_deg, PITCH_MAX_DEG)


## The lowest the camera may be above the cloth at core xy.
func clearance_at(xy: Vector2) -> float:
	if absf(xy.x) < _half_size.x and absf(xy.y) < _half_size.y:
		return CLOTH_CLEARANCE_M
	return RAIL_CLEARANCE_M


func _process(delta: float) -> void:
	if _watching:
		_step_watch(delta)
		return
	if _ease < 0.0:
		set_process(false)
		return
	_ease = minf(_ease + delta / _ease_s, 1.0)
	var k := smoothstep(0.0, 1.0, _ease)
	var cue := _from_cue.lerp(_target_cue, k)
	var aim := lerp_angle(_from_aim, _target_aim, k)
	var elevation := lerpf(_from_elevation, _target_elevation, k)
	if _blending:
		_shown_cue = cue
		_shown_aim = aim
		_shown_elevation = elevation
		var pose := _blend_from.interpolate_with(pose_for(cue, aim, elevation), k)
		if not pose.is_equal_approx(transform):
			transform = pose
			moved.emit()
	else:
		_show(cue, aim, elevation)
	if _ease >= 1.0:
		_stop_ease()


# Each part closes on its target exponentially, so a new target every few frames never jerks.
func _step_watch(delta: float) -> void:
	if _watch_target.is_empty():
		return
	var k := 1.0 - exp(-delta / WATCH_TAU_S)
	_watch["focus"] = (_watch["focus"] as Vector2).lerp(_watch_target["focus"], k)
	_watch["distance"] = lerpf(_watch["distance"], _watch_target["distance"], k)
	_watch["pitch_deg"] = lerpf(_watch["pitch_deg"], _watch_target["pitch_deg"], k)
	var pose := watch_pose(_watch["focus"], _watch["distance"], _watch["pitch_deg"], _watch_aim)
	if not pose.is_equal_approx(transform):
		transform = pose
		moved.emit()


## blend: start from the transform as it is, not the shown parameters, and take RETURN_S.
func _start_ease(blend: bool = false) -> void:
	_from_cue = _shown_cue
	_from_aim = _shown_aim
	_from_elevation = _shown_elevation
	_blending = blend
	_blend_from = transform
	_ease_s = RETURN_S if blend else EASE_S
	if _ease < 0.0:
		Perf.set_active(&"camera", true)
	_ease = 0.0
	set_process(true)


func _stop_ease() -> void:
	if _ease < 0.0:
		return
	_ease = -1.0
	_blending = false
	set_process(false)
	Perf.set_active(&"camera", false)


func _show(cue_xy: Vector2, aim_rad: float, elevation_rad: float) -> void:
	_shown_cue = cue_xy
	_shown_aim = aim_rad
	_shown_elevation = elevation_rad
	_apply()


func _apply() -> void:
	if not _has_pose or _watching:
		return
	var pose := pose_for(_shown_cue, _shown_aim, _shown_elevation)
	if pose.is_equal_approx(transform):
		return
	transform = pose
	moved.emit()
