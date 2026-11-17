class_name CameraRig
extends Node3D
## The camera (spec 8.5): behind the cue ball while aiming, and the full-table (broadcast) view while
## a shot plays and in the overview. Child: Camera3D.
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
## A tilt while it is lifted takes the camera off the cue: it moves from where it is, down to
## PITCH_MIN_DEG under the cue if the fingers ask (the cue fades out of the way, CueView.clear_of),
## and no longer follows the cue's angle until the view is reset for the next shot.
##
## Finger-driven changes (aim drags, pinch, tilt, the elevation control) apply at once. A follow()
## that moves the cue ball more than JUMP_M or the aim more than JUMP_RAD, or an elevation change of
## more than ELEVATION_JUMP_RAD (the cue forced over a ball or the rail), eases there over EASE_S.
##
## The broadcast view is the full-table view of KaiC's reference game, measured from his recording:
## well back at one end, BROADCAST_PITCH_DEG down on a long lens, the table upright on the screen,
## its outline FULL_TABLE_HEIGHT of the height and centred, every pocket clear of the controls
## (keep_clear), the far end narrower. While a shot plays it stands at the black end (FULL_TABLE_YAW) and
## stays there rather than chasing the balls. set_overview() shows it too, from the end behind the
## aim. In both, orbit() walks round the table at that angle, refitting the distance to keep the
## whole table in view; in the overview pinch zooms it. Moving between the aiming camera and the
## overview blends the whole pose over BLEND_S (see _step_blend).
##
## A shot (spec 8.5, ShotCycle) holds the camera and only ever cuts it: hold(true) keeps it where it
## is for the stroke, show_pocket cuts to the pocket camera and show_broadcast to the full-table
## view. Releasing is the one move: a swoop back behind the cue ball over RETURN_S, closing in on
## the cue ball as it swings round it rather than cutting across the table.
##
## While the opponent is at the table the person may watch (spec 10.5, set_watch): "above" is the
## whole table from straight overhead, its length across the screen and the black on the right,
## which a pinch zooms and a drag then pans (pan); "round" is the overview's walk round the table
## with its own direction and zoom. Watching, everything else asked of the rig (follow, hold, the
## cuts, the swoop, reset_view, the overview) is taken in as usual but moves nothing; "cue" goes
## back to wherever those calls have left the rig's own view, cutting to it while a shot holds the
## camera and blending otherwise.

signal moved
signal overview_changed(on: bool)
signal watch_changed(view: String)

const PITCH_MIN_DEG := 4.0
const PITCH_MAX_DEG := 82.0
const DEFAULT_PITCH_DEG := 16.0
## Vertical. The phone's 2.17:1 screen makes the horizontal view about twice as wide, and balls at
## its edges stretch with the angle off the centre; 40 degrees stretched them about 13 %.
const FOV_DEG := 34.0
## The broadcast view's long lens: from further back the far end shrinks less. The reference game's
## measures about 17 degrees, with the far cushion two thirds as wide as the near one.
const BROADCAST_FOV_DEG := 17.5
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
const BROADCAST_PITCH_DEG := 30.0
## The share of the view's height the table's outline fills, centred, as the reference game's does;
## that also keeps it below main.gd's notice line (16 canvas units down, about 60 deep).
const FULL_TABLE_HEIGHT := 0.82
## The most of the view's width the outline may fill seen from an end and from a side, where the
## long table runs across the wide screen and the width, not the height, sets the size; between, it
## follows the yaw.
const END_ON_WIDTH := 0.68
const SIDE_ON_WIDTH := 0.9
## The full-table view of a shot stands at the black end looking towards baulk, the black at the
## bottom of the screen, as in KaiC's recording.
const FULL_TABLE_YAW := PI
## How far round from an end the walk has gone before the controls press on the fit in full (see
## broadcast_pose).
const WALK_FIT_DEG := 20.0
## A drag across the whole screen walks about half way round.
const ORBIT_RAD_PER_PX := 0.0016
const OVERVIEW_ZOOM_MIN := 0.5
const OVERVIEW_ZOOM_MAX := 1.5
## Between the aiming camera and the broadcast view.
const BLEND_S := 1.5
## The swoop back behind the cue ball after a shot: KaiC's reference game takes about a second.
const RETURN_S := 1.1
## The pocket camera, as measured from KaiC's recording of the reference game: on a wide lens, low
## and close outside the table, POCKET_BACK_M beyond the pocket's drop centre and POCKET_HEIGHT_M
## above the bed, pitched to put the pocket POCKET_FRAME_Y down the frame, so the ball rolls down
## the screen into it. A corner's stands on the ball's way in, at most POCKET_OFF_AXIS_DEG off the
## pocket's diagonal (further round it would look along a cushion, half the picture the floor);
## a middle's stands square behind its pocket, looking straight across the table.
const POCKET_FOV_DEG := 50.0
const POCKET_HEIGHT_M := 0.26
const POCKET_BACK_M := 0.34
const POCKET_FRAME_Y := 0.87
const POCKET_OFF_AXIS_DEG := 30.0
## The pocket camera turns, as little as it must, to keep the ball's way in within this share of
## the view's half width, while the pocket stays within POCKET_KEEP_CENTRE of it.
const POCKET_TRACK_SHARE := 0.85
const POCKET_KEEP_CENTRE := 0.5
## A way in shorter than this (a ball nudged in from the jaws) gives no direction; the pocket's own
## line out of the table stands in.
const POCKET_MIN_RUN_M := 0.05
## How far beyond the table's outline (TableView.bounds()) and how high a shot camera may stand: the
## arena round the table (Stage).
const ROOM_MARGIN_M := 3.0
const ROOM_HEIGHT_M := 4.0
## How far ahead of a straight line the height runs while blending: up first when rising, down last
## when descending, so a camera low behind the cue ball clears the rails as it pulls back. Above
## 0.1875 the height would overshoot its end.
const BLEND_ARC := 0.18
## Canvas units kept between a pocket and a control.
const CLEAR_MARGIN := 12.0
## The smallest share of its fit the broadcast view shrinks to, clearing the controls, before
## giving up.
const MIN_FIT_SCALE := 0.3
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
## A corner and a middle pocket's drop centre and radius (x, y, radius), mirrored to the others.
const FALLBACK_POCKETS: Array[Vector3] = [Vector3(1.8157, 0.9202, 0.042), Vector3(0.0, 0.9985, 0.044)]
const WATCH_VIEWS: Array[String] = ["cue", "above", "round"]
## The view from above: a longish lens, so the rails' sides barely show, with ABOVE_MARGIN canvas
## units round the table inside what the controls leave (keep_clear).
const ABOVE_FOV_DEG := 30.0
const ABOVE_MARGIN := 24.0
const ABOVE_ZOOM_MAX := 2.5

