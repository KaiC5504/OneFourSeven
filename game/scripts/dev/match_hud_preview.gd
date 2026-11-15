extends Control
## Dev preview of the match's HUD pieces (scoreboard, referee's line, choice strip, nomination
## ring, cue placer) over a plain cloth, with the shot controls where they sit in play, laid out
## in the iPhone 17 Pro Max's landscape safe area whatever the window is:
##   python scripts/background.py <godot> --path game --resolution 1912x880 res://scenes/dev/match_hud_preview.tscn -- --state=foul_choice --mute --no-link --screenshot=<abs path> --quit-after=2
## States: play (the default), foul_choice, free_ball, placing, placed, snookers, rerack, toss.
## The table is flat colour seen from a fixed camera, not the game's: only the pieces are judged.

const HudScript := preload("res://scripts/hud.gd")
const FRAME_PATH := "res://data/rules/example_frame_view.json"
const MATCH_PATH := "res://data/rules/example_match.json"
const STATE_ARG := "--state="
const CLOTH := Color(0.05, 0.25, 0.11)
## The phone in landscape: 440 pt tall, safe insets 62 pt left and right (Dynamic Island) and
## 21 pt at the bottom (home indicator).
const PHONE_HEIGHT_PT := 440.0
const INSET_SIDE_PT := 62.0
const INSET_BOTTOM_PT := 21.0
const LINE_GAP := 8.0

var _scoreboard := Scoreboard.new()
var _line := RefereeLine.new()
var _strip := ChoiceStrip.new()
var _placer := CuePlacer.new()
var _nomination := Nomination.new()
var _spin := SpinWidget.new()
var _elevation := ElevationControl.new()
var _stroke := StrokeControl.new()
var _setup := ShotSetup.new()


func _ready() -> void:
	var state := "play"
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with(STATE_ARG):
			state = arg.substr(STATE_ARG.length())
	var frame: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(FRAME_PATH))
	var match_state: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(MATCH_PATH))
	var calls: Array = []
	var options: Array[String] = []
	var ringed := ""
	frame["free_ball"] = false
	frame["pending"] = "none"
	match state:
		"play":
			frame["scores"] = [33, 31]
			frame["break_points"] = 9
			frame["on"] = {"kind": "any_colour"}
			ringed = "blue"
		"foul_choice":
			frame["pending"] = "after_foul"
			calls = [{"kind": "foul", "value": 4, "detail": "no_hit"}, {"kind": "miss", "value": 0, "detail": "S3.14(c)"}]
			options = ["play", "play_again", "replace"]
		"free_ball":
			frame["free_ball"] = true
			frame["pending"] = "after_foul"
			calls = [{"kind": "foul", "value": 4, "detail": "no_hit"}, {"kind": "miss", "value": 0, "detail": "S3.14(c)"},
					{"kind": "free_ball", "value": 0, "detail": ""}]
			options = ["play", "play_again", "replace"]
		"placing", "placed":
			frame["cue_in_hand"] = true
			frame["scores"] = [28, 31]
			_move_ball(frame, "cue", Vector2(-1.2, 0.08))
			if state == "placing":
				calls = [{"kind": "foul", "value": 4, "detail": "in_off"}]
		"snookers":
			frame["scores"] = [12, 58]
			frame["striker"] = 1
			frame["break_points"] = 22
			frame["on"] = {"kind": "colour", "colour": "yellow"}
			frame["balls"] = _without(frame, "red")
			(frame["view"] as Dictionary)["points_remaining"] = 27
			(frame["view"] as Dictionary)["required"] = [20, 0]
		"rerack":
			frame["pending"] = "rerack"
			calls = [{"kind": "rerack_offer", "value": 0, "detail": ""}]
			options = ["accept_rerack", "refuse_rerack"]
		"toss":
			frame["scores"] = [52, 52]
			frame["on"] = {"kind": "colour", "colour": "black"}
			frame["pending"] = "toss"
			frame["cue_in_hand"] = true
			for ball: String in ["red", "yellow", "green", "brown", "blue", "pink"]:
				frame["balls"] = _without(frame, ball)
			_move_ball(frame, "cue", Vector2(-1.2, 0.0))
			(frame["view"] as Dictionary)["points_remaining"] = 7
			calls = [{"kind": "respotted_black", "value": 0, "detail": ""}]
			options = ["play_first", "play_second"]
		_:
			push_error("match_hud_preview: unknown state %s" % state)

	_build_table(frame)
	_nomination.name = "Nomination"
	add_child(_nomination)
	for ball: Dictionary in frame["balls"]:
		if ball["ball"] == ringed:
			_nomination.mark(Vector2(ball["x"], ball["y"]))
	for control: Control in [_spin, _elevation, _stroke]:
		add_child(control)
		control.call("bind", _setup)
	_scoreboard.auto_place = false
	for piece: Control in [_scoreboard, _line, _strip, _placer]:
		add_child(piece)
	_scoreboard.show_frame(frame, match_state, 0)
	_line.announce(calls, 0)
	if not options.is_empty():
		_strip.offer(options, frame)
	if state == "placing" or state == "placed":
		_placer.begin(func(xy: Vector2) -> Vector2: return xy)
	if state == "placed":
		_placer.button("confirm").pressed.emit()
	get_viewport().size_changed.connect(_place)
	_place()


