extends TestCase
## The shot cycle (spec 8.5): the camera sequence chosen at the strike, the pocket camera's pose,
## the timing from the cut to the swoop back, the break and what the aim turns to next, and the
## points floating out of the pocket.

const HudScript := preload("res://scripts/hud.gd")
const CANVAS := Vector2(1912.0, 880.0)
const PHONE_SCREEN := Rect2(0, 0, 2868, 1320)
const PHONE_SAFE_AREAS: Array[Rect2] = [
	Rect2(186, 0, 2868 - 372, 1320 - 63),
	Rect2(186, 0, 2868 - 186, 1320 - 63),
	Rect2(0, 0, 2868 - 186, 1320 - 63),
]
const DT := 1.0 / 60.0


## A shot scripted ball by ball: each listed ball runs in a straight line from `from` at t0 to `to`
## at t1, and stays put otherwise.
class ScriptedPlayback extends ShotPlayback:
	var scripted_events: Array[Dictionary] = []
	## index -> [from: Vector2, to: Vector2, t0, t1]
	var runs := {}
	var balls := 3

	func duration() -> float:
		return 5.0

	func ball_count() -> int:
		return balls

	func events() -> Array[Dictionary]:
		return scripted_events

	func state_at(t: float) -> PackedFloat64Array:
		var state := PackedFloat64Array()
		state.resize(balls * STRIDE)
		for i in balls:
			var run: Array = runs.get(i, [Vector2.ZERO, Vector2.ZERO, 0.0, 1.0])
			var f := clampf((t - float(run[2])) / (float(run[3]) - float(run[2])), 0.0, 1.0)
			var xy := (run[0] as Vector2).lerp(run[1], f)
			state[i * STRIDE + X] = xy.x
			state[i * STRIDE + Y] = xy.y
			state[i * STRIDE + POCKET] = -1.0
		return state


func _balls() -> Array[Dictionary]:
	return [{"ball": "cue", "index": 0, "x": -1.0, "y": 0.0}, {"ball": "red", "index": 3, "x": 1.0, "y": 0.3},
			{"ball": "black", "index": 0, "x": 1.4605, "y": 0.0}]


func _pockets() -> Array:
	return Physics.table_shape()["pockets"]


func _pocket(pocket_name: String) -> int:
	var pockets := _pockets()
	for i in pockets.size():
		if pockets[i]["name"] == pocket_name:
			return i
	failures.append("no pocket %s" % pocket_name)
	return 0


func _centre(index: int) -> Vector2:
	if index < 0 or index >= _pockets().size():
		return Vector2(1.8, 0.9)
	var c: Array = _pockets()[index]["drop_centre"]
	return Vector2(float(c[0]), float(c[1]))


## The red (ball 1) hit at contact_t and dropping into `pocket` at drop_t from (1.0, 0.3).
func _pot(pocket: int, contact_t: float, drop_t: float) -> ScriptedPlayback:
	var playback := ScriptedPlayback.new()
	playback.scripted_events = [{"t": 0.0, "kind": "strike", "a": 0, "b": -1, "body": -1},
			{"t": contact_t, "kind": "ball_ball", "a": 0, "b": 1, "body": -1},
			{"t": drop_t, "kind": "pocket", "a": 1, "b": -1, "body": pocket}]
	playback.runs[1] = [Vector2(1.0, 0.3), _centre(pocket), contact_t, drop_t]
	return playback


## in_tree false keeps it out of the tree, where it frames the phone's 1912 x 880 canvas whatever
## the test window is.
func _rig(in_tree: bool = true) -> CameraRig:
	var rig := CameraRig.new()
	var cam := Camera3D.new()
	cam.name = "Camera3D"
	rig.add_child(cam)
	if in_tree:
		tree.root.add_child(rig)
	var table := TableView.new()
	tree.root.add_child(table)
	table.build(Physics.table_shape(), OfsTable.dimensions())
	rig.set_table_bounds(table.bounds())
	table.free()
	return rig


