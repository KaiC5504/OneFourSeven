extends Node3D
## Dev preview of the aiming controls, for screenshots and trying them on Windows: a plain green
## table, spheres for balls, the camera rig, gestures, drag aim and the fine-tune wheel. With
## --screenshot=<path> it also saves <path>_low.png, _mid.png, _high.png and _zoom.png, one per
## entry in CAPTURES, before the harness's --quit-after runs out (keep it at 4 s or more).

const OPENING_GAP := 0.0005
const RAIL_WIDTH := 0.16
const RAIL_HEIGHT := 0.045
const AIM_LINE_LENGTH := 2.0
## [name, pitch_deg, zoom_scale]
const CAPTURES: Array[Array] = [
	["low", CameraRig.PITCH_MIN_DEG, 1.0],
	["mid", 40.0, 1.0],
	["high", CameraRig.PITCH_MAX_DEG, 1.0],
	["zoom", CameraRig.DEFAULT_PITCH_DEG, 0.5],
]
const FIRST_CAPTURE_S := 1.4
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

@onready var _rig: CameraRig = $CameraRig
@onready var _gestures: Gestures = $Gestures
@onready var _aim: AimControl = $AimControl
@onready var _wheel: FineTuneWheel = $Hud/FineTuneWheel

var _setup := ShotSetup.new()
var _cue_xy := Vector2.ZERO
var _ball_radius := 0.02625
var _aim_line := MeshInstance3D.new()


func _ready() -> void:
	_build_stage()
	var dims := OfsTable.dimensions()
	var layout := OfsTable.opening_layout(OPENING_GAP)
	if dims.is_empty() or layout.is_empty():
		push_error("aim_preview: OfsTable returned no table")
		return
	_ball_radius = float(dims["ball_radius"])
	_build_table(Vector2(float(dims["playing_length"]), float(dims["playing_width"])) / 2.0)
	_build_balls(layout)
	_build_aim_line()

	_aim.bind(_setup, _gestures, _rig)
	_wheel.bind(_setup)
	_gestures.pinched.connect(func(factor: float, _centre: Vector2) -> void: _rig.pinch(factor))
	_gestures.two_finger_dragged.connect(func(relative: Vector2) -> void: _rig.tilt(relative.y))
	_setup.changed.connect(_on_setup_changed)
	var pink := OfsTable.spot("pink")
	var pink_xy := Vector2(float(pink.get("x", 1.0)), float(pink.get("y", 0.0)))
	_setup.set_aim((pink_xy - _cue_xy).angle() + deg_to_rad(1.5))

	var path := _screenshot_arg()
	if not path.is_empty():
		_capture_all(path)


func _on_setup_changed(_what: StringName) -> void:
	_rig.follow(_cue_xy, _setup.aim_rad)
	_aim_line.position = TableSpace.core_to_godot(_cue_xy.x, _cue_xy.y, 0.0012)
	_aim_line.rotation = Vector3(0.0, TableSpace.core_angle_to_yaw(_setup.aim_rad), 0.0)


func _screenshot_arg() -> String:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--screenshot="):
			return arg.substr("--screenshot=".length())
	return ""


func _capture_all(path: String) -> void:
	Perf.set_active(&"aim_preview", true)
	await get_tree().create_timer(FIRST_CAPTURE_S).timeout
	var base := path.get_basename()
	for capture: Array in CAPTURES:
		_rig.set_pitch_deg(capture[1])
		_rig.set_zoom_scale(capture[2])
		for i in 3:
			await RenderingServer.frame_post_draw
		var image := get_viewport().get_texture().get_image()
		var file := "%s_%s.png" % [base, capture[0]]
		var err := image.save_png(file) if image != null else ERR_CANT_CREATE
		if err != OK:
			push_error("aim_preview: could not save %s: %s" % [file, error_string(err)])
	Perf.set_active(&"aim_preview", false)


func _build_stage() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.035, 0.04, 0.045)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.62, 0.66, 0.7)
	env.ambient_light_energy = 0.4
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-75.0, 15.0, 0.0)
	light.shadow_enabled = true
	add_child(light)


func _build_table(half: Vector2) -> void:
	var cloth := PlaneMesh.new()
	cloth.size = Vector2(half.y, half.x) * 2.0
	_add_mesh(cloth, _material(Color(0.07, 0.34, 0.14)), Vector3.ZERO)
	var wood := _material(Color(0.29, 0.15, 0.07))
	for side: float in [-1.0, 1.0]:
		var end_rail := BoxMesh.new()
		end_rail.size = Vector3((half.y + RAIL_WIDTH) * 2.0, RAIL_HEIGHT, RAIL_WIDTH)
		_add_mesh(end_rail, wood, TableSpace.core_to_godot((half.x + RAIL_WIDTH / 2.0) * side, 0.0, RAIL_HEIGHT / 2.0))
		var side_rail := BoxMesh.new()
		side_rail.size = Vector3(RAIL_WIDTH, RAIL_HEIGHT, half.x * 2.0)
		_add_mesh(side_rail, wood, TableSpace.core_to_godot(0.0, (half.y + RAIL_WIDTH / 2.0) * side, RAIL_HEIGHT / 2.0))


func _build_balls(layout: Array) -> void:
	var mesh := SphereMesh.new()
	mesh.radius = _ball_radius
	mesh.height = _ball_radius * 2.0
	for placement: Dictionary in layout:
		var kind := String(placement["ball"])
		var xy := Vector2(float(placement["x"]), float(placement["y"]))
		if kind == "cue":
			_cue_xy = xy
		_add_mesh(mesh, _material(BALL_COLOURS.get(kind, Color.WHITE)), TableSpace.core_to_godot(xy.x, xy.y, _ball_radius))


func _build_aim_line() -> void:
	var line := BoxMesh.new()
	line.size = Vector3(0.002, 0.001, AIM_LINE_LENGTH)
	var material := _material(Color(1.0, 1.0, 1.0, 0.6))
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	line.material = material
	# Starts at the cue ball and runs along the node's forward (-Z).
	var inner := MeshInstance3D.new()
	inner.mesh = line
	inner.position = Vector3(0.0, 0.0, -AIM_LINE_LENGTH / 2.0)
	_aim_line.add_child(inner)
	add_child(_aim_line)


func _add_mesh(mesh: Mesh, material: Material, at: Vector3) -> void:
	var node := MeshInstance3D.new()
	node.mesh = mesh
	node.material_override = material
	node.position = at
	add_child(node)


func _material(colour: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = colour
	m.roughness = 0.6
	return m
