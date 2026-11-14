extends TestCase


func _rig() -> CameraRig:
	var rig := CameraRig.new()
	var cam := Camera3D.new()
	cam.name = "Camera3D"
	rig.add_child(cam)
	tree.root.add_child(rig)
	return rig


## Outside the tree, so it fits the phone's 1912 x 880 canvas whatever the test window is.
func _loose_rig() -> CameraRig:
	var rig := CameraRig.new()
	var cam := Camera3D.new()
	cam.name = "Camera3D"
	rig.add_child(cam)
	return rig


func _eye_xy(rig: CameraRig) -> Vector2:
	return TableSpace.godot_to_core(rig.transform.origin)


func _height(rig: CameraRig) -> float:
	return rig.transform.origin.y


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
		rig._process(CameraRig.BLEND_S)
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
				rig._process(CameraRig.BLEND_S)
				var eye := _eye_xy(rig)
				var floor_m := rig.clearance_at(eye)
				check(_height(rig) >= floor_m - 1e-6,
						"height %f >= %f at %s (cue %s, aim %f, pitch %f)" % [_height(rig), floor_m, eye, cue, aim, pitch])
				check(_height(rig) >= CameraRig.CLOTH_CLEARANCE_M - 1e-6, "above the balls")
	rig.set_pitch_deg(CameraRig.PITCH_MIN_DEG)
	rig.follow(Vector2(1.76, 0.0), PI)
	rig._process(CameraRig.BLEND_S)
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
	rig._process(CameraRig.BLEND_S)
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
						rig._process(CameraRig.BLEND_S)
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
	rig._process(CameraRig.BLEND_S)
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
		rig._process(CameraRig.BLEND_S)
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
		rig._process(CameraRig.BLEND_S)
		var to_camera := rig.transform.affine_inverse()
		for x: float in [bounds.position.x, bounds.end.x]:
			for z: float in [bounds.position.z, bounds.end.z]:
				var corner := to_camera * Vector3(x, bounds.end.y, z)
				var depth := -corner.z
				check(depth > 0.0 and absf(corner.x) < tan_h * depth * 0.97 and absf(corner.y) < tan_v * depth * 0.97,
						"rail corner %s in view for %s" % [Vector2(x, z), entry])
	rig.free()


func test_reset_view_returns_behind_the_cue_ball_after_a_shot() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.0), 0.0)
	rig.set_overview(true)
	rig._process(CameraRig.BLEND_S)
	rig.hold(true)
	rig.reset_view()
	rig.hold(false)
	rig._process(CameraRig.BLEND_S)
	check(not rig.is_overview(), "out of the overview")
	check_near(rig.pitch_deg(), CameraRig.DEFAULT_PITCH_DEG, 1e-9, "the default tilt")
	check_near(rig.zoom_scale(), 1.0, 1e-9, "the default zoom")
	check(rig.transform.is_equal_approx(rig.pose_for(Vector2(-1.0, 0.0), 0.0)), "behind the cue ball")
	rig.free()


const HudScript := preload("res://scripts/hud.gd")
const CANVAS := Vector2(1912.0, 880.0)
# iPhone 17 Pro Max landscape, as test_main.gd has it: 62 pt side insets either way round and the
# home indicator, on the same 1912 x 880 canvas.
const PHONE_SCREEN := Rect2(0, 0, 2868, 1320)
const PHONE_SAFE_AREAS: Array[Rect2] = [
	Rect2(186, 0, 2868 - 372, 1320 - 63),
	Rect2(186, 0, 2868 - 186, 1320 - 63),
	Rect2(0, 0, 2868 - 186, 1320 - 63),
]
## A blend's frame at 60 fps: the most the eye may move and the view may turn.
const STEP_MAX_M := 0.2
const TURN_MAX_DEG := 3.0
## The bottom of main.gd's notice line ("Drag to walk round the table"): 16 down, about 60 deep.
const NOTICE_BOTTOM := 80.0