enum ShotView { BROADCAST, STILL, POCKET }

var _pitch_deg := DEFAULT_PITCH_DEG
## A tilt took the camera off a raised cue: the lift is no longer the cue's but what the fingers
## left of it, in degrees above _pitch_deg.
var _off_cue := false
var _own_lift_deg := 0.0
var _zoom := 1.0
var _held := false
## What the camera shows while held.
var _shot_view := ShotView.BROADCAST
var _pocket_pose := Transform3D.IDENTITY
## Where a shot found the camera (or where the blend it found running was going), and the lens,
## kept until its first cut.
var _still_pose := Transform3D.IDENTITY
var _still_fov := FOV_DEG
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
var _from_fov := FOV_DEG
var _from_eye_v := Vector3.ZERO
var _from_turn_v := Vector3.ZERO
var _goal_eye := Vector3.ZERO
var _goal_turn := Vector3.ZERO
## The core point a swoop swings the eye round, or NAN when the eye runs straight; where the view
## looked at the start and where the goal looks (_focus_of).
var _swing_about := Vector2(NAN, NAN)
var _from_focus := Vector3.ZERO
var _goal_focus := Vector3.ZERO
## The broadcast view: the core direction it looks along, and the overview's zoom.
var _yaw := 0.0
var _view_zoom := 1.0
var _outline := Stage.DEFAULT_BOUNDS
var _keep_clear: Array[Rect2] = []
var _clear_boxes: Array[PackedVector2Array] = []
var _control_boxes: Array[PackedVector2Array] = []
var _clear_warned := false
## The pockets' drop circles' hull (core metres), and as Godot points at the cloth and the rail top.
var _pocket_hull := PackedVector2Array()
var _pocket_points := PackedVector3Array()
var _half_size := FALLBACK_HALF_SIZE
var _ball_radius := FALLBACK_BALL_RADIUS
## Canvas units at the top the aiming view centres below (set_aim_top).
var _aim_top := 0.0
var _watch := "cue"
## What the controls leave of the canvas (keep_clear), for the view from above.
var _clear_rect := Rect2()
var _above_warned := false
var _above_zoom := 1.0
## Core metres the view from above has slid off the table's centre.
var _above_pan := Vector2.ZERO
var _round_yaw := 0.0
var _round_zoom := 1.0


func _init() -> void:
	var dims := OfsTable.dimensions()
	if dims.has("playing_length") and dims.has("playing_width") and dims.has("ball_radius"):
		_half_size = Vector2(float(dims["playing_length"]), float(dims["playing_width"])) / 2.0
		_ball_radius = float(dims["ball_radius"])
	else:
		Log.warn("app", "app.error", {"kind": "message",
				"message": "camera_rig: OfsTable.dimensions() gave no table; framing and clearances use the standard table"})
	var pockets: Array = Physics.table_shape().get("pockets", [])
	_pocket_hull = _hull_of(pockets)
	if _pocket_hull.size() < 3:
		Log.warn("app", "app.error", {"kind": "message", "message": "camera_rig: the table shape gave no pockets (%d: %s); the broadcast view keeps the standard table's clear"
				% [pockets.size(), pockets]})
		var standard: Array = []
		for p in FALLBACK_POCKETS:
			for corner: Vector2 in [Vector2(1, 1), Vector2(1, -1), Vector2(-1, 1), Vector2(-1, -1)]:
				standard.append({"drop_centre": [p.x * corner.x, p.y * corner.y], "drop_radius": p.z})
		_pocket_hull = _hull_of(standard)
	_place_pockets()


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
	elif _held or _overview or _ease >= 0.0 or _watch != "cue":
		pass
	elif cue_xy.distance_to(_shown_cue) > JUMP_M or absf(angle_difference(_shown_aim, aim_rad)) > JUMP_RAD:
		_start_ease()
	else:
		_show(cue_xy, aim_rad, _shown_elevation)


## The table's outline (TableView.bounds(), Godot metres) the broadcast view fits.
func set_table_bounds(bounds: AABB) -> void:
	if bounds.has_volume():
		_outline = bounds
		_place_pockets()
		_apply()


