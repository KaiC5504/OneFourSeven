extends TestCase

const KINDS: Array[String] = ["strike", "ball_ball", "ball_line", "ball_arc", "pocket"]
const S := ShotPlayback.STRIDE


func _audio() -> TableAudio:
	var audio := TableAudio.new()
	tree.root.add_child(audio)
	audio.rng.seed = 147
	return audio


func _state(balls: Array) -> PackedFloat64Array:
	var state := PackedFloat64Array()
	state.resize(balls.size() * S)
	for i in balls.size():
		var ball: Dictionary = balls[i]
		state[i * S + ShotPlayback.X] = float(ball.get("x", 0.0))
		state[i * S + ShotPlayback.Y] = float(ball.get("y", 0.0))
		state[i * S + ShotPlayback.VX] = float(ball.get("vx", 0.0))
		state[i * S + ShotPlayback.VY] = float(ball.get("vy", 0.0))
		state[i * S + ShotPlayback.MOTION] = float(ball.get("motion", ShotPlayback.STATIONARY))
		state[i * S + ShotPlayback.POCKET] = -1.0
	return state


func _two_balls() -> PackedFloat64Array:
	return _state([{"x": -1.0, "y": 0.2}, {"x": 0.6, "y": -0.4}])


func _event(kind: String, speed: float, body: int = 0) -> Dictionary:
	return {"t": 0.5, "kind": kind, "a": 0, "b": 1 if kind == "ball_ball" else -1, "body": body,
			"from": "rolling", "to": "rolling", "speed": speed}


## What on_event played: [sound, volume_db, pitch_scale, position] per sound.
func _played(audio: TableAudio) -> Array[Array]:
	var played: Array[Array] = []
	audio.sound_played.connect(func(sound: String, db: float, pitch: float, at: Vector3) -> void:
		played.append([sound, db, pitch, at]))
	return played


func _sounds(played: Array[Array]) -> Array[String]:
	var names: Array[String] = []
	for p in played:
		names.append(p[0])
	return names


func test_event_kinds_map_to_sounds() -> void:
	var audio := _audio()
	var played := _played(audio)
	# Ball 0 is in the top-left pocket's mouth, where the cushions' nose lines cross.
	var in_the_mouth := _state([{"x": 1.7845 + 0.01, "y": 0.889 + 0.01}, {"x": 0.6, "y": -0.4}])
	for kind in KINDS:
		audio.on_event(_event(kind, 1.0), in_the_mouth)
	for kind: String in ["transition", "nap", "guard"]:
		audio.on_event(_event(kind, 1.0), in_the_mouth)
	check_eq(_sounds(played), ["tip", "click", "cushion", "jaw", "pocket"] as Array[String], "sounds")
	audio.free()


func test_a_jaws_curve_is_the_cushion_until_the_ball_is_in_the_mouth() -> void:
	# Each jaw is one long arc that starts as the cushion's nose turning away. Along the left
	# middle's top jaw, a ball's radius out from it: where its centre is still on the table side
	# of the left cushion's nose line the hit sounds like the cushion, past it like the jaw.
	var audio := _audio()
	var played := _played(audio)
	var shape := Physics.table_shape()
	var arcs: Array = shape["arcs"]
	var jaw := -1
	var nose_y := NAN
	for i in arcs.size():
		if arcs[i]["name"] == "middle_left_jaw_top":
			jaw = i
	for line: Dictionary in shape["lines"]:
		if line["name"] == "left_top":
			nose_y = float(line["p1"][1])
	check(jaw >= 0 and not is_nan(nose_y), "the left middle's top jaw and the left nose are in the shape")
	if jaw < 0 or is_nan(nose_y):
		audio.free()
		return
	var centre := Vector2(float(arcs[jaw]["centre"][0]), float(arcs[jaw]["centre"][1]))
	var reach := float(arcs[jaw]["radius"]) + 0.02625
	var from := float(arcs[jaw]["angle_start"])
	var to := float(arcs[jaw]["angle_end"])
	var expected: Array[String] = []
	var behind := 0
	for i in 11:
		var at := centre + Vector2.from_angle(lerpf(from, to, i / 10.0)) * reach
		var in_the_mouth := at.y > nose_y
		behind += 1 if in_the_mouth else 0
		expected.append("jaw" if in_the_mouth else "cushion")
		audio.on_event(_event("ball_arc", 1.0, jaw), _state([{"x": at.x, "y": at.y}, {"x": 0.6, "y": -0.4}]))
	check_eq(_sounds(played), expected, "cushion along the curve, jaw in the mouth")
	check(behind > 0 and behind < 11, "the curve has both: %d of 11 in the mouth" % behind)
	# With no place for the ball, or no noses to tell by, a jaw's arc sounds like the jaw.
	check_eq(audio.sound_for(_event("ball_arc", 1.0, jaw)), "jaw", "no state")
	audio.use_table_shape({})
	check_eq(audio.sound_for(_event("ball_arc", 1.0, jaw), _two_balls()), "jaw", "no shape")
	audio.free()


