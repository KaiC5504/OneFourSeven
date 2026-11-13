extends TestCase
## The controls checked against the spec (8.1-8.3, 8.8, 15), with KaiC's M2 phone-session decisions
## where they replace it: the spin ball adjusted directly with no enlarge step, and the strike
## modes Push (the finger stroke) and Pull and release in place of the power bar.
## Controls are driven through Gestures.feed(), _gui_input() and their injectable clocks, with
## real-mouse events (device 0), the Windows path. Settings a test changes are put back.

var _now := 0.0
var _nodes: Array[Node] = []


func _clock() -> float:
	return _now


func _add(node: Node) -> Node:
	tree.root.add_child(node)
	_nodes.append(node)
	return node


func _free_all() -> void:
	for node in _nodes:
		if is_instance_valid(node):
			node.free()
	_nodes.clear()


## As the viewport delivers it: the gui_input signal, then _gui_input when the script has one.
func _gui(control: Control, event: InputEvent) -> void:
	control.gui_input.emit(event)
	var script := control.get_script() as Script
	if script == null:
		return
	for method: Dictionary in script.get_script_method_list():
		if method["name"] == "_gui_input":
			control.call("_gui_input", event)
			return


func _button(button: MouseButton, pressed: bool, position: Vector2, device: int = 0) -> InputEventMouseButton:
	var e := InputEventMouseButton.new()
	e.device = device
	e.button_index = button
	e.pressed = pressed
	e.position = position
	e.global_position = position
	e.factor = 1.0
	if pressed:
		e.button_mask = 1 << (int(button) - 1)
	return e


func _motion(position: Vector2, relative: Vector2, mask: int, device: int = 0) -> InputEventMouseMotion:
	var e := InputEventMouseMotion.new()
	e.device = device
	e.position = position
	e.global_position = position
	e.relative = relative
	e.screen_relative = relative
	e.button_mask = mask
	return e


func _set_setting(key: String, value: Variant) -> Variant:
	var old: Variant = Settings.get_value(key)
	Settings.set_value(key, value)
	return old


func _make_wheel(setup: ShotSetup) -> FineTuneWheel:
	var wheel := FineTuneWheel.new()
	wheel.size = Vector2(80.0, 800.0)
	wheel.clock = _clock
	_add(wheel)
	wheel.bind(setup)
	return wheel


## A vertical drag on the wheel, one motion event per entry of `moves` (canvas units, down positive).
func _wheel_drag(wheel: FineTuneWheel, moves: Array[float]) -> void:
	var pos := Vector2(40.0, 400.0)
	_now += 0.5
	_gui(wheel, _button(MOUSE_BUTTON_LEFT, true, pos))
	for dy in moves:
		_now += 0.016
		pos += Vector2(0.0, dy)
		_gui(wheel, _motion(pos, Vector2(0.0, dy), MOUSE_BUTTON_MASK_LEFT))
	_gui(wheel, _button(MOUSE_BUTTON_LEFT, false, pos))


func _px_per_step() -> float:
	return FineTuneWheel.PX_PER_STEP / float(Settings.get_value("fine_tune_sensitivity"))


func test_fine_tune_one_step_is_step_deg_and_up_is_plus_aim() -> void:
	# spec 8.1: fine-tune steps of about 0.01 degrees; dragging up is +aim_rad
	check_eq(FineTuneWheel.STEP_DEG, 0.01, "STEP_DEG")
	var setup := ShotSetup.new()
	setup.set_aim(0.3)
	var wheel := _make_wheel(setup)
	var step := deg_to_rad(FineTuneWheel.STEP_DEG)
	_wheel_drag(wheel, [-_px_per_step()])
	check_near(setup.aim_rad, 0.3 + step, 1e-12, "one step up")
	_wheel_drag(wheel, [_px_per_step()])
	check_near(setup.aim_rad, 0.3, 1e-12, "one step down undoes it")
	_free_all()


