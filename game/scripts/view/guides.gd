class_name Guides
extends Node3D
## Guide lines (spec 8.4). Levels 0-2 are straight-line geometry only: 0 nothing, 1 the aim line
## to the first contact and a ghost ball there, 2 also a short line along the object ball's line
## of centres. Level 3 draws the noise-free simulated paths of the cue ball and object balls at
## ShotSetup.guide_pace_m_s(), shown beside the guide, recomputed only when something changed and
## at most PREDICT_MAX_PER_S times a second. Guides never alter the shot.
##
## Children: AimLine, GhostRing, GhostBall, ObjectLine, Paths (MeshInstance3D), PaceLabel (Label3D).

const PREDICT_MAX_PER_S := 5
## A prediction slower than this logs guide.predict_slow.
const PREDICT_BUDGET_US := 4000.0
const RIBBON_WIDTH := 0.0025
const RIBBON_HEIGHT := 0.0012
const OBJECT_LINE_LENGTH := 0.3
const RING_SEGMENTS := 48
## Paths are sampled at about this many points per ball, but never finer than PATH_MIN_STEP_S.
const PATH_POINTS := 400.0
const PATH_MIN_STEP_S := 0.005
## A ball whose path never leaves this radius of its start didn't move.
const MOVED_M := 0.001
const LINE_COLOUR := Color(1.0, 1.0, 1.0, 0.8)
const OBJECT_LINE_COLOUR := Color(1.0, 1.0, 1.0, 0.6)
const GHOST_COLOUR := Color(1.0, 1.0, 1.0, 0.18)
## Each ball's colour, lifted where the real one would vanish into the green cloth.
const PATH_COLOURS := {
	"cue": Color(1.0, 1.0, 1.0, 0.85),
	"red": Color(1.0, 0.36, 0.3, 0.9),
	"yellow": Color(1.0, 0.86, 0.2, 0.85),
	"green": Color(0.55, 1.0, 0.55, 0.85),
	"brown": Color(0.8, 0.5, 0.25, 0.85),
	"blue": Color(0.3, 0.5, 1.0, 0.85),
	"pink": Color(1.0, 0.6, 0.75, 0.85),
	"black": Color(0.04, 0.04, 0.04, 0.85),
}
const PACE_LABEL_HEIGHT := 0.03
const PACE_LABEL_SIDE := 0.05
# Tolerances for "already touching": squared-distance slack (about 1e-11 m) and arc angle slack.
const TOUCH_EPS := 1.0e-12
const ANGLE_EPS := 1.0e-9
const SEGMENT_EPS := 1.0e-9

## (balls: Array[Dictionary], cue_ball: int, strike: Dictionary) -> OfsShotRecord; tests replace it.
var predictor: Callable = Physics.predict
## Seconds; tests replace it.
var clock: Callable = func() -> float: return Time.get_ticks_usec() / 1.0e6
## OfsPhysics.table_shape(); while empty, show_for reads Physics.table_shape().
var table_shape: Dictionary = {}
## Metres; while 0, show_for reads OfsTable.dimensions().
var ball_radius := 0.0

var _aim_line := MeshInstance3D.new()
var _ghost_ring := MeshInstance3D.new()
var _ghost_ball := MeshInstance3D.new()
var _object_line := MeshInstance3D.new()
var _paths := MeshInstance3D.new()
var _pace_label := Label3D.new()
var _ribbon_material := StandardMaterial3D.new()
var _level := 0
var _contact: Dictionary = {"kind": "none"}
var _geometry_key: Array = []
var _built_radius := 0.0
## The inputs the drawn paths show; a different key needs a prediction.
var _predicted_key: Array = []
## {key, balls, cue_ball, strike} waiting for the next allowed moment; empty when none waits.
var _pending: Dictionary = {}
var _last_predict_s := -INF
var _last_predict_us := 0.0
var _refusal_logged := false


