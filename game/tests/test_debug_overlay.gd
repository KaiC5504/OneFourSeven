extends TestCase
## The debug overlay (spec 16.6): hidden until toggled, the numbers it shows, where it sits.

const OverlayScript := preload("res://scripts/ui/debug_overlay.gd")
const PERF := {"fps": 119.6, "frame_ms_avg": 8.36, "frame_ms_p95": 9.12, "frame_ms_max": 14.04,
		"dropped_frames": 3, "thermal_state": "fair", "footprint_mb": 412.4, "idle": false}

var _log_start := 0


func _overlay() -> DebugOverlay:
	var overlay: DebugOverlay = OverlayScript.new()
	tree.root.add_child(overlay)
	return overlay


func test_text_shows_every_number() -> void:
	var text := OverlayScript.format_text(PERF, 412.0, 1530.0, "0123456789abcdef", "fedcba9876543210",
			"connected", "0.1.0 (12) abcdef0")
	for want: String in ["build    0.1.0 (12) abcdef0", "fps      119.6", "8.4 avg  9.1 p95  14.0 max ms",
			"dropped  3", "shot     0.41 ms", "predict  1.53 ms", "thermal  fair", "memory   412 MB",
			"params   0123456789abcdef", "pending  fedcba9876543210", "link     connected"]:
		check(text.contains(want), "shows %s in:\n%s" % [want, text])
	check(not text.contains("idle"), "not idle")


func test_text_before_anything_is_known() -> void:
	var perf := PERF.duplicate()
	perf["footprint_mb"] = -1.0
	perf["idle"] = true
	var text := OverlayScript.format_text(perf, -1.0, -1.0, "abc", "", "off", "dev")
	check(text.contains("shot     -"), "no shot yet")
	check(text.contains("predict  -"), "no prediction yet")
	check(text.contains("pending  -"), "nothing pending")
	check(text.contains("memory   unavailable"), "footprint query failed")
	check(text.contains("  idle"), "idle shown")


func test_build_text() -> void:
	var info := {"version": "0.1.0", "build_number": "7", "git_sha": "32f4be738834", "git_dirty": true}
	check_eq(OverlayScript.build_text(info), "0.1.0 (7) 32f4be7 dirty", "build")
	info["git_dirty"] = false
	check_eq(OverlayScript.build_text(info), "0.1.0 (7) 32f4be7", "clean")
	check(OverlayScript.build_text(null).begins_with("unknown"), "no build_info.json")
	check(OverlayScript.build_text({"version": "1"}).begins_with("unknown"), "incomplete")


func test_hidden_until_toggled_and_logged() -> void:
	var overlay := _overlay()
	check(not overlay.visible, "hidden at start")
	check(overlay.get("_timer").is_stopped(), "no refresh while hidden")
	_mark_log()
	overlay.toggle("three_finger_tap")
	check(overlay.visible, "shown")
	check(not overlay.get("_timer").is_stopped(), "refreshing while shown")
	check(overlay.shown_text().contains("params   " + Params.current_hash()), "filled at once:\n" + overlay.shown_text())
	overlay.toggle("three_finger_tap")
	check(not overlay.visible, "hidden again")
	check(overlay.get("_timer").is_stopped(), "refresh stopped")
	overlay.set_shown(false, "harness")
	check_eq(_logged("overlay.toggled"), [{"visible": true, "via": "three_finger_tap"},
			{"visible": false, "via": "three_finger_tap"}] as Array[Dictionary], "one line per change, none for no change")
	overlay.free()


func test_f3_toggles_it() -> void:
	var overlay := _overlay()
	_mark_log()
	var f3 := InputEventKey.new()
	f3.physical_keycode = KEY_F3
	f3.pressed = true
	overlay._unhandled_input(f3)
	check(overlay.visible, "F3 shows it")
	var release := InputEventKey.new()
	release.physical_keycode = KEY_F3
	overlay._unhandled_input(release)
	check(overlay.visible, "the release does nothing")
	var echo := f3.duplicate() as InputEventKey
	echo.echo = true
	overlay._unhandled_input(echo)
	check(overlay.visible, "a held key's repeats do nothing")
	check_eq(_logged("overlay.toggled"), [{"visible": true, "via": "key"}] as Array[Dictionary], "logged via key")
	overlay.free()


func test_shot_times_show_and_negative_keeps_the_last() -> void:
	var overlay := _overlay()
	overlay.set_shown(true, "harness")
	overlay.set_shot_times(250.0, 90.0)
	overlay.call("_refresh")
	check(overlay.shown_text().contains("shot     0.25 ms"), "sim time")
	check(overlay.shown_text().contains("predict  0.09 ms"), "prediction time")
	overlay.set_shot_times(-1.0, 120.0)
	overlay.call("_refresh")
	check(overlay.shown_text().contains("shot     0.25 ms"), "kept")
	check(overlay.shown_text().contains("predict  0.12 ms"), "updated")
	overlay.free()


func test_never_takes_input() -> void:
	var overlay := _overlay()
	check_eq(overlay.mouse_filter, Control.MOUSE_FILTER_IGNORE, "overlay")
	check_eq((overlay.get("_label") as Control).mouse_filter, Control.MOUSE_FILTER_IGNORE, "its text")
	overlay.free()


func test_sits_top_right_inside_the_safe_area() -> void:
	var overlay := _overlay()
	overlay.set_shown(true, "harness")
	# iPhone 17 Pro Max in landscape: 2868 x 1320 px, the island's inset on the left and the
	# rounded corners' on the right; the canvas is 1912 x 880.
	var window := Rect2(0, 0, 2868, 1320)
	var safe := Rect2(186, 0, 2496, 1257)
	overlay.place_in(safe, window, Vector2(1912, 880), 3.0)
	var label := overlay.get("_label") as Label
	var canvas_safe := OverlayScript.safe_rect(safe, window, Vector2(1912, 880))
	check_near(canvas_safe.end.x, (186.0 + 2496.0) / 1.5, 1e-3, "safe right edge in canvas units")
	check_near(label.position.x + label.size.x, canvas_safe.end.x - OverlayScript.MARGIN.x, 1e-3, "right-aligned to the safe area")
	check_near(label.position.y, OverlayScript.MARGIN.y, 1e-3, "at the top")
	check(label.size.x > 100.0 and label.size.y > 100.0, "sized to its text: %s" % label.size)
	var points := label.get_theme_font_size("font_size") * 1.5 / 3.0
	check(points >= OverlayScript.MIN_POINTS, "legible on the phone: %.1f pt" % points)
	check(label.position.y + label.size.y < 880.0, "fits the screen height")
	overlay.free()


func test_safe_rect_clips_to_the_window() -> void:
	var rect := OverlayScript.safe_rect(Rect2(0, 0, 3840, 2160), Rect2(100, 50, 1912, 880), Vector2(1912, 880))
	check_eq(rect, Rect2(0, 0, 1912, 880), "a desktop window: the whole canvas")


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


func _logged(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append((parsed as Dictionary).get("data", {}))
	return found