func test_fine_tune_k_steps_give_k_times_step_deg() -> void:
	# spec 8.1: fine-tune steps of about 0.01 degrees, for thin cuts
	var step := deg_to_rad(FineTuneWheel.STEP_DEG)
	for k: int in [1, 3, 7, 25]:
		var setup := ShotSetup.new()
		setup.set_aim(-1.2)
		var wheel := _make_wheel(setup)
		# The same k steps as one move and as half-step moves.
		_wheel_drag(wheel, [-k * _px_per_step()])
		check_near(setup.aim_rad, -1.2 + k * step, 1e-12, "%d steps in one move" % k)
		var halves: Array[float] = []
		for _i: int in 2 * k:
			halves.append(-0.5 * _px_per_step())
		_wheel_drag(wheel, halves)
		check_near(setup.aim_rad, -1.2 + 2 * k * step, 1e-12, "%d more steps in half-step moves" % k)
		# spec 16.10: each change is logged as a fine-tune sample.
		var logged := 0.0
		for sample in setup.fine_tune_samples:
			logged += sample[1]
		check(not setup.fine_tune_samples.is_empty(), "%d steps: fine-tune samples recorded" % k)
		check_near(logged, 2 * k * step, 1e-12, "%d steps: samples add up to the aim change" % k)
		_free_all()


func test_fine_tune_double_sensitivity_halves_the_drag_per_step() -> void:
	# spec 8.1 and 15: fine-tune sensitivity is a setting
	var setup := ShotSetup.new()
	setup.set_aim(0.0)
	var wheel := _make_wheel(setup)
	var step := deg_to_rad(FineTuneWheel.STEP_DEG)
	var base := _px_per_step()
	var old: Variant = _set_setting("fine_tune_sensitivity", 2.0 * float(Settings.get_value("fine_tune_sensitivity")))
	_wheel_drag(wheel, [-0.5 * base * 4.0])
	check_near(setup.aim_rad, 4.0 * step, 1e-12, "half the drag gives the same 4 steps")
	_wheel_drag(wheel, [-base * 4.0])
	check_near(setup.aim_rad, 12.0 * step, 1e-12, "the old drag for 4 steps now gives 8")
	Settings.set_value("fine_tune_sensitivity", old)
	_free_all()


## [setup, gestures, rig, aim], all in the tree; the rig behind a cue ball at (-1, 0).
func _make_aim() -> Array:
	var setup := ShotSetup.new()
	var gestures: Gestures = _add(Gestures.new())
	var rig := CameraRig.new()
	var camera := Camera3D.new()
	camera.name = "Camera3D"
	rig.add_child(camera)
	_add(rig)
	rig.follow(Vector2(-1.0, 0.0), 0.0)
	var aim: AimControl = _add(AimControl.new())
	aim.bind(setup, gestures, rig)
	return [setup, gestures, rig, aim]


## A horizontal left-drag through the gestures; returns the x the gestures reported as drag_moved.
func _aim_drag(gestures: Gestures, moves: Array[float]) -> float:
	var reported := [0.0]
	var record := func(_p: Vector2, rel: Vector2) -> void: reported[0] += rel.x
	gestures.drag_moved.connect(record)
	var pos := Vector2(900.0, 500.0)
	gestures.feed(_button(MOUSE_BUTTON_LEFT, true, pos))
	for dx in moves:
		pos += Vector2(dx, 0.0)
		gestures.feed(_motion(pos, Vector2(dx, 0.0), MOUSE_BUTTON_MASK_LEFT))
	gestures.feed(_button(MOUSE_BUTTON_LEFT, false, pos))
	gestures.drag_moved.disconnect(record)
	return float(reported[0])


## Lets a smoothed zoom settle: until zoom_scale holds for 10 frames, at most a second.
func _settle(rig: CameraRig) -> void:
	var last := rig.zoom_scale()
	var steady := 0
	var deadline := Time.get_ticks_msec() + 1000
	while steady < 10 and Time.get_ticks_msec() < deadline:
		await tree.process_frame
		var z := rig.zoom_scale()
		steady = steady + 1 if z == last else 0
		last = z


