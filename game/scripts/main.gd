extends Node3D
## The M0 placeholder: a static table with the opening layout.

# Cushion, rail and pocket sizes are visual approximations for M0; the real pocket and
# cushion geometry arrives with its data file in M1 (spec 6.3).
const CUSHION_WIDTH := 0.05
const CUSHION_HEIGHT := 0.037
const RAIL_WIDTH := 0.11
const RAIL_TOP := 0.045
const BODY_BOTTOM := -0.22
const CLOTH_DEPTH := 0.03
# Each pocket is two circular cuts, set back from the cushion noses: a mouth through the
# cushions and rail, about 89 mm (corner) and 104 mm (middle) wide at the noses, and behind
# it the hole itself, which stops at the nose line so the cloth never shows a bite.
const CORNER_MOUTH_RADIUS := 0.05
const CORNER_MOUTH_SETBACK := 0.015
const CORNER_HOLE_RADIUS := 0.04
const CORNER_HOLE_SETBACK := 0.04
const MIDDLE_MOUTH_RADIUS := 0.06
const MIDDLE_MOUTH_SETBACK := 0.03
const MIDDLE_HOLE_RADIUS := 0.055
const MIDDLE_HOLE_SETBACK := 0.06
const POCKET_DEPTH := 0.045
# The mouth cut dips this far into the bed, outside the playing area only, so its floor
# isn't coplanar with the cloth.
const MOUTH_DIP := 0.0005
const CONTACT_SHADOW_SCALE := 1.7
const CONTACT_SHADOW_LIFT := 0.0008
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
	# One split over a range fitted to the table in _frame_camera spends the whole shadow
	# map on the table.
	_light.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	# The Mobile renderer defaults to hard shadows on phones, which alias into a sawtooth.
	RenderingServer.directional_soft_shadow_filter_set_quality(RenderingServer.SHADOW_QUALITY_SOFT_LOW)


func _build_table(dims: Dictionary) -> void:
	var half_length := float(dims["playing_length"]) / 2.0
	var half_width := float(dims["playing_width"]) / 2.0
	var cloth := _material(Color(0.07, 0.34, 0.14), 0.95)
	var cushion_cloth := _material(Color(0.06, 0.3, 0.125), 0.95)
	var wood := _material(Color(0.29, 0.15, 0.07), 0.45)
	wood.clearcoat_enabled = true
	wood.clearcoat = 0.4
	var dark_wood := _material(Color(0.14, 0.07, 0.035), 0.6)
	# The rail overhang shadows the body at a grazing angle, which only ever shows as acne.
	dark_wood.disable_receive_shadows = true
	var pocket := _material(Color(0.006, 0.006, 0.006), 1.0)
	pocket.metallic_specular = 0.0
	var marking := _material(Color(0.82, 0.86, 0.82), 1.0)

	# Bed, cushions and rails are one CSG solid so the pockets cut them cleanly; the faces a
	# cut leaves take the cut's material.
	var top := CSGCombiner3D.new()
	top.name = "TableTop"
	var cloth_half := Vector2(half_length + CUSHION_WIDTH, half_width + CUSHION_WIDTH)
	var bed_bottom := -CLOTH_DEPTH * 2.0
	_add_csg_box(top, "Cloth", Vector2.ZERO, cloth_half * 2.0, bed_bottom, 0.0, cloth)
	# Cushions run into the corners; the pocket mouths shape their ends.
	for side: float in [-1.0, 1.0]:
		_add_csg_box(top, "Cushion", Vector2((half_length + CUSHION_WIDTH / 2.0) * side, 0.0), Vector2(CUSHION_WIDTH, cloth_half.y * 2.0), 0.0, CUSHION_HEIGHT, cushion_cloth)
		_add_csg_box(top, "Cushion", Vector2(0.0, (half_width + CUSHION_WIDTH / 2.0) * side), Vector2(cloth_half.x * 2.0, CUSHION_WIDTH), 0.0, CUSHION_HEIGHT, cushion_cloth)
	var outer_half := cloth_half + Vector2(RAIL_WIDTH, RAIL_WIDTH)
	for side: float in [-1.0, 1.0]:
		_add_csg_box(top, "Rail", Vector2((cloth_half.x + RAIL_WIDTH / 2.0) * side, 0.0), Vector2(RAIL_WIDTH, outer_half.y * 2.0), bed_bottom, RAIL_TOP, wood)
		_add_csg_box(top, "Rail", Vector2(0.0, (cloth_half.y + RAIL_WIDTH / 2.0) * side), Vector2(cloth_half.x * 2.0, RAIL_WIDTH), bed_bottom, RAIL_TOP, wood)
	for sx: float in [-1.0, 1.0]:
		for sy: float in [-1.0, 1.0]:
			var corner := Vector2(half_length, half_width) * Vector2(sx, sy)
			var outwards := Vector2(sx, sy)
			_add_mouth(top, corner + outwards * CORNER_MOUTH_SETBACK, CORNER_MOUTH_RADIUS, Vector2(half_length, half_width), cloth)
			top.add_child(_cut(corner + outwards * CORNER_HOLE_SETBACK, CORNER_HOLE_RADIUS, -POCKET_DEPTH, pocket), true)
		var middle := Vector2(0.0, half_width * sx)
		_add_mouth(top, middle + Vector2(0.0, MIDDLE_MOUTH_SETBACK * sx), MIDDLE_MOUTH_RADIUS, Vector2(half_length, half_width), cloth)
		top.add_child(_cut(middle + Vector2(0.0, MIDDLE_HOLE_SETBACK * sx), MIDDLE_HOLE_RADIUS, -POCKET_DEPTH, pocket), true)
	_table.add_child(top)
	_add_box("Body", Vector2.ZERO, outer_half * 2.0 - Vector2(0.04, 0.04), BODY_BOTTOM, bed_bottom, dark_wood)

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
	var shadow := _contact_shadow_mesh(radius)
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
		var contact := MeshInstance3D.new()
		contact.name = "ContactShadow"
		contact.mesh = shadow
		contact.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		contact.position = Vector3(0.0, -radius + CONTACT_SHADOW_LIFT, 0.0)
		ball.add_child(contact)
		_balls.add_child(ball)


