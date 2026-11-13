class_name TableAudio
extends Node3D
## Table sounds (spec 13.2): ball clicks, cushion hits, pocket drops and rattles, the tip, the
## miscue and a faint roll, each from the event's place on the table so they pan with the camera,
## louder and brighter with impact speed, never twice the same. Volume follows
## Settings "volume"; everything plays on the Master bus.


func on_event(_event: Dictionary, _state: PackedFloat64Array) -> void:
	pass


## The rolling sound follows the moving balls.
func on_frame(_state: PackedFloat64Array, _t: float, _dt: float) -> void:
	pass


func stop_all() -> void:
	pass
