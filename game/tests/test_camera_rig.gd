extends TestCase


func _rig() -> CameraRig:
	var rig := CameraRig.new()
	var cam := Camera3D.new()
	cam.name = "Camera3D"
	rig.add_child(cam)
	tree.root.add_child(rig)
	return rig


func _eye_xy(rig: CameraRig) -> Vector2:
	return TableSpace.godot_to_core(rig.camera().global_position)


func _height(rig: CameraRig) -> float:
	return rig.camera().global_position.y


func _forward(rig: CameraRig) -> Vector3:
	return -rig.camera().global_transform.basis.z


## A Godot direction's horizontal part as a core direction.
func _core_dir(v: Vector3) -> Vector2:
	return Vector2(-v.z, -v.x).normalized()


func _camera_active() -> bool:
	return Perf._active.has(&"camera")


func test_behind_the_cue_ball_looking_along_the_aim() -> void:
	var rig := _rig()
	for entry: Array in [[Vector2(0.3, -0.2), 0.7], [Vector2(-1.0, 0.25), -2.5], [Vector2(0.0, 0.0), PI]]:
		var cue: Vector2 = entry[0]
		var aim: float = entry[1]
		var along := Vector2(cos(aim), sin(aim))
		rig.follow(cue, aim)
		rig._process(1.0)
		var back := (_eye_xy(rig) - cue).normalized()
		check_near(back.dot(along), -1.0, 1e-6, "camera behind the cue ball along -aim for %s" % [entry])
		check_near(_core_dir(_forward(rig)).dot(along), 1.0, 1e-6, "looking along the aim for %s" % [entry])
		check(_forward(rig).y < 0.0, "looking down")
	rig.free()


func test_cue_ball_sits_low_in_view() -> void:
	var rig := _rig()
	var cue := Vector2(-0.8, 0.1)
	rig.follow(cue, 0.0)
	var to_cue := (TableSpace.core_to_godot(cue.x, cue.y, 0.02625) - rig.camera().global_position).normalized()
	var forward := _forward(rig)
	check(to_cue.y < forward.y, "the cue ball is below the centre of view")
	var half_fov := deg_to_rad(CameraRig.FOV_DEG / 2.0)
	check(to_cue.angle_to(forward) < half_fov * 0.9, "and inside the view")
	check(to_cue.angle_to(forward) > half_fov * 0.3, "well below the centre")
	rig.free()


func test_high_pitch_still_looks_along_the_aim_and_pulls_out() -> void:
	var rig := _rig()
	var cue := Vector2(-1.0, 0.3)
	rig.follow(cue, 0.4)
	var low_distance := rig.camera().global_position.distance_to(TableSpace.core_to_godot(cue.x, cue.y))
	rig.set_pitch_deg(CameraRig.PITCH_MAX_DEG)
	check_near(_core_dir(_forward(rig)).dot(Vector2(cos(0.4), sin(0.4))), 1.0, 1e-6, "along the aim")
	check(_forward(rig).y < -0.95, "nearly straight down")
	var high_distance := rig.camera().global_position.distance_to(TableSpace.core_to_godot(cue.x, cue.y))
	check(high_distance > low_distance * 2.5, "pulled out to show the table")
	rig.free()


func test_tilt_down_raises_the_camera_and_clamps() -> void:
	var rig := _rig()
	rig.follow(Vector2.ZERO, 0.0)
	var pitch := rig.pitch_deg()
	var height := _height(rig)
	rig.tilt(50.0)
	check_near(rig.pitch_deg(), pitch + 50.0 * CameraRig.TILT_DEG_PER_PX, 1e-9, "pitch")
	check(_height(rig) > height, "dragging down raises the camera")
	rig.tilt(10000.0)
	check_eq(rig.pitch_deg(), CameraRig.PITCH_MAX_DEG, "clamped high")
	rig.tilt(-10000.0)
	check_eq(rig.pitch_deg(), CameraRig.PITCH_MIN_DEG, "clamped low")
	rig.free()


func test_zoom_scale_and_pinch_clamps() -> void:
	var rig := _rig()
	check_eq(rig.zoom_scale(), 1.0, "1 at the default distance")
	var cue := Vector2(0.2, 0.0)
	rig.follow(cue, 0.0)
	var cue_3d := TableSpace.core_to_godot(cue.x, cue.y, 0.02625)
	var far := rig.camera().global_position.distance_to(cue_3d)
	rig.pinch(2.0)
	check_near(rig.zoom_scale(), 0.5, 1e-12, "pinching out x2 halves it")
	check_near(rig.camera().global_position.distance_to(cue_3d), far * 0.5, 1e-4, "half as far from the cue ball")
	rig.pinch(100.0)
	check_eq(rig.zoom_scale(), CameraRig.ZOOM_MIN, "clamped in")
	rig.pinch(0.001)
	check_eq(rig.zoom_scale(), CameraRig.ZOOM_MAX, "clamped out")
	rig.pinch(0.0)
	rig.pinch(-1.0)
	check_eq(rig.zoom_scale(), CameraRig.ZOOM_MAX, "nonsense factors ignored")
	rig.free()


