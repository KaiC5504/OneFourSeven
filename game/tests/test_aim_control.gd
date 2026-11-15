extends TestCase

## What a drag's first move, the one that made the touch a drag, doesn't turn.
const SLOP := Gestures.DRAG_SLOP


class Rig:
	var setup := ShotSetup.new()
	var gestures := Gestures.new()
	var camera_rig := CameraRig.new()
	var aim := AimControl.new()
	## Seconds between the clock's readings: long by default, so no move counts as the lift's.
	var tick_s := 1.0
	var _now := 0.0

	func _init() -> void:
		aim.bind(setup, gestures, camera_rig)
		aim.clock = func() -> float:
			_now += tick_s
			return _now

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
	check_near(r.setup.aim_rad, (100.0 - SLOP) * per_px, 1e-12, "a 100-unit drag left, less the slop")
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
	check_near(r.setup.aim_rad, (100.0 - SLOP) * AimControl.BASE_RAD_PER_PX * 0.5, 1e-12, "aim")
	r.free_all()


func test_sensitivity_scales_the_change() -> void:
	var before: Variant = Settings.get_value("aim_sensitivity")
	Settings.set_value("aim_sensitivity", 2.0)
	var r := Rig.new()
	var per_px := r.aim.rad_per_px()
	r.drag(Vector2(500, 400), Vector2(400, 400))
	Settings.set_value("aim_sensitivity", before)
	check_near(per_px, AimControl.BASE_RAD_PER_PX * 2.0, 1e-15, "rad_per_px")
	check_near(r.setup.aim_rad, 2.0 * (100.0 - SLOP) * AimControl.BASE_RAD_PER_PX, 1e-12, "aim")
	r.free_all()


func test_many_small_drags_add_up_without_snapping() -> void:
	var r := Rig.new()
	r.setup.set_aim(0.3)
	var expected := 0.3
	for i in 50:
		r.drag(Vector2(500, 400), Vector2(491, 400))
		expected += (9.0 - SLOP) * AimControl.BASE_RAD_PER_PX
	check_near(r.setup.aim_rad, expected, 1e-12, "aim")
	r.free_all()


func test_disabled_ignores_drags() -> void:
	var r := Rig.new()
	r.aim.enabled = false
	r.drag(Vector2(500, 400), Vector2(300, 400))
	check_eq(r.setup.aim_rad, 0.0, "aim")
	r.aim.enabled = true
	r.drag(Vector2(500, 400), Vector2(400, 400))
	check_near(r.setup.aim_rad, (100.0 - SLOP) * AimControl.BASE_RAD_PER_PX, 1e-12, "aim after re-enabling")
	r.free_all()


func test_aim_is_finer_while_the_line_meets_a_ball() -> void:
	# KaiC: fine while aiming at a ball, full speed elsewhere.
	var r := Rig.new()
	var state := {"on_ball": false}
	r.aim.on_ball = func() -> bool: return state["on_ball"]
	r.drag(Vector2(500, 400), Vector2(400, 400))
	check_near(r.setup.aim_rad, (100.0 - SLOP) * AimControl.BASE_RAD_PER_PX, 1e-12, "full speed off a ball")
	state["on_ball"] = true
	var before := r.setup.aim_rad
	r.drag(Vector2(500, 400), Vector2(400, 400))
	check_near(r.setup.aim_rad - before, (100.0 - SLOP) * AimControl.BASE_RAD_PER_PX * AimControl.ON_BALL_SCALE, 1e-12,
			"finer on a ball")
	check(AimControl.ON_BALL_SCALE < 0.5, "clearly finer")
	r.free_all()


func test_fine_tune_sensitivity_sets_the_speed_on_a_ball_and_aim_sensitivity_the_rest() -> void:
	# KaiC: the fine-tune setting has to change the drag on the target ball.
	var r := Rig.new()
	var state := {"on_ball": true}
	r.aim.on_ball = func() -> bool: return state["on_ball"]
	var fine: Variant = Settings.get_value("fine_tune_sensitivity")
	var coarse: Variant = Settings.get_value("aim_sensitivity")
	Settings.set_value("fine_tune_sensitivity", 0.5)
	Settings.set_value("aim_sensitivity", 3.0)
	var on_ball := r.aim.rad_per_px()
	state["on_ball"] = false
	var off_ball := r.aim.rad_per_px()
	Settings.set_value("fine_tune_sensitivity", fine)
	Settings.set_value("aim_sensitivity", coarse)
	check_near(on_ball, AimControl.BASE_RAD_PER_PX * AimControl.ON_BALL_SCALE * 0.5, 1e-15, "on a ball: the fine-tune setting alone")
	check_near(off_ball, AimControl.BASE_RAD_PER_PX * 3.0, 1e-15, "off a ball: the aim setting alone")
	r.free_all()


func test_a_new_touch_starts_the_aim_from_rest() -> void:
	var r := Rig.new()
	# The touch becomes a drag 9 units out; only the unit past the slop turns the aim, and every
	# unit after it.
	r.drag(Vector2(500, 400), Vector2(491, 400))
	check_near(r.setup.aim_rad, (9.0 - SLOP) * AimControl.BASE_RAD_PER_PX, 1e-12, "no jump by the slop")
	r.setup.set_aim(0.0)
	r.drag(Vector2(500, 400), Vector2(480, 400), 20)
	check_near(r.setup.aim_rad, (20.0 - SLOP) * AimControl.BASE_RAD_PER_PX, 1e-12, "then one for one")
	r.free_all()


func test_the_fingers_roll_as_it_lifts_is_taken_back() -> void:
	# KaiC: the line moved by itself after he let go.
	var r := Rig.new()
	r.drag(Vector2(500, 400), Vector2(400, 400), 4)
	var settled := r.setup.aim_rad
	# A new touch, a slow correction, then 6 units of roll in the last moments before the lift.
	var down := InputEventScreenTouch.new()
	down.position = Vector2(500, 400)
	down.pressed = true
	r.gestures.feed(down)
	for x: float in [480.0, 470.0]:
		var move := InputEventScreenDrag.new()
		move.position = Vector2(x, 400)
		r.gestures.feed(move)
	var held := r.setup.aim_rad
	check(held != settled, "the correction turned the aim")
	# The finger rests before it lifts.
	r._now += 1.0
	r.tick_s = 0.008
	for x: float in [468.0, 466.0]:
		var move := InputEventScreenDrag.new()
		move.position = Vector2(x, 400)
		r.gestures.feed(move)
	check(r.setup.aim_rad != held, "the roll turns the aim while the finger is down")
	var up := InputEventScreenTouch.new()
	# The release itself reports a further jump.
	up.position = Vector2(464, 400)
	r.gestures.feed(up)
	check_near(r.setup.aim_rad, held, 1e-12, "and is taken back at the lift")
	r.free_all()


func test_a_drag_let_go_on_the_move_keeps_its_turn() -> void:
	var r := Rig.new()
	r.tick_s = 0.008
	r.drag(Vector2(500, 400), Vector2(440, 400), 6)
	check_near(r.setup.aim_rad, (60.0 - SLOP) * AimControl.BASE_RAD_PER_PX, 1e-12, "further than a roll: kept")
	r.free_all()