func test_an_object_ball_dropping_early_gets_the_pocket_camera() -> void:
	var top_left := _pocket("top_left")
	var playback := _pot(top_left, 0.4, 1.6)
	var plan := ShotCycle.plan(playback, _balls(), 0, true, _pockets())
	check_eq(plan["sequence"], "pocket", "the pocket sequence")
	check_eq(plan["reason"], "", "no reason against it")
	check_eq(plan["ball"], "red 3", "the red's label")
	check_eq(plan["index"], 1, "its place in the list")
	check_eq(plan["pocket"], top_left, "into the top left")
	check_near(plan["drop_t"], 1.6, 1e-12, "at its drop")
	check_eq(plan["middle"], false, "a corner")
	check((plan["drop_centre"] as Vector2).is_equal_approx(_centre(top_left)), "the pocket's drop centre")
	var expected := Vector2(1.0, 0.3).lerp(_centre(top_left), (1.1 - 0.4) / 1.2)
	check((plan["from"] as Vector2).distance_to(expected) < 1e-9, "where it was half a second before: %s" % plan["from"])
	check_eq(plan["pots"], [{"index": 1, "pocket": top_left, "t": 1.6, "ball": "red", "label": "red 3", "value": 1}],
			"the pots")
	check_eq(plan["cue_potted"], false, "the cue ball stayed up")


func test_the_way_in_starts_at_the_first_hit_when_that_is_later() -> void:
	var middle := _pocket("middle_left")
	var playback := _pot(middle, 1.4, 1.6)
	var plan := ShotCycle.plan(playback, _balls(), 0, true, _pockets())
	check_eq(plan["middle"], true, "a middle pocket")
	check((plan["from"] as Vector2).distance_to(Vector2(1.0, 0.3)) < 1e-9, "where it was hit: %s" % plan["from"])


func test_each_reason_for_the_broadcast_view() -> void:
	var pocket := _pocket("bottom_right")
	var quiet := ScriptedPlayback.new()
	quiet.scripted_events = [{"t": 0.0, "kind": "strike", "a": 0, "b": -1, "body": -1}]
	var plan := ShotCycle.plan(quiet, _balls(), 0, true, _pockets())
	check_eq([plan["sequence"], plan["reason"]], ["broadcast", "no pot"], "nothing potted")
	check(not plan.has("ball") and not plan.has("drop_t"), "no drop to tell of")

	var in_off := ScriptedPlayback.new()
	in_off.scripted_events = [{"t": 0.0, "kind": "strike", "a": 0, "b": -1, "body": -1},
			{"t": 1.2, "kind": "pocket", "a": 0, "b": -1, "body": pocket}]
	plan = ShotCycle.plan(in_off, _balls(), 0, true, _pockets())
	check_eq([plan["sequence"], plan["reason"], plan["ball"], plan["pocket"]], ["broadcast", "cue ball only", "cue", pocket],
			"only the cue ball")
	check_eq(plan["cue_potted"], true, "the cue ball potted")

	plan = ShotCycle.plan(_pot(pocket, 1.0, ShotCycle.POCKET_CAM_MAX_T + 0.1), _balls(), 0, true, _pockets())
	check_eq([plan["sequence"], plan["reason"], plan["ball"]], ["broadcast", "late pot", "red 3"], "too late")

	plan = ShotCycle.plan(_pot(pocket, 0.5, ShotCycle.POCKET_CAM_MAX_T - 0.1), _balls(), 0, true, _pockets())
	check_eq(plan["sequence"], "pocket", "just in time")

	plan = ShotCycle.plan(_pot(pocket, 0.5, 1.0), _balls(), 0, false, _pockets())
	check_eq([plan["sequence"], plan["reason"]], ["broadcast", "setting off"], "the setting off")

	plan = ShotCycle.plan(_pot(7, 0.5, 1.0), _balls(), 0, true, _pockets())
	check_eq([plan["sequence"], plan["reason"]], ["broadcast", "no pocket"], "a pocket the table doesn't have")
	check(String(plan.get("problem", "")).contains("pocket 7"), "says which: %s" % plan.get("problem"))


func test_the_first_object_ball_is_the_one_watched_and_every_pot_scores() -> void:
	var playback := _pot(_pocket("top_left"), 0.4, 1.6)
	playback.scripted_events.insert(2, {"t": 0.9, "kind": "pocket", "a": 0, "b": -1, "body": _pocket("middle_left")})
	playback.scripted_events.append({"t": 2.5, "kind": "pocket", "a": 2, "b": -1, "body": _pocket("top_right")})
	var plan := ShotCycle.plan(playback, _balls(), 0, true, _pockets())
	check_eq([plan["sequence"], plan["ball"]], ["pocket", "red 3"], "the red, not the cue ball that went first")
	check_eq((plan["pots"] as Array).map(func(p: Dictionary) -> String: return p["label"]), ["red 3", "black"], "both in order")
	check_eq(plan["cue_potted"], true, "and the cue ball in off")


