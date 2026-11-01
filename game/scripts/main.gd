extends Node3D
## The M0 placeholder: a static table with the opening layout. Agent E owns this file.

# Cushion, rail and pocket sizes are visual approximations for M0; the real pocket and
# cushion geometry arrives with its data file in M1 (spec 6.3).
const CUSHION_WIDTH := 0.05
const CUSHION_HEIGHT := 0.037
const CORNER_GAP := 0.07
const MIDDLE_GAP := 0.105
const RAIL_WIDTH := 0.11
const RAIL_TOP := 0.045
const BODY_BOTTOM := -0.22
const CLOTH_DEPTH := 0.03
const MARKING_WIDTH := 0.004
const MARKING_HEIGHT := 0.0004
const OPENING_GAP := 0.0005

const CAMERA_PITCH_DEG := 40.0
const CAMERA_FOV_DEG := 40.0
const FRAME_MARGIN := 0.94

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
const SPOT_COLOURS: Array[String] = ["yellow", "green", "brown", "blue", "pink", "black"]

@onready var _table: Node3D = $Table
@onready var _balls: Node3D = $Balls
@onready var _camera: Camera3D = $Camera3D
@onready var _light: DirectionalLight3D = $Light
@onready var _environment: WorldEnvironment = $WorldEnvironment

var _frame_corners: Array[Vector3] = []


func _ready() -> void:
	_setup_lighting()
	var dims := OfsTable.dimensions()
	var layout := OfsTable.opening_layout(OPENING_GAP)
	if dims.is_empty() or layout.is_empty():
		push_error("main: OfsTable returned no table (dimensions %d keys, layout %d balls)" % [dims.size(), layout.size()])
		return
	_build_table(dims)
	_build_balls(layout, float(dims["ball_radius"]))
	_frame_camera()
	get_viewport().size_changed.connect(_frame_camera)
	Log.info("table", "table.loaded", {
		"balls": _balls.get_child_count(),
		"layout": "opening",
		"params_hash": Params.current_hash(),
	})


func _setup_lighting() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.035, 0.04, 0.045)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.62, 0.66, 0.7)
	env.ambient_light_energy = 0.35
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	_environment.environment = env

	# Tilted slightly off vertical so the ball shadows read as contact shadows from a
	# light above the table rather than vanishing directly under each ball.
	_light.rotation_degrees = Vector3(-78.0, 12.0, 0.0)
	_light.light_energy = 1.25
	_light.light_angular_distance = 2.0
	_light.shadow_enabled = true
	_light.shadow_blur = 1.5
	_light.directional_shadow_max_distance = 8.0


