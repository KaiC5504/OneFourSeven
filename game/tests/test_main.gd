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


## The scene as the app opens it: at Home.
func _spawn_home() -> Node:
	check(not OfsTable.dimensions().is_empty(), "OfsTable.dimensions() returns the table")
	var main: Node = load("res://scenes/main.tscn").instantiate()
	tree.root.add_child(main)
	await tree.process_frame
	return main


## The free table, entered from Home, with the camera landed behind the cue ball.
func _spawn() -> Node:
	var main: Node = await _spawn_home()
	check(main.call("harness_open_screen", "table"), "Home leads to the free table")
	(main.get_node("CameraRig") as CameraRig)._process(CameraRig.RETURN_S)
	await tree.process_frame
	return main


func test_the_app_opens_at_home_over_the_whole_table() -> void:
	var main: Node = await _spawn_home()
	var hud: Node = main.get_node("Hud")
	var state: Dictionary = main.call("harness_state")
	check_eq(state["screen"], "home", "the screen")
	check_eq(state["mode"], "home", "the mode")
	check((hud.get_node("HomeScreen") as Control).visible, "the Home screen shows")
	for control_name: String in ["FineTuneWheel", "StrokeCue", "PullCue", "ElevationControl", "SpinWidget", "EditPalette",
			"FreeTableBar", "Scoreboard", "MatchSetupScreen", "PauseMenu", "FrameSummary", "MatchSummary", "SettingsPanel",
			"Notice", "BreakPill"]:
		check(not (hud.get_node(control_name) as Control).visible, "%s is not showing" % control_name)
	check(not (hud.get_node("ChoiceStrip") as ChoiceStrip).is_offering(), "no choice is offered")
	check(not (hud.get_node("CuePlacer") as CuePlacer).covered_rect().has_area(), "no cue ball to place")
	check_eq((hud.get_node("RefereeLine") as RefereeLine).text(), "", "the referee says nothing")
	check(not (main.get_node("Cue") as Node3D).visible, "no cue")
	check(not (main.get_node("Guides/AimLine") as Node3D).visible, "no guides")
	check(not (main.get_node("Nomination") as Nomination).is_marking(), "no nomination ring")
	check(not (main.get_node("AimControl") as AimControl).enabled, "no aiming")
	check(not (main.get_node("Gestures") as Gestures).enabled, "the table takes no touches")
	check_eq(main.get_node("Balls").get_child_count(), 22, "the rack is on the table behind it")
	var rig: CameraRig = main.get_node("CameraRig")
	check_eq(state["camera"]["view"], "shot", "the camera is held")
	check(rig.transform.is_equal_approx(rig.broadcast_pose(CameraRig.FULL_TABLE_YAW)), "at the full-table view from the black end")
	# The whole table stands between Home's name and its buttons.
	var title := (hud.get_node("HomeScreen/Title") as Control).get_rect()
	var row := (hud.get_node("HomeScreen/Buttons") as Control).get_rect()
	var bounds: AABB = (main.get_node("Table") as TableView).bounds()
	var camera: Camera3D = main.get_node("CameraRig/Camera3D")
	var canvas: Vector2 = main.get_viewport().get_visible_rect().size
	var top := INF
	var bottom := -INF
	for x: float in [bounds.position.x, bounds.end.x]:
		for z: float in [bounds.position.z, bounds.end.z]:
			var at := camera.unproject_position(Vector3(x, bounds.end.y, z))
			top = minf(top, at.y)
			bottom = maxf(bottom, at.y)
			check(at.x > 0.0 and at.x < canvas.x, "the table's corner %s is on the screen" % at)
	check(top > title.end.y, "the table's far end is under the name: %s under %s" % [top, title.end.y])
	check(bottom < row.position.y, "its near end is above the buttons: %s above %s" % [bottom, row.position.y])
	check(bottom - top > (row.position.y - title.end.y) * 0.6, "and it fills most of the room between them: %s of %s"
			% [bottom - top, row.position.y - title.end.y])
	# The fit is worked out once, not at every poll of the safe area.
	var pose := rig.transform
	main.call("_layout")
	check(rig.transform == pose, "laying out again moves nothing")
	# Above the controls and the bar, under the settings panel and the debug overlay.
	var home := hud.get_node("HomeScreen").get_index()
	for below: String in ["SpinWidget", "StrokeCue", "FreeTableBar", "Scoreboard", "ChoiceStrip", "CuePlacer"]:
		check(home > hud.get_node(below).get_index(), "Home above %s" % below)
	for above: String in ["SettingsPanel", "Overlay"]:
		check(home < hud.get_node(above).get_index(), "Home under %s" % above)
		for screen: String in ["MatchSetupScreen", "PauseMenu", "FrameSummary", "MatchSummary"]:
			check(hud.get_node(screen).get_index() < hud.get_node(above).get_index(), "%s under %s" % [screen, above])
	main.free()


