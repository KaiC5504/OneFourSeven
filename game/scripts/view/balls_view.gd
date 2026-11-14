class_name BallsView
extends Node3D
## The balls (spec 13.1). Children are built from a ball list in its order, so state index i is
## child i. Each is named "cue", "red_<index>" or the colour, with metas "ball", "index" and
## "core_xy" ([x, y] at full precision; the harness's build_state reads them). Spin shows by
## turning each ball by its angular velocity; markings make it visible on the cue ball.
##
## With the bought table model's balls in res://licensed (BOUGHT_PATH) those are drawn: their
## meshes, colours and gloss, with our six dots on the cue ball and our soft highlight. Without
## them, our own.

## Our own balls' colours; ball_colour() gives the colours on the table.
const BALL_COLOURS := {
	"cue": Color(0.93, 0.91, 0.86),
	"red": Color(0.62, 0.03, 0.03),
	"yellow": Color(0.93, 0.76, 0.08),
	"green": Color(0.03, 0.36, 0.16),
	"brown": Color(0.36, 0.17, 0.07),
	"blue": Color(0.04, 0.15, 0.58),
	"pink": Color(0.96, 0.52, 0.62),
	"black": Color(0.02, 0.02, 0.02),
}
const DOT_COLOUR := Color(0.72, 0.04, 0.05)
## cos of the dots' angular radius: 6 mm across on a 52.5 mm ball.
const DOT_COS := 0.99345
const RADIUS_FALLBACK := 0.02625
const CONTACT_SHADOW_SCALE := 1.7
const CONTACT_SHADOW_LIFT := 0.0008
const PICK_RADII := 1.6
## A dying ball's drop; one that comes in faster moves in at its own pace and is gone sooner.
const DROP_SECONDS := 0.3
## No drop is shorter: a ball takes about this long to fall DROP_DEPTH.
const DROP_MIN_SECONDS := 0.15
## The part of a drop spent moving in over the hole.
const DROP_IN_FRACTION := 2.0 / 3.0
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
	ROUGHNESS = 0.38;
	SPECULAR = 0.3;
	CLEARCOAT = 0.2;
	CLEARCOAT_ROUGHNESS = 0.3;
}
"""

const BOUGHT_PATH := "res://licensed/balls.gltf"
const BOUGHT_NODES := {
	"cue": "BallCue",
	"red": "BallRed",
	"yellow": "BallYellow",
	"green": "BallGreen",
	"brown": "BallBrown",
	"blue": "BallBlue",
	"pink": "BallPink",
	"black": "BallBlack",
}
## A bought mesh further than this from our ball's size is not drawn: it would sink into the cloth
## or float over it.
const BOUGHT_SIZE_TOLERANCE := 0.0005

# The bought balls: each mesh picks its colour from one swatch texture, and the surface keeps the
# model's roughness for what it reflects. The table light's highlight is ours: at their roughness
# it is a hard white dot, so light() repeats what BALL_SHADER's surface does with a light (roughness
# 0.38, specular 0.3, clearcoat 0.2 at roughness 0.3) with the renderer's own formulas.
const BOUGHT_SHADER := """
shader_type spatial;

uniform sampler2D swatches : source_color, filter_linear_mipmap, repeat_disable;
uniform float gloss_roughness = 0.1;
uniform float gloss_specular = 0.5;
uniform vec3 dot_colour : source_color = vec3(0.7, 0.04, 0.05);
uniform float dot_cos = 2.0;

const float LOBE_ALPHA = 0.1444;
const float LOBE_F0 = 0.0144;
const float LOBE_F90 = 0.7128;
const float COAT = 0.2;
const float COAT_ALPHA = 0.0307;

varying vec3 local_dir;
// light()'s own NORMAL and VIEW are too coarse for the clearcoat's tight lobe: it comes out speckled.
varying vec3 exact_normal;
varying vec3 exact_view;

// The GGX lobe from sin^2 of the angle between the normal and the half vector.
float ggx(float sin2, float alpha) {
	float a2 = alpha * alpha;
	float k = alpha / (sin2 + (1.0 - sin2) * a2);
	return k * k / PI;
}

void vertex() {
	local_dir = VERTEX;
}

void fragment() {
	vec3 a = abs(normalize(local_dir));
	float m = max(a.x, max(a.y, a.z));
	float w = fwidth(m);
	ALBEDO = mix(texture(swatches, UV).rgb, dot_colour, smoothstep(dot_cos - w, dot_cos + w, m));
	ROUGHNESS = gloss_roughness;
	SPECULAR = gloss_specular;
	// A sphere's normal is its direction from the centre, which the mesh's coarse poles only approximate.
	exact_normal = normalize(mat3(VIEW_MATRIX) * (MODEL_NORMAL_MATRIX * normalize(local_dir)));
	exact_view = normalize(-VERTEX);
	NORMAL = exact_normal;
}

