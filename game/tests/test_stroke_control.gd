extends TestCase

const STEP := 1.0 / 120.0
const AREA := Vector2(300.0, 860.0)

var fake_t := 0.0
var _struck: Array[float] = []
var _cancelled: Array[String] = []
var _backswing: Array[float] = []


func _make(setup: ShotSetup = null) -> StrokeControl:
	var stroke := StrokeControl.new()
	tree.root.add_child(stroke)
	stroke.size = AREA
	stroke.clock = func() -> float: return fake_t
	stroke.wander_enabled = func() -> bool: return false
	stroke.bind(setup)
	stroke.struck.connect(func(speed: float) -> void: _struck.append(speed))
	stroke.cancelled.connect(func(reason: String) -> void: _cancelled.append(reason))
	stroke.backswing_changed.connect(func(m: float) -> void: _backswing.append(m))
	return stroke


func _touch(control: Control, pressed: bool, pos: Vector2, canceled: bool = false) -> void:
	var e := InputEventScreenTouch.new()
	e.index = 0
	e.pressed = pressed
	e.canceled = canceled
	e.position = pos
	control._gui_input(e)


func _drag(control: Control, pos: Vector2) -> void:
	var e := InputEventScreenDrag.new()
	e.index = 0
	e.position = pos
	control._gui_input(e)


## Moves the finger at a steady velocity (units/s) for `seconds`, one drag per 120 Hz frame.
func _move(control: Control, from: Vector2, velocity: Vector2, seconds: float) -> Vector2:
	var t0 := fake_t
	var steps := roundi(seconds / STEP)
	for i in range(1, steps + 1):
		fake_t = t0 + i * STEP
		_drag(control, from + velocity * (i * STEP))
	return from + velocity * (steps * STEP)


func _max_speed() -> float:
	return Physics.param("max_cue_speed_m_s", 10.0)


func test_steady_push_sets_the_speed() -> void:
	var setup := ShotSetup.new()
	var stroke := _make(setup)
	fake_t = 0.0
	var pos := Vector2(150.0, 300.0)
	_touch(stroke, true, pos)
	pos = _move(stroke, pos, Vector2(0.0, 200.0 / 0.3), 0.3)
	check_near(pos.y, 500.0, 0.01, "pulled back 200 units")
	pos = _move(stroke, pos, Vector2(0.0, -3000.0), 0.1)
	_touch(stroke, false, pos)
	var expected := StrokeControl.speed_for_push(3000.0, _max_speed())
	check_eq(_struck.size(), 1, "struck once")
	if _struck.size() == 1:
		check_near(_struck[0], expected, expected * 0.01, "struck speed")
	check_near(setup.speed_m_s, expected, expected * 0.01, "setup speed")
	check_eq(_cancelled.size(), 0, "not cancelled")
	# Touch-down, 36 pull frames, 12 push frames and the release.
	check_eq(setup.stroke_samples.size(), 50, "every event recorded")
	check_near(setup.stroke_samples[0][0], 0.0, 1e-9, "first sample time")
	check_near(setup.stroke_samples[49][2], pos.y, 1e-3, "last sample y")
	check(not stroke.is_stroking(), "stroke over")
	stroke.free()


func test_only_the_final_window_counts() -> void:
	var stroke := _make(ShotSetup.new())
	var pos := Vector2(150.0, 300.0)
	_touch(stroke, true, pos)
	pos = _move(stroke, pos, Vector2(0.0, 600.0), 0.25)
	pos = _move(stroke, pos, Vector2(0.0, -500.0), 0.2)
	pos = _move(stroke, pos, Vector2(0.0, -4000.0), 0.08)
	_touch(stroke, false, pos)
	var expected := StrokeControl.speed_for_push(4000.0, _max_speed())
	check_eq(_struck.size(), 1, "struck once")
	if _struck.size() == 1:
		check_near(_struck[0], expected, expected * 0.01, "the fast final push")
	stroke.free()