func _init() -> void:
	_ribbon_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_ribbon_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_ribbon_material.vertex_color_use_as_albedo = true
	_ribbon_material.vertex_color_is_srgb = true
	_ribbon_material.cull_mode = BaseMaterial3D.CULL_DISABLED
	for pair: Array in [[_aim_line, "AimLine"], [_ghost_ring, "GhostRing"], [_object_line, "ObjectLine"], [_paths, "Paths"]]:
		var node: MeshInstance3D = pair[0]
		node.name = pair[1]
		node.mesh = ArrayMesh.new()
		node.material_override = _ribbon_material
		node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(node)
	var ghost_material := StandardMaterial3D.new()
	ghost_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	ghost_material.albedo_color = GHOST_COLOUR
	ghost_material.roughness = 0.35
	_ghost_ball.name = "GhostBall"
	_ghost_ball.material_override = ghost_material
	_ghost_ball.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_ghost_ball)
	_pace_label.name = "PaceLabel"
	_pace_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_pace_label.no_depth_test = true
	# Left-aligned so the text starts beside the cue ball instead of across it.
	_pace_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	_pace_label.font_size = 40
	_pace_label.outline_size = 10
	_pace_label.pixel_size = 0.0006
	_pace_label.modulate = Color(1.0, 1.0, 1.0, 0.9)
	_pace_label.outline_modulate = Color(0.0, 0.0, 0.0, 0.6)
	add_child(_pace_label)
	set_process(false)
	_set_visible_nodes(false, false, false)


func show_for(balls: Array[Dictionary], cue_ball: int, setup: ShotSetup, level: int) -> void:
	_level = clampi(level, 0, 3)
	if _level == 0 or cue_ball < 0 or cue_ball >= balls.size():
		hide_all()
		return
	var r := _radius()
	# The shape's size is in the key so geometry drawn before Physics was configured is redone.
	var geometry_key: Array = [cue_ball, setup.aim_rad, balls.duplicate(true), _shape().size()]
	if _level < 3:
		_pending = {}
		set_process(false)
		if geometry_key != _geometry_key or r != _built_radius:
			_geometry_key = geometry_key
			_contact = first_contact(balls, cue_ball, setup.aim_rad, _shape(), r)
			_build_geometry(balls, r)
		var has_contact: bool = _contact["kind"] != "none"
		_set_visible_nodes(has_contact, _level == 2 and _contact["kind"] == "ball", false)
		return
	var key: Array = geometry_key + [setup.tip_x, setup.tip_y, setup.elevation_rad, setup.guide_pace_m_s()]
	_place_pace_label(balls[cue_ball], setup)
	if key != _predicted_key:
		_request_prediction(key, balls, cue_ball, setup)
	elif not _pending.is_empty():
		# Back where the drawn paths already are: an older wait would only draw a stale aim.
		_pending = {}
		set_process(false)
	_set_visible_nodes(false, false, true)


func hide_all() -> void:
	_pending = {}
	set_process(false)
	_set_visible_nodes(false, false, false)


## The last prediction's wall time in microseconds, for the debug overlay.
func last_predict_us() -> float:
	return _last_predict_us


