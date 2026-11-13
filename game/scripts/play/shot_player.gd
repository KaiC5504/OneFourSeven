class_name ShotPlayer
extends Node
## Plays a shot back from its record (spec 5): advances shot time by real time times `speed`,
## emits every frame's state and each event as play reaches it, and keeps the game rendering at
## the display rate while it plays (Perf.set_active). Logs shot.playback when it ends.

signal frame(state: PackedFloat64Array, t: float, dt: float)
signal event_reached(event: Dictionary, state: PackedFloat64Array)
signal finished(shot_id: String)

var speed := 1.0


func play(_playback: ShotPlayback, _shot_id: String) -> void:
	pass


func is_playing() -> bool:
	return false


## Jumps to the end, emitting the final frame and every event not yet reached, then finished.
func skip_to_end() -> void:
	pass
