class_name CameraRig
extends Node3D
## The camera (spec 8.5): behind the cue ball while aiming, and the TV broadcast view of the whole
## table while a shot plays and in the overview. Child: Camera3D.
##
## Aiming, the camera looks along the aim. Pinch zooms; a two-finger vertical drag tilts it from cue
## level up to a nearly overhead planning view. Up to PLAN_FROM_DEG it looks at a point LOOK_AHEAD_M
## along the aim line, so the cue ball sits low in view with the line ahead above it. From there to
## PITCH_MAX_DEG the framing pulls out to FAR_DISTANCE_M and the point it looks at slides towards
## the table centre. Pinching scales the distance and the look-ahead together (a dolly towards the
## cue ball); in the planning view zooming in also slides the view back towards the cue ball.
##
## A raised cue lifts the camera to at least ABOVE_CUE_DEG above the cue's own angle, so the cue
## comes down to the ball from below the eye as a player sees it over the butt, and its butt passes
## under the camera instead of through the view. The lift changes the angle only, not the framing.
##
## Finger-driven changes (aim drags, pinch, tilt, the elevation control) apply at once. A follow()
## that moves the cue ball more than JUMP_M or the aim more than JUMP_RAD, or an elevation change of
## more than ELEVATION_JUMP_RAD (the cue forced over a ball or the rail), eases there over EASE_S.
##
## The broadcast view is the main TV camera of a match (KaiC's reference, a World Grand Prix frame):
## at one end, high and well back, BROADCAST_PITCH_DEG down, the whole table in frame clear of the
## controls (keep_clear), the far end narrower. While a shot plays (held) it stands at the end
## behind the shot, so the picture keeps the player's left and right, and stays there like the TV
## camera rather than chasing the balls. set_overview() shows it too. In both, orbit() walks round
## the table at that angle, refitting the distance to keep the whole table in view; in the overview
## pinch zooms it. Moving between the aiming camera and the broadcast view blends the whole pose over
## BLEND_S (see _step_blend).

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
const BROADCAST_PITCH_DEG := 55.0
## The share of the half-view the table's outline may fill.
const BROADCAST_FILL := 0.92
## A drag across the whole screen walks about half way round.
const ORBIT_RAD_PER_PX := 0.0016
const OVERVIEW_ZOOM_MIN := 0.5
const OVERVIEW_ZOOM_MAX := 1.5
## Between the aiming camera and the broadcast view.
const BLEND_S := 1.0
## How far ahead of a straight line the height runs while blending: up first when rising, down last
## when descending, so a camera low behind the cue ball clears the rails as it pulls back. Above
## 0.1875 the height would overshoot its end.
const BLEND_ARC := 0.18
## Canvas units kept between the table's outline and a control.
const CLEAR_MARGIN := 12.0
## The furthest the broadcast view backs off to clear the controls before giving up.
const MAX_PULL_BACK_M := 20.0
## The phone's canvas, for a camera not in a viewport yet.
const DEFAULT_CANVAS := Vector2(1912.0, 880.0)
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
var _overview := false
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
## 0 to 1 through an ease or a blend; negative when neither runs.
var _ease := -1.0
var _ease_s := EASE_S
## A blend between views: where it started (the eye, and the view's direction as (yaw, pitch, 0)
## in radians, see _turn), how fast each was changing, and the goal at the last step.
var _blending := false
var _from_eye := Vector3.ZERO
var _from_turn := Vector3.ZERO
var _from_eye_v := Vector3.ZERO
var _from_turn_v := Vector3.ZERO
var _goal_eye := Vector3.ZERO
var _goal_turn := Vector3.ZERO
## The broadcast view: the core direction it looks along, and the overview's zoom.
var _yaw := 0.0
var _view_zoom := 1.0
var _outline := Stage.DEFAULT_BOUNDS
var _keep_clear: Array[Rect2] = []
var _clear_boxes: Array[PackedVector2Array] = []
var _pull_back_warned := false
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
	if _ease >= 0.0:
		Perf.set_active(&"camera", false)


## Puts the rig behind cue_xy (core metres) along aim_rad. While the broadcast view shows, it only
## remembers where to come back to.
func follow(cue_xy: Vector2, aim_rad: float) -> void:
	_target_cue = cue_xy
	_target_aim = aim_rad
	if not _has_pose:
		_has_pose = true
		_show(cue_xy, aim_rad, _target_elevation)
	elif _held or _overview or _ease >= 0.0:
		pass
	elif cue_xy.distance_to(_shown_cue) > JUMP_M or absf(angle_difference(_shown_aim, aim_rad)) > JUMP_RAD:
		_start_ease()
	else:
		_show(cue_xy, aim_rad, _shown_elevation)