func _build_table(dims: Dictionary) -> void:
	var half_length := float(dims["playing_length"]) / 2.0
	var half_width := float(dims["playing_width"]) / 2.0
	var cloth := _material(Color(0.07, 0.34, 0.14), 0.95)
	var cushion_cloth := _material(Color(0.06, 0.3, 0.125), 0.95)
	var wood := _material(Color(0.29, 0.15, 0.07), 0.45)
	wood.clearcoat_enabled = true
	wood.clearcoat = 0.4
	var dark_wood := _material(Color(0.14, 0.07, 0.035), 0.6)
	var pocket := _material(Color(0.006, 0.006, 0.006), 1.0)
	pocket.metallic_specular = 0.0
	var marking := _material(Color(0.82, 0.86, 0.82), 1.0)

	var cloth_half := Vector2(half_length + CUSHION_WIDTH, half_width + CUSHION_WIDTH)
	_add_box("Cloth", Vector2.ZERO, cloth_half * 2.0, -CLOTH_DEPTH, 0.0, cloth)

	var cushion_length := half_width * 2.0 - CORNER_GAP * 2.0
	var end_x := half_length + CUSHION_WIDTH / 2.0
	for side: float in [-1.0, 1.0]:
		_add_box("Cushion", Vector2(end_x * side, 0.0), Vector2(CUSHION_WIDTH, cushion_length), 0.0, CUSHION_HEIGHT, cushion_cloth)
	var side_length := half_length - CORNER_GAP - MIDDLE_GAP / 2.0
	var side_centre := MIDDLE_GAP / 2.0 + side_length / 2.0
	var side_y := half_width + CUSHION_WIDTH / 2.0
	for sx: float in [-1.0, 1.0]:
		for sy: float in [-1.0, 1.0]:
			_add_box("Cushion", Vector2(side_centre * sx, side_y * sy), Vector2(side_length, CUSHION_WIDTH), 0.0, CUSHION_HEIGHT, cushion_cloth)

	var outer_half := cloth_half + Vector2(RAIL_WIDTH, RAIL_WIDTH)
	for side: float in [-1.0, 1.0]:
		_add_box("Rail", Vector2((cloth_half.x + RAIL_WIDTH / 2.0) * side, 0.0), Vector2(RAIL_WIDTH, outer_half.y * 2.0), -CLOTH_DEPTH * 2.0, RAIL_TOP, wood)
		_add_box("Rail", Vector2(0.0, (cloth_half.y + RAIL_WIDTH / 2.0) * side), Vector2(cloth_half.x * 2.0, RAIL_WIDTH), -CLOTH_DEPTH * 2.0, RAIL_TOP, wood)
	_add_box("Body", Vector2.ZERO, outer_half * 2.0 - Vector2(0.04, 0.04), BODY_BOTTOM, -CLOTH_DEPTH * 2.0, dark_wood)

	# Pockets are dark discs cut down through rail and cushion ends, which from the
	# playing camera reads as the hole and its leather.
	for sx: float in [-1.0, 1.0]:
		for sy: float in [-1.0, 1.0]:
			_add_disc("Pocket", Vector2((half_length + 0.02) * sx, (half_width + 0.02) * sy), 0.064, -0.01, RAIL_TOP + 0.001, pocket)
		_add_disc("Pocket", Vector2(0.0, (half_width + 0.036) * sx), 0.058, -0.01, RAIL_TOP + 0.001, pocket)

	var baulk_x := float(dims["baulk_line_x"])
	_add_box("BaulkLine", Vector2(baulk_x, 0.0), Vector2(MARKING_WIDTH, half_width * 2.0), 0.0, MARKING_HEIGHT, marking)
	_add_d(baulk_x, float(dims["d_radius"]), marking)
	for colour in SPOT_COLOURS:
		var spot := OfsTable.spot(colour)
		if spot.is_empty():
			push_error("main: OfsTable.spot(%s) is empty" % colour)
			continue
		_add_disc("Spot", Vector2(float(spot["x"]), float(spot["y"])), 0.005, 0.0, MARKING_HEIGHT, marking)

	_frame_corners.clear()
	for sx: float in [-1.0, 1.0]:
		for sy: float in [-1.0, 1.0]:
			for h: float in [RAIL_TOP, BODY_BOTTOM]:
				_frame_corners.append(TableSpace.core_to_godot(outer_half.x * sx, outer_half.y * sy, h))


func _build_balls(layout: Array, radius: float) -> void:
	var mesh := SphereMesh.new()
	mesh.radius = radius
	mesh.height = radius * 2.0
	mesh.radial_segments = 48
	mesh.rings = 24
	var materials := {}
	for kind: String in BALL_COLOURS:
		var m := _material(BALL_COLOURS[kind], 0.18)
		m.clearcoat_enabled = true
		m.clearcoat = 0.6
		m.clearcoat_roughness = 0.08
		materials[kind] = m
	for placement: Dictionary in layout:
		var kind := String(placement["ball"])
		var index := int(placement["index"])
		var ball := MeshInstance3D.new()
		ball.name = "red_%d" % index if kind == "red" else kind
		ball.mesh = mesh
		ball.material_override = materials.get(kind, materials["cue"])
		ball.set_meta("ball", kind)
		ball.set_meta("index", index)
		# Node positions are single precision; the state dump reports the layout's doubles.
		ball.set_meta("core_xy", [float(placement["x"]), float(placement["y"])])
		ball.position = TableSpace.core_to_godot(float(placement["x"]), float(placement["y"]), radius)
		_balls.add_child(ball)


