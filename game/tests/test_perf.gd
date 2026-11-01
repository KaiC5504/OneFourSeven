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
