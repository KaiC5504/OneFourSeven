extends TestCase

const STEP_RAD := FineTuneWheel.STEP_DEG * PI / 180.0

var _now := [0.0]


func _wheel(setup: ShotSetup) -> FineTuneWheel:
	var w := FineTuneWheel.new()
	w.size = FineTuneWheel.MIN_SIZE
	w.clock = func() -> float: return _now[0]
	tree.root.add_child(w)
	w.bind(setup)
	return w


func _touch(w: FineTuneWheel, index: int, y: float, pressed: bool) -> void:
	var e := InputEventScreenTouch.new()
	e.index = index
	e.position = Vector2(60, y)
	e.pressed = pressed
	w._gui_input(e)


## Drags touch `index` from y0 to y1 in steps of `step` canvas units.
func _drag(w: FineTuneWheel, index: int, y0: float, y1: float, step: float) -> void:
	var count := ceili(absf(y1 - y0) / step)
	for i in range(1, count + 1):
		var e := InputEventScreenDrag.new()
		e.index = index
		e.position = Vector2(60, lerpf(y0, y1, float(i) / count))
		w._gui_input(e)


func _swipe(w: FineTuneWheel, y0: float, y1: float, step: float = 1.5) -> void:
	_touch(w, 0, y0, true)
	_drag(w, 0, y0, y1, step)
	_touch(w, 0, y1, false)


func _mouse(w: FineTuneWheel, button: MouseButton, y: float, pressed: bool, device: int = 0) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = button
	e.position = Vector2(60, y)
	e.pressed = pressed
	e.device = device
	w._gui_input(e)


func _motion(w: FineTuneWheel, y: float, device: int = 0) -> void:
	var e := InputEventMouseMotion.new()
	e.position = Vector2(60, y)
	e.device = device
	w._gui_input(e)


func test_30_steps_up_is_plus_0_30_degrees() -> void:
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	_swipe(w, 300.0, 300.0 - 30.0 * FineTuneWheel.PX_PER_STEP)
	check_near(rad_to_deg(setup.aim_rad), 0.30, 1e-9, "aim in degrees")
	check_eq(setup.fine_tune_samples.size(), 30, "one sample per step")
	w.free()


func test_dragging_down_turns_the_aim_negative() -> void:
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	_swipe(w, 100.0, 100.0 + 12.0 * FineTuneWheel.PX_PER_STEP, 4.0)
	check_near(rad_to_deg(setup.aim_rad), -0.12, 1e-9, "aim in degrees")
	w.free()


func test_sensitivity_2_halves_the_units_per_step() -> void:
	var before: Variant = Settings.get_value("fine_tune_sensitivity")
	Settings.set_value("fine_tune_sensitivity", 2.0)
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	var per_step := w.units_per_step()
	_swipe(w, 300.0, 300.0 - 10.0 * FineTuneWheel.PX_PER_STEP)
	Settings.set_value("fine_tune_sensitivity", before)
	check_near(per_step, FineTuneWheel.PX_PER_STEP / 2.0, 1e-12, "units per step")
	check_near(rad_to_deg(setup.aim_rad), 0.20, 1e-9, "twice the steps for the same drag")
	w.free()


func test_remainder_carries_over() -> void:
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	_touch(w, 0, 300.0, true)
	_drag(w, 0, 300.0, 290.0, 10.0)
	check_eq(setup.fine_tune_samples.size(), 1, "10 units up: one step")
	_drag(w, 0, 290.0, 288.0, 2.0)
	check_eq(setup.fine_tune_samples.size(), 2, "2 more: the 4 left over make a second step")
	_drag(w, 0, 288.0, 293.0, 5.0)
	check_eq(setup.fine_tune_samples.size(), 2, "5 back down: no step yet")
	_drag(w, 0, 293.0, 294.0, 1.0)
	check_eq(setup.fine_tune_samples.size(), 3, "6 down: one step back")
	_touch(w, 0, 294.0, false)
	check_near(rad_to_deg(setup.aim_rad), 0.01, 1e-12, "net one step up")
	w.free()


func test_samples_record_time_and_step() -> void:
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	_now[0] = 4.5
	_touch(w, 0, 200.0, true)
	_drag(w, 0, 200.0, 200.0 - FineTuneWheel.PX_PER_STEP, FineTuneWheel.PX_PER_STEP)
	_now[0] = 5.25
	_drag(w, 0, 194.0, 194.0 + FineTuneWheel.PX_PER_STEP, FineTuneWheel.PX_PER_STEP)
	_touch(w, 0, 200.0, false)
	check_eq(setup.fine_tune_samples.size(), 2, "samples")
	check_eq(setup.fine_tune_samples[0][0], 4.5, "first: time")
	check_near(setup.fine_tune_samples[0][1], STEP_RAD, 1e-18, "first: up")
	check_eq(setup.fine_tune_samples[1][0], 5.25, "second: time")
	check_near(setup.fine_tune_samples[1][1], -STEP_RAD, 1e-18, "second: down")
	check_eq(setup.log_fields()["fine_tune_samples"].size(), 2, "in shot.input's fields")
	w.free()


