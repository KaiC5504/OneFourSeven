extends TestCase

const RADIUS := 0.02625
const CUE_XY := Vector2(-1.0475, -0.146)
const TOLERANCE := 0.0005


func _cue() -> CueView:
	var cue := CueView.new()
	tree.root.add_child(cue)
	return cue


func test_tip_addresses_the_ball_at_the_offset_with_the_gap() -> void:
	var cue := _cue()
	var centre := TableSpace.core_to_godot(CUE_XY.x, CUE_XY.y, RADIUS)
	var tips: Array[Vector2] = [Vector2.ZERO, Vector2(0.0, 0.5), Vector2(0.0, -0.4), Vector2(0.45, 0.0),
			Vector2(-0.3, 0.3)]
	for aim: float in [0.0, 0.7, -2.4]:
		for elevation: float in [0.0, 0.35]:
			for tip in tips:
				cue.set_pose(CUE_XY, aim, elevation, tip, 0.0)
				var label := "aim %.1f elevation %.2f tip %s" % [aim, elevation, tip]
				var axis := cue.axis()
				var touch := cue.tip_position() + axis * CueView.GAP
				check_near(touch.distance_to(centre), RADIUS, TOLERANCE, label + ": tip plus the gap on the ball")
				# The offset is square to the cue: x along "right", y along "up" square to the axis.
				var right := TableSpace.core_to_godot(sin(aim), -cos(aim))
				var up := right.cross(axis).normalized()
				var from_centre := touch - centre
				check_near(from_centre.dot(right), tip.x * RADIUS, TOLERANCE, label + ": side offset")
				check_near(from_centre.dot(up), tip.y * RADIUS, TOLERANCE, label + ": height offset")
	cue.free()


func test_axis_follows_the_aim_and_tilts_down_by_the_elevation() -> void:
	var cue := _cue()
	cue.set_pose(CUE_XY, 0.4, 0.3, Vector2.ZERO, 0.0)
	var axis := cue.axis()
	check_near(axis.y, -sin(0.3), 1e-5, "tilted down into the ball")
	var flat := TableSpace.godot_to_core(Vector3(axis.x, 0.0, axis.z)).normalized()
	check_near(flat.angle(), 0.4, 1e-5, "points along the aim")
	cue.free()


func test_backswing_pulls_the_tip_back_along_the_cue() -> void:
	var cue := _cue()
	cue.set_pose(CUE_XY, 1.0, 0.2, Vector2(0.2, -0.1), 0.0)
	var addressed := cue.tip_position()
	cue.set_pose(CUE_XY, 1.0, 0.2, Vector2(0.2, -0.1), 0.12)
	var pulled := cue.tip_position()
	check_near(addressed.distance_to(pulled), 0.12, 1e-5, "moved by the backswing")
	check((addressed - pulled).normalized().is_equal_approx(cue.axis()), "straight back along the axis")
	cue.free()


func test_the_cue_is_full_length_and_tapered() -> void:
	# Ours or the bought one (res://licensed/cue.gltf), as it stands in the CueView's own frame.
	var cue := _cue()
	cue.set_pose(CUE_XY, 0.0, 0.0, Vector2.ZERO, 0.0)
	var instance := cue.get_node("Mesh") as MeshInstance3D
	var box := instance.transform * instance.mesh.get_aabb()
	check_near(box.size.z, CueView.LENGTH, 1e-4, "length")
	check_near(box.size.x, CueView.BUTT_DIAMETER, 1e-4, "butt diameter")
	check_near(box.position.z, 0.0, 1e-5, "the tip's face is the node's origin")
	check_near(box.get_center().x, 0.0, 1e-4, "on the node's axis (x)")
	check_near(box.get_center().y, 0.0, 1e-4, "on the node's axis (y)")
	# Slimmer at the tip than at the butt: the vertices of its first and last 5 cm.
	var tip_girth := 0.0
	var butt_girth := 0.0
	for s in instance.mesh.get_surface_count():
		for vertex: Vector3 in instance.mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX]:
			var v := instance.transform * vertex
			if v.z < 0.05:
				tip_girth = maxf(tip_girth, Vector2(v.x, v.y).length())
			elif v.z > CueView.LENGTH - 0.05:
				butt_girth = maxf(butt_girth, Vector2(v.x, v.y).length())
	check(tip_girth > 0.003 and tip_girth < 0.008, "a tip of 6 to 16 mm across: %f" % (2.0 * tip_girth))
	check_near(2.0 * butt_girth, CueView.BUTT_DIAMETER, 1e-3, "widest at the butt")
	check_eq(instance.layers, Stage.MOVING_LAYER, "out of the table's reflection probe")
	cue.free()


func test_the_bought_cue_is_shown_when_it_is_there() -> void:
	var cue := _cue()
	var instance := cue.get_node("Mesh") as MeshInstance3D
	var bought := ResourceLoader.exists(CueView.BOUGHT_PATH)
	check_eq(instance.mesh is ArrayMesh and instance.mesh.get_surface_count() == 4, not bought, "our own four-part cue only without it")
	if bought:
		var material := instance.get_active_material(0) as BaseMaterial3D
		check(material != null and material.albedo_texture != null
				and material.albedo_texture.resource_path.begins_with("res://licensed/"), "the model's own textures")
		check(instance.material_override != null and instance.material_override != instance.mesh.surface_get_material(0),
				"on a copy: the model's material is left alone")
	cue.free()


func test_the_cue_dissolves_before_the_camera_clips_it() -> void:
	var cue := _cue()
	var instance := cue.get_node("Mesh") as MeshInstance3D
	var mesh := instance.mesh
	check(CueView.NEAR_HIDDEN_M > CameraRig.NEAR_M, "hidden before the near plane cuts it open")
	for i in mesh.get_surface_count():
		var material := instance.get_active_material(i) as BaseMaterial3D
		check_eq(material.distance_fade_mode, BaseMaterial3D.DISTANCE_FADE_PIXEL_DITHER, "surface %d dithers out" % i)
		check_near(material.distance_fade_min_distance, CueView.NEAR_HIDDEN_M, 1e-6, "surface %d hidden up close" % i)
		check_near(material.distance_fade_max_distance, CueView.NEAR_SOLID_M, 1e-6, "surface %d solid beyond" % i)
	cue.free()


func test_show_and_strike() -> void:
	var cue := _cue()
	cue.set_pose(CUE_XY, 0.0, 0.1, Vector2.ZERO, 0.1)
	cue.show_cue(false)
	check(not cue.visible, "hidden")
	cue.show_cue(true)
	check(cue.visible, "shown")
	var before := cue.tip_position()
	cue.play_strike(3.0)
	await tree.create_timer(0.08).timeout
	check(cue.tip_position().distance_to(before) > 0.0, "the tip moves through")
	await tree.create_timer(0.6).timeout
	check(not cue.visible, "hidden after the strike")
	cue.free()