func test_miscue_flag_is_honoured_for_one_strike() -> void:
	var audio := _audio()
	var played := _played(audio)
	audio.begin_shot({"miscue": true})
	audio.on_event(_event("ball_ball", 1.0), _two_balls())
	audio.on_event(_event("strike", 1.0), _two_balls())
	audio.on_event(_event("strike", 1.0), _two_balls())
	audio.begin_shot({"miscue": false})
	audio.on_event(_event("strike", 1.0), _two_balls())
	audio.begin_shot({})
	audio.on_event(_event("strike", 1.0), _two_balls())
	check_eq(_sounds(played), ["click", "miscue", "tip", "tip", "tip"] as Array[String],
			"a miscue strike, then plain strikes")
	audio.free()


func test_pocket_facing_sounds_like_the_jaw() -> void:
	var audio := _audio()
	var played := _played(audio)
	audio.use_table_shape({"lines": [{"name": "a", "kind": "nose"}, {"name": "b", "kind": "facing"}]})
	audio.on_event(_event("ball_line", 1.0, 0), _two_balls())
	audio.on_event(_event("ball_line", 1.0, 1), _two_balls())
	check_eq(_sounds(played), ["cushion", "jaw"] as Array[String], "nose then facing")
	audio.free()


func test_volume_never_falls_with_speed() -> void:
	for kind: String in KINDS + ["tip", "miscue", "click", "cushion", "jaw", "pocket"]:
		var previous := -INF
		var speed := 0.0
		while speed <= 15.0:
			var db := TableAudio.volume_db_for(kind, speed)
			check(db >= previous, "%s: %.2f dB at %.3f m/s after %.2f dB" % [kind, db, speed, previous])
			if speed > TableAudio.MIN_SPEED * 2.0:
				check(db > previous, "%s rises with speed at %.3f m/s" % [kind, speed])
			check(db <= 0.0, "%s stays below full scale at %.1f m/s" % [kind, speed])
			previous = db
			speed += 0.01 if speed < 0.5 else 0.25
	check(TableAudio.volume_db_for("ball_ball", 3.0) - TableAudio.volume_db_for("ball_ball", 0.1) > 15.0,
			"a hard click is much louder than a gentle one")
	check_eq(TableAudio.volume_db_for("transition", 5.0), TableAudio.SILENT_DB, "no level for a silent kind")


func test_pitch_rises_with_speed() -> void:
	check(TableAudio.pitch_for("ball_ball", 5.0) > TableAudio.pitch_for("ball_ball", 0.2), "pitch")


func test_nothing_below_the_threshold() -> void:
	var audio := _audio()
	var played := _played(audio)
	for kind: String in ["strike", "ball_ball", "ball_line", "ball_arc"]:
		audio.on_event(_event(kind, 0.019), _two_balls())
		audio.on_event(_event(kind, 0.0), _two_balls())
	check_eq(played.size(), 0, "resting contacts make no sound")
	audio.on_event(_event("pocket", 0.005), _two_balls())
	check_eq(_sounds(played), ["pocket"] as Array[String], "a ball creeping into a pocket still drops")
	audio.free()


