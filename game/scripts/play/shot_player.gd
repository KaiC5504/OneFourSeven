class_name ShotPlayer
extends Node
## Plays a shot back from its record (spec 5): advances shot time by real time times `speed`,
## emits every frame's state and each event as play reaches it, and keeps the game rendering at
## the display rate while it plays (Perf.set_active). Logs shot.playback when it ends.

signal frame(state: PackedFloat64Array, t: float, dt: float)
signal event_reached(event: Dictionary, state: PackedFloat64Array)
signal finished(shot_id: String)

const PerfScript := preload("res://scripts/autoload/perf.gd")
const PERF_REASON := &"shot"

var speed := 1.0

var _playback: ShotPlayback
var _shot_id := ""
var _events: Array[Dictionary] = []
var _next_event := 0
var _t := 0.0
var _duration := 0.0
var _playing := false
var _skipping := false
var _frames := 0
var _dropped := 0
var _frame_ms_max := 0.0
var _wall_s := 0.0
var _frame_budget_ms := 0.0
var _first_advance := false


func _process(delta: float) -> void:
	if _playing:
		advance(delta)


## Starts at t = 0: emits the first frame and any events at t = 0 now, and finishes at once when
## the shot has no duration. A shot still playing is skipped to its end first.
func play(playback: ShotPlayback, shot_id: String) -> void:
	if _playing:
		skip_to_end()
	_playback = playback
	_shot_id = shot_id
	_events = playback.events()
	_next_event = 0
	_t = 0.0
	_duration = maxf(playback.duration(), 0.0)
	_playing = true
	_skipping = false
	_frames = 0
	_dropped = 0
	_frame_ms_max = 0.0
	_wall_s = 0.0
	_frame_budget_ms = PerfScript.target_ms_for(DisplayServer.screen_get_refresh_rate()) * PerfScript.DROPPED_FACTOR
	_first_advance = true
	Perf.set_active(PERF_REASON, true)
	_step(0.0)


## One frame of play: dt_real seconds of real time, scaled by `speed`.
func advance(dt_real: float) -> void:
	if not _playing:
		return
	_wall_s += dt_real
	var ms := dt_real * 1000.0
	_frame_ms_max = maxf(_frame_ms_max, ms)
	# The first frame waited out whatever the game was doing before the shot (often the idle
	# loop's 30 fps), so it can't be a frame the playback dropped.
	if not _first_advance and ms > _frame_budget_ms:
		_dropped += 1
	_first_advance = false
	_step(minf(_t + dt_real * maxf(speed, 0.0), _duration))


func is_playing() -> bool:
	return _playing


## True while skip_to_end is emitting the events play hadn't reached, so listeners such as
## sound and haptics can let them pass silently.
func is_skipping() -> bool:
	return _skipping


## Shot time now, seconds.
func time() -> float:
	return _t


## Jumps to the end, emitting the final frame and every event not yet reached, then finished.
func skip_to_end() -> void:
	if not _playing:
		return
	_skipping = true
	_step(_duration)


# Listeners may call play or skip_to_end from inside these signals, so after each emit the loop
# checks that this playback is still the one playing.
func _step(t: float) -> void:
	var playback := _playback
	var dt := t - _t
	_t = t
	_frames += 1
	frame.emit(playback.state_at(t), t, dt)
	while _playing and _playback == playback and _next_event < _events.size():
		var e := _events[_next_event]
		# Every event comes out by the end, even one recorded past the duration.
		if float(e.get("t", 0.0)) > t and t < _duration:
			break
		_next_event += 1
		event_reached.emit(e, playback.state_at(float(e.get("t", 0.0))))
	if _playing and _playback == playback and t >= _duration:
		_finish()


func _finish() -> void:
	_playing = false
	var skipped := _skipping
	_skipping = false
	Perf.set_active(PERF_REASON, false)
	var data := {
		"shot_id": _shot_id,
		"t_end": _t,
		"wall_s": _wall_s,
		"frames": _frames,
		"dropped_frames": _dropped,
		"frame_ms_max": _frame_ms_max,
		"events": _next_event,
		"speed": speed,
		"skipped": skipped,
	}
	if _dropped > 0:
		Log.warn("shot", "shot.playback", data)
	else:
		Log.info("shot", "shot.playback", data)
	finished.emit(_shot_id)
