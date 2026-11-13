class_name ShotSetup
extends RefCounted
## The shot being set up (spec 8): what the controls write and the strike is built from. One
## instance per table, shared by the aim, fine-tune, spin, elevation and strike controls, the
## guides and the cue view. Angles in radians, speeds in m/s, tip offsets in ball radii (x right
## of centre as the player sees it, y above centre). aim_rad is the cue's direction in core
## coordinates, from +x towards +y (ofs::phys::CueStrike).

## &"aim", &"tip", &"elevation", &"elevation_min", &"speed", &"mode" or &"reset".
signal changed(what: StringName)

## Push: pull back, push forward and release; the push speed sets the pace (spec 8.3).
const STROKE := "stroke"
## Pull and release: the pull sets the power and the release fires (KaiC, M2 phone session).
const PULL := "pull"
## Guide level 3's pace before the session's first shot (spec 8.4).
const DEFAULT_PACE_M_S := 1.5
## Keeps shot.input well under the logger's 10,000-value cap after a long fine-tune session.
const MAX_FINE_TUNE_SAMPLES := 400

var aim_rad := 0.0
var tip_x := 0.0
var tip_y := 0.0
## What the player asked for. The cue obstruction may raise it (spec 7.3).
var elevation_rad := 0.0
## The obstruction's minimum for the current aim and tip; the integrator keeps it current.
var elevation_min_rad := 0.0
## The next strike's cue speed: set at the release of a push or a pull.
var speed_m_s := 0.0
## The pull's power, 0 to 1: live while pulling, then the last one set (Space replays it).
var power := 0.0
var strike_mode := STROKE
## True while a pull is past its threshold; guide level 3 then uses the pull's speed.
var pulling := false
## The last shot's speed, whichever way it was struck.
var last_stroke_speed_m_s := 0.0
## Raw input for shot.input (spec 16.10): [t_s, x, y] in the cue strip's canvas units for the
## latest press on it only (the push stroke, or the pull), and [t_s, delta_rad] for the last
## MAX_FINE_TUNE_SAMPLES fine-tune steps. t_s is the control's clock.
var stroke_samples: Array[PackedFloat64Array] = []
var fine_tune_samples: Array[PackedFloat64Array] = []
## Aim error added by the stroke-wander setting at release (spec 8.3), included in to_strike().
var wander_rad := 0.0


func set_aim(rad: float) -> void:
	aim_rad = wrapf(rad, -PI, PI)
	changed.emit(&"aim")


func add_aim(delta_rad: float) -> void:
	set_aim(aim_rad + delta_rad)


func set_tip(x: float, y: float) -> void:
	tip_x = x
	tip_y = y
	changed.emit(&"tip")


func set_elevation(rad: float) -> void:
	elevation_rad = maxf(rad, 0.0)
	changed.emit(&"elevation")


func set_elevation_min(rad: float) -> void:
	if is_equal_approx(rad, elevation_min_rad):
		return
	elevation_min_rad = maxf(rad, 0.0)
	changed.emit(&"elevation_min")


func effective_elevation() -> float:
	return maxf(elevation_rad, elevation_min_rad)


func elevation_forced() -> bool:
	return elevation_min_rad > elevation_rad


func set_speed(m_s: float, bar_power: float = -1.0) -> void:
	speed_m_s = maxf(m_s, 0.0)
	if bar_power >= 0.0:
		power = clampf(bar_power, 0.0, 1.0)
	changed.emit(&"speed")


## A pull in progress at power 0-1 and the speed it would strike at.
func set_pull(pull_power: float, m_s: float) -> void:
	pulling = true
	set_speed(m_s, pull_power)


## The pull is over: released at, or cancelled back to, this power and speed.
func end_pull(pull_power: float, m_s: float) -> void:
	pulling = false
	set_speed(m_s, pull_power)


func set_strike_mode(mode: String) -> void:
	strike_mode = mode
	pulling = false
	changed.emit(&"mode")


## The pace guide level 3 assumes (spec 8.4): a pull's while pulling, else the last shot's.
func guide_pace_m_s() -> float:
	if strike_mode == PULL and pulling:
		return speed_m_s
	return last_stroke_speed_m_s if last_stroke_speed_m_s > 0.0 else DEFAULT_PACE_M_S


## A new stroke press: only the stroke that strikes is logged.
func clear_stroke_samples() -> void:
	stroke_samples.clear()


func add_stroke_sample(t_s: float, position: Vector2) -> void:
	stroke_samples.append(PackedFloat64Array([t_s, position.x, position.y]))


func add_fine_tune_sample(t_s: float, delta_rad: float) -> void:
	if fine_tune_samples.size() >= MAX_FINE_TUNE_SAMPLES:
		fine_tune_samples.remove_at(0)
	fine_tune_samples.append(PackedFloat64Array([t_s, delta_rad]))


## The strike OfsPhysics takes: the requested elevation (the core raises it to the obstruction
## minimum itself and reports that) and the aim with any wander error.
func to_strike() -> Dictionary:
	return {
		"aim_rad": wrapf(aim_rad + wander_rad, -PI, PI),
		"tip_x": tip_x,
		"tip_y": tip_y,
		"elevation_rad": elevation_rad,
		"speed_m_s": speed_m_s,
	}


## Everything a retake restores.
func snapshot() -> Dictionary:
	return {
		"aim_rad": aim_rad, "tip_x": tip_x, "tip_y": tip_y, "elevation_rad": elevation_rad,
		"speed_m_s": speed_m_s, "power": power, "strike_mode": strike_mode,
	}


func restore(snap: Dictionary) -> void:
	aim_rad = float(snap.get("aim_rad", aim_rad))
	tip_x = float(snap.get("tip_x", tip_x))
	tip_y = float(snap.get("tip_y", tip_y))
	elevation_rad = float(snap.get("elevation_rad", elevation_rad))
	speed_m_s = float(snap.get("speed_m_s", speed_m_s))
	power = float(snap.get("power", power))
	strike_mode = String(snap.get("strike_mode", strike_mode))
	pulling = false
	changed.emit(&"reset")


## After a strike: the samples start over; spin and elevation go back to centre ball and the
## minimum when reset_spin is on (spec 8.2).
func after_shot(reset_spin: bool) -> void:
	if speed_m_s > 0.0:
		last_stroke_speed_m_s = speed_m_s
	pulling = false
	stroke_samples.clear()
	fine_tune_samples.clear()
	wander_rad = 0.0
	if reset_spin:
		tip_x = 0.0
		tip_y = 0.0
		elevation_rad = 0.0
	changed.emit(&"reset")


## The shot.input fields this object knows (spec 16.10); the caller adds shot_id, mode and
## guide_level.
func log_fields() -> Dictionary:
	var stroke: Array = []
	for s in stroke_samples:
		stroke.append(Array(s))
	var fine: Array = []
	for s in fine_tune_samples:
		fine.append(Array(s))
	return {
		"strike_mode": strike_mode,
		"aim_rad": aim_rad,
		"wander_rad": wander_rad,
		"tip_x": tip_x,
		"tip_y": tip_y,
		"elevation_requested_rad": elevation_rad,
		"elevation_min_rad": elevation_min_rad,
		"speed_m_s": speed_m_s,
		"power": power,
		"stroke_samples": stroke,
		"fine_tune_samples": fine,
	}