func test_drag_aim_zoomed_in_turns_less() -> void:
	# spec 8.1: sensitivity scales with camera zoom, zoomed in means finer control
	var parts := _make_aim()
	var setup: ShotSetup = parts[0]
	var gestures: Gestures = parts[1]
	var rig: CameraRig = parts[2]
	var aim: AimControl = parts[3]
	var sensitivity := float(Settings.get_value("aim_sensitivity"))
	var moves: Array[float] = [20.0, 20.0, 20.0, 20.0, 20.0, 20.0]

	var z0 := rig.zoom_scale()
	check_near(aim.rad_per_px(), AimControl.BASE_RAD_PER_PX * z0 * sensitivity, 1e-12, "rate at the default zoom")
	var a0 := setup.aim_rad
	var dragged0 := _aim_drag(gestures, moves)
	var turned0 := absf(setup.aim_rad - a0)
	check(dragged0 > 0.0, "the drag was reported")
	check(turned0 > 0.0, "the drag turned the aim")

	rig.pinch(2.0)
	await _settle(rig)
	var z1 := rig.zoom_scale()
	check(z1 < z0, "pinching in lowers zoom_scale: %f -> %f" % [z0, z1])
	check_near(aim.rad_per_px(), AimControl.BASE_RAD_PER_PX * z1 * sensitivity, 1e-12, "rate when zoomed in")
	var a1 := setup.aim_rad
	var dragged1 := _aim_drag(gestures, moves)
	var turned1 := absf(setup.aim_rad - a1)
	check(turned1 < turned0, "the same drag zoomed in turns less: %f vs %f" % [turned1, turned0])
	if dragged0 > 0.0 and dragged1 > 0.0 and turned0 > 0.0:
		check_near((turned1 / dragged1) / (turned0 / dragged0), z1 / z0, 1e-6, "turn per unit of drag follows zoom_scale")
	_free_all()


func test_drag_aim_never_snaps_through_a_ball_direction() -> void:
	# spec 8.1: aim never snaps to balls or pockets
	var parts := _make_aim()
	var setup: ShotSetup = parts[0]
	var gestures: Gestures = parts[1]
	var aim: AimControl = parts[3]
	# The aim starts exactly on the direction from the cue ball (-1, 0) to a red at (0.4, 0.35).
	var on_ball := atan2(0.35, 1.4)
	setup.set_aim(on_ball)
	var rate := aim.rad_per_px()
	var reported := [0.0]
	gestures.drag_moved.connect(func(_p: Vector2, rel: Vector2) -> void: reported[0] += rel.x)

	# One drag, a unit at a time: 30 reported units right, then back through the ball's direction
	# and 20 units past it.
	var pos := Vector2(900.0, 500.0)
	gestures.feed(_button(MOUSE_BUTTON_LEFT, true, pos))
	for _i: int in 200:
		if float(reported[0]) >= 30.0:
			break
		pos += Vector2(1.0, 0.0)
		gestures.feed(_motion(pos, Vector2(1.0, 0.0), MOUSE_BUTTON_MASK_LEFT))
	var out := float(reported[0])
	var turned := setup.aim_rad - on_ball
	check(out > 0.0, "the first move was reported")
	check(absf(turned) > 0.0, "the first move turned the aim")
	var sign := signf(turned) if turned != 0.0 else 1.0
	check_near(turned, sign * rate * out, 1e-12, "aim = start + drag sum")
	var crossed := false
	var past := 0
	for _i: int in 200:
		pos += Vector2(-1.0, 0.0)
		gestures.feed(_motion(pos, Vector2(-1.0, 0.0), MOUSE_BUTTON_MASK_LEFT))
		var total := float(reported[0])
		check_near(setup.aim_rad, on_ball + sign * rate * total, 1e-12, "aim = start + drag sum at %f" % total)
		if total == 0.0:
			crossed = true
			check_near(setup.aim_rad, on_ball, 1e-12, "on the ball's direction, not pulled to it")
		if total < 0.0:
			past += 1
			if past >= 20:
				break
	gestures.feed(_button(MOUSE_BUTTON_LEFT, false, pos))
	check(crossed, "the drag passed exactly through the ball's direction")
	check(past >= 20, "and went on past it")
	_free_all()


func _make_spin(setup: ShotSetup) -> SpinWidget:
	var spin := SpinWidget.new()
	spin.size = Vector2(240.0, 240.0)
	_add(spin)
	spin.bind(setup)
	return spin


func test_spin_is_set_directly_on_the_ball() -> void:
	# spec 8.2 had "tap it to enlarge, then drag"; KaiC (M2 phone session): one ball, adjusted
	# directly by dragging on it, no tap-to-enlarge step.
	var setup := ShotSetup.new()
	var spin := _make_spin(setup)
	var size := spin.size
	var centre := spin.ball_centre()
	var r := spin.ball_radius()
	_gui(spin, _button(MOUSE_BUTTON_LEFT, true, centre + Vector2(0.0, r * 0.6)))
	check_near(setup.tip_y, -0.6, 1e-4, "the first press sets screw")
	_gui(spin, _motion(centre + Vector2(r * 0.3, r * 0.6), Vector2(r * 0.3, 0.0), MOUSE_BUTTON_MASK_LEFT))
	_gui(spin, _button(MOUSE_BUTTON_LEFT, false, centre + Vector2(r * 0.3, r * 0.6)))
	check_near(setup.tip_x, 0.3, 1e-4, "dragging adds side")
	check_eq(spin.size, size, "the widget never changes size")
	_free_all()


