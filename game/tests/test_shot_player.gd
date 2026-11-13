extends TestCase

const SCHEMA_PATH := "res://data/log_schema.json"


## Ball 0 rolls along +x at 1 m/s from the origin until `duration`; ball 1 rests at (0.5, 0.2).
class FakePlayback extends ShotPlayback:
	var _duration: float
	var _events: Array[Dictionary]

	func _init(length: float, scripted: Array[Dictionary]) -> void:
		_duration = length
		_events = scripted

	func duration() -> float:
		return _duration

	func ball_count() -> int:
		return 2

	func state_at(t: float) -> PackedFloat64Array:
		var s := clampf(t, 0.0, _duration)
		var moving := s < _duration
		var state := PackedFloat64Array()
		state.resize(2 * STRIDE)
		state[X] = s
		state[VX] = 1.0 if moving else 0.0
		state[MOTION] = ROLLING if moving else STATIONARY
		state[POCKET] = -1.0
		state[STRIDE + X] = 0.5
		state[STRIDE + Y] = 0.2
		state[STRIDE + POCKET] = -1.0
		return state

	func events() -> Array[Dictionary]:
		return _events


## Everything the player emitted, in order.
class Recorder:
	var order: Array[String] = []
	var frames: Array[float] = []
	var events: Array[Dictionary] = []
	var event_x: Array[float] = []
	var finished: Array[String] = []
	var skipping_during_events: Array[bool] = []
	var player: ShotPlayer

	func _init(p: ShotPlayer) -> void:
		player = p
		p.frame.connect(_on_frame)
		p.event_reached.connect(_on_event)
		p.finished.connect(_on_finished)

	func _on_frame(_state: PackedFloat64Array, t: float, _dt: float) -> void:
		frames.append(t)
		order.append("frame")

	func _on_event(e: Dictionary, state: PackedFloat64Array) -> void:
		events.append(e)
		event_x.append(state[ShotPlayback.X])
		skipping_during_events.append(player.is_skipping())
		order.append("event")

	func _on_finished(id: String) -> void:
		finished.append(id)
		order.append("finished")


func _event(t: float, kind: String) -> Dictionary:
	return {"t": t, "kind": kind, "a": 0, "b": 1, "body": -1, "from": "rolling", "to": "rolling", "speed": 1.0}


func _script() -> Array[Dictionary]:
	return [_event(0.0, "strike"), _event(0.25, "ball_ball"), _event(0.5, "ball_line"),
			_event(0.5, "ball_ball"), _event(0.75, "transition"), _event(1.0, "pocket")]


func _player() -> ShotPlayer:
	var player := ShotPlayer.new()
	# Not in the tree: tests drive advance() themselves, with no _process in between.
	return player


func _run(player: ShotPlayer, dt: float, limit: int = 10000) -> int:
	var steps := 0
	while player.is_playing() and steps < limit:
		player.advance(dt)
		steps += 1
	return steps


func _shot_id() -> String:
	return "test-shot-%d-%d" % [Time.get_ticks_usec(), randi()]


func _logged(shot_id: String) -> Dictionary:
	OfsLog.flush()
	var path := OfsLog.file_path()
	if path.is_empty() or not FileAccess.file_exists(path):
		return {}
	for line in FileAccess.get_file_as_string(path).split("\n", false):
		if line.contains("\"shot.playback\"") and line.contains(shot_id):
			return JSON.parse_string(line)
	return {}


func test_each_event_once_in_order() -> void:
	var player := _player()
	var rec := Recorder.new(player)
	var scripted := _script()
	player.play(FakePlayback.new(1.0, scripted), "a")
	_run(player, 0.07)
	check_eq(rec.events.size(), scripted.size(), "every event emitted once")
	for i in mini(rec.events.size(), scripted.size()):
		check(rec.events[i] == scripted[i], "event %d in order" % i)
		check_near(rec.event_x[i], float(scripted[i]["t"]), 1e-9, "event %d gets the state at its own time" % i)
	check(not rec.skipping_during_events.has(true), "not skipping during normal play")
	player.free()


func test_events_reached_by_the_frame_that_passes_them() -> void:
	var player := _player()
	var rec := Recorder.new(player)
	player.play(FakePlayback.new(1.0, _script()), "a")
	check_eq(rec.events.size(), 1, "the strike at t = 0 comes out with play()")
	player.advance(0.3)
	check_eq(rec.events.size(), 2, "the click at 0.25 by t = 0.3")
	player.advance(0.2)
	check_eq(rec.events.size(), 4, "both events at exactly 0.5")
	player.free()


func test_finished_once_after_the_final_frame() -> void:
	var player := _player()
	var rec := Recorder.new(player)
	player.play(FakePlayback.new(1.0, _script()), "shot-1")
	_run(player, 0.1)
	player.advance(0.1)
	player.skip_to_end()
	check_eq(rec.finished, ["shot-1"] as Array[String], "finished once with the shot id")
	check_near(rec.frames[-1], 1.0, 1e-12, "final frame at the duration")
	check_eq(rec.order[-1], "finished", "finished comes last")
	check_eq(rec.order.rfind("frame") < rec.order.find("finished"), true, "final frame before finished")
	check(not player.is_playing(), "stopped")
	for i in range(1, rec.frames.size()):
		check(rec.frames[i] >= rec.frames[i - 1], "frame times never go back")
	player.free()