## Canvas rectangles (the controls) the broadcast view keeps the pockets out from under.
func keep_clear(rects: Array[Rect2]) -> void:
	if rects == _keep_clear:
		return
	_keep_clear = rects.duplicate()
	_clear_boxes.clear()
	_control_boxes.clear()
	var canvas := _canvas()
	var beyond := Rect2(-canvas, canvas * 3.0)
	var clear_lo := Vector2.ZERO
	var clear_hi := canvas
	# Each control keeps the pockets out of its column of the screen, from its inner edge out, or out
	# of the band from the top down to its bottom when that gives up less of the screen. That leaves
	# them one clear rectangle: against the controls' own shapes a pocket slipping round a corner,
	# from beside a control to above it, springs the fit in or out as the view walks round. No band
	# runs along the bottom, where the near rail sits.
	for rect in rects:
		var grown := rect.grow(CLEAR_MARGIN)
		_control_boxes.append(_corners(grown))
		var column := Rect2(beyond.position, Vector2(grown.end.x - beyond.position.x, beyond.size.y))
		var column_cut := grown.end.x / canvas.x
		if grown.get_center().x > canvas.x / 2.0:
			column = Rect2(Vector2(grown.position.x, beyond.position.y), Vector2(beyond.end.x - grown.position.x, beyond.size.y))
			column_cut = 1.0 - grown.position.x / canvas.x
		var band := Rect2(beyond.position, Vector2(beyond.size.x, grown.end.y - beyond.position.y))
		var banded := grown.end.y / canvas.y < column_cut
		_clear_boxes.append(_corners(band if banded else column))
		if banded:
			clear_lo.y = maxf(clear_lo.y, grown.end.y)
		elif grown.get_center().x > canvas.x / 2.0:
			clear_hi.x = minf(clear_hi.x, grown.position.x)
		else:
			clear_lo.x = maxf(clear_lo.x, grown.end.x)
	_clear_rect = Rect2(clear_lo, clear_hi - clear_lo)
	_apply()


static func _corners(rect: Rect2) -> PackedVector2Array:
	return PackedVector2Array([rect.position, Vector2(rect.end.x, rect.position.y), rect.end, Vector2(rect.position.x, rect.end.y)])


## The aiming view centres on the canvas below `units` (canvas units from the top) rather than on
## the whole canvas: it looks up by the angle half of them take, so what the view frames sits
## below whatever covers them. 0 frames the whole canvas.
func set_aim_top(units: float) -> void:
	var top := maxf(units, 0.0)
	if is_equal_approx(top, _aim_top):
		return
	_aim_top = top
	_apply()


## The planning view's distance looking along aim_rad: FAR_DISTANCE_M along the table, further
## on a slant, where the table's footprint runs longer up the screen.
func far_distance(aim_rad: float) -> float:
	var along := _half_size.x * absf(cos(aim_rad)) + _half_size.y * absf(sin(aim_rad))
	return FAR_DISTANCE_M * maxf(along / _half_size.x, 1.0)


## The cue's elevation as drawn (ShotSetup.effective_elevation()), in radians: a raised cue lifts
## the camera above it, until a tilt takes it off the cue.
func set_cue_elevation(elevation_rad: float) -> void:
	if not is_finite(elevation_rad):
		return
	_target_elevation = maxf(elevation_rad, 0.0)
	if _held or _overview or not _has_pose or _ease >= 0.0 or _watch != "cue":
		if not _has_pose:
			_shown_elevation = _target_elevation
		return
	if absf(_target_elevation - _shown_elevation) > ELEVATION_JUMP_RAD:
		_start_ease()
	else:
		_show(_shown_cue, _shown_aim, _target_elevation)


func cue_elevation() -> float:
	return _target_elevation


## Zooms the aiming camera, the overview or the watching view. Ignored while a shot plays, unless
## watching.
func pinch(factor: float) -> void:
	if not (factor > 0.0):
		return
	if _watch == "above":
		var zoom := clampf(_above_zoom * factor, 1.0, ABOVE_ZOOM_MAX)
		if zoom == _above_zoom:
			return
		_above_zoom = zoom
		_above_pan = _above_pan.clamp(-_above_pan_limit(zoom), _above_pan_limit(zoom))
		_apply()
		return
	if _watch == "round":
		_round_zoom = clampf(_round_zoom / factor, OVERVIEW_ZOOM_MIN, OVERVIEW_ZOOM_MAX)
		_apply()
		return
	if _held:
		return
	if _overview:
		_view_zoom = clampf(_view_zoom / factor, OVERVIEW_ZOOM_MIN, OVERVIEW_ZOOM_MAX)
	else:
		_zoom = clampf(_zoom / factor, ZOOM_MIN, ZOOM_MAX)
	_apply()


## relative_px: the two-finger drag's vertical movement in canvas units, down positive. In the
## overview it goes back behind the cue ball, tilted. Nothing while watching.
func tilt(relative_px: float) -> void:
	if _held or _watch != "cue":
		return
	_leave_overview()
	var change := relative_px * TILT_DEG_PER_PX
	if not _off_cue:
		var lift := pitch_for(_target_elevation) - _pitch_deg
		if lift > 0.0:
			_off_cue = true
			_own_lift_deg = lift
	if _off_cue and change < 0.0:
		# Down through what is left of the lift first, so the framing stays the tilt's own.
		var from_lift := minf(-change, _own_lift_deg)
		_own_lift_deg -= from_lift
		change += from_lift
	set_pitch_deg(_pitch_deg + change)


## relative_px: a one-finger drag's horizontal movement in canvas units. Walks the broadcast view
## round the table, the same way an aim drag swings the cue; nothing while aiming or watching from
## above.
func orbit(relative_px: float) -> void:
	if _watch == "round":
		_round_yaw = wrapf(_round_yaw - relative_px * ORBIT_RAD_PER_PX, -PI, PI)
		_apply()
		return
	if not is_broadcast() or _watch == "above":
		return
	_yaw = wrapf(_yaw - relative_px * ORBIT_RAD_PER_PX, -PI, PI)
	_apply()


## relative: a one-finger drag in canvas units. From above, zoomed in, it slides the table under
## the finger, no further than brings an edge of the table to the edge of the view; otherwise it
## is orbit's.
func pan(relative: Vector2) -> void:
	if _watch != "above":
		orbit(relative.x)
		return
	var limit := _above_pan_limit(_above_zoom)
	# The screen's right is core +x and its top core +y.
	var slid := (_above_pan + Vector2(-relative.x, relative.y) * _above_metres_per_unit(_above_area(), _above_zoom)).clamp(-limit, limit)
	if slid == _above_pan:
		return
	_above_pan = slid
	_apply()


