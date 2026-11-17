class_name DebugOverlay
extends Control
## The hidden on-device overlay (spec 16.6), opened by a three-finger tap (F3 on Windows): fps,
## frame times, the last shot's simulation time, the last prediction's time, the opponent's last
## decision (spec 10.6), thermal state, parameter hash and phone link status. Release builds have
## it too: TestFlight builds are release.
##
## While shown it refreshes every REFRESH_SECONDS and only rewrites text that changed, so it never
## keeps an idle game drawing; while hidden nothing runs.

const BUILD_INFO_PATH := "res://data/build_info.json"
const REFRESH_SECONDS := 0.5
const MARGIN := Vector2(16.0, 10.0)
# Canvas units; 33 px, 11 pt, on the phone's 1320 px tall screen. Below hud.gd's size so the
# block stays compact, never below Apple's smallest legible size.
const FONT_SIZE := 22
const MIN_POINTS := 11.0
const NONE := "-"

## Canvas units at the safe area's right edge the block keeps clear of: the free table's stroke
## column, so the overlay never hides a control.
var clear_right := 0.0:
	set(value):
		if value != clear_right:
			clear_right = value
			_place()

var _label: Label
var _timer: Timer
var _sim_us := -1.0
var _predict_us := -1.0
var _think := ""
var _build := ""


## The safe area in canvas units. It is in screen pixels and covers the whole screen on desktop,
## so it is clipped to the window first (as hud.gd's safe_origin).
static func safe_rect(safe_area: Rect2, window_rect: Rect2, canvas_size: Vector2) -> Rect2:
	var inside := safe_area.intersection(window_rect)
	if not inside.has_area():
		inside = window_rect
	var scale := canvas_size / window_rect.size.max(Vector2.ONE)
	return Rect2((inside.position - window_rect.position) * scale, inside.size * scale)


## screen_scale is pixels per point, canvas_scale pixels per canvas unit.
static func font_size_for(screen_scale: float, canvas_scale: float) -> int:
	return maxi(FONT_SIZE, ceili(MIN_POINTS * maxf(screen_scale, 1.0) / maxf(canvas_scale, 0.01)))


static func build_text(info: Variant) -> String:
	if not info is Dictionary or not (info as Dictionary).has_all(["version", "build_number", "git_sha"]):
		return "unknown (run scripts/sync_data.py)"
	var build: Dictionary = info
	return "%s (%s) %s%s" % [build["version"], build["build_number"], str(build["git_sha"]).left(7),
			" dirty" if build.get("git_dirty", false) else ""]


## The opponent's last decision in one line: level, time, simulations, steps and how it stopped,
## from a stroke's "think" (OfsMatch.opponent_take: ms, sims, steps, stopped). "" when there is none.
static func think_text(level: String, think: Variant) -> String:
	if not think is Dictionary or level.is_empty():
		return ""
	var t: Dictionary = think
	return "%s %.0f ms  %d sims  %d steps  %s" % [level, float(t.get("ms", 0.0)), int(t.get("sims", 0)),
			int(t.get("steps", 0)), str(t.get("stopped", "?"))]


static func ms_text(us: float) -> String:
	return "%.2f ms" % (us / 1000.0) if us >= 0.0 else NONE


## perf is Perf.last_summary(); times are microseconds, negative when there is none yet.
static func format_text(perf: Dictionary, sim_us: float, predict_us: float, params_hash: String,
		pending_hash: String, link_status: String, build: String, think: String = "") -> String:
	var footprint: float = perf.get("footprint_mb", -1.0)
	var lines := PackedStringArray([
		"build    " + build,
		"fps      %.1f%s" % [perf.get("fps", 0.0), "  idle" if perf.get("idle", false) else ""],
		"frame    %.1f avg  %.1f p95  %.1f max ms" % [perf.get("frame_ms_avg", 0.0),
				perf.get("frame_ms_p95", 0.0), perf.get("frame_ms_max", 0.0)],
		"dropped  %d" % int(perf.get("dropped_frames", 0)),
		"shot     " + ms_text(sim_us),
		"predict  " + ms_text(predict_us),
		"think    " + (think if not think.is_empty() else NONE),
		"thermal  " + str(perf.get("thermal_state", "unknown")),
		"memory   " + ("%.0f MB" % footprint if footprint >= 0.0 else "unavailable"),
		"params   " + (params_hash if not params_hash.is_empty() else NONE),
		"pending  " + (pending_hash if not pending_hash.is_empty() else NONE),
		"link     " + link_status,
	])
	return "\n".join(lines)


