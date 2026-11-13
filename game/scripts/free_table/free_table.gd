class_name FreeTable
extends Node
## Free table (spec 12.1): a full rack or an empty table, an edit mode that moves, adds and
## removes balls and places the cue ball anywhere legal, retake (the exact pre-shot state and
## shot setup again) and undo and redo across the session's shots. No rules or fouls.

signal state_changed(balls: Array[Dictionary], cue_ball: int, reason: StringName)
signal history_changed(can_undo: bool, can_redo: bool, can_retake: bool)

var balls: Array[Dictionary] = []
var cue_ball := 0
var edit_mode := false


## "full_rack" or "empty" (the cue ball alone, in the D).
func load_setup(_name: String) -> void:
	pass


## A position from elsewhere (the harness): replaces the table without touching history.
func set_balls(_balls: Array[Dictionary], _cue_ball: int) -> void:
	pass


## setup_snapshot is ShotSetup.snapshot() as it was struck.
func shot_started(_setup_snapshot: Dictionary) -> void:
	pass


## OfsShotRecord.final_balls(): potted balls leave the table; a potted cue ball comes back in the D.
func shot_finished(_final_balls: Array) -> void:
	pass


## Each returns the ShotSetup snapshot to restore, or {} when there is nothing to do.
func retake() -> Dictionary:
	return {}


func undo() -> Dictionary:
	return {}


func redo() -> Dictionary:
	return {}


func set_edit_mode(_on: bool) -> void:
	pass


## Edits; each false (and logged) when the spot isn't legal.
func move_ball(_index: int, _xy: Vector2) -> bool:
	return false


func add_ball(_ball: String, _xy: Vector2) -> bool:
	return false


func remove_ball(_index: int) -> bool:
	return false


## On the cloth inside the cushions and clear of every other ball (ignore: that ball's index).
func is_legal(_xy: Vector2, _ignore: int = -1) -> bool:
	return false
