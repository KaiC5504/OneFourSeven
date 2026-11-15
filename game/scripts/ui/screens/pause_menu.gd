class_name PauseMenu
extends Control
## The match's pause menu (spec 11.1): Resume, Concede frame, Concede match, Settings and Quit.
## Quit asks to confirm in place, on the same card, since the match is discarded (spec 11.2); so do
## the two concessions, which cannot be withdrawn (Section 4 Rule 2(e)). A modal card in the middle
## of the safe area; tapping outside it resumes.
##
## Contract skeleton (M3): the signatures and signals are fixed; the look and the body are the
## builder's.

signal resume_pressed
## Each is emitted only after the confirming press.
signal concede_frame_confirmed
signal concede_match_confirmed
signal quit_confirmed
signal settings_pressed

var auto_place := true


## can_concede_frame: whether the engine offers the person concede_frame now (the frame view's
## options); the button is disabled otherwise.
func open(_can_concede_frame: bool) -> void:
	visible = true


func close() -> void:
	visible = false


## "resume", "concede_frame", "concede_match", "settings", "quit", and while a confirmation
## shows, "confirm" and "cancel".
func button(_id: String) -> Button:
	return null


## Which action is waiting for its confirming press: "concede_frame", "concede_match", "quit" or "".
func confirming() -> String:
	return ""


func place_in(_safe_rect: Rect2) -> void:
	pass