func _ready() -> void:
	visible = false
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_label = Label.new()
	_label.name = "Text"
	_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var font := SystemFont.new()
	# Fixed-width digits, so the right-aligned block doesn't jitter as the numbers change.
	font.font_names = PackedStringArray(["Menlo", "Consolas", "monospace"])
	_label.add_theme_font_override("font", font)
	_label.add_theme_color_override("font_color", Color(0.92, 0.94, 0.92))
	var panel := StyleBoxFlat.new()
	panel.bg_color = Color(0.0, 0.0, 0.0, 0.62)
	panel.set_corner_radius_all(6)
	panel.set_content_margin_all(10.0)
	_label.add_theme_stylebox_override("normal", panel)
	add_child(_label)
	_timer = Timer.new()
	_timer.wait_time = REFRESH_SECONDS
	_timer.timeout.connect(_refresh)
	add_child(_timer)
	var info := FileAccess.get_file_as_string(BUILD_INFO_PATH)
	_build = build_text(JSON.parse_string(info) if not info.is_empty() else null)
	get_viewport().size_changed.connect(_place)


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("overlay_toggle", false, true):
		toggle("key")
		get_viewport().set_input_as_handled()


## via: three_finger_tap, key or harness (overlay.toggled).
func toggle(via: String) -> void:
	set_shown(not visible, via)


func set_shown(shown: bool, via: String) -> void:
	if shown == visible:
		return
	visible = shown
	Log.info("ui", "overlay.toggled", {"visible": shown, "via": via})
	if shown:
		_refresh()
		_timer.start()
	else:
		_timer.stop()


## The opponent's last decision, as think_text gives it.
func set_think(level: String, think: Variant) -> void:
	var text := think_text(level, think)
	if not text.is_empty():
		_think = text


## Microseconds; a negative time leaves that one as it was.
func set_shot_times(sim_us: float, predict_us: float) -> void:
	if sim_us >= 0.0:
		_sim_us = sim_us
	if predict_us >= 0.0:
		_predict_us = predict_us


func shown_text() -> String:
	return _label.text if _label else ""


func _refresh() -> void:
	var content := format_text(Perf.last_summary(), _sim_us, _predict_us, Params.current_hash(),
			Params.pending_hash(), Link.get_status(), _build, _think)
	if _label.text != content:
		_label.text = content
	_place()


func _place() -> void:
	if not is_inside_tree() or not visible:
		return
	var window := get_window()
	place_in(Rect2(DisplayServer.get_display_safe_area()), Rect2(Vector2(window.position), Vector2(window.size)),
			get_viewport_rect().size, DisplayServer.screen_get_scale())


## Top-right inside the safe area, left of clear_right, for a safe area and window in screen pixels
## and the canvas size in canvas units. Writes only what changed.
func place_in(safe_area: Rect2, window_rect: Rect2, canvas_size: Vector2, screen_scale: float) -> void:
	var font_size := font_size_for(screen_scale, window_rect.size.y / maxf(canvas_size.y, 1.0))
	if _label.get_theme_font_size("font_size") != font_size:
		_label.add_theme_font_size_override("font_size", font_size)
	var block := _label.get_combined_minimum_size()
	if _label.size != block:
		_label.size = block
	var safe := safe_rect(safe_area, window_rect, canvas_size)
	var origin := Vector2(safe.end.x - clear_right - MARGIN.x - block.x, safe.position.y + MARGIN.y)
	if _label.position != origin:
		_label.position = origin
