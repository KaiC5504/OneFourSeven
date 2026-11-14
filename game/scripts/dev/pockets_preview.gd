extends Node3D
## The pockets up close, for screenshots: stage, table, the opening layout and a red at a corner
## and a middle pocket for scale. User argument --view=top|corner_top|corner_low|middle_top|
## middle_low|middle_back|tv (default top); middle_back looks over the rail into the left middle,
## as the reference photo of a middle pocket does; tv is the broadcast camera's long lens, 30
## degrees down from 5 m behind baulk. --sunk adds a red at the end of its drop into the top left
## corner and the left middle, where BallsView hides a potted ball.
##   godot --path game --resolution 1912x880 res://scenes/dev/pockets_preview.tscn -- --view=corner_top --mute --no-link --screenshot=<path> --quit-after=6

const VIEWS: Array[String] = ["top", "corner_top", "corner_low", "middle_top", "middle_low", "middle_back", "tv"]
const OPENING_GAP := 0.0005
const BALL_RADIUS := 0.02625
const TV_DISTANCE := 5.0
const TV_PITCH := deg_to_rad(30.0)
const TV_FOV := 20.0

var _camera: Camera3D


func _ready() -> void:
	var view := "top"
	var sunk := false
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--view="):
			view = arg.substr(7)
		elif arg == "--sunk":
			sunk = true
	if not VIEWS.has(view):
		Log.warn("app", "app.error", {"kind": "message",
				"message": "pockets_preview: unknown view '%s'; known: %s" % [view, ", ".join(VIEWS)]})
		view = "top"

	var stage := Stage.new()
	stage.name = "Stage"
	add_child(stage)
	stage.setup()

	var shape := Physics.table_shape()
	if shape.is_empty():
		shape = TableView.shape_from_json(FileAccess.get_file_as_string(Physics.TABLE_SHAPE_PATH))
	var table := TableView.new()
	table.name = "Table"
	add_child(table)
	table.build(shape, OfsTable.dimensions())
	stage.set_table_bounds(table.bounds())

	var layout: Array[Dictionary] = []
	layout.assign(OfsTable.opening_layout(OPENING_GAP))
	layout.append({"ball": "red", "index": 90, "x": 1.70, "y": 0.80})
	layout.append({"ball": "red", "index": 91, "x": 0.06, "y": 0.86})
	var sunk_at: Array[Vector2] = []
	if sunk:
		for pocket: Dictionary in shape.get("pockets", []):
			if String(pocket["name"]) in ["top_left", "middle_left"]:
				var centre := Vector2(float(pocket["drop_centre"][0]), float(pocket["drop_centre"][1]))
				sunk_at.append(centre)
				layout.append({"ball": "red", "index": 92 + sunk_at.size(), "x": centre.x, "y": centre.y})
	var balls := BallsView.new()
	balls.name = "Balls"
	add_child(balls)
	balls.build(layout)
	balls.set_pockets(shape.get("pockets", []))
	var count := balls.get_child_count()
	for i in sunk_at.size():
		var ball := balls.get_child(count - sunk_at.size() + i) as Node3D
		ball.position = TableSpace.core_to_godot(sunk_at[i].x, sunk_at[i].y, BALL_RADIUS - BallsView.DROP_DEPTH)

	_camera = Camera3D.new()
	_camera.fov = 40.0
	_camera.near = 0.01
	add_child(_camera)
	_camera.current = true
	match view:
		"top":
			_look(Vector3(0.0, 3.1, 0.0), Vector3.ZERO, Vector3(-1.0, 0.0, 0.0))
		"corner_top":
			_look(TableSpace.core_to_godot(1.76, 0.86, 0.42), TableSpace.core_to_godot(1.80, 0.90, 0.0), Vector3(-1.0, 0.0, 0.0))
		"corner_low":
			_look(TableSpace.core_to_godot(1.45, 0.56, 0.14), TableSpace.core_to_godot(1.82, 0.93, 0.0), Vector3.UP)
		"middle_top":
			_look(TableSpace.core_to_godot(0.0, 0.90, 0.40), TableSpace.core_to_godot(0.0, 0.95, 0.0), Vector3(0.0, 0.0, 1.0))
		"middle_low":
			_look(TableSpace.core_to_godot(-0.25, 0.55, 0.14), TableSpace.core_to_godot(0.0, 0.96, 0.0), Vector3.UP)
		"middle_back":
			_look(TableSpace.core_to_godot(0.0, 1.1, 0.19), TableSpace.core_to_godot(0.0, 0.975, 0.0), Vector3.UP)
		"tv":
			_camera.fov = TV_FOV
			_camera.far = 20.0
			_look(TableSpace.core_to_godot(-TV_DISTANCE * cos(TV_PITCH), 0.0, TV_DISTANCE * sin(TV_PITCH)), Vector3.ZERO, Vector3.UP)
	stage.dress_camera(_camera)
	stage.fit_shadows(_camera)
	stage.capture_reflections()
	Perf.set_active(&"pockets_preview", true)


func _look(from: Vector3, at: Vector3, up: Vector3) -> void:
	_camera.position = from
	_camera.look_at(at, up)
