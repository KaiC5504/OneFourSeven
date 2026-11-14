extends TestCase
## Guides (spec 8.4): the straight-line first contact, which nodes each level shows, the level 3
## throttle and its slow-prediction log, and that guides never touch the shot setup.

const Preview := preload("res://scripts/dev/guides_preview.gd")
const R := 0.02625


class FakeClock extends RefCounted:
	var now := 100.0

	func read() -> float:
		return now


class FakeRecord extends RefCounted:
	var paths: Array[PackedVector2Array] = []

	func t_end() -> float:
		return 2.0

	func ball_count() -> int:
		return paths.size()

	func path(ball: int, _max_step_s: float) -> PackedVector2Array:
		return paths[ball]

	func final_balls() -> Array:
		return []


## Counts calls, keeps what it was given, and can take `cost_s` of the fake clock's time.
class FakePredictor extends RefCounted:
	var calls: Array[Dictionary] = []
	var record: Variant = null
	var clock: FakeClock
	var cost_s := 0.0

	func predict(balls: Array[Dictionary], cue_ball: int, strike: Dictionary) -> Variant:
		calls.append({"balls": balls, "cue_ball": cue_ball, "strike": strike.duplicate()})
		clock.now += cost_s
		return record


func _shape() -> Dictionary:
	return Preview.shape_from_json(FileAccess.get_file_as_string("res://data/table_shape.json"))


func _balls(positions: Array) -> Array[Dictionary]:
	var balls: Array[Dictionary] = []
	# [x, y] Arrays keep full precision; Vector2 is single precision.
	for i in positions.size():
		var p: Variant = positions[i]
		balls.append({"ball": "cue" if i == 0 else "red", "index": maxi(i - 1, 0), "x": float(p[0]), "y": float(p[1])})
	return balls


func _contact(positions: Array, aim_rad: float) -> Dictionary:
	return Guides.first_contact(_balls(positions), 0, aim_rad, _shape(), R)


func test_straight_on() -> void:
	var c := _contact([Vector2(-1.0, 0.0), Vector2(0.0, 0.0)], 0.0)
	check_eq(c["kind"], "ball", "kind")
	check_eq(c["index"], 1, "index")
	check_near(c["distance"], 1.0 - 2.0 * R, 1e-9, "distance")
	check_near((c["ghost"] as Vector2).x, -2.0 * R, 1e-6, "ghost x")
	check_near((c["ghost"] as Vector2).y, 0.0, 1e-6, "ghost y")
	check_near((c["object_dir"] as Vector2).x, 1.0, 1e-6, "object_dir x")


func test_half_ball_cuts_thirty_degrees() -> void:
	var c := _contact([Vector2(-1.0, 0.0), Vector2(0.0, R)], 0.0)
	check_eq(c["kind"], "ball", "kind")
	check_near((c["object_dir"] as Vector2).angle(), deg_to_rad(30.0), 1e-6, "object_dir angle")
	check_near(c["distance"], 1.0 - 2.0 * R * cos(deg_to_rad(30.0)), 1e-9, "distance")
	check_near((c["ghost"] as Vector2).distance_to(Vector2(0.0, R)), 2.0 * R, 1e-6, "ghost touches the object ball")


func test_nearest_of_two_wins_whatever_the_order() -> void:
	var c := _contact([Vector2(-1.0, 0.0), Vector2(0.5, 0.0), Vector2(0.0, 0.01)], 0.0)
	check_eq(c["index"], 2, "nearer ball")
	check_near(c["distance"], 1.0 - sqrt(4.0 * R * R - 0.0001), 1e-9, "distance")


func test_ball_behind_is_ignored() -> void:
	var c := _contact([Vector2(0.0, 0.0), Vector2(-0.5, 0.0)], 0.0)
	check_eq(c["kind"], "cushion", "past the ball behind to the top cushion")
	check_near(c["distance"], 1.7845 - R, 1e-9, "distance")


func test_touching_ball_is_hit_at_distance_zero() -> void:
	var c := _contact([[-1.0, 0.0], [-1.0 + 2.0 * R, 0.0]], 0.0)
	check_eq(c["kind"], "ball", "kind")
	check_near(c["distance"], 0.0, 1e-12, "distance")
	check_near((c["object_dir"] as Vector2).x, 1.0, 1e-6, "object_dir x")
	var away := _contact([[-1.0, 0.0], [-1.0 + 2.0 * R, 0.0]], PI)
	check_eq(away["kind"], "cushion", "aiming away from a touching ball")