func test_home_leads_to_the_free_table_and_back() -> void:
	var main: Node = await _spawn_home()
	var hud: Node = main.get_node("Hud")
	var home: HomeScreen = hud.get_node("HomeScreen")
	var bar: FreeTableBar = hud.get_node("FreeTableBar")
	var rig: CameraRig = main.get_node("CameraRig")
	var table: FreeTable = main.get_node("FreeTable")
	var before: Array[Dictionary] = table.balls.duplicate(true)
	home.button("free_table").pressed.emit()
	check(not home.visible and bar.visible, "the free table's bar takes Home's place")
	check_eq(main.call("harness_state")["screen"], "table", "the screen")
	check_eq(main.call("harness_state")["mode"], "free_table", "the mode")
	check(not rig.is_held() and rig.is_easing(), "the camera comes down behind the cue ball")
	check((main.get_node("Gestures") as Gestures).enabled and (main.get_node("AimControl") as AimControl).enabled, "aiming")
	check_eq(table.balls, before, "the rack as it was racked")
	check_eq(table.history_size(), 1, "and nothing added to its history")
	bar.button("menu").button_pressed = true
	bar.button("home").pressed.emit()
	check(home.visible and not bar.visible and not bar.is_menu_open(), "the menu's Home goes back")
	check_eq(main.call("harness_state")["screen"], "home", "the screen again")
	check(rig.is_held() and not (main.get_node("Cue") as Node3D).visible, "the full-table view, no cue")
	home.button("settings").pressed.emit()
	check((hud.get_node("SettingsPanel") as Control).visible, "Settings opens the settings panel")
	check(home.visible, "over Home")
	main.free()


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
	check(main.get_node_or_null("Stage/BallHighlight") == null, "no hard canopy highlight on the balls")
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


## The free table's controls, as main.gd places them, by their layout key.
const PLACED := {"FineTuneWheel": "wheel", "StrokeCue": "cue", "PullCue": "cue", "ElevationControl": "elevation",
		"SpinWidget": "spin"}


## The bar's row size once its theme applies, in the tree.
func _row_size() -> Vector2:
	var probe := FreeTableBar.new()
	tree.root.add_child(probe)
	var size := probe.row_rect().size
	probe.free()
	return size


func test_controls_follow_the_layout() -> void:
	var main: Node = await _spawn()
	var hud: Node = main.get_node("Hud")
	var canvas: Vector2 = main.get_viewport().get_visible_rect().size
	var window := main.get_window()
	var safe: Rect2 = HudScript.safe_rect(Rect2(DisplayServer.get_display_safe_area()),
			Rect2(Vector2(window.position), Vector2(window.size)), canvas)
	var bar: FreeTableBar = hud.get_node("FreeTableBar")
	var rects: Dictionary = HudScript.layout(safe, bar.row_rect().size, canvas, "", false)
	check_eq(bar.row_rect(), rects["bar"], "the bar's row where the layout expects")
	var wheel := hud.get_node("FineTuneWheel") as Control
	for control_name: String in PLACED:
		if control_name != "FineTuneWheel":
			check_eq((hud.get_node(control_name) as Control).get_rect(), rects[PLACED[control_name]], control_name)
	check_eq((hud.get_node("SpinWidget") as Control).size, Vector2.ONE * SpinWidget.SIZE, "the spin ball's own size")
	check(not wheel.visible, "no fine-tune wheel unless its setting is on")
	check_eq((rects["wheel"] as Rect2).size, Vector2.ZERO, "and no room kept for it")
	Settings.set_value("fine_tune_wheel", true)
	rects = HudScript.layout(safe, bar.row_rect().size, canvas)
	check(wheel.visible, "the setting shows the wheel")
	check_eq(wheel.get_rect(), rects["wheel"], "where the layout expects")
	check_eq(wheel.mouse_filter, Control.MOUSE_FILTER_STOP, "the wheel takes its own touches")
	Settings.set_value("fine_tune_wheel", Settings.DEFAULTS["fine_tune_wheel"])
	check(not wheel.visible, "and off hides it again")
	var push_mode: bool = Settings.get_value("strike_mode") == "stroke"
	check_eq((hud.get_node("StrokeCue") as Control).visible, push_mode, "the push cue in Push mode")
	check_eq((hud.get_node("PullCue") as Control).visible, not push_mode, "the pull cue in Pull mode")
	check(hud.get_node_or_null("LinkLabel") == null and hud.get_node_or_null("PowerBar") == null, "no link text or power bar")
	main.free()


