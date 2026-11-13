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
	"red_0": Vector2(0.89225 + 2.0 * RADIUS, 0.0),
}
# The physics rack leaves seeded micro-gaps of up to 0.1 mm between the reds (spec 7.7), which add
# up across the pack.
const RED_TOLERANCE := 1e-3
# iPhone 17 Pro Max landscape: 2868x1320 px, 62 pt (186 px) side insets either way round, 21 pt
# (63 px) home indicator; the canvas is 1912 x 880.
const PHONE_SCREEN := Rect2(0, 0, 2868, 1320)
const PHONE_CANVAS := Vector2(1912, 880)
const PHONE_SAFE_AREAS: Array[Rect2] = [
	Rect2(186, 0, 2868 - 372, 1320 - 63),
	Rect2(186, 0, 2868 - 186, 1320 - 63),
	Rect2(0, 0, 2868 - 186, 1320 - 63),
]


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
		var xy: Array = ball.get_meta("core_xy")
		var want: Vector2 = EXPECTED[ball_name]
		var tolerance := RED_TOLERANCE if ball_name.begins_with("red") else 1e-6
		check_near(xy[0], want.x, tolerance, "%s core x" % ball_name)
		check_near(xy[1], want.y, tolerance, "%s core y" % ball_name)
		var core := TableSpace.godot_to_core(ball.position)
		check_near(core.x, xy[0], 1e-5, "%s node x follows core_xy" % ball_name)
		check_near(core.y, xy[1], 1e-5, "%s node y follows core_xy" % ball_name)
		check_near(ball.position.y, RADIUS, 1e-6, "%s rests on the cloth" % ball_name)
	main.free()


func test_balls_are_the_free_tables_full_rack() -> void:
	var main: Node = await _spawn()
	var table: FreeTable = main.get_node("FreeTable")
	var balls := main.get_node("Balls").get_children()
	check_eq(table.balls.size(), balls.size(), "a node per ball in the list")
	for i in mini(balls.size(), table.balls.size()):
		var xy: Array = balls[i].get_meta("core_xy")
		check_eq([xy[0], xy[1]], [table.balls[i]["x"], table.balls[i]["y"]], "node %d at its list entry's doubles" % i)
	check_eq(table.history_size(), 1, "the rack is the first history state")
	main.free()


func test_camera_looks_from_behind_the_cue_ball_towards_black() -> void:
	var main: Node = await _spawn()
	var camera: Camera3D = main.get_node("CameraRig/Camera3D")
	var black := main.get_node("Balls/black") as Node3D
	var cue := main.get_node("Balls/cue") as Node3D
	check(camera.global_position.z > cue.global_position.z, "camera behind the cue ball")
	check(camera.global_position.y > 0.2, "camera above the table")
	check(not camera.is_position_behind(black.global_position), "black in front of the camera")
	check(not camera.is_position_behind(cue.global_position), "cue ball in front of the camera")
	var cue_view: Node3D = main.get_node("Cue")
	check(cue_view.visible, "the cue addresses the cue ball")
	check(cue_view.global_position.distance_to(cue.global_position) < 0.05, "cue tip at the cue ball")
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


func test_table_is_drawn_from_the_physics_outline() -> void:
	var main: Node = await _spawn()
	check(main.get_node_or_null("Table/Mesh") is MeshInstance3D, "the table mesh")
	var shape := Physics.table_shape()
	var pockets := 0
	for child in main.get_node("Table").get_children():
		if child.name.begins_with("Pocket_"):
			pockets += 1
	check_eq(pockets, 6, "a marker per pocket")
	check_eq(pockets, (shape.get("pockets", []) as Array).size(), "the outline's pockets")
	check(main.get_node_or_null("Table/TableTop") == null, "no M0 CSG table left")
	main.free()