func test_spin_tip_for_local_is_clamped_to_max_tip() -> void:
	# spec 8.2 and contract: x right, y up, ball radii, clamped to MAX_TIP
	var spin := _make_spin(ShotSetup.new())
	var centre := spin.size * 0.5
	var right := spin.tip_for_local(centre + Vector2(1.0e5, 0.0))
	check_near(right.x, SpinWidget.MAX_TIP, 1e-3, "far right: tip x")
	check_near(right.y, 0.0, 1e-3, "far right: tip y")
	var up := spin.tip_for_local(centre + Vector2(0.0, -1.0e5))
	check_near(up.x, 0.0, 1e-3, "far up: tip x")
	check_near(up.y, SpinWidget.MAX_TIP, 1e-3, "far up (topspin): tip y")
	var down_left := spin.tip_for_local(centre + Vector2(-1.0e5, 1.0e5))
	check_near(down_left.length(), SpinWidget.MAX_TIP, 1e-3, "far down-left: clamped to MAX_TIP")
	check(down_left.x < 0.0 and down_left.y < 0.0, "far down-left is left side and screw")
	_free_all()


func test_spin_marker_goes_past_the_miscue_ring_up_to_max_tip() -> void:
	# spec 7.3 and 8.2: the miscue limit is a ring; offsets beyond it are allowed (and miscue)
	var limit := Physics.param("miscue_limit", 0.55)
	check(limit < SpinWidget.MAX_TIP, "the miscue ring (%f) lies inside MAX_TIP" % limit)
	var setup := ShotSetup.new()
	var spin := _make_spin(setup)
	var pos := spin.size * 0.5
	_gui(spin, _button(MOUSE_BUTTON_LEFT, true, pos))
	for _i: int in 10:
		pos += Vector2(100.0, 0.0)
		_gui(spin, _motion(pos, Vector2(100.0, 0.0), MOUSE_BUTTON_MASK_LEFT))
	_gui(spin, _button(MOUSE_BUTTON_LEFT, false, pos))
	var tip := Vector2(setup.tip_x, setup.tip_y)
	check(tip.length() > limit, "the marker passes the miscue ring: tip %s" % tip)
	check_near(tip.length(), SpinWidget.MAX_TIP, 1e-3, "and stops at MAX_TIP")
	check_near(setup.tip_y, 0.0, 1e-3, "pure side")
	_free_all()


func test_after_shot_resets_spin_and_elevation_only_when_asked() -> void:
	# spec 8.2: both reset to centre ball and minimum elevation after each shot (a setting)
	var setup := ShotSetup.new()
	setup.set_elevation_min(0.05)
	setup.set_tip(0.3, -0.2)
	setup.set_elevation(0.4)
	setup.after_shot(false)
	check_eq(Vector2(setup.tip_x, setup.tip_y), Vector2(0.3, -0.2), "kept tip")
	check_near(setup.elevation_rad, 0.4, 1e-12, "kept elevation")
	setup.after_shot(true)
	check_eq(Vector2(setup.tip_x, setup.tip_y), Vector2.ZERO, "centre ball")
	check_near(setup.effective_elevation(), setup.elevation_min_rad, 1e-12, "minimum elevation")


func test_elevation_forced_up_to_the_obstruction_minimum() -> void:
	# spec 7.3 and 8.2: a lower elevation is raised to the minimum and the control shows it
	var setup := ShotSetup.new()
	var elevation := ElevationControl.new()
	elevation.size = Vector2(80.0, 300.0)
	_add(elevation)
	elevation.bind(setup)
	setup.set_elevation(0.1)
	setup.set_elevation_min(0.3)
	check_near(setup.effective_elevation(), 0.3, 1e-12, "effective elevation is the minimum")
	check(setup.elevation_forced(), "the setup reports it forced")
	check(elevation.is_forced(), "the control shows it forced")
	setup.set_elevation_min(0.05)
	check_near(setup.effective_elevation(), 0.1, 1e-12, "above the minimum the request stands")
	check(not elevation.is_forced(), "no longer forced")
	_free_all()


