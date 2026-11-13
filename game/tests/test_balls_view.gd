extends TestCase

const RADIUS := 0.02625


func _layout() -> Array[Dictionary]:
	var layout: Array[Dictionary] = []
	layout.assign(OfsTable.opening_layout(0.0005))
	check_eq(layout.size(), 22, "opening layout")
	return layout


func _build() -> BallsView:
	var view := BallsView.new()
	tree.root.add_child(view)
	view.build(_layout())
	return view


func _state(layout: Array[Dictionary]) -> PackedFloat64Array:
	var state := PackedFloat64Array()
	state.resize(layout.size() * ShotPlayback.STRIDE)
	for i in layout.size():
		var o := i * ShotPlayback.STRIDE
		state[o + ShotPlayback.X] = float(layout[i]["x"])
		state[o + ShotPlayback.Y] = float(layout[i]["y"])
		state[o + ShotPlayback.POCKET] = -1.0
	return state


func test_children_follow_the_list_with_names_and_metas() -> void:
	var layout := _layout()
	var view := _build()
	check_eq(view.get_child_count(), layout.size(), "one child per ball")
	for i in layout.size():
		var ball := view.get_child(i) as MeshInstance3D
		var kind := String(layout[i]["ball"])
		var index := int(layout[i]["index"])
		check_eq(String(ball.name), "red_%d" % index if kind == "red" else kind, "name of ball %d" % i)
		check_eq(String(ball.get_meta("ball")), kind, "ball meta %d" % i)
		check_eq(int(ball.get_meta("index")), index, "index meta %d" % i)
		var xy: Array = ball.get_meta("core_xy")
		check_eq(float(xy[0]), float(layout[i]["x"]), "core x %d at full precision" % i)
		check_eq(float(xy[1]), float(layout[i]["y"]), "core y %d at full precision" % i)
		check_near(ball.position.y, RADIUS, 1e-6, "ball %d on the cloth" % i)
		check(ball.get_node_or_null("ContactShadow") != null, "ball %d has a contact shadow" % i)
	view.free()


func test_only_the_cue_ball_has_dots() -> void:
	var view := _build()
	for ball: Node in view.get_children():
		var material := (ball as MeshInstance3D).material_override as ShaderMaterial
		var dots := float(material.get_shader_parameter("dot_cos")) < 1.0
		check_eq(dots, ball.name == "cue", "%s dots" % ball.name)
	view.free()


func test_apply_state_moves_balls_to_core_positions() -> void:
	var layout := _layout()
	var view := _build()
	var state := _state(layout)
	state[3 * ShotPlayback.STRIDE + ShotPlayback.X] = 0.5
	state[3 * ShotPlayback.STRIDE + ShotPlayback.Y] = -0.25
	view.apply_state(state, 0.01)
	var ball := view.get_child(3) as Node3D
	check(ball.position.is_equal_approx(TableSpace.core_to_godot(0.5, -0.25, RADIUS)), "ball 3 at %s" % ball.position)
	var xy: Array = ball.get_meta("core_xy")
	check_eq(float(xy[0]), 0.5, "core_xy x follows")
	check_eq(float(xy[1]), -0.25, "core_xy y follows")
	view.free()


func test_a_ball_rolling_towards_the_top_turns_its_top_forwards() -> void:
	var layout := _layout()
	var view := _build()
	var state := _state(layout)
	var speed := 1.0
	var dt := 0.01
	state[ShotPlayback.VX] = speed
	state[ShotPlayback.WY] = speed / RADIUS
	state[ShotPlayback.MOTION] = ShotPlayback.ROLLING
	view.apply_state(state, dt)
	var cue := view.get_child(0) as Node3D
	var top := cue.basis * Vector3(0.0, RADIUS, 0.0)
	check(top.z < 0.0, "top moved towards Godot -Z (core +x): %s" % top)
	check_near(absf(top.x), 0.0, 1e-6, "no sideways turn")
	check_near(atan2(-top.z, top.y), speed / RADIUS * dt, 1e-5, "turned by w dt")
	var shadow := cue.get_node("ContactShadow") as Node3D
	check(shadow.global_position.y > 0.0004 and shadow.global_position.y < 0.002, "shadow stays on the cloth: %f" % shadow.global_position.y)
	check_near(shadow.global_position.x, cue.global_position.x, 1e-6, "shadow under the ball (x)")
	check_near(shadow.global_position.z, cue.global_position.z, 1e-6, "shadow under the ball (z)")
	view.free()


func test_a_pocketed_ball_drops_into_its_pocket_and_hides() -> void:
	var layout := _layout()
	var view := _build()
	var shape := TableView.shape_from_json(FileAccess.get_file_as_string(Physics.TABLE_SHAPE_PATH))
	view.set_pockets(shape["pockets"])
	var state := _state(layout)
	var o := 5 * ShotPlayback.STRIDE
	state[o + ShotPlayback.X] = 1.79
	state[o + ShotPlayback.Y] = 0.9
	state[o + ShotPlayback.MOTION] = ShotPlayback.POCKETED
	state[o + ShotPlayback.POCKET] = 0.0
	view.apply_state(state, 0.01)
	var ball := view.get_child(5) as Node3D
	check(view.is_dropping(), "dropping")
	check(ball.visible, "still visible while it drops")
	await tree.create_timer(BallsView.DROP_SECONDS + 0.2).timeout
	check(not view.is_dropping(), "the drop ended")
	check(not ball.visible, "hidden once it has dropped")
	var centre := _v2(shape["pockets"][0]["drop_centre"])
	check(TableSpace.godot_to_core(ball.position).distance_to(centre) < 1e-4, "ended over the drop centre")
	check(ball.position.y < 0.0, "below the cloth")
	# Later frames keep it gone; a fresh state with it back on the table shows it again.
	view.apply_state(state, 0.01)
	check(not ball.visible, "stays hidden")
	view.apply_state(_state(layout), 0.01)
	check(ball.visible, "shown again when back on the table")
	view.free()


func test_finish_drops_ends_every_drop_at_once() -> void:
	var layout := _layout()
	var view := _build()
	var state := _state(layout)
	state[2 * ShotPlayback.STRIDE + ShotPlayback.MOTION] = ShotPlayback.POCKETED
	state[2 * ShotPlayback.STRIDE + ShotPlayback.POCKET] = 9.0
	view.apply_state(state, 0.01)
	view.finish_drops()
	check(not view.is_dropping(), "no drop left")
	check(not (view.get_child(2) as Node3D).visible, "hidden")
	view.free()


func test_ball_at_screen_picks_the_ball_under_the_point() -> void:
	var view := _build()
	var camera := Camera3D.new()
	tree.root.add_child(camera)
	camera.position = Vector3(0.0, 2.5, 0.0)
	camera.look_at(Vector3.ZERO, Vector3(0.0, 0.0, -1.0))
	camera.current = true
	for i: int in [0, 3, 18, 21]:
		var ball := view.get_child(i) as Node3D
		var screen := camera.unproject_position(ball.global_position)
		check_eq(view.ball_at_screen(camera, screen), i, "ball %s under its own centre" % ball.name)
	check_eq(view.ball_at_screen(camera, camera.unproject_position(Vector3(0.6, RADIUS, 0.6))), -1, "empty cloth")
	camera.free()
	view.free()


static func _v2(value: Variant) -> Vector2:
	return Vector2(float(value[0]), float(value[1]))
