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


## The camera's distance from the cue's surface, the cue drawn as CueView draws it.
func _clearance_from_cue(rig: CameraRig, cue_xy: Vector2, aim: float, elevation: float) -> float:
	var pose := CueView.pose(cue_xy, aim, elevation, Vector2.ZERO, 0.0)
	var back := pose.basis.z
	var eye := rig.camera().global_position
	var along := clampf((eye - pose.origin).dot(back), 0.0, CueView.LENGTH)
	var nearest := pose.origin + back * along
	return eye.distance_to(nearest) - CueView.BUTT_DIAMETER / 2.0


func test_a_raised_cue_lifts_the_camera_above_it() -> void:
	var rig := _rig()
	var cue := Vector2(-1.758, 0.2)
	rig.follow(cue, 0.0)
	var flat := rig.transform
	rig.set_cue_elevation(deg_to_rad(2.0))
	check(rig.transform.is_equal_approx(flat), "a cue barely raised leaves the view alone")
	var elevation := deg_to_rad(41.0)
	rig.set_cue_elevation(elevation)
	rig._process(1.0)
	check_near(rig.pitch_for(elevation), 41.0 + CameraRig.ABOVE_CUE_DEG, 1e-9, "pitched above the cue")
	check_eq(rig.pitch_deg(), CameraRig.DEFAULT_PITCH_DEG, "the player's tilt is kept")
	check_near(-_forward(rig).y, sin(deg_to_rad(41.0 + CameraRig.ABOVE_CUE_DEG)), 1e-6, "looking down that steeply")
	check(_clearance_from_cue(rig, cue, 0.0, elevation) > 0.3, "well clear of the butt")
	var to_cue := (TableSpace.core_to_godot(cue.x, cue.y, 0.02625) - rig.camera().global_position).normalized()
	var half_fov := deg_to_rad(CameraRig.FOV_DEG / 2.0)
	check(to_cue.y < _forward(rig).y and to_cue.angle_to(_forward(rig)) < half_fov * 0.9,
			"the cue ball still low in view")
	rig.set_pitch_deg(80.0)
	check_near(-_forward(rig).y, sin(deg_to_rad(80.0)), 0.02, "a higher tilt wins")
	rig.free()


func test_a_raised_cue_never_comes_near_the_camera() -> void:
	var rig := _rig()
	var cues: Array[Vector2] = [Vector2(-1.758, 0.2), Vector2(0.4, -0.3), Vector2(1.7, 0.85)]
	for zoom: float in [CameraRig.ZOOM_MIN, 1.0, CameraRig.ZOOM_MAX]:
		rig.set_zoom_scale(zoom)
		for pitch: float in [CameraRig.PITCH_MIN_DEG, CameraRig.DEFAULT_PITCH_DEG, 30.0]:
			rig.set_pitch_deg(pitch)
			for elevation: float in [0.25, 0.45, 0.6, 0.75]:
				for cue in cues:
					for aim: float in [0.0, 2.2, -1.0]:
						rig.set_cue_elevation(elevation)
						rig.follow(cue, aim)
						rig._process(1.0)
						var gap := _clearance_from_cue(rig, cue, aim, elevation)
						check(gap > 0.1, "%.3f m from the cue (zoom %.2f, pitch %.0f, elevation %.2f, cue %s, aim %.1f)"
								% [gap, zoom, pitch, elevation, cue, aim])
	rig.free()


func test_forced_elevation_eases_and_dragged_elevation_is_immediate() -> void:
	var rig := _rig()
	var cue := Vector2(0.2, 0.1)
	rig.follow(cue, 0.5)
	rig.set_cue_elevation(0.7)
	check(rig.is_easing(), "a jump of the cue over a ball eases")
	rig._process(CameraRig.EASE_S + 0.01)
	check(not rig.is_easing(), "settled")
	check(rig.transform.is_equal_approx(rig.pose_for(cue, 0.5, 0.7)), "above the raised cue")
	rig.set_cue_elevation(0.72)
	check(not rig.is_easing(), "a drag-sized change")
	check(rig.transform.is_equal_approx(rig.pose_for(cue, 0.5, 0.72)), "applies at once")
	rig.hold(true)
	rig.set_cue_elevation(0.0)
	check(rig.transform.is_equal_approx(rig.pose_for(cue, 0.5, 0.72)), "held still during a shot")
	rig.hold(false)
	check(rig.is_easing(), "eases back down after it")
	rig._process(1.0)
	check(rig.transform.is_equal_approx(rig.pose_for(cue, 0.5, 0.0)), "back at cue level")
	rig.set_cue_elevation(NAN)
	check_eq(rig.cue_elevation(), 0.0, "nonsense ignored")
	rig.free()


