extends Node
## Physics autoload: the one OfsPhysics the game plays with (spec 5). Configured from Params at
## start, and again when Params.commit_pending offers new parameters at a shot boundary
## (try_params); Params applies them only once this has taken them.

const TABLE_SHAPE_PATH := "res://data/table_shape.json"

var sim := OfsPhysics.new()

var _shots := 0
var _rng := RandomNumberGenerator.new()
var _values: Dictionary = {}


func _ready() -> void:
	_rng.randomize()
	_configure(Params.current_text(), Params.current_hash())


## Reconfigures with new parameters and the embedded table shape; false, keeping the previous
## configuration, when the binding refuses them (Params.commit_pending logs params.refused).
func try_params(params_text: String, params_hash: String) -> bool:
	return _configure(params_text, params_hash)


## Configures the binding with the given parameters and the embedded table shape. A refusal keeps
## the previous configuration (or none) and logs app.error besides the binding's physics.configured.
func _configure(params_text: String, params_hash: String) -> bool:
	var table_text := FileAccess.get_file_as_string(TABLE_SHAPE_PATH)
	if table_text.is_empty():
		_refused("%s is missing or empty (%s); run scripts/sync_data.py" % [TABLE_SHAPE_PATH,
				error_string(FileAccess.get_open_error())])
		return false
	if not sim.configure(params_text, params_hash, table_text):
		_refused("parameters %s or the table shape were refused (see physics.configured); still on %s" % [
				params_hash, sim.params_hash() if sim.is_ready() else "no configuration"])
		return false
	_values = sim.params_values()
	return true


func is_ready() -> bool:
	return sim.is_ready()


## "<session>-<n>", n counting this session's shots from 1.
func new_shot_id() -> String:
	_shots += 1
	return "%s-%d" % [Log.session(), _shots]


## A fresh 63-bit seed for a played shot; logged in its repro bundle.
func new_seed() -> int:
	return ((_rng.randi() & 0x7fffffff) << 32) | _rng.randi()


## The shot as played; null when the binding refused it (it logs shot.rejected).
func simulate(balls: Array[Dictionary], cue_ball: int, strike: Dictionary, context: Dictionary) -> OfsShotRecord:
	return sim.simulate(balls, cue_ball, strike, new_seed(), context)


## Guide level 3's noise-free prediction; null when it can't be simulated.
func predict(balls: Array[Dictionary], cue_ball: int, strike: Dictionary) -> OfsShotRecord:
	return sim.predict(balls, cue_ball, strike)


func min_cue_elevation(balls: Array[Dictionary], cue_ball: int, strike: Dictionary) -> float:
	return sim.min_cue_elevation(balls, cue_ball, strike)


## Why the last simulate, predict or min_cue_elevation was refused; "" after a success.
func last_error() -> String:
	# Extensions built before OfsPhysics.last_error existed still load.
	if sim.has_method("last_error"):
		return sim.call("last_error")
	return ""


## Empty until configured.
func table_shape() -> Dictionary:
	return sim.table_shape()


## The full rack with seeded micro-gaps (spec 7.7), cue ball first; empty until configured.
func opening_rack(seed: int) -> Array[Dictionary]:
	var balls: Array[Dictionary] = []
	for ball: Dictionary in sim.opening_rack(seed):
		balls.append(ball)
	return balls


## A parameter in use, such as "miscue_limit" or "max_cue_speed_m_s"; `fallback` before configure.
func param(name: String, fallback: float) -> float:
	var value: Variant = _values.get(name)
	if value is float or value is int:
		return float(value)
	return fallback


func _refused(message: String) -> void:
	Log.error("app", "app.error", {"kind": "message", "message": "physics: " + message})