## The table's outline (TableView.bounds(), Godot metres) the broadcast view fits.
func set_table_bounds(bounds: AABB) -> void:
	if bounds.has_volume():
		_outline = bounds
		_apply()


## Canvas rectangles (the controls) the broadcast view keeps the table out from under.
func keep_clear(rects: Array[Rect2]) -> void:
	if rects == _keep_clear:
		return
	_keep_clear = rects.duplicate()
	_clear_boxes.clear()
	for rect in rects:
		var grown := rect.grow(CLEAR_MARGIN)
		_clear_boxes.append(PackedVector2Array([grown.position, Vector2(grown.end.x, grown.position.y), grown.end,
				Vector2(grown.position.x, grown.end.y)]))
	_apply()


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
	if _held or _overview or not _has_pose or _ease >= 0.0:
		if not _has_pose:
			_shown_elevation = _target_elevation
		return
	if absf(_target_elevation - _shown_elevation) > ELEVATION_JUMP_RAD:
		_start_ease()
	else:
		_show(_shown_cue, _shown_aim, _target_elevation)


func cue_elevation() -> float:
	return _target_elevation


## Zooms the aiming camera, or the overview. Ignored while a shot plays.
func pinch(factor: float) -> void:
	if _held or not (factor > 0.0):
		return
	if _overview:
		_view_zoom = clampf(_view_zoom / factor, OVERVIEW_ZOOM_MIN, OVERVIEW_ZOOM_MAX)
	else:
		_zoom = clampf(_zoom / factor, ZOOM_MIN, ZOOM_MAX)
	_apply()


## relative_px: the two-finger drag's vertical movement in canvas units, down positive. In the
## overview it goes back behind the cue ball, tilted.
func tilt(relative_px: float) -> void:
	if _held:
		return
	_leave_overview()
	set_pitch_deg(_pitch_deg + relative_px * TILT_DEG_PER_PX)


## relative_px: a one-finger drag's horizontal movement in canvas units. Walks the broadcast view
## round the table, the same way an aim drag swings the cue; nothing while aiming.
func orbit(relative_px: float) -> void:
	if not is_broadcast():
		return
	_yaw = wrapf(_yaw - relative_px * ORBIT_RAD_PER_PX, -PI, PI)
	_apply()


## The broadcast view from the end behind the aim, or back behind the cue ball, blended. Ignored
## while a shot plays.
func set_overview(on: bool) -> void:
	if on == _overview or _held:
		return
	_overview = on
	if on:
		_yaw = end_behind(_target_aim)
		_view_zoom = 1.0
	_start_blend()
	overview_changed.emit(on)


func is_overview() -> bool:
	return _overview


## True while the broadcast view is the camera's goal: a shot playing or the overview.
func is_broadcast() -> bool:
	return _held or _overview


## "aim", "shot" or "overview".
func view_name() -> String:
	if _held:
		return "shot"
	return "overview" if _overview else "aim"


## The broadcast view's direction (core angle) for a shot along aim_rad: from baulk towards the
## black when the aim heads that way, else from the black end towards baulk.
static func end_behind(aim_rad: float) -> float:
	return 0.0 if cos(aim_rad) > 0.0 else PI


## The default tilt and zoom, out of the overview, applied by the next follow() or release. Every
## shot starts from behind the cue ball, so a tilt left high can't strand the player overhead.
func reset_view() -> void:
	var was_overview := _overview
	if _overview:
		_overview = false
		overview_changed.emit(false)
	if _pitch_deg == DEFAULT_PITCH_DEG and _zoom == 1.0 and not was_overview:
		return
	_pitch_deg = DEFAULT_PITCH_DEG
	_zoom = 1.0
	if not _held:
		_start_blend()


func _leave_overview() -> void:
	if _overview:
		_overview = false
		_start_blend()
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


## A shot playing: blends to the broadcast view from the end behind the shot and holds it; follow()
## only remembers where to go and pinch and tilt are ignored. Releasing blends back behind the cue
## ball at the last follow().
func hold(on: bool) -> void:
	if on == _held:
		return
	_held = on
	if on:
		if _overview:
			_overview = false
			overview_changed.emit(false)
		_yaw = end_behind(_target_aim)
		_view_zoom = 1.0
	_start_blend()


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


