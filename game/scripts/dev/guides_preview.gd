extends Control
## Guide levels 0-3 side by side on a plain table, each from behind the cue ball, for screenshots:
##   godot --path game --resolution 2868x1320 res://scenes/dev/guides_preview.tscn -- --mute --screenshot=<path> --quit-after=4
## Level 3 uses Physics.predict once the physics is configured, else a made-up record that shows
## how paths draw.

const TABLE_SHAPE_PATH := "res://data/table_shape.json"
const CLOTH := Color(0.07, 0.34, 0.14)
const CUE_BALL := 0
const OBJECT_BALL := 1
const BALLS: Array[Dictionary] = [
	{"ball": "cue", "index": 0, "x": 0.8, "y": 0.42},
	{"ball": "red", "index": 0, "x": 1.25, "y": 0.55},
	{"ball": "red", "index": 1, "x": 1.05, "y": -0.35},
	{"ball": "red", "index": 2, "x": 1.62, "y": 0.3},
	{"ball": "pink", "index": 0, "x": 0.89225, "y": 0.0},
	{"ball": "blue", "index": 0, "x": 0.0, "y": 0.0},
]
const BALL_COLOURS := {
	"cue": Color(0.93, 0.92, 0.86),
	"red": Color(0.62, 0.03, 0.04),
	"pink": Color(0.96, 0.52, 0.62),
	"blue": Color(0.04, 0.15, 0.58),
}


## A made-up shot record: the cue ball to the ghost ball and off along the tangent line to the
## first cushion, the object ball to its pocket.
class PreviewRecord extends RefCounted:
	var paths: Array[PackedVector2Array] = []

	func t_end() -> float:
		return 3.0

	func ball_count() -> int:
		return paths.size()

	func path(ball: int, _max_step_s: float) -> PackedVector2Array:
		return paths[ball] if ball >= 0 and ball < paths.size() else PackedVector2Array()

	func final_balls() -> Array:
		return []