## The table's outline as main.gd gives it to the rig.
func _table_bounds() -> AABB:
	var table := TableView.new()
	tree.root.add_child(table)
	table.build(Physics.table_shape(), OfsTable.dimensions())
	var bounds := table.bounds()
	table.free()
	return bounds


## The controls' rectangles: Windows (the whole canvas safe), then the phone either way round.
func _control_layouts() -> Array:
	var probe := FreeTableBar.new()
	tree.root.add_child(probe)
	var row := probe.row_rect().size
	probe.free()
	var safes: Array[Rect2] = [Rect2(Vector2.ZERO, CANVAS)]
	for safe_px in PHONE_SAFE_AREAS:
		safes.append(HudScript.safe_rect(safe_px, PHONE_SCREEN, CANVAS))
	var layouts: Array = []
	for safe in safes:
		var rects: Dictionary = HudScript.layout(safe, row, CANVAS)
		var controls: Array[Rect2] = []
		for key: String in ["bar", "spin", "elevation", "cue", "wheel"]:
			controls.append(rects[key])
		layouts.append(controls)
	return layouts


## [name, Godot point]: each pocket's drop centre on the cloth and each cushion's two ends.
func _markers() -> Array:
	var shape := Physics.table_shape()
	var found: Array = []
	for pocket: Dictionary in shape.get("pockets", []):
		var c: Array = pocket["drop_centre"]
		found.append(["pocket %s" % pocket["name"], TableSpace.core_to_godot(float(c[0]), float(c[1]))])
	for line: Dictionary in shape.get("lines", []):
		if line["kind"] == "nose":
			for end: String in ["p1", "p2"]:
				var p: Array = line[end]
				found.append(["cushion %s %s" % [line["name"], end],
						TableSpace.core_to_godot(float(p[0]), float(p[1]), 0.037)])
	return found


func _on_canvas(at: Vector2) -> bool:
	return Rect2(Vector2.ZERO, CANVAS).has_point(at)


func _under(at: Vector2, controls: Array[Rect2]) -> String:
	for rect in controls:
		if rect.has_point(at):
			return str(rect)
	return ""


func _rail_corners_in_view(rig: CameraRig, pose: Transform3D, what: String, bounds := Stage.DEFAULT_BOUNDS) -> void:
	for i in 8:
		var corner := bounds.get_endpoint(i)
		var at := rig.project(pose, Vector3(corner.x, bounds.end.y, corner.z), CameraRig.BROADCAST_FOV_DEG)
		check(_on_canvas(at), "%s: rail corner %s on screen at %s" % [what, corner, at])


## The projected width of the outline's rail-top edge at the end the view at `pose` stands behind.
func _near_rail_width(rig: CameraRig, pose: Transform3D, bounds: AABB, yaw: float) -> float:
	var z := bounds.end.z if cos(yaw) > 0.0 else bounds.position.z
	return rig.project(pose, Vector3(bounds.end.x, bounds.end.y, z), CameraRig.BROADCAST_FOV_DEG).distance_to(
			rig.project(pose, Vector3(bounds.position.x, bounds.end.y, z), CameraRig.BROADCAST_FOV_DEG))