func test_the_planning_view_frames_the_whole_table() -> void:
	var rig := _rig()
	rig.set_pitch_deg(CameraRig.PITCH_MAX_DEG)
	var half_fov := tan(deg_to_rad(CameraRig.FOV_DEG / 2.0))
	var bounds := Stage.DEFAULT_BOUNDS
	for entry: Array in [[Vector2(-1.0, 0.1), 0.0], [Vector2(1.5, -0.4), PI], [Vector2(0.0, 0.6), PI / 2.0]]:
		rig.follow(entry[0], entry[1])
		rig._process(1.0)
		var to_camera := rig.camera().global_transform.affine_inverse()
		for x: float in [bounds.position.x, bounds.end.x]:
			for z: float in [bounds.position.z, bounds.end.z]:
				var corner := to_camera * Vector3(x, bounds.end.y, z)
				check(absf(corner.y / -corner.z) < half_fov * 0.97,
						"rail corner %s inside the view's height for %s" % [Vector2(x, z), entry])
	rig.free()


func test_the_planning_view_takes_in_the_whole_table_at_the_phones_aspect() -> void:
	# KaiC couldn't see the whole table: the overview frames all of it, rails and six pockets.
	var rig := _rig()
	rig.set_pitch_deg(CameraRig.PITCH_MAX_DEG)
	var tan_v := tan(deg_to_rad(CameraRig.FOV_DEG / 2.0))
	var tan_h := tan_v * CameraRig.DEFAULT_ASPECT
	var bounds := Stage.DEFAULT_BOUNDS
	var entries: Array = []
	for k in 24:
		entries.append([Vector2(-1.2 + 0.1 * k, 0.7 - 0.06 * k), TAU * k / 24.0])
	for entry: Array in entries:
		rig.follow(entry[0], entry[1])
		rig._process(1.0)
		var to_camera := rig.transform.affine_inverse()
		for x: float in [bounds.position.x, bounds.end.x]:
			for z: float in [bounds.position.z, bounds.end.z]:
				var corner := to_camera * Vector3(x, bounds.end.y, z)
				var depth := -corner.z
				check(depth > 0.0 and absf(corner.x) < tan_h * depth * 0.97 and absf(corner.y) < tan_v * depth * 0.97,
						"rail corner %s in view for %s" % [Vector2(x, z), entry])
	rig.free()


func test_overview_goes_to_the_planning_view_and_back() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.0), 0.0)
	rig.set_pitch_deg(30.0)
	rig.set_zoom_scale(0.6)
	var changes: Array[bool] = []
	rig.overview_changed.connect(func(on: bool) -> void: changes.append(on))
	rig.set_overview(true)
	check(rig.is_overview() and rig.pitch_deg() == CameraRig.PITCH_MAX_DEG and rig.zoom_scale() == 1.0, "the planning view")
	check(rig.is_easing() and _camera_active(), "eased into")
	rig._process(1.0)
	check(rig.transform.is_equal_approx(rig.pose_for(Vector2(-1.0, 0.0), 0.0)), "there")
	check(not _camera_active(), "settled")
	rig.set_overview(false)
	check_near(rig.pitch_deg(), 30.0, 1e-9, "back to the tilt before")
	check_near(rig.zoom_scale(), 0.6, 1e-6, "and the zoom")
	rig._process(1.0)
	rig.set_overview(true)
	rig.tilt(-20.0)
	check(not rig.is_overview(), "a tilt leaves it")
	check_eq(changes, [true, false, true, false], "overview_changed each time")
	rig.hold(true)
	rig.set_overview(true)
	check(not rig.is_overview(), "not while a shot holds the camera")
	rig.free()


func test_watching_frames_the_points_and_glides() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.0), 0.0)
	var points := PackedVector2Array([Vector2(-0.9, 0.1), Vector2(1.2, 0.6), Vector2(0.4, -0.5)])
	rig.watch(points)
	check(not rig.is_watching(), "only while held")
	rig.hold(true)
	rig.watch(points)
	check(rig.is_watching() and _camera_active(), "watching keeps the game drawing")
	var framing := rig.watch_framing(points, 0.0)
	check(float(framing["pitch_deg"]) >= CameraRig.WATCH_PITCHES_DEG[0], "from above")
	var pose := rig.watch_pose(framing["focus"], framing["distance"], framing["pitch_deg"], 0.0)
	var to_camera := pose.affine_inverse()
	var tan_v := tan(deg_to_rad(CameraRig.FOV_DEG / 2.0))
	for p in points:
		var local := to_camera * TableSpace.core_to_godot(p.x, p.y, 0.02625)
		check(-local.z > 0.0 and absf(local.y) < tan_v * -local.z and absf(local.x) < tan_v * CameraRig.DEFAULT_ASPECT * -local.z,
				"%s in the framing" % p)
	check(pose.origin.distance_to(rig.transform.origin) > 0.5, "the camera starts where it was")
	for i in 120:
		rig._process(1.0 / 60.0)
	check(rig.transform.origin.distance_to(pose.origin) < 0.01, "and glides to the framing")
	var far := rig.watch_framing(PackedVector2Array([Vector2(-1.7, -0.85), Vector2(1.7, 0.85)]), 0.0)
	check(float(far["distance"]) <= CameraRig.FAR_DISTANCE_M, "never further out than the planning view")
	rig.hold(false)
	check(not rig.is_watching() and rig.is_easing(), "back with an ease")
	rig._process(CameraRig.RETURN_S)
	check(rig.transform.is_equal_approx(rig.pose_for(Vector2(-1.0, 0.0), 0.0)), "behind the cue ball")
	check(not _camera_active(), "and idle")
	rig.free()
