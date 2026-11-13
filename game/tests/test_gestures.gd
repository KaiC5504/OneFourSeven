extends TestCase


class Recorder:
	var events: Array[Array] = []

	func _init(g: Gestures) -> void:
		g.drag_started.connect(func(p: Vector2) -> void: events.append(["drag_started", p]))
		g.drag_moved.connect(func(p: Vector2, r: Vector2) -> void: events.append(["drag_moved", p, r]))
		g.drag_ended.connect(func(p: Vector2) -> void: events.append(["drag_ended", p]))
		g.tapped.connect(func(p: Vector2) -> void: events.append(["tapped", p]))
		g.pinched.connect(func(f: float, c: Vector2) -> void: events.append(["pinched", f, c]))
		g.two_finger_dragged.connect(func(r: Vector2) -> void: events.append(["two_finger_dragged", r]))
		g.three_finger_tapped.connect(func() -> void: events.append(["three_finger_tapped"]))

	func names() -> Array[String]:
		var out: Array[String] = []
		for e: Array in events:
			out.append(e[0])
		return out

	func of(name: String) -> Array[Array]:
		var out: Array[Array] = []
		for e: Array in events:
			if e[0] == name:
				out.append(e)
		return out

	func count(name: String) -> int:
		return of(name).size()

	func pinch_product() -> float:
		var product := 1.0
		for e: Array in of("pinched"):
			product *= float(e[1])
		return product

	func two_finger_sum() -> Vector2:
		var total := Vector2.ZERO
		for e: Array in of("two_finger_dragged"):
			total += e[1] as Vector2
		return total


var _now := [0.0]


func _gestures() -> Gestures:
	var g := Gestures.new()
	g.clock = func() -> float: return _now[0]
	_now[0] = 10.0
	return g


func _down(g: Gestures, index: int, at: Vector2) -> void:
	var e := InputEventScreenTouch.new()
	e.index = index
	e.position = at
	e.pressed = true
	g.feed(e)


func _up(g: Gestures, index: int, at: Vector2, canceled: bool = false) -> void:
	var e := InputEventScreenTouch.new()
	e.index = index
	e.position = at
	e.pressed = false
	e.canceled = canceled
	g.feed(e)


func _move(g: Gestures, index: int, at: Vector2) -> void:
	var e := InputEventScreenDrag.new()
	e.index = index
	e.position = at
	g.feed(e)


func _button(g: Gestures, button: MouseButton, at: Vector2, pressed: bool,
		device: int = 0, factor: float = 1.0) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = button
	e.position = at
	e.pressed = pressed
	e.device = device
	e.factor = factor
	g.feed(e)


func _motion(g: Gestures, at: Vector2, relative: Vector2, device: int = 0, mask: int = 0) -> void:
	var e := InputEventMouseMotion.new()
	e.position = at
	e.relative = relative
	e.device = device
	e.button_mask = mask
	g.feed(e)


## Moves both fingers in `steps` equal steps from their start to their end positions.
func _move_two(g: Gestures, a0: Vector2, a1: Vector2, b0: Vector2, b1: Vector2, steps: int) -> void:
	for i in range(1, steps + 1):
		var t := float(i) / steps
		_move(g, 0, a0.lerp(a1, t))
		_move(g, 1, b0.lerp(b1, t))


func test_one_finger_drag() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(100, 100))
	_move(g, 0, Vector2(105, 100))
	check_eq(r.events.size(), 0, "nothing inside the slop")
	_move(g, 0, Vector2(120, 100))
	_move(g, 0, Vector2(150, 110))
	_up(g, 0, Vector2(150, 110))
	check_eq(r.names(), ["drag_started", "drag_moved", "drag_moved", "drag_ended"] as Array[String], "sequence")
	check_eq(r.events[0][1], Vector2(100, 100), "drag starts where the finger touched down")
	check_eq(r.events[1][2], Vector2(20, 0), "first move includes the slop")
	check_eq(r.events[2][2], Vector2(30, 10), "second move")
	check_eq(r.events[3][1], Vector2(150, 110), "drag ends where the finger lifted")
	g.free()


func test_short_still_touch_is_a_tap() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(200, 200))
	_move(g, 0, Vector2(203, 202))
	_now[0] += 0.1
	_up(g, 0, Vector2(203, 202))
	check_eq(r.names(), ["tapped"] as Array[String], "a tap only")
	check_eq(r.events[0][1], Vector2(200, 200), "at the touch-down point")
	g.free()