func test_never_under_the_cloth_or_in_a_rail() -> void:
	var rig := _rig()
	rig.set_zoom_scale(CameraRig.ZOOM_MIN)
	var cues: Array[Vector2] = [Vector2(1.76, 0.0), Vector2(-1.76, 0.86), Vector2(0.0, -0.87), Vector2(0.4, 0.2)]
	for pitch: float in [CameraRig.PITCH_MIN_DEG, 10.0, 30.0]:
		rig.set_pitch_deg(pitch)
		for cue in cues:
			for k in 8:
				var aim := k * TAU / 8.0
				rig.follow(cue, aim)
				rig._process(1.0)
				var eye := _eye_xy(rig)
				var floor_m := rig.clearance_at(eye)
				check(_height(rig) >= floor_m - 1e-6,
						"height %f >= %f at %s (cue %s, aim %f, pitch %f)" % [_height(rig), floor_m, eye, cue, aim, pitch])
				check(_height(rig) >= CameraRig.CLOTH_CLEARANCE_M - 1e-6, "above the balls")
	rig.set_pitch_deg(CameraRig.PITCH_MIN_DEG)
	rig.follow(Vector2(1.76, 0.0), PI)
	rig._process(1.0)
	check(_eye_xy(rig).x > 1.7845, "camera out over the black-end rail")
	check(_height(rig) >= CameraRig.RAIL_CLEARANCE_M - 1e-6, "and above it")
	rig.free()


func test_aim_drag_changes_are_immediate() -> void:
	var rig := _rig()
	var cue := Vector2(-0.5, 0.1)
	rig.follow(cue, 0.2)
	for i in 10:
		var aim := 0.2 + 0.03 * (i + 1)
		rig.follow(cue, aim)
		check(not rig.is_easing(), "no easing for a drag-sized change")
		check(rig.transform.is_equal_approx(rig.pose_for(cue, aim)), "at the new pose at once")
	rig.free()


func test_big_jump_eases_over_a_quarter_second() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.0), 0.0)
	var start := rig.transform
	var cue := Vector2(0.8, -0.3)
	rig.follow(cue, 2.0)
	check(rig.is_easing(), "easing")
	check(_camera_active(), "Perf keeps drawing while it eases")
	check(rig.transform.is_equal_approx(start), "hasn't moved yet")
	rig._process(CameraRig.EASE_S / 2.0)
	check(not rig.transform.is_equal_approx(start), "moving")
	check(not rig.transform.is_equal_approx(rig.pose_for(cue, 2.0)), "not there yet")
	rig._process(CameraRig.EASE_S / 2.0 + 0.001)
	check(not rig.is_easing(), "settled")
	check(not _camera_active(), "Perf can idle again")
	check(rig.transform.is_equal_approx(rig.pose_for(cue, 2.0)), "at the target")
	rig.free()


func test_aim_change_during_an_ease_ends_at_the_latest_target() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.0), 0.0)
	rig.follow(Vector2(0.5, 0.0), 0.0)
	rig._process(0.1)
	rig.follow(Vector2(0.5, 0.0), 0.1)
	rig._process(0.2)
	check(not rig.is_easing(), "settled")
	check(rig.transform.is_equal_approx(rig.pose_for(Vector2(0.5, 0.0), 0.1)), "at the latest target")
	rig.free()


func test_hold_freezes_then_eases_back() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.0), 0.0)
	var held := rig.transform
	rig.hold(true)
	rig.follow(Vector2(0.6, 0.2), 1.0)
	rig.pinch(2.0)
	rig.tilt(100.0)
	check(rig.transform.is_equal_approx(held), "frozen")
	check_eq(rig.zoom_scale(), 1.0, "pinch ignored while held")
	check_eq(rig.pitch_deg(), CameraRig.DEFAULT_PITCH_DEG, "tilt ignored while held")
	rig.hold(false)
	check(rig.is_easing(), "returns behind the cue ball with an ease")
	rig._process(1.0)
	check(rig.transform.is_equal_approx(rig.pose_for(Vector2(0.6, 0.2), 1.0)), "behind the new position")
	check(not _camera_active(), "settled")
	rig.free()


func test_hold_during_an_ease_stops_it() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.0), 0.0)
	rig.follow(Vector2(1.0, 0.0), 0.0)
	rig._process(0.05)
	rig.hold(true)
	check(not rig.is_easing(), "stopped")
	check(not _camera_active(), "Perf released")
	rig.hold(false)
	rig._process(1.0)
	check(rig.transform.is_equal_approx(rig.pose_for(Vector2(1.0, 0.0), 0.0)), "finishes after the hold")
	rig.free()


func test_moved_is_emitted_when_the_camera_moves() -> void:
	var rig := _rig()
	var count := [0]
	rig.moved.connect(func() -> void: count[0] += 1)
	rig.follow(Vector2.ZERO, 0.0)
	check_eq(count[0], 1, "follow")
	rig.follow(Vector2.ZERO, 0.0)
	check_eq(count[0], 1, "not when nothing changed")
	rig.pinch(1.2)
	rig.tilt(20.0)
	check_eq(count[0], 3, "pinch and tilt")
	rig.tilt(-100000.0)
	rig.tilt(-10.0)
	check_eq(count[0], 4, "not again once clamped")
	rig.free()


func test_camera_settings() -> void:
	var rig := _rig()
	check_eq(rig.camera().fov, CameraRig.FOV_DEG, "fov")
	check_eq(rig.camera().transform, Transform3D.IDENTITY, "the camera sits at the rig's origin")
	rig.free()
