extends TestCase
## OpponentShow's clock driven by hand: what backswing_m() says at each moment, and when the
## stroke is handed back.

const DT := 1.0 / 240.0
const MAX_SPEED := 6.5
const MAX_FRAMES := 5000


class Recorder extends RefCounted:
	var shots: Array[Dictionary] = []

	func take(shot: Dictionary) -> void:
		shots.append(shot)


func _shot(speed: Variant) -> Dictionary:
	return {"shot_id": "s-1", "strike": {"speed_m_s": speed, "tip": [0.0, 0.0]}}


func _show() -> Array:
	var show := OpponentShow.new()
	var recorder := Recorder.new()
	show.strike_due.connect(recorder.take)
	tree.root.add_child(show)
	return [show, recorder]


## The first frame after begin, which counts no time.
func _start(show: OpponentShow, shot: Dictionary, pace: float = 1.0) -> void:
	show.begin(shot, pace, MAX_SPEED)
	show._process(0.5)


## Steps until the stroke is due; the seconds counted, or -1 if it never came.
func _run_out(show: OpponentShow, recorder: Recorder) -> float:
	var before := recorder.shots.size()
	var t := 0.0
	for i in MAX_FRAMES:
		show._process(DT)
		t += DT
		if recorder.shots.size() > before:
			return t
	return -1.0


## The backswing once the show is `seconds` in (the first frame already taken).
func _step_to(show: OpponentShow, from_s: float, seconds: float) -> float:
	var t := from_s
	while t + DT <= seconds + 1e-9:
		show._process(DT)
		t += DT
	return t


func _hold_backswing(shot: Dictionary) -> float:
	var parts := _show()
	var show: OpponentShow = parts[0]
	_start(show, shot)
	var into_hold := OpponentShow.STAND_S + OpponentShow.FEATHERS * OpponentShow.FEATHER_S \
			+ OpponentShow.BACKSWING_S + OpponentShow.HOLD_S * 0.5
	_step_to(show, 0.0, into_hold)
	var back := show.backswing_m()
	show.free()
	return back


func test_the_timeline() -> void:
	var parts := _show()
	var show: OpponentShow = parts[0]
	var recorder: Recorder = parts[1]
	var shot := _shot(3.25)
	var last_back := lerpf(OpponentShow.BACKSWING_MIN_M, OpponentShow.BACKSWING_MAX_M, 0.5)
	var total := show.total_seconds(shot, 1.0)
	check_near(total, OpponentShow.STAND_S + OpponentShow.FEATHERS * OpponentShow.FEATHER_S
			+ OpponentShow.BACKSWING_S + OpponentShow.HOLD_S, 1e-9, "the four parts")
	check(total >= 0.6, "at least the spec's natural pause")
	_start(show, shot)
	check(show.is_showing(), "showing")
	var feather_start := OpponentShow.STAND_S
	var back_start := OpponentShow.STAND_S + OpponentShow.FEATHERS * OpponentShow.FEATHER_S
	var back_end := back_start + OpponentShow.BACKSWING_S
	var stand_max := 0.0
	var peaks: Array[float] = []
	var lows: Array[float] = []
	for i in OpponentShow.FEATHERS:
		peaks.append(0.0)
		lows.append(1.0)
	var at_back_end := -1.0
	var hold_min := 1.0
	var hold_max := 0.0
	var last := 0.0
	var rising := true
	var t := 0.0
	var struck_at := -1.0
	while t < total + 1.0 and recorder.shots.is_empty():
		var b := show.backswing_m()
		if t < feather_start:
			stand_max = maxf(stand_max, b)
		elif t < back_start:
			var k := mini(int((t - feather_start) / OpponentShow.FEATHER_S), OpponentShow.FEATHERS - 1)
			peaks[k] = maxf(peaks[k], b)
			# Each feather's own start, where it is back at the address.
			if absf(t - (feather_start + k * OpponentShow.FEATHER_S)) <= DT:
				lows[k] = minf(lows[k], b)
		elif t < back_end:
			rising = rising and b >= last - 1e-12
			if back_end - t <= DT * 1.5:
				at_back_end = b
		else:
			hold_min = minf(hold_min, b)
			hold_max = maxf(hold_max, b)
		last = b
		show._process(DT)
		t += DT
		if not recorder.shots.is_empty():
			struck_at = t
	check_eq(stand_max, 0.0, "still while it stands")
	for k in OpponentShow.FEATHERS:
		check_near(peaks[k], OpponentShow.FEATHER_PULL_M, OpponentShow.FEATHER_PULL_M * 0.02, "feather %d's peak" % k)
		check(lows[k] < 0.001, "back at the address before feather %d: %f" % [k, lows[k]])
	check(rising, "the last backswing only draws back")
	check_near(at_back_end, last_back, last_back * 0.01, "the last backswing ends at the length for the speed")
	check_near(hold_min, last_back, 1e-9, "held at the back (lowest)")
	check_near(hold_max, last_back, 1e-9, "held at the back (highest)")
	check_eq(recorder.shots.size(), 1, "strike_due once")
	if recorder.shots.size() == 1:
		check_eq(recorder.shots[0], shot, "with the stroke given")
	check(struck_at >= total - 1e-9 and struck_at < total + DT + 1e-9, "at total_seconds: %f against %f" % [struck_at, total])
	check(not show.is_showing(), "over")
	check_eq(show.backswing_m(), 0.0, "the cue view's stroke takes over")
	for i in 100:
		show._process(DT)
	check_eq(recorder.shots.size(), 1, "never again")
	show.free()