func test_level_and_pitch_vary_around_the_model() -> void:
	var audio := _audio()
	var played := _played(audio)
	for i in 40:
		audio.stop_all()
		audio.on_event(_event("ball_ball", 1.0), _two_balls())
	var expected := TableAudio.volume_db_for("click", 1.0) + linear_to_db(float(Settings.get_value("volume")))
	var dbs := {}
	for p in played:
		check(absf(float(p[1]) - expected) <= TableAudio.RANDOM_DB + 1e-6, "within 1.5 dB: %f" % p[1])
		var pitch_ratio := float(p[2]) / TableAudio.pitch_for("click", 1.0)
		check(absf(pitch_ratio - 1.0) <= TableAudio.RANDOM_PITCH + 1e-6, "within 4%%: %f" % pitch_ratio)
		dbs[snappedf(float(p[1]), 0.01)] = true
	check(dbs.size() > 20, "repeats differ")
	audio.free()


func test_sounds_sit_where_they_happen() -> void:
	var audio := _audio()
	var played := _played(audio)
	audio.on_event(_event("ball_ball", 1.0), _two_balls())
	audio.on_event(_event("ball_line", 1.0), _two_balls())
	var r := float(OfsTable.dimensions().get("ball_radius", TableAudio.DEFAULT_BALL_RADIUS))
	var mid := TableSpace.core_to_godot(-0.2, -0.1, r)
	check((played[0][3] as Vector3).distance_to(mid) < 1e-5, "a click between the two balls: %s" % played[0][3])
	check((played[1][3] as Vector3).distance_to(TableSpace.core_to_godot(-1.0, 0.2, r)) < 1e-5, "a cushion hit at ball a")
	audio.free()


func test_voices_are_capped_and_the_quietest_stolen() -> void:
	var audio := _audio()
	var played := _played(audio)
	# Cushion sounds last about 200 ms, far longer than this test takes.
	for i in TableAudio.VOICES:
		audio.on_event(_event("ball_line", 0.5 + 0.1 * i), _two_balls())
	check_eq(audio.active_voices(), TableAudio.VOICES, "every voice busy")
	var voices: Array[AudioStreamPlayer3D] = []
	for child in audio.get_children():
		if child is AudioStreamPlayer3D and child.name.begins_with("Voice"):
			voices.append(child)
	check_eq(voices.size(), TableAudio.VOICES, "pool size")
	var quietest := voices[0]
	for v in voices:
		if v.volume_db < quietest.volume_db:
			quietest = v
	audio.on_event(_event("ball_line", 0.05), _two_balls())
	check_eq(played.size(), TableAudio.VOICES, "a sound quieter than every voice is dropped")
	audio.on_event(_event("ball_line", 4.0), _two_balls())
	check_eq(played.size(), TableAudio.VOICES + 1, "a loud one gets a voice")
	check_eq(audio.active_voices(), TableAudio.VOICES, "still capped")
	check_near(quietest.volume_db, float(played[-1][1]), 1e-4, "it took the quietest voice")
	audio.stop_all()
	check_eq(audio.active_voices(), 0, "stop_all frees every voice")
	audio.free()


func test_settings_volume_scales_and_zero_mutes() -> void:
	var audio := _audio()
	var played := _played(audio)
	var before: Variant = Settings.get_value("volume")
	Settings.set_value("volume", 1.0)
	audio.rng.seed = 5
	audio.on_event(_event("pocket", 1.0), _two_balls())
	Settings.set_value("volume", 0.5)
	audio.rng.seed = 5
	audio.on_event(_event("pocket", 1.0), _two_balls())
	Settings.set_value("volume", 0.0)
	audio.on_event(_event("pocket", 1.0), _two_balls())
	Settings.set_value("volume", before)
	check_eq(played.size(), 2, "nothing at volume 0")
	if played.size() == 2:
		check_near(float(played[0][1]) - float(played[1][1]), 6.0206, 1e-3, "half volume is 6 dB down")
	check_eq(AudioServer.get_bus_volume_db(AudioServer.get_bus_index(&"Master")), 0.0, "Master bus left alone")
	audio.free()


