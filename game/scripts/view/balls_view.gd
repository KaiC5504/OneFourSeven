class_name BallsView
extends Node3D
## The balls (spec 13.1). Children are built from a ball list in its order, so state index i is
## child i. Each is named "cue", "red_<index>" or the colour, with metas "ball", "index" and
## "core_xy" ([x, y] at full precision; the harness's build_state reads them). Spin shows by
## turning each ball by its angular velocity; markings make it visible on the cue ball.

const BALL_COLOURS := {
	"cue": Color(0.93, 0.92, 0.86),
	"red": Color(0.62, 0.03, 0.04),
	"yellow": Color(0.93, 0.76, 0.08),
	"green": Color(0.03, 0.36, 0.16),
	"brown": Color(0.36, 0.17, 0.07),
	"blue": Color(0.04, 0.15, 0.58),
	"pink": Color(0.96, 0.52, 0.62),
	"black": Color(0.02, 0.02, 0.022),
}
const DOT_COLOUR := Color(0.72, 0.04, 0.05)
## cos of the dots' angular radius: 6 mm across on a 52.5 mm ball.
const DOT_COS := 0.99345
const RADIUS_FALLBACK := 0.02625
const CONTACT_SHADOW_SCALE := 1.7
const CONTACT_SHADOW_LIFT := 0.0008
const PICK_RADII := 1.6
const DROP_SECONDS := 0.3
const DROP_DEPTH := 0.075

# Dots at the six axis points of the ball's own frame, anti-aliased with fwidth so they stay
# round and crisp from the overhead view to a close-up.
const BALL_SHADER := """
shader_type spatial;

uniform vec3 base_colour : source_color = vec3(0.9);
uniform vec3 dot_colour : source_color = vec3(0.7, 0.04, 0.05);
uniform float dot_cos = 2.0;

varying vec3 local_dir;

void vertex() {
	local_dir = VERTEX;
}

void fragment() {
	vec3 a = abs(normalize(local_dir));
	float m = max(a.x, max(a.y, a.z));
	float w = fwidth(m);
	ALBEDO = mix(base_colour, dot_colour, smoothstep(dot_cos - w, dot_cos + w, m));
	ROUGHNESS = 0.16;
	SPECULAR = 0.5;
	CLEARCOAT = 1.0;
	CLEARCOAT_ROUGHNESS = 0.05;
}
"""

var radius := RADIUS_FALLBACK
var _balls: Array[MeshInstance3D] = []
var _shadows: Array[MeshInstance3D] = []
var _xy: Array[Array] = []
var _pockets: Array[Vector2] = []
# Seconds into each ball's drop, or -1 when it isn't dropping.
var _drop_elapsed := PackedFloat32Array()
var _drop_from := PackedVector3Array()
var _drop_to := PackedVector3Array()
var _gone := PackedByteArray()
var _dropping := 0
var _short_state_logged := false


## The pockets' drop centres, from OfsPhysics.table_shape()["pockets"], for the potted balls' drop.
func set_pockets(pockets: Array) -> void:
	_pockets.clear()
	for p: Variant in pockets:
		var centre: Variant = p["drop_centre"]
		_pockets.append(Vector2(float(centre[0]), float(centre[1])))