func test_the_pocket_camera_frames_the_pocket_and_the_ball_coming_in() -> void:
	var rig := _rig(false)
	var half := Vector2(float(OfsTable.dimensions()["playing_length"]), float(OfsTable.dimensions()["playing_width"])) / 2.0
	var r := float(OfsTable.dimensions()["ball_radius"])
	for pocket: Dictionary in _pockets():
		var c: Array = pocket["drop_centre"]
		var centre := Vector2(float(c[0]), float(c[1]))
		var middle := not bool(pocket["corner"])
		# Middle pockets take balls up to about 45 degrees off square; corners anything from along one
		# cushion round to along the other.
		var ways_in: Array[Vector2] = []
		if middle:
			for off_deg: float in [-40.0, -20.0, 0.0, 20.0, 40.0]:
				ways_in.append(Vector2(0.0, -signf(centre.y)).rotated(deg_to_rad(off_deg)))
		else:
			var along_long := Vector2(-signf(centre.x), 0.0)
			var along_end := Vector2(0.0, -signf(centre.y))
			for f: float in [0.08, 0.3, 0.5, 0.7, 0.92]:
				ways_in.append(along_long.slerp(along_end, f))
		for way: Vector2 in ways_in:
			for run: float in [0.6, 1.5]:
				var from := centre + way * run
				var what := "%s from %s" % [pocket["name"], from]
				var pose := rig.pocket_pose(centre, from, middle)
				var eye_xy := TableSpace.godot_to_core(pose.origin)
				check(absf(eye_xy.x) > half.x or absf(eye_xy.y) > half.y, "%s: outside the cushions at %s" % [what, eye_xy])
				check(pose.origin.y >= CameraRig.POCKET_HEIGHT_M - 1e-6, "%s: above the bed, %.2f m" % [what, pose.origin.y])
				check(rig.in_room(pose.origin), "%s: in the room at %s" % [what, pose.origin])
				check((-pose.basis.z).y < 0.0, "%s: looking down" % what)
				var at_pocket := rig.project(pose, TableSpace.core_to_godot(centre.x, centre.y), CameraRig.POCKET_FOV_DEG)
				check(Rect2(Vector2.ZERO, CANVAS).has_point(at_pocket) and at_pocket.y > CANVAS.y * 0.6,
						"%s: the pocket low in the frame at %s" % [what, at_pocket])
				var at_ball := rig.project(pose, TableSpace.core_to_godot(from.x, from.y, r), CameraRig.POCKET_FOV_DEG)
				check((pose.affine_inverse() * TableSpace.core_to_godot(from.x, from.y, r)).z < 0.0
						and Rect2(Vector2.ZERO, CANVAS).has_point(at_ball) and at_ball.y < at_pocket.y,
						"%s: the ball coming in above it at %s" % [what, at_ball])
	# A ball nudged in from the jaws, and one that came round off a jaw from outside, still give a pose.
	var top_left := _centre(_pocket("top_left"))
	for from: Vector2 in [top_left, top_left + Vector2(0.3, 0.3)]:
		var pose := rig.pocket_pose(top_left, from, false)
		var eye_xy := TableSpace.godot_to_core(pose.origin)
		check(absf(eye_xy.x) > half.x or absf(eye_xy.y) > half.y, "from %s: outside the cushions at %s" % [from, eye_xy])
		check(pose.origin.is_finite() and rig.in_room(pose.origin), "from %s: a pose in the room" % from)
	rig.free()


