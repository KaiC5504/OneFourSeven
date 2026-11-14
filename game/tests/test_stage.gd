extends TestCase
## Stage: the light, the floor under the table and the bought lamp over it (those tests pass without
## checking anything when the lamp's file isn't in the project, as in CI).

var _log_start := 0


func _stage(with_lamp := false, lamp_path := Stage.LAMP_PATH) -> Stage:
	var stage := Stage.new()
	if with_lamp:
		stage.lamp_enabled = true
	stage.lamp_path = lamp_path
	tree.root.add_child(stage)
	stage.setup()
	return stage


func _camera_at(height: float) -> Camera3D:
	var camera := Camera3D.new()
	tree.root.add_child(camera)
	camera.global_position = Vector3(0.0, height, 2.5)
	return camera


# False, with a note in the test output, on a machine without the bought files.
func _has_lamp() -> bool:
	if ResourceLoader.exists(Stage.LAMP_PATH):
		return true
	print("      skipped: %s isn't in this project" % Stage.LAMP_PATH)
	return false


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


func _logged(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append((parsed as Dictionary).get("data", {}))
	return found


func test_the_key_light_shines_straight_down_with_soft_shadows() -> void:
	var stage := _stage()
	var light := stage.get_node_or_null("Light") as DirectionalLight3D
	check(light != null, "the key light")
	if light != null:
		check((-light.global_transform.basis.z).is_equal_approx(Vector3.DOWN), "straight down")
		check_near(light.light_energy, Stage.KEY_ENERGY, 1e-6, "energy")
		check_near(light.light_specular, Stage.KEY_SPECULAR, 1e-6, "the balls' soft highlight")
		check(light.shadow_enabled and light.light_angular_distance > 0.0, "soft shadows")
	var world := stage.get_node_or_null("WorldEnvironment") as WorldEnvironment
	check(world != null and world.environment != null, "the environment")
	if world != null and world.environment != null:
		check_near(world.environment.ambient_light_energy, Stage.AMBIENT_ENERGY, 1e-6, "ambient energy")
	stage.free()


func test_the_floor_lies_under_the_table_and_takes_its_shadow() -> void:
	var stage := _stage()
	var ground := stage.get_node_or_null("Floor") as MeshInstance3D
	check(ground != null, "the floor")
	if ground == null:
		stage.free()
		return
	check_near(ground.position.y, Stage.FLOOR_Y, 1e-6, "under the table's legs")
	check_eq(ground.layers, Stage.SCENERY_LAYER, "seen by the cameras, out of the probe")
	check_eq(ground.cast_shadow, GeometryInstance3D.SHADOW_CASTING_SETTING_OFF, "casts no shadow")
	check(ground.get_aabb().size.x > 2.0 * Stage.FLOOR_FADE_TO and ground.get_aabb().size.z > 2.0 * Stage.FLOOR_FADE_TO,
			"it reaches past where it fades into the backdrop")
	var material := ground.material_override as ShaderMaterial
	check(material != null, "its material")
	if material != null:
		var bounds := AABB(Vector3(-1.0, Stage.FLOOR_Y, -1.9), Vector3(2.0, 0.87, 3.8))
		stage.set_table_bounds(bounds)
		check_eq(material.get_shader_parameter("table_half"), Vector2(1.0, 1.9), "the shadow's outline is the table's")
		check_eq(material.get_shader_parameter("table_centre"), Vector2.ZERO, "under the table")
		check(float(material.get_shader_parameter("shade")) > 0.0 and float(material.get_shader_parameter("shade")) < 0.5,
				"a shadow, not a black hole")
	stage.free()


func test_the_probe_leaves_the_scenery_out() -> void:
	var stage := _stage()
	var probe := stage.get_node_or_null("Reflections") as ReflectionProbe
	check(probe != null, "the probe")
	if probe != null:
		check_eq(probe.cull_mask & Stage.SCENERY_LAYER, 0, "the lamp and floor aren't captured")
		check_eq(probe.reflection_mask & Stage.SCENERY_LAYER, 0, "nor lit by it")
		check(probe.cull_mask & Stage.TABLE_LAYER != 0 and probe.cull_mask & Stage.PROBE_ONLY_LAYER != 0, "the table and canopy are")
	var canopy := stage.get_node_or_null("Canopy") as MeshInstance3D
	check(canopy != null and canopy.layers == Stage.PROBE_ONLY_LAYER, "the canopy, for the probe alone")
	var camera := _camera_at(0.3)
	stage.dress_camera(camera)
	check(camera.cull_mask & Stage.SCENERY_LAYER != 0, "a dressed camera sees the scenery")
	check_eq(camera.cull_mask & Stage.PROBE_ONLY_LAYER, 0, "and not the canopy")
	camera.free()
	stage.free()


func test_the_lamp_is_all_there_below_and_gone_before_the_camera_reaches_it() -> void:
	check_near(Stage.lamp_shown(0.1), 1.0, 1e-9, "from table height")
	check_near(Stage.lamp_shown(Stage.LAMP_SHOWN_BELOW), 1.0, 1e-9, "at the height it starts to fade")
	check_near(Stage.lamp_shown(Stage.LAMP_GONE_ABOVE), 0.0, 1e-9, "at the height it is gone")
	check_near(Stage.lamp_shown(3.0), 0.0, 1e-9, "from the full-table camera")
	var previous := 1.0
	for i in 21:
		var shown := Stage.lamp_shown(lerpf(Stage.LAMP_SHOWN_BELOW, Stage.LAMP_GONE_ABOVE, i / 20.0))
		check(shown <= previous + 1e-9 and shown >= 0.0 and shown <= 1.0, "it only fades as the camera rises: %f after %f" % [shown, previous])
		previous = shown


func test_no_lamp_in_a_headless_run_or_without_its_file() -> void:
	_mark_log()
	var headless := _stage()
	check(headless.get_node_or_null("Lamp") == null, "a headless run hangs no lamp")
	headless.free()
	var missing := _stage(true, "res://licensed/no_such_lamp.gltf")
	check(missing.get_node_or_null("Lamp") == null, "no file, no lamp")
	var camera := _camera_at(0.3)
	missing.dress_camera(camera)
	missing.fit_shadows(camera)
	camera.free()
	missing.free()
	check_eq(_logged("app.error").size(), 0, "and nothing to report: the lamp is an extra")


func test_the_lamp_hangs_over_the_table_and_fades_with_the_camera() -> void:
	if not _has_lamp():
		return
	var stage := _stage(true)
	var lamp := stage.get_node_or_null("Lamp") as MeshInstance3D
	check(lamp != null, "the lamp")
	if lamp == null:
		stage.free()
		return
	check_eq(lamp.layers, Stage.SCENERY_LAYER, "seen by the cameras, out of the probe")
	check_eq(lamp.cast_shadow, GeometryInstance3D.SHADOW_CASTING_SETTING_OFF, "casts no shadow")
	var box := lamp.global_transform * lamp.get_aabb()
	check(box.position.y > Stage.LAMP_GONE_ABOVE, "it is gone before a rising camera is level with it: hangs from %f" % box.position.y)
	check(absf(box.get_center().x) < 0.01 and absf(box.get_center().z) < 0.01, "over the table's centre: %s" % box.get_center())
	for surface in lamp.mesh.get_surface_count():
		var material := lamp.get_surface_override_material(surface) as BaseMaterial3D
		check(material != null, "surface %d tuned" % surface)
		if material != null and material.resource_name == Stage.LAMP_SHADE:
			check(material.albedo_color.get_luminance() > 0.7 and material.emission_enabled, "pale, glowing shades")
		elif material != null and material.resource_name == Stage.LAMP_METAL:
			check(material.emission_enabled and material.emission_texture != null, "the bulbs glow")
	var camera := _camera_at(0.3)
	stage.dress_camera(camera)
	check(lamp.visible and lamp.transparency == 0.0, "all there for the aiming camera")
	camera.global_position.y = 0.5 * (Stage.LAMP_SHOWN_BELOW + Stage.LAMP_GONE_ABOVE)
	stage.fit_shadows(camera)
	check(lamp.visible and lamp.transparency > 0.0 and lamp.transparency < 1.0, "fading as the camera rises: %f" % lamp.transparency)
	camera.global_position.y = 2.5
	stage.fit_shadows(camera)
	check(not lamp.visible, "hidden from the full-table camera")
	camera.global_position.y = 0.2
	stage.fit_shadows(camera)
	check(lamp.visible and lamp.transparency == 0.0, "and back when the camera comes down")
	camera.free()
	stage.free()


# A lamp file without the lamp's mesh (here the table's) is logged and left out.
func test_a_lamp_file_without_the_lamp_is_logged() -> void:
	if not _has_lamp() or not ResourceLoader.exists(TableView.MODEL_PATH):
		return
	_mark_log()
	var stage := _stage(true, TableView.MODEL_PATH)
	check(stage.get_node_or_null("Lamp") == null, "no lamp")
	var errors := _logged("app.error")
	check_eq(errors.size(), 1, "logged once")
	if errors.size() == 1:
		check(String(errors[0].get("message", "")).contains(TableView.MODEL_PATH) and String(errors[0].get("message", "")).contains("Lamp"),
				"naming the file and the missing mesh: %s" % errors[0].get("message"))
	stage.free()
