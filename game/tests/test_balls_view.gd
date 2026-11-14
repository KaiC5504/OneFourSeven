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
		check(material.shader.code.contains("dot_cos") and material.shader.code.contains("local_dir"), "%s: the dots are in its shader" % ball.name)
	view.free()


func test_the_bought_balls_are_drawn_when_they_are_there() -> void:
	# res://licensed/balls.gltf is the bought model's (never in the repository): with it each ball
	# is the model's mesh with its colour from the model's swatch texture and the model's gloss, the
	# highlight still ours; without it, our own sphere and colours.
	var view := _build()
	var bought := ResourceLoader.exists(BallsView.BOUGHT_PATH)
	check_eq(not BallsView.bought(RADIUS).is_empty(), bought, "the model is used exactly when it is there")
	var meshes := {}
	for ball: Node in view.get_children():
		var instance := ball as MeshInstance3D
		var material := instance.material_override as ShaderMaterial
		var kind := String(instance.get_meta("ball"))
		meshes[kind] = instance.mesh
		var box := instance.mesh.get_aabb()
		check_near(box.size.x, 2.0 * RADIUS, 1e-3, "%s: a ball's width" % ball.name)
		check(box.get_center().length() < 1e-4, "%s: centred on its node" % ball.name)
		check_eq(instance.layers, Stage.BALL_LAYER, "%s: on the balls' layer" % ball.name)
		check_eq(instance.mesh is SphereMesh, not bought, "%s: our sphere only without the model" % ball.name)
		if bought:
			check(material.get_shader_parameter("swatches") is Texture2D, "%s: the model's colours" % ball.name)
			check_near(float(material.get_shader_parameter("gloss_roughness")), float(BallsView.bought(RADIUS)["roughness"]), 1e-9,
					"%s: the model's gloss" % ball.name)
			check(material.shader.code.contains("void light()"), "%s: our highlight" % ball.name)
		else:
			check_eq(material.get_shader_parameter("base_colour"), BallsView.BALL_COLOURS[kind], "%s: our colour" % ball.name)
	if bought:
		check(meshes["red"] != meshes["black"] and meshes["cue"] != meshes["red"], "each kind has its own mesh")
	else:
		check_eq(meshes["red"], meshes["black"], "one sphere for all")
	view.free()


func test_swatch_colours_are_the_balls_colours() -> void:
	var bought: Dictionary = BallsView.bought(RADIUS).get("colours", {})
	for kind: String in BallsView.BALL_COLOURS:
		var colour := BallsView.ball_colour(kind)
		if bought.has(kind):
			check_eq(colour, bought[kind], "%s: the model's swatch" % kind)
		else:
			check_eq(colour, BallsView.BALL_COLOURS[kind], "%s: our own colour" % kind)
	if not bought.is_empty():
		# Read from the model's texture: each is the colour it is named for.
		var red: Color = bought["red"]
		var blue: Color = bought["blue"]
		var black: Color = bought["black"]
		var white: Color = bought["cue"]
		check(red.r > 0.4 and red.r > 2.0 * red.g and red.r > 2.0 * red.b, "red is red: %s" % red)
		check(blue.b > 0.4 and blue.b > blue.r and blue.b > blue.g, "blue is blue: %s" % blue)
		check(black.get_luminance() < 0.1, "black is dark: %s" % black)
		check(white.get_luminance() > 0.8, "the cue ball is white: %s" % white)
	check_eq(BallsView.ball_colour("no such ball"), BallsView.ball_colour("cue"), "an unknown kind is drawn as the cue ball")