## The view of the opponent's turn: "cue" (the rig's own views), "above" (the whole table from
## overhead) or "round" (the overview's walk round it). An unknown view is ignored.
func set_watch(view: String) -> void:
	if not WATCH_VIEWS.has(view):
		push_warning("camera_rig: no watching view \"%s\" (the views are %s); staying with \"%s\"" % [view, WATCH_VIEWS, _watch])
		return
	if view == _watch:
		return
	var was := _watch
	_watch = view
	match view:
		"above":
			_above_zoom = 1.0
			_above_pan = Vector2.ZERO
			_begin_blend(EASE_S)
		"round":
			# From above the walk starts side-on, the table turned the same way on the screen.
			_round_yaw = PI / 2.0 if was == "above" else end_behind(_target_aim)
			_round_zoom = 1.0
			_begin_blend()
		_:
			if _held:
				# A shot only ever cuts.
				_stop_ease()
				_apply()
			else:
				_begin_blend()
	watch_changed.emit(view)


func watch() -> String:
	return _watch


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
	return (_held and _shot_view == ShotView.BROADCAST) or _overview


## "aim", "shot", "pocket" or "overview".
func view_name() -> String:
	if _held:
		return "pocket" if _shot_view == ShotView.POCKET else "shot"
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
	var was_off_cue := _off_cue
	_off_cue = false
	_own_lift_deg = 0.0
	if _pitch_deg == DEFAULT_PITCH_DEG and _zoom == 1.0 and not was_overview and not was_off_cue:
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


## A shot playing: the camera stays where it is until show_pocket or show_broadcast cuts it;
## follow() only remembers where to go and pinch and tilt are ignored. A blend under way (the last
## shot's swoop, when the next is struck before it lands) runs on to where it was going rather than
## stopping in mid air. Releasing swoops back behind the cue ball at the last follow() over
## RETURN_S, the only move a shot makes.
func hold(on: bool) -> void:
	if on == _held:
		return
	if not on:
		_held = false
		_start_blend(RETURN_S, _target_cue)
		return
	# Watching, the camera is elsewhere: the rig's own view is where it would have been.
	var elsewhere := _blending or _watch != "cue"
	_still_pose = _goal() if elsewhere else transform
	_still_fov = _view_fov() if elsewhere else camera().fov
	if not _blending:
		_stop_ease()
	_held = true
	_shot_view = ShotView.STILL
	if _overview:
		_overview = false
		overview_changed.emit(false)
	_yaw = FULL_TABLE_YAW
	_view_zoom = 1.0


func is_held() -> bool:
	return _held


## Cuts, with no blend, to `pose` (pocket_pose) while a shot holds the camera.
func show_pocket(pose: Transform3D) -> void:
	if not _held:
		return
	_shot_view = ShotView.POCKET
	_pocket_pose = pose
	if _watch != "cue":
		return
	_stop_ease()
	camera().fov = POCKET_FOV_DEG
	_place(pose)


## Cuts, with no blend, to the full-table view from the black end while a shot holds the camera.
func show_broadcast() -> void:
	if not _held or _shot_view == ShotView.BROADCAST:
		return
	_shot_view = ShotView.BROADCAST
	if _watch != "cue":
		return
	_stop_ease()
	_apply()


## The pocket camera's pose (see POCKET_FOV_DEG) for a ball dropping into the pocket at drop_centre
## (core metres) coming in from from_xy. middle: a middle pocket. track: where else the ball is
## while the camera watches, most important first; the view turns to keep from_xy and then as many
## of those as it can in the frame.
func pocket_pose(drop_centre: Vector2, from_xy: Vector2, middle: bool, track := PackedVector2Array()) -> Transform3D:
	var out := Vector2(0.0, signf(drop_centre.y)) if middle else Vector2(signf(drop_centre.x), signf(drop_centre.y)).normalized()
	var line := out
	var into := drop_centre - from_xy
	if not middle and into.length() >= POCKET_MIN_RUN_M:
		var off_axis := deg_to_rad(POCKET_OFF_AXIS_DEG)
		line = out.rotated(clampf(out.angle_to(into), -off_axis, off_axis))
	var eye_xy := drop_centre + line * POCKET_BACK_M
	var half_fov := deg_to_rad(POCKET_FOV_DEG) / 2.0
	var below_centre := atan((POCKET_FRAME_Y - 0.5) * 2.0 * tan(half_fov))
	var pitch := atan2(POCKET_HEIGHT_M, POCKET_BACK_M) - below_centre
	# A point at bearing b off the view's centre line is within share s of the half width when
	# |tan b| <= s * tan(half the horizontal view) * cos(pitch); for a point below the eye that errs
	# on the safe side.
	var edge := tan(half_fov) * _canvas().aspect() * cos(pitch)
	var keep := atan(POCKET_KEEP_CENTRE * edge)
	var lowest := -keep
	var highest := keep
	var reach := atan(POCKET_TRACK_SHARE * edge)
	var centre_line := (-line).angle()
	var points := PackedVector2Array([from_xy])
	points.append_array(track)
	for point in points:
		if not point.is_finite() or point.distance_to(eye_xy) < POCKET_MIN_RUN_M:
			continue
		var bearing := angle_difference(centre_line, (point - eye_xy).angle())
		if absf(bearing) >= PI / 2.0:
			continue
		# The turns that keep this point in, cut down to those the points before it left; when none
		# of those do, the nearest of them.
		lowest = clampf(bearing - reach, lowest, highest)
		highest = clampf(bearing + reach, lowest, highest)
	var yaw := centre_line + clampf(0.0, lowest, highest)
	return Transform3D(_basis_for(Vector3(yaw, pitch, 0.0)), TableSpace.core_to_godot(eye_xy.x, eye_xy.y, POCKET_HEIGHT_M))


