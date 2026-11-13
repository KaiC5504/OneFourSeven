extends Node
## Settings autoload (spec 15): every player setting, saved to user://settings.json. M2 has the
## ones the feel build needs; M5 adds the full settings screen.

signal changed(key: String, value: Variant)

const PATH := "user://settings.json"
const VERSION := 1
const STRIKE_MODES: Array[String] = ["stroke", "power_bar"]
const DEFAULTS := {
	"strike_mode": "stroke",
	"guide_level": 1,
	"aim_sensitivity": 1.0,
	"fine_tune_sensitivity": 1.0,
	"stroke_wander": false,
	"reset_spin_after_shot": true,
	"volume": 0.8,
	"haptics": true,
}

var _values := DEFAULTS.duplicate()


func get_value(key: String) -> Variant:
	return _values.get(key, DEFAULTS.get(key))


## Refuses an unknown key or a value of the wrong type or range (logged); otherwise stores it,
## saves, logs settings.changed and emits changed.
func set_value(key: String, value: Variant) -> bool:
	if not DEFAULTS.has(key):
		return false
	_values[key] = value
	changed.emit(key, value)
	return true


func all() -> Dictionary:
	return _values.duplicate()