func test_a_potted_ball_rolls_over_the_lip_and_stops_in_the_net() -> void:
	# A ball is potted with its centre over the edge of the hole (the drop circle). From every side
	# of a corner and a middle pocket, at rest and at pace, it never comes nearer that edge than
	# its own radius on the way in, ends over the drop centre and never sinks past DROP_DEPTH.
	var layout := _layout()
	var view := _build()
	var shape := TableView.shape_from_json(FileAccess.get_file_as_string(Physics.TABLE_SHAPE_PATH))
	var pockets: Array = shape["pockets"]
	view.set_pockets(pockets)
	var ball := view.get_child(5) as Node3D
	var o := 5 * ShotPlayback.STRIDE
	for p: int in [0, 2]:
		var centre := _v2(pockets[p]["drop_centre"])
		var drop_r := float(pockets[p]["drop_radius"])
		# Out of the pocket along its axis: the diagonal at a corner, square to the cushion at a middle.
		var towards_table := -Vector2(signf(centre.x), signf(centre.y)).normalized() if bool(pockets[p]["corner"]) 				else Vector2(0.0, -signf(centre.y))
		for turn: float in [-0.25, 0.0, 0.25]:
			for speed: float in [0.0, 0.3, 2.0, 8.0]:
				var label := "pocket %d, %.2f rad round, %.1f m/s" % [p, turn, speed]
				var from := centre + towards_table.rotated(turn) * drop_r
				var state := _state(layout)
				state[o + ShotPlayback.X] = from.x
				state[o + ShotPlayback.Y] = from.y
				state[o + ShotPlayback.VX] = (centre - from).normalized().x * speed
				state[o + ShotPlayback.VY] = (centre - from).normalized().y * speed
				state[o + ShotPlayback.MOTION] = ShotPlayback.ROLLING
				view.apply_state(state, 0.0)
				state[o + ShotPlayback.VX] = 0.0
				state[o + ShotPlayback.VY] = 0.0
				state[o + ShotPlayback.MOTION] = ShotPlayback.POCKETED
				state[o + ShotPlayback.POCKET] = float(p)
				view.apply_state(state, 0.0)
				view.set_process(false)
				var lip := TableSpace.core_to_godot(from.x, from.y, 0.0)
				var nearest := INF
				var lowest := INF
				var steps := 0
				var first_step := 0.0
				while view.is_dropping() and steps < 1000:
					view._process(0.001)
					steps += 1
					if steps == 1:
						first_step = TableSpace.godot_to_core(ball.position).distance_to(from)
					nearest = minf(nearest, ball.position.distance_to(lip))
					lowest = minf(lowest, ball.position.y)
					check(TableSpace.godot_to_core(ball.position).distance_to(centre) <= drop_r + 1e-6, label + ": stays over the hole")
				check(nearest > RADIUS - 2e-5, label + ": over the lip, not through it (%f)" % nearest)
				check_near(lowest, RADIUS - BallsView.DROP_DEPTH, 1e-6, label + ": down to its rest in the net")
				check(TableSpace.godot_to_core(ball.position).distance_to(centre) < 1e-5, label + ": ends over the drop centre")
				check(steps <= roundi(BallsView.DROP_SECONDS * 1000.0) + 1, label + ": no longer than a dying ball's drop (%d ms)" % steps)
				check(steps >= roundi(BallsView.DROP_MIN_SECONDS * 1000.0) - 1, label + ": no shorter than the shortest (%d ms)" % steps)
				# It carries on at the pace it came in with, up to a ball's fall time for the whole drop.
				check_near(first_step, speed * 0.001, 0.15 * speed * 0.001 + 2e-5, label + ": the pace it came in with")
				view.apply_state(_state(layout), 0.0)
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
	check(not view.is_processing(), "no per-frame work before a drop")
	view.apply_state(state, 0.01)
	var ball := view.get_child(5) as Node3D
	check(view.is_dropping(), "dropping")
	check(view.is_processing(), "animates the drop each frame")
	check(ball.visible, "still visible while it drops")
	await tree.create_timer(BallsView.DROP_SECONDS + 0.2).timeout
	check(not view.is_dropping(), "the drop ended")
	check(not view.is_processing(), "and stops working each frame")
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
