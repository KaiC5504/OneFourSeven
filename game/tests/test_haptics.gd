extends TestCase

const KINDS: Array[String] = ["strike", "ball_ball", "ball_line", "ball_arc", "pocket"]


## A Haptics whose vibrate call and clock are recorded and scripted.
class Rig:
	var haptics := Haptics.new()
	var calls: Array[Vector2] = []
	var now := 1000

	func _init() -> void:
		haptics.handheld = true
		haptics.vibrate = func(duration_ms: int, amplitude: float) -> void:
			calls.append(Vector2(duration_ms, amplitude))
		haptics.clock_ms = func() -> int: return now


func _event(kind: String, speed: float) -> Dictionary:
	return {"t": 0.0, "kind": kind, "a": 0, "b": 1, "body": -1, "from": "", "to": "", "speed": speed}


func test_pulses_never_weaken_with_speed() -> void:
	for kind in KINDS:
		var previous := Vector2.ZERO
		var speed := 0.0
		while speed <= 12.0:
			var pulse := Haptics.pulse_for(kind, speed)
			check(pulse.x >= previous.x and pulse.y >= previous.y,
					"%s at %.2f m/s: %s after %s" % [kind, speed, pulse, previous])
			check(pulse.y <= 1.0, "%s amplitude within 0-1" % kind)
			previous = pulse
			speed += 0.05
	check_eq(Haptics.pulse_for("transition", 3.0), Vector2.ZERO, "no pulse for a transition")


func test_pulse_shapes() -> void:
	var soft := Haptics.pulse_for("strike", 0.5)
	var hard := Haptics.pulse_for("strike", 9.0)
	check(soft.x >= 12.0 and hard.x <= 20.0 and hard.x > soft.x, "strike 12-20 ms: %s, %s" % [soft, hard])
	check_eq(Haptics.pulse_for("ball_ball", 0.29), Vector2.ZERO, "nothing for a click under 0.3 m/s")
	check_near(Haptics.pulse_for("ball_ball", 1.5).x, 8.0, 1e-9, "a click is about 8 ms")
	check_near(Haptics.pulse_for("ball_ball", 1.5).y, 0.5, 1e-9, "scaled by speed / 3")
	check(Haptics.pulse_for("ball_line", 1.5).y < Haptics.pulse_for("ball_ball", 1.5).y, "a cushion is lighter")
	check_near(Haptics.pulse_for("pocket", 1.0).x, 15.0, 1e-9, "a pocket is about 15 ms")


func test_vibrates_with_the_pulse() -> void:
	var rig := Rig.new()
	rig.haptics.on_event(_event("ball_ball", 1.5))
	check_eq(rig.calls, [Vector2(8, 0.5)] as Array[Vector2], "one call with the click's pulse")
	rig.haptics.free()


func test_at_most_one_pulse_per_30_ms() -> void:
	var rig := Rig.new()
	for i in 10:
		rig.haptics.on_event(_event("ball_ball", 2.0))
		rig.now += 5
	check_eq(rig.calls.size(), 2, "50 ms of clicks give pulses at 0 and 30 ms")
	rig.now += 29
	rig.haptics.on_event(_event("pocket", 1.0))
	check_eq(rig.calls.size(), 3, "a pocket 30 ms after the last pulse")
	rig.haptics.on_event(_event("pocket", 1.0))
	check_eq(rig.calls.size(), 3, "not straight after")
	rig.haptics.free()


func test_weak_contacts_dont_use_up_the_window() -> void:
	var rig := Rig.new()
	rig.haptics.on_event(_event("ball_ball", 0.1))
	rig.haptics.on_event(_event("strike", 3.0))
	check_eq(rig.calls.size(), 1, "the strike still pulses")
	rig.haptics.free()


func test_setting_off_means_nothing() -> void:
	var rig := Rig.new()
	var before: Variant = Settings.get_value("haptics")
	Settings.set_value("haptics", false)
	for kind in KINDS:
		rig.haptics.on_event(_event(kind, 5.0))
		rig.now += 100
	Settings.set_value("haptics", before)
	check_eq(rig.calls.size(), 0, "no pulses with haptics off")
	rig.haptics.on_event(_event("strike", 5.0))
	check_eq(rig.calls.size(), 1, "back on")
	rig.haptics.free()


func test_not_handheld_does_nothing() -> void:
	var rig := Rig.new()
	rig.haptics.handheld = false
	rig.haptics.on_event(_event("strike", 5.0))
	rig.haptics.on_event(_event("pocket", 1.0))
	check_eq(rig.calls.size(), 0, "no pulses off a phone")
	OfsLog.flush()
	var logged := 0
	for line in FileAccess.get_file_as_string(OfsLog.file_path()).split("\n", false):
		if line.contains("\"haptics.unavailable\""):
			logged += 1
	check_eq(logged, 1, "haptics.unavailable logged once per session")
	var fresh := Haptics.new()
	check(not fresh.handheld or OS.has_feature("mobile"), "a PC is not handheld")
	fresh.free()
	rig.haptics.free()