func test_long_still_touch_is_not_a_tap() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(200, 200))
	_now[0] += Gestures.TAP_MAX_S + 0.1
	_up(g, 0, Vector2(200, 200))
	check_eq(r.events.size(), 0, "no events")
	g.free()


func test_system_cancelled_touch_is_not_a_tap() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(200, 200))
	_up(g, 0, Vector2(200, 200), true)
	check_eq(r.events.size(), 0, "no tap")
	g.free()


func test_pinch_from_100_to_200_apart_is_factor_2() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(450, 400))
	_down(g, 1, Vector2(550, 400))
	_move_two(g, Vector2(450, 400), Vector2(400, 400), Vector2(550, 400), Vector2(600, 400), 10)
	check_near(r.pinch_product(), 2.0, 1e-9, "cumulative factor")
	check_eq(r.count("two_finger_dragged"), 0, "a zoom doesn't tilt")
	check_eq(r.count("drag_moved"), 0, "no aim drag")
	var last: Array = r.of("pinched")[-1]
	check_eq(last[2], Vector2(500, 400), "centre")
	_up(g, 0, Vector2(400, 400))
	_up(g, 1, Vector2(600, 400))
	check_eq(r.count("tapped"), 0, "no tap")
	g.free()


func test_one_finger_pinch_is_a_pinch() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(400, 400))
	_down(g, 1, Vector2(500, 400))
	for i in range(1, 11):
		_move(g, 1, Vector2(500 + 10 * i, 400))
	check_near(r.pinch_product(), 2.0, 1e-9, "factor")
	check_eq(r.count("two_finger_dragged"), 0, "no tilt")
	g.free()


func test_pinch_in_zooms_out() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(400, 400))
	_down(g, 1, Vector2(600, 400))
	_move_two(g, Vector2(400, 400), Vector2(450, 400), Vector2(600, 400), Vector2(550, 400), 10)
	check_near(r.pinch_product(), 0.5, 1e-9, "factor")
	g.free()


func test_two_finger_vertical_drag() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(400, 300))
	_down(g, 1, Vector2(600, 300))
	_move_two(g, Vector2(400, 300), Vector2(400, 400), Vector2(600, 300), Vector2(600, 400), 20)
	check_eq(r.two_finger_sum(), Vector2(0, 100), "total relative")
	check_eq(r.count("pinched"), 0, "a tilt doesn't zoom")
	check_eq(r.count("drag_moved"), 0, "no aim drag")
	g.free()


func test_wobbly_tilt_does_not_zoom() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(400, 300))
	_down(g, 1, Vector2(600, 300))
	for i in range(1, 31):
		var wobble := 3.0 if i % 2 == 0 else -3.0
		_move(g, 0, Vector2(400 - wobble, 300 + 5 * i))
		_move(g, 1, Vector2(600 + wobble, 300 + 5 * i))
	check_eq(r.count("pinched"), 0, "no zoom")
	check_near(r.two_finger_sum().y, 150.0, 1e-3, "tilted the whole way")
	g.free()


func test_pinch_switches_to_tilt_only_past_the_hysteresis() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(450, 400))
	_down(g, 1, Vector2(550, 400))
	_move_two(g, Vector2(450, 400), Vector2(400, 400), Vector2(550, 400), Vector2(600, 400), 5)
	check_eq(g.mode(), Gestures.Mode.PINCH, "pinching")
	_move_two(g, Vector2(400, 400), Vector2(400, 420), Vector2(600, 400), Vector2(600, 420), 4)
	check_eq(g.mode(), Gestures.Mode.PINCH, "a short parallel move stays a pinch")
	check_eq(r.count("two_finger_dragged"), 0, "no tilt yet")
	_move_two(g, Vector2(400, 420), Vector2(400, 520), Vector2(600, 420), Vector2(600, 520), 20)
	check_eq(g.mode(), Gestures.Mode.TWO_DRAG, "a long parallel move switches to a tilt")
	check(r.count("two_finger_dragged") > 0, "tilting after the switch")
	g.free()


func test_three_finger_tap() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(300, 300))
	_now[0] += 0.08
	_down(g, 1, Vector2(400, 300))
	_now[0] += 0.08
	_down(g, 2, Vector2(500, 300))
	_move(g, 1, Vector2(404, 303))
	_now[0] += 0.1
	_up(g, 0, Vector2(300, 300))
	_up(g, 1, Vector2(404, 303))
	_up(g, 2, Vector2(500, 300))
	check_eq(r.names(), ["three_finger_tapped"] as Array[String], "only the three-finger tap")
	g.free()


