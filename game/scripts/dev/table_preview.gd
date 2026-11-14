extends Node3D
## The table and its stage from the cameras that matter, for screenshots: stage, table, the opening
## layout and a red at a corner and a middle pocket for scale. One run saves every view asked for,
## each once the reflections are captured, then quits:
##   python scripts/background.py <godot> --path game --resolution 1434x660 res://scenes/dev/table_preview.tscn -- --mute --no-link --out=<folder> [--tag=<name>] [--views=aim,corner,middle,above]
## Files land as <folder>/<tag>_<view>.png. Views: aim (the game's aim view from baulk), low (the same
## with the camera down at 5 degrees, where the lamp shows), corner and middle (a pocket close up
## from table height), above (the whole table), corner_top and middle_top (a pocket from straight
## above), cueball (the reflections on the balls), room (from across the room: legs, floor, lamp),
## lamp (the lamp from below the height where it starts to fade), under (up into its shades from
## the baulk cushion).
## With OFS_TABLE=procedural in the environment the table is the procedural one (TableView.SOURCE_ENV).
## --time=<view> draws that view uncapped for a few seconds and prints its frame time instead.
## A JSON object in OFS_TABLE_TUNE overrides the stage's numbers for the run, to try values before
## they go into stage.gd and table_view.gd: key, key_specular, ambient, sky, exposure, canopy,
## cloth_specular, cloth_rim, cloth_rim_tint, trim_specular; and racks or table_shadow at 0 to time
## a frame without the model's racks or its shadow.

const VIEWS: Array[String] = ["aim", "low", "corner", "middle", "above", "corner_top", "middle_top", "cueball", "room", "lamp", "under"]
const OPENING_GAP := 0.0005
const BALL_RADIUS := 0.02625
const AIM_FOV := 34.0
const AIM_DISTANCE := 1.55
const AIM_LOOK_AHEAD := 0.5
## Frames drawn in a view before its picture is taken, so its shadows and fades have settled.
const SETTLE_FRAMES := 12
const TIMED_SECONDS := 4.0

var _camera: Camera3D
var _stage: Stage
var _cue_xy := Vector2.ZERO
var _aim := 0.0
var _views: Array[String] = []
var _out := ""
var _tag := "table"
var _timed := ""
var _frames := 0
var _started_usec := 0
var _timed_frames := 0


func _ready() -> void:
	_views.assign(VIEWS.slice(0, 5))
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--views="):
			_views.assign(arg.substr(8).split(",", false))
		elif arg.begins_with("--out="):
			_out = arg.substr(6)
		elif arg.begins_with("--tag="):
			_tag = arg.substr(6)
		elif arg.begins_with("--time="):
			_timed = arg.substr(7)
			_views.assign([_timed])
	for view in _views:
		if not VIEWS.has(view):
			Log.warn("app", "app.error", {"kind": "message",
					"message": "table_preview: unknown view '%s'; known: %s" % [view, ", ".join(VIEWS)]})
	if not _timed.is_empty():
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
		Engine.max_fps = 0

	_stage = Stage.new()
	_stage.name = "Stage"
	add_child(_stage)
	_stage.setup()

	var shape := Physics.table_shape()
	if shape.is_empty():
		shape = TableView.shape_from_json(FileAccess.get_file_as_string(Physics.TABLE_SHAPE_PATH))
	var table := TableView.new()
	table.name = "Table"
	add_child(table)
	table.build(shape, OfsTable.dimensions())
	_stage.set_table_bounds(table.bounds())

	var layout: Array[Dictionary] = []
	layout.assign(OfsTable.opening_layout(OPENING_GAP))
	layout.append({"ball": "red", "index": 90, "x": 1.70, "y": 0.80})
	layout.append({"ball": "red", "index": 91, "x": 0.06, "y": 0.86})
	var balls := BallsView.new()
	balls.name = "Balls"
	add_child(balls)
	balls.build(layout)
	balls.set_pockets(shape.get("pockets", []))
	_cue_xy = Vector2(float(layout[0]["x"]), float(layout[0]["y"]))
	_aim = (Vector2(float(layout[1]["x"]), float(layout[1]["y"])) - _cue_xy).angle()

	_camera = Camera3D.new()
	_camera.near = 0.02
	_camera.far = 40.0
	add_child(_camera)
	_camera.current = true
	_stage.dress_camera(_camera)
	_tune(table)
	_place(_views[0] if not _views.is_empty() else "aim")
	_stage.capture_reflections()
	Perf.set_active(&"table_preview", true)


func _process(_delta: float) -> void:
	# The stage processes only while it is capturing the reflections.
	if _stage.is_processing() or _views.is_empty():
		return
	_frames += 1
	if not _timed.is_empty():
		if _frames == SETTLE_FRAMES:
			_started_usec = Time.get_ticks_usec()
			_timed_frames = 0
		elif _frames > SETTLE_FRAMES:
			_timed_frames += 1
			var elapsed := (Time.get_ticks_usec() - _started_usec) / 1e6
			if elapsed >= TIMED_SECONDS:
				print("table_preview: %s %.3f ms a frame (%d frames in %.2f s)" % [_timed, 1000.0 * elapsed / _timed_frames, _timed_frames, elapsed])
				get_tree().quit()
		return
	if _frames < SETTLE_FRAMES:
		return
	var view: String = _views.pop_front()
	if not _out.is_empty():
		var path := _out.path_join("%s_%s.png" % [_tag, view])
		var error := get_viewport().get_texture().get_image().save_png(path)
		if error != OK:
			Log.error("app", "app.error", {"kind": "message", "file": path,
					"message": "table_preview: %s wasn't saved: %s" % [path, error_string(error)]})
	if _views.is_empty():
		get_tree().quit()
		return
	_frames = 0
	_place(_views[0])