func test_stepped_signal() -> void:
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	var steps: Array[int] = []
	w.stepped.connect(func(n: int) -> void: steps.append(n))
	_swipe(w, 300.0, 300.0 - 3.0 * FineTuneWheel.PX_PER_STEP, 3.0 * FineTuneWheel.PX_PER_STEP)
	check_eq(steps, [3] as Array[int], "three steps in one move")
	w.free()


func test_second_finger_is_ignored() -> void:
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	_touch(w, 0, 300.0, true)
	_touch(w, 1, 100.0, true)
	_drag(w, 1, 100.0, 40.0, 3.0)
	check_eq(setup.aim_rad, 0.0, "the second finger turns nothing")
	_drag(w, 0, 300.0, 294.0, 3.0)
	check_near(setup.aim_rad, STEP_RAD, 1e-15, "the first still turns it")
	w.free()


func test_mouse_left_drag() -> void:
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	_mouse(w, MOUSE_BUTTON_LEFT, 300.0, true)
	for i in range(1, 11):
		_motion(w, 300.0 - i * FineTuneWheel.PX_PER_STEP)
	_mouse(w, MOUSE_BUTTON_LEFT, 240.0, false)
	_motion(w, 100.0)
	check_near(rad_to_deg(setup.aim_rad), 0.10, 1e-9, "ten steps, none after release")
	w.free()


func test_mouse_wheel_is_one_step_per_notch() -> void:
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	for i in 3:
		_mouse(w, MOUSE_BUTTON_WHEEL_UP, 100.0, true)
		_mouse(w, MOUSE_BUTTON_WHEEL_UP, 100.0, false)
	_mouse(w, MOUSE_BUTTON_WHEEL_DOWN, 100.0, true)
	_mouse(w, MOUSE_BUTTON_WHEEL_DOWN, 100.0, false)
	check_near(setup.aim_rad, 2.0 * STEP_RAD, 1e-15, "three up, one down")
	check_eq(setup.fine_tune_samples.size(), 4, "samples")
	w.free()


func test_emulated_mouse_is_ignored() -> void:
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	var emulated := InputEvent.DEVICE_ID_EMULATION
	_mouse(w, MOUSE_BUTTON_LEFT, 300.0, true, emulated)
	_motion(w, 240.0, emulated)
	_mouse(w, MOUSE_BUTTON_LEFT, 240.0, false, emulated)
	_mouse(w, MOUSE_BUTTON_WHEEL_UP, 240.0, true, emulated)
	check_eq(setup.aim_rad, 0.0, "nothing from emulated mouse events")
	w.free()


func test_cancel_press_frees_the_wheel() -> void:
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	_touch(w, 1, 300.0, true)
	w.cancel_press()
	check(not w.is_pressed(), "the press is over")
	_drag(w, 1, 300.0, 240.0, 3.0)
	check_eq(setup.aim_rad, 0.0, "the old finger turns nothing")
	_touch(w, 1, 240.0, false)
	_swipe(w, 300.0, 300.0 - 5.0 * FineTuneWheel.PX_PER_STEP)
	check_near(setup.aim_rad, 5.0 * STEP_RAD, 1e-15, "a new press turns it")
	w.cancel_press()
	check(not w.is_pressed(), "harmless with no press")
	w.free()


func test_focus_lost_frees_the_wheel() -> void:
	for what: int in [Node.NOTIFICATION_APPLICATION_FOCUS_OUT, Node.NOTIFICATION_APPLICATION_PAUSED]:
		var setup := ShotSetup.new()
		var w := _wheel(setup)
		_mouse(w, MOUSE_BUTTON_LEFT, 300.0, true)
		w._notification(what)
		check(not w.is_pressed(), "released on %d" % what)
		_motion(w, 240.0)
		check_eq(setup.aim_rad, 0.0, "no turn after")
		_swipe(w, 300.0, 300.0 - FineTuneWheel.PX_PER_STEP)
		check_near(setup.aim_rad, STEP_RAD, 1e-15, "a new press turns it")
		w.free()


func test_samples_keep_the_last_400() -> void:
	var setup := ShotSetup.new()
	var w := _wheel(setup)
	_touch(w, 0, 300.0, true)
	for i in 450:
		_now[0] = float(i)
		_drag(w, 0, 300.0 - i * FineTuneWheel.PX_PER_STEP, 300.0 - (i + 1) * FineTuneWheel.PX_PER_STEP, FineTuneWheel.PX_PER_STEP)
	_touch(w, 0, 0.0, false)
	check_near(rad_to_deg(setup.aim_rad), 4.5, 1e-9, "every step turned the aim")
	check_eq(setup.fine_tune_samples.size(), ShotSetup.MAX_FINE_TUNE_SAMPLES, "capped")
	check_eq(setup.fine_tune_samples[0][0], 50.0, "the oldest dropped")
	check_eq(setup.fine_tune_samples[-1][0], 449.0, "the newest kept")
	w.free()


func test_unbound_wheel_does_not_fail() -> void:
	var w := FineTuneWheel.new()
	w.size = FineTuneWheel.MIN_SIZE
	tree.root.add_child(w)
	_swipe(w, 300.0, 200.0)
	check(not w.is_pressed(), "released")
	w.free()