## True when a camera at Godot point `eye` is in the arena: within ROOM_MARGIN_M of the table's
## outline, below ROOM_HEIGHT_M, and above the cloth's or rails' clearance.
func in_room(eye: Vector3) -> bool:
	var xy := TableSpace.godot_to_core(eye)
	var grown := _outline.grow(ROOM_MARGIN_M)
	return eye.x > grown.position.x and eye.x < grown.end.x and eye.z > grown.position.z and eye.z < grown.end.z \
			and eye.y < ROOM_HEIGHT_M and eye.y >= clearance_at(xy)


func is_easing() -> bool:
	return _ease >= 0.0


## Ends an ease behind the cue ball at once, at its goal; a blend between views runs on.
func finish_ease() -> void:
	if _ease >= 0.0 and not _blending:
		_show(_target_cue, _target_aim, _target_elevation)
		_stop_ease()


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
	var basis := Basis.looking_at(target - eye, Vector3.UP)
	if _aim_top > 0.0:
		var per_tan := _canvas().y / 2.0 / tan(deg_to_rad(FOV_DEG) / 2.0)
		basis = basis.rotated(basis.x.normalized(), atan(_aim_top / 2.0 / per_tan))
	return Transform3D(basis, eye)


## The rig's transform for the broadcast view looking along yaw_rad (a core angle: 0 stands at
## baulk looking towards the black), BROADCAST_PITCH_DEG down, as close as the table's outline fits
## FULL_TABLE_HEIGHT of the view's height, centred, and at most END_ON_WIDTH to SIDE_ON_WIDTH of
## its width, with every pocket clear of the controls; times zoom.
func broadcast_pose(yaw_rad: float, zoom: float = 1.0) -> Transform3D:
	var basis := _broadcast_basis(yaw_rad)
	var forward := -basis.z
	var bounds := _broadcast_bounds(basis, yaw_rad)
	var eye := _clear_eye(basis, bounds)
	# End-on the table stands upright in the middle of the screen, clear of the controls by itself,
	# and takes the frame's own fit, as the reference game's does. The controls' columns and bands
	# (keep_clear), which would hold it smaller and lower, press on it only as the walk takes it
	# round, eased in over WALK_FIT_DEG.
	var off_end := asin(absf(sin(yaw_rad)))
	if off_end < deg_to_rad(WALK_FIT_DEG):
		var end_yaw := end_behind(yaw_rad)
		var end_basis := _broadcast_basis(end_yaw)
		var end_on := Transform3D(end_basis, _eye(end_basis, _broadcast_bounds(end_basis, end_yaw), 1.0, 0.0))
		if not _covers(end_on, _control_boxes):
			eye = _eye(basis, bounds, 1.0, 0.0).lerp(eye, smoothstep(0.0, deg_to_rad(WALK_FIT_DEG), off_end))
	var to_cloth := (eye.y - _ball_radius) / -forward.y
	return Transform3D(basis, eye + forward * to_cloth * (1.0 - zoom))


func _broadcast_basis(yaw_rad: float) -> Basis:
	var pitch := deg_to_rad(BROADCAST_PITCH_DEG)
	var level := TableSpace.core_to_godot(cos(yaw_rad), sin(yaw_rad))
	return Basis.looking_at(level * cos(pitch) + Vector3.DOWN * sin(pitch), Vector3.UP)


## _frame_bounds for the broadcast view's frame at yaw_rad: FULL_TABLE_HEIGHT of the canvas high,
## END_ON_WIDTH to SIDE_ON_WIDTH wide, centred.
func _broadcast_bounds(basis: Basis, yaw_rad: float) -> PackedFloat64Array:
	var canvas := _canvas()
	var width := canvas.x * lerpf(END_ON_WIDTH, SIDE_ON_WIDTH, sin(yaw_rad) * sin(yaw_rad))
	var height := canvas.y * FULL_TABLE_HEIGHT
	return _frame_bounds(basis, Rect2((canvas.x - width) / 2.0, (canvas.y - height) / 2.0, width, height))


## The rig's transform for the view from above: straight down on ABOVE_FOV_DEG, the screen's right
## towards the black, the table's outline as large as fits ABOVE_MARGIN inside what the controls
## leave and centred there; times zoom (1 to ABOVE_ZOOM_MAX), slid by pan_m (core metres, held to
## pan's limits).
func above_pose(zoom: float = 1.0, pan_m := Vector2.ZERO) -> Transform3D:
	var area := _above_area()
	var canvas := _canvas()
	var per_unit := _above_metres_per_unit(area, zoom)
	var depth := per_unit * canvas.y / 2.0 / tan(deg_to_rad(ABOVE_FOV_DEG) / 2.0)
	var limit := _above_pan_limit(zoom)
	var slid := pan_m.clamp(-limit, limit)
	var basis := _basis_for(Vector3(PI / 2.0, PI / 2.0, 0.0))
	var middle := Vector3(_outline.get_center().x, _outline.end.y, _outline.get_center().z) + TableSpace.core_to_godot(slid.x, slid.y)
	# Off the canvas's centre by as much as the clear area's middle is.
	var off := (area.get_center() - canvas / 2.0) * per_unit
	return Transform3D(basis, middle - basis.x * off.x + basis.y * off.y + basis.z * depth)


## What the controls leave of the canvas, less ABOVE_MARGIN; the whole canvas when that is too
## small to show the table.
func _above_area() -> Rect2:
	var canvas := _canvas()
	var whole := Rect2(Vector2.ZERO, canvas).grow(-ABOVE_MARGIN)
	if _keep_clear.is_empty():
		return whole
	var area := _clear_rect.grow(-ABOVE_MARGIN)
	if area.size.x < canvas.x * MIN_FIT_SCALE or area.size.y < canvas.y * MIN_FIT_SCALE:
		if not _above_warned:
			_above_warned = true
			Log.warn("app", "app.error", {"kind": "message", "message": "camera_rig: the controls %s leave the view from above only %s of the canvas %s; it uses the whole canvas"
					% [_keep_clear, area, canvas]})
		return whole
	return area