## The straight-line first contact along aim_rad from the cue ball (radius r in metres): {kind:
## "ball" | "cushion" | "pocket" | "none", index (the ball, else -1), ghost: Vector2 (the cue
## ball's centre at contact, core xy), distance (metres the cue ball travels), object_dir: Vector2
## (unit, along the line of centres for a ball, else Vector2.ZERO)}. shape is
## OfsPhysics.table_shape(). Jaws and facings count as "cushion"; "pocket" means the centre reaches
## a drop circle without touching anything first, as a ball sent cleanly through a mouth does.
## Potted balls (pocket >= 0) and balls behind the cue ball are ignored; a ball already touching
## the cue ball in the aim's direction is hit at distance 0.
static func first_contact(balls: Array[Dictionary], cue_ball: int, aim_rad: float, shape: Dictionary, r: float) -> Dictionary:
	var result := {"kind": "none", "index": -1, "ghost": Vector2.ZERO, "distance": 0.0, "object_dir": Vector2.ZERO}
	if cue_ball < 0 or cue_ball >= balls.size():
		return result
	var cx := float(balls[cue_ball]["x"])
	var cy := float(balls[cue_ball]["y"])
	var dx := cos(aim_rad)
	var dy := sin(aim_rad)
	var best := INF
	var kind := "none"
	var index := -1
	for i in balls.size():
		if i == cue_ball or int(balls[i].get("pocket", -1)) >= 0:
			continue
		var t := _ray_circle(cx - float(balls[i]["x"]), cy - float(balls[i]["y"]), dx, dy, 2.0 * r)
		if t < best:
			best = t
			kind = "ball"
			index = i
	for line: Dictionary in shape.get("lines", []):
		var t := _ray_line(cx, cy, dx, dy, line, r)
		if t < best:
			best = t
			kind = "cushion"
			index = -1
	for arc: Dictionary in shape.get("arcs", []):
		var t := _ray_arc(cx, cy, dx, dy, arc, r)
		if t < best:
			best = t
			kind = "cushion"
			index = -1
	for pocket: Dictionary in shape.get("pockets", []):
		var centre: Variant = pocket["drop_centre"]
		var t := _ray_circle(cx - float(centre[0]), cy - float(centre[1]), dx, dy, float(pocket["drop_radius"]))
		if t < best:
			best = t
			kind = "pocket"
			index = -1
	if kind == "none":
		return result
	var gx := cx + best * dx
	var gy := cy + best * dy
	result["kind"] = kind
	result["index"] = index
	result["ghost"] = Vector2(gx, gy)
	result["distance"] = best
	if kind == "ball":
		var ox := float(balls[index]["x"]) - gx
		var oy := float(balls[index]["y"]) - gy
		var length := sqrt(ox * ox + oy * oy)
		result["object_dir"] = Vector2(ox / length, oy / length) if length > 0.0 else Vector2(dx, dy)
	return result


## Distance along the unit ray (dx, dy) until its origin, at w from a circle's centre, reaches
## `radius`; INF when it never does. A start already on or inside the circle counts as 0 when
## heading inwards.
static func _ray_circle(wx: float, wy: float, dx: float, dy: float, radius: float) -> float:
	var b := wx * dx + wy * dy
	var c := wx * wx + wy * wy - radius * radius
	if c <= TOUCH_EPS:
		return 0.0 if b < 0.0 else INF
	var disc := b * b - c
	if b >= 0.0 or disc < 0.0:
		return INF
	return -b - sqrt(disc)


static func _ray_line(cx: float, cy: float, dx: float, dy: float, line: Dictionary, r: float) -> float:
	var p1: Variant = line["p1"]
	var p2: Variant = line["p2"]
	var normal: Variant = line["normal"]
	var nx := float(normal[0])
	var ny := float(normal[1])
	var heading := dx * nx + dy * ny
	if heading >= 0.0:
		return INF
	var x1 := float(p1[0])
	var y1 := float(p1[1])
	var height := (cx - x1) * nx + (cy - y1) * ny
	if height < 0.0:
		return INF
	var t := maxf((height - r) / -heading, 0.0)
	var ex := float(p2[0]) - x1
	var ey := float(p2[1]) - y1
	var s := ((cx + t * dx - x1) * ex + (cy + t * dy - y1) * ey) / (ex * ex + ey * ey)
	return t if s >= -SEGMENT_EPS and s <= 1.0 + SEGMENT_EPS else INF


static func _ray_arc(cx: float, cy: float, dx: float, dy: float, arc: Dictionary, r: float) -> float:
	var centre: Variant = arc["centre"]
	var ox := float(centre[0])
	var oy := float(centre[1])
	var t := _ray_circle(cx - ox, cy - oy, dx, dy, float(arc["radius"]) + r)
	if t == INF:
		return INF
	var start := float(arc["angle_start"])
	var past := fposmod(atan2(cy + t * dy - oy, cx + t * dx - ox) - start, TAU)
	if past <= float(arc["angle_end"]) - start + ANGLE_EPS or past >= TAU - ANGLE_EPS:
		return t
	return INF