func test_three_fingers_landing_too_slowly_is_not_a_tap() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(300, 300))
	_down(g, 1, Vector2(400, 300))
	_now[0] += Gestures.THREE_FINGER_WINDOW_S + 0.05
	_down(g, 2, Vector2(500, 300))
	_up(g, 0, Vector2(300, 300))
	_up(g, 1, Vector2(400, 300))
	_up(g, 2, Vector2(500, 300))
	check_eq(r.count("three_finger_tapped"), 0, "too slow")
	g.free()


func test_three_fingers_that_move_are_not_a_tap() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(300, 300))
	_down(g, 1, Vector2(400, 300))
	_down(g, 2, Vector2(500, 300))
	_move(g, 2, Vector2(500, 340))
	_up(g, 0, Vector2(300, 300))
	_up(g, 1, Vector2(400, 300))
	_up(g, 2, Vector2(500, 340))
	check_eq(r.events.size(), 0, "nothing")
	g.free()


func test_second_finger_ends_the_drag_at_once() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(100, 400))
	_move(g, 0, Vector2(160, 400))
	_down(g, 1, Vector2(700, 400))
	check_eq(r.names(), ["drag_started", "drag_moved", "drag_ended"] as Array[String], "ended on the second touch")
	check_eq(r.events[2][1], Vector2(160, 400), "ended where the first finger was")
	_up(g, 1, Vector2(700, 400))
	_move(g, 0, Vector2(400, 400))
	_up(g, 0, Vector2(400, 400))
	check_eq(r.count("drag_moved"), 1, "the finger left behind doesn't drag")
	check_eq(r.count("drag_started"), 1, "no new drag")
	check_eq(r.count("tapped"), 0, "no tap")
	g.free()


func test_finger_left_after_a_pinch_does_not_drag() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(450, 400))
	_down(g, 1, Vector2(550, 400))
	_move_two(g, Vector2(450, 400), Vector2(400, 400), Vector2(550, 400), Vector2(600, 400), 5)
	_up(g, 1, Vector2(600, 400))
	_move(g, 0, Vector2(200, 400))
	_up(g, 0, Vector2(200, 400))
	check_eq(r.count("drag_started"), 0, "no drag")
	check_eq(r.count("tapped"), 0, "no tap")
	_down(g, 0, Vector2(100, 100))
	_move(g, 0, Vector2(130, 100))
	check_eq(r.count("drag_started"), 1, "a fresh touch drags again")
	g.free()


func test_mouse_left_drag_and_click() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_button(g, MOUSE_BUTTON_LEFT, Vector2(100, 100), true)
	_motion(g, Vector2(130, 100), Vector2(30, 0))
	_button(g, MOUSE_BUTTON_LEFT, Vector2(130, 100), false)
	check_eq(r.names(), ["drag_started", "drag_moved", "drag_ended"] as Array[String], "left-drag is a one-finger drag")
	check_eq(r.events[1][2], Vector2(30, 0), "relative")
	r.events.clear()
	_button(g, MOUSE_BUTTON_LEFT, Vector2(300, 300), true)
	_button(g, MOUSE_BUTTON_LEFT, Vector2(300, 300), false)
	check_eq(r.names(), ["tapped"] as Array[String], "a click is a tap")
	r.events.clear()
	_motion(g, Vector2(400, 300), Vector2(100, 0))
	check_eq(r.events.size(), 0, "a hover does nothing")
	g.free()


func test_mouse_right_drag_is_a_two_finger_drag() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_button(g, MOUSE_BUTTON_RIGHT, Vector2(500, 300), true)
	_motion(g, Vector2(500, 325), Vector2(0, 25), 0, MOUSE_BUTTON_MASK_RIGHT)
	_motion(g, Vector2(500, 335), Vector2(0, 10), 0, MOUSE_BUTTON_MASK_RIGHT)
	_button(g, MOUSE_BUTTON_RIGHT, Vector2(500, 335), false)
	_motion(g, Vector2(500, 400), Vector2(0, 65))
	check_eq(r.names(), ["two_finger_dragged", "two_finger_dragged"] as Array[String], "while held only")
	check_eq(r.two_finger_sum(), Vector2(0, 35), "relative")
	g.free()


func test_right_drag_ends_when_its_release_is_lost() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_button(g, MOUSE_BUTTON_RIGHT, Vector2(500, 300), true)
	_motion(g, Vector2(500, 325), Vector2(0, 25), 0, MOUSE_BUTTON_MASK_RIGHT)
	# Released over a HUD control: the release never reaches the gestures.
	_motion(g, Vector2(500, 400), Vector2(0, 75), 0, MOUSE_BUTTON_MASK_LEFT)
	_motion(g, Vector2(500, 450), Vector2(0, 50))
	check_eq(r.names(), ["two_finger_dragged"] as Array[String], "no tilt once the button is up")
	g.free()


