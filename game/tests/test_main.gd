extends TestCase

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
	check(not OfsTable.dimensions().is_empty(), "OfsTable.dimensions() returns the table")
	var main: Node = load("res://scenes/main.tscn").instantiate()
	tree.root.add_child(main)
	await tree.process_frame
	return main


func test_scene_has_22_balls() -> void:
	var main: Node = await _spawn()
	var balls := main.get_node("Balls").get_children()
	check_eq(balls.size(), 22, "ball nodes")
	var reds := 0
	for ball in balls:
		if String(ball.get_meta("ball", "")) == "red":
			reds += 1
	check_eq(reds, 15, "reds")
	main.free()


func test_balls_sit_at_documented_core_positions() -> void:
	var main: Node = await _spawn()
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


func test_camera_looks_from_baulk_towards_black() -> void:
	var main: Node = await _spawn()
	var camera: Camera3D = main.get_node("Camera3D")
	check(camera.position.z > 1.7845, "camera behind the baulk cushion")
	check(camera.position.y > 0.5, "camera elevated")
	var black := main.get_node("Balls/black") as Node3D
	var cue := main.get_node("Balls/cue") as Node3D
	check(not camera.is_position_behind(black.global_position), "black in front of the camera")
	check(not camera.is_position_behind(cue.global_position), "cue ball in front of the camera")
	main.free()


func test_every_ball_has_a_contact_shadow_on_the_cloth() -> void:
	var main: Node = await _spawn()
	for ball: Node in main.get_node("Balls").get_children():
		var shadow := ball.get_node_or_null("ContactShadow") as MeshInstance3D
		check(shadow != null, "%s has a contact shadow" % ball.name)
		if shadow == null:
			continue
		var height := shadow.global_position.y
		check(height > 0.0004 and height < 0.002, "%s shadow just above the cloth and markings: %f" % [ball.name, height])
		check_eq(shadow.cast_shadow, GeometryInstance3D.SHADOW_CASTING_SETTING_OFF, "%s shadow casts no shadow" % ball.name)
	main.free()


func test_six_pockets_cut_into_the_table() -> void:
	var main: Node = await _spawn()
	var top := main.get_node_or_null("Table/TableTop") as CSGCombiner3D
	check(top != null, "CSG table top")
	if top != null:
		var holes := 0
		var mouths := 0
		for child in top.get_children():
			if child.name.begins_with("PocketMouth"):
				mouths += 1
			elif child.name.begins_with("Pocket"):
				holes += 1
		check_eq(holes, 6, "pocket holes")
		check_eq(mouths, 6, "pocket mouths")
	main.free()


func test_shadow_range_covers_the_whole_table() -> void:
	var main: Node = await _spawn()
	var camera: Camera3D = main.get_node("Camera3D")
	var light: DirectionalLight3D = main.get_node("Light")
	var forward := -camera.global_transform.basis.z
	for corner: Vector3 in [Vector3(-1.1, 0.0, -2.0), Vector3(1.1, 0.0, -2.0)]:
		check(light.directional_shadow_max_distance > (corner - camera.position).dot(forward), "far corner %s in shadow range" % corner)
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


func test_link_label_is_inside_the_safe_area_in_both_landscape_directions() -> void:
	var main: Node = await _spawn()
	var label: Label = main.get_node("Hud/LinkLabel")
	var screen := Rect2(0, 0, 2868, 1320)
	var canvas := Vector2(1912, 880)
	# The island side always gets the full inset; the other side is tried with and without
	# one, with the island on either side.
	var safe_areas: Array[Rect2] = [
		Rect2(186, 0, 2868 - 372, 1320 - 63),
		Rect2(186, 0, 2868 - 186, 1320 - 63),
		Rect2(0, 0, 2868 - 186, 1320 - 63),
	]
	for safe in safe_areas:
		label.place_in(safe, screen, canvas, 3.0)
		var font_size := label.get_theme_font_size("font_size")
		var text_size := label.get_theme_font("font").get_string_size(label.text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size)
		var px_per_unit := screen.size.y / canvas.y
		var on_screen := Rect2(label.position * px_per_unit, text_size * px_per_unit)
		check(safe.encloses(on_screen), "label %s inside safe area %s" % [on_screen, safe])
	main.free()


func test_link_label_moves_when_the_safe_area_changes() -> void:
	var main: Node = await _spawn()
	var label: Label = main.get_node("Hud/LinkLabel")
	var screen := Rect2(0, 0, 2868, 1320)
	var canvas := Vector2(1912, 880)
	label.place_in(screen, screen, canvas, 3.0)
	var before := label.position
	label.place_in(Rect2(186, 0, 2868 - 372, 1320 - 63), screen, canvas, 3.0)
	check_near(label.position.x - before.x, 124.0, 0.01, "moved right by the inset in canvas units")
	main.free()


func test_link_label_is_at_least_11_points_on_the_phone() -> void:
	# 3 px per point; the 880-unit canvas fills the 1320 px screen height, 1.5 px per unit.
	var size := HudScript.font_size_for(3.0, 1.5)
	check(size * 1.5 / 3.0 >= 11.0, "%d canvas units is %.1f pt" % [size, size * 1.5 / 3.0])
	var scaled_down := HudScript.font_size_for(3.0, 0.5)
	check(scaled_down * 0.5 / 3.0 >= 11.0, "still 11 pt when the canvas is scaled down: %d" % scaled_down)


func test_safe_area_is_clipped_to_the_window() -> void:
	# On Windows the safe area is the whole desktop; a window inside it needs no inset.
	var origin := HudScript.safe_origin(Rect2(0, 0, 2560, 1400), Rect2(300, 200, 956, 440), Vector2(1912, 880))
	check_eq(origin, Vector2.ZERO, "window inside the safe area")
