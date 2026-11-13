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
	check(SpinWidget.SIZE >= 180.0 and SpinWidget.SIZE <= 220.0, "180 to 220 units across")
	var c := widget.ball_centre()
	_tap(widget, c)
	_tap(widget, c + Vector2(0.0, -40.0))
	await tree.process_frame
	check_eq(widget.size, Vector2.ONE * SpinWidget.SIZE, "taps never enlarge it")
	widget.free()


func test_a_press_anywhere_on_the_ball_puts_the_tip_there() -> void:
	# KaiC: the hit point adjusted directly, with no tap-to-enlarge step.
	var setup := ShotSetup.new()
	var widget := _make(setup)
	var c := widget.ball_centre()
	var r := widget.ball_radius()
	_touch(widget, true, c + Vector2(0.0, -r * 0.6))
	check_near(setup.tip_y, 0.6, 1e-4, "top at the first touch")
	check_near(setup.tip_x, 0.0, 1e-4, "no side")
	_drag(widget, c + Vector2(-r * 0.3, r * 0.5))
	check_near(setup.tip_x, -0.3, 1e-4, "dragged: left")
	check_near(setup.tip_y, -0.5, 1e-4, "dragged: screw")
	_touch(widget, false, c + Vector2(-r * 0.3, r * 0.5))
	widget.free()


func test_tip_mapping() -> void:
	var widget := _make()
	var c := widget.ball_centre()
	var r := widget.ball_radius()
	check_near(r, SpinWidget.SIZE / 2.0 - SpinWidget.MARGIN, 1e-9, "the ball fills the widget")
	check_eq(widget.tip_for_local(c), Vector2.ZERO, "centre is centre ball")
	check(widget.tip_for_local(c + Vector2(r * 0.5, 0.0)).is_equal_approx(Vector2(0.5, 0.0)), "right is +x")
	check(widget.tip_for_local(c + Vector2(0.0, -r * 0.4)).is_equal_approx(Vector2(0.0, 0.4)), "up is top, +y")
	check(widget.tip_for_local(c + Vector2(-r * 0.2, r * 0.3)).is_equal_approx(Vector2(-0.2, -0.3)), "left and screw")
	var tip := Vector2(0.31, -0.42)
	check(widget.tip_for_local(widget.local_for_tip(tip)).is_equal_approx(tip), "round trip")
	widget.free()


func test_tip_clamped_to_max() -> void:
	var widget := _make()
	var c := widget.ball_centre()
	var tip := widget.tip_for_local(c + Vector2(1000.0, -1000.0))
	check_near(tip.length(), SpinWidget.MAX_TIP, 1e-5, "clamped to MAX_TIP")
	check_near(tip.angle(), PI / 4.0, 1e-5, "in the dragged direction")
	widget.free()


func test_miscue_ring_radius() -> void:
	var widget := _make()
	var limit := Physics.param("miscue_limit", 0.55)
	check_near(widget.miscue_ring_radius(), limit * widget.ball_radius(), 1e-4, "ring at the miscue limit")
	check(SpinWidget.MAX_TIP > limit, "the marker can pass the ring")
	widget.free()


func test_drag_writes_the_tip() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	var c := widget.ball_centre()
	var r := widget.ball_radius()
	# Grab the marker a little off its centre: it doesn't jump to the finger.
	_touch(widget, true, c + Vector2(10.0, 10.0))
	check_eq(_tips.size(), 0, "grabbing doesn't move it")
	_drag(widget, c + Vector2(10.0 - r * 0.2, 10.0 + r * 0.4))
	check_near(setup.tip_x, -0.2, 1e-4, "setup tip x")
	check_near(setup.tip_y, -0.4, 1e-4, "setup tip y")
	check(_tips[-1].is_equal_approx(Vector2(setup.tip_x, setup.tip_y)), "tip_changed")
	# Past the ring is allowed.
	_drag(widget, c + Vector2(10.0, 10.0 + r * 0.7))
	check_near(setup.tip_y, -0.7, 1e-4, "past the miscue ring")
	_touch(widget, false, c + Vector2(10.0, 10.0 + r * 0.7))
	check_eq(SpinWidget.describe(Vector2(setup.tip_x, setup.tip_y), 0.55), "screw 0.70 · miscue", "readout warns")
	# A press away from the marker puts it there.
	_tap(widget, c + Vector2(r * 0.3, -r * 0.1))
	check_near(setup.tip_x, 0.3, 1e-4, "tap places x")
	check_near(setup.tip_y, 0.1, 1e-4, "tap places y")
	_touch(widget, true, c + Vector2(r * 0.3, -r * 0.1), true)
	check_eq(Vector2(setup.tip_x, setup.tip_y), Vector2.ZERO, "double tap centres")
	_drag(widget, c + Vector2(r * 0.5, 0.0))
	check_eq(Vector2(setup.tip_x, setup.tip_y), Vector2.ZERO, "and the rest of that touch does nothing")
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
		# Further than GRAB_RADIUS from the marker, so the press places it.
		_touch(widget, true, c + Vector2(60.0, 0.0))
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
	_touch(widget, true, c + Vector2(widget.ball_radius() * 0.7, 0.0))
	check_near(setup.tip_x, 0.7, 1e-4, "a new press does")
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
	e = e.duplicate()
	e.device = 0
	widget._gui_input(e)
	check(setup.tip_x > 0.0, "a real mouse press places the tip")
	widget.free()