func test_release_outside_the_viewport_is_not_a_move() -> void:
	var g := _gestures()
	tree.root.add_child(g)
	var r := Recorder.new(g)
	_down(g, 0, Vector2(100, 100))
	_move(g, 0, Vector2(150, 100))
	# How iOS may report a touch it cancelled.
	_up(g, 0, Vector2(-1, -1))
	check_eq(r.names(), ["drag_started", "drag_moved", "drag_ended"] as Array[String], "no move to the release point")
	check_eq(r.events[2][1], Vector2(150, 100), "the drag ends where the finger last was")
	_down(g, 0, Vector2(100, 100))
	_move(g, 0, Vector2(150, 100))
	_up(g, 0, Vector2(170, 110))
	check_eq(r.of("drag_moved")[-1][2], Vector2(20, 10), "a release inside still moves")
	g.free()


func test_mouse_wheel_is_pinch() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_button(g, MOUSE_BUTTON_WHEEL_UP, Vector2(10, 20), true)
	_button(g, MOUSE_BUTTON_WHEEL_UP, Vector2(10, 20), false)
	_button(g, MOUSE_BUTTON_WHEEL_DOWN, Vector2(10, 20), true, 0, 0.0)
	_button(g, MOUSE_BUTTON_WHEEL_DOWN, Vector2(10, 20), false)
	check_eq(r.count("pinched"), 2, "one per notch, on the press")
	check_near(float(r.of("pinched")[0][1]), 1.1, 1e-12, "up zooms in x1.1")
	check_near(float(r.of("pinched")[1][1]), 1.0 / 1.1, 1e-12, "down zooms out /1.1 (factor 0 is one notch)")
	check_eq(r.of("pinched")[0][2], Vector2(10, 20), "centre at the pointer")
	g.free()


func test_emulated_mouse_is_ignored() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	var emulated := InputEvent.DEVICE_ID_EMULATION
	_button(g, MOUSE_BUTTON_LEFT, Vector2(100, 100), true, emulated)
	_motion(g, Vector2(160, 100), Vector2(60, 0), emulated)
	_button(g, MOUSE_BUTTON_LEFT, Vector2(160, 100), false, emulated)
	_button(g, MOUSE_BUTTON_RIGHT, Vector2(100, 100), true, emulated)
	_motion(g, Vector2(100, 160), Vector2(0, 60), emulated)
	_button(g, MOUSE_BUTTON_WHEEL_UP, Vector2(100, 100), true, emulated)
	check_eq(r.events.size(), 0, "nothing from emulated mouse events")
	# The touch they were emulated from is the one that counts.
	_down(g, 0, Vector2(100, 100))
	_button(g, MOUSE_BUTTON_LEFT, Vector2(100, 100), true, emulated)
	_move(g, 0, Vector2(160, 100))
	_motion(g, Vector2(160, 100), Vector2(60, 0), emulated)
	check_eq(r.count("drag_moved"), 1, "one drag, not two")
	g.free()


func test_disabled_ignores_everything() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	g.enabled = false
	_down(g, 0, Vector2(100, 100))
	_move(g, 0, Vector2(200, 100))
	_up(g, 0, Vector2(200, 100))
	_button(g, MOUSE_BUTTON_WHEEL_UP, Vector2(10, 10), true)
	_button(g, MOUSE_BUTTON_RIGHT, Vector2(10, 10), true)
	_motion(g, Vector2(10, 40), Vector2(0, 30))
	check_eq(r.events.size(), 0, "no events while disabled")
	g.free()


func test_disabling_mid_drag_ends_it_and_forgets_the_finger() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(100, 100))
	_move(g, 0, Vector2(150, 100))
	g.enabled = false
	check_eq(r.names(), ["drag_started", "drag_moved", "drag_ended"] as Array[String], "drag ended")
	g.enabled = true
	_move(g, 0, Vector2(300, 100))
	_up(g, 0, Vector2(300, 100))
	check_eq(r.events.size(), 3, "the old finger starts nothing")
	g.free()


func test_focus_lost_ends_the_drag() -> void:
	var g := _gestures()
	var r := Recorder.new(g)
	_down(g, 0, Vector2(100, 100))
	_move(g, 0, Vector2(150, 100))
	g._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	check_eq(r.names()[-1], "drag_ended", "drag ended")
	_move(g, 0, Vector2(250, 100))
	check_eq(r.count("drag_moved"), 1, "no more moves")
	g.free()