func test_half_speed_takes_twice_the_advances() -> void:
	var player := _player()
	player.play(FakePlayback.new(1.0, _script()), "a")
	var normal := _run(player, 1.0 / 64.0)
	player.speed = 0.5
	player.play(FakePlayback.new(1.0, _script()), "b")
	var slow := _run(player, 1.0 / 64.0)
	check_eq(normal, 64, "a 1 s shot in 64 steps of 1/64 s")
	check_eq(slow, 128, "twice the steps at speed 0.5")
	player.free()


func test_frame_dt_is_shot_time() -> void:
	var player := _player()
	var dts: Array[float] = []
	player.frame.connect(func(_s: PackedFloat64Array, _t: float, dt: float) -> void: dts.append(dt))
	player.speed = 0.5
	player.play(FakePlayback.new(0.3, _script()), "a")
	player.advance(0.2)
	player.advance(0.2)
	player.advance(0.2)
	player.advance(0.2)
	check_near(dts[1], 0.1, 1e-12, "dt is real time times speed")
	check_near(dts[-1], 0.1, 1e-12, "the last frame only advances to the duration")
	check(not player.is_playing(), "finished at 0.3")
	player.free()


func test_skip_to_end() -> void:
	var player := _player()
	var rec := Recorder.new(player)
	var id := _shot_id()
	player.play(FakePlayback.new(1.0, _script()), id)
	player.advance(0.3)
	player.skip_to_end()
	check_eq(rec.events.size(), _script().size(), "every event still emitted once")
	check_eq(rec.skipping_during_events, [false, false, true, true, true, true] as Array[bool], "is_skipping during the skip")
	check_eq(rec.finished.size(), 1, "finished once")
	check_near(rec.frames[-1], 1.0, 1e-12, "final frame at the duration")
	check(not player.is_skipping(), "skipping over")
	player.skip_to_end()
	check_eq(rec.finished.size(), 1, "skipping a finished shot does nothing")
	var logged := _logged(id)
	check_eq(logged.get("data", {}).get("skipped"), true, "logged as skipped")
	player.free()


func test_zero_duration_finishes_at_once() -> void:
	var player := _player()
	var rec := Recorder.new(player)
	var events: Array[Dictionary] = [_event(0.0, "strike")]
	player.play(FakePlayback.new(0.0, events), "zero")
	check(not player.is_playing(), "not playing")
	check_eq(rec.finished, ["zero"] as Array[String], "finished inside play()")
	check_eq(rec.events.size(), 1, "its event emitted")
	check_eq(rec.frames.size(), 1, "one frame")
	player.free()


func test_play_during_play_finishes_the_first() -> void:
	var player := _player()
	var rec := Recorder.new(player)
	player.play(FakePlayback.new(1.0, _script()), "first")
	player.advance(0.1)
	player.play(FakePlayback.new(1.0, _script()), "second")
	_run(player, 0.1)
	check_eq(rec.finished, ["first", "second"] as Array[String], "both finish, in order")
	check_eq(rec.events.size(), 2 * _script().size(), "every event of both")
	player.free()


func test_perf_active_while_playing() -> void:
	var player := _player()
	player.play(FakePlayback.new(1.0, _script()), "a")
	check(not Perf.is_idle(), "the game renders at the display rate while a shot plays")
	_run(player, 0.1)
	# Input or another active reason could still hold Perf, so only the shot's own hold is checked.
	player.play(FakePlayback.new(1.0, _script()), "b")
	player.skip_to_end()
	player.free()


func test_shot_playback_logged_with_every_field() -> void:
	var player := _player()
	var id := _shot_id()
	player.play(FakePlayback.new(1.0, _script()), id)
	player.advance(0.25)
	# Steps exact in binary, short enough not to count as dropped at up to 240 Hz.
	for i in 192:
		player.advance(1.0 / 256.0)
	check(not player.is_playing(), "finished")
	var line := _logged(id)
	check(not line.is_empty(), "shot.playback in the session log")
	check_eq(line.get("level"), "info", "info when nothing dropped")
	var data: Dictionary = line.get("data", {})
	check_eq(data.get("shot_id"), id, "shot_id")
	check_near(float(data.get("t_end", -1.0)), 1.0, 1e-12, "t_end")
	check_near(float(data.get("wall_s", -1.0)), 1.0, 1e-12, "wall_s sums the real deltas")
	check_eq(int(data.get("frames", -1)), 194, "frames: play()'s and one per advance")
	check_eq(int(data.get("dropped_frames", -1)), 0, "the first frame's long wait isn't a drop")
	check_near(float(data.get("frame_ms_max", -1.0)), 250.0, 1e-6, "frame_ms_max")
	check_eq(int(data.get("events", -1)), _script().size(), "events")
	check_near(float(data.get("speed", -1.0)), 1.0, 1e-9, "speed")
	check_eq(data.get("skipped"), false, "skipped")
	var schema: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(SCHEMA_PATH))
	var fields: Dictionary = schema["events"]["shot.playback"]["fields"]
	for field: String in fields:
		if bool(fields[field]["required"]):
			check(data.has(field), "required field %s" % field)
	for field: String in data:
		check(fields.has(field), "%s is in the schema" % field)
	player.free()


func test_dropped_frames_log_at_warn() -> void:
	var player := _player()
	var id := _shot_id()
	player.play(FakePlayback.new(1.0, _script()), id)
	player.advance(0.001)
	# Far past 1.5 refresh intervals at any refresh rate a display has.
	player.advance(0.25)
	player.advance(0.25)
	player.advance(0.001)
	_run(player, 0.001)
	var line := _logged(id)
	check_eq(line.get("level"), "warn", "warn when frames dropped")
	check_eq(int(line.get("data", {}).get("dropped_frames", -1)), 2, "two dropped frames")
	player.free()
