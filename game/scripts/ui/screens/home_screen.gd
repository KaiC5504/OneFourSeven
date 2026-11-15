class_name HomeScreen
extends Control
## Home (spec 11.1), where the app opens from M3: Play, Free table and Settings, over the table
## seen from above. Stats arrive in M5 and get no button until then. Covers the canvas and takes
## every touch while it shows.
##
## Contract skeleton (M3): the signatures and signals are fixed; the look and the body are the
## builder's.

signal play_pressed
signal free_table_pressed
signal settings_pressed

## When true the screen fits itself to the safe area; set false to call place_in.
var auto_place := true


func open() -> void:
	visible = true


func close() -> void:
	visible = false


## "play", "free_table" or "settings".
func button(_id: String) -> Button:
	return null


func place_in(_safe_rect: Rect2) -> void:
	pass