func test_window_start_is_interpolated() -> void:
	# Samples every 0.05 s: the window starts between two of them.
	var t := PackedFloat64Array([0.0, 0.05, 0.10, 0.15])
	var x := PackedFloat64Array([0.0, 0.0, 0.0, 0.0])
	var y := PackedFloat64Array([500.0, 400.0, 300.0, 200.0])
	var v := StrokeControl.finger_velocity(t, x, y, 0.08)
	check_near(v.y, 2000.0, 1e-6, "steady 2000 units/s")
	# A stroke shorter than the window: the finger rested where it touched down.
	var short := StrokeControl.finger_velocity(PackedFloat64Array([0.0, 0.04]),
			PackedFloat64Array([0.0, 0.0]), PackedFloat64Array([300.0, 260.0]), 0.08)
	check_near(short.y, 500.0, 1e-6, "short stroke over the whole window")


func test_dragging_out_cancels() -> void:
	var setup := ShotSetup.new()
	var stroke := _make(setup)
	var pos := Vector2(150.0, 600.0)
	_touch(stroke, true, pos)
	pos = _move(stroke, pos, Vector2(0.0, 1000.0), 0.4)
	check(pos.y > AREA.y + StrokeControl.LEAVE_MARGIN, "finger left the area")
	check_eq(_cancelled, [StrokeControl.LEFT_AREA] as Array[String], "cancelled on leaving")
	check(not stroke.is_stroking(), "stroke over")
	# The rest of that touch does nothing.
	pos = _move(stroke, pos, Vector2(0.0, -4000.0), 0.2)
	_touch(stroke, false, pos)
	check_eq(_struck.size(), 0, "never struck")
	check_eq(_cancelled.size(), 1, "cancelled once")
	check_eq(setup.speed_m_s, 0.0, "speed untouched")
	check_eq(_backswing[-1], 0.0, "cue back at the ball")
	stroke.free()


func test_leaving_sideways_cancels() -> void:
	var stroke := _make()
	var pos := Vector2(150.0, 400.0)
	_touch(stroke, true, pos)
	pos = _move(stroke, pos, Vector2(-1500.0, 0.0), 0.2)
	check_eq(_cancelled, [StrokeControl.LEFT_AREA] as Array[String], "cancelled")
	stroke.free()


func test_within_the_margin_carries_on() -> void:
	var stroke := _make()
	var pos := Vector2(150.0, 400.0)
	_touch(stroke, true, pos)
	pos = _move(stroke, pos, Vector2(0.0, 0.0), 0.1)
	_drag(stroke, Vector2(-StrokeControl.LEAVE_MARGIN + 5.0, 400.0))
	check(stroke.is_stroking(), "still stroking just outside the edge")
	stroke.free()


func test_release_without_push_cancels() -> void:
	var setup := ShotSetup.new()
	var stroke := _make(setup)
	var pos := Vector2(150.0, 300.0)
	_touch(stroke, true, pos)
	pos = _move(stroke, pos, Vector2(0.0, 600.0), 0.3)
	pos = _move(stroke, pos, Vector2.ZERO, 0.2)
	_touch(stroke, false, pos)
	check_eq(_struck.size(), 0, "no strike after holding still")
	check_eq(_cancelled, [StrokeControl.NO_PUSH] as Array[String], "no push")
	# Released while still pulling back.
	_touch(stroke, true, pos)
	pos = _move(stroke, pos, Vector2(0.0, 800.0), 0.2)
	_touch(stroke, false, pos)
	check_eq(_struck.size(), 0, "no strike on a pull")
	check_eq(_cancelled.size(), 2, "cancelled again")
	check_eq(setup.speed_m_s, 0.0, "speed untouched")
	stroke.free()


func test_a_tap_is_not_a_shot() -> void:
	var stroke := _make()
	_touch(stroke, true, Vector2(150.0, 300.0))
	fake_t += STEP
	_touch(stroke, false, Vector2(150.0, 297.0))
	check_eq(_struck.size(), 0, "a tap with a little jitter does not strike")
	stroke.free()


func test_cancelled_touch_cancels() -> void:
	var stroke := _make()
	var pos := Vector2(150.0, 300.0)
	_touch(stroke, true, pos)
	pos = _move(stroke, pos, Vector2(0.0, -3000.0), 0.1)
	_touch(stroke, false, pos, true)
	check_eq(_struck.size(), 0, "a cancelled touch never strikes")
	check_eq(_cancelled.size(), 1, "cancelled")
	stroke.free()


