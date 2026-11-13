extends TestCase

var _shots := 0


func _make(setup: ShotSetup = null) -> PowerBar:
	var bar := PowerBar.new()
	tree.root.add_child(bar)
	bar.size = Vector2(140.0, 600.0)
	bar.bind(setup)
	bar.shoot_pressed.connect(func() -> void: _shots += 1)
	return bar


func _touch(control: Control, pressed: bool, pos: Vector2) -> void:
	var e := InputEventScreenTouch.new()
	e.index = 0
	e.pressed = pressed
	e.position = pos
	control._gui_input(e)


func _drag(control: Control, pos: Vector2) -> void:
	var e := InputEventScreenDrag.new()
	e.index = 0
	e.position = pos
	control._gui_input(e)


func test_curve() -> void:
	var top := 10.0
	check_eq(PowerBar.speed_for_power(0.0, top), 0.0, "0 at 0")
	check_near(PowerBar.speed_for_power(1.0, top), top, 1e-9, "max at 1")
	check_near(PowerBar.speed_for_power(0.5, top), top / 4.0, 1e-9, "finer at the low end")
	check_near(PowerBar.speed_for_power(1.5, top), top, 1e-9, "clamped")
	for p: float in [0.0, 0.1, 0.387, 0.75, 1.0]:
		check_near(PowerBar.power_for_speed(PowerBar.speed_for_power(p, top), top), p, 1e-9, "inverse at %s" % p)


func test_drag_sets_power_and_speed() -> void:
	var setup := ShotSetup.new()
	var bar := _make(setup)
	var powers: Array[float] = []
	bar.power_changed.connect(func(p: float) -> void: powers.append(p))
	var y := bar.local_y_for_power(0.6)
	_touch(bar, true, Vector2(70.0, y))
	check_near(setup.power, 0.6, 1e-4, "jumps to the press")
	_drag(bar, Vector2(70.0, bar.local_y_for_power(0.3)))
	_touch(bar, false, Vector2(70.0, bar.local_y_for_power(0.3)))
	var max_speed := Physics.param("max_cue_speed_m_s", 10.0)
	check_near(setup.power, 0.3, 1e-4, "power")
	check_near(setup.speed_m_s, PowerBar.speed_for_power(0.3, max_speed), 1e-4, "speed")
	check_near(powers[-1], 0.3, 1e-4, "power_changed")
	check_eq(bar.readout_text(), "30%%\n%.2f m/s" % PowerBar.speed_for_power(0.3, max_speed), "readout")
	check_eq(_shots, 0, "dragging never shoots")
	bar.free()


func test_shoot() -> void:
	var setup := ShotSetup.new()
	var bar := _make(setup)
	var button := bar.shoot_rect().get_center()
	_touch(bar, true, button)
	_touch(bar, false, button)
	check_eq(_shots, 0, "no shot at zero power")
	bar.set_power(0.5)
	_touch(bar, true, button)
	check_eq(_shots, 0, "fires on release, not press")
	_touch(bar, false, button)
	check_eq(_shots, 1, "shoot_pressed")
	_touch(bar, true, button)
	_drag(bar, Vector2(70.0, 50.0))
	_touch(bar, false, Vector2(70.0, 50.0))
	check_eq(_shots, 1, "released off the button: no shot")
	check_near(setup.power, 0.5, 1e-9, "a press on Shoot doesn't move the slider")
	bar.free()


func test_power_bar_mode_sets_the_speed() -> void:
	var setup := ShotSetup.new()
	var bar := _make(setup)
	setup.set_speed(4.2)
	setup.set_strike_mode(ShotSetup.POWER_BAR)
	check_near(setup.speed_m_s, ShotSetup.DEFAULT_PACE_M_S, 1e-6, "starts at the default pace")
	check(bar.can_shoot(), "ready to shoot")
	bar.set_power(0.2)
	setup.set_strike_mode(ShotSetup.STROKE)
	setup.set_speed(4.2)
	setup.set_strike_mode(ShotSetup.POWER_BAR)
	check_near(setup.speed_m_s, PowerBar.speed_for_power(0.2, Physics.param("max_cue_speed_m_s", 10.0)), 1e-6, "back to the bar's speed")
	check_near(setup.power, 0.2, 1e-9, "the bar's power kept")
	bar.free()