func test_the_broadcast_view_shows_every_pocket_and_cushion_end_clear_of_the_controls() -> void:
	# KaiC: "Make the table bigger", from a TV frame whose near rail spans about 65 % of its width.
	var rig := _loose_rig()
	var bounds := _table_bounds()
	rig.set_table_bounds(bounds)
	var markers := _markers()
	check_eq(markers.size(), 6 + 12, "six pockets and six cushions' ends")
	check(CameraRig.BROADCAST_PITCH_DEG >= 25.0 and CameraRig.BROADCAST_PITCH_DEG <= 35.0, "the TV camera's low angle")
	for controls: Array[Rect2] in _control_layouts():
		rig.keep_clear(controls)
		for yaw: float in [0.0, PI]:
			var pose := rig.broadcast_pose(yaw)
			var tops: Array[float] = []
			for marker: Array in markers:
				var at := rig.project(pose, marker[1], CameraRig.BROADCAST_FOV_DEG)
				tops.append(at.y)
				check(_on_canvas(at), "%s on screen at %s (yaw %.2f)" % [marker[0], at, yaw])
				var under := _under(at, controls)
				check(under.is_empty(), "%s at %s not under the control %s (yaw %.2f)" % [marker[0], at, under, yaw])
				check(at.y > NOTICE_BOTTOM, "%s at %s below the notice line (yaw %.2f)" % [marker[0], at, yaw])
			_rail_corners_in_view(rig, pose, "yaw %.2f" % yaw, bounds)
			var forward := -pose.basis.z
			check_near(rad_to_deg(asin(-forward.y)), CameraRig.BROADCAST_PITCH_DEG, 1e-4, "looking down at the TV angle")
			check_near(_core_dir(forward).dot(Vector2(cos(yaw), sin(yaw))), 1.0, 1e-9, "along the table")
			var near_rail := _near_rail_width(rig, pose, bounds, yaw)
			# On the long lens the table fills the height first.
			check(near_rail > CANVAS.x * 0.5 and near_rail < CANVAS.x * 0.7,
					"the near rail over half the width: %.0f (yaw %.2f, %s)" % [near_rail, yaw, controls])
			var near_end := TableSpace.core_to_godot(-1.7845 * cos(yaw), 0.0)
			var far_end := TableSpace.core_to_godot(1.7845 * cos(yaw), 0.0)
			var side := Vector3(0.889, 0.0, 0.0)
			var near_width := rig.project(pose, near_end + side, CameraRig.BROADCAST_FOV_DEG).distance_to(rig.project(pose, near_end - side, CameraRig.BROADCAST_FOV_DEG))
			var far_width := rig.project(pose, far_end + side, CameraRig.BROADCAST_FOV_DEG).distance_to(rig.project(pose, far_end - side, CameraRig.BROADCAST_FOV_DEG))
			# TV's long lens: the far end about 0.7 of the near, where the aiming lens made it 0.4.
			check(far_width < near_width * 0.85 and far_width > near_width * 0.55,
					"the far end narrower, but not by much: %.0f vs %.0f" % [far_width, near_width])
			check(rig.project(pose, near_end, CameraRig.BROADCAST_FOV_DEG).y > rig.project(pose, far_end, CameraRig.BROADCAST_FOV_DEG).y, "the near cushion at the bottom")
			check(tops.max() - tops.min() > CANVAS.y * 0.7, "the table fills the height: %.0f" % [tops.max() - tops.min()])
	rig.free()


