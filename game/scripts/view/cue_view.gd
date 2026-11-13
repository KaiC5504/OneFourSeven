class_name CueView
extends Node3D
## The cue (spec 7.3: a tapered cylinder, tip 9.5 mm, butt 29 mm, 1450 mm long) behind the cue
## ball along the aim, raised to the elevation, its tip at the chosen offset, pulled back by the
## stroke's backswing.
##
## The node's origin is the tip's face and its -Z the cue's axis towards the ball; X is the
## shot's "right" and Y the "up" square to the cue, the frame the strike's tip offsets use
## (core/src/physics/cue.cpp).

const LENGTH := 1.45
const TIP_DIAMETER := 0.0095
const BUTT_DIAMETER := 0.029
## Between the tip and the ball when the cue is addressing it.
const GAP := 0.003
const BALL_RADIUS_FALLBACK := 0.02625
const SEGMENTS := 24
const TIP_LENGTH := 0.008
const FERRULE_END := 0.026
const BUTT_START := 1.0
const STRIKE_HOLD := 0.25
## The cue dissolves where it comes this close to the camera (a dither, so it stays opaque), so the
## butt never fills the view as a dark wedge when a low or zoomed-in camera sits over it.
const NEAR_HIDDEN_M := 0.05
const NEAR_SOLID_M := 0.15

var _mesh_instance: MeshInstance3D
var _ball_radius := BALL_RADIUS_FALLBACK
var _cue_xy := Vector2.ZERO
var _aim := 0.0
var _elevation := 0.0
var _tip := Vector2.ZERO
var _backswing := 0.0
var _strike: Tween


func _ready() -> void:
	_ensure_mesh()


## cue_xy in core metres; tip in ball radii (x right, y up).
func set_pose(cue_xy: Vector2, aim_rad: float, elevation_rad: float, tip: Vector2, backswing_m: float) -> void:
	_ensure_mesh()
	_stop_strike()
	_cue_xy = cue_xy
	_aim = aim_rad
	_elevation = elevation_rad
	_tip = tip
	_backswing = backswing_m
	transform = pose(cue_xy, aim_rad, elevation_rad, tip, backswing_m, _ball_radius)


## The stroke going through to the ball, then the cue hides for the shot.
func play_strike(speed_m_s: float) -> void:
	_stop_strike()
	var through := clampf(0.02 + 0.008 * speed_m_s, 0.02, 0.08)
	var travel := _backswing + GAP + through
	var seconds := clampf(travel / maxf(speed_m_s, 0.4), 0.04, 0.35)
	Perf.set_active(&"cue_strike", true)
	_strike = create_tween()
	_strike.tween_method(_set_pull, _backswing, -GAP - through, seconds).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_IN)
	_strike.tween_interval(STRIKE_HOLD)
	_strike.tween_callback(_end_strike)


func show_cue(on: bool) -> void:
	if on:
		_stop_strike()
	visible = on


## Where the tip's face is for a pose, in the parent's space (the table's, Godot axes).
static func pose(cue_xy: Vector2, aim_rad: float, elevation_rad: float, tip: Vector2, backswing_m: float,
		ball_radius: float = BALL_RADIUS_FALLBACK) -> Transform3D:
	var ca := cos(aim_rad)
	var sa := sin(aim_rad)
	var ce := cos(elevation_rad)
	var se := sin(elevation_rad)
	var forward := _core_vector(ca * ce, sa * ce, -se)
	var up := _core_vector(ca * se, sa * se, ce)
	var right := _core_vector(sa, -ca, 0.0)
	var depth := sqrt(maxf(0.0, 1.0 - tip.x * tip.x - tip.y * tip.y))
	var centre := TableSpace.core_to_godot(cue_xy.x, cue_xy.y, ball_radius)
	var contact := centre + (right * tip.x + up * tip.y - forward * depth) * ball_radius
	return Transform3D(Basis(right, up, -forward), contact - forward * (GAP + backswing_m))


func tip_position() -> Vector3:
	return global_position


## Along the cue towards the ball, in world space.
func axis() -> Vector3:
	return -global_transform.basis.z


static func _core_vector(x: float, y: float, z: float) -> Vector3:
	return Vector3(-y, z, -x)


func _set_pull(pull: float) -> void:
	transform = pose(_cue_xy, _aim, _elevation, _tip, pull, _ball_radius)


func _end_strike() -> void:
	visible = false
	_strike = null
	Perf.set_active(&"cue_strike", false)


func _stop_strike() -> void:
	if _strike != null:
		_strike.kill()
		_strike = null
		Perf.set_active(&"cue_strike", false)


