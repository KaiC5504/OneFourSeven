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


func test_tap_expands_and_close_collapses() -> void:
	var widget := _make()
	check(not widget.expanded, "starts collapsed")
	check_eq(widget.size, Vector2(SpinWidget.COLLAPSED_SIZE, SpinWidget.COLLAPSED_SIZE), "collapsed size")
	_touch(widget, true, Vector2(70.0, 70.0))
	_drag(widget, Vector2(140.0, 70.0))
	_touch(widget, false, Vector2(140.0, 70.0))
	check(not widget.expanded, "a drag is not a tap")
	_tap(widget, Vector2(70.0, 70.0))
	check(widget.expanded, "a tap expands")
	# Godot applies a new minimum size on the next frame.
	await tree.process_frame
	check_eq(widget.size, Vector2(SpinWidget.EXPANDED_SIZE, SpinWidget.EXPANDED_SIZE + SpinWidget.READOUT_HEIGHT), "expanded size")
	check_eq(widget.get_combined_minimum_size().x, SpinWidget.EXPANDED_SIZE, "grows through its minimum size")
	check_eq(_tips.size(), 0, "expanding doesn't move the tip")
	_tap(widget, widget.close_centre())
	check(not widget.expanded, "the close button collapses")
	widget.free()


func test_press_outside_collapses() -> void:
	var widget := _make()
	widget.expand()
	await tree.process_frame
	var inside := InputEventScreenTouch.new()
	inside.pressed = true
	inside.position = widget.position + Vector2(200.0, 200.0)
	widget._input(inside)
	check(widget.expanded, "a press inside keeps it open")
	var outside := InputEventScreenTouch.new()
	outside.pressed = true
	outside.position = widget.position + Vector2(SpinWidget.EXPANDED_SIZE + 50.0, 20.0)
	widget._input(outside)
	check(not widget.expanded, "a press outside collapses")
	widget.free()


func test_tip_mapping() -> void:
	var widget := _make()
	widget.expand()
	await tree.process_frame
	var c := widget.ball_centre()
	var r := widget.ball_radius()
	check(r > 150.0, "the expanded ball is large: %f" % r)
	check_eq(widget.tip_for_local(c), Vector2.ZERO, "centre is centre ball")
	check(widget.tip_for_local(c + Vector2(r * 0.5, 0.0)).is_equal_approx(Vector2(0.5, 0.0)), "right is +x")
	check(widget.tip_for_local(c + Vector2(0.0, -r * 0.4)).is_equal_approx(Vector2(0.0, 0.4)), "up is top, +y")
	check(widget.tip_for_local(c + Vector2(-r * 0.2, r * 0.3)).is_equal_approx(Vector2(-0.2, -0.3)), "left and screw")
	var tip := Vector2(0.31, -0.42)
	check(widget.tip_for_local(widget.local_for_tip(tip)).is_equal_approx(tip), "round trip")
	widget.collapse()
	await tree.process_frame
	var small := widget.ball_radius()
	check(small < 80.0, "collapsed ball is small")
	check(widget.tip_for_local(widget.ball_centre() + Vector2(small * 0.5, 0.0)).is_equal_approx(Vector2(0.5, 0.0)), "collapsed mapping")
	widget.free()


func test_tip_clamped_to_max() -> void:
	var widget := _make()
	widget.expand()
	await tree.process_frame
	var c := widget.ball_centre()
	var tip := widget.tip_for_local(c + Vector2(1000.0, -1000.0))
	check_near(tip.length(), SpinWidget.MAX_TIP, 1e-5, "clamped to MAX_TIP")
	check_near(tip.angle(), PI / 4.0, 1e-5, "in the dragged direction")
	widget.free()


func test_miscue_ring_radius() -> void:
	var widget := _make()
	widget.expand()
	await tree.process_frame
	var limit := Physics.param("miscue_limit", 0.55)
	check_near(widget.miscue_ring_radius(), limit * widget.ball_radius(), 1e-4, "ring at the miscue limit")
	check(SpinWidget.MAX_TIP > limit, "the marker can pass the ring")
	widget.free()


func test_drag_writes_the_tip() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	widget.expand()
	await tree.process_frame
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
	widget.free()