func test_a_faster_stroke_draws_back_further() -> void:
	var slow := _hold_backswing(_shot(1.0))
	var medium := _hold_backswing(_shot(3.0))
	var fast := _hold_backswing(_shot(5.5))
	check(slow < medium and medium < fast, "%f < %f < %f" % [slow, medium, fast])
	check_near(_hold_backswing(_shot(MAX_SPEED)), OpponentShow.BACKSWING_MAX_M, 1e-9, "the highest speed")
	check_near(_hold_backswing(_shot(20.0)), OpponentShow.BACKSWING_MAX_M, 1e-9, "beyond it")
	check_near(_hold_backswing(_shot(0.0)), OpponentShow.BACKSWING_MIN_M, 1e-9, "the lowest speed")
	check_near(_hold_backswing(_shot(4)), lerpf(OpponentShow.BACKSWING_MIN_M, OpponentShow.BACKSWING_MAX_M,
			4.0 / MAX_SPEED), 1e-9, "a whole number")


func test_pace_scales_the_length() -> void:
	var parts := _show()
	var show: OpponentShow = parts[0]
	var recorder: Recorder = parts[1]
	var shot := _shot(3.0)
	var total := show.total_seconds(shot, 1.0)
	check_near(show.total_seconds(shot, 2.0), total * 2.0, 1e-9, "twice as long")
	check_near(show.total_seconds(shot, 0.5), total * 0.5, 1e-9, "half as long")
	check_eq(show.total_seconds(shot, 0.0), 0.0, "pace 0")
	_start(show, shot, 2.0)
	var t := _step_to(show, 0.0, OpponentShow.STAND_S * 1.5)
	check_eq(show.backswing_m(), 0.0, "the stand is longer too")
	var left := _run_out(show, recorder)
	check(left >= 0.0, "struck")
	check_near(t + left, total * 2.0, DT + 1e-9, "struck at the scaled length")
	show.free()


func test_pace_zero_strikes_on_the_next_frame() -> void:
	var parts := _show()
	var show: OpponentShow = parts[0]
	var recorder: Recorder = parts[1]
	var shot := _shot(3.0)
	show.begin(shot, 0.0, MAX_SPEED)
	check(recorder.shots.is_empty(), "not within begin")
	check(show.is_processing(), "waiting for the frame")
	show._process(DT)
	check_eq(recorder.shots.size(), 1, "on the next frame")
	if recorder.shots.size() == 1:
		check_eq(recorder.shots[0], shot, "with the stroke")
	check(not show.is_showing(), "over")
	check(not show.is_processing(), "asleep")
	show.free()


