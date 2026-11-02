extends TestCase

const PerfScript := preload("res://scripts/autoload/perf.gd")
const SCHEMA_PATH := "res://data/log_schema.json"


func _window(frames: Array, target_ms: float = 1000.0 / 60.0) -> PerfScript.FrameWindow:
	var window := PerfScript.FrameWindow.new(target_ms)
	for ms: float in frames:
		window.add(ms)
	return window


func test_summary_of_known_frames() -> void:
	var s := _window([10.0, 10.0, 10.0, 10.0, 100.0]).summary()
	check_near(s["frame_ms_avg"], 28.0, 1e-4, "avg")
	check_near(s["frame_ms_max"], 100.0, 1e-4, "max")
	check_near(s["frame_ms_p95"], 100.0, 1e-4, "p95")
	check_eq(s["dropped_frames"], 1, "dropped")


func test_p95_is_nearest_rank() -> void:
	var frames := []
	for i in range(20, 0, -1):
		frames.append(float(i))
	check_near(_window(frames).summary()["frame_ms_p95"], 19.0, 1e-4, "p95 of 1..20")


func test_frames_past_the_buffer_still_count() -> void:
	var window := _window([], 1000.0 / 120.0)
	for i in PerfScript.BUFFER_SIZE + 100:
		window.add(4.0)
	window.add(500.0)
	window.add(30.0)
	var s := window.summary()
	check_eq(window.count, PerfScript.BUFFER_SIZE + 102, "count")
	check_near(s["frame_ms_max"], 500.0, 1e-4, "max includes a frame past the buffer")
	check_eq(s["dropped_frames"], 2, "dropped includes frames past the buffer")
	var expected_avg := (4.0 * (PerfScript.BUFFER_SIZE + 100) + 530.0) / (PerfScript.BUFFER_SIZE + 102)
	check_near(s["frame_ms_avg"], expected_avg, 1e-3, "avg over every frame")


func test_reset_clears_the_window() -> void:
	var window := _window([500.0, 500.0])
	window.reset()
	window.add(8.0)
	var s := window.summary()
	check_eq(window.count, 1, "count")
	check_near(s["frame_ms_max"], 8.0, 1e-4, "max")
	check_eq(s["dropped_frames"], 0, "dropped")


func test_dropped_threshold_is_one_and_a_half_targets() -> void:
	var target := 1000.0 / 120.0
	check_eq(_window([target * 1.49, target * 1.51, target * 3.0], target).summary()["dropped_frames"], 2, "dropped at 120 Hz")


func test_idle_loop_frames_are_never_dropped() -> void:
	var window := _window([], 1000.0 / 240.0)
	window.add(6.9, false)
	window.add(6.9, true)
	var s := window.summary()
	check_eq(s["dropped_frames"], 1, "only the rendering frame counts")
	check_near(s["frame_ms_max"], 6.9, 1e-4, "both count in max")


func test_empty_summary() -> void:
	var s := _window([]).summary()
	check_eq(s["dropped_frames"], 0, "dropped")
	check_eq(s["frame_ms_max"], 0.0, "max")


func test_fps_counts_drawn_frames() -> void:
	check_near(PerfScript.fps_for(60, 1_000_000), 60.0, 1e-6, "60 drawn in a second")
	check_near(PerfScript.fps_for(1, 1_000_000), 1.0, 1e-6, "idle: one frame drawn")
	check_near(PerfScript.fps_for(30, 500_000), 60.0, 1e-6, "half a second")
	check_eq(PerfScript.fps_for(10, 0), 0.0, "no time elapsed")


func test_target_from_refresh_rate() -> void:
	check_near(PerfScript.target_ms_for(120.0), 1000.0 / 120.0, 1e-6, "120 Hz")
	check_near(PerfScript.target_ms_for(-1.0), 1000.0 / 60.0, 1e-6, "unknown refresh rate falls back to 60 Hz")