func test_potted_ball_is_ignored() -> void:
	var balls := _balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0)])
	balls[1]["pocket"] = 2
	check_eq(Guides.first_contact(balls, 0, 0.0, _shape(), R)["kind"], "cushion", "potted ball")


func test_cushion_noses() -> void:
	var top := _contact([Vector2(0.0, 0.0)], 0.0)
	check_eq(top["kind"], "cushion", "top kind")
	check_eq(top["index"], -1, "top index")
	check_near((top["ghost"] as Vector2).x, 1.7845 - R, 1e-6, "top ghost x")
	check_eq(top["object_dir"], Vector2.ZERO, "no object_dir off a cushion")
	var side := _contact([Vector2(0.5, 0.0)], PI / 2.0)
	check_eq(side["kind"], "cushion", "side kind")
	check_near(side["distance"], 0.889 - R, 1e-9, "side distance")


func test_jaw_is_a_cushion() -> void:
	# Between the middle pocket's facing and its top jaw, so the jaw's curve is what it meets.
	var c := _contact([[0.03, 0.0]], PI / 2.0)
	var expected := 0.9525 - sqrt(pow(0.0635 + R, 2.0) - pow(0.1075 - 0.03, 2.0))
	check_eq(c["kind"], "cushion", "kind")
	check_near(c["distance"], expected, 1e-9, "distance")


func test_through_a_mouth_is_a_pocket() -> void:
	var middle := _contact([Vector2(0.0, 0.0)], PI / 2.0)
	check_eq(middle["kind"], "pocket", "middle kind")
	check_eq(middle["index"], -1, "middle index")
	check_near(middle["distance"], 0.9985 - 0.044, 1e-9, "middle distance to the drop circle")
	var top_left: Array = (_shape()["pockets"] as Array)[0]["drop_centre"]
	var drop := Vector2(float(top_left[0]), float(top_left[1]))
	var corner := _contact([drop - Vector2(0.8, 0.8)], PI / 4.0)
	check_eq(corner["kind"], "pocket", "corner kind")
	check_near(corner["distance"], 0.8 * sqrt(2.0) - 0.042, 1e-6, "corner distance")


func test_bad_cue_index_is_none() -> void:
	check_eq(_contact([Vector2.ZERO], 0.0)["kind"], "cushion", "valid")
	check_eq(Guides.first_contact(_balls([Vector2.ZERO]), 3, 0.0, _shape(), R)["kind"], "none", "out of range")


## The caller keeps `predictor`: Callables to its methods don't keep it alive.
func _guides(predictor: FakePredictor) -> Guides:
	var guides := Guides.new()
	guides.table_shape = _shape()
	guides.ball_radius = R
	guides.clock = predictor.clock.read
	guides.predictor = predictor.predict
	return guides


func _predictor(record: Variant = null) -> FakePredictor:
	var predictor := FakePredictor.new()
	predictor.clock = FakeClock.new()
	predictor.record = record
	return predictor


## Names of the visible children, sorted.
func _visible(guides: Guides) -> Array[String]:
	var shown: Array[String] = []
	for child in guides.get_children():
		if (child as Node3D).visible:
			shown.append(String(child.name))
	shown.sort()
	return shown


func test_levels_show_their_nodes() -> void:
	var record := FakeRecord.new()
	record.paths = [PackedVector2Array([Vector2(-1.0, 0.0), Vector2(0.5, 0.0)]), PackedVector2Array([Vector2(0.0, 0.0)])]
	var predictor := _predictor(record)
	var guides := _guides(predictor)
	var balls := _balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.1)])
	var setup := ShotSetup.new()
	setup.set_aim(0.1)
	check_eq(_visible(guides), [] as Array[String], "nothing before show_for")
	guides.show_for(balls, 0, setup, 1)
	check_eq(_visible(guides), ["AimLine", "GhostBall", "GhostRing"] as Array[String], "level 1")
	guides.show_for(balls, 0, setup, 2)
	check_eq(_visible(guides), ["AimLine", "GhostBall", "GhostRing", "ObjectLine"] as Array[String], "level 2")
	guides.show_for(balls, 0, setup, 3)
	check_eq(_visible(guides), ["PaceLabel", "Paths"] as Array[String], "level 3")
	guides.hide_all()
	check_eq(_visible(guides), [] as Array[String], "hide_all")
	guides.show_for(balls, 0, setup, 2)
	guides.show_for(balls, 0, setup, 0)
	check_eq(_visible(guides), [] as Array[String], "level 0")
	setup.set_aim(-0.6)
	guides.show_for(balls, 0, setup, 2)
	check_eq(_visible(guides), ["AimLine", "GhostBall", "GhostRing"] as Array[String], "level 2 off a cushion has no object line")
	guides.free()