func test_the_opening_view_keeps_the_cue_ball_and_baulk_colours_clear() -> void:
	var main: Node = await _spawn()
	await tree.process_frame
	var camera: Camera3D = main.get_node("CameraRig/Camera3D")
	var hud: Node = main.get_node("Hud")
	var controls: Array[Control] = []
	for control_name: String in PLACED:
		controls.append(hud.get_node(control_name))
	var bar: FreeTableBar = hud.get_node("FreeTableBar")
	var viewport := main.get_viewport().get_visible_rect().size
	for ball_name: String in ["cue", "yellow", "brown", "green", "blue", "pink", "black"]:
		var centre := (main.get_node("Balls/" + ball_name) as Node3D).global_position
		var at := camera.unproject_position(centre)
		var edge := camera.unproject_position(centre + camera.global_basis.x * RADIUS)
		var disc := Rect2(at, Vector2.ZERO).grow(at.distance_to(edge))
		for control in controls:
			if control.visible:
				check(not _overlaps(control.name, control.get_global_rect(), disc), "%s clear of %s (%s vs %s)" % [ball_name, control.name, disc, control.get_global_rect()])
		check(not bar.row_rect().intersects(disc), "%s clear of the bar" % ball_name)
		# The same view on the phone (the camera keeps its height), with its safe areas.
		var scale := PHONE_CANVAS.y / viewport.y
		var on_phone := Rect2((disc.position - viewport / 2.0) * scale + PHONE_CANVAS / 2.0, disc.size * scale)
		for safe_px in PHONE_SAFE_AREAS:
			var safe := HudScript.safe_rect(safe_px, PHONE_SCREEN, PHONE_CANVAS)
			var rects: Dictionary = HudScript.layout(safe, _row_size(), PHONE_CANVAS)
			for key: String in ["bar", "spin", "elevation", "cue", "wheel"]:
				check(not _overlaps(key, rects[key], on_phone), "phone: %s clear of the %s (%s vs %s)" % [ball_name, key, on_phone, rects[key]])
	main.free()


## The spin ball is round, and its square's corners pass touches to the table.
static func _overlaps(control: String, rect: Rect2, disc: Rect2) -> bool:
	if control != "spin" and control != "SpinWidget":
		return rect.intersects(disc)
	var apart := rect.get_center().distance_to(disc.get_center())
	return apart < rect.size.x / 2.0 + disc.size.x / 2.0


func test_the_debug_overlay_hides_no_control_on_the_phone() -> void:
	var main: Node = await _spawn()
	var overlay: DebugOverlay = main.get_node("Hud/Overlay")
	overlay.set_shown(true, "key")
	for safe_px in PHONE_SAFE_AREAS:
		var safe: Rect2 = HudScript.safe_rect(safe_px, PHONE_SCREEN, PHONE_CANVAS)
		var rects: Dictionary = HudScript.layout(safe, _row_size(), PHONE_CANVAS)
		# As main.gd's layout sets it.
		overlay.clear_right = safe.end.x - (rects["cue"] as Rect2).position.x
		overlay.place_in(safe_px, PHONE_SCREEN, PHONE_CANVAS, 3.0)
		var block := (overlay.get_node("Text") as Control).get_rect()
		check(safe.encloses(block), "inside the safe area: %s in %s" % [block, safe])
		for key: String in ["cue", "wheel", "elevation", "spin", "bar"]:
			check(not block.intersects(rects[key]), "clear of the %s (%s vs %s)" % [key, block, rects[key]])
	overlay.set_shown(false, "key")
	main.free()