func test_idle_after_two_quiet_seconds() -> void:
	check(PerfScript.idle_at(10_000_000, -1, 0, 0), "no input yet")
	check(not PerfScript.idle_at(10_000_000, 9_000_000, 0, 0), "input a second ago")
	check(PerfScript.idle_at(10_000_000, 8_000_000, 0, 0), "input two seconds ago")
	check(not PerfScript.idle_at(10_000_000, -1, 1, 0), "something active")
	check(not PerfScript.idle_at(10_000_000, 1_000_000, 0, 1), "a touch pressed nine seconds ago and still held")


func test_set_active_turns_idle_off_and_back() -> void:
	check(Perf.is_idle(), "idle with no input during tests")
	Perf.set_active(&"test", true)
	check(not Perf.is_idle(), "active")
	if Perf.get("_throttle"):
		check(not OS.low_processor_usage_mode, "full-rate rendering while active")
	Perf.set_active(&"test", false)
	check(Perf.is_idle(), "idle again")
	if Perf.get("_throttle"):
		check(OS.low_processor_usage_mode, "throttled again")


func test_idle_cap_keeps_a_lower_one() -> void:
	check_eq(PerfScript.IDLE_MAX_FPS, 30, "30 fps: the first input waits at most 33 ms")
	check_eq(PerfScript.idle_max_fps_for(0), 30, "no cap while active")
	check_eq(PerfScript.idle_max_fps_for(144), 30, "a higher cap")
	check_eq(PerfScript.idle_max_fps_for(20), 20, "a lower cap stays")


func test_headless_runs_are_not_capped() -> void:
	if DisplayServer.get_name() == "headless":
		check(not Perf.get("_cap_idle_fps"), "no idle cap headless")
		check_eq(Engine.max_fps, 0, "the test run's loop is uncapped")


func _engine_state() -> Array:
	return [Engine.max_fps, OS.low_processor_usage_mode]


func _restore(state: Array) -> void:
	Engine.max_fps = state[0]
	OS.low_processor_usage_mode = state[1]


# A Perf outside the tree that throttles and caps as on the phone, started idle while the game's
# own cap is active_max_fps. It changes the engine's real settings: callers restore them.
func _phone_perf(active_max_fps: int) -> PerfScript:
	Engine.max_fps = active_max_fps
	var perf := PerfScript.new()
	perf._ready()
	perf._throttle = true
	perf._cap_idle_fps = true
	perf._apply_idle()
	return perf


func test_idle_caps_the_loop_and_the_first_input_lifts_it_at_once() -> void:
	var saved := _engine_state()
	var perf := _phone_perf(0)
	check(perf.is_idle(), "idle at start")
	check_eq(Engine.max_fps, PerfScript.IDLE_MAX_FPS, "capped while idle")
	check(OS.low_processor_usage_mode, "throttled while idle")
	perf._on_window_input(InputEventScreenTouch.new())
	# Lifted inside the input's own frame, so that frame ends without waiting out the cap.
	check(not perf.is_idle(), "active after input")
	check_eq(Engine.max_fps, 0, "uncapped as soon as the input arrives")
	check(not OS.low_processor_usage_mode, "full-rate rendering after input")
	perf.free()
	_restore(saved)


func test_activity_restores_the_games_own_cap() -> void:
	var saved := _engine_state()
	for own: int in [144, 20]:
		var perf := _phone_perf(own)
		check_eq(Engine.max_fps, PerfScript.idle_max_fps_for(own), "idle cap over %d" % own)
		perf.set_active(&"test", true)
		check_eq(Engine.max_fps, own, "the game's own cap while active")
		perf.set_active(&"test", false)
		check_eq(Engine.max_fps, PerfScript.idle_max_fps_for(own), "capped again over %d" % own)
		perf.free()
	_restore(saved)