## Metres of the rail top a canvas unit covers in the view from above. The table's length runs
## across the screen and its width up it.
func _above_metres_per_unit(area: Rect2, zoom: float) -> float:
	return maxf(_outline.size.z / area.size.x, _outline.size.x / area.size.y) / clampf(zoom, 1.0, ABOVE_ZOOM_MAX)


## How far (core metres, along and across the table) the view from above at `zoom` may slide: until
## an edge of the table reaches the edge of the clear area, none while the table fits inside it.
func _above_pan_limit(zoom: float) -> Vector2:
	var area := _above_area()
	var seen := area.size / 2.0 * _above_metres_per_unit(area, zoom)
	return (Vector2(_outline.size.z, _outline.size.x) / 2.0 - seen).max(Vector2.ZERO)


## The camera's pitch in degrees with the cue raised to elevation_rad: the tilt, or higher over a
## raised cue; once a tilt has taken it off the cue, the tilt and what that left of the lift.
func pitch_for(elevation_rad: float) -> float:
	if _off_cue:
		return minf(_pitch_deg + _own_lift_deg, PITCH_MAX_DEG)
	if elevation_rad <= 0.0:
		return _pitch_deg
	return clampf(rad_to_deg(elevation_rad) + ABOVE_CUE_DEG, _pitch_deg, PITCH_MAX_DEG)


## The lowest the camera may be above the cloth at core xy.
func clearance_at(xy: Vector2) -> float:
	if absf(xy.x) < _half_size.x and absf(xy.y) < _half_size.y:
		return CLOTH_CLEARANCE_M
	return RAIL_CLEARANCE_M


## Where the camera at `pose` draws a Godot point on the canvas, for views not shown yet.
func project(pose: Transform3D, point: Vector3, fov_deg: float = FOV_DEG) -> Vector2:
	var canvas := _canvas()
	var local := pose.affine_inverse() * point
	var scale := canvas.y / 2.0 / tan(deg_to_rad(fov_deg) / 2.0) / maxf(-local.z, 1e-6)
	return canvas / 2.0 + Vector2(local.x, -local.y) * scale


## The table outline's top corners in order round it.
func _outline_corners() -> PackedVector3Array:
	var lo := _outline.position
	var hi := _outline.end
	return PackedVector3Array([Vector3(lo.x, hi.y, lo.z), Vector3(hi.x, hi.y, lo.z), Vector3(hi.x, hi.y, hi.z),
			Vector3(lo.x, hi.y, hi.z)])


## What _eye needs to place the outline inside the canvas rectangle `frame` looking along `basis`:
## the frame's top, bottom, right and left edges as tangents off the view's centre, then for each
## the outline's furthest reach past it.
# For a fixed orientation each edge of the frame is a plane through the eye, and a point is inside it
# when the eye is far enough along that plane's normal: a linear bound per point and edge.
func _frame_bounds(basis: Basis, frame: Rect2) -> PackedFloat64Array:
	var forward := -basis.z
	var half := _canvas() / 2.0
	var per_tan := half.y / tan(deg_to_rad(BROADCAST_FOV_DEG) / 2.0)
	var tans := PackedFloat64Array([(half.y - frame.position.y) / per_tan, (frame.end.y - half.y) / per_tan,
			(frame.end.x - half.x) / per_tan, (half.x - frame.position.x) / per_tan])
	var normals: Array[Vector3] = [basis.y - forward * tans[0], -basis.y - forward * tans[1], basis.x - forward * tans[2],
			-basis.x - forward * tans[3]]
	var bounds := tans.duplicate()
	for normal in normals:
		var reach := -INF
		for p in _outline_corners():
			reach = maxf(reach, p.dot(normal))
		bounds.append(reach)
	return bounds


## The eye with the outline inside the frame _frame_bounds gave, centred across. At scale 1 it is
## the closest such eye, meeting the top and bottom bounds (or the sides, whichever is further
## back); a smaller scale backs off until the table is that much smaller. low 0 centres the outline
## between the top and bottom, 1 puts it as low as it goes. Raising or lowering the eye along its
## own up leaves where everything is across the screen exactly as it was.
func _eye(basis: Basis, bounds: PackedFloat64Array, scale: float, low: float) -> Vector3:
	var forward := -basis.z
	var along := minf(-(bounds[4] + bounds[5]) / (bounds[0] + bounds[1]), -(bounds[6] + bounds[7]) / (bounds[2] + bounds[3]))
	if scale < 1.0:
		var centre := Vector3(_outline.get_center().x, _outline.end.y, _outline.get_center().z).dot(forward)
		along = centre - (centre - along) / scale
	var lowest := -bounds[5] - bounds[1] * along
	var up := lerpf((bounds[4] + bounds[0] * along + lowest) / 2.0, lowest, low)
	var side := (bounds[6] + bounds[2] * along - bounds[7] - bounds[3] * along) / 2.0
	return basis.x * side + basis.y * up + forward * along


## The broadcast eye with every pocket clear of the controls: the closest that is clear with the
## table as low as it goes, then raised towards the middle as far as it stays clear. Across the
## screen only the size matters, so the controls' columns set the size and the bands the height.
func _clear_eye(basis: Basis, bounds: PackedFloat64Array) -> Vector3:
	var scale := 1.0
	if _covers_controls(Transform3D(basis, _eye(basis, bounds, scale, 1.0))):
		scale = MIN_FIT_SCALE
		if _covers_controls(Transform3D(basis, _eye(basis, bounds, scale, 1.0))):
			if not _clear_warned:
				_clear_warned = true
				Log.warn("app", "app.error", {"kind": "message", "message": "camera_rig: the broadcast view can't clear the pockets of the controls %s at %.0f %% of its fit (outline %s, canvas %s)"
						% [_keep_clear, MIN_FIT_SCALE * 100.0, _outline, _canvas()]})
			return _eye(basis, bounds, scale, 1.0)
		var covers := 1.0
		for i in 12:
			var mid := (scale + covers) / 2.0
			if _covers_controls(Transform3D(basis, _eye(basis, bounds, mid, 1.0))):
				covers = mid
			else:
				scale = mid
	var low := 1.0
	var too_high := 0.0
	if not _covers_controls(Transform3D(basis, _eye(basis, bounds, scale, too_high))):
		return _eye(basis, bounds, scale, too_high)
	for i in 10:
		var mid := (low + too_high) / 2.0
		if _covers_controls(Transform3D(basis, _eye(basis, bounds, scale, mid))):
			too_high = mid
		else:
			low = mid
	return _eye(basis, bounds, scale, low)