func test_the_walk_round_fits_the_table_clear_of_the_controls_at_every_yaw() -> void:
	var rig := _loose_rig()
	var bounds := _table_bounds()
	rig.set_table_bounds(bounds)
	var markers := _markers()
	var pockets := markers.slice(0, 6)
	for controls: Array[Rect2] in _control_layouts():
		rig.keep_clear(controls)
		var last := rig.broadcast_pose(0.0)
		var side_on := 0.0
		var jumps: Array[String] = []
		var faults: Array[String] = []
		for k in range(1, 361):
			var yaw := deg_to_rad(k)
			var pose := rig.broadcast_pose(yaw)
			for marker: Array in markers:
				var at := rig.project(pose, marker[1], CameraRig.BROADCAST_FOV_DEG)
				if not _on_canvas(at) or not _under(at, controls).is_empty():
					faults.append("%s at %s (yaw %d)" % [marker[0], at, k])
			for marker: Array in pockets:
				var at := rig.project(pose, marker[1], CameraRig.BROADCAST_FOV_DEG)
				if at.y <= NOTICE_BOTTOM:
					faults.append("%s at %s under the notice line (yaw %d)" % [marker[0], at, k])
			for i in 8:
				var corner := bounds.get_endpoint(i)
				var at := rig.project(pose, Vector3(corner.x, bounds.end.y, corner.z), CameraRig.BROADCAST_FOV_DEG)
				if not _on_canvas(at):
					faults.append("rail corner %s at %s (yaw %d)" % [corner, at, k])
			# A degree round is about 7 cm along the walk. A fit springing in or out, as one did with a
			# pocket slipping round a control's corner, moves the eye several times that in one step.
			var reach := Vector2(pose.origin.x, pose.origin.z).length() - Vector2(last.origin.x, last.origin.z).length()
			var rise := pose.origin.y - last.origin.y
			if absf(reach) > 0.1 or absf(rise) > 0.1:
				jumps.append("yaw %d: %.3f m out, %.3f m up" % [k, reach, rise])
			if k == 90:
				var left := rig.project(pose, Vector3(bounds.end.x, bounds.end.y, bounds.end.z), CameraRig.BROADCAST_FOV_DEG)
				var right := rig.project(pose, Vector3(bounds.end.x, bounds.end.y, bounds.position.z), CameraRig.BROADCAST_FOV_DEG)
				side_on = left.distance_to(right)
			last = pose
		check_eq(faults, [] as Array[String], "every pocket, cushion end and rail corner in view and clear (%s)" % [controls])
		check_eq(jumps, [] as Array[String], "the fit changes smoothly with the walk (%s)" % [controls])
		check(side_on > CANVAS.x * 0.6, "side-on the long rail across most of the screen: %.0f (%s)" % [side_on, controls])
	rig.free()


func test_the_broadcast_end_is_behind_the_shot() -> void:
	var rig := _rig()
	for entry: Array in [[0.3, 0.0], [-1.2, 0.0], [2.5, PI], [-2.0, PI], [PI / 2.0 + 0.05, PI]]:
		var aim: float = entry[0]
		rig.follow(Vector2(-0.5, 0.2), aim)
		rig._process(CameraRig.BLEND_S)
		rig.hold(true)
		rig._process(CameraRig.BLEND_S)
		check(not rig.is_easing(), "settled")
		check(rig.transform.is_equal_approx(rig.broadcast_pose(entry[1])), "the broadcast view for aim %.2f" % aim)
		var eye := _eye_xy(rig)
		if entry[1] == 0.0:
			check(eye.x < -1.95, "aiming towards the black: the baulk end (%s)" % eye)
		else:
			check(eye.x > 1.95, "aiming towards baulk: the black end (%s)" % eye)
		check(_height(rig) > 1.0, "looking down from above the table: %.2f m" % _height(rig))
		rig.follow(Vector2(0.6, -0.2), aim + 1.0)
		check(rig.transform.is_equal_approx(rig.broadcast_pose(entry[1])), "steady while the shot plays")
		rig.hold(false)
		rig._process(CameraRig.BLEND_S)
	rig.free()


## Steps a blend frame by frame at 60 fps until it settles: [eye steps, turn steps in degrees,
## clearance failures].
func _run_blend(rig: CameraRig, frames: int) -> Array:
	var steps: Array[float] = []
	var turns: Array[float] = []
	var low: Array[String] = []
	var last := rig.transform
	for i in frames:
		rig._process(1.0 / 60.0)
		steps.append(rig.transform.origin.distance_to(last.origin))
		turns.append(rad_to_deg((-rig.transform.basis.z).angle_to(-last.basis.z)))
		last = rig.transform
		var floor_m := rig.clearance_at(_eye_xy(rig))
		if _height(rig) < floor_m - 1e-9:
			low.append("frame %d: %.3f m at %s, under %.2f" % [i, _height(rig), _eye_xy(rig), floor_m])
		if not rig.is_easing():
			break
	return [steps, turns, low]