func test_the_idle_frame_the_first_input_ends_is_not_dropped() -> void:
	var saved := _engine_state()
	var perf := _phone_perf(0)
	var idle_frame_usec := int(1e6 / PerfScript.IDLE_MAX_FPS)
	perf._last_usec = Time.get_ticks_usec() - idle_frame_usec
	perf._on_window_input(InputEventScreenTouch.new())
	perf._process(0.0)
	check_eq(perf._window.summary()["dropped_frames"], 0, "the idle frame the touch ended")
	perf._last_usec = Time.get_ticks_usec() - idle_frame_usec
	perf._process(0.0)
	check_eq(perf._window.summary()["dropped_frames"], 1, "a slow active frame still counts")
	perf.free()
	_restore(saved)


func _touch(index: int, pressed: bool) -> InputEventScreenTouch:
	var touch := InputEventScreenTouch.new()
	touch.index = index
	touch.pressed = pressed
	return touch


func _button(button: MouseButton, pressed: bool) -> InputEventMouseButton:
	var event := InputEventMouseButton.new()
	event.button_index = button
	event.pressed = pressed
	return event


func _quiet_usec() -> int:
	return int(PerfScript.INPUT_QUIET_SECONDS * 1e6)


func test_a_touch_held_still_keeps_the_game_active() -> void:
	var saved := _engine_state()
	var perf := _phone_perf(0)
	perf._on_window_input(_touch(0, true))
	# A finger at rest sends nothing more, so the last event is long past the quiet time.
	perf._update_idle(Time.get_ticks_usec() + 10 * _quiet_usec())
	check(not perf.is_idle(), "active while the finger rests on the screen")
	check_eq(Engine.max_fps, 0, "uncapped while held")
	check(not OS.low_processor_usage_mode, "full-rate rendering while held")
	perf._on_window_input(_touch(0, false))
	check(not perf.is_idle(), "active straight after the release")
	perf._update_idle(Time.get_ticks_usec() + _quiet_usec())
	check(perf.is_idle(), "idle once the release is two seconds old")
	check_eq(Engine.max_fps, PerfScript.IDLE_MAX_FPS, "capped again")
	perf.free()
	_restore(saved)


func test_each_touch_and_button_is_held_until_its_own_release() -> void:
	var perf := PerfScript.new()
	perf._on_window_input(_touch(0, true))
	perf._on_window_input(_touch(1, true))
	perf._on_window_input(_touch(0, false))
	check_eq(perf.held_count(), 1, "the second finger is still down")
	perf._on_window_input(_button(MOUSE_BUTTON_LEFT, true))
	check_eq(perf.held_count(), 2, "a mouse button")
	perf._on_window_input(_button(MOUSE_BUTTON_WHEEL_DOWN, true))
	check_eq(perf.held_count(), 2, "a wheel step is not held")
	var cancelled := _touch(1, false)
	cancelled.canceled = true
	perf._on_window_input(cancelled)
	perf._on_window_input(_button(MOUSE_BUTTON_LEFT, false))
	check_eq(perf.held_count(), 0, "a cancelled touch and the button released")
	perf._on_window_input(_touch(3, false))
	check_eq(perf.held_count(), 0, "a release without its press")
	perf.free()


func _log_lines_with(text: String) -> PackedStringArray:
	OfsLog.flush()
	var found := PackedStringArray()
	var path := OfsLog.file_path()
	if path.is_empty() or not FileAccess.file_exists(path):
		return found
	for line in FileAccess.get_file_as_string(path).split("\n", false):
		if line.contains(text):
			found.append(line)
	return found


func test_losing_focus_drops_held_touches_and_logs_it() -> void:
	var saved := _engine_state()
	var marker := "still held; dropped"
	for what: int in [Node.NOTIFICATION_APPLICATION_FOCUS_OUT, Node.NOTIFICATION_APPLICATION_PAUSED]:
		var perf := _phone_perf(0)
		perf._on_window_input(_touch(0, true))
		perf._on_window_input(_button(MOUSE_BUTTON_LEFT, true))
		var before := _log_lines_with(marker).size()
		perf.notification(what)
		check_eq(perf.held_count(), 0, "nothing held after notification %d" % what)
		perf._update_idle(Time.get_ticks_usec() + _quiet_usec())
		check(perf.is_idle(), "idles although the releases never came (%d)" % what)
		var logged := _log_lines_with(marker)
		check_eq(logged.size(), before + 1, "one line for notification %d" % what)
		if logged.size() == before + 1:
			var line := logged[before]
			check(line.contains("\"event\":\"app.error\"") and line.contains("\"level\":\"warn\""), line)
			check(line.contains("touch 0, mouse 1"), "names what was held: " + line)
		perf.notification(what)
		check_eq(_log_lines_with(marker).size(), before + 1, "nothing held, nothing logged (%d)" % what)
		perf.free()
	_restore(saved)


