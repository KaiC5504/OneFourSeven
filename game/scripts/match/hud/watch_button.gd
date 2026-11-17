class_name WatchButton
extends Button
## The view of the opponent's turn (spec 10.5): behind its cue, the table from above, or walking
## round it. One button that steps through the three; it shows the view a tap would go to. The
## table scene shows it while the opponent is at the table and asks CameraRig for the view chosen.

## A tap chose `view`, one of VIEWS.
signal view_chosen(view: String)

const VIEWS: Array[String] = ["cue", "above", "round"]
## What the button says when a tap would go to that view.
const TEXT := {"cue": "Cue view", "above": "From above", "round": "Walk round"}
const SIZE := Vector2(190.0, 72.0)


## Shows `view` as the one now on, without emitting.
func set_view(_view: String) -> void:
	pass


## The view now on.
func view() -> String:
	return "cue"


## Puts the button in the top-right corner of `area` (canvas units, the safe area less whatever
## the scoreboard takes).
func place_in(_area: Rect2) -> void:
	pass
