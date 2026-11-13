class_name FreeTableBar
extends Control
## The free table's buttons: Retake, Undo, Redo, Edit, Rack, Empty, Settings.

signal retake_pressed
signal undo_pressed
signal redo_pressed
signal edit_toggled(on: bool)
## "full_rack" or "empty".
signal setup_chosen(name: String)
signal settings_pressed


func set_history(_can_undo: bool, _can_redo: bool, _can_retake: bool) -> void:
	pass


func set_edit_mode(_on: bool) -> void:
	pass