func test_a_cap_set_while_idle_applies_on_waking() -> void:
	var saved := _engine_state()
	var perf := _phone_perf(0)
	perf.set_max_fps(60)
	check_eq(Engine.max_fps, PerfScript.IDLE_MAX_FPS, "the idle cap holds while idle")
	perf.set_max_fps(20)
	check_eq(Engine.max_fps, 20, "a lower cap applies at once, idle too")
	perf.set_max_fps(60)
	perf.set_active(&"test", true)
	check_eq(Engine.max_fps, 60, "the cap set while idle, on waking")
	perf.set_max_fps(90)
	check_eq(Engine.max_fps, 90, "applied at once while active")
	perf.set_active(&"test", false)
	check_eq(Engine.max_fps, PerfScript.IDLE_MAX_FPS, "the idle cap again")
	perf.set_max_fps(0)
	perf.set_active(&"test", true)
	check_eq(Engine.max_fps, 0, "no cap")
	perf.free()
	_restore(saved)


func test_set_max_fps_without_the_idle_cap_sets_the_engine() -> void:
	var saved := _engine_state()
	var perf := PerfScript.new()
	perf.set_max_fps(45)
	check_eq(Engine.max_fps, 45, "runs without the idle cap")
	perf.free()
	_restore(saved)


class InputSwallower extends Node:
	var swallowed := 0

	func _input(_event: InputEvent) -> void:
		swallowed += 1
		get_viewport().set_input_as_handled()


func test_input_the_scene_handles_still_reaches_perf() -> void:
	var saved := _engine_state()
	var saved_input: int = Perf.get("_last_input_usec")
	var perf := PerfScript.new()
	tree.root.add_child(perf)
	# Added after Perf, so its _input comes first and stops the event there.
	var swallower := InputSwallower.new()
	tree.root.add_child(swallower)
	Input.parse_input_event(_button(MOUSE_BUTTON_LEFT, true))
	Input.flush_buffered_events()
	check_eq(swallower.swallowed, 1, "the scene got the press and handled it")
	check(not perf.is_idle(), "Perf woke all the same")
	check_eq(perf.held_count(), 1, "and holds the button")
	Input.parse_input_event(_button(MOUSE_BUTTON_LEFT, false))
	Input.flush_buffered_events()
	check_eq(perf.held_count(), 0, "released")
	swallower.free()
	perf.free()
	_restore(saved)
	# The autoload saw both events too; put back its no-input state and let it recompute.
	Perf.set("_last_input_usec", saved_input)
	Perf.set_active(&"test", false)
	check(Perf.is_idle(), "the autoload is idle again")


func test_perf_second_has_every_schema_field() -> void:
	var schema: Variant = JSON.parse_string(FileAccess.get_file_as_string(SCHEMA_PATH))
	check(schema is Dictionary, "schema readable from %s (run scripts/sync_data.py)" % SCHEMA_PATH)
	if not schema is Dictionary:
		return
	var fields: Dictionary = schema["events"]["perf.second"]["fields"]
	var data := Perf.second_data(Time.get_ticks_usec())
	check_eq(data.keys().size(), fields.size(), "field count")
	for field: String in fields:
		check(data.has(field), "perf.second has " + field)
	check(data["frames_drawn"] is int, "frames_drawn is an integer")
	check(data["idle"] is bool, "idle is a boolean")
	check(data["footprint_mb"] is float, "footprint_mb is a number")
