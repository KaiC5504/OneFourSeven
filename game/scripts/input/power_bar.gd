class_name PowerBar
extends Control
## The power bar (spec 8.3): set power on a slider with a non-linear scale for fine low-power
## control, then tap Shoot.

signal power_changed(power: float)
signal shoot_pressed


func bind(_setup: ShotSetup) -> void:
	pass


## Cue speed for a bar position 0-1: 0 at 0, max_speed_m_s at 1, finer at the low end.
static func speed_for_power(_power: float, _max_speed_m_s: float) -> float:
	return 0.0