func _place() -> void:
	var canvas := get_viewport_rect().size
	var unit := canvas.y / PHONE_HEIGHT_PT
	var safe := Rect2(INSET_SIDE_PT * unit, 0.0, canvas.x - 2.0 * INSET_SIDE_PT * unit,
			canvas.y - INSET_BOTTOM_PT * unit)
	var rects := HudScript.layout(safe, Vector2.ZERO, canvas, "", false)
	var spin: Rect2 = rects["spin"]
	var cue: Rect2 = rects["cue"]
	_spin.position = spin.position
	_spin.size = spin.size
	_elevation.position = (rects["elevation"] as Rect2).position
	_elevation.size = (rects["elevation"] as Rect2).size
	_stroke.position = cue.position
	_stroke.size = cue.size
	_scoreboard.place_in(safe)
	var under := _scoreboard.covered_rect().end.y + LINE_GAP
	_line.place_in(Rect2(safe.position.x, under, safe.size.x, safe.end.y - under))
	# What the shot controls leave free, kept even about the middle of the screen so the strip
	# stands under the scoreboard: the spin ball reaches further in than the power cue.
	var reach := minf(safe.get_center().x - spin.end.x, cue.position.x - safe.get_center().x) - HudScript.GAP
	var free := Rect2(safe.get_center().x - reach, safe.position.y, 2.0 * reach, cue.end.y - safe.position.y)
	_strip.place_in(free)
	_placer.place_in(free)


# A flat cloth and flat balls: nothing here is lit, so no stage is needed.
func _build_table(frame: Dictionary) -> void:
	var cloth := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(400.0, 400.0)
	plane.material = _flat(CLOTH)
	cloth.mesh = plane
	add_child(cloth)
	var radius := float(OfsTable.dimensions().get("ball_radius", BallsView.RADIUS_FALLBACK))
	for ball: Dictionary in frame["balls"]:
		var sphere := SphereMesh.new()
		sphere.radius = radius
		sphere.height = 2.0 * radius
		sphere.material = _flat(BallsView.BALL_COLOURS.get(ball["ball"], Color.WHITE))
		var node := MeshInstance3D.new()
		node.mesh = sphere
		node.position = TableSpace.core_to_godot(ball["x"], ball["y"], radius)
		add_child(node)
	var camera := Camera3D.new()
	camera.fov = 30.0
	add_child(camera)
	camera.look_at_from_position(TableSpace.core_to_godot(-3.3, 0.0, 1.25), TableSpace.core_to_godot(0.1, 0.0, 0.0))
	camera.current = true


static func _flat(colour: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = colour
	return material


static func _without(frame: Dictionary, kind: String) -> Array:
	return (frame["balls"] as Array).filter(func(ball: Dictionary) -> bool: return ball["ball"] != kind)


static func _move_ball(frame: Dictionary, kind: String, to: Vector2) -> void:
	for ball: Dictionary in frame["balls"]:
		if ball["ball"] == kind:
			ball["x"] = to.x
			ball["y"] = to.y