func test_wander_on() -> void:
	var errors: Array[float] = []
	for lateral: float in [0.0, 150.0, 300.0, -300.0, 1500.0]:
		var setup := ShotSetup.new()
		var stroke := _make(setup)
		stroke.wander_enabled = func() -> bool: return true
		var pos := Vector2(150.0, 300.0)
		_touch(stroke, true, pos)
		pos = _move(stroke, pos, Vector2(0.0, 600.0), 0.3)
		pos = _move(stroke, pos, Vector2(lateral, -3000.0), 0.1)
		_touch(stroke, false, pos)
		errors.append(setup.wander_rad)
		stroke.free()
	check_near(errors[0], 0.0, 1e-6, "a straight push has no error")
	check_near(errors[2], -StrokeControl.WANDER_GAIN * 0.1, 1e-4, "drifting right turns the aim right")
	check_near(errors[1], errors[2] / 2.0, 1e-5, "proportional to the sideways speed")
	check_near(errors[3], -errors[2], 1e-6, "drifting left turns it left")
	check_near(errors[4], -StrokeControl.MAX_WANDER_RAD, 1e-9, "bounded")


func test_wander_off() -> void:
	var setup := ShotSetup.new()
	setup.wander_rad = 0.5
	var stroke := _make(setup)
	var pos := Vector2(150.0, 300.0)
	_touch(stroke, true, pos)
	pos = _move(stroke, pos, Vector2(0.0, 600.0), 0.3)
	pos = _move(stroke, pos, Vector2(300.0, -3000.0), 0.1)
	_touch(stroke, false, pos)
	check_eq(_struck.size(), 1, "struck")
	check_eq(setup.wander_rad, 0.0, "no error with the setting off")
	stroke.free()


func test_wander_follows_the_setting_by_default() -> void:
	var stroke := StrokeControl.new()
	check_eq(stroke.wander_enabled.call(), Settings.get_value("stroke_wander") == true, "reads Settings")
	check_eq(Settings.DEFAULTS["stroke_wander"], false, "off by default")
	stroke.free()


func test_backswing_follows_the_pull() -> void:
	var stroke := _make()
	var pos := Vector2(150.0, 300.0)
	_touch(stroke, true, pos)
	pos = _move(stroke, pos, Vector2(0.0, 220.0 / 0.2), 0.2)
	check_near(_backswing[-1], 220.0 * StrokeControl.BACKSWING_M_PER_UNIT, 1e-6, "follows the finger")
	check_near(StrokeControl.backswing_for_pull(880.0), 0.25, 1e-9, "a full-height pull is 0.25 m")
	pos = _move(stroke, pos, Vector2(0.0, -400.0 / 0.1), 0.1)
	check_eq(_backswing[-1], 0.0, "pushed past the start: the cue is at the ball")
	var count := _backswing.size()
	_drag(stroke, pos + Vector2(0.0, -10.0))
	check_eq(_backswing.size(), count, "emitted only on change")
	stroke.free()


func test_speed_for_push_curve() -> void:
	var top := 10.0
	check_eq(StrokeControl.speed_for_push(0.0, top), 0.0, "0 at 0")
	check_eq(StrokeControl.speed_for_push(-500.0, top), 0.0, "0 for a pull")
	check_near(StrokeControl.speed_for_push(StrokeControl.FASTEST_PUSH_PX_S, top), top, 1e-9, "max at the fastest push")
	check_near(StrokeControl.speed_for_push(StrokeControl.FASTEST_PUSH_PX_S * 2.0, top), top, 1e-9, "max beyond it")
	check(StrokeControl.speed_for_push(StrokeControl.FASTEST_PUSH_PX_S / 2.0, top) < top / 2.0, "finer at low pace")
	var last := 0.0
	for push in range(100, 6001, 100):
		var s := StrokeControl.speed_for_push(float(push), top)
		check(s > last, "rises with the push at %d" % push)
		last = s