func test_cut_hold_rise_and_settle_on_time() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.1), 0.3)
	var cycle := ShotCycle.new(rig)
	var top_left := _pocket("top_left")
	var plan := ShotCycle.plan(_pot(top_left, 0.4, 1.6), _balls(), 0, true, _pockets())
	var pose := rig.pocket_pose(plan["drop_centre"], plan["from"], plan["middle"])
	var moves := [0]
	rig.moved.connect(func() -> void: moves[0] += 1)
	var behind := rig.transform
	cycle.start(plan, pose)
	check(rig.is_held() and rig.view_name() == "shot", "held for the shot")
	check(rig.transform.is_equal_approx(behind) and not rig.is_easing(), "still behind the cue ball for the stroke")
	cycle.update(ShotCycle.CUT_T - 0.01)
	check(rig.transform.is_equal_approx(behind) and moves[0] == 0, "still there just before the cut")
	cycle.update(ShotCycle.CUT_T + 0.01)
	check_eq(rig.view_name(), "pocket", "cut to the pocket")
	check(rig.transform.is_equal_approx(pose) and not rig.is_easing(), "at once, no blend")
	check_eq(moves[0], 1, "moved fired once, so the shadows refit")
	check_eq(rig.camera().fov, CameraRig.POCKET_FOV_DEG, "on the pocket camera's lens")
	check(not Perf._active.has(&"camera"), "a still camera keeps nothing drawing")
	check(not cycle.settle_ready(1.0), "no settle at the pocket")
	cycle.update(1.6 + ShotCycle.HOLD_AFTER_DROP_S - 0.01)
	check_eq(rig.view_name(), "pocket", "held until 0.7 s after the drop")
	var rise_at := 1.6 + ShotCycle.HOLD_AFTER_DROP_S + 0.01
	cycle.update(rise_at)
	check(rig.view_name() == "shot" and rig.is_easing(), "moving up to the TV view")
	var largest := 0.0
	var travelled := 0.0
	var turned := 0.0
	var frames := 0
	var last := rig.transform
	while rig.is_easing() and frames < 200:
		rig._process(DT)
		frames += 1
		var step := rig.transform.origin.distance_to(last.origin)
		largest = maxf(largest, step)
		travelled += step
		turned = maxf(turned, rad_to_deg((-rig.transform.basis.z).angle_to(-last.basis.z)))
		last = rig.transform
	check(absf(frames - ShotCycle.RISE_S * 60.0) <= 2.0, "smoothly, over RISE_S: %d frames" % frames)
	# From the far corner round to the TV camera at baulk is a long way in a second: smooth means no
	# frame much over the smoothstep's peak, and the view turning no faster than the TV blend does.
	check(largest < 1.75 * travelled / frames and turned < 3.5,
			"no jump: at most %.3f m (%.3f on average) and %.2f° a frame" % [largest, travelled / frames, turned])
	check(rig.transform.is_equal_approx(rig.broadcast_pose(CameraRig.end_behind(0.3))), "at the TV view behind the shot")
	check_eq(rig.camera().fov, CameraRig.BROADCAST_FOV_DEG, "on its long lens")
	check(not cycle.settle_ready(rise_at + ShotCycle.RISE_S + ShotCycle.MIN_TV_HOLD_S - 0.01), "the TV view holds a moment")
	check(cycle.settle_ready(rise_at + ShotCycle.RISE_S + ShotCycle.MIN_TV_HOLD_S + 0.01), "then the settle may come")
	rig.free()


func test_a_shot_with_no_pot_rises_at_the_strike() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.1), 0.3)
	var cycle := ShotCycle.new(rig)
	cycle.start({"sequence": "broadcast", "reason": "no pot", "pots": [], "cue_potted": false})
	check(rig.view_name() == "shot" and rig.is_easing(), "rising to the TV view at once")
	cycle.update(0.5)
	check(rig.view_name() == "shot", "no pocket camera")
	check(not cycle.settle_ready(CameraRig.BLEND_S + ShotCycle.MIN_TV_HOLD_S - 0.01), "not before the TV view has held")
	check(cycle.settle_ready(CameraRig.BLEND_S + ShotCycle.MIN_TV_HOLD_S + 0.01), "the settle once it has")
	rig.free()


func test_a_drag_at_the_pocket_goes_up_to_walk_round_and_a_skip_cancels() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.1), 0.3)
	var cycle := ShotCycle.new(rig)
	var plan := ShotCycle.plan(_pot(_pocket("top_left"), 0.4, 1.6), _balls(), 0, true, _pockets())
	cycle.start(plan, rig.pocket_pose(plan["drop_centre"], plan["from"], false))
	cycle.update(0.5)
	check_eq(rig.view_name(), "pocket", "at the pocket")
	check(cycle.walk_round(0.6), "a drag goes up")
	check(rig.is_broadcast() and rig.is_easing(), "to the TV view, where a drag walks round")
	check(not cycle.walk_round(0.7), "only once")
	cycle.update(1.6 + ShotCycle.HOLD_AFTER_DROP_S + 0.1)
	check(rig.is_broadcast(), "no second rise at the drop")
	check(cycle.settle_ready(0.6 + ShotCycle.RISE_S + ShotCycle.MIN_TV_HOLD_S + 0.01), "settles after the TV view held")
	cycle.start(plan, rig.pocket_pose(plan["drop_centre"], plan["from"], false))
	cycle.cancel()
	check(cycle.settle_ready(0.0), "a skip settles at once")
	cycle.update(1.0)
	check(rig.view_name() != "pocket", "and cuts to nothing")
	rig.free()