func test_the_stage_lights_and_layers_as_the_textures_readme_says() -> void:
	var main: Node = await _spawn()
	var camera: Camera3D = main.get_node("CameraRig/Camera3D")
	check(camera.environment != null and camera.environment.background_mode == Environment.BG_COLOR,
			"the camera sees the plain backdrop")
	check_eq(camera.cull_mask & Stage.PROBE_ONLY_LAYER, 0, "and not the canopy")
	var probe: ReflectionProbe = main.get_node("Stage/Reflections")
	check_eq(probe.cull_mask, Stage.TABLE_LAYER | Stage.PROBE_ONLY_LAYER, "the probe captures the table and canopy only")
	var highlight: AreaLight3D = main.get_node("Stage/BallHighlight")
	check_eq(highlight.light_cull_mask, Stage.BALL_LAYER, "the canopy highlight lights the balls only")
	for ball: Node in main.get_node("Balls").get_children():
		check_eq((ball as VisualInstance3D).layers, Stage.BALL_LAYER, "%s on the balls' layer" % ball.name)
		check_eq((ball.get_node("ContactShadow") as VisualInstance3D).layers, Stage.BALL_LAYER, "and its shadow")
	check_eq((main.get_node("Cue/Mesh") as VisualInstance3D).layers, Stage.MOVING_LAYER, "the cue out of the probe")
	for guide: Node in main.get_node("Guides").get_children():
		check_eq((guide as VisualInstance3D).layers, Stage.MOVING_LAYER, "guide %s out of the probe" % guide.name)
	var table: ArrayMesh = (main.get_node("Table/Mesh") as MeshInstance3D).mesh
	for surface: int in [TableView.SURFACE_CLOTH, TableView.SURFACE_WOOD, TableView.SURFACE_LEATHER]:
		var material := table.surface_get_material(surface) as StandardMaterial3D
		check(material.albedo_texture != null and material.normal_texture != null and material.roughness_texture != null,
				"surface %d textured" % surface)
		check(table.surface_get_format(surface) & Mesh.ARRAY_FORMAT_TANGENT != 0, "surface %d has tangents for its normal map" % surface)
	main.free()


func test_shadow_range_covers_the_whole_table() -> void:
	var main: Node = await _spawn()
	var camera: Camera3D = main.get_node("CameraRig/Camera3D")
	var light: DirectionalLight3D = main.get_node("Stage/Light")
	var bounds: AABB = (main.get_node("Table") as TableView).bounds()
	check(bounds.has_volume(), "the table reports its bounds")
	var forward := -camera.global_transform.basis.z
	for i in 8:
		var corner := bounds.get_endpoint(i)
		var depth := (corner - camera.global_position).dot(forward)
		check(light.directional_shadow_max_distance >= minf(depth, Stage.MAX_SHADOW_DISTANCE),
				"corner %s in shadow range" % corner)
	main.free()


func test_controls_follow_the_layout() -> void:
	var main: Node = await _spawn()
	var hud: Node = main.get_node("Hud")
	var canvas: Vector2 = main.get_viewport().get_visible_rect().size
	var window := main.get_window()
	var safe: Rect2 = HudScript.safe_rect(Rect2(DisplayServer.get_display_safe_area()),
			Rect2(Vector2(window.position), Vector2(window.size)), canvas)
	var bar: FreeTableBar = hud.get_node("FreeTableBar")
	var rects: Dictionary = HudScript.layout(safe, bar.size)
	check_eq(bar.size, (bar.get_child(0) as Control).get_combined_minimum_size(), "the bar's size is what it draws")
	check_eq(Rect2(bar.position, bar.size), rects["bar"], "the bar places itself where the layout expects")
	for pair: Array in [["FineTuneWheel", "wheel"], ["StrokeArea", "stroke"], ["PowerBar", "power"],
			["ElevationControl", "elevation"]]:
		var control: Control = hud.get_node(pair[0])
		check_eq(control.get_rect(), rects[pair[1]], pair[0])
	var spin: SpinWidget = hud.get_node("SpinWidget")
	var corner: Vector2 = rects["spin_corner"]
	check_eq(spin.get_rect(), Rect2(corner - Vector2(0.0, SpinWidget.COLLAPSED_SIZE), Vector2.ONE * SpinWidget.COLLAPSED_SIZE),
			"spin widget collapsed in its corner")
	spin.expand()
	await tree.process_frame
	check_eq(spin.get_rect().position + Vector2(0.0, spin.size.y), corner, "expanded, it grows up and right from the corner")
	check_eq(spin.size, Vector2(SpinWidget.EXPANDED_SIZE, SpinWidget.EXPANDED_SIZE + SpinWidget.READOUT_HEIGHT), "expanded size")
	check(spin.get_index() > hud.get_node("ElevationControl").get_index(), "expanded, it covers the elevation control")
	check_eq((hud.get_node("FineTuneWheel") as Control).mouse_filter, Control.MOUSE_FILTER_STOP, "the wheel takes its own touches")
	var stroke_mode: bool = Settings.get_value("strike_mode") == "stroke"
	check_eq((hud.get_node("StrokeArea") as Control).visible, stroke_mode, "stroke area in stroke mode")
	check_eq((hud.get_node("PowerBar") as Control).visible, not stroke_mode, "power bar in power-bar mode")
	main.free()


