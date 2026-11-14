extends Node3D
## The pockets up close, for screenshots: stage, table, the opening layout and a red at a corner
## and a middle pocket for scale. User argument --view=top|corner_top|corner_low|middle_top|
## middle_low (default top).

const VIEWS: Array[String] = ["top", "corner_top", "corner_low", "middle_top", "middle_low"]
const OPENING_GAP := 0.0005
const BALL_RADIUS := 0.02625

var _camera: Camera3D


func _ready() -> void:
	var view := "top"
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--view="):
			view = arg.substr(7)
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
	var balls := BallsView.new()
	balls.name = "Balls"
	add_child(balls)
	balls.build(layout)
	balls.set_pockets(shape.get("pockets", []))

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
	stage.dress_camera(_camera)
	stage.fit_shadows(_camera)
	stage.capture_reflections()
	Perf.set_active(&"pockets_preview", true)


func _look(from: Vector3, at: Vector3, up: Vector3) -> void:
	_camera.position = from
	_camera.look_at(at, up)