## A soft dark blob on the cloth under a ball. The directional shadow alone is too faint
## and too small at phone resolution to read as the ball resting on the cloth.
func _contact_shadow_mesh(radius: float) -> PlaneMesh:
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
	var farthest := 0.0
	for corner in _frame_corners:
		farthest = maxf(farthest, (corner - _camera.position).dot(forward))
	_light.directional_shadow_max_distance = farthest + 0.1


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


func _add_csg_box(parent: CSGCombiner3D, node_name: String, centre: Vector2, size: Vector2, bottom: float, top: float, material: Material) -> void:
	var box := CSGBox3D.new()
	box.name = node_name
	box.size = Vector3(size.y, top - bottom, size.x)
	box.material = material
	box.position = TableSpace.core_to_godot(centre.x, centre.y, (top + bottom) / 2.0)
	parent.add_child(box, true)


## A vertical circular cut from above the rails down to `bottom`.
func _cut(centre: Vector2, radius: float, bottom: float, material: Material) -> CSGCylinder3D:
	var top := RAIL_TOP + 0.01
	var cut := CSGCylinder3D.new()
	cut.name = "Pocket"
	cut.operation = CSGShape3D.OPERATION_SUBTRACTION
	cut.radius = radius
	cut.height = top - bottom
	cut.sides = 48
	cut.material = material
	cut.position = TableSpace.core_to_godot(centre.x, centre.y, (top + bottom) / 2.0)
	return cut


## A pocket mouth: a circular cut through cushions and rail that leaves the playing area,
## inside the cushion noses, untouched.
func _add_mouth(parent: CSGCombiner3D, centre: Vector2, radius: float, playing_half: Vector2, material: Material) -> void:
	var mouth := CSGCombiner3D.new()
	mouth.name = "PocketMouth"
	mouth.operation = CSGShape3D.OPERATION_SUBTRACTION
	var cut := _cut(centre, radius, -MOUTH_DIP, material)
	cut.operation = CSGShape3D.OPERATION_UNION
	mouth.add_child(cut)
	var playing := CSGBox3D.new()
	playing.operation = CSGShape3D.OPERATION_SUBTRACTION
	playing.size = Vector3(playing_half.y * 2.0, RAIL_TOP + 0.05, playing_half.x * 2.0)
	playing.material = material
	mouth.add_child(playing)
	parent.add_child(mouth, true)


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