func test_the_swoop_back_lands_on_the_new_aim() -> void:
	var rig := _rig(false)
	var r := float(OfsTable.dimensions()["ball_radius"])
	for entry: Array in [[Vector2(-1.0, 0.1), 0.3, Vector2(0.4, 0.5), 2.6], [Vector2(0.5, -0.3), 2.8, Vector2(1.2, 0.6), -0.4],
			[Vector2(0.0, 0.0), 0.0, Vector2(-1.4, 0.7), -PI / 2.0]]:
		var cue: Vector2 = entry[2]
		var aim: float = entry[3]
		rig.follow(entry[0], entry[1])
		rig._process(CameraRig.BLEND_S)
		rig.hold(true)
		rig._process(CameraRig.BLEND_S)
		# The aim is set before the camera moves.
		rig.follow(cue, aim)
		rig.reset_view()
		rig.hold(false)
		var what := "to %s aim %.2f" % [cue, aim]
		check(rig.view_name() == "aim" and rig.is_easing(), "%s: swooping" % what)
		var frames := 0
		var off: Array[String] = []
		while rig.is_easing() and frames < 200:
			rig._process(DT)
			frames += 1
			var at := rig.project(rig.transform, TableSpace.core_to_godot(cue.x, cue.y, r), rig.camera().fov)
			if not Rect2(Vector2.ZERO, CANVAS).has_point(at):
				off.append("frame %d at %s" % [frames, at])
		check(absf(frames - CameraRig.RETURN_S * 60.0) <= 2.0, "%s: RETURN_S long, %d frames" % [what, frames])
		check_eq(off, [] as Array[String], "%s: the cue ball in view all the way down" % what)
		check(rig.transform.is_equal_approx(rig.pose_for(cue, aim)), "%s: exactly behind the cue ball on the aim" % what)
		check_eq(rig.camera().fov, CameraRig.FOV_DEG, "%s: on the aiming lens" % what)
	check(not Perf._active.has(&"camera"), "the camera lets the game idle once there")
	rig.free()


func test_prefer_a_colour_after_a_red_and_a_red_while_any_remain() -> void:
	var cycle := ShotCycle.new()
	var with_reds := _balls()
	var no_reds: Array[Dictionary] = [_balls()[0], _balls()[2]]
	check_eq(cycle.prefer(with_reds), "red", "nothing potted in the break: a red")
	check_eq(cycle.prefer(no_reds), "colour", "nothing potted and no reds: a colour")
	cycle.last_potted = "red"
	check_eq(cycle.prefer(with_reds), "colour", "a colour after a red")
	cycle.last_potted = "colour"
	check_eq(cycle.prefer(with_reds), "red", "a red after a colour")
	check_eq(cycle.prefer(no_reds), "colour", "a colour after a colour once the reds are gone")


func _shot(cycle: ShotCycle, pots: Array, cue_potted: bool) -> Dictionary:
	var listed: Array[Dictionary] = []
	for pot: Array in pots:
		listed.append({"ball": pot[0], "label": pot[1], "value": ShotCycle.VALUES[pot[0]]})
	cycle.start({"sequence": "broadcast", "reason": "", "pots": listed, "cue_potted": cue_potted})
	for pot in listed:
		cycle.count(int(pot["value"]))
	return cycle.end_shot()


func test_the_break_runs_on_until_a_miss_or_an_in_off() -> void:
	var cycle := ShotCycle.new()
	check_eq(_shot(cycle, [["red", "red 3"]], false), {"potted": ["red 3"], "points": 1, "break": 1}, "a red")
	check_eq(cycle.last_potted, "red", "a red last")
	check_eq(_shot(cycle, [["black", "black"]], false), {"potted": ["black"], "points": 7, "break": 8}, "the black")
	check_eq(cycle.prefer(_balls()), "red", "a red next")
	check_eq(_shot(cycle, [["red", "red 1"], ["red", "red 2"]], false)["break"], 10, "two reds")
	check_eq(cycle.shown_break, 10, "shown")
	cycle.start({"sequence": "pocket", "pots": [{"ball": "blue", "label": "blue", "value": 5}], "cue_potted": true})
	cycle.count(5)
	check_eq(cycle.shown_break, 15, "the in-off's pot counts up while it plays")
	check_eq(cycle.end_shot(), {"potted": ["blue", "cue"], "points": 5, "break": 0}, "then the in-off ends the break")
	check_eq([cycle.shown_break, cycle.last_potted], [0, ""], "nothing shown, nothing potted")
	check_eq(_shot(cycle, [["pink", "pink"]], false)["break"], 6, "a new break")
	check_eq(_shot(cycle, [], false), {"potted": [], "points": 0, "break": 0}, "a miss ends it")
	_shot(cycle, [["green", "green"]], false)
	cycle.reset_break()
	check_eq([cycle.break_total, cycle.shown_break, cycle.last_potted], [0, 0, ""], "reset")