func test_layout_fits_the_phones_safe_area_either_way_round() -> void:
	var row := _row_size()
	check(row.y <= 110.0, "the bar is one slim row: %s" % row)
	for safe_px in PHONE_SAFE_AREAS:
		var safe: Rect2 = HudScript.safe_rect(safe_px, PHONE_SCREEN, PHONE_CANVAS)
		var rects: Dictionary = HudScript.layout(safe, row, PHONE_CANVAS)
		var placed := {}
		for key: String in ["bar", "spin", "elevation", "cue", "wheel"]:
			placed[key] = rects[key]
			if key != "cue" and key != "spin":
				check(safe.encloses(rects[key]), "%s %s inside the safe area %s" % [key, rects[key], safe])
		for key: String in ["cue", "spin"]:
			check(Rect2(Vector2.ZERO, PHONE_CANVAS).encloses(rects[key]), "the %s on the screen: %s" % [key, rects[key]])
		check((rects["spin"] as Rect2).position.x >= safe.position.x, "the spin ball clear of the rounded corner")
		check_near(PHONE_CANVAS.y - (rects["spin"] as Rect2).end.y, HudScript.EDGE, 0.5, "and EDGE from the bottom")
		var keys := placed.keys()
		for i in keys.size():
			for j in range(i + 1, keys.size()):
				check(not (placed[keys[i]] as Rect2).intersects(placed[keys[j]]), "%s clear of %s" % [keys[i], keys[j]])
		var spin: Rect2 = rects["spin"]
		var elevation: Rect2 = rects["elevation"]
		var cue: Rect2 = rects["cue"]
		var wheel: Rect2 = rects["wheel"]
		check(spin.position.x - safe.position.x <= HudScript.EDGE and safe.end.y - spin.end.y <= HudScript.EDGE,
				"the spin ball in the bottom-left corner")
		check(elevation.end.y < spin.position.y and elevation.end.y > spin.position.y - 2.0 * HudScript.GAP,
				"the elevation right above it")
		check(elevation.size.y - ElevationControl.PAD_TOP - ElevationControl.PAD_BOTTOM >= 200.0,
				"its track at least 200 units: %s" % elevation)
		# The housing's side unknown: its centre line on the safe edge, clear of an island there.
		check_near(cue.get_center().x, minf(safe.end.x, PHONE_CANVAS.x - HudScript.CUE_FROM_EDGE), 0.5,
				"the cue at the right edge")
		check(cue.size.y > safe.size.y * 0.9, "the cue the full usable height")
		check(cue.size.x >= 88.0, "a 44 pt touch target")
		check(cue.size.y - CueStrip.TIP_REST >= PullControl.FULL_PULL, "room to pull to full power")
		check(wheel.end.x < cue.position.x and wheel.position.x > cue.position.x - 2.0 * HudScript.GAP - wheel.size.x,
				"the wheel beside the cue")
		check(wheel.size.x >= 88.0 and wheel.size.x < 120.0, "slimmer than before, still a touch target")
		# The middle of the screen, where the table is, stays clear.
		var middle := Rect2(PHONE_CANVAS.x * 0.2, 0.0, PHONE_CANVAS.x * 0.6, PHONE_CANVAS.y)
		for key: String in ["spin", "elevation", "cue", "wheel"]:
			check(not (placed[key] as Rect2).intersects(middle), "%s clear of the table's middle" % key)
		check((rects["bottom"] as Rect2).size.x > safe.size.x * 0.9, "edit mode's row spans the bottom")


