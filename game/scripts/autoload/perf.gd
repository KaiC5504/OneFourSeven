extends Node
## Perf autoload: perf.second and perf.hitch (spec 16.10). Agent E owns this file.

const HITCH_MS := 50.0
const STARTUP_SECONDS := 3.0
const DROPPED_FACTOR := 1.5
# Preallocated so a frame never allocates; a second at 240 Hz fits with room to spare.
const BUFFER_SIZE := 512

var _frames := PackedFloat32Array()
var _count := 0
var _window_start_usec := 0
var _last_usec := 0
var _start_usec := 0
var _target_ms := 1000.0 / 60.0


## Summary of the first `count` frame times (ms). p95 is nearest-rank. Dropped frames are
## those longer than DROPPED_FACTOR times the target interval.
static func summarize(frame_ms: PackedFloat32Array, count: int, target_ms: float) -> Dictionary:
	count = mini(count, frame_ms.size())
	if count <= 0:
		return {"fps": 0.0, "frame_ms_avg": 0.0, "frame_ms_p95": 0.0, "frame_ms_max": 0.0, "dropped_frames": 0}
	var total := 0.0
	var longest := 0.0
	var dropped := 0
	for i in count:
		var ms := frame_ms[i]
		total += ms
		longest = maxf(longest, ms)
		if ms > target_ms * DROPPED_FACTOR:
			dropped += 1
	var sorted := frame_ms.slice(0, count)
	sorted.sort()
	var p95 := sorted[ceili(0.95 * count) - 1]
	return {
		"fps": count * 1000.0 / total if total > 0.0 else 0.0,
		"frame_ms_avg": total / count,
		"frame_ms_p95": p95,
		"frame_ms_max": longest,
		"dropped_frames": dropped,
	}


static func target_ms_for(refresh_rate: float) -> float:
	return 1000.0 / (refresh_rate if refresh_rate > 1.0 else 60.0)


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	_frames.resize(BUFFER_SIZE)
	_target_ms = target_ms_for(DisplayServer.screen_get_refresh_rate())
	_start_usec = Time.get_ticks_usec()
	_window_start_usec = _start_usec
	_last_usec = _start_usec


# Frame times come from the clock rather than `delta`, which Godot smooths and would hide
# the very hitches this is here to catch.
func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	var ms := (now - _last_usec) / 1000.0
	_last_usec = now
	if _count < BUFFER_SIZE:
		_frames[_count] = ms
		_count += 1
	if ms > HITCH_MS:
		_log_hitch(ms, now)
	if now - _window_start_usec >= 1_000_000:
		_log_second()
		_window_start_usec = now
		_count = 0


func _log_hitch(ms: float, now: int) -> void:
	var scene := get_tree().current_scene
	Log.warn("perf", "perf.hitch", {
		"frame_ms": ms,
		"scene": scene.scene_file_path.get_file().get_basename() if scene else "",
		"activity": "startup" if (now - _start_usec) < STARTUP_SECONDS * 1_000_000 else "idle",
	})


func _log_second() -> void:
	var data := summarize(_frames, _count, _target_ms)
	data["static_memory_mb"] = OS.get_static_memory_usage() / 1e6
	data["thermal_state"] = OfsDevice.thermal_state()
	data["idle"] = true
	Log.info("perf", "perf.second", data)