func test_mouse_strokes_and_emulated_mouse_is_ignored() -> void:
	var stroke := _make()
	var press := InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_LEFT
	press.pressed = true
	press.position = Vector2(150.0, 300.0)
	press.device = InputEvent.DEVICE_ID_EMULATION
	stroke._gui_input(press)
	check(not stroke.is_stroking(), "emulated mouse ignored")
	press.device = 0
	stroke._gui_input(press)
	check(stroke.is_stroking(), "real mouse starts a stroke")
	var pos := press.position
	for i in range(1, 13):
		fake_t = i * STEP
		var motion := InputEventMouseMotion.new()
		pos = Vector2(150.0, 300.0 - 2500.0 * i * STEP)
		motion.position = pos
		stroke._gui_input(motion)
	var release := InputEventMouseButton.new()
	release.button_index = MOUSE_BUTTON_LEFT
	release.pressed = false
	release.position = pos
	stroke._gui_input(release)
	check_eq(_struck.size(), 1, "struck")
	if _struck.size() == 1:
		var expected := StrokeControl.speed_for_push(2500.0, _max_speed())
		check_near(_struck[0], expected, expected * 0.01, "mouse push speed")
	stroke.free()


func test_second_finger_is_ignored() -> void:
	var stroke := _make()
	_touch(stroke, true, Vector2(150.0, 300.0))
	var other := InputEventScreenTouch.new()
	other.index = 1
	other.pressed = false
	other.position = Vector2(100.0, 100.0)
	stroke._gui_input(other)
	check(stroke.is_stroking(), "another finger's release doesn't end the stroke")
	stroke.free()


func test_hiding_cancels() -> void:
	var stroke := _make()
	_touch(stroke, true, Vector2(150.0, 300.0))
	stroke.hide()
	check_eq(_cancelled, ["hidden"] as Array[String], "cancelled when hidden")
	stroke.free()


func test_focus_lost_cancels_and_frees_the_area() -> void:
	for what: int in [Node.NOTIFICATION_APPLICATION_FOCUS_OUT, Node.NOTIFICATION_APPLICATION_PAUSED]:
		_cancelled.clear()
		var stroke := _make()
		_touch(stroke, true, Vector2(150.0, 300.0))
		stroke._notification(what)
		check_eq(_cancelled, ["focus lost"] as Array[String], "cancelled on %d" % what)
		check(not stroke.is_stroking(), "the press is over")
		var pos := Vector2(150.0, 300.0)
		_touch(stroke, true, pos)
		pos = _move(stroke, pos, Vector2(0.0, -3000.0), 0.1)
		_touch(stroke, false, pos)
		check_eq(_struck.size(), 1, "a new stroke strikes")
		_struck.clear()
		stroke.free()


func test_release_reported_outside_the_area_never_strikes() -> void:
	var setup := ShotSetup.new()
	var stroke := _make(setup)
	stroke.position = Vector2(1400.0, 10.0)
	var pos := Vector2(150.0, 300.0)
	_touch(stroke, true, pos)
	pos = _move(stroke, pos, Vector2(0.0, -3000.0), 0.1)
	# A cancelled touch as iOS may report it: released at the screen's (-1, -1).
	fake_t += STEP
	_touch(stroke, false, Vector2(-1.0, -1.0) - stroke.position)
	check_eq(_struck.size(), 0, "no strike")
	check_eq(_cancelled, [StrokeControl.LEFT_AREA] as Array[String], "cancelled as leaving the area")
	check_eq(setup.speed_m_s, 0.0, "speed untouched")
	stroke.free()


func test_each_press_starts_the_samples_over() -> void:
	var setup := ShotSetup.new()
	var stroke := _make(setup)
	fake_t = 0.0
	var pos := Vector2(150.0, 300.0)
	for i in 3:
		_touch(stroke, true, pos)
		pos = _move(stroke, pos, Vector2(0.0, 300.0), 0.2)
		_touch(stroke, false, pos)
	check_eq(_cancelled.size(), 3, "three strokes that never pushed")
	_touch(stroke, true, pos)
	var t0 := fake_t
	pos = _move(stroke, pos, Vector2(0.0, -3000.0), 0.1)
	_touch(stroke, false, pos)
	check_eq(_struck.size(), 1, "struck")
	# Touch-down, 12 push frames and the release.
	check_eq(setup.stroke_samples.size(), 14, "only the stroke that struck")
	check_near(setup.stroke_samples[0][0], t0, 1e-9, "starting at its touch-down")
	stroke.free()
