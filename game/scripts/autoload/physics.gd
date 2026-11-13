extends Node
## Physics autoload: the one OfsPhysics the game plays with (spec 5). Configured from Params at
## start, and again whenever Params applies new parameters, which happens only at a shot
## boundary (Params.commit_pending).

const TABLE_SHAPE_PATH := "res://data/table_shape.json"

var sim := OfsPhysics.new()


func is_ready() -> bool:
	return sim.is_ready()


## "<session>-<n>", n counting this session's shots from 1.
func new_shot_id() -> String:
	return ""


## A fresh 63-bit seed for a played shot; logged in its repro bundle.
func new_seed() -> int:
	return 0


## The shot as played; null when the binding refused it (it logs shot.rejected).
func simulate(_balls: Array[Dictionary], _cue_ball: int, _strike: Dictionary, _context: Dictionary) -> OfsShotRecord:
	return null


## Guide level 3's noise-free prediction; null when it can't be simulated.
func predict(_balls: Array[Dictionary], _cue_ball: int, _strike: Dictionary) -> OfsShotRecord:
	return null


func min_cue_elevation(_balls: Array[Dictionary], _cue_ball: int, _strike: Dictionary) -> float:
	return 0.0


## OfsPhysics.table_shape(): empty until configured.
func table_shape() -> Dictionary:
	return {}


## The full rack with seeded micro-gaps (spec 7.7), cue ball first; empty until configured.
func opening_rack(_seed: int) -> Array[Dictionary]:
	return []


## A parameter in use, such as "miscue_limit" or "max_cue_speed_m_s"; `fallback` before configure.
func param(_name: String, fallback: float) -> float:
	return fallback
