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


## Starts the show for `shot` (MatchController.opponent_ready's Dictionary: its "strike" has
## speed_m_s). `pace` scales every time (MatchController.pace: 0 strikes on the next frame).
## `max_speed_m_s` is the physics' top cue speed, for the length of the last backswing.
func begin(_shot: Dictionary, _pace: float, _max_speed_m_s: float) -> void:
	pass


## Drops the show without striking (the match was left).
func cancel() -> void:
	pass


func is_showing() -> bool:
	return false


## Holds the show where it is while the pause menu is up.
func set_paused(_on: bool) -> void:
	pass


## How far the cue is drawn back from its address now, in metres: 0 while it stands, up to
## FEATHER_PULL_M in a feather, up to the last backswing before the stroke.
func backswing_m() -> float:
	return 0.0


## The whole show's length for `shot` at `pace`, in seconds.
func total_seconds(_shot: Dictionary, _pace: float) -> float:
	return 0.0
