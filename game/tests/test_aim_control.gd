extends TestCase


class Rig:
	var setup := ShotSetup.new()
	var gestures := Gestures.new()
	var camera_rig := CameraRig.new()
	var aim := AimControl.new()

	func _init() -> void:
		aim.bind(setup, gestures, camera_rig)

	func drag(from: Vector2, to: Vector2, steps: int = 1) -> void:
		var down := InputEventScreenTouch.new()
		down.position = from
		down.pressed = true
		gestures.feed(down)
		for i in range(1, steps + 1):
			var move := InputEventScreenDrag.new()
			move.position = from.lerp(to, float(i) / steps)
			gestures.feed(move)
		var up := InputEventScreenTouch.new()
		up.position = to
		gestures.feed(up)

	func free_all() -> void:
		gestures.free()
		camera_rig.free()
		aim.free()


func test_100_unit_drag_turns_100_times_rad_per_px() -> void:
	var r := Rig.new()
	var per_px := r.aim.rad_per_px()
	check_near(per_px, AimControl.BASE_RAD_PER_PX, 1e-15, "default rad_per_px")
	r.drag(Vector2(500, 400), Vector2(400, 400), 4)
	check_near(r.setup.aim_rad, 100.0 * per_px, 1e-12, "a 100-unit drag left")
	r.free_all()


func test_vertical_movement_does_not_turn() -> void:
	var r := Rig.new()
	r.drag(Vector2(500, 300), Vector2(500, 500), 5)
	check_eq(r.setup.aim_rad, 0.0, "aim")
	r.free_all()


func test_dragging_right_turns_clockwise_from_above() -> void:
	var r := Rig.new()
	r.drag(Vector2(400, 400), Vector2(500, 400))
	check(r.setup.aim_rad < 0.0, "aim_rad decreases")
	# From aim 0 (towards the black) a clockwise turn points to core -y: the player's right.
	check(sin(r.setup.aim_rad) < 0.0, "towards the player's right")
	r.free_all()


func test_half_zoom_halves_the_change() -> void:
	var r := Rig.new()
	r.camera_rig.set_zoom_scale(0.5)
	check_near(r.aim.rad_per_px(), AimControl.BASE_RAD_PER_PX * 0.5, 1e-15, "rad_per_px")
	r.drag(Vector2(500, 400), Vector2(400, 400))
	check_near(r.setup.aim_rad, 100.0 * AimControl.BASE_RAD_PER_PX * 0.5, 1e-12, "aim")
	r.free_all()


func test_sensitivity_scales_the_change() -> void:
	var before: Variant = Settings.get_value("aim_sensitivity")
	Settings.set_value("aim_sensitivity", 2.0)
	var r := Rig.new()
	var per_px := r.aim.rad_per_px()
	r.drag(Vector2(500, 400), Vector2(400, 400))
	Settings.set_value("aim_sensitivity", before)
	check_near(per_px, AimControl.BASE_RAD_PER_PX * 2.0, 1e-15, "rad_per_px")
	check_near(r.setup.aim_rad, 200.0 * AimControl.BASE_RAD_PER_PX, 1e-12, "aim")
	r.free_all()


func test_many_small_drags_add_up_without_snapping() -> void:
	var r := Rig.new()
	r.setup.set_aim(0.3)
	var expected := 0.3
	for i in 50:
		r.drag(Vector2(500, 400), Vector2(491, 400))
		expected += 9.0 * AimControl.BASE_RAD_PER_PX
	check_near(r.setup.aim_rad, expected, 1e-12, "aim")
	r.free_all()


func test_disabled_ignores_drags() -> void:
	var r := Rig.new()
	r.aim.enabled = false
	r.drag(Vector2(500, 400), Vector2(300, 400))
	check_eq(r.setup.aim_rad, 0.0, "aim")
	r.aim.enabled = true
	r.drag(Vector2(500, 400), Vector2(400, 400))
	check_near(r.setup.aim_rad, 100.0 * AimControl.BASE_RAD_PER_PX, 1e-12, "aim after re-enabling")
	r.free_all()
