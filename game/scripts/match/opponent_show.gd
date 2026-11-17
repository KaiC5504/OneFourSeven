class_name OpponentShow
extends Node
## The opponent at the table before it strikes (spec 10.5): its cue stands on the line, feathers
## and goes through, so even an instant decision does not look robotic. It only keeps the time and
## says how far the cue is drawn back; the table scene poses the cue with backswing_m() and
## strikes on strike_due.

## The feathering is done: strike `shot` now (the Dictionary given to begin()).
signal strike_due(shot: Dictionary)

## Seconds the cue stands still on its line first, at pace 1.
const STAND_S := 0.35
## Feathers before the stroke, and the seconds each takes (back and forward).
const FEATHERS := 2
const FEATHER_S := 0.42
## How far a feather draws the cue back, and the last backswing at the lowest and the highest
## cue speed, in metres.
const FEATHER_PULL_M := 0.07
const BACKSWING_MIN_M := 0.06
const BACKSWING_MAX_M := 0.24
## Seconds the last backswing takes, and its hold at the back.
const BACKSWING_S := 0.38
const HOLD_S := 0.12

var _shot: Dictionary = {}
var _pace := 1.0
var _last_back_m := BACKSWING_MIN_M
var _elapsed := 0.0
var _showing := false
var _paused := false
var _fresh := false


func _init() -> void:
	set_process(false)


# Entering the tree turns processing on because _process exists.
func _ready() -> void:
	set_process(_showing and not _paused)


## Starts the show for `shot` (MatchController.opponent_ready's Dictionary: its "strike" has
## speed_m_s). `pace` scales every time (MatchController.pace: 0 strikes on the next frame).
## `max_speed_m_s` is the physics' top cue speed, for the length of the last backswing.
func begin(shot: Dictionary, pace: float, max_speed_m_s: float) -> void:
	_shot = shot
	_pace = maxf(pace, 0.0)
	_last_back_m = last_backswing_m(shot, max_speed_m_s)
	_elapsed = 0.0
	_showing = true
	_fresh = true
	set_process(not _paused)


## Drops the show without striking (the match was left).
func cancel() -> void:
	_showing = false
	_shot = {}
	# Leaving the match from the pause menu may never unpause: the next show must not start frozen.
	_paused = false
	set_process(false)


func is_showing() -> bool:
	return _showing


## Holds the show where it is while the pause menu is up.
func set_paused(on: bool) -> void:
	_paused = on
	set_process(_showing and not on)


## How far the cue is drawn back from its address now, in metres: 0 while it stands, up to
## FEATHER_PULL_M in a feather, up to the last backswing before the stroke.
func backswing_m() -> float:
	if not _showing:
		return 0.0
	var t := _elapsed
	var stand := STAND_S * _pace
	if t < stand:
		return 0.0
	t -= stand
	var feather := FEATHER_S * _pace
	if t < feather * FEATHERS:
		var u := fmod(t, feather) / feather
		return FEATHER_PULL_M * pow(sin(PI * u), 2.0)
	t -= feather * FEATHERS
	var back := BACKSWING_S * _pace
	if t < back:
		var u := t / back
		return _last_back_m * (1.0 - (1.0 - u) * (1.0 - u))
	return _last_back_m


## The whole show's length for `shot` at `pace`, in seconds.
func total_seconds(_shot_given: Dictionary, pace: float) -> float:
	return (STAND_S + FEATHERS * FEATHER_S + BACKSWING_S + HOLD_S) * maxf(pace, 0.0)


## The last backswing's length for `shot`: the lowest when it carries no usable speed.
static func last_backswing_m(shot: Dictionary, max_speed_m_s: float) -> float:
	var strike: Variant = shot.get("strike")
	if not strike is Dictionary or max_speed_m_s <= 0.0:
		return BACKSWING_MIN_M
	var speed: Variant = (strike as Dictionary).get("speed_m_s")
	if not (speed is float or speed is int) or not is_finite(float(speed)):
		return BACKSWING_MIN_M
	return lerpf(BACKSWING_MIN_M, BACKSWING_MAX_M, clampf(float(speed) / max_speed_m_s, 0.0, 1.0))


func _process(delta: float) -> void:
	if not _showing or _paused:
		set_process(false)
		return
	# The first frame's delta is time from before the show began.
	if _fresh:
		_fresh = false
	else:
		_elapsed += delta
	if _elapsed < total_seconds(_shot, _pace):
		return
	var shot := _shot
	# Before the signal: a listener may begin the next show.
	cancel()
	strike_due.emit(shot)
