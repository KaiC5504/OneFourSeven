extends TestCase

var _tips: Array[Vector2] = []


func _make(setup: ShotSetup = null) -> SpinWidget:
	var widget := SpinWidget.new()
	tree.root.add_child(widget)
	widget.position = Vector2(100.0, 100.0)
	widget.bind(setup)
	widget.tip_changed.connect(func(tip: Vector2) -> void: _tips.append(tip))
	return widget


func _touch(control: Control, pressed: bool, pos: Vector2, double_tap: bool = false) -> void:
	var e := InputEventScreenTouch.new()
	e.index = 0
	e.pressed = pressed
	e.double_tap = double_tap
	e.position = pos
	control._gui_input(e)


func _drag(control: Control, pos: Vector2) -> void:
	var e := InputEventScreenDrag.new()
	e.index = 0
	e.position = pos
	control._gui_input(e)


func _tap(control: Control, pos: Vector2) -> void:
	_touch(control, true, pos)
	_touch(control, false, pos)


func test_always_the_same_size() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	check_eq(widget.size, Vector2.ONE * SpinWidget.SIZE, "its size")
	# KaiC: bigger, so the hit point is easy to see; 120 pt on the phone.
	check(SpinWidget.SIZE >= 230.0 and SpinWidget.SIZE <= 280.0, "230 to 280 units across")
	var c := widget.ball_centre()
	_tap(widget, c)
	_tap(widget, c + Vector2(0.0, -40.0))
	await tree.process_frame
	check_eq(widget.size, Vector2.ONE * SpinWidget.SIZE, "taps never enlarge it")
	widget.free()


func test_a_press_never_moves_the_crosshair() -> void:
	# KaiC: the crosshair must not jump to the finger, which would hide the point it sets.
	var setup := ShotSetup.new()
	var widget := _make(setup)
	var c := widget.ball_centre()
	var r := widget.ball_radius()
	_touch(widget, true, c + Vector2(r * 0.5, r * 0.5))
	check_eq(Vector2(setup.tip_x, setup.tip_y), Vector2.ZERO, "pressing off centre leaves centre ball")
	check_eq(_tips.size(), 0, "no tip change")
	_touch(widget, false, c + Vector2(r * 0.5, r * 0.5))
	setup.set_tip(0.1, -0.2)
	_tap(widget, c)
	check(Vector2(setup.tip_x, setup.tip_y).is_equal_approx(Vector2(0.1, -0.2)), "a tap elsewhere keeps the tip")
	widget.free()


func test_the_crosshair_moves_by_the_fingers_movement() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	var c := widget.ball_centre()
	var r := widget.ball_radius()
	var reach := widget.reach()
	# Start low on the ball and move up: the crosshair moves up from centre, not to the finger.
	var start := c + Vector2(0.0, r * 0.6)
	_touch(widget, true, start)
	_drag(widget, start + Vector2(0.0, -r * 0.4))
	check_near(setup.tip_y, 0.4 * SpinWidget.DRAG_GAIN * reach, 1e-4, "top by the finger's movement times the gain")
	check_near(setup.tip_x, 0.0, 1e-4, "no side")
	check(_tips[-1].is_equal_approx(Vector2(setup.tip_x, setup.tip_y)), "tip_changed")
	# Movements add up, in any direction.
	_drag(widget, start + Vector2(-r * 0.2, -r * 0.4))
	check_near(setup.tip_x, -0.2 * SpinWidget.DRAG_GAIN * reach, 1e-4, "then left")
	check_near(setup.tip_y, 0.4 * SpinWidget.DRAG_GAIN * reach, 1e-4, "top kept")
	_touch(widget, false, start + Vector2(-r * 0.2, -r * 0.4))
	# A second press carries on from where the crosshair is.
	var before := Vector2(setup.tip_x, setup.tip_y)
	_touch(widget, true, c)
	_drag(widget, c + Vector2(r * 0.1, 0.0))
	check_near(setup.tip_x, before.x + 0.1 * SpinWidget.DRAG_GAIN * reach, 1e-4, "from where it was")
	_touch(widget, false, c + Vector2(r * 0.1, 0.0))
	widget.free()


