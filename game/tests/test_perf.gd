extends TestCase

const PerfScript := preload("res://scripts/autoload/perf.gd")


func test_summary_of_known_frames() -> void:
	var frames := PackedFloat32Array([10.0, 10.0, 10.0, 10.0, 100.0])
	var s := PerfScript.summarize(frames, frames.size(), 1000.0 / 60.0)
	check_near(s["frame_ms_avg"], 28.0, 1e-4, "avg")
	check_near(s["frame_ms_max"], 100.0, 1e-4, "max")
	check_near(s["frame_ms_p95"], 100.0, 1e-4, "p95")
	check_near(s["fps"], 5000.0 / 140.0, 1e-3, "fps")
	check_eq(s["dropped_frames"], 1, "dropped")


func test_p95_is_nearest_rank() -> void:
	var frames := PackedFloat32Array()
	for i in range(20, 0, -1):
		frames.append(float(i))
	var s := PerfScript.summarize(frames, frames.size(), 1000.0 / 60.0)
	check_near(s["frame_ms_p95"], 19.0, 1e-4, "p95 of 1..20")


func test_only_first_count_frames_are_used() -> void:
	var frames := PackedFloat32Array([8.0, 8.0, 500.0, 500.0])
	var s := PerfScript.summarize(frames, 2, 1000.0 / 120.0)
	check_near(s["frame_ms_max"], 8.0, 1e-4, "max ignores the unused tail")
	check_eq(s["dropped_frames"], 0, "dropped")


func test_dropped_threshold_is_one_and_a_half_targets() -> void:
	var target := 1000.0 / 120.0
	var frames := PackedFloat32Array([target * 1.49, target * 1.51, target * 3.0])
	check_eq(PerfScript.summarize(frames, 3, target)["dropped_frames"], 2, "dropped at 120 Hz")


func test_empty_summary() -> void:
	var s := PerfScript.summarize(PackedFloat32Array(), 0, 16.0)
	check_eq(s["dropped_frames"], 0, "dropped")
	check_eq(s["fps"], 0.0, "fps")


func test_target_from_refresh_rate() -> void:
	check_near(PerfScript.target_ms_for(120.0), 1000.0 / 120.0, 1e-6, "120 Hz")
	check_near(PerfScript.target_ms_for(-1.0), 1000.0 / 60.0, 1e-6, "unknown refresh rate falls back to 60 Hz")