func _process(_delta: float) -> void:
	if _pending.is_empty():
		set_process(false)
		return
	var now := float(clock.call())
	if now - _last_predict_s >= 1.0 / PREDICT_MAX_PER_S:
		_predict_pending(now)


func _request_prediction(key: Array, balls: Array[Dictionary], cue_ball: int, setup: ShotSetup) -> void:
	var strike := setup.to_strike()
	strike["aim_rad"] = setup.aim_rad
	strike["speed_m_s"] = setup.guide_pace_m_s()
	_pending = {"key": key, "balls": balls.duplicate(true), "cue_ball": cue_ball, "strike": strike}
	var now := float(clock.call())
	if now - _last_predict_s >= 1.0 / PREDICT_MAX_PER_S:
		_predict_pending(now)
	else:
		set_process(true)


func _predict_pending(now: float) -> void:
	var job := _pending
	_pending = {}
	set_process(false)
	_last_predict_s = now
	var balls: Array[Dictionary] = job["balls"]
	var record: Variant = predictor.call(balls, job["cue_ball"], job["strike"])
	_last_predict_us = (float(clock.call()) - now) * 1.0e6
	if _last_predict_us > PREDICT_BUDGET_US:
		Log.warn("ui", "guide.predict_slow", {"sim_us": _last_predict_us, "budget_us": PREDICT_BUDGET_US, "level": _level})
	_predicted_key = job["key"]
	if record == null:
		_set_ribbons(_paths, [], [])
		if not _refusal_logged:
			_refusal_logged = true
			Log.warn("app", "app.error", {"kind": "message", "message":
					"guides: no level 3 prediction (predictor returned null for cue ball %d of %d balls, strike %s); paths hidden until one succeeds"
					% [job["cue_ball"], balls.size(), JSON.stringify(job["strike"])]})
		return
	_refusal_logged = false
	var lines: Array[PackedVector2Array] = []
	var colours: Array[Color] = []
	var paths := _moving_paths(record)
	for i: int in paths:
		lines.append(paths[i])
		colours.append(PATH_COLOURS.get(String(balls[i].get("ball", "")) if i < balls.size() else "", Color.WHITE))
	_set_ribbons(_paths, lines, colours)


## Every moving ball's centre path by ball index. All record access is here, so tests can hand
## in any object with t_end(), ball_count() and path(ball, max_step_s).
static func _moving_paths(record: Variant) -> Dictionary:
	var paths := {}
	var step := maxf(PATH_MIN_STEP_S, float(record.t_end()) / PATH_POINTS)
	for i in int(record.ball_count()):
		var path: PackedVector2Array = record.path(i, step)
		for p in path:
			if p.distance_to(path[0]) > MOVED_M:
				paths[i] = path
				break
	return paths


func _build_geometry(balls: Array[Dictionary], r: float) -> void:
	if r != _built_radius:
		_built_radius = r
		var sphere := SphereMesh.new()
		sphere.radius = r
		sphere.height = 2.0 * r
		sphere.radial_segments = 32
		sphere.rings = 16
		_ghost_ball.mesh = sphere
		var ring := PackedVector2Array()
		for i in RING_SEGMENTS + 1:
			ring.append(Vector2.from_angle(TAU * i / RING_SEGMENTS) * r)
		_set_ribbons(_ghost_ring, [ring], [LINE_COLOUR])
	if _contact["kind"] == "none":
		return
	var cue: Dictionary = balls[int(_geometry_key[0])]
	var ghost: Vector2 = _contact["ghost"]
	_set_ribbons(_aim_line, [PackedVector2Array([Vector2(float(cue["x"]), float(cue["y"])), ghost])], [LINE_COLOUR])
	_ghost_ring.position = TableSpace.core_to_godot(ghost.x, ghost.y, 0.0)
	_ghost_ball.position = TableSpace.core_to_godot(ghost.x, ghost.y, r)
	if _contact["kind"] == "ball":
		var target: Dictionary = balls[int(_contact["index"])]
		var from := Vector2(float(target["x"]), float(target["y"]))
		var dir: Vector2 = _contact["object_dir"]
		_set_ribbons(_object_line, [PackedVector2Array([from, from + dir * OBJECT_LINE_LENGTH])], [OBJECT_LINE_COLOUR])