func test_safe_area_is_clipped_to_the_window() -> void:
	# On Windows the safe area is the whole desktop; a window inside it needs no inset.
	var origin := HudScript.safe_origin(Rect2(0, 0, 2560, 1400), Rect2(300, 200, 956, 440), Vector2(1912, 880))
	check_eq(origin, Vector2.ZERO, "window inside the safe area")


func test_safe_area_clears_the_dynamic_island() -> void:
	# iPhone 17 Pro Max landscape: 2868x1320 px, 62 pt (186 px) side insets either way round,
	# 21 pt (63 px) home indicator; the canvas is 1912 wide.
	var origin := HudScript.safe_origin(PHONE_SAFE_AREAS[0], PHONE_SCREEN, PHONE_CANVAS)
	check_near(origin.x, 124.0, 0.01, "left inset in canvas units")
	check_near(origin.y, 0.0, 0.01, "top inset")


func test_the_link_status_is_a_dot_on_the_menu() -> void:
	var main: Node = await _spawn()
	var bar: FreeTableBar = main.get_node("Hud/FreeTableBar")
	check_eq(bar.link_status(), Link.get_status(), "the bar shows the link's status")
	Link.status_changed.emit("connected")
	check_eq(bar.link_status(), "connected", "and follows it")
	Link.status_changed.emit(Link.get_status())
	main.free()


func test_the_menu_opens_over_the_controls_and_reaches_its_items() -> void:
	var main: Node = await _spawn()
	var bar: FreeTableBar = main.get_node("Hud/FreeTableBar")
	var hud: Node = main.get_node("Hud")
	bar.open_menu()
	await tree.process_frame
	var list := bar.list_rect()
	for id: String in ["rack", "empty", "edit", "settings"]:
		check(list.encloses(bar.button(id).get_global_rect()), "%s in the list" % id)
	check(bar.get_index() > hud.get_node("SpinWidget").get_index() and bar.get_index() > hud.get_node("ElevationControl").get_index(),
			"the open menu above the shot controls")
	bar.close_menu()
	main.free()


func test_the_cue_stands_at_the_screens_edge_away_from_the_island() -> void:
	# KaiC: the power cue at the very right, as in the game he pointed to. iPhone 17 Pro Max: the
	# island spans 11 to 48 pt from its edge (22 to 96 units), the safe area starts at 62 pt.
	var safe: Rect2 = HudScript.safe_rect(PHONE_SAFE_AREAS[0], PHONE_SCREEN, PHONE_CANVAS)
	var island_clear := 96.0
	var clear: Rect2 = HudScript.layout(safe, _row_size(), PHONE_CANVAS, "left")["cue"]
	check_near(PHONE_CANVAS.x - clear.get_center().x, HudScript.CUE_FROM_EDGE, 0.5, "island on the left: at the edge")
	check_near(clear.end.x, PHONE_CANVAS.x, 0.5, "its touch area reaches the screen's edge")
	for housing: String in ["right", ""]:
		var cue: Rect2 = HudScript.layout(safe, _row_size(), PHONE_CANVAS, housing)["cue"]
		var drawn_right := cue.get_center().x + CueStrip.BUTT_WIDTH / 2.0
		check(PHONE_CANVAS.x - drawn_right > island_clear, "island %s: the drawn cue clear of it (%s)" % [housing, cue])
		check(cue.get_center().x > clear.get_center().x - 2.0 * HudScript.CUE_FROM_EDGE, "still at the right edge")
	# Windows: no safe margins, the cue at the window's edge.
	var desk: Rect2 = HudScript.layout(Rect2(Vector2.ZERO, PHONE_CANVAS), _row_size(), PHONE_CANVAS)["cue"]
	check_near(PHONE_CANVAS.x - desk.get_center().x, HudScript.CUE_FROM_EDGE, 0.5, "desktop: at the edge")
	for rects: Dictionary in [HudScript.layout(safe, _row_size(), PHONE_CANVAS, "left"),
			HudScript.layout(safe, _row_size(), PHONE_CANVAS, "right")]:
		check(not (rects["wheel"] as Rect2).intersects(rects["cue"]), "the wheel beside the cue")
		check(safe.encloses(rects["wheel"]), "the wheel inside the safe area")

