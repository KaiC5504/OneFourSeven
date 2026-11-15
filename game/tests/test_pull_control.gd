extends TestCase
## PullControl, Pull and release: the pull's power and pace, the release, and every way a pull
## cancels.

const STRIP := Vector2(120.0, 820.0)
const X := 60.0

var _now := 0.0
var _struck: Array[float] = []
var _cancelled: Array[String] = []
var _backswing: Array[float] = []


func _make(setup: ShotSetup = null) -> PullControl:
	var pull := PullControl.new()
	tree.root.add_child(pull)
	pull.size = STRIP
	pull.clock = func() -> float: return _now
	pull.bind(setup)
	pull.struck.connect(func(speed: float) -> void: _struck.append(speed))
	pull.cancelled.connect(func(reason: String) -> void: _cancelled.append(reason))
	pull.backswing_changed.connect(func(m: float) -> void: _backswing.append(m))
	return pull


func _touch(control: Control, pressed: bool, pos: Vector2, canceled: bool = false) -> void:
	var e := InputEventScreenTouch.new()
	e.index = 0
	e.pressed = pressed
	e.canceled = canceled
	e.position = pos
	control._gui_input(e)


func _drag(control: Control, pos: Vector2) -> void:
	_now += 1.0 / 120.0
	var e := InputEventScreenDrag.new()
	e.index = 0
	e.position = pos
	control._gui_input(e)


## Press at `from`, then move straight down `units` in steps of 10, without releasing.
func _pull_down(control: Control, from: Vector2, units: float) -> Vector2:
	_touch(control, true, from)
	var y := 0.0
	while y < units:
		y = minf(y + 10.0, units)
		_drag(control, from + Vector2(0.0, y))
	return from + Vector2(0.0, units)


func _max_speed() -> float:
	return Physics.param("max_cue_speed_m_s", 10.0)


func test_curve() -> void:
	var top := 10.0
	check_eq(PullControl.speed_for_power(0.0, top), 0.0, "0 at 0")
	check_near(PullControl.speed_for_power(1.0, top), top, 1e-9, "max at 1")
	check_near(PullControl.speed_for_power(0.5, top), top * pow(0.5, 1.5), 1e-9, "finer at the low end")
	check_near(PullControl.speed_for_power(1.5, top), top, 1e-9, "clamped")
	for p: float in [0.0, 0.1, 0.387, 0.75, 1.0]:
		check_near(PullControl.power_for_speed(PullControl.speed_for_power(p, top), top), p, 1e-9, "inverse at %s" % p)


func test_pull_mapping() -> void:
	check_eq(PullControl.power_for_pull(0.0), 0.0, "no pull, no power")
	check_near(PullControl.power_for_pull(PullControl.FULL_PULL / 2.0), 0.5, 1e-12, "linear in the pull")
	check_eq(PullControl.power_for_pull(PullControl.FULL_PULL), 1.0, "full at FULL_PULL")
	check_eq(PullControl.power_for_pull(PullControl.FULL_PULL * 2.0), 1.0, "and beyond")
	check_near(PullControl.pull_for_power(0.3), 0.3 * PullControl.FULL_PULL, 1e-9, "inverse")
	# The phone's strip is about 820 units tall: full power from a press in its upper part.
	check(PullControl.FULL_PULL <= 0.6 * STRIP.y, "full power reachable: %s" % PullControl.FULL_PULL)
	check_near(PullControl.backswing_for_pull(PullControl.FULL_PULL), PullControl.BACKSWING_AT_FULL_M, 1e-12, "full backswing")


func test_a_pull_of_300_units_fires_at_its_speed() -> void:
	# spec 16.3: a scripted pull of N units and release gives speed X within 1%.
	var setup := ShotSetup.new()
	setup.set_strike_mode(ShotSetup.PULL)
	setup.wander_rad = 0.01
	var pull := _make(setup)
	var at := _pull_down(pull, Vector2(X, 100.0), 300.0)
	check(pull.is_pulling(), "still pulling until the release")
	check_eq(_struck.size(), 0, "no shot before the release")
	_touch(pull, false, at)
	var power := 300.0 / PullControl.FULL_PULL
	var want := _max_speed() * pow(power, PullControl.POWER_EXPONENT)
	check_eq(_struck.size(), 1, "the release fires once")
	if _struck.size() == 1:
		check_near(_struck[0], want, want * 0.01, "speed for a 300-unit pull")
	check_near(setup.speed_m_s, want, want * 0.01, "the setup's speed")
	check_near(setup.power, power, 1e-9, "the setup's power")
	check(not setup.pulling, "the pull is over")
	check_eq(setup.wander_rad, 0.0, "no wander from a pull")
	check_eq(setup.stroke_samples.size(), 32, "the press, 30 moves and the release recorded")
	check_eq(_cancelled.size(), 0, "not cancelled")
	pull.free()


