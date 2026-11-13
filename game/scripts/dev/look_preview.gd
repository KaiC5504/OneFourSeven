extends Node3D
## The look on its own, for screenshots: stage, table, the opening layout and the cue addressing the
## reds. User arguments: --view=behind|high|overhead|pocket|baulk|middle|cueball (default behind),
## and --uncapped to switch off vsync and the frame cap for a frame-time measurement.

const VIEWS: Array[String] = ["behind", "high", "overhead", "pocket", "baulk", "middle", "cueball"]
const OPENING_GAP := 0.0005
const ELEVATION_RAD := 0.12
const TIP := Vector2(0.15, -0.25)
const BACKSWING_M := 0.06

var _camera: Camera3D
var _stage: Stage


func _ready() -> void:
	var view := "behind"
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--view="):
			view = arg.substr(7)
		elif arg == "--uncapped":
			DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
			Engine.max_fps = 0
	if not VIEWS.has(view):
		Log.warn("app", "app.error", {"kind": "message",
				"message": "look_preview: unknown view '%s'; known: %s" % [view, ", ".join(VIEWS)]})
		view = "behind"

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
	var balls := BallsView.new()
	balls.name = "Balls"
	add_child(balls)
	balls.build(layout)
	balls.set_pockets(shape.get("pockets", []))
	_turn_cue_ball(balls, layout)

	var cue_xy := Vector2(float(layout[0]["x"]), float(layout[0]["y"]))
	var apex := Vector2(float(layout[1]["x"]), float(layout[1]["y"]))
	var aim := (apex - cue_xy).angle()
	var cue := CueView.new()
	cue.name = "Cue"
	add_child(cue)
	cue.set_pose(cue_xy, aim, ELEVATION_RAD, TIP, BACKSWING_M)

	_camera = Camera3D.new()
	_camera.fov = 40.0
	_camera.near = 0.02
	add_child(_camera)
	_camera.current = true
	_place_camera(view, cue_xy, aim)
	_stage.dress_camera(_camera)
	_stage.fit_shadows(_camera)
	_stage.capture_reflections()
	Perf.set_active(&"look_preview", true)


func _place_camera(view: String, cue_xy: Vector2, aim: float) -> void:
	var ball := TableSpace.core_to_godot(cue_xy.x, cue_xy.y, 0.02625)
	var dir := TableSpace.core_to_godot(cos(aim), sin(aim))
	match view:
		"behind":
			_look(ball - dir * 0.75 + Vector3.UP * 0.2, ball + dir * 0.9, Vector3.UP)
		"high":
			_look(Vector3(0.0, 3.0, 4.3), Vector3(0.0, 0.0, -0.25), Vector3.UP)
		"overhead":
			_look(Vector3(0.0, 3.5, 0.0), Vector3.ZERO, Vector3(-1.0, 0.0, 0.0))
		"pocket":
			_look(TableSpace.core_to_godot(1.4, 0.55, 0.32), TableSpace.core_to_godot(1.8, 0.9, 0.0), Vector3.UP)
		"baulk":
			_look(TableSpace.core_to_godot(-1.2, 0.35, 0.45), TableSpace.core_to_godot(-1.8, 0.9, 0.0), Vector3.UP)
		"middle":
			_look(TableSpace.core_to_godot(-0.3, 0.5, 0.28), TableSpace.core_to_godot(0.0, 0.93, 0.0), Vector3.UP)
		"cueball":
			_look(ball + Vector3(0.12, 0.07, 0.17), ball + Vector3(0.0, 0.0, 0.02), Vector3.UP)


func _look(from: Vector3, at: Vector3, up: Vector3) -> void:
	_camera.position = from
	_camera.look_at(at, up)


# A turned cue ball shows that the dots ride with its spin rather than sitting square to the view.
func _turn_cue_ball(balls: BallsView, layout: Array[Dictionary]) -> void:
	var state := PackedFloat64Array()
	state.resize(layout.size() * ShotPlayback.STRIDE)
	for i in layout.size():
		var o := i * ShotPlayback.STRIDE
		state[o + ShotPlayback.X] = float(layout[i]["x"])
		state[o + ShotPlayback.Y] = float(layout[i]["y"])
		state[o + ShotPlayback.POCKET] = -1.0
	state[ShotPlayback.WX] = 0.35
	state[ShotPlayback.WY] = 0.5
	state[ShotPlayback.WZ] = 0.25
	balls.apply_state(state, 1.0)