func test_level_three_draws_moving_balls_at_the_guide_pace() -> void:
	var record := FakeRecord.new()
	record.paths = [
		PackedVector2Array([Vector2(-1.0, 0.0), Vector2(-0.05, 0.0), Vector2(-0.05, 0.4)]),
		PackedVector2Array([Vector2(0.0, 0.0), Vector2(0.8, 0.0)]),
		PackedVector2Array([Vector2(0.5, 0.5), Vector2(0.5, 0.5)]),
	]
	var predictor := _predictor(record)
	var guides := _guides(predictor)
	var setup := ShotSetup.new()
	setup.set_aim(0.02)
	setup.set_speed(3.0, 0.5)
	setup.wander_rad = 0.05
	guides.show_for(_balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0), Vector2(0.5, 0.5)]), 0, setup, 3)
	check_eq(predictor.calls.size(), 1, "one prediction")
	var strike: Dictionary = predictor.calls[0]["strike"]
	check_near(strike["speed_m_s"], ShotSetup.DEFAULT_PACE_M_S, 1e-9, "stroke mode before any stroke: default pace")
	check_near(strike["aim_rad"], 0.02, 1e-9, "no wander")
	var mesh: ArrayMesh = (guides.get_node("Paths") as MeshInstance3D).mesh
	check_eq(mesh.get_surface_count(), 1, "one surface of ribbons")
	if mesh.get_surface_count() == 1:
		# Two moving balls: three segments, six vertices each; the still ball draws nothing.
		check_eq((mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX] as PackedVector3Array).size(), 18, "vertices")
	check_eq((guides.get_node("PaceLabel") as Label3D).text, "pace 1.5 m/s", "default pace label")
	setup.last_stroke_speed_m_s = 2.4
	predictor.clock.now += 1.0
	guides.show_for(_balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0), Vector2(0.5, 0.5)]), 0, setup, 3)
	check_eq((guides.get_node("PaceLabel") as Label3D).text, "last stroke 2.4 m/s", "stroke label")
	setup.set_strike_mode(ShotSetup.PULL)
	predictor.clock.now += 1.0
	guides.show_for(_balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0), Vector2(0.5, 0.5)]), 0, setup, 3)
	check_eq((guides.get_node("PaceLabel") as Label3D).text, "last stroke 2.4 m/s", "pull mode between pulls: the last shot's")
	setup.set_pull(0.6, 3.0)
	predictor.clock.now += 1.0
	guides.show_for(_balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0), Vector2(0.5, 0.5)]), 0, setup, 3)
	check_eq((guides.get_node("PaceLabel") as Label3D).text, "pull 3.0 m/s", "pulling: the pull's")
	check_near(predictor.calls[-1]["strike"]["speed_m_s"], 3.0, 1e-9, "the pull's pace predicted")
	guides.free()


func test_throttle_keeps_to_the_rate_and_predicts_the_last_aim() -> void:
	var predictor := _predictor(FakeRecord.new())
	var guides := _guides(predictor)
	tree.root.add_child(guides)
	var balls := _balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0)])
	var setup := ShotSetup.new()
	for i in 20:
		predictor.clock.now = 100.0 + i * 0.05
		setup.set_aim(0.001 * i)
		guides.show_for(balls, 0, setup, 3)
		await tree.process_frame
	predictor.clock.now = 101.5
	await tree.process_frame
	await tree.process_frame
	check(predictor.calls.size() <= 6, "at most 6 predictions in 1 s, got %d" % predictor.calls.size())
	check(predictor.calls.size() >= 5, "kept predicting while the aim moved, got %d" % predictor.calls.size())
	check_near(predictor.calls[-1]["strike"]["aim_rad"], 0.019, 1e-9, "the final aim was predicted")
	guides.queue_free()


