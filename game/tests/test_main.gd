extends TestCase
## The tests marked NEEDS OFSTABLE pass only once OfsTable returns real data (agents B and
## C, M0); until then OfsTable.dimensions() is empty and main builds no table.

const HudScript := preload("res://scripts/hud.gd")
const RADIUS := 0.02625
const BAULK_LINE_X := -1.0475
# Documented positions (spec 6.1/6.2, core/include/ofs/table_geometry.hpp).
const EXPECTED := {
	"cue": Vector2(BAULK_LINE_X, -0.146),
	"yellow": Vector2(BAULK_LINE_X, -0.292),
	"green": Vector2(BAULK_LINE_X, 0.292),
	"brown": Vector2(BAULK_LINE_X, 0.0),
	"blue": Vector2(0.0, 0.0),
	"pink": Vector2(0.89225, 0.0),
	"black": Vector2(1.4605, 0.0),
	"red_0": Vector2(0.89225 + 2.0 * RADIUS + 0.0005, 0.0),
}


func _spawn() -> Node:
	var main: Node = load("res://scenes/main.tscn").instantiate()
	tree.root.add_child(main)
	await tree.process_frame
	return main


func _ofstable_ready() -> bool:
	if OfsTable.dimensions().is_empty():
		failures.append("NEEDS OFSTABLE: OfsTable.dimensions() is empty (agents B/C not landed)")
		return false
	return true


# NEEDS OFSTABLE
func test_scene_has_22_balls() -> void:
	var main: Node = await _spawn()
	if _ofstable_ready():
		var balls := main.get_node("Balls").get_children()
		check_eq(balls.size(), 22, "ball nodes")
		var reds := 0
		for ball in balls:
			if String(ball.get_meta("ball", "")) == "red":
				reds += 1
		check_eq(reds, 15, "reds")
	main.free()


# NEEDS OFSTABLE
func test_balls_sit_at_documented_core_positions() -> void:
	var main: Node = await _spawn()
	if _ofstable_ready():
		for ball_name: String in EXPECTED:
			var ball := main.get_node_or_null("Balls/" + ball_name) as Node3D
			check(ball != null, "ball %s exists" % ball_name)
			if ball == null:
				continue
			var core := TableSpace.godot_to_core(ball.position)
			var want: Vector2 = EXPECTED[ball_name]
			check_near(core.x, want.x, 1e-5, "%s core x" % ball_name)
			check_near(core.y, want.y, 1e-5, "%s core y" % ball_name)
			check_near(ball.position.y, RADIUS, 1e-6, "%s rests on the cloth" % ball_name)
	main.free()


# NEEDS OFSTABLE
func test_camera_looks_from_baulk_towards_black() -> void:
	var main: Node = await _spawn()
	if _ofstable_ready():
		var camera: Camera3D = main.get_node("Camera3D")
		check(camera.position.z > 1.7845, "camera behind the baulk cushion")
		check(camera.position.y > 0.5, "camera elevated")
		var black := main.get_node("Balls/black") as Node3D
		var cue := main.get_node("Balls/cue") as Node3D
		check(not camera.is_position_behind(black.global_position), "black in front of the camera")
		check(not camera.is_position_behind(cue.global_position), "cue ball in front of the camera")
	main.free()


func test_link_label_text() -> void:
	check_eq(HudScript.text_for("off"), "PC link: off", "off")
	check_eq(HudScript.text_for("searching"), "PC link: searching…", "searching")
	check_eq(HudScript.text_for("connected"), "PC link: connected", "connected")
	check_eq(HudScript.text_for("lost"), "PC link: lost", "lost")


func test_link_label_follows_status() -> void:
	var main: Node = await _spawn()
	var label: Label = main.get_node("Hud/LinkLabel")
	check_eq(label.text, HudScript.text_for(Link.get_status()), "initial text")
	Link.status_changed.emit("lost")
	check_eq(label.text, "PC link: lost", "after status_changed")
	main.free()


func test_link_label_clears_the_dynamic_island() -> void:
	# iPhone 17 Pro Max landscape: 2868x1320 px, 62 pt (186 px) side insets either way
	# round, 21 pt (63 px) home indicator; the canvas is 1912 wide.
	var screen := Rect2(0, 0, 2868, 1320)
	var safe := Rect2(186, 0, 2868 - 372, 1320 - 63)
	var canvas := Vector2(1912, 880)
	var origin := HudScript.safe_origin(safe, screen, canvas)
	check_near(origin.x, 124.0, 0.01, "left inset in canvas units")
	check_near(origin.y, 0.0, 0.01, "top inset")


func test_safe_area_is_clipped_to_the_window() -> void:
	# On Windows the safe area is the whole desktop; a window inside it needs no inset.
	var origin := HudScript.safe_origin(Rect2(0, 0, 2560, 1400), Rect2(300, 200, 956, 440), Vector2(1912, 880))
	check_eq(origin, Vector2.ZERO, "window inside the safe area")