func _max_speed() -> float:
	return Physics.param("max_cue_speed_m_s", 10.0)


## The slowest push that reaches max_speed; -1 when none up to 1e7 canvas units/s does.
func _full_push(max_speed: float) -> float:
	var hi := 1.0
	while StrokeControl.speed_for_push(hi, max_speed) < max_speed - 1e-9:
		hi *= 2.0
		if hi > 1.0e7:
			return -1.0
	var lo := 0.0
	for _i: int in 60:
		var mid := 0.5 * (lo + hi)
		if StrokeControl.speed_for_push(mid, max_speed) >= max_speed - 1e-9:
			hi = mid
		else:
			lo = mid
	return hi


func _make_stroke(setup: ShotSetup) -> StrokeControl:
	var stroke := StrokeControl.new()
	stroke.size = Vector2(400.0, 400.0)
	stroke.clock = _clock
	_add(stroke)
	stroke.bind(setup)
	return stroke


## Press near the top, pull back over 0.3 s, then push up at rates[i] canvas units/s for
## durations[i] s in turn, sampled every 5 ms, and release at the end of the last. sideways:
## canvas units/s of drift during the push. The control is sized to hold the whole stroke.
func _stroke(stroke: StrokeControl, rates: PackedFloat64Array, durations: PackedFloat64Array, sideways: float = 0.0) -> void:
	var push := 0.0
	var push_time := 0.0
	for i: int in rates.size():
		push += rates[i] * durations[i]
		push_time += durations[i]
	var margin := 40.0
	var back := push + margin
	stroke.size = Vector2(400.0 + 2.0 * absf(sideways) * push_time, back + 2.0 * margin)
	var pos := Vector2(stroke.size.x * 0.5, margin)
	_now += 1.0
	_gui(stroke, _button(MOUSE_BUTTON_LEFT, true, pos))
	for _i: int in 30:
		_now += 0.01
		var rel := Vector2(0.0, back / 30.0)
		pos += rel
		_gui(stroke, _motion(pos, rel, MOUSE_BUTTON_MASK_LEFT))
	var dt := 0.005
	for i: int in rates.size():
		for _k: int in roundi(durations[i] / dt):
			_now += dt
			var rel := Vector2(sideways * dt, -rates[i] * dt)
			pos += rel
			_gui(stroke, _motion(pos, rel, MOUSE_BUTTON_MASK_LEFT))
	_gui(stroke, _button(MOUSE_BUTTON_LEFT, false, pos))


## Strokes and returns the struck speeds (empty when it never struck).
func _struck_by(stroke: StrokeControl, rates: PackedFloat64Array, durations: PackedFloat64Array, sideways: float = 0.0) -> Array[float]:
	var speeds: Array[float] = []
	var record := func(speed: float) -> void: speeds.append(speed)
	stroke.struck.connect(record)
	_stroke(stroke, rates, durations, sideways)
	stroke.struck.disconnect(record)
	return speeds


func test_stroke_speed_comes_from_the_last_window_only() -> void:
	# spec 8.3: the push speed over the last ~80 ms before release sets cue speed
	var max_speed := _max_speed()
	var full := _full_push(max_speed)
	check(full > 0.0, "speed_for_push reaches max_cue_speed")
	var fast := 0.6 * full if full > 0.0 else 1500.0
	var slow := 0.1 * full if full > 0.0 else 250.0
	var window := 1.5 * StrokeControl.WINDOW_S
	var mixed := _struck_by(_make_stroke(ShotSetup.new()), PackedFloat64Array([slow, fast]), PackedFloat64Array([0.5, window]))
	check_eq(mixed.size(), 1, "a slow push with a fast final window strikes once")
	var setup := ShotSetup.new()
	var all_slow := _struck_by(_make_stroke(setup), PackedFloat64Array([slow]), PackedFloat64Array([0.5 + window]))
	check_eq(all_slow.size(), 1, "a slow push strikes once")
	if mixed.size() == 1 and all_slow.size() == 1:
		var want := StrokeControl.speed_for_push(fast, max_speed)
		check_near(mixed[0], want, 1e-3 * max_speed, "the fast final window sets the speed")
		check(mixed[0] > all_slow[0] + 0.1, "the fast window beats the slow push: %f vs %f" % [mixed[0], all_slow[0]])
		check_near(all_slow[0], StrokeControl.speed_for_push(slow, max_speed), 1e-3 * max_speed, "the slow push speed")
		check_near(setup.speed_m_s, all_slow[0], 1e-9, "the strike speed goes to the setup")
	_free_all()