func test_guides_and_readout_follow_the_pull() -> void:
	var setup := ShotSetup.new()
	setup.set_strike_mode(ShotSetup.PULL)
	var pull := _make(setup)
	var before := setup.guide_pace_m_s()
	_touch(pull, true, Vector2(X, 100.0))
	_drag(pull, Vector2(X, 100.0 + PullControl.MIN_PULL / 2.0))
	check(not setup.pulling, "under MIN_PULL the guides keep the last pace")
	check_eq(setup.guide_pace_m_s(), before, "the last pace")
	_drag(pull, Vector2(X, 100.0 + 240.0))
	check(setup.pulling, "past MIN_PULL it is a pull")
	var speed := PullControl.speed_for_power(0.5, _max_speed())
	check_near(setup.guide_pace_m_s(), speed, 1e-9, "guide level 3 uses the pull's pace")
	check_near(pull.power(), 0.5, 1e-9, "power")
	check_eq(pull.readout_text(), "50%%\n%s m/s" % ("%.2f" % speed if speed < 1.0 else "%.1f" % speed), "readout")
	check_near(_backswing[-1], 0.5 * PullControl.BACKSWING_AT_FULL_M, 1e-9, "the cue on the table pulled back")
	pull.free()


func test_a_tap_is_not_a_shot() -> void:
	var setup := ShotSetup.new()
	setup.set_speed(1.25, 0.3)
	var pull := _make(setup)
	_touch(pull, true, Vector2(X, 300.0))
	_drag(pull, Vector2(X, 300.0 + PullControl.MIN_PULL - 2.0))
	_touch(pull, false, Vector2(X, 300.0 + PullControl.MIN_PULL - 2.0))
	check_eq(_struck.size(), 0, "no shot")
	check_eq(_cancelled, [PullControl.NO_PULL] as Array[String], "cancelled: no pull")
	check_eq([setup.speed_m_s, setup.power], [1.25, 0.3], "the setup untouched")
	pull.free()


func test_sliding_back_to_the_start_cancels() -> void:
	var setup := ShotSetup.new()
	setup.set_strike_mode(ShotSetup.PULL)
	setup.set_speed(2.0, 0.4)
	var pull := _make(setup)
	var at := _pull_down(pull, Vector2(X, 200.0), 200.0)
	check(setup.pulling, "pulling")
	while at.y > 200.0:
		at.y -= 20.0
		_drag(pull, at)
	check_eq(_cancelled, [PullControl.SLID_BACK] as Array[String], "cancelled on reaching the start")
	check(not pull.is_pulling(), "the pull is over")
	check(not setup.pulling, "and the guides know")
	check_eq([setup.speed_m_s, setup.power], [2.0, 0.4], "the power before the press is back")
	check_eq(_backswing[-1], 0.0, "the cue back at the ball")
	_drag(pull, at + Vector2(0.0, 100.0))
	check(not pull.is_pulling(), "the rest of that touch does nothing")
	_touch(pull, false, at + Vector2(0.0, 100.0))
	check_eq(_struck.size(), 0, "nor does its release")
	pull.free()


func test_sliding_out_sideways_cancels() -> void:
	var setup := ShotSetup.new()
	var pull := _make(setup)
	var at := _pull_down(pull, Vector2(X, 100.0), 200.0)
	while at.x > -PullControl.LEAVE_MARGIN - 10.0:
		at.x -= 30.0
		_drag(pull, at)
	check_eq(_cancelled, [PullControl.LEFT_STRIP] as Array[String], "cancelled on leaving the strip")
	_touch(pull, false, at)
	check_eq(_struck.size(), 0, "never struck")
	check_eq(_cancelled.size(), 1, "cancelled once")
	pull.free()