func test_the_whole_ball_can_be_struck_and_none_of_it_miscues() -> void:
	# KaiC: the whole ball can be hit, with no miscue rim: the drawn edge is the reach, just
	# inside the miscue limit.
	var setup := ShotSetup.new()
	var widget := _make(setup)
	var c := widget.ball_centre()
	var r := widget.ball_radius()
	var limit := Physics.param("miscue_limit", 0.55)
	check_near(widget.reach(), limit * SpinWidget.EDGE, 1e-6, "the reach")
	check(widget.reach() < limit, "inside the miscue limit")
	_touch(widget, true, c)
	for i in range(1, 9):
		_drag(widget, c + Vector2(0.0, -r * 0.5 * i))
	check_near(setup.tip_y, widget.reach(), 1e-4, "dragged far up: stops at the reach")
	check(widget.local_for_tip(Vector2(setup.tip_x, setup.tip_y)).distance_to(c + Vector2(0.0, -r)) < 1e-3,
			"drawn on the ball's edge")
	check_eq(SpinWidget.describe(Vector2(setup.tip_x, setup.tip_y), limit), "top %.2f" % widget.reach(),
			"no miscue at the edge")
	check(Vector2(setup.tip_x, setup.tip_y).length() <= limit, "the edge is a fair hit")
	# Straight back down by the finger's movement, no lag from the overshoot.
	_drag(widget, c + Vector2(0.0, -r * 4.0 + r * 0.2))
	check_near(setup.tip_y, widget.reach() - 0.2 * SpinWidget.DRAG_GAIN * widget.reach(), 1e-4, "back at once")
	check(setup.tip_y < limit, "inside the limit again")
	_touch(widget, false, c)
	widget.free()


func test_tip_mapping() -> void:
	var widget := _make()
	var c := widget.ball_centre()
	var r := widget.ball_radius()
	var reach := widget.reach()
	check_near(r, SpinWidget.SIZE / 2.0 - SpinWidget.MARGIN, 1e-9, "the ball fills the widget")
	check_eq(widget.tip_for_local(c), Vector2.ZERO, "centre is centre ball")
	check(widget.tip_for_local(c + Vector2(r * 0.5, 0.0)).is_equal_approx(Vector2(0.5 * reach, 0.0)), "right is +x")
	check(widget.tip_for_local(c + Vector2(0.0, -r * 0.4)).is_equal_approx(Vector2(0.0, 0.4 * reach)), "up is top, +y")
	check(widget.tip_for_local(c + Vector2(-r * 0.2, r * 0.3)).is_equal_approx(Vector2(-0.2, -0.3) * reach), "left and screw")
	var tip := Vector2(0.31, -0.42)
	check(widget.tip_for_local(widget.local_for_tip(tip)).is_equal_approx(tip), "round trip")
	widget.free()


func test_tip_clamped_to_the_reach() -> void:
	var widget := _make()
	var c := widget.ball_centre()
	var tip := widget.tip_for_local(c + Vector2(1000.0, -1000.0))
	check_near(tip.length(), widget.reach(), 1e-5, "clamped to the reach")
	check_near(tip.angle(), PI / 4.0, 1e-5, "in the dragged direction")
	check(widget.reach() <= 1.0, "never past the ball's edge")
	widget.free()


func test_double_tap_centres() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	var c := widget.ball_centre()
	setup.set_tip(0.2, 0.3)
	_touch(widget, true, c + Vector2(30.0, 0.0), true)
	check_eq(Vector2(setup.tip_x, setup.tip_y), Vector2.ZERO, "double tap centres")
	_drag(widget, c + Vector2(90.0, 0.0))
	check_eq(Vector2(setup.tip_x, setup.tip_y), Vector2.ZERO, "and the rest of that touch does nothing")
	_touch(widget, false, c + Vector2(90.0, 0.0))
	widget.free()


func test_the_touch_area_is_the_ball() -> void:
	var widget := _make()
	check(widget._has_point(widget.ball_centre()), "the middle")
	check(widget._has_point(widget.ball_centre() + Vector2(SpinWidget.SIZE / 2.0 - 1.0, 0.0)), "the rim")
	check(not widget._has_point(Vector2(2.0, 2.0)), "not the square's corner")
	widget.free()