func test_stroke_dragged_out_of_the_area_cancels() -> void:
	# spec 8.3: dragging out of the stroke area cancels the stroke
	var setup := ShotSetup.new()
	setup.set_speed(1.25)
	var stroke := _make_stroke(setup)
	var struck := [0]
	var cancelled: Array[String] = []
	stroke.struck.connect(func(_s: float) -> void: struck[0] += 1)
	stroke.cancelled.connect(func(reason: String) -> void: cancelled.append(reason))
	var pos := Vector2(200.0, 100.0)
	_now += 1.0
	_gui(stroke, _button(MOUSE_BUTTON_LEFT, true, pos))
	for _i: int in 10:
		_now += 0.01
		pos += Vector2(0.0, 20.0)
		_gui(stroke, _motion(pos, Vector2(0.0, 20.0), MOUSE_BUTTON_MASK_LEFT))
	# Out through the left edge while pushing.
	for _i: int in 10:
		_now += 0.005
		pos += Vector2(-40.0, -10.0)
		_gui(stroke, _motion(pos, Vector2(-40.0, -10.0), MOUSE_BUTTON_MASK_LEFT))
	check(pos.x < 0.0, "the drag left the area")
	_gui(stroke, _button(MOUSE_BUTTON_LEFT, false, pos))
	check_eq(int(struck[0]), 0, "no strike")
	check_eq(cancelled.size(), 1, "cancelled once")
	check_near(setup.speed_m_s, 1.25, 1e-12, "the setup's speed is untouched")
	_free_all()


func test_stroke_curve_is_monotonic_and_finer_at_the_low_end() -> void:
	# spec 8.3: a tunable curve that gives fine control at low power, max at full power
	var max_speed := 10.0
	var full := _full_push(max_speed)
	check(full > 0.0, "speed_for_push reaches max_speed")
	if full <= 0.0:
		return
	check_eq(StrokeControl.speed_for_push(0.0, max_speed), 0.0, "0 at 0")
	check_near(StrokeControl.speed_for_push(full, max_speed), max_speed, 1e-6, "max at the fastest push")
	check_near(StrokeControl.speed_for_push(3.0 * full, max_speed), max_speed, 1e-9, "max beyond it")
	var previous := 0.0
	for i: int in 301:
		var speed := StrokeControl.speed_for_push(1.5 * full * i / 300.0, max_speed)
		check(speed >= previous - 1e-12, "monotonic at %d/300" % i)
		previous = speed
	var h := 0.01 * full
	var low := StrokeControl.speed_for_push(0.1 * full + h, max_speed) - StrokeControl.speed_for_push(0.1 * full - h, max_speed)
	var high := StrokeControl.speed_for_push(0.9 * full + h, max_speed) - StrokeControl.speed_for_push(0.9 * full - h, max_speed)
	check(low < high, "slope at 10%% (%f) below slope at 90%% (%f)" % [low, high])


func test_pull_curve_is_monotonic_and_finer_at_the_low_end() -> void:
	# spec 8.3 asked the power bar for a non-linear scale with fine low-power control; the pull that
	# replaced it (KaiC, M2 phone session) keeps that curve over the pull's length.
	var max_speed := 10.0
	var full := PullControl.FULL_PULL
	var speed_at := func(units: float) -> float: return PullControl.speed_for_power(PullControl.power_for_pull(units), max_speed)
	check_eq(speed_at.call(0.0), 0.0, "0 at no pull")
	check_near(speed_at.call(full), max_speed, 1e-9, "max at the full pull")
	var previous := 0.0
	for i: int in 201:
		var speed: float = speed_at.call(full * i / 200.0)
		check(speed >= previous - 1e-12, "monotonic at %d/200" % i)
		previous = speed
	var low: float = speed_at.call(0.11 * full) - speed_at.call(0.09 * full)
	var high: float = speed_at.call(0.91 * full) - speed_at.call(0.89 * full)
	check(low < high, "slope at 10%% (%f) below slope at 90%% (%f)" % [low, high])
	check(speed_at.call(0.5 * full) < 0.5 * max_speed, "half the pull is under half speed")