func test_roll_follows_the_fastest_ball() -> void:
	var audio := _audio()
	var volume_db := linear_to_db(float(Settings.get_value("volume")))
	audio.on_frame(_state([
			{"x": 0.0, "y": 0.0, "vx": 0.5, "motion": ShotPlayback.ROLLING},
			{"x": 1.0, "y": 0.3, "vx": 1.2, "vy": -1.6, "motion": ShotPlayback.SLIDING},
			{"x": 0.2, "y": 0.1, "vx": 5.0, "motion": ShotPlayback.POCKETED}]), 0.1, 0.01)
	check_near(audio.roll_level_db(), TableAudio.roll_db_for(2.0) + volume_db, 1e-6, "level of the 2 m/s ball")
	var roll := audio.get_node("Roll") as AudioStreamPlayer3D
	var r := float(OfsTable.dimensions().get("ball_radius", TableAudio.DEFAULT_BALL_RADIUS))
	check(roll.position.distance_to(TableSpace.core_to_godot(1.0, 0.3, r)) < 1e-5, "at the fastest ball")
	audio.on_frame(_state([{"x": 0.0, "vx": 0.01, "motion": ShotPlayback.ROLLING},
			{"x": 1.0, "wz": 3.0, "motion": ShotPlayback.SPINNING}]), 2.0, 0.01)
	check_eq(audio.roll_level_db(), TableAudio.SILENT_DB, "silent when everything rests")
	check(TableAudio.roll_db_for(12.0) <= TableAudio.ROLL_MAX_DB, "faint even at break-off speed")
	check(TableAudio.roll_db_for(1.0) < TableAudio.roll_db_for(2.0), "louder when faster")
	audio.free()


func test_processes_only_while_the_roll_sounds() -> void:
	var audio := _audio()
	await tree.process_frame
	check(not audio.is_processing(), "idle at the start")
	audio.on_event(_event("ball_ball", 2.0), _two_balls())
	check(not audio.is_processing(), "a click plays without the frame loop")
	audio.on_frame(_state([{"x": 0.0, "vx": 1.0, "motion": ShotPlayback.ROLLING}]), 0.1, 0.01)
	check(audio.is_processing(), "on while a ball rolls")
	audio.on_frame(_state([{"x": 0.0, "motion": ShotPlayback.STATIONARY}]), 0.2, 0.01)
	for i in 40:
		audio._process(0.02)
	check(not audio.is_processing(), "off once the roll has faded")
	audio.on_frame(_state([{"x": 0.0, "vx": 1.0, "motion": ShotPlayback.ROLLING}]), 0.3, 0.01)
	audio.stop_all()
	check(not audio.is_processing(), "and after stop_all")
	audio.free()


func test_every_sound_loads() -> void:
	var files: Array[String] = [TableAudio.ROLL_SOUND]
	for sound: String in TableAudio.SOUNDS:
		for file: String in TableAudio.SOUNDS[sound]:
			files.append(file)
	check_eq(files.size(), 17, "17 sounds")
	for file in files:
		var stream := load("res://audio/%s.wav" % file) as AudioStreamWAV
		check(stream != null, "%s loads as AudioStreamWAV" % file)
		if stream == null:
			continue
		check(stream.get_length() > 0.01, "%s has length" % file)
		check_eq(stream.mix_rate, 44100, "%s rate" % file)
		check(not stream.stereo, "%s is mono" % file)
		if file != TableAudio.ROLL_SOUND:
			check_eq(stream.loop_mode, AudioStreamWAV.LOOP_DISABLED, "%s plays once" % file)


func test_roll_loop_loops() -> void:
	var stream := load("res://audio/roll_loop.wav") as AudioStreamWAV
	check(stream != null, "roll_loop loads")
	if stream != null:
		check_eq(stream.loop_mode, AudioStreamWAV.LOOP_FORWARD, "loops forward")
		check_eq(stream.loop_begin, 0, "from the start")


func test_a_missing_sound_logs_once() -> void:
	var name := "no_such_sound_%d" % randi()
	check(TableAudio._stream(name) == null, "missing sound is null")
	check(TableAudio._stream(name) == null, "still null")
	OfsLog.flush()
	var count := 0
	for line in FileAccess.get_file_as_string(OfsLog.file_path()).split("\n", false):
		if line.contains("\"audio.error\"") and line.contains(name):
			count += 1
	check_eq(count, 1, "audio.error logged once")


func test_bad_ball_index_logs_and_still_plays() -> void:
	var audio := _audio()
	var played := _played(audio)
	var e := _event("ball_line", 1.0)
	e["a"] = 40
	audio.on_event(e, _two_balls())
	check_eq(played.size(), 1, "played anyway")
	OfsLog.flush()
	check(FileAccess.get_file_as_string(OfsLog.file_path()).contains("names ball 40"), "logged")
	audio.free()