func test_within_the_margin_and_below_the_strip_carry_on() -> void:
	var setup := ShotSetup.new()
	var pull := _make(setup)
	var at := _pull_down(pull, Vector2(X, 500.0), 200.0)
	_drag(pull, at + Vector2(PullControl.LEAVE_MARGIN, 0.0))
	_drag(pull, Vector2(X, STRIP.y + 200.0))
	check(pull.is_pulling(), "still pulling")
	check_eq(pull.power(), 1.0, "full power below the strip")
	_touch(pull, false, Vector2(X, STRIP.y + 200.0))
	check_eq(_struck.size(), 1, "fires")
	pull.free()


func test_cancelled_touch_and_release_outside_never_strike() -> void:
	for outside: bool in [false, true]:
		var setup := ShotSetup.new()
		var pull := _make(setup)
		var at := _pull_down(pull, Vector2(X, 100.0), 200.0)
		if outside:
			# iOS can report a cancelled touch as a release near the screen's (-1, -1).
			_touch(pull, false, Vector2(-1.0, -1.0) - pull.position - Vector2(300.0, 0.0))
		else:
			_touch(pull, false, at, true)
		check_eq(_struck.size(), 0, "no shot (outside %s)" % outside)
		check_eq(_cancelled.size(), 1, "cancelled (outside %s)" % outside)
		_cancelled.clear()
		pull.free()


func test_cancel_press_and_focus_lost_free_the_strip() -> void:
	for what: int in [-1, Node.NOTIFICATION_APPLICATION_FOCUS_OUT, Node.NOTIFICATION_APPLICATION_PAUSED]:
		var setup := ShotSetup.new()
		var pull := _make(setup)
		var at := _pull_down(pull, Vector2(X, 100.0), 200.0)
		if what < 0:
			pull.cancel_press("shot started")
		else:
			pull._notification(what)
		check(not pull.is_pulling(), "freed by %d" % what)
		_touch(pull, false, at)
		check_eq(_struck.size(), 0, "the old finger's release does nothing (%d)" % what)
		at = _pull_down(pull, Vector2(X, 100.0), 100.0)
		_touch(pull, false, at)
		check_eq(_struck.size(), 1, "a new pull fires (%d)" % what)
		_struck.clear()
		_cancelled.clear()
		pull.free()


func test_space_shoots_the_last_power() -> void:
	var setup := ShotSetup.new()
	var pull := _make(setup)
	check(not pull.shoot_last_power(), "nothing to shoot at zero power")
	setup.set_speed(9.0, 0.5)
	check(pull.shoot_last_power(), "shoots")
	var want := PullControl.speed_for_power(0.5, _max_speed())
	check_eq(_struck, [want] as Array[float], "at the power's speed")
	check_eq(setup.speed_m_s, want, "written to the setup")
	_pull_down(pull, Vector2(X, 100.0), 100.0)
	check(not pull.shoot_last_power(), "not during a pull")
	pull.free()


func test_pull_mode_starts_at_the_default_pace() -> void:
	var setup := ShotSetup.new()
	var pull := _make(setup)
	setup.set_strike_mode(ShotSetup.PULL)
	check_near(setup.speed_m_s, ShotSetup.DEFAULT_PACE_M_S, 1e-9, "the default pace")
	check_near(setup.power, PullControl.power_for_speed(ShotSetup.DEFAULT_PACE_M_S, _max_speed()), 1e-12, "its power")
	setup.set_speed(4.0, 0.2)
	setup.set_strike_mode(ShotSetup.STROKE)
	setup.set_strike_mode(ShotSetup.PULL)
	check_eq(setup.power, 0.2, "a power already set is kept")
	pull.free()


func test_mouse_pulls_and_emulated_mouse_is_ignored() -> void:
	for device: int in [InputEvent.DEVICE_ID_EMULATION, 0]:
		var pull := _make(ShotSetup.new())
		var press := InputEventMouseButton.new()
		press.device = device
		press.button_index = MOUSE_BUTTON_LEFT
		press.pressed = true
		press.position = Vector2(X, 100.0)
		pull._gui_input(press)
		var motion := InputEventMouseMotion.new()
		motion.device = device
		motion.position = Vector2(X, 300.0)
		pull._gui_input(motion)
		var release := press.duplicate() as InputEventMouseButton
		release.pressed = false
		release.position = motion.position
		pull._gui_input(release)
		check_eq(_struck.size(), 0 if device == InputEvent.DEVICE_ID_EMULATION else 1, "device %d" % device)
		pull.free()