func _place_pace_label(cue: Dictionary, setup: ShotSetup) -> void:
	var pace := setup.guide_pace_m_s()
	# Before the session's first stroke the pace is the default, not anything the player did.
	var from_stroke := setup.strike_mode == ShotSetup.STROKE and setup.last_stroke_speed_m_s > 0.0
	var text := ("last stroke %.1f m/s" if from_stroke else "pace %.1f m/s") % pace
	if _pace_label.text != text:
		_pace_label.text = text
	var right := Vector2(sin(setup.aim_rad), -cos(setup.aim_rad)) * PACE_LABEL_SIDE
	_pace_label.position = TableSpace.core_to_godot(float(cue["x"]) + right.x, float(cue["y"]) + right.y, PACE_LABEL_HEIGHT)


func _set_visible_nodes(geometry: bool, object_line: bool, paths: bool) -> void:
	_aim_line.visible = geometry
	_ghost_ring.visible = geometry
	_ghost_ball.visible = geometry
	_object_line.visible = object_line
	_paths.visible = paths
	_pace_label.visible = paths


func _radius() -> float:
	if ball_radius <= 0.0:
		ball_radius = float(OfsTable.dimensions().get("ball_radius", 0.0))
	return ball_radius


func _shape() -> Dictionary:
	if table_shape.is_empty():
		table_shape = Physics.table_shape()
	return table_shape


## Flat ribbons RIBBON_WIDTH wide just above the cloth along each polyline (core xy, relative to
## the node), mitred at the joints so overlapping quads don't darken them.
static func _set_ribbons(node: MeshInstance3D, lines: Array, colours: Array) -> void:
	var mesh: ArrayMesh = node.mesh
	mesh.clear_surfaces()
	var vertices := PackedVector3Array()
	var vertex_colours := PackedColorArray()
	for li in lines.size():
		var points := PackedVector2Array()
		for p: Vector2 in lines[li]:
			if points.is_empty() or p.distance_to(points[points.size() - 1]) > 1.0e-4:
				points.append(p)
		if points.size() < 2:
			continue
		var offsets := PackedVector2Array()
		for i in points.size():
			var before := (points[i] - points[i - 1]).normalized().orthogonal() if i > 0 else Vector2.ZERO
			var after := (points[i + 1] - points[i]).normalized().orthogonal() if i < points.size() - 1 else Vector2.ZERO
			var miter := before + after
			var reference := after if i < points.size() - 1 else before
			if i == 0 or i == points.size() - 1 or miter.length_squared() < 1.0e-6:
				offsets.append(reference * RIBBON_WIDTH / 2.0)
			else:
				miter = miter.normalized()
				offsets.append(miter * RIBBON_WIDTH / 2.0 / maxf(miter.dot(reference), 0.5))
		var colour: Color = colours[li]
		for i in points.size() - 1:
			var a := points[i]
			var b := points[i + 1]
			for p: Vector2 in [a + offsets[i], a - offsets[i], b + offsets[i + 1], a - offsets[i], b - offsets[i + 1], b + offsets[i + 1]]:
				vertices.append(TableSpace.core_to_godot(p.x, p.y, RIBBON_HEIGHT))
				vertex_colours.append(colour)
	if vertices.is_empty():
		return
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_COLOR] = vertex_colours
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
