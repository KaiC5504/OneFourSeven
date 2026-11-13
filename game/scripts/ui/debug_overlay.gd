class_name DebugOverlay
extends Control
## The hidden on-device overlay (spec 16.6), opened by a three-finger tap (F3 on Windows): fps,
## frame times, the last shot's simulation time, the last prediction's time, thermal state,
## parameter hash and phone link status.


func toggle(_via: String) -> void:
	pass


func set_shot_times(_sim_us: float, _predict_us: float) -> void:
	pass