func test_stroke_wander_is_off_by_default_and_adds_no_aim_error() -> void:
	# spec 8.3: an optional setting, off by default, that adds aim error when the stroke wanders
	check_eq(Settings.DEFAULTS["stroke_wander"], false, "stroke_wander default")
	var old: Variant = _set_setting("stroke_wander", false)
	var setup := ShotSetup.new()
	setup.set_aim(0.7)
	var stroke := _make_stroke(setup)
	var full := _full_push(_max_speed())
	var rate := 0.4 * full if full > 0.0 else 800.0
	var speeds := _struck_by(stroke, PackedFloat64Array([rate]), PackedFloat64Array([0.3]), 300.0)
	check_eq(speeds.size(), 1, "the wandering stroke strikes")
	check_eq(setup.wander_rad, 0.0, "no wander error")
	check_near(float(setup.to_strike()["aim_rad"]), 0.7, 1e-12, "the strike keeps the aim")
	Settings.set_value("stroke_wander", old)
	_free_all()


## Counts of every Gestures signal, plus the relatives they carried.
class Heard:
	var counts := {}
	var drag_relative := Vector2.ZERO
	var two_finger_relative := Vector2.ZERO
	var factors: Array[float] = []
	var last_drag_position := Vector2.INF
	var ended_position := Vector2.INF

	func _init(g: Gestures) -> void:
		for s: String in ["drag_started", "drag_moved", "drag_ended", "tapped", "pinched", "two_finger_dragged", "three_finger_tapped"]:
			counts[s] = 0
		g.drag_started.connect(func(_p: Vector2) -> void: _count("drag_started"))
		g.drag_moved.connect(func(p: Vector2, rel: Vector2) -> void:
			_count("drag_moved")
			drag_relative += rel
			last_drag_position = p)
		g.drag_ended.connect(func(p: Vector2) -> void:
			_count("drag_ended")
			ended_position = p)
		g.tapped.connect(func(_p: Vector2) -> void: _count("tapped"))
		g.pinched.connect(func(f: float, _c: Vector2) -> void:
			_count("pinched")
			factors.append(f))
		g.two_finger_dragged.connect(func(rel: Vector2) -> void:
			_count("two_finger_dragged")
			two_finger_relative += rel)
		g.three_finger_tapped.connect(func() -> void: _count("three_finger_tapped"))

	func _count(s: String) -> void:
		counts[s] = int(counts[s]) + 1

	func total() -> int:
		var n := 0
		for s: String in counts:
			n += int(counts[s])
		return n


func _left_drag(g: Gestures, device: int) -> Vector2:
	var pos := Vector2(500.0, 400.0)
	g.feed(_button(MOUSE_BUTTON_LEFT, true, pos, device))
	for _i: int in 6:
		pos += Vector2(15.0, 4.0)
		g.feed(_motion(pos, Vector2(15.0, 4.0), MOUSE_BUTTON_MASK_LEFT, device))
	g.feed(_button(MOUSE_BUTTON_LEFT, false, pos, device))
	return pos


func _right_drag(g: Gestures, device: int) -> void:
	var pos := Vector2(500.0, 400.0)
	g.feed(_button(MOUSE_BUTTON_RIGHT, true, pos, device))
	for _i: int in 6:
		pos += Vector2(0.0, 12.0)
		g.feed(_motion(pos, Vector2(0.0, 12.0), MOUSE_BUTTON_MASK_RIGHT, device))
	g.feed(_button(MOUSE_BUTTON_RIGHT, false, pos, device))


func _wheel(g: Gestures, button: MouseButton, device: int) -> void:
	g.feed(_button(button, true, Vector2(600.0, 300.0), device))
	g.feed(_button(button, false, Vector2(600.0, 300.0), device))


func test_left_drag_is_a_one_finger_drag() -> void:
	# spec 8.8: a left-drag acts as a one-finger drag
	var g: Gestures = _add(Gestures.new())
	var heard := Heard.new(g)
	var end := _left_drag(g, 0)
	check_eq(int(heard.counts["drag_started"]), 1, "drag_started")
	check(int(heard.counts["drag_moved"]) >= 1, "drag_moved")
	check_eq(int(heard.counts["drag_ended"]), 1, "drag_ended")
	check(heard.drag_relative.x > 0.0, "the drag moved right")
	check_eq(heard.last_drag_position, end, "the last drag_moved is at the pointer")
	check_eq(heard.ended_position, end, "drag_ended at the release")
	check_eq(int(heard.counts["two_finger_dragged"]) + int(heard.counts["pinched"]), 0, "nothing else")
	_free_all()