func test_no_change_no_prediction() -> void:
	var predictor := _predictor(FakeRecord.new())
	var guides := _guides(predictor)
	tree.root.add_child(guides)
	var balls := _balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0)])
	var setup := ShotSetup.new()
	for i in 10:
		predictor.clock.now = 100.0 + i * 0.5
		guides.show_for(balls, 0, setup, 3)
		await tree.process_frame
	check_eq(predictor.calls.size(), 1, "one prediction for unchanged inputs")
	setup.set_tip(0.0, 0.3)
	predictor.clock.now += 0.5
	guides.show_for(balls, 0, setup, 3)
	check_eq(predictor.calls.size(), 2, "spin change predicts again")
	balls[1]["x"] = 0.2
	predictor.clock.now += 0.5
	guides.show_for(balls, 0, setup, 3)
	check_eq(predictor.calls.size(), 3, "a moved ball (same Array, edited in place) predicts again")
	guides.queue_free()


func test_hidden_guides_drop_a_waiting_prediction() -> void:
	var predictor := _predictor(FakeRecord.new())
	var guides := _guides(predictor)
	tree.root.add_child(guides)
	var balls := _balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0)])
	var setup := ShotSetup.new()
	guides.show_for(balls, 0, setup, 3)
	setup.set_aim(0.2)
	guides.show_for(balls, 0, setup, 3)
	guides.hide_all()
	predictor.clock.now += 1.0
	await tree.process_frame
	await tree.process_frame
	check_eq(predictor.calls.size(), 1, "nothing predicted while hidden")
	guides.queue_free()


func test_slow_prediction_is_logged() -> void:
	var predictor := _predictor(FakeRecord.new())
	predictor.cost_s = 0.007357
	var guides := _guides(predictor)
	guides.show_for(_balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0)]), 0, ShotSetup.new(), 3)
	check_near(guides.last_predict_us(), 7357.0, 0.01, "last_predict_us")
	var found: Dictionary = {}
	OfsLog.flush()
	for line in FileAccess.get_file_as_string(OfsLog.file_path()).split("\n", false):
		if line.contains("\"event\":\"guide.predict_slow\""):
			found = (JSON.parse_string(line) as Dictionary)
	check(not found.is_empty(), "guide.predict_slow logged")
	if not found.is_empty():
		var data: Dictionary = found["data"]
		check_near(data["sim_us"], 7357.0, 0.01, "sim_us")
		check_near(data["budget_us"], Guides.PREDICT_BUDGET_US, 1e-9, "budget_us")
		check_eq(data["level"], 3.0, "level")
		check_eq(found["level"], "warn", "log level")
	guides.free()


func test_fast_prediction_is_not_logged() -> void:
	var predictor := _predictor(FakeRecord.new())
	predictor.cost_s = 0.001
	var guides := _guides(predictor)
	OfsLog.flush()
	var before := FileAccess.get_file_as_string(OfsLog.file_path()).count("guide.predict_slow")
	guides.show_for(_balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0)]), 0, ShotSetup.new(), 3)
	OfsLog.flush()
	check_eq(FileAccess.get_file_as_string(OfsLog.file_path()).count("guide.predict_slow"), before, "no slow log")
	guides.free()


func test_setup_is_unchanged() -> void:
	var predictor := _predictor(FakeRecord.new())
	var guides := _guides(predictor)
	var setup := ShotSetup.new()
	setup.set_aim(0.3)
	setup.set_tip(0.2, -0.4)
	setup.set_elevation(0.1)
	setup.set_speed(2.0, 0.4)
	setup.wander_rad = 0.01
	setup.add_stroke_sample(0.1, Vector2(3.0, 4.0))
	var emitted: Array[StringName] = []
	setup.changed.connect(func(what: StringName) -> void: emitted.append(what))
	var before := [setup.snapshot(), setup.log_fields(), setup.to_strike(), setup.last_stroke_speed_m_s, setup.elevation_min_rad]
	var balls := _balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0)])
	for level in 4:
		predictor.clock.now += 1.0
		guides.show_for(balls, 0, setup, level)
	check_eq([setup.snapshot(), setup.log_fields(), setup.to_strike(), setup.last_stroke_speed_m_s, setup.elevation_min_rad], before, "setup")
	check_eq(emitted, [] as Array[StringName], "no changed signals")
	guides.free()


