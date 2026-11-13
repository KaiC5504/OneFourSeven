extends TestCase

const MAX := 80.0 * PI / 180.0


func _make(setup: ShotSetup = null) -> ElevationControl:
	var control := ElevationControl.new()
	tree.root.add_child(control)
	control.size = Vector2(120.0, 420.0)
	control.bind(setup)
	return control


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


func test_scale() -> void:
	check_eq(ElevationControl.elevation_for_travel(0.0, MAX), 0.0, "bottom is level")
	check_near(ElevationControl.elevation_for_travel(1.0, MAX), MAX, 1e-9, "top is the maximum")
	check_near(rad_to_deg(ElevationControl.elevation_for_travel(0.5, MAX)), 20.0, 1e-9, "half the travel is 20°")
	check_near(ElevationControl.travel_for_elevation(deg_to_rad(20.0), MAX), 0.5, 1e-9, "20° is half the travel")
	for deg: float in [0.0, 2.5, 7.0, 15.0, 34.0, 60.0, 80.0]:
		var t := ElevationControl.travel_for_elevation(deg_to_rad(deg), MAX)
		check_near(rad_to_deg(ElevationControl.elevation_for_travel(t, MAX)), deg, 1e-9, "round trip at %s°" % deg)
	# Another maximum keeps 20° at half the travel.
	var other := deg_to_rad(85.0)
	check_near(rad_to_deg(ElevationControl.elevation_for_travel(0.5, other)), 20.0, 1e-9, "20° at half for 85°")
	check_near(ElevationControl.elevation_for_travel(2.0, MAX), MAX, 1e-9, "clamped above")
	check_eq(ElevationControl.elevation_for_travel(-1.0, MAX), 0.0, "clamped below")


func test_local_both_ways() -> void:
	var control := _make()
	var max_rad := Physics.param("max_elevation_rad", MAX)
	var bottom := control.local_y_for_elevation(0.0)
	var top := control.local_y_for_elevation(max_rad)
	check(top < bottom, "up the screen is higher")
	check_near(control.local_y_for_elevation(deg_to_rad(20.0)), (top + bottom) / 2.0, 1e-3, "20° halfway up")
	for deg: float in [3.0, 12.0, 45.0]:
		var y := control.local_y_for_elevation(deg_to_rad(deg))
		check_near(rad_to_deg(control.elevation_for_local(Vector2(60.0, y))), deg, 1e-3, "local round trip at %s°" % deg)
	control.free()


func test_drag_sets_the_elevation() -> void:
	var setup := ShotSetup.new()
	var control := _make(setup)
	var changes: Array[float] = []
	control.elevation_changed.connect(func(rad: float) -> void: changes.append(rad))
	var bottom := control.local_y_for_elevation(0.0)
	var top := control.local_y_for_elevation(Physics.param("max_elevation_rad", MAX))
	# Grabbing the thumb at the bottom and dragging it halfway up.
	_touch(control, true, Vector2(60.0, bottom - 10.0))
	check_eq(changes.size(), 0, "grabbing the thumb doesn't move it")
	_drag(control, Vector2(60.0, (top + bottom) / 2.0 - 10.0))
	check_near(rad_to_deg(setup.elevation_rad), 20.0, 1e-3, "halfway up is 20°")
	check_near(changes[-1], setup.elevation_rad, 1e-9, "elevation_changed")
	_drag(control, Vector2(60.0, -500.0))
	check_near(setup.elevation_rad, Physics.param("max_elevation_rad", MAX), 1e-9, "stops at the maximum")
	_touch(control, false, Vector2(60.0, -500.0))
	# A press away from the thumb jumps there.
	_touch(control, true, Vector2(60.0, control.local_y_for_elevation(deg_to_rad(10.0))))
	check_near(rad_to_deg(setup.elevation_rad), 10.0, 1e-3, "jumps to the press")
	control.free()


func test_forced() -> void:
	var setup := ShotSetup.new()
	var control := _make(setup)
	setup.set_elevation(deg_to_rad(5.0))
	check(not control.is_forced(), "not forced with no minimum")
	check_eq(control.readout_text(), "5.0°", "plain readout")
	setup.set_elevation_min(deg_to_rad(34.0))
	check(control.is_forced(), "forced when the minimum is higher")
	check_near(control.effective(), deg_to_rad(34.0), 1e-9, "effective is the minimum")
	check_near(control.requested(), deg_to_rad(5.0), 1e-9, "the request is kept")
	check_eq(control.readout_text(), "raised to 34°", "forced readout")
	setup.set_elevation(deg_to_rad(40.0))
	check(not control.is_forced(), "not forced above the minimum")
	check_eq(control.readout_text(), "40°", "readout above the minimum")
	control.free()


func test_a_small_raise_reads_as_normal() -> void:
	var setup := ShotSetup.new()
	var control := _make(setup)
	# The baulk-line case: the butt clears the rail at about 2°.
	setup.set_elevation(0.0)
	setup.set_elevation_min(deg_to_rad(2.0))
	check(control.is_forced(), "still forced")
	check(not control.shows_forced(), "but not shown as forced")
	check_eq(control.readout_text(), "2.0°", "the effective elevation, plainly")
	setup.set_elevation(deg_to_rad(5.0))
	setup.set_elevation_min(deg_to_rad(7.9))
	check(not control.shows_forced(), "2.9° above the request")
	check_eq(control.readout_text(), "7.9°", "plain")
	setup.set_elevation_min(deg_to_rad(8.0))
	check(control.shows_forced(), "3° above the request")
	check_eq(control.readout_text(), "raised to 8.0°", "shown as forced")
	control.free()


func test_cancel_press_frees_the_control() -> void:
	var setup := ShotSetup.new()
	var control := _make(setup)
	var y10 := control.local_y_for_elevation(deg_to_rad(10.0))
	var y30 := control.local_y_for_elevation(deg_to_rad(30.0))
	_touch(control, true, Vector2(60.0, y10))
	control.cancel_press()
	_drag(control, Vector2(60.0, y30))
	check_near(rad_to_deg(setup.elevation_rad), 10.0, 1e-3, "the old finger moves nothing")
	_touch(control, true, Vector2(60.0, y30))
	check_near(rad_to_deg(setup.elevation_rad), 30.0, 1e-3, "a new press works")
	control.free()


func test_focus_lost_frees_the_control() -> void:
	for what: int in [Node.NOTIFICATION_APPLICATION_FOCUS_OUT, Node.NOTIFICATION_APPLICATION_PAUSED]:
		var setup := ShotSetup.new()
		var control := _make(setup)
		_touch(control, true, Vector2(60.0, control.local_y_for_elevation(deg_to_rad(10.0))))
		control._notification(what)
		var y := control.local_y_for_elevation(deg_to_rad(30.0))
		_drag(control, Vector2(60.0, y))
		check_near(rad_to_deg(setup.elevation_rad), 10.0, 1e-3, "no move after %d" % what)
		_touch(control, true, Vector2(60.0, y))
		check_near(rad_to_deg(setup.elevation_rad), 30.0, 1e-3, "a new press works after %d" % what)
		control.free()