func test_wheel_is_pinch() -> void:
	# spec 8.8: the scroll wheel as pinch-zoom
	var g: Gestures = _add(Gestures.new())
	var heard := Heard.new(g)
	_wheel(g, MOUSE_BUTTON_WHEEL_UP, 0)
	var up := heard.factors.duplicate()
	heard.factors.clear()
	_wheel(g, MOUSE_BUTTON_WHEEL_DOWN, 0)
	var down := heard.factors.duplicate()
	check(not up.is_empty(), "wheel up pinches")
	check(not down.is_empty(), "wheel down pinches")
	if not up.is_empty() and not down.is_empty():
		var f_up: float = up[0]
		var f_down: float = down[0]
		check(f_up > 0.0 and f_down > 0.0, "factors are positive")
		check((f_up - 1.0) * (f_down - 1.0) < 0.0, "up and down zoom opposite ways: %f, %f" % [f_up, f_down])
	check_eq(int(heard.counts["drag_moved"]) + int(heard.counts["two_finger_dragged"]), 0, "no drags")
	_free_all()


func test_right_drag_is_a_two_finger_drag() -> void:
	# spec 8.8: a right-drag as a two-finger drag
	var g: Gestures = _add(Gestures.new())
	var heard := Heard.new(g)
	_right_drag(g, 0)
	check(int(heard.counts["two_finger_dragged"]) >= 1, "two_finger_dragged")
	check(heard.two_finger_relative.y > 0.0, "a downward drag reports down positive")
	check_eq(int(heard.counts["drag_started"]) + int(heard.counts["drag_moved"]), 0, "not a one-finger drag")
	_free_all()


func test_emulated_mouse_events_give_nothing() -> void:
	# spec 8.8: the mouse mapping is for a real mouse; touches emulated as mouse events are not
	# gestures a second time
	var g: Gestures = _add(Gestures.new())
	var heard := Heard.new(g)
	var emulated := InputEvent.DEVICE_ID_EMULATION
	_left_drag(g, emulated)
	_right_drag(g, emulated)
	_wheel(g, MOUSE_BUTTON_WHEEL_UP, emulated)
	_wheel(g, MOUSE_BUTTON_WHEEL_DOWN, emulated)
	check_eq(heard.total(), 0, "no gesture signals: %s" % heard.counts)
	# The same input from the real mouse is heard, so the silence above is the device's doing.
	_left_drag(g, 0)
	check(heard.total() > 0, "real mouse input is heard")
	_free_all()


func test_settings_defaults() -> void:
	# spec 15 defaults: finger stroke (Push), spin and elevation reset, guide level 1
	var d: Dictionary = Settings.DEFAULTS
	for key: String in ["strike_mode", "guide_level", "fine_tune_sensitivity", "aim_sensitivity", "stroke_wander", "reset_spin_after_shot", "volume", "haptics"]:
		check(d.has(key), "setting %s exists" % key)
	check_eq(d.get("strike_mode"), "stroke", "strike mode: finger stroke")
	check_eq(d.get("strike_mode"), ShotSetup.STROKE, "the setup's name for it")
	check_eq(d.get("guide_level"), 1, "guide level 1 (spec 8.4)")
	check_eq(d.get("reset_spin_after_shot"), true, "spin and elevation reset after each shot")
	check_eq(d.get("stroke_wander"), false, "stroke wander off (spec 8.3)")
	check_eq(d.get("haptics"), true, "haptics on")
	check_eq(Settings.STRIKE_MODES, [ShotSetup.STROKE, ShotSetup.PULL] as Array[String], "Push and Pull and release (KaiC, M2)")
	check_eq(ShotSetup.new().strike_mode, ShotSetup.STROKE, "a new setup strikes by finger stroke")


func test_settings_refuse_values_outside_the_spec() -> void:
	# spec 15: guide lines 0-3; strike mode Push or Pull (the power bar is gone, KaiC M2)
	for bad: Array in [["guide_level", 4], ["guide_level", -1], ["strike_mode", "laser"], ["strike_mode", "power_bar"],
			["no_such_setting", 1]]:
		var key: String = bad[0]
		var old: Variant = Settings.get_value(key)
		var accepted := Settings.set_value(key, bad[1])
		check(not accepted, "%s = %s refused" % [key, bad[1]])
		if accepted and old != null:
			Settings.set_value(key, old)
