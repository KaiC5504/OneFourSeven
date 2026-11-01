extends Node
## Perf autoload: perf.second and perf.hitch (spec 16.10), and the idle state that throttles
## rendering (spec 13.4).
##
## With application/run/low_processor_mode on, Godot draws only when something changed. Idle
## keeps it on; input or anything registered through set_active turns it off so motion runs
## at the display rate rather than at low-processor mode's capped loop.

const HITCH_MS := 50.0
const STARTUP_SECONDS := 3.0
const DROPPED_FACTOR := 1.5
const INPUT_QUIET_SECONDS := 2.0
# Preallocated so a frame never allocates; a second at 240 Hz fits with room to spare.
const BUFFER_SIZE := 512


## One window of main-loop frame times (ms). Average and max cover every frame, p95 (nearest
## rank) the first BUFFER_SIZE of them. Only frames added as rendering can count as dropped.
class FrameWindow:
	var count := 0
	var _target_ms: float
	var _frames := PackedFloat32Array()
	var _total_ms := 0.0
	var _max_ms := 0.0
	var _dropped := 0

	func _init(target_ms: float) -> void:
		_target_ms = target_ms
		_frames.resize(BUFFER_SIZE)

	func add(ms: float, rendering: bool = true) -> void:
		if count < _frames.size():
			_frames[count] = ms
		count += 1
		_total_ms += ms
		_max_ms = maxf(_max_ms, ms)
		if rendering and ms > _target_ms * DROPPED_FACTOR:
			_dropped += 1

	func summary() -> Dictionary:
		if count == 0:
			return {"frame_ms_avg": 0.0, "frame_ms_p95": 0.0, "frame_ms_max": 0.0, "dropped_frames": 0}
		var sorted := _frames.slice(0, mini(count, _frames.size()))
		sorted.sort()
		return {
			"frame_ms_avg": _total_ms / count,
			"frame_ms_p95": sorted[ceili(0.95 * sorted.size()) - 1],
			"frame_ms_max": _max_ms,
			"dropped_frames": _dropped,
		}

	func reset() -> void:
		count = 0
		_total_ms = 0.0
		_max_ms = 0.0
		_dropped = 0


var _window: FrameWindow
var _window_start_usec := 0
var _window_start_drawn := 0
var _last_usec := 0
var _start_usec := 0
var _last_input_usec := -1
var _active := {}
var _idle := true
var _throttle := false


static func target_ms_for(refresh_rate: float) -> float:
	return 1000.0 / (refresh_rate if refresh_rate > 1.0 else 60.0)


static func fps_for(frames_drawn: int, elapsed_usec: int) -> float:
	return frames_drawn * 1e6 / elapsed_usec if elapsed_usec > 0 else 0.0


## Idle unless there was input in the last INPUT_QUIET_SECONDS or something is active.
## last_input_usec < 0 means no input yet.
static func idle_at(now_usec: int, last_input_usec: int, active_count: int) -> bool:
	if active_count > 0:
		return false
	return last_input_usec < 0 or now_usec - last_input_usec >= INPUT_QUIET_SECONDS * 1e6


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	_throttle = OS.low_processor_usage_mode
	_window = FrameWindow.new(target_ms_for(DisplayServer.screen_get_refresh_rate()))
	_start_usec = Time.get_ticks_usec()
	_start_window(_start_usec)


func _notification(what: int) -> void:
	# The first frame after a resume would otherwise carry the whole time away as a hitch.
	if what == NOTIFICATION_APPLICATION_RESUMED or what == NOTIFICATION_APPLICATION_FOCUS_IN:
		if _window != null:
			_start_window(Time.get_ticks_usec())


func _input(_event: InputEvent) -> void:
	_last_input_usec = Time.get_ticks_usec()
	_update_idle(_last_input_usec)


# Frame times come from the clock rather than `delta`, which Godot smooths and would hide
# the very hitches this is here to catch.
func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	var ms := (now - _last_usec) / 1000.0
	_last_usec = now
	# A throttled idle loop is paced by low-processor mode's sleep, not the display, and
	# draws nothing that could be dropped.
	_window.add(ms, not (_throttle and _idle))
	if ms > HITCH_MS:
		_log_hitch(ms, now)
	if now - _window_start_usec >= 1_000_000:
		Log.info("perf", "perf.second", second_data(now))
		_start_window(now)
	_update_idle(now)


## Something other than input that needs every frame drawn (an animation, a capture).
func set_active(reason: StringName, active: bool) -> void:
	if active:
		_active[reason] = true
	else:
		_active.erase(reason)
	_update_idle(Time.get_ticks_usec())


func is_idle() -> bool:
	return _idle


func second_data(now: int) -> Dictionary:
	var drawn := Engine.get_frames_drawn() - _window_start_drawn
	var data := _window.summary()
	data["fps"] = fps_for(drawn, now - _window_start_usec)
	data["frames_drawn"] = drawn
	data["static_memory_mb"] = OS.get_static_memory_usage() / 1e6
	data["footprint_mb"] = OfsDevice.memory_footprint_mb()
	data["thermal_state"] = OfsDevice.thermal_state()
	data["idle"] = _idle
	return data


func _start_window(now: int) -> void:
	_window.reset()
	_window_start_usec = now
	_window_start_drawn = Engine.get_frames_drawn()
	_last_usec = now


func _update_idle(now: int) -> void:
	var idle := idle_at(now, _last_input_usec, _active.size())
	if idle == _idle:
		return
	_idle = idle
	if _throttle:
		OS.low_processor_usage_mode = idle


func _log_hitch(ms: float, now: int) -> void:
	var scene := get_tree().current_scene
	var activity := "idle" if _idle else "active"
	if now - _start_usec < STARTUP_SECONDS * 1_000_000:
		activity = "startup"
	Log.warn("perf", "perf.hitch", {
		"frame_ms": ms,
		"scene": scene.scene_file_path.get_file().get_basename() if scene else "",
		"activity": activity,
	})