## The rig's transform for the broadcast view looking along yaw_rad (a core angle: 0 stands at
## baulk looking towards the black), BROADCAST_PITCH_DEG down, at the distance that fits the
## table's outline into BROADCAST_FILL of the view clear of the controls, times zoom.
func broadcast_pose(yaw_rad: float, zoom: float = 1.0) -> Transform3D:
	var pitch := deg_to_rad(BROADCAST_PITCH_DEG)
	var level := TableSpace.core_to_godot(cos(yaw_rad), sin(yaw_rad))
	var basis := Basis.looking_at(level * cos(pitch) + Vector3.DOWN * sin(pitch), Vector3.UP)
	var forward := -basis.z
	var tan_v := tan(deg_to_rad(FOV_DEG) / 2.0) * BROADCAST_FILL
	var tan_h := tan_v * _aspect()
	# For a fixed orientation each edge of the view is a plane through the eye, and a point is inside
	# it when the eye is far enough along that plane's normal: a linear bound per point and edge.
	# The eye that meets the top and bottom bounds (or the sides, whichever is further back)
	# exactly, centred between the other pair, is the closest fit.
	var top := -INF
	var bottom := -INF
	var right := -INF
	var left := -INF
	for p in _outline_corners():
		top = maxf(top, p.dot(basis.y - forward * tan_v))
		bottom = maxf(bottom, p.dot(-basis.y - forward * tan_v))
		right = maxf(right, p.dot(basis.x - forward * tan_h))
		left = maxf(left, p.dot(-basis.x - forward * tan_h))
	var along := minf(-(top + bottom) / (2.0 * tan_v), -(right + left) / (2.0 * tan_h))
	var eye := basis.x * (right - left) / 2.0 + basis.y * (top - bottom) / 2.0 + forward * along
	eye -= forward * _pull_back_for_controls(eye, basis)
	var to_cloth := (eye.y - _ball_radius) / -forward.y
	return Transform3D(basis, eye + forward * to_cloth * (1.0 - zoom))


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


## Where the camera at `pose` draws a Godot point on the canvas, for views not shown yet.
func project(pose: Transform3D, point: Vector3) -> Vector2:
	var canvas := _canvas()
	var local := pose.affine_inverse() * point
	var scale := canvas.y / 2.0 / tan(deg_to_rad(FOV_DEG) / 2.0) / maxf(-local.z, 1e-6)
	return canvas / 2.0 + Vector2(local.x, -local.y) * scale


## The table outline's top corners in order round it.
func _outline_corners() -> PackedVector3Array:
	var lo := _outline.position
	var hi := _outline.end
	return PackedVector3Array([Vector3(lo.x, hi.y, lo.z), Vector3(hi.x, hi.y, lo.z), Vector3(hi.x, hi.y, hi.z),
			Vector3(lo.x, hi.y, hi.z)])


## Metres to back the eye off along the view so the outline is clear of every keep_clear rectangle.
## Backing off shrinks the table towards the middle of the view, where no control is.
func _pull_back_for_controls(eye: Vector3, basis: Basis) -> float:
	if not _covers_controls(Transform3D(basis, eye)):
		return 0.0
	var near := 0.0
	var far := 0.25
	while _covers_controls(Transform3D(basis, eye + basis.z * far)):
		near = far
		far *= 2.0
		if far > MAX_PULL_BACK_M:
			if not _pull_back_warned:
				_pull_back_warned = true
				Log.warn("app", "app.error", {"kind": "message", "message": "camera_rig: the broadcast view can't clear the controls %s within %.0f m (outline %s, canvas %s)"
						% [_keep_clear, MAX_PULL_BACK_M, _outline, _canvas()]})
			return MAX_PULL_BACK_M
	for i in 14:
		var mid := (near + far) / 2.0
		if _covers_controls(Transform3D(basis, eye + basis.z * mid)):
			near = mid
		else:
			far = mid
	return far


func _covers_controls(pose: Transform3D) -> bool:
	if _clear_boxes.is_empty():
		return false
	var outline := PackedVector2Array()
	for p in _outline_corners():
		outline.append(project(pose, p))
	for box in _clear_boxes:
		if not Geometry2D.intersect_polygons(outline, box).is_empty():
			return true
	return false


func _canvas() -> Vector2:
	if is_inside_tree():
		var view := get_viewport().get_visible_rect().size
		if view.y > 0.0:
			return view
	return DEFAULT_CANVAS


func _aspect() -> float:
	var canvas := _canvas()
	return canvas.x / canvas.y


## A view's direction as (yaw, pitch, 0): the core angle it looks along and how far down, in
## radians. Every pose here is level, so the two describe it whole.
static func _turn(pose: Transform3D) -> Vector3:
	var forward := -pose.basis.z
	var level := TableSpace.godot_to_core(forward)
	return Vector3(atan2(level.y, level.x), asin(clampf(-forward.y, -1.0, 1.0)), 0.0)