func test_layout_fits_the_phones_safe_area_either_way_round() -> void:
	# The bar measures itself once its theme applies, in the tree.
	var probe := FreeTableBar.new()
	tree.root.add_child(probe)
	var bar_size := probe.size
	probe.free()
	for safe_px in PHONE_SAFE_AREAS:
		var safe: Rect2 = HudScript.safe_rect(safe_px, PHONE_SCREEN, PHONE_CANVAS)
		var rects: Dictionary = HudScript.layout(safe, bar_size)
		var corner: Vector2 = rects["spin_corner"]
		var spin := Rect2(corner - Vector2(0.0, SpinWidget.COLLAPSED_SIZE), Vector2.ONE * SpinWidget.COLLAPSED_SIZE)
		var expanded := Rect2(corner - Vector2(0.0, SpinWidget.EXPANDED_SIZE + SpinWidget.READOUT_HEIGHT),
				Vector2(SpinWidget.EXPANDED_SIZE, SpinWidget.EXPANDED_SIZE + SpinWidget.READOUT_HEIGHT))
		var placed := {"bar": rects["bar"], "wheel": rects["wheel"], "stroke": rects["stroke"],
				"spin": spin, "elevation": rects["elevation"]}
		for key: String in placed:
			check(safe.encloses(placed[key]), "%s %s inside the safe area %s" % [key, placed[key], safe])
		check(safe.encloses(rects["power"]), "power bar inside the safe area")
		check(safe.encloses(expanded), "the expanded spin widget inside the safe area")
		check((rects["stroke"] as Rect2).encloses(rects["power"]), "the power bar where the stroke area is")
		var keys := placed.keys()
		for i in keys.size():
			for j in range(i + 1, keys.size()):
				check(not (placed[keys[i]] as Rect2).intersects(placed[keys[j]]), "%s clear of %s" % [keys[i], keys[j]])
		check(not expanded.intersects(rects["bar"]), "the expanded spin widget clears the bar")
		check(not expanded.intersects(rects["stroke"]), "and the stroke area")
		# The middle of the screen, where the table is, stays clear.
		var middle := Rect2(PHONE_CANVAS.x * 0.3, 0.0, PHONE_CANVAS.x * 0.4, PHONE_CANVAS.y * 0.75)
		for key: String in placed:
			check(not (placed[key] as Rect2).intersects(middle), "%s clear of the table's middle" % key)
		check(safe.end.x - (rects["wheel"] as Rect2).end.x <= HudScript.EDGE, "the wheel at the right edge")
		check((rects["stroke"] as Rect2).size.y > safe.size.y * 0.9, "the stroke area runs the full height")
		check((rects["bottom"] as Rect2).position.x > (rects["bar"] as Rect2).end.x, "the palette's row clears the bar")


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