func _check_smooth(run: Array, what: String) -> void:
	var steps: Array[float] = run[0]
	var turns: Array[float] = run[1]
	check(absf(steps.size() - CameraRig.BLEND_S * 60.0) <= 3.0, "%s: BLEND_S long, %d frames" % [what, steps.size()])
	var mid := steps.size() / 2
	check(steps.max() < STEP_MAX_M, "%s: at most %.3f m a frame" % [what, steps.max()])
	check(turns.max() < TURN_MAX_DEG, "%s: at most %.2f° a frame" % [what, turns.max()])
	for k in 3:
		check(steps[k] < steps[mid] and steps[-1 - k] < steps[mid], "%s: slow at both ends (%.4f, %.4f vs %.4f mid)"
				% [what, steps[k], steps[-1 - k], steps[mid]])
		check(turns[k] <= maxf(turns[mid], 0.05) and turns[-1 - k] <= maxf(turns[mid], 0.05),
				"%s: turning slowly at both ends (%.3f, %.3f vs %.3f mid)" % [what, turns[k], turns[-1 - k], turns[mid]])
	check_eq(run[2], [] as Array[String], "%s: never under the cloth's or rails' clearance" % what)


func test_the_shot_blends_up_to_the_broadcast_view_and_back_down_smoothly() -> void:
	var rig := _rig()
	# Low behind the cue ball, zoomed in, near a cushion: the hardest starts.
	var starts: Array = [[Vector2(-1.05, -0.146), 0.02], [Vector2(1.70, 0.8), PI - 0.2], [Vector2(0.0, -0.85), PI / 2.0],
			[Vector2(-1.7, 0.0), 0.0], [Vector2(0.9, 0.3), -2.0]]
	for pitch: float in [CameraRig.PITCH_MIN_DEG, CameraRig.DEFAULT_PITCH_DEG]:
		for zoom: float in [CameraRig.ZOOM_MIN, 1.0]:
			rig.set_pitch_deg(pitch)
			rig.set_zoom_scale(zoom)
			for start: Array in starts:
				rig.follow(start[0], start[1])
				rig._process(CameraRig.BLEND_S)
				var what := "cue %s aim %.2f pitch %.0f zoom %.2f" % [start[0], start[1], pitch, zoom]
				rig.hold(true)
				_check_smooth(_run_blend(rig, 120), "up, " + what)
				check(rig.transform.is_equal_approx(rig.broadcast_pose(CameraRig.end_behind(start[1]))),
						"at the broadcast view, " + what)
				rig.hold(false)
				_check_smooth(_run_blend(rig, 120), "down, " + what)
				check(rig.transform.is_equal_approx(rig.pose_for(start[0], start[1])), "behind the cue ball again, " + what)
	check(not _camera_active(), "settled")
	rig.free()


func test_a_shot_finished_mid_blend_turns_back_without_a_jump() -> void:
	var rig := _rig()
	for frames_up: int in [6, 18, 30, 50]:
		rig.follow(Vector2(-1.05, -0.146), 0.1)
		rig._process(CameraRig.BLEND_S)
		rig.hold(true)
		var last := rig.transform.origin
		var before := 0.0
		for i in frames_up:
			rig._process(1.0 / 60.0)
			before = rig.transform.origin.distance_to(last)
			last = rig.transform.origin
		# The aim stays as struck; the cue ball has run on.
		rig.follow(Vector2(0.4, 0.3), 0.1)
		rig.hold(false)
		var run := _run_blend(rig, 120)
		var steps: Array[float] = run[0]
		check(absf(steps[0] - before) < 0.012,
				"after %d frames: the first step back %.4f m, the last one up %.4f m" % [frames_up, steps[0], before])
		check(steps.max() < STEP_MAX_M, "after %d frames: at most %.3f m a frame" % [frames_up, steps.max()])
		var turns: Array[float] = run[1]
		check(turns.max() < TURN_MAX_DEG, "after %d frames: turning gently, at most %.2f° a frame" % [frames_up, turns.max()])
		check_eq(run[2], [] as Array[String], "after %d frames: clear of the cloth and rails" % frames_up)
		check(not rig.is_easing() and rig.transform.is_equal_approx(rig.pose_for(Vector2(0.4, 0.3), 0.1)),
				"after %d frames: behind the cue ball where it stopped" % frames_up)
	rig.free()