func _tune(table: TableView) -> void:
	var text := OS.get_environment("OFS_TABLE_TUNE")
	if text.is_empty():
		return
	var parsed: Variant = JSON.parse_string(text)
	if not parsed is Dictionary:
		Log.warn("app", "app.error", {"kind": "message", "message": "table_preview: OFS_TABLE_TUNE isn't a JSON object: %s" % text})
		return
	var light := _stage.get_node("Light") as DirectionalLight3D
	# The camera draws with the stage's own copy of the environment (Stage.dress_camera).
	var environments: Array[Environment] = [(_stage.get_node("WorldEnvironment") as WorldEnvironment).environment, _camera.environment]
	var canopy := (_stage.get_node("Canopy") as MeshInstance3D).material_override as StandardMaterial3D
	var model := table.get_node_or_null("Model/Table") as MeshInstance3D
	var cloth: ShaderMaterial = model.get_surface_override_material(0) as ShaderMaterial if model != null else null
	for key: String in parsed:
		var value := float(parsed[key])
		match key:
			"key":
				light.light_energy = value
			"key_specular":
				light.light_specular = value
			"ambient":
				for environment in environments:
					environment.ambient_light_energy = value
			"sky":
				(environments[0].sky.sky_material as PanoramaSkyMaterial).energy_multiplier = value
			"exposure":
				for environment in environments:
					environment.tonemap_exposure = value
			"canopy":
				canopy.emission_energy_multiplier = value
			"cloth_specular", "cloth_rim", "cloth_rim_tint", "trim_specular":
				if cloth != null:
					cloth.set_shader_parameter(key, value)
			"racks", "table_shadow":
				# 0 hides the racks, or stops the table casting shadows: what each costs a frame.
				var part := table.get_node_or_null("Model/Racks" if key == "racks" else "Model/Table") as MeshInstance3D
				if part != null and key == "racks":
					part.visible = value > 0.0
				elif part != null:
					part.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if value > 0.0 else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			_:
				Log.warn("app", "app.error", {"kind": "message", "message": "table_preview: OFS_TABLE_TUNE has no '%s'" % key})


func _place(view: String) -> void:
	var ball := TableSpace.core_to_godot(_cue_xy.x, _cue_xy.y, BALL_RADIUS)
	var dir := TableSpace.core_to_godot(cos(_aim), sin(_aim))
	_camera.fov = 40.0
	match view:
		"aim":
			_aim_from(ball, dir, 16.0)
		"low":
			_aim_from(ball, dir, 5.0)
		"corner":
			_look(TableSpace.core_to_godot(1.45, 0.56, 0.14), TableSpace.core_to_godot(1.82, 0.93, 0.0), Vector3.UP)
		"middle":
			_look(TableSpace.core_to_godot(-0.25, 0.55, 0.14), TableSpace.core_to_godot(0.0, 0.96, 0.0), Vector3.UP)
		"above":
			_look(Vector3(0.0, 3.1, 0.0), Vector3.ZERO, Vector3(-1.0, 0.0, 0.0))
		"corner_top":
			_look(TableSpace.core_to_godot(1.76, 0.86, 0.42), TableSpace.core_to_godot(1.80, 0.90, 0.0), Vector3(-1.0, 0.0, 0.0))
		"middle_top":
			_look(TableSpace.core_to_godot(0.0, 0.90, 0.40), TableSpace.core_to_godot(0.0, 0.95, 0.0), Vector3(0.0, 0.0, 1.0))
		"cueball":
			_look(ball + Vector3(0.12, 0.07, 0.17), ball + Vector3(0.0, 0.0, 0.02), Vector3.UP)
		"room":
			_look(TableSpace.core_to_godot(-3.6, -2.4, 0.5), TableSpace.core_to_godot(0.0, 0.0, 0.1), Vector3.UP)
		"lamp":
			_look(TableSpace.core_to_godot(-1.5, -0.9, 0.5), TableSpace.core_to_godot(0.0, 0.0, 0.95), Vector3.UP)
		"under":
			_look(TableSpace.core_to_godot(-1.7, 0.0, 0.25), TableSpace.core_to_godot(0.0, 0.0, 0.8), Vector3.UP)
		_:
			_aim_from(ball, dir, 16.0)
	_stage.fit_shadows(_camera)


# As CameraRig stands behind the cue ball: `pitch_deg` down, looking a little way past the ball.
func _aim_from(ball: Vector3, dir: Vector3, pitch_deg: float) -> void:
	_camera.fov = AIM_FOV
	var target := ball + dir * AIM_LOOK_AHEAD
	var pitch := deg_to_rad(pitch_deg)
	_look(target - dir * AIM_DISTANCE * cos(pitch) + Vector3.UP * AIM_DISTANCE * sin(pitch), target, Vector3.UP)


func _look(from: Vector3, at: Vector3, up: Vector3) -> void:
	_camera.position = from
	_camera.look_at(at, up)