func _covers_controls(pose: Transform3D) -> bool:
	return _covers(pose, _clear_boxes)


## True when the broadcast view at `pose` draws the pockets' hull over any of `boxes` (canvas
## polygons).
func _covers(pose: Transform3D, boxes: Array[PackedVector2Array]) -> bool:
	if boxes.is_empty():
		return false
	var canvas := _canvas()
	var per_m := canvas.y / 2.0 / tan(deg_to_rad(BROADCAST_FOV_DEG) / 2.0)
	var to_view := pose.affine_inverse()
	var drawn := PackedVector2Array()
	for p in _pocket_points:
		var local := to_view * p
		drawn.append(canvas / 2.0 + Vector2(local.x, -local.y) * per_m / maxf(-local.z, 1e-6))
	var pockets := Geometry2D.convex_hull(drawn)
	for box in boxes:
		if not Geometry2D.intersect_polygons(pockets, box).is_empty():
			return true
	return false


## The convex hull round the pockets' drop circles in core metres; `pockets` as
## OfsPhysics.table_shape() gives them.
static func _hull_of(pockets: Array) -> PackedVector2Array:
	var points := PackedVector2Array()
	for pocket: Variant in pockets:
		if not (pocket is Dictionary and (pocket as Dictionary).has("drop_centre") and (pocket as Dictionary).has("drop_radius")):
			continue
		var c: Array = pocket["drop_centre"]
		if c.size() < 2:
			continue
		for k in 8:
			points.append(Vector2(float(c[0]), float(c[1])) + Vector2.from_angle(k * TAU / 8.0) * float(pocket["drop_radius"]))
	return Geometry2D.convex_hull(points) if points.size() >= 3 else PackedVector2Array()


# At the cloth and at the rail top, whichever a view draws further out.
func _place_pockets() -> void:
	_pocket_points.clear()
	for p in _pocket_hull:
		_pocket_points.append(TableSpace.core_to_godot(p.x, p.y))
		_pocket_points.append(TableSpace.core_to_godot(p.x, p.y, _outline.end.y))


func _canvas() -> Vector2:
	if is_inside_tree():
		var view := get_viewport().get_visible_rect().size
		if view.y > 0.0:
			return view
	return DEFAULT_CANVAS


## A view's direction as (yaw, pitch, 0): the core angle it looks along and how far down, in
## radians. Every pose here is level, so the two describe it whole.
static func _turn(pose: Transform3D) -> Vector3:
	var forward := -pose.basis.z
	var level := TableSpace.godot_to_core(forward)
	if level.length() < 1e-6:
		# Straight down: the top of the screen says which way it faces.
		level = TableSpace.godot_to_core(pose.basis.y)
	return Vector3(atan2(level.y, level.x), asin(clampf(-forward.y, -1.0, 1.0)), 0.0)


static func _basis_for(turn: Vector3) -> Basis:
	var pitch := clampf(turn.y, deg_to_rad(-80.0), PI / 2.0)
	var level := TableSpace.core_to_godot(cos(turn.x), sin(turn.x))
	var forward := level * cos(pitch) + Vector3.DOWN * sin(pitch)
	if pitch > deg_to_rad(85.0):
		# Nearly straight down UP no longer fixes the screen's top; the view's own up does.
		return Basis.looking_at(forward, level * sin(pitch) + Vector3.UP * cos(pitch))
	return Basis.looking_at(forward, Vector3.UP)


func _view_fov() -> float:
	if _held and _shot_view == ShotView.POCKET:
		return POCKET_FOV_DEG
	if _held and _shot_view == ShotView.STILL:
		return _still_fov
	return BROADCAST_FOV_DEG if is_broadcast() else FOV_DEG


func _goal() -> Transform3D:
	if _held and _shot_view == ShotView.POCKET:
		return _pocket_pose
	if _held and _shot_view == ShotView.STILL:
		return _still_pose
	if is_broadcast():
		return broadcast_pose(_yaw, _view_zoom)
	return pose_for(_target_cue, _target_aim, _target_elevation)


## Where the camera goes: the watching view's pose, or the rig's own (_goal).
func _camera_goal() -> Transform3D:
	match _watch:
		"above":
			return above_pose(_above_zoom, _above_pan)
		"round":
			return broadcast_pose(_round_yaw, _round_zoom)
	return _goal()