func test_cancel_emits_nothing() -> void:
	var parts := _show()
	var show: OpponentShow = parts[0]
	var recorder: Recorder = parts[1]
	_start(show, _shot(3.0))
	_step_to(show, 0.0, OpponentShow.STAND_S + OpponentShow.FEATHER_S * 0.5)
	check(show.backswing_m() > 0.0, "mid-feather")
	show.cancel()
	check(not show.is_showing(), "over")
	check_eq(show.backswing_m(), 0.0, "cue at rest")
	check(not show.is_processing(), "asleep")
	for i in MAX_FRAMES:
		show._process(DT)
	check(recorder.shots.is_empty(), "nothing struck")
	show.free()


func test_paused_holds_where_it_is() -> void:
	var parts := _show()
	var show: OpponentShow = parts[0]
	var recorder: Recorder = parts[1]
	var shot := _shot(3.0)
	var total := show.total_seconds(shot, 1.0)
	_start(show, shot)
	var t := _step_to(show, 0.0, OpponentShow.STAND_S + OpponentShow.FEATHER_S * 0.3)
	var frozen := show.backswing_m()
	check(frozen > 0.0, "mid-feather")
	show.set_paused(true)
	check(not show.is_processing(), "asleep while paused")
	check(show.is_showing(), "still on")
	for i in MAX_FRAMES:
		show._process(DT)
	check_eq(show.backswing_m(), frozen, "the cue stays put")
	check(recorder.shots.is_empty(), "nothing struck while paused")
	show.set_paused(false)
	check(show.is_processing(), "going again")
	var left := _run_out(show, recorder)
	check(left >= 0.0, "struck")
	check_near(t + left, total, DT + 1e-9, "the paused time is not counted")
	show.free()


func test_paused_before_begin_starts_frozen() -> void:
	var parts := _show()
	var show: OpponentShow = parts[0]
	var recorder: Recorder = parts[1]
	show.set_paused(true)
	check(not show.is_processing(), "nothing on")
	show.begin(_shot(3.0), 0.0, MAX_SPEED)
	check(not show.is_processing(), "the pause menu is still up")
	show.set_paused(false)
	show._process(DT)
	check_eq(recorder.shots.size(), 1, "goes once unpaused")
	show.free()


func test_begin_again_restarts_with_the_new_stroke() -> void:
	var parts := _show()
	var show: OpponentShow = parts[0]
	var recorder: Recorder = parts[1]
	var first := _shot(1.0)
	var second := _shot(6.0)
	second["shot_id"] = "s-2"
	var total := show.total_seconds(second, 1.0)
	_start(show, first)
	_step_to(show, 0.0, OpponentShow.STAND_S + OpponentShow.FEATHER_S * 1.5)
	check(show.backswing_m() > 0.0, "in the second feather")
	_start(show, second)
	check(show.is_showing(), "on")
	check_eq(show.backswing_m(), 0.0, "standing again")
	var left := _run_out(show, recorder)
	check_near(left, total, DT + 1e-9, "a whole show from the restart")
	check_eq(recorder.shots.size(), 1, "one stroke")
	if recorder.shots.size() == 1:
		check_eq(recorder.shots[0]["shot_id"], "s-2", "the new one")
	show.free()


func test_a_stroke_with_no_speed_takes_the_lowest_backswing() -> void:
	var strokes: Array[Dictionary] = [{}, {"strike": {}}, {"strike": "none"}, {"strike": {"speed_m_s": "fast"}},
			{"strike": {"speed_m_s": NAN}}]
	for shot in strokes:
		check_near(_hold_backswing(shot), OpponentShow.BACKSWING_MIN_M, 1e-9, var_to_str(shot))
		var parts := _show()
		var show: OpponentShow = parts[0]
		var recorder: Recorder = parts[1]
		_start(show, shot)
		check(_run_out(show, recorder) >= 0.0, "%s: still struck" % var_to_str(shot))
		show.free()
	check_eq(OpponentShow.last_backswing_m(_shot(3.0), 0.0), OpponentShow.BACKSWING_MIN_M, "no top speed")


func test_processing_only_while_showing() -> void:
	var parts := _show()
	var show: OpponentShow = parts[0]
	var recorder: Recorder = parts[1]
	check(not show.is_processing(), "asleep before begin")
	check(not show.is_showing(), "nothing on")
	check_eq(show.backswing_m(), 0.0, "cue at rest")
	_start(show, _shot(3.0))
	check(show.is_processing(), "awake during the show")
	_run_out(show, recorder)
	check(not show.is_processing(), "asleep after it")
	show.free()
