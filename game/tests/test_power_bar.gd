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


func test_cancel_press_frees_the_bar() -> void:
	var setup := ShotSetup.new()
	var bar := _make(setup)
	bar.set_power(0.5)
	_touch(bar, true, Vector2(70.0, bar.local_y_for_power(0.5)))
	bar.cancel_press()
	_drag(bar, Vector2(70.0, bar.local_y_for_power(0.8)))
	check_near(setup.power, 0.5, 1e-9, "the old finger moves nothing")
	_touch(bar, false, Vector2(70.0, bar.local_y_for_power(0.8)))
	var button := bar.shoot_rect().get_center()
	_touch(bar, true, button)
	bar.cancel_press()
	_touch(bar, false, button)
	check_eq(_shots, 0, "a cancelled press on Shoot never shoots")
	_touch(bar, true, button)
	_touch(bar, false, button)
	check_eq(_shots, 1, "a new press shoots")
	bar.free()


func test_focus_lost_frees_the_bar() -> void:
	for what: int in [Node.NOTIFICATION_APPLICATION_FOCUS_OUT, Node.NOTIFICATION_APPLICATION_PAUSED]:
		var setup := ShotSetup.new()
		var bar := _make(setup)
		bar.set_power(0.5)
		_touch(bar, true, Vector2(70.0, bar.local_y_for_power(0.5)))
		bar._notification(what)
		_drag(bar, Vector2(70.0, bar.local_y_for_power(0.8)))
		check_near(setup.power, 0.5, 1e-9, "no move after %d" % what)
		_touch(bar, true, Vector2(70.0, bar.local_y_for_power(0.3)))
		check_near(setup.power, 0.3, 1e-4, "a new press works after %d" % what)
		bar.free()


func test_speed_follows_new_parameters_and_resets() -> void:
	var setup := ShotSetup.new()
	setup.set_strike_mode(ShotSetup.POWER_BAR)
	var bar := _make(setup)
	bar.set_power(0.5)
	var old_text := Params.current_text()
	var old_hash := Params.current_hash()
	# The text in use may be the embedded file or OfsPhysics.member_json's compact copy of it.
	var value := RegEx.create_from_string('("max_cue_speed_m_s"\\s*:\\s*)10(\\.0)?\\b')
	var new_text := value.sub(old_text, "${1}8.0")
	check(new_text != old_text, "the test changes the maximum")
	var files := '{"table_params":%s}' % new_text
	Params.apply_remote(files, OfsLog.content_hash(files))
	Params.commit_pending("test-power-bar")
	check_near(Physics.param("max_cue_speed_m_s", 0.0), 8.0, 1e-12, "the new maximum in use")
	check_eq(setup.speed_m_s, bar.speed(), "the setup's speed is the readout's")
	check_near(setup.speed_m_s, 2.0, 1e-12, "half power at 8 m/s")
	check_near(setup.power, 0.5, 1e-12, "power kept")

	# A harness shot writes a speed without a power; the reset after it puts the bar's back.
	setup.set_speed(7.0)
	setup.after_shot(true)
	check_eq(setup.speed_m_s, bar.speed(), "after the shot: the bar's speed")
	# A retake restores the snapshot's power, at the maximum now in use.
	var snap := setup.snapshot()
	snap["speed_m_s"] = 2.5
	setup.restore(snap)
	check_eq(setup.speed_m_s, bar.speed(), "after a restore: the bar's speed")

	Params.apply_remote('{"table_params":%s}' % old_text, old_hash)
	Params.commit_pending("test-restore")
	check_eq(Params.current_hash(), old_hash, "parameters restored")
	check_eq(setup.speed_m_s, bar.speed(), "and the speed follows them back")
	check_near(setup.speed_m_s, 2.5, 1e-12, "half power at 10 m/s")

	# In stroke mode the stroke's speed stands.
	setup.set_strike_mode(ShotSetup.STROKE)
	setup.set_speed(3.3)
	setup.after_shot(true)
	check_eq(setup.speed_m_s, 3.3, "stroke mode: untouched by a reset")
	bar.free()


func test_shown_bar_writes_its_speed() -> void:
	var setup := ShotSetup.new()
	var bar := _make(setup)
	bar.set_power(0.4)
	bar.hide()
	setup.strike_mode = ShotSetup.POWER_BAR
	setup.speed_m_s = 9.0
	bar.show()
	check_eq(setup.speed_m_s, bar.speed(), "shown: the setup strikes at the readout's speed")
	bar.free()
