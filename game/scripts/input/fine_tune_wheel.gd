class_name FineTuneWheel
extends Control
## The fine-tune thumb wheel on the right screen edge (spec 8.1). Every PX_PER_STEP canvas units
## of vertical drag, divided by Settings "fine_tune_sensitivity", turn the aim one STEP_DEG step:
## dragging up turns the aim anticlockwise seen from above (to the player's left, +aim_rad). Each
## change is recorded with ShotSetup.add_fine_tune_sample.

const STEP_DEG := 0.01
const PX_PER_STEP := 6.0

## Seconds; tests replace it.
var clock: Callable = func() -> float: return Time.get_ticks_usec() / 1.0e6


func bind(_setup: ShotSetup) -> void:
	pass