func test_hold_ignores_pinch_and_tilt_and_follow_waits_for_the_release() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.0), 0.0)
	rig.hold(true)
	check(rig.is_easing() and _camera_active(), "blending to the broadcast view keeps the game drawing")
	rig.follow(Vector2(0.6, 0.2), 1.0)
	rig.pinch(2.0)
	rig.tilt(100.0)
	check_eq(rig.zoom_scale(), 1.0, "pinch ignored while held")
	check_eq(rig.pitch_deg(), CameraRig.DEFAULT_PITCH_DEG, "tilt ignored while held")
	rig._process(CameraRig.BLEND_S)
	check(rig.transform.is_equal_approx(rig.broadcast_pose(0.0)), "the broadcast view behind the shot")
	check(not _camera_active(), "held still there, the game can idle")
	rig.hold(false)
	check(rig.is_easing(), "returns behind the cue ball with a blend")
	rig._process(CameraRig.BLEND_S)
	check(rig.transform.is_equal_approx(rig.pose_for(Vector2(0.6, 0.2), 1.0)), "behind the new position")
	check(not _camera_active(), "settled")
	rig.free()


func test_a_drag_walks_round_the_table_in_the_overview() -> void:
	var rig := _loose_rig()
	var controls: Array[Rect2] = _control_layouts()[0]
	rig.keep_clear(controls)
	var cue := Vector2(-1.0, 0.1)
	rig.follow(cue, 0.2)
	rig.orbit(300.0)
	check(rig.transform.is_equal_approx(rig.pose_for(cue, 0.2)), "no walking while aiming")
	rig.set_overview(true)
	rig._process(CameraRig.BLEND_S)
	check(rig.transform.is_equal_approx(rig.broadcast_pose(0.0)), "the broadcast view from baulk")
	var quarter := (PI / 2.0) / CameraRig.ORBIT_RAD_PER_PX
	var start_eye := _eye_xy(rig)
	for i in 30:
		rig.orbit(-quarter / 30.0)
	check(not rig.is_easing(), "a drag moves it at once")
	check_near(_core_dir(-rig.transform.basis.z).dot(Vector2(0.0, 1.0)), 1.0, 1e-6,
			"a quarter turn: side-on, looking across the table")
	var eye := _eye_xy(rig)
	check(eye.y < -1.1 and absf(eye.x) < 0.2, "walked round to the side: %s from %s" % [eye, start_eye])
	check_near(rad_to_deg(asin(-(-rig.transform.basis.z).y)), CameraRig.BROADCAST_PITCH_DEG, 1e-4, "at the same angle")
	for marker: Array in _markers():
		var at := rig.project(rig.transform, marker[1], CameraRig.BROADCAST_FOV_DEG)
		check(_on_canvas(at) and _under(at, controls).is_empty(), "side-on: %s in view and clear at %s" % [marker[0], at])
	_rail_corners_in_view(rig, rig.transform, "side-on")
	var centre := rig.project(rig.transform, Vector3.ZERO, CameraRig.BROADCAST_FOV_DEG)
	check(centre.distance_to(CANVAS / 2.0) < 120.0, "round the table's centre: %s" % centre)
	var distance := rig.transform.origin.length()
	rig.pinch(1.5)
	check(rig.is_overview(), "a pinch zooms the overview")
	check(rig.transform.origin.length() < distance * 0.85, "in: %.2f m from %.2f" % [rig.transform.origin.length(), distance])
	rig.pinch(0.01)
	check(rig.transform.origin.length() > distance * 1.1, "and out, to its limit")
	check_eq(rig.zoom_scale(), 1.0, "the aiming zoom left alone")
	rig.set_overview(false)
	rig._process(CameraRig.BLEND_S)
	check(rig.transform.is_equal_approx(rig.pose_for(cue, 0.2)), "the eye again: behind the cue ball at the aim it had")
	rig.free()