## Replaces every ball with the list's (the free table after a shot, an edit, a retake).
func build(balls: Array[Dictionary]) -> void:
	for child in get_children():
		remove_child(child)
		child.queue_free()
	_balls.clear()
	_shadows.clear()
	_xy.clear()
	_set_dropping(0)
	_short_state_logged = false
	var dims := OfsTable.dimensions()
	radius = float(dims.get("ball_radius", RADIUS_FALLBACK))
	var mesh := SphereMesh.new()
	mesh.radius = radius
	mesh.height = radius * 2.0
	mesh.radial_segments = 64
	mesh.rings = 32
	var shader := Shader.new()
	shader.code = BALL_SHADER
	var materials := {}
	var shadow := _contact_shadow_mesh()
	for placement in balls:
		var kind := String(placement.get("ball", ""))
		var index := int(placement.get("index", 0))
		if not BALL_COLOURS.has(kind):
			Log.warn("app", "app.error", {"kind": "message",
					"message": "balls_view: unknown ball '%s' at list index %d; drawn as the cue ball" % [kind, _balls.size()]})
		var colour_key := kind if BALL_COLOURS.has(kind) else "cue"
		if not materials.has(colour_key):
			var m := ShaderMaterial.new()
			m.shader = shader
			m.set_shader_parameter("base_colour", BALL_COLOURS[colour_key])
			m.set_shader_parameter("dot_colour", DOT_COLOUR)
			m.set_shader_parameter("dot_cos", DOT_COS if colour_key == "cue" else 2.0)
			materials[colour_key] = m
		var x := float(placement.get("x", 0.0))
		var y := float(placement.get("y", 0.0))
		var ball := MeshInstance3D.new()
		ball.name = "red_%d" % index if kind == "red" else kind
		ball.mesh = mesh
		ball.material_override = materials[colour_key]
		ball.set_meta("ball", kind)
		ball.set_meta("index", index)
		# Node positions are single precision; the state dump reports these doubles. The array is
		# updated in place as the ball moves.
		var xy := [x, y]
		ball.set_meta("core_xy", xy)
		ball.position = TableSpace.core_to_godot(x, y, radius)
		var contact := MeshInstance3D.new()
		contact.name = "ContactShadow"
		contact.mesh = shadow
		contact.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		contact.position = Vector3(0.0, -radius + CONTACT_SHADOW_LIFT, 0.0)
		ball.add_child(contact)
		add_child(ball, true)
		_balls.append(ball)
		_shadows.append(contact)
		_xy.append(xy)
	var count := _balls.size()
	_drop_elapsed.resize(count)
	_drop_elapsed.fill(-1.0)
	_drop_from.resize(count)
	_drop_to.resize(count)
	_gone.resize(count)
	_gone.fill(0)


## Moves every ball to `state` (ShotPlayback layout) and turns it by its spin over dt seconds of
## shot time. A ball that has become pocketed plays its drop into the pocket and then hides.
func apply_state(state: PackedFloat64Array, dt: float) -> void:
	var count := mini(_balls.size(), state.size() / ShotPlayback.STRIDE)
	if count < _balls.size() and not _short_state_logged:
		_short_state_logged = true
		Log.warn("app", "app.error", {"kind": "message",
				"message": "balls_view: state has %d doubles for %d balls; the rest stay put" % [state.size(), _balls.size()]})
	for i in count:
		var o := i * ShotPlayback.STRIDE
		var x := state[o + ShotPlayback.X]
		var y := state[o + ShotPlayback.Y]
		if int(state[o + ShotPlayback.MOTION]) == ShotPlayback.POCKETED:
			if _gone[i] == 0 and _drop_elapsed[i] < 0.0:
				_start_drop(i, x, y, int(state[o + ShotPlayback.POCKET]))
			continue
		if _gone[i] != 0 or _drop_elapsed[i] >= 0.0:
			_restore(i)
		var ball := _balls[i]
		ball.position = Vector3(-y, radius, -x)
		var xy := _xy[i]
		xy[0] = x
		xy[1] = y
		# Core angular velocity in Godot axes; (x, y, z) -> (-y, z, -x) is a proper rotation.
		var w := Vector3(-state[o + ShotPlayback.WY], state[o + ShotPlayback.WZ], -state[o + ShotPlayback.WX])
		var rate := w.length()
		if rate * dt > 1e-9:
			ball.basis = (Basis(w / rate, rate * dt) * ball.basis).orthonormalized()
			_place_shadow(i)