func _ensure_mesh() -> void:
	if _mesh_instance != null:
		return
	_ball_radius = float(OfsTable.dimensions().get("ball_radius", BALL_RADIUS_FALLBACK))
	var mesh := ArrayMesh.new()
	var dome := PackedVector2Array([Vector2(0.0, 0.0), Vector2(0.0004, 0.0022), Vector2(0.0012, 0.0036),
			Vector2(0.0025, 0.0045), Vector2(0.004, _radius_at(0.004)), Vector2(TIP_LENGTH, _radius_at(TIP_LENGTH))])
	_lathe(mesh, dome, _material(Color(0.2, 0.36, 0.62), 0.95, 0.0))
	# Mostly diffuse: the cue is out of the table's probe, and a full metal shows only the dim
	# studio it reflects, nearly black.
	_lathe(mesh, _taper(TIP_LENGTH, FERRULE_END, 2), _material(Color(0.8, 0.62, 0.32), 0.4, 0.3))
	_lathe(mesh, _taper(FERRULE_END, BUTT_START, 12), _wood_material("ash", 0.7, 0.6, 0.2))
	var butt := _taper(BUTT_START, LENGTH - 0.004, 8)
	var end_radius := _radius_at(LENGTH)
	butt.append_array(PackedVector2Array([Vector2(LENGTH - 0.0015, end_radius * 0.92),
			Vector2(LENGTH, end_radius * 0.6), Vector2(LENGTH, 0.0)]))
	_lathe(mesh, butt, _wood_material("ebony", 0.6, 1.0, 0.08))
	_mesh_instance = MeshInstance3D.new()
	_mesh_instance.name = "Mesh"
	# Out of the table's reflection probe, which would otherwise keep the cue where it was captured.
	_mesh_instance.layers = Stage.MOVING_LAYER
	_mesh_instance.mesh = mesh
	add_child(_mesh_instance)


static func _radius_at(from_tip: float) -> float:
	var f := clampf(from_tip / LENGTH, 0.0, 1.0)
	return 0.5 * (TIP_DIAMETER + (BUTT_DIAMETER - TIP_DIAMETER) * f)


static func _taper(from: float, to: float, steps: int) -> PackedVector2Array:
	var out := PackedVector2Array()
	for i in steps + 1:
		var z := lerpf(from, to, float(i) / steps)
		out.append(Vector2(z, _radius_at(z)))
	return out


## A surface of revolution about +Z from (z, radius) points running away from the tip. UVs are in
## metres, U along the cue and V round it, so the wood's grain runs along the cue.
static func _lathe(mesh: ArrayMesh, profile: PackedVector2Array, material: Material) -> void:
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	var indices := PackedInt32Array()
	for j in profile.size():
		var t := profile[mini(j + 1, profile.size() - 1)] - profile[maxi(j - 1, 0)]
		var n := Vector2(t.x, -t.y).normalized()
		for k in SEGMENTS + 1:
			var angle := TAU * k / SEGMENTS
			var radial := Vector3(cos(angle), sin(angle), 0.0)
			verts.append(radial * profile[j].y + Vector3(0.0, 0.0, profile[j].x))
			normals.append((radial * n.x + Vector3(0.0, 0.0, n.y)).normalized())
			uvs.append(Vector2(profile[j].x, angle * profile[j].y))
	var cols := SEGMENTS + 1
	for j in profile.size() - 1:
		for k in SEGMENTS:
			var i := j * cols + k
			# Clockwise seen from outside, Godot's front.
			indices.append_array(PackedInt32Array([i, i + cols, i + 1, i + 1, i + cols, i + cols + 1]))
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_INDEX] = indices
	# Tangents from the UVs, for the normal maps.
	var st := SurfaceTool.new()
	st.create_from_arrays(arrays)
	st.generate_tangents()
	st.commit(mesh)
	mesh.surface_set_material(mesh.get_surface_count() - 1, material)


static func _material(colour: Color, roughness: float, metallic: float, clearcoat: float = 0.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = colour
	m.roughness = roughness
	m.metallic = metallic
	m.distance_fade_mode = BaseMaterial3D.DISTANCE_FADE_PIXEL_DITHER
	m.distance_fade_min_distance = NEAR_HIDDEN_M
	m.distance_fade_max_distance = NEAR_SOLID_M
	if clearcoat > 0.0:
		m.clearcoat_enabled = true
		m.clearcoat = clearcoat
		m.clearcoat_roughness = 0.1
	return m


## The cue's woods (game/textures/README.md): ash for the shaft, ebony for the butt, 1 m a tile.
static func _wood_material(wood: String, roughness: float, clearcoat: float, clearcoat_roughness: float) -> StandardMaterial3D:
	var m := _material(Color.WHITE, roughness, 0.0, clearcoat)
	m.clearcoat_roughness = clearcoat_roughness
	m.albedo_texture = _texture("%s_albedo.jpg" % wood)
	var normal := _texture("%s_normal.jpg" % wood)
	if normal != null:
		m.normal_enabled = true
		m.normal_texture = normal
		m.normal_scale = 0.3
	m.roughness_texture = _texture("%s_roughness.jpg" % wood)
	m.roughness_texture_channel = BaseMaterial3D.TEXTURE_CHANNEL_RED
	m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	return m


static func _texture(file: String) -> Texture2D:
	var path := "res://textures/cue/" + file
	var texture := load(path) as Texture2D
	if texture == null:
		Log.error("app", "app.error", {"kind": "message",
				"message": "cue_view: %s didn't load; the cue is drawn without it" % path, "file": path})
	return texture