func test_overview_shows_the_broadcast_view_and_back() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.0), 2.9)
	rig.set_pitch_deg(30.0)
	rig.set_zoom_scale(0.6)
	var changes: Array[bool] = []
	rig.overview_changed.connect(func(on: bool) -> void: changes.append(on))
	rig.set_overview(true)
	check(rig.is_overview() and rig.view_name() == "overview", "the overview")
	check(rig.pitch_deg() == 30.0 and rig.zoom_scale() == 0.6, "the aiming tilt and zoom kept for later")
	check(rig.is_easing() and _camera_active(), "blended into")
	rig.follow(Vector2(-1.0, 0.0), 3.0)
	rig._process(CameraRig.BLEND_S)
	check(rig.transform.is_equal_approx(rig.broadcast_pose(PI)), "from the end behind the aim, the black end")
	check(not _camera_active(), "settled")
	rig.set_overview(false)
	rig._process(CameraRig.BLEND_S)
	check(rig.transform.is_equal_approx(rig.pose_for(Vector2(-1.0, 0.0), 3.0)), "back behind the cue ball at the latest aim")
	rig.set_overview(true)
	rig._process(CameraRig.BLEND_S)
	rig.tilt(-20.0)
	check(not rig.is_overview() and rig.is_easing(), "a tilt leaves it, blending")
	rig._process(CameraRig.BLEND_S)
	check_near(rig.pitch_deg(), 26.0, 1e-9, "tilted from where it was")
	check(rig.transform.is_equal_approx(rig.pose_for(Vector2(-1.0, 0.0), 3.0)), "behind the cue ball")
	check_eq(changes, [true, false, true, false], "overview_changed each time")
	rig.set_overview(true)
	rig.hold(true)
	check(not rig.is_overview() and rig.view_name() == "shot", "a shot takes over from the overview")
	check_eq(changes, [true, false, true, false, true, false], "and says so")
	rig.set_overview(true)
	check(not rig.is_overview(), "not while a shot holds the camera")
	rig.free()


func test_the_tv_view_changes_to_its_long_lens_smoothly_and_back() -> void:
	# A TV main camera's long lens keeps the far end wide; the lens turns with the blend, not at once.
	var rig := _loose_rig()
	rig.follow(Vector2(-1.0, 0.1), 0.2)
	check_eq(rig.camera().fov, CameraRig.FOV_DEG, "aiming on the normal lens")
	rig.set_overview(true)
	var last := rig.camera().fov
	var largest_step := 0.0
	for i in 60:
		rig._process(CameraRig.BLEND_S / 60.0)
		largest_step = maxf(largest_step, absf(rig.camera().fov - last))
		last = rig.camera().fov
	check_eq(rig.camera().fov, CameraRig.BROADCAST_FOV_DEG, "the TV view on its long lens")
	check(largest_step < (CameraRig.FOV_DEG - CameraRig.BROADCAST_FOV_DEG) / 20.0, "turned gradually: %.2f deg at most a frame" % largest_step)
	rig.set_overview(false)
	rig._process(CameraRig.BLEND_S / 2.0)
	check(rig.camera().fov > CameraRig.BROADCAST_FOV_DEG and rig.camera().fov < CameraRig.FOV_DEG, "halfway back: %.1f" % rig.camera().fov)
	rig._process(CameraRig.BLEND_S)
	check_eq(rig.camera().fov, CameraRig.FOV_DEG, "back on the normal lens")
	rig.free()
