class_name ExhibitionSummary
extends Control
## After Watch a 147 (spec 10.1): the break the 147 made, and Again or Home. Built like the frame
## summary (ScreenKit).

signal again_pressed
signal home_pressed

var auto_place := true


## result: {break_points: int, maximum: bool, why: "maximum" | "missed" | "fouled" | "left"}.
func open(_result: Dictionary) -> void:
	pass


func close() -> void:
	pass


## "again" or "home".
func control(_id: String) -> Control:
	return null


## Every line it shows, top to bottom, for tests and the harness.
func texts() -> PackedStringArray:
	return PackedStringArray()


func place_in(_safe_rect: Rect2) -> void:
	pass
