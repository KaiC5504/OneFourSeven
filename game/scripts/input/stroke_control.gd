class_name StrokeControl
extends Control
## The finger stroke (spec 8.3) in its stroke area: pull back (drag down), push forward (drag up)
## and release. The push speed over the last WINDOW_S before release sets the cue speed through
## speed_for_push, which gives fine control at low power. The cue follows the backswing. Dragging
## out of the area cancels. With Settings "stroke_wander" on, sideways wander during the push adds
## a small aim error (ShotSetup.wander_rad).

signal backswing_changed(distance_m: float)
signal struck(speed_m_s: float)
signal cancelled(reason: String)

const WINDOW_S := 0.08

## Seconds; tests replace it.
var clock: Callable = func() -> float: return Time.get_ticks_usec() / 1.0e6


func bind(_setup: ShotSetup) -> void:
	pass


## Cue speed for a push of push_px_per_s canvas units per second: 0 at 0, rising slowly at first,
## max_speed_m_s at and beyond the fastest push.
static func speed_for_push(_push_px_per_s: float, _max_speed_m_s: float) -> float:
	return 0.0