func test_refused_prediction_is_logged_once_and_draws_nothing() -> void:
	var predictor := _predictor(null)
	var guides := _guides(predictor)
	var setup := ShotSetup.new()
	var balls := _balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0)])
	OfsLog.flush()
	var before := FileAccess.get_file_as_string(OfsLog.file_path()).count("guides: no level 3 prediction")
	for i in 3:
		setup.set_aim(0.01 * i)
		predictor.clock.now += 1.0
		guides.show_for(balls, 0, setup, 3)
	OfsLog.flush()
	check_eq(predictor.calls.size(), 3, "predicted each change")
	check_eq(FileAccess.get_file_as_string(OfsLog.file_path()).count("guides: no level 3 prediction"), before + 1, "one app.error")
	check_eq(((guides.get_node("Paths") as MeshInstance3D).mesh as ArrayMesh).get_surface_count(), 0, "no paths")
	guides.free()


func test_a_table_version_spares_comparing_the_balls() -> void:
	var predictor := _predictor(FakeRecord.new())
	var guides := _guides(predictor)
	var balls := _balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0)])
	var setup := ShotSetup.new()
	guides.show_for(balls, 0, setup, 3, 7)
	balls[1]["x"] = 0.2
	predictor.clock.now += 1.0
	guides.show_for(balls, 0, setup, 3, 7)
	check_eq(predictor.calls.size(), 1, "the same version is the same table")
	predictor.clock.now += 1.0
	guides.show_for(balls, 0, setup, 3, 8)
	check_eq(predictor.calls.size(), 2, "a new version predicts again")
	check_near(float(predictor.calls[-1]["balls"][1]["x"]), 0.2, 1e-12, "with the table as it is")
	guides.free()


func test_the_pace_label_clears_the_balls_beside_the_cue_ball() -> void:
	var predictor := _predictor(FakeRecord.new())
	var guides := _guides(predictor)
	var label: Label3D = guides.get_node("PaceLabel")
	var setup := ShotSetup.new()
	# Aiming along +x, the player's right is -y in core, +x in Godot.
	guides.show_for(_balls([Vector2(-1.0, 0.0), Vector2(1.0, 0.5)]), 0, setup, 3)
	check_eq(label.horizontal_alignment, HORIZONTAL_ALIGNMENT_LEFT, "nothing beside it: on the right")
	check(label.position.x > 0.0, "right of the cue ball")
	predictor.clock.now += 1.0
	guides.show_for(_balls([Vector2(-1.0, 0.0), Vector2(-1.0, -0.15)]), 0, setup, 3)
	check(label.position.x < 0.0 and label.horizontal_alignment == HORIZONTAL_ALIGNMENT_RIGHT,
			"a ball on the right: the label goes left")
	predictor.clock.now += 1.0
	guides.show_for(_balls([Vector2(-1.0, 0.0), Vector2(-1.0, -0.15), Vector2(-1.0, 0.15)]), 0, setup, 3)
	check(label.position.y < 0.0 and label.horizontal_alignment == HORIZONTAL_ALIGNMENT_CENTER,
			"balls either side: under the cue ball, got %s" % label.position)
	guides.free()


func test_the_ghost_ring_is_a_tube_round_the_ghost_ball() -> void:
	var guides := _guides(_predictor(FakeRecord.new()))
	guides.show_for(_balls([Vector2(-1.0, 0.0), Vector2(0.0, 0.0)]), 0, ShotSetup.new(), 1)
	var ring := (guides.get_node("GhostRing") as MeshInstance3D).mesh as TorusMesh
	check(ring != null, "a torus, as thick from any angle")
	if ring != null:
		check_near((ring.inner_radius + ring.outer_radius) / 2.0, R, 1e-6, "round the ghost ball")
		check_near(ring.outer_radius - ring.inner_radius, 2.0 * Guides.RING_TUBE_RADIUS, 1e-6, "the tube's thickness")
	guides.free()


func test_aims_at_ball_is_true_only_when_a_ball_comes_first() -> void:
	var guides := Guides.new()
	guides.table_shape = _shape()
	guides.ball_radius = R
	var balls: Array[Dictionary] = [{"ball": "cue", "index": 0, "x": 0.0, "y": 0.0},
			{"ball": "red", "index": 0, "x": 0.5, "y": 0.0}]
	check(guides.aims_at_ball(balls, 0, 0.0), "straight at the red")
	check(guides.aims_at_ball(balls, 0, atan2(1.9 * R, 0.5)), "a thin edge of it")
	check(not guides.aims_at_ball(balls, 0, PI / 2.0), "away from it: a cushion first")
	check(not guides.aims_at_ball(balls, 0, PI), "behind the cue ball")
	guides.free()