## table_shape.json as OfsPhysics.table_shape() returns it: arcs name their pocket by index.
static func shape_from_json(text: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(text)
	if not parsed is Dictionary:
		return {}
	var shape: Dictionary = parsed
	var pocket_index := {}
	for pocket: Dictionary in shape.get("pockets", []):
		pocket_index[pocket["name"]] = pocket_index.size()
	for arc: Dictionary in shape.get("arcs", []):
		arc["pocket"] = pocket_index.get(arc["pocket"], -1)
	return shape


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var shape: Dictionary = Physics.table_shape()
	if shape.is_empty():
		shape = shape_from_json(FileAccess.get_file_as_string(TABLE_SHAPE_PATH))
	var radius := float(OfsTable.dimensions().get("ball_radius", 0.02625))
	var setup := ShotSetup.new()
	setup.set_aim(_aim_at_pocket(shape, radius))
	setup.last_stroke_speed_m_s = 2.4
	var grid := GridContainer.new()
	grid.columns = 2
	grid.set_anchors_preset(Control.PRESET_FULL_RECT)
	grid.add_theme_constant_override("h_separation", 4)
	grid.add_theme_constant_override("v_separation", 4)
	add_child(grid)
	for level in 4:
		grid.add_child(_view(level, shape, radius, setup))


## The aim that pots the object ball in the top left pocket.
func _aim_at_pocket(shape: Dictionary, radius: float) -> float:
	var pocket: Variant = shape["pockets"][0]["drop_centre"]
	var target := Vector2(float(BALLS[OBJECT_BALL]["x"]), float(BALLS[OBJECT_BALL]["y"]))
	var ghost := target - (Vector2(float(pocket[0]), float(pocket[1])) - target).normalized() * 2.0 * radius
	return (ghost - Vector2(float(BALLS[CUE_BALL]["x"]), float(BALLS[CUE_BALL]["y"]))).angle()


func _view(level: int, shape: Dictionary, radius: float, setup: ShotSetup) -> SubViewportContainer:
	var container := SubViewportContainer.new()
	container.stretch = true
	container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	var viewport := SubViewport.new()
	viewport.own_world_3d = true
	viewport.msaa_3d = Viewport.MSAA_2X
	container.add_child(viewport)

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.035, 0.04, 0.045)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.62, 0.66, 0.7)
	env.ambient_light_energy = 0.35
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var world_env := WorldEnvironment.new()
	world_env.environment = env
	viewport.add_child(world_env)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-78.0, 12.0, 0.0)
	light.light_energy = 1.25
	viewport.add_child(light)

	_add_box(viewport, Vector2(3.569 + 0.3, 1.778 + 0.3), -0.002, Color(0.29, 0.15, 0.07))
	_add_box(viewport, Vector2(3.569, 1.778), 0.0, CLOTH)
	for pocket: Dictionary in shape.get("pockets", []):
		var centre: Variant = pocket["drop_centre"]
		var hole := MeshInstance3D.new()
		var disc := CylinderMesh.new()
		disc.top_radius = float(pocket["drop_radius"]) + 0.01
		disc.bottom_radius = disc.top_radius
		disc.height = 0.001
		hole.mesh = disc
		hole.material_override = _material(Color(0.01, 0.01, 0.01))
		hole.position = TableSpace.core_to_godot(float(centre[0]), float(centre[1]), 0.0006)
		viewport.add_child(hole)
	for ball in BALLS:
		var node := MeshInstance3D.new()
		var sphere := SphereMesh.new()
		sphere.radius = radius
		sphere.height = 2.0 * radius
		node.mesh = sphere
		node.material_override = _material(BALL_COLOURS[ball["ball"]])
		node.position = TableSpace.core_to_godot(float(ball["x"]), float(ball["y"]), radius)
		viewport.add_child(node)

	var cue := Vector2(float(BALLS[CUE_BALL]["x"]), float(BALLS[CUE_BALL]["y"]))
	var forward := Vector2.from_angle(setup.aim_rad)
	var camera := Camera3D.new()
	camera.fov = 45.0
	viewport.add_child(camera)
	var eye := cue - forward * 0.55
	var look := cue + forward * 0.6
	camera.look_at_from_position(TableSpace.core_to_godot(eye.x, eye.y, 0.3), TableSpace.core_to_godot(look.x, look.y, 0.0), Vector3.UP)

	var guides := Guides.new()
	guides.table_shape = shape
	guides.ball_radius = radius
	if not Physics.is_ready():
		var record := _made_up_record(shape, radius, setup.aim_rad)
		guides.predictor = func(_balls: Array[Dictionary], _cue_ball: int, _strike: Dictionary) -> Variant: return record
	viewport.add_child(guides)
	guides.show_for(BALLS, CUE_BALL, setup, level)

	var label := Label.new()
	label.text = "level %d" % level
	label.position = Vector2(24, 16)
	label.add_theme_font_size_override("font_size", 40)
	viewport.add_child(label)
	return container


func _made_up_record(shape: Dictionary, radius: float, aim_rad: float) -> PreviewRecord:
	var record := PreviewRecord.new()
	var hit := Guides.first_contact(BALLS, CUE_BALL, aim_rad, shape, radius)
	for ball in BALLS:
		record.paths.append(PackedVector2Array([Vector2(float(ball["x"]), float(ball["y"]))]))
	if hit["kind"] != "ball":
		return record
	var ghost: Vector2 = hit["ghost"]
	var object_dir: Vector2 = hit["object_dir"]
	var aim := Vector2.from_angle(aim_rad)
	var tangent := (aim - object_dir * aim.dot(object_dir)).normalized()
	var alone: Array[Dictionary] = [{"ball": "cue", "index": 0, "x": ghost.x, "y": ghost.y}]
	var rail := Guides.first_contact(alone, 0, tangent.angle(), shape, radius)
	var bounce: Vector2 = rail["ghost"]
	var off := Vector2(tangent.x, -tangent.y) if absf(bounce.y) > 0.8 else Vector2(-tangent.x, tangent.y)
	var cue_path := PackedVector2Array([record.paths[CUE_BALL][0], ghost, bounce, bounce + off * 0.35])
	record.paths[CUE_BALL] = cue_path
	var pocket: Variant = shape["pockets"][0]["drop_centre"]
	record.paths[hit["index"]] = PackedVector2Array([record.paths[hit["index"]][0], Vector2(float(pocket[0]), float(pocket[1]))])
	return record


func _add_box(parent: Node, size: Vector2, top: float, colour: Color) -> void:
	var box := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = Vector3(size.y, 0.02, size.x)
	box.mesh = mesh
	box.material_override = _material(colour)
	box.position = Vector3(0.0, top - 0.01, 0.0)
	parent.add_child(box)


func _material(colour: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = colour
	m.roughness = 0.5
	return m