func test_collapsed_drag_does_not_move_the_tip() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	_touch(widget, true, Vector2(75.0, 75.0))
	_drag(widget, Vector2(110.0, 40.0))
	check_eq(_tips.size(), 0, "no tip change while collapsed")
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


func test_focus_lost_frees_the_widget() -> void:
	for what: int in [Node.NOTIFICATION_APPLICATION_FOCUS_OUT, Node.NOTIFICATION_APPLICATION_PAUSED]:
		var widget := _make()
		_touch(widget, true, Vector2(70.0, 70.0))
		widget._notification(what)
		_touch(widget, false, Vector2(70.0, 70.0))
		check(not widget.expanded, "the lost press does nothing on %d" % what)
		_tap(widget, Vector2(70.0, 70.0))
		check(widget.expanded, "a new tap expands after %d" % what)
		widget.free()


func test_collapse_ends_a_press() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	_touch(widget, true, Vector2(70.0, 70.0))
	widget.collapse()
	_tap(widget, Vector2(70.0, 70.0))
	check(widget.expanded, "a new tap expands")
	await tree.process_frame
	var c := widget.ball_centre()
	var r := widget.ball_radius()
	_touch(widget, true, c)
	widget.collapse()
	widget.expand()
	_drag(widget, c + Vector2(r * 0.5, 0.0))
	check_eq(setup.tip_x, 0.0, "the old finger moves nothing")
	_touch(widget, true, c + Vector2(r * 0.5, 0.0))
	check_near(setup.tip_x, 0.5, 1e-4, "a new press does")
	widget.free()


func test_expanded_inset_keeps_the_panel_clear_of_its_neighbour() -> void:
	var setup := ShotSetup.new()
	var widget := _make(setup)
	widget.expanded_inset = 300.0
	check_eq(widget.size, Vector2.ONE * SpinWidget.COLLAPSED_SIZE, "collapsed as before")
	widget.expand()
	await tree.process_frame
	var tall := SpinWidget.EXPANDED_SIZE + SpinWidget.READOUT_HEIGHT
	check_eq(widget.size, Vector2(300.0 + SpinWidget.EXPANDED_SIZE, tall), "grown by the inset")
	check_eq(widget.panel_rect(), Rect2(300.0, 0.0, SpinWidget.EXPANDED_SIZE, tall), "the panel past the inset")
	check(not widget._has_point(Vector2(150.0, 200.0)), "the strip takes no input")
	check(widget._has_point(Vector2(400.0, 200.0)), "the panel does")
	check_near(widget.ball_centre().x, 300.0 + SpinWidget.EXPANDED_SIZE / 2.0, 1e-9, "the ball in the panel")
	check(widget.panel_rect().has_point(widget.close_centre()), "the close button in the panel")
	check_eq(widget.tip_for_local(widget.ball_centre()), Vector2.ZERO, "centre ball at its centre")
	var c := widget.ball_centre()
	_tap(widget, c + Vector2(widget.ball_radius() * 0.3, 0.0))
	check_near(setup.tip_x, 0.3, 1e-4, "the tip maps in the panel")
	var strip := InputEventScreenTouch.new()
	strip.pressed = true
	strip.position = widget.position + Vector2(150.0, 200.0)
	widget._input(strip)
	check(not widget.expanded, "a press in the strip collapses it, like any press outside")
	await tree.process_frame
	check_eq(widget.size, Vector2.ONE * SpinWidget.COLLAPSED_SIZE, "collapsed size")
	check_eq(widget.panel_rect(), Rect2(Vector2.ZERO, widget.size), "collapsed: all of it")
	widget.free()


func test_emulated_mouse_is_ignored() -> void:
	var widget := _make()
	var e := InputEventMouseButton.new()
	e.button_index = MOUSE_BUTTON_LEFT
	e.device = InputEvent.DEVICE_ID_EMULATION
	e.position = Vector2(75.0, 75.0)
	e.pressed = true
	widget._gui_input(e)
	e = e.duplicate()
	e.pressed = false
	widget._gui_input(e)
	check(not widget.expanded, "emulated mouse ignored")
	e.device = 0
	e.pressed = true
	widget._gui_input(e)
	e = e.duplicate()
	e.pressed = false
	widget._gui_input(e)
	check(widget.expanded, "a real mouse click expands")
	widget.free()