static func _basis_for(turn: Vector3) -> Basis:
	var pitch := clampf(turn.y, deg_to_rad(-80.0), deg_to_rad(85.0))
	var level := TableSpace.core_to_godot(cos(turn.x), sin(turn.x))
	return Basis.looking_at(level * cos(pitch) + Vector3.DOWN * sin(pitch), Vector3.UP)


func _goal() -> Transform3D:
	if is_broadcast():
		return broadcast_pose(_yaw, _view_zoom)
	return pose_for(_target_cue, _target_aim, _target_elevation)


func _process(delta: float) -> void:
	if _ease < 0.0:
		set_process(false)
		return
	_ease = minf(_ease + delta / _ease_s, 1.0)
	if _blending:
		_step_blend()
	else:
		var k := smoothstep(0.0, 1.0, _ease)
		_show(_from_cue.lerp(_target_cue, k), lerp_angle(_from_aim, _target_aim, k),
				lerpf(_from_elevation, _target_elevation, k))
	if _ease >= 1.0:
		_stop_ease()


# The eye and the view's yaw and pitch each run a cubic from where they were, at the speed they were
# changing, to the goal at rest (with no starting speed, smoothstep), so a blend that replaces
# another halfway keeps its speed. The goal is fetched every step, so aiming or walking round during
# a blend carries into it. The height adds a bump that is zero, with zero slope, at both ends
# (BLEND_ARC). Turning by angle rather than towards a point keeps the turn spread over the blend:
# a low camera looking at a point just ahead would swing nearly all its pitch in the first rise.
func _step_blend() -> void:
	var s := _ease
	var goal := _goal()
	_goal_eye = goal.origin
	_goal_turn = _turn(goal)
	_goal_turn.x = _from_turn.x + angle_difference(_from_turn.x, _goal_turn.x)
	if s >= 1.0:
		if not is_broadcast():
			_shown_cue = _target_cue
			_shown_aim = _target_aim
			_shown_elevation = _target_elevation
		_place(goal)
		return
	var k := smoothstep(0.0, 1.0, s)
	var carry := s * (1.0 - s) * (1.0 - s) * _ease_s
	var eye := _from_eye.lerp(_goal_eye, k) + _from_eye_v * carry
	eye.y += absf(_goal_eye.y - _from_eye.y) * BLEND_ARC * 16.0 * s * s * (1.0 - s) * (1.0 - s)
	eye.y = maxf(eye.y, clearance_at(TableSpace.godot_to_core(eye)))
	var turn := _from_turn.lerp(_goal_turn, k) + _from_turn_v * carry
	_place(Transform3D(_basis_for(turn), eye))


## The blend's eye and turn velocities now, per second; zero when no blend runs.
func _blend_velocity() -> Array[Vector3]:
	if not _blending or _ease < 0.0:
		return [Vector3.ZERO, Vector3.ZERO]
	var s := _ease
	var dk := 6.0 * s * (1.0 - s) / _ease_s
	var dcarry := (1.0 - s) * (1.0 - 3.0 * s)
	var darc := absf(_goal_eye.y - _from_eye.y) * BLEND_ARC * 32.0 * s * (1.0 - s) * (1.0 - 2.0 * s) / _ease_s
	return [(_goal_eye - _from_eye) * dk + _from_eye_v * dcarry + Vector3.UP * darc,
			(_goal_turn - _from_turn) * dk + _from_turn_v * dcarry]


func _start_blend() -> void:
	if not _has_pose:
		return
	var velocity := _blend_velocity()
	_from_eye = transform.origin
	_from_turn = _turn(transform)
	_from_eye_v = velocity[0]
	_from_turn_v = velocity[1]
	_goal_eye = _from_eye
	_goal_turn = _from_turn
	_blending = true
	_ease_s = BLEND_S
	if _ease < 0.0:
		Perf.set_active(&"camera", true)
	_ease = 0.0
	set_process(true)


func _start_ease() -> void:
	_from_cue = _shown_cue
	_from_aim = _shown_aim
	_from_elevation = _shown_elevation
	_blending = false
	_ease_s = EASE_S
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


# A blend fetches its goal itself each step.
func _apply() -> void:
	if not _has_pose or _blending:
		return
	_place(broadcast_pose(_yaw, _view_zoom) if is_broadcast() else pose_for(_shown_cue, _shown_aim, _shown_elevation))


func _place(pose: Transform3D) -> void:
	if pose.is_equal_approx(transform):
		return
	transform = pose
	moved.emit()