## Places the camera behind the baulk end at a fixed pitch, as close as it can get while
## the whole table stays in view at the current aspect ratio.
func _frame_camera() -> void:
	if _frame_corners.is_empty():
		return
	var size := get_viewport().get_visible_rect().size
	var aspect := size.x / maxf(size.y, 1.0)
	_camera.fov = CAMERA_FOV_DEG
	var pitch := deg_to_rad(CAMERA_PITCH_DEG)
	var forward := Vector3(0.0, -sin(pitch), -cos(pitch))
	var best_distance := INF
	var best_target := Vector3.ZERO
	# The near end projects larger than the far end, so the best target is not the table
	# centre; search along the table's axis for the one that allows the closest camera.
	for step in range(-40, 41):
		var target := Vector3(0.0, 0.0, step * 0.025)
		var distance := _fit_distance(target, forward, aspect)
		if distance < best_distance:
			best_distance = distance
			best_target = target
	_camera.position = best_target - forward * best_distance
	_camera.look_at(best_target, Vector3.UP)


func _fit_distance(target: Vector3, forward: Vector3, aspect: float) -> float:
	var tan_v := tan(deg_to_rad(CAMERA_FOV_DEG) / 2.0) * FRAME_MARGIN
	var tan_h := tan_v * aspect
	var right := Vector3.RIGHT
	var up := right.cross(forward)
	var low := 0.5
	var high := 30.0
	for i in 40:
		var mid := (low + high) / 2.0
		var eye := target - forward * mid
		var fits := true
		for corner in _frame_corners:
			var v := corner - eye
			var depth := v.dot(forward)
			if depth <= 0.05 or absf(v.dot(right) / depth) > tan_h or absf(v.dot(up) / depth) > tan_v:
				fits = false
				break
		if fits:
			high = mid
		else:
			low = mid
	return high


func _material(colour: Color, roughness: float) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = colour
	m.roughness = roughness
	return m


## An axis-aligned box from core-space centre and size (x along the table, y across),
## spanning heights bottom..top.
func _add_box(node_name: String, centre: Vector2, size: Vector2, bottom: float, top: float, material: Material) -> void:
	var mesh := BoxMesh.new()
	mesh.size = Vector3(size.y, top - bottom, size.x)
	var node := MeshInstance3D.new()
	node.name = node_name
	node.mesh = mesh
	node.material_override = material
	node.position = TableSpace.core_to_godot(centre.x, centre.y, (top + bottom) / 2.0)
	_table.add_child(node, true)


func _add_disc(node_name: String, centre: Vector2, radius: float, bottom: float, top: float, material: Material) -> void:
	var mesh := CylinderMesh.new()
	mesh.top_radius = radius
	mesh.bottom_radius = radius
	mesh.height = top - bottom
	mesh.radial_segments = 32
	mesh.rings = 1
	var node := MeshInstance3D.new()
	node.name = node_name
	node.mesh = mesh
	node.material_override = material
	node.position = TableSpace.core_to_godot(centre.x, centre.y, (top + bottom) / 2.0)
	_table.add_child(node, true)


## The D: a thin semicircular band on the baulk side of the baulk line.
func _add_d(baulk_x: float, radius: float, material: StandardMaterial3D) -> void:
	var segments := 64
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	for i in segments + 1:
		var angle := -PI / 2.0 + PI * i / segments
		for r: float in [radius - MARKING_WIDTH / 2.0, radius + MARKING_WIDTH / 2.0]:
			vertices.append(TableSpace.core_to_godot(baulk_x - r * cos(angle), r * sin(angle), MARKING_HEIGHT))
			normals.append(Vector3.UP)
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLE_STRIP, arrays)
	var node := MeshInstance3D.new()
	node.name = "D"
	node.mesh = mesh
	var double_sided: StandardMaterial3D = material.duplicate()
	double_sided.cull_mode = BaseMaterial3D.CULL_DISABLED
	node.material_override = double_sided
	_table.add_child(node)
