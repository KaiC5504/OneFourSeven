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
	check(PerfScript.idle_at(10_000_000, -1, 0), "no input yet")
	check(not PerfScript.idle_at(10_000_000, 9_000_000, 0), "input a second ago")
	check(PerfScript.idle_at(10_000_000, 8_000_000, 0), "input two seconds ago")
	check(not PerfScript.idle_at(10_000_000, -1, 1), "something active")


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
	perf._input(InputEventScreenTouch.new())
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
	perf._input(InputEventScreenTouch.new())
	perf._process(0.0)
	check_eq(perf._window.summary()["dropped_frames"], 0, "the idle frame the touch ended")
	perf._last_usec = Time.get_ticks_usec() - idle_frame_usec
	perf._process(0.0)
	check_eq(perf._window.summary()["dropped_frames"], 1, "a slow active frame still counts")
	perf.free()
	_restore(saved)


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