func _camera_fov() -> float:
	match _watch:
		"above":
			return ABOVE_FOV_DEG
		"round":
			return BROADCAST_FOV_DEG
	return _view_fov()


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
#
# A swoop (_swing_about set) runs the eye's level part round that point instead of straight: its
# angle round the point runs the smoothstep and its distance from it runs ahead of that (see
# _blend_at), so a camera coming down from the full-table view onto an aim the other way swings
# round the cue ball as the reference game's does, rather than crossing over it. It looks at a point
# sliding from where the view looked to where the goal looks, both at the balls' height: turned by
# angle, a camera high and far away still pitched for the full-table view would look over the cue
# ball into the dark while it came down.
func _step_blend() -> void:
	var s := _ease
	var goal := _camera_goal()
	_goal_eye = goal.origin
	_goal_turn = _turn(goal)
	_goal_turn.x = _from_turn.x + angle_difference(_from_turn.x, _goal_turn.x)
	_goal_focus = _focus_of(goal)
	var swing := _swing()
	if s >= 1.0:
		if not is_broadcast() and _watch == "cue":
			_shown_cue = _target_cue
			_shown_aim = _target_aim
			_shown_elevation = _target_elevation
		camera().fov = _camera_fov()
		_place(goal)
		return
	camera().fov = lerpf(_from_fov, _camera_fov(), smoothstep(0.0, 1.0, s))
	var pose := _blend_at(s, swing)
	var eye: Vector3 = pose[0]
	eye.y = maxf(eye.y, clearance_at(TableSpace.godot_to_core(eye)))
	_place(Transform3D(_basis_for(pose[1]), eye))


## How far a swoop swings the eye round _swing_about towards the goal, in radians; 0 for a blend
## that runs straight, or an end too close to the point to have an angle round it.
func _swing() -> float:
	if is_nan(_swing_about.x):
		return 0.0
	var from := TableSpace.godot_to_core(_from_eye) - _swing_about
	var to := TableSpace.godot_to_core(_goal_eye) - _swing_about
	if from.length() < POCKET_MIN_RUN_M or to.length() < POCKET_MIN_RUN_M:
		return 0.0
	return angle_difference(from.angle(), to.angle())


## The blend's eye and turn (see _turn) at s, 0 to 1, towards the goal of the last step, before the
## clearance.
func _blend_at(s: float, swing: float) -> Array[Vector3]:
	var k := smoothstep(0.0, 1.0, s)
	var carry := s * (1.0 - s) * (1.0 - s) * _ease_s
	var eye := _from_eye.lerp(_goal_eye, k)
	if swing != 0.0:
		var from := TableSpace.godot_to_core(_from_eye) - _swing_about
		var to := TableSpace.godot_to_core(_goal_eye) - _swing_about
		# The eye closes on the point ahead of its turn round it, as the reference game's does: the
		# table grows from the first frames, and the long way round is travelled close in rather
		# than far out in the room.
		var closing := 1.0 - (1.0 - k) * (1.0 - k)
		var level := _swing_about + from.normalized().rotated(swing * k) * lerpf(from.length(), to.length(), closing)
		eye = TableSpace.core_to_godot(level.x, level.y, eye.y)
	var turn := _from_turn.lerp(_goal_turn, k)
	if is_nan(_swing_about.x):
		eye.y += absf(_goal_eye.y - _from_eye.y) * BLEND_ARC * 16.0 * s * s * (1.0 - s) * (1.0 - s)
	else:
		# The pitch runs the smoothstep and the height follows from it: a height run straight down
		# would pitch the camera sharply in the last frames, as it closes in on a point just ahead.
		var focus := _from_focus.lerp(_goal_focus, k)
		var to_focus := TableSpace.godot_to_core(focus - eye)
		eye.y = focus.y + to_focus.length() * tan(turn.y)
		if to_focus.length() > 1e-6:
			turn.x = _from_turn.x + angle_difference(_from_turn.x, to_focus.angle())
	return [eye + _from_eye_v * carry, turn + _from_turn_v * carry]


## Where the view at `pose` looks: its centre line at the balls' height, or NEAR_DISTANCE_M ahead
## when it looks level or up.
func _focus_of(pose: Transform3D) -> Vector3:
	var forward := -pose.basis.z
	var drop := pose.origin.y - _ball_radius
	if forward.y < -0.02 and drop > 0.0:
		return pose.origin + forward * minf(drop / -forward.y, FAR_M)
	return pose.origin + forward * NEAR_DISTANCE_M


## The blend's eye and turn velocities now, per second; zero when no blend runs.
func _blend_velocity() -> Array[Vector3]:
	if not _blending or _ease < 0.0:
		return [Vector3.ZERO, Vector3.ZERO]
	var swing := _swing()
	var lo := maxf(_ease - 1e-4, 0.0)
	var hi := minf(_ease + 1e-4, 1.0)
	var a := _blend_at(lo, swing)
	var b := _blend_at(hi, swing)
	var dt := (hi - lo) * _ease_s
	var turned := b[1] - a[1]
	turned.x = angle_difference(a[1].x, b[1].x)
	return [(b[0] - a[0]) / dt, turned / dt]


## swing_about: the core point a swoop swings round (see _step_blend), NAN to run straight. While
## watching the rig's own view moves nothing.
func _start_blend(duration_s: float = BLEND_S, swing_about := Vector2(NAN, NAN)) -> void:
	if _watch == "cue":
		_begin_blend(duration_s, swing_about)


func _begin_blend(duration_s: float = BLEND_S, swing_about := Vector2(NAN, NAN)) -> void:
	if not _has_pose:
		return
	var velocity := _blend_velocity()
	_from_eye = transform.origin
	_from_turn = _turn(transform)
	_from_fov = camera().fov
	_from_eye_v = velocity[0]
	_from_turn_v = velocity[1]
	_goal_eye = _from_eye
	_goal_turn = _from_turn
	_swing_about = swing_about
	_from_focus = _focus_of(transform)
	_goal_focus = _from_focus
	_blending = true
	_ease_s = duration_s
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
	camera().fov = _camera_fov()
	if _watch != "cue":
		_place(_camera_goal())
	elif _held and _shot_view == ShotView.POCKET:
		_place(_pocket_pose)
	elif _held and _shot_view == ShotView.STILL:
		_place(_still_pose)
	elif is_broadcast():
		_place(broadcast_pose(_yaw, _view_zoom))
	else:
		_place(pose_for(_shown_cue, _shown_aim, _shown_elevation))


func _place(pose: Transform3D) -> void:
	if pose.is_equal_approx(transform):
		return
	transform = pose
	moved.emit()