## The index of the ball under a screen point, or -1.
func ball_at_screen(camera: Camera3D, screen_pos: Vector2) -> int:
	var hit: Variant = TableSpace.screen_to_core(camera, screen_pos, radius)
	if hit == null:
		return -1
	var point: Vector2 = hit
	var best := -1
	var best_distance := PICK_RADII * radius
	for i in _balls.size():
		if _gone[i] != 0 or _drop_elapsed[i] >= 0.0 or not _balls[i].visible:
			continue
		var distance := point.distance_to(Vector2(float(_xy[i][0]), float(_xy[i][1])))
		if distance <= best_distance:
			best_distance = distance
			best = i
	return best


## Ends every drop under way at once, its ball hidden (a skip to the end, a rebuild).
func finish_drops() -> void:
	for i in _balls.size():
		if _drop_elapsed[i] >= 0.0:
			_finish_drop(i)


func is_dropping() -> bool:
	return _dropping > 0


func _process(delta: float) -> void:
	if _dropping == 0:
		return
	for i in _balls.size():
		if _drop_elapsed[i] < 0.0:
			continue
		var t := minf(_drop_elapsed[i] + delta, DROP_SECONDS)
		_drop_elapsed[i] = t
		var f := t / DROP_SECONDS
		var p := _drop_from[i].lerp(_drop_to[i], smoothstep(0.0, 1.0, minf(f * 1.5, 1.0)))
		p.y = radius - DROP_DEPTH * f * f
		_balls[i].position = p
		if f >= 1.0:
			_finish_drop(i)


func _start_drop(i: int, x: float, y: float, pocket: int) -> void:
	var to := Vector2(x, y)
	if pocket >= 0 and pocket < _pockets.size():
		to = _pockets[pocket]
	else:
		Log.warn("app", "app.error", {"kind": "message",
				"message": "balls_view: ball %d potted in pocket %d with %d pockets set; it sinks where it is" % [i, pocket, _pockets.size()]})
	_drop_from[i] = Vector3(-y, radius, -x)
	_drop_to[i] = Vector3(-to.y, radius, -to.x)
	_balls[i].position = _drop_from[i]
	var xy := _xy[i]
	xy[0] = x
	xy[1] = y
	_shadows[i].visible = false
	_drop_elapsed[i] = 0.0
	_set_dropping(_dropping + 1)


func _finish_drop(i: int) -> void:
	_drop_elapsed[i] = -1.0
	_gone[i] = 1
	_balls[i].visible = false
	_set_dropping(_dropping - 1)


func _restore(i: int) -> void:
	if _drop_elapsed[i] >= 0.0:
		_drop_elapsed[i] = -1.0
		_set_dropping(_dropping - 1)
	_gone[i] = 0
	_balls[i].visible = true
	_shadows[i].visible = true


func _set_dropping(count: int) -> void:
	_dropping = maxi(count, 0)
	Perf.set_active(&"ball_drop", _dropping > 0)


# The shadow is the ball's child but must stay flat on the cloth under it, whatever the spin.
func _place_shadow(i: int) -> void:
	var inverse := _balls[i].basis.transposed()
	_shadows[i].transform = Transform3D(inverse, inverse * Vector3(0.0, -radius + CONTACT_SHADOW_LIFT, 0.0))


## A soft dark blob on the cloth under a ball. The directional shadow alone is too faint and too
## small at phone resolution to read as the ball resting on the cloth.
func _contact_shadow_mesh() -> PlaneMesh:
	var gradient := Gradient.new()
	gradient.offsets = PackedFloat32Array([0.0, 0.35, 1.0])
	gradient.colors = PackedColorArray([Color(0.0, 0.0, 0.0, 0.85), Color(0.0, 0.0, 0.0, 0.5), Color(0.0, 0.0, 0.0, 0.0)])
	var texture := GradientTexture2D.new()
	texture.gradient = gradient
	texture.fill = GradientTexture2D.FILL_RADIAL
	texture.fill_from = Vector2(0.5, 0.5)
	texture.fill_to = Vector2(1.0, 0.5)
	texture.width = 64
	texture.height = 64
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_texture = texture
	var mesh := PlaneMesh.new()
	mesh.size = Vector2.ONE * radius * CONTACT_SHADOW_SCALE * 2.0
	mesh.material = material
	return mesh