void light() {
	float nl = clamp(dot(exact_normal, LIGHT), 0.0, 1.0);
	DIFFUSE_LIGHT += nl * ATTENUATION * LIGHT_COLOR / PI;
	vec3 h = normalize(exact_view + LIGHT);
	vec3 across = cross(exact_normal, h);
	float sin2 = dot(exact_normal, h) > 0.0 ? min(dot(across, across), 1.0) : 1.0;
	float nv = max(dot(exact_normal, exact_view), 1e-4);
	float lh = max(dot(LIGHT, h), 1e-4);
	float fresnel = pow(1.0 - lh, 5.0);
	float lobe = ggx(sin2, LOBE_ALPHA) * 0.5 / mix(2.0 * nl * nv, nl + nv, LOBE_ALPHA) * mix(LOBE_F0, LOBE_F90, fresnel);
	float coat = COAT * ggx(sin2, COAT_ALPHA) * 0.25 / (lh * lh) * mix(0.04, 1.0, fresnel);
	SPECULAR_LIGHT += nl * (lobe + coat) * SPECULAR_AMOUNT * ATTENUATION * LIGHT_COLOR;
}
"""

## The bought balls once looked for: {"meshes": {kind: Mesh}, "swatches": Texture2D, "roughness",
## "specular", "radius", "colours": {kind: Color}}, or {} when they aren't there or can't be used.
static var _bought := {}
static var _bought_looked := false

var radius := RADIUS_FALLBACK
var _balls: Array[MeshInstance3D] = []
var _shadows: Array[MeshInstance3D] = []
var _xy: Array[Array] = []
var _pockets: Array[Vector2] = []
# Seconds into each ball's drop, or -1 when it isn't dropping.
var _drop_elapsed := PackedFloat32Array()
var _drop_from := PackedVector3Array()
var _drop_to := PackedVector3Array()
# Per drop: its length in seconds, the seconds spent moving in, and the pace it starts at as a
# multiple of the mean pace of moving in (0 from rest, at most 3).
var _drop_pace := PackedVector3Array()
# Each ball's last velocity on the cloth, core m/s.
var _velocity := PackedVector2Array()
var _gone := PackedByteArray()
var _dropping := 0
var _short_state_logged := false
var _mesh: SphereMesh
var _shader: Shader
var _bought_shader: Shader
var _materials := {}
var _shadow_mesh: PlaneMesh


func _ready() -> void:
	set_process(_dropping > 0)


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
	_mesh = SphereMesh.new()
	_mesh.radius = radius
	_mesh.height = radius * 2.0
	_mesh.radial_segments = 64
	_mesh.rings = 32
	_shader = Shader.new()
	_shader.code = BALL_SHADER
	_bought_shader = null
	if not bought(radius).is_empty():
		_bought_shader = Shader.new()
		_bought_shader.code = BOUGHT_SHADER
	_materials = {}
	_shadow_mesh = _contact_shadow_mesh()
	for placement in balls:
		_add_ball(placement)
	_reset_drops()


## Moves to a new list (the free table after a shot, an edit, a retake) keeping every ball whose
## name is still in it, turned as it was, so the cue ball's dots don't jump between shots. New
## balls are built, missing ones freed, the children put in the list's order, and any drop ends.
func sync(balls: Array[Dictionary]) -> void:
	if _mesh == null:
		build(balls)
		return
	var wanted := {}
	for placement in balls:
		wanted[_ball_name(placement)] = true
	var kept := {}
	for i in _balls.size():
		var ball := _balls[i]
		if wanted.has(String(ball.name)) and not kept.has(String(ball.name)):
			kept[String(ball.name)] = [ball, _shadows[i]]
		else:
			remove_child(ball)
			ball.queue_free()
	_balls.clear()
	_shadows.clear()
	_xy.clear()
	_short_state_logged = false
	for placement in balls:
		var entry: Array = kept.get(_ball_name(placement), [])
		if entry.is_empty():
			_add_ball(placement)
			continue
		var ball: MeshInstance3D = entry[0]
		var shadow: MeshInstance3D = entry[1]
		var xy := [float(placement.get("x", 0.0)), float(placement.get("y", 0.0))]
		ball.set_meta("core_xy", xy)
		ball.position = TableSpace.core_to_godot(xy[0], xy[1], radius)
		ball.visible = true
		shadow.visible = true
		_balls.append(ball)
		_shadows.append(shadow)
		_xy.append(xy)
	for i in _balls.size():
		move_child(_balls[i], i)
	_reset_drops()


## Puts ball i at core xy on the cloth, as an edit drag shows it before the move is made.
func place_ball(i: int, xy: Vector2) -> void:
	if i < 0 or i >= _balls.size():
		return
	_balls[i].position = TableSpace.core_to_godot(xy.x, xy.y, radius)
	_xy[i][0] = float(xy.x)
	_xy[i][1] = float(xy.y)


func _add_ball(placement: Dictionary) -> void:
	var kind := String(placement.get("ball", ""))
	var index := int(placement.get("index", 0))
	if not BALL_COLOURS.has(kind):
		Log.warn("app", "app.error", {"kind": "message",
				"message": "balls_view: unknown ball '%s' at list index %d; drawn as the cue ball" % [kind, _balls.size()]})
	var colour_key := kind if BALL_COLOURS.has(kind) else "cue"
	var model := bought(radius) if _bought_shader != null else {}
	if not _materials.has(colour_key):
		var m := ShaderMaterial.new()
		if model.is_empty():
			m.shader = _shader
			m.set_shader_parameter("base_colour", BALL_COLOURS[colour_key])
		else:
			m.shader = _bought_shader
			m.set_shader_parameter("swatches", model["swatches"])
			m.set_shader_parameter("gloss_roughness", model["roughness"])
			m.set_shader_parameter("gloss_specular", model["specular"])
		m.set_shader_parameter("dot_colour", DOT_COLOUR)
		m.set_shader_parameter("dot_cos", DOT_COS if colour_key == "cue" else 2.0)
		_materials[colour_key] = m
	var x := float(placement.get("x", 0.0))
	var y := float(placement.get("y", 0.0))
	var ball := MeshInstance3D.new()
	ball.name = _ball_name(placement)
	ball.mesh = _mesh if model.is_empty() else model["meshes"][colour_key]
	ball.material_override = _materials[colour_key]
	# The balls' own layer: out of the table's reflection probe, and lit by the canopy highlight.
	ball.layers = Stage.BALL_LAYER
	ball.set_meta("ball", kind)
	ball.set_meta("index", index)
	# Node positions are single precision; the state dump reports these doubles. The array is
	# updated in place as the ball moves.
	var xy := [x, y]
	ball.set_meta("core_xy", xy)
	ball.position = TableSpace.core_to_godot(x, y, radius)
	var contact := MeshInstance3D.new()
	contact.name = "ContactShadow"
	contact.mesh = _shadow_mesh
	contact.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	contact.layers = Stage.BALL_LAYER
	contact.position = Vector3(0.0, -radius + CONTACT_SHADOW_LIFT, 0.0)
	ball.add_child(contact)
	add_child(ball, true)
	_balls.append(ball)
	_shadows.append(contact)
	_xy.append(xy)


## The colour a kind of ball has on the table ("cue", "red" or a colour), for swatches.
static func ball_colour(kind: String) -> Color:
	var colours: Dictionary = bought(RADIUS_FALLBACK).get("colours", {})
	var key := kind if BALL_COLOURS.has(kind) else "cue"
	return colours.get(key, BALL_COLOURS[key])


## The bought balls for balls of `ball_radius`, loaded once; {} without them (see _bought).
static func bought(ball_radius: float) -> Dictionary:
	if not _bought_looked:
		_bought_looked = true
		_bought = _load_bought()
	if not _bought.is_empty() and absf(float(_bought["radius"]) - ball_radius) > BOUGHT_SIZE_TOLERANCE:
		return {}
	return _bought


static func _load_bought() -> Dictionary:
	if not ResourceLoader.exists(BOUGHT_PATH):
		return {}
	var scene := load(BOUGHT_PATH) as PackedScene
	var root: Node = scene.instantiate() if scene != null else null
	if root == null:
		return _bought_refused("it didn't load as a scene")
	var meshes := {}
	var colours := {}
	var material: BaseMaterial3D
	var size := 0.0
	var image: Image
	for kind: String in BOUGHT_NODES:
		var node := root.find_child(BOUGHT_NODES[kind], true, false) as MeshInstance3D
		var mesh: Mesh = node.mesh if node != null else null
		var surface: BaseMaterial3D = mesh.surface_get_material(0) as BaseMaterial3D if mesh != null and mesh.get_surface_count() == 1 else null
		if surface == null or surface.albedo_texture == null:
			root.free()
			return _bought_refused("node %s is missing, or is not one surface with a colour texture" % BOUGHT_NODES[kind])
		var box := mesh.get_aabb()
		if box.get_center().length() > BOUGHT_SIZE_TOLERANCE or absf(box.size.x - box.size.y) > BOUGHT_SIZE_TOLERANCE \
				or absf(box.size.x - box.size.z) > BOUGHT_SIZE_TOLERANCE or (size > 0.0 and absf(box.size.x - size) > BOUGHT_SIZE_TOLERANCE):
			root.free()
			return _bought_refused("node %s is not a ball centred on its origin, the others' size (bounds %s)" % [BOUGHT_NODES[kind], box])
		size = box.size.x
		meshes[kind] = mesh
		if material == null:
			material = surface
			image = _readable(surface.albedo_texture)
		if image != null:
			var uvs: PackedVector2Array = mesh.surface_get_arrays(0)[Mesh.ARRAY_TEX_UV]
			if not uvs.is_empty():
				var middle := Vector2.ZERO
				for uv in uvs:
					middle += uv
				middle /= uvs.size()
				colours[kind] = image.get_pixel(clampi(int(middle.x * image.get_width()), 0, image.get_width() - 1),
						clampi(int(middle.y * image.get_height()), 0, image.get_height() - 1))
	root.free()
	return {"meshes": meshes, "swatches": material.albedo_texture, "roughness": material.roughness,
			"specular": material.metallic_specular, "radius": size / 2.0, "colours": colours}


## The texture's pixels, or null (logged) when this build can't read them: the swatches then stay
## our own colours.
static func _readable(texture: Texture2D) -> Image:
	var image := texture.get_image()
	if image != null and image.is_compressed() and image.decompress() != OK:
		image = null
	if image == null or image.is_empty():
		Log.warn("app", "app.error", {"kind": "message", "file": texture.resource_path,
				"message": "balls_view: %s can't be read as pixels here; the edit swatches keep our own colours" % texture.resource_path})
		return null
	return image


static func _bought_refused(why: String) -> Dictionary:
	Log.warn("app", "app.error", {"kind": "message", "file": BOUGHT_PATH,
			"message": "balls_view: %s: %s; our own balls are drawn" % [BOUGHT_PATH, why]})
	return {}


static func _ball_name(placement: Dictionary) -> String:
	var kind := String(placement.get("ball", ""))
	return "red_%d" % int(placement.get("index", 0)) if kind == "red" else kind


func _reset_drops() -> void:
	_set_dropping(0)
	var count := _balls.size()
	_drop_elapsed.resize(count)
	_drop_elapsed.fill(-1.0)
	_drop_from.resize(count)
	_drop_to.resize(count)
	_drop_pace.resize(count)
	_velocity.resize(count)
	_velocity.fill(Vector2.ZERO)
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
		_velocity[i] = Vector2(state[o + ShotPlayback.VX], state[o + ShotPlayback.VY])
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
	for i in _balls.size():
		if _drop_elapsed[i] < 0.0:
			continue
		var pace := _drop_pace[i]
		var t := minf(_drop_elapsed[i] + delta, pace.x)
		_drop_elapsed[i] = t
		var f := t / pace.x
		# In over the hole: from the pace the ball came in with to a stop over the drop centre.
		var u := minf(t / pace.y, 1.0)
		var p := _drop_from[i].lerp(_drop_to[i], u * u * (3.0 - 2.0 * u) + pace.z * u * (1.0 - u) * (1.0 - u))
		# A ball is potted with its centre over the hole's lip. Sinking as the cube of the time, it
		# rolls over the lip as it moves in; as the square, it sinks a millimetre into the cloth there.
		p.y = radius - DROP_DEPTH * f * f * f
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
	# A ball stopped dead on the lip of a hole that starts at the pocket's mouth looks caught: it
	# carries on at the pace it had towards the drop centre, and the faster it is the sooner it falls.
	var in_seconds := DROP_SECONDS * DROP_IN_FRACTION
	var across := to - Vector2(x, y)
	var start := 0.0
	if across.length_squared() > 0.0:
		start = maxf(_velocity[i].dot(across), 0.0) * in_seconds / across.length_squared()
	# Past 3 the easing would overshoot the drop centre: the ball moves in sooner instead.
	if start > 3.0:
		in_seconds *= 3.0 / start
		start = 3.0
	_drop_pace[i] = Vector3(maxf(in_seconds / DROP_IN_FRACTION, DROP_MIN_SECONDS), in_seconds, start)
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
	set_process(_dropping > 0)


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