func test_points_rise_out_of_the_pocket_and_fade() -> void:
	var camera := Camera3D.new()
	tree.root.add_child(camera)
	camera.current = true
	camera.position = Vector3(0.0, 0.4, -2.6)
	var popups := PotPopups.new()
	tree.root.add_child(popups)
	popups.pop(7, Vector2(1.8156, 0.9201))
	check_eq(popups.count(), 1, "one popup")
	check(Perf._active.has(&"pot_popup"), "it keeps the game drawing")
	var label := popups.get_child(0) as Label3D
	check_eq(label.text, "7", "the black's 7")
	var start := label.position
	check(start.distance_to(TableSpace.core_to_godot(1.8156, 0.9201, PotPopups.START_HEIGHT_M)) < 1e-5, "out of the pocket")
	check(label.no_depth_test and label.billboard == BaseMaterial3D.BILLBOARD_ENABLED, "seen over the fittings, facing the camera")
	popups._process(PotPopups.LIFE_S * 0.5)
	check(label.position.y > start.y + PotPopups.RISE_M * 0.5, "rising: %.3f" % (label.position.y - start.y))
	check(label.modulate.a < 1.0 and label.modulate.a > 0.3, "fading: %.2f" % label.modulate.a)
	popups._process(PotPopups.LIFE_S * 0.49)
	check_eq(popups.count(), 1, "still there just before LIFE_S")
	popups._process(PotPopups.LIFE_S * 0.02)
	check_eq(popups.count(), 0, "gone after LIFE_S")
	check(not Perf._active.has(&"pot_popup"), "and the game can idle")
	popups.pop(1, Vector2.ZERO)
	popups.pop(2, Vector2.ZERO)
	popups.clear()
	check(popups.count() == 0 and not Perf._active.has(&"pot_popup"), "clear ends them all")
	popups.free()
	camera.free()


func test_the_break_pill_sits_clear_of_the_controls_and_the_tv_views_pockets() -> void:
	var probe := FreeTableBar.new()
	tree.root.add_child(probe)
	var row := probe.row_rect().size
	probe.free()
	var safes: Array[Rect2] = [Rect2(Vector2.ZERO, CANVAS)]
	for safe_px in PHONE_SAFE_AREAS:
		safes.append(HudScript.safe_rect(safe_px, PHONE_SCREEN, CANVAS))
	var rig := CameraRig.new()
	var cam := Camera3D.new()
	cam.name = "Camera3D"
	rig.add_child(cam)
	for safe in safes:
		var rects: Dictionary = HudScript.layout(safe, row, CANVAS)
		var band: Rect2 = rects["break"]
		check(safe.encloses(band), "%s: the pill's band inside the safe area %s" % [band, safe])
		for key: String in ["spin", "cue", "wheel", "elevation", "bar"]:
			check(not band.intersects(rects[key]), "%s: clear of the %s %s" % [band, key, rects[key]])
		# A long break's pill, centred as main.gd centres it.
		var pill := Rect2(Vector2(safe.get_center().x - 110.0, band.position.y), Vector2(220.0, band.size.y))
		check(band.encloses(pill), "a wide pill %s fits the band %s" % [pill, band])
		var controls: Array[Rect2] = []
		for key: String in ["bar", "spin", "elevation", "cue", "wheel"]:
			controls.append(rects[key])
		rig.keep_clear(controls)
		for yaw: float in [0.0, PI]:
			var pose := rig.broadcast_pose(yaw)
			for pocket: Dictionary in _pockets():
				var c: Array = pocket["drop_centre"]
				var at := rig.project(pose, TableSpace.core_to_godot(float(c[0]), float(c[1])), CameraRig.BROADCAST_FOV_DEG)
				check(not pill.grow(CameraRig.CLEAR_MARGIN).has_point(at), "the TV view's %s pocket at %s clear of the pill %s"
						% [pocket["name"], at, pill])
	rig.free()