func test_follows_the_setup() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	setup.set_tip(0.2, 0.3)
	check_eq(widget.current_tip(), Vector2(0.2, 0.3), "shows the setup's tip")
	setup.after_shot(true)
	check_eq(widget.current_tip(), Vector2.ZERO, "back to centre after the shot")
	widget.free()


func test_readout() -> void:
	check_eq(SpinWidget.describe(Vector2(-0.2, -0.4), 0.55), "screw 0.40 · left 0.20", "screw and left")
	check_eq(SpinWidget.describe(Vector2(0.15, 0.3), 0.55), "top 0.30 · right 0.15", "top and right")
	check_eq(SpinWidget.describe(Vector2.ZERO, 0.55), "centre ball", "centre")
	check_eq(SpinWidget.describe(Vector2(0.0, 0.6), 0.55), "top 0.60 · miscue", "past the ring")


func test_readout_shows_while_dragging_or_off_centre() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	check(not widget.readout_rect().has_area(), "hidden at centre ball")
	var c := widget.ball_centre()
	_touch(widget, true, c)
	check(widget.readout_rect().has_area(), "shown while the finger is down")
	_touch(widget, false, c)
	check(not widget.readout_rect().has_area(), "hidden again at centre ball")
	setup.set_tip(0.0, -0.3)
	var box := widget.readout_rect()
	check(box.has_area(), "shown off centre")
	check(box.position.x >= widget.size.x, "beside the ball, not over it: %s" % box)
	check(SpinWidget.FONT_SIZE >= 22, "its text at least 11 pt on the phone")
	widget.free()


func test_focus_lost_frees_the_widget() -> void:
	for what: int in [Node.NOTIFICATION_APPLICATION_FOCUS_OUT, Node.NOTIFICATION_APPLICATION_PAUSED]:
		var setup := ShotSetup.new()
		var widget := _make(setup)
		var c := widget.ball_centre()
		_touch(widget, true, c)
		widget._notification(what)
		check(not widget.is_pressed(), "the press ends on %d" % what)
		_drag(widget, c + Vector2(60.0, 0.0))
		check_eq(setup.tip_x, 0.0, "the lost finger moves nothing on %d" % what)
		_touch(widget, true, c)
		_drag(widget, c + Vector2(60.0, 0.0))
		check(setup.tip_x > 0.0, "a new press works after %d" % what)
		widget.free()


func test_cancel_press_ends_a_press() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	var c := widget.ball_centre()
	_touch(widget, true, c)
	widget.cancel_press("shot started")
	_drag(widget, c + Vector2(widget.ball_radius() * 0.7, 0.0))
	check_eq(setup.tip_x, 0.0, "the old finger moves nothing")
	_touch(widget, true, c)
	_drag(widget, c + Vector2(widget.ball_radius() * 0.4, 0.0))
	check_near(setup.tip_x, 0.4 * SpinWidget.DRAG_GAIN * widget.reach(), 1e-4, "a new press does")
	widget.free()


func test_emulated_mouse_is_ignored() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	var e := InputEventMouseButton.new()
	e.button_index = MOUSE_BUTTON_LEFT
	e.device = InputEvent.DEVICE_ID_EMULATION
	e.position = widget.ball_centre() + Vector2(60.0, 0.0)
	e.pressed = true
	widget._gui_input(e)
	check_eq(setup.tip_x, 0.0, "emulated mouse ignored")
	var motion := InputEventMouseMotion.new()
	motion.device = InputEvent.DEVICE_ID_EMULATION
	motion.position = e.position + Vector2(40.0, 0.0)
	widget._gui_input(motion)
	check_eq(setup.tip_x, 0.0, "emulated drag ignored")
	e = e.duplicate()
	e.device = 0
	widget._gui_input(e)
	motion = motion.duplicate()
	motion.device = 0
	widget._gui_input(motion)
	check(setup.tip_x > 0.0, "a real mouse drag moves the tip")
	widget.free()
