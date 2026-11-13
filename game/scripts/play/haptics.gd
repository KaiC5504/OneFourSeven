class_name Haptics
extends Node
## Vibration on the strike, ball contacts and pockets, scaled by impact strength (spec 13.3),
## through Godot's Input.vibrate_handheld. Off when Settings "haptics" is off.
##
## On iOS, Godot 4.7 plays each call as one Core Haptics continuous event of duration_ms with
## HapticIntensity = amplitude (no sharpness set), through a fresh pattern player each time. If
## that feels mushy on the phone, spec 13.3's fallback is a plugin playing transient events.

## At most one pulse in this many milliseconds, so a break-off doesn't buzz.
const MIN_INTERVAL_MS := 30
const BALL_MIN_SPEED := 0.3
const STRIKE_FULL_SPEED := 8.0
const TICK_MS := 4
const TICK_AMPLITUDE := 0.2

## Called as vibrate.call(duration_ms: int, amplitude: float); tests replace it.
var vibrate := Callable(Input, &"vibrate_handheld")
## Milliseconds now; tests replace it.
var clock_ms := Callable(Time, &"get_ticks_msec")
## Whether this device can vibrate: a phone with a haptic engine.
var handheld := _device_has_haptics()

static var _unavailable_logged := false

var _last_pulse_ms := -1


func on_event(event: Dictionary) -> void:
	if not bool(Settings.get_value("haptics")):
		return
	if not handheld:
		_log_unavailable()
		return
	var pulse := pulse_for(String(event.get("kind", "")), float(event.get("speed", 0.0)))
	if pulse.x <= 0.0 or pulse.y <= 0.0:
		return
	var now := int(clock_ms.call())
	if _last_pulse_ms >= 0 and now - _last_pulse_ms < MIN_INTERVAL_MS:
		return
	_last_pulse_ms = now
	vibrate.call(roundi(pulse.x), pulse.y)


## A faint tick for a step of the fine-tune wheel, under the same rate limit as the events.
func tick() -> void:
	if not bool(Settings.get_value("haptics")):
		return
	if not handheld:
		_log_unavailable()
		return
	var now := int(clock_ms.call())
	if _last_pulse_ms >= 0 and now - _last_pulse_ms < MIN_INTERVAL_MS:
		return
	_last_pulse_ms = now
	vibrate.call(TICK_MS, TICK_AMPLITUDE)


## (duration_ms, amplitude 0-1) for an event; zero for none. Never weaker for a faster hit.
static func pulse_for(kind: String, speed: float) -> Vector2:
	var s := maxf(speed, 0.0)
	match kind:
		"strike":
			if s <= 0.0:
				return Vector2.ZERO
			var k := clampf(s / STRIKE_FULL_SPEED, 0.0, 1.0)
			return Vector2(lerpf(12.0, 20.0, k), lerpf(0.35, 1.0, sqrt(k)))
		"ball_ball":
			if s < BALL_MIN_SPEED:
				return Vector2.ZERO
			return Vector2(8.0, clampf(s / 3.0, 0.0, 1.0))
		"ball_line", "ball_arc":
			if s < BALL_MIN_SPEED:
				return Vector2.ZERO
			return Vector2(6.0, 0.6 * clampf(s / 3.0, 0.0, 1.0))
		"pocket":
			return Vector2(15.0, clampf(0.45 + s / 6.0, 0.45, 0.9))
	return Vector2.ZERO


func _log_unavailable() -> void:
	if _unavailable_logged:
		return
	_unavailable_logged = true
	var reason := "%s is not a handheld platform; Input.vibrate_handheld does nothing there" % OS.get_name()
	if OS.has_feature("mobile"):
		reason = "%s (%s) reports no haptic engine; Godot would fall back to the 0.4 s system vibration" % [
				OS.get_name(), OS.get_model_name()]
	Log.info("audio", "haptics.unavailable", {"reason": reason})


static func _device_has_haptics() -> bool:
	if not OS.has_feature("mobile"):
		return false
	# Godot's iOS vibrate_handheld falls back to the long system buzz without a haptic engine,
	# which would be worse than nothing on every ball contact.
	if Engine.has_singleton("AppleEmbedded"):
		return bool(Engine.get_singleton("AppleEmbedded").call("supports_haptic_engine"))
	return true
