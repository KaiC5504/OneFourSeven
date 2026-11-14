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
	var track: PackedVector2Array = plan["track"]
	check_eq(track.size(), ShotCycle.TRACK_POINTS, "its run for the pocket camera to keep in frame")
	check(track[0].distance_to(_centre(top_left)) < 1e-9 and track[-1].distance_to(Vector2(1.0, 0.3)) < 1e-9,
			"from the drop back to where it stood at the cut: %s" % track)
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


## The red (ball 1) hit at contact_t and running from (1.0, 0.3) towards `pocket`, stopping at
## arrive_t with its centre `short_m` outside the drop circle. Nothing drops.
func _miss(pocket: int, contact_t: float, arrive_t: float, short_m: float) -> ScriptedPlayback:
	var playback := ScriptedPlayback.new()
	playback.scripted_events = [{"t": 0.0, "kind": "strike", "a": 0, "b": -1, "body": -1},
			{"t": contact_t, "kind": "ball_ball", "a": 0, "b": 1, "body": -1}]
	var from := Vector2(1.0, 0.3)
	var centre := _centre(pocket)
	var stop := centre + (from - centre).normalized() * (float(_pockets()[pocket]["drop_radius"]) + short_m)
	playback.runs[1] = [from, stop, contact_t, arrive_t]
	return playback


func test_a_ball_played_at_a_pocket_that_stays_out_gets_the_same_pocket_camera() -> void:
	# KaiC: the cut must not tell him whether the pot goes in.
	var top_left := _pocket("top_left")
	var plan := ShotCycle.plan(_miss(top_left, 0.4, 1.6, 0.06), _balls(), 0, true, _pockets())
	check_eq([plan["sequence"], plan["reason"]], ["pocket", "near miss"], "the pocket camera, for a miss")
	check_eq([plan["ball"], plan["index"], plan["pocket"]], ["red 3", 1, top_left], "the struck red, at the pocket it was played at")
	check(not plan.has("drop_t") and (plan["pots"] as Array).is_empty() and not plan["cue_potted"], "nothing dropped")
	check_near(plan["near_t"], 1.6, ShotCycle.ATTEMPT_STEP_S, "timed from when it is nearest the pocket")
	check((plan["drop_centre"] as Vector2).is_equal_approx(_centre(top_left)) and not plan["middle"], "that pocket's camera")
	check((plan["from"] as Vector2).distance_to(_centre(top_left)) > 0.1 and (plan["track"] as PackedVector2Array).size() == ShotCycle.TRACK_POINTS,
			"with the ball's way in to frame")
	var potted := ShotCycle.plan(_pot(top_left, 0.4, 1.6), _balls(), 0, true, _pockets())
	check_eq(ShotCycle.cut_time(plan), ShotCycle.cut_time(potted), "cut to at the same moment as a pot")
	check_near(ShotCycle.pocket_leave_time(plan), ShotCycle.pocket_leave_time(potted), ShotCycle.ATTEMPT_STEP_S,
			"and left as long after the ball arrives")

	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.1), 0.3)
	var cycle := ShotCycle.new(rig)
	var pose := rig.pocket_pose(plan["drop_centre"], plan["from"], plan["middle"], plan["track"])
	cycle.start(plan, pose)
	cycle.update(ShotCycle.CUT_T + 0.001)
	check(rig.view_name() == "pocket" and rig.transform.is_equal_approx(pose), "the cut goes to the pocket")
	cycle.update(float(plan["near_t"]) + ShotCycle.HOLD_AFTER_DROP_S - 0.02)
	check_eq(rig.view_name(), "pocket", "held while the ball comes up short")
	cycle.update(float(plan["near_t"]) + ShotCycle.HOLD_AFTER_DROP_S + 0.02)
	check(rig.view_name() == "shot" and rig.is_broadcast(), "then the full table")
	check_eq(cycle.end_shot()["break"], 0, "and no break from it")
	rig.free()

	var middle := _pocket("middle_left")
	plan = ShotCycle.plan(_miss(middle, 0.3, 1.0, 0.15), _balls(), 0, true, _pockets())
	check_eq([plan["sequence"], plan["pocket"], plan["middle"]], ["pocket", middle, true], "a middle pocket's too")

	var in_off := _miss(top_left, 0.4, 1.6, 0.06)
	in_off.scripted_events.append({"t": 2.5, "kind": "pocket", "a": 0, "b": -1, "body": _pocket("top_right")})
	plan = ShotCycle.plan(in_off, _balls(), 0, true, _pockets())
	check_eq([plan["sequence"], plan["ball"], plan["cue_potted"]], ["pocket", "red 3", true], "an in-off after it still shows the attempt")


func test_a_shot_not_played_at_a_pocket_still_cuts_to_the_full_table() -> void:
	var top_left := _pocket("top_left")
	var wide := ShotCycle.plan(_miss(top_left, 0.4, 1.6, ShotCycle.ATTEMPT_NEAR_M + 0.05), _balls(), 0, true, _pockets())
	check_eq([wide["sequence"], wide["reason"]], ["broadcast", "no pot"], "a ball stopping well short")

	var away := ScriptedPlayback.new()
	away.scripted_events = [{"t": 0.0, "kind": "strike", "a": 0, "b": -1, "body": -1},
			{"t": 0.4, "kind": "ball_ball", "a": 0, "b": 1, "body": -1}]
	var centre := _centre(top_left)
	var beside := centre + (Vector2.ZERO - centre).normalized() * (float(_pockets()[top_left]["drop_radius"]) + 0.1)
	away.runs[1] = [beside, Vector2(0.2, 0.1), 0.4, 1.6]
	var plan := ShotCycle.plan(away, _balls(), 0, true, _pockets())
	check_eq(plan["sequence"], "broadcast", "a ball knocked away from the pocket it lay beside")

	var blocked := _miss(top_left, 0.4, 1.6, 0.06)
	blocked.scripted_events.append({"t": 0.8, "kind": "ball_ball", "a": 1, "b": 2, "body": -1})
	plan = ShotCycle.plan(blocked, _balls(), 0, true, _pockets())
	check_eq(plan["sequence"], "broadcast", "a ball that meets another on the way")

	plan = ShotCycle.plan(_miss(top_left, 0.4, ShotCycle.POCKET_CAM_MAX_T + 0.5, 0.06), _balls(), 0, true, _pockets())
	check_eq(plan["sequence"], "broadcast", "a ball that gets there too late, like a late pot")

	plan = ShotCycle.plan(_miss(top_left, 0.4, 1.6, 0.06), _balls(), 0, false, _pockets())
	check_eq([plan["sequence"], plan["reason"]], ["broadcast", "no pot"], "the setting off")

	var no_hit := ScriptedPlayback.new()
	no_hit.scripted_events = [{"t": 0.0, "kind": "strike", "a": 0, "b": -1, "body": -1}]
	no_hit.runs[0] = [Vector2(-1.0, 0.0), _centre(top_left), 0.0, 2.0]
	check(ShotCycle.attempt(no_hit, 0, _pockets()).is_empty(), "the cue ball alone running at a pocket")


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
	var screen := Rect2(Vector2.ZERO, CANVAS)
	check(CameraRig.POCKET_HEIGHT_M <= 0.45, "low: %.2f m above the bed, well under the lamp" % CameraRig.POCKET_HEIGHT_M)
	for pocket: Dictionary in _pockets():
		var c: Array = pocket["drop_centre"]
		var centre := Vector2(float(c[0]), float(c[1]))
		var middle := not bool(pocket["corner"])
		# Middle pockets take balls up to about 45 degrees off square; corners anything from along one
		# cushion round to along the other.
		var ways_in: Array[Vector2] = []
		if middle:
			for off_deg: float in [-50.0, -40.0, -20.0, 0.0, 20.0, 40.0, 50.0]:
				ways_in.append(Vector2(0.0, -signf(centre.y)).rotated(deg_to_rad(off_deg)))
		else:
			var along_long := Vector2(-signf(centre.x), 0.0)
			var along_end := Vector2(0.0, -signf(centre.y))
			for f: float in [0.03, 0.08, 0.3, 0.5, 0.7, 0.92, 0.97]:
				ways_in.append(along_long.slerp(along_end, f))
		for way: Vector2 in ways_in:
			# As far back along its line as the table goes: a long pot from the far end.
			var furthest := INF
			if absf(way.x) > 1e-9:
				furthest = minf(furthest, (signf(way.x) * (half.x - r) - centre.x) / way.x)
			if absf(way.y) > 1e-9:
				furthest = minf(furthest, (signf(way.y) * (half.y - r) - centre.y) / way.y)
			for run: float in [0.3, 0.6, 1.5, furthest]:
				# The ball's run as ShotCycle.track_of gives it: from the pocket back to its start.
				var track := PackedVector2Array()
				for k in ShotCycle.TRACK_POINTS:
					track.append(centre + way * run * float(k) / (ShotCycle.TRACK_POINTS - 1))
				var from := centre + way * minf(run, 1.0)
				var what := "%s from %s" % [pocket["name"], track[-1]]
				var pose := rig.pocket_pose(centre, from, middle, track)
				var eye_xy := TableSpace.godot_to_core(pose.origin)
				check(absf(eye_xy.x) > half.x or absf(eye_xy.y) > half.y, "%s: outside the cushions at %s" % [what, eye_xy])
				check_near(pose.origin.y, CameraRig.POCKET_HEIGHT_M, 1e-6, "%s: low above the bed" % what)
				check(eye_xy.distance_to(centre) < 0.6, "%s: close behind the pocket, %.2f m" % [what, eye_xy.distance_to(centre)])
				check(rig.in_room(pose.origin), "%s: in the room at %s" % [what, pose.origin])
				check((-pose.basis.z).y < 0.0, "%s: looking down" % what)
				var at_bed := TableSpace.core_to_godot(centre.x, centre.y)
				var at_pocket := rig.project(pose, at_bed, CameraRig.POCKET_FOV_DEG)
				check(screen.has_point(at_pocket) and at_pocket.y > CANVAS.y * 0.75 and absf(at_pocket.x - CANVAS.x / 2.0) < CANVAS.x * 0.26,
						"%s: the pocket low and central in the frame at %s" % [what, at_pocket])
				var radius := float(pocket["drop_radius"])
				var mouth := rig.project(pose, at_bed + pose.basis.x * radius, CameraRig.POCKET_FOV_DEG).distance_to(
						rig.project(pose, at_bed - pose.basis.x * radius, CameraRig.POCKET_FOV_DEG))
				check(mouth > CANVAS.x * 0.07, "%s: close, the drop %.0f units across" % [what, mouth])
				var at_ball := rig.project(pose, TableSpace.core_to_godot(from.x, from.y, r), CameraRig.POCKET_FOV_DEG)
				check((pose.affine_inverse() * TableSpace.core_to_godot(from.x, from.y, r)).z < 0.0
						and screen.has_point(at_ball) and at_ball.y < at_pocket.y,
						"%s: the ball coming in above it at %s" % [what, at_ball])
				for point in track:
					var ball := TableSpace.core_to_godot(point.x, point.y, r)
					var at := rig.project(pose, ball, CameraRig.POCKET_FOV_DEG)
					check((pose.affine_inverse() * ball).z < 0.0 and screen.has_point(at),
							"%s: in the frame all the way, at %s it is at %s" % [what, point, at])
			var level := TableSpace.godot_to_core(-rig.pocket_pose(centre, centre + way * 0.6, middle).basis.z).normalized()
			if middle:
				var square := rig.pocket_pose(centre, centre + way * 0.6, true)
				check_near(TableSpace.godot_to_core(square.origin).x, centre.x, 1e-6, "%s: a middle's camera square behind it" % pocket["name"])
				check_near(level.dot(Vector2(0.0, -signf(centre.y))), 1.0, 1e-9, "%s: looking straight across" % pocket["name"])
			else:
				check(level.dot(way) > cos(deg_to_rad(16.0)), "%s: looking back up the ball's way in %s" % [pocket["name"], way])
	# A ball nudged in from the jaws, and one that came round off a jaw from outside, still give a pose.
	var top_left := _centre(_pocket("top_left"))
	for from: Vector2 in [top_left, top_left + Vector2(0.3, 0.3)]:
		var pose := rig.pocket_pose(top_left, from, false)
		var eye_xy := TableSpace.godot_to_core(pose.origin)
		check(absf(eye_xy.x) > half.x or absf(eye_xy.y) > half.y, "from %s: outside the cushions at %s" % [from, eye_xy])
		check(pose.origin.is_finite() and rig.in_room(pose.origin), "from %s: a pose in the room" % from)
		var at_pocket := rig.project(pose, TableSpace.core_to_godot(top_left.x, top_left.y), CameraRig.POCKET_FOV_DEG)
		check(screen.has_point(at_pocket), "from %s: the pocket in the frame at %s" % [from, at_pocket])
	rig.free()


func test_a_pot_cuts_to_the_pocket_then_to_the_full_table_on_time() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.1), 0.3)
	var cycle := ShotCycle.new(rig)
	var top_left := _pocket("top_left")
	var plan := ShotCycle.plan(_pot(top_left, 0.4, 1.6), _balls(), 0, true, _pockets())
	var pose := rig.pocket_pose(plan["drop_centre"], plan["from"], plan["middle"], plan["track"])
	var moves := [0]
	rig.moved.connect(func() -> void: moves[0] += 1)
	var behind := rig.transform
	check_near(ShotCycle.CUT_T, 0.1, 1e-9, "the cut comes a tenth of a second after the strike")
	cycle.start(plan, pose)
	check(rig.is_held() and rig.view_name() == "shot", "held for the shot")
	check(rig.transform.is_equal_approx(behind) and not rig.is_easing(), "still behind the cue ball for the stroke")
	cycle.update(ShotCycle.CUT_T - 0.01)
	check(rig.transform.is_equal_approx(behind) and moves[0] == 0, "still there just before the cut")
	cycle.update(ShotCycle.CUT_T + 0.001)
	check_eq(rig.view_name(), "pocket", "cut to the pocket")
	check(rig.transform.is_equal_approx(pose) and not rig.is_easing(), "at once, no blend")
	check_eq(moves[0], 1, "moved fired once, so the shadows refit")
	check_eq(rig.camera().fov, CameraRig.POCKET_FOV_DEG, "on the pocket camera's lens")
	check(not Perf._active.has(&"camera"), "a still camera keeps nothing drawing")
	check(not cycle.settle_ready(1.0), "no settle at the pocket")
	cycle.update(1.6 + ShotCycle.HOLD_AFTER_DROP_S - 0.01)
	check_eq(rig.view_name(), "pocket", "held until 0.7 s after the drop")
	check(rig.transform.is_equal_approx(pose) and moves[0] == 1, "without moving")
	var cut_at := 1.6 + ShotCycle.HOLD_AFTER_DROP_S + 0.001
	cycle.update(cut_at)
	check(rig.view_name() == "shot" and rig.is_broadcast(), "then the full-table view")
	check(not rig.is_easing() and rig.transform.is_equal_approx(rig.broadcast_pose(CameraRig.FULL_TABLE_YAW)),
			"cut there at once from baulk, no glide up")
	check_eq(moves[0], 2, "one more move")
	check_eq(rig.camera().fov, CameraRig.BROADCAST_FOV_DEG, "on its long lens")
	check(not Perf._active.has(&"camera"), "still: nothing kept drawing")
	check(ShotCycle.MIN_TV_HOLD_S >= 0.8, "the still full table is seen for at least 0.8 s: %.2f" % ShotCycle.MIN_TV_HOLD_S)
	check(not cycle.settle_ready(cut_at + ShotCycle.MIN_TV_HOLD_S - 0.01), "the full-table view holds")
	check(cycle.settle_ready(cut_at + ShotCycle.MIN_TV_HOLD_S + 0.01), "then the settle may come")
	check_eq(moves[0], 2, "and it never moved while it held")
	rig.free()


func test_the_pocket_view_lasts_at_least_a_second() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.1), 0.3)
	var cycle := ShotCycle.new(rig)
	var pocket := _pocket("bottom_right")
	check_near(ShotCycle.MIN_POCKET_S, 1.0, 1e-9, "the shortest pocket view in the reference game")
	# [the drop, the cut to the pocket, the cut on to the full table]
	for entry: Array in [[1.6, 0.1, 2.3], [0.5, 0.1, 1.2], [0.4, 0.1, 1.1], [0.3, 0.1, 1.1], [0.15, 0.1, 1.1]]:
		var drop_t: float = entry[0]
		var plan := ShotCycle.plan(_pot(pocket, drop_t / 2.0, drop_t), _balls(), 0, true, _pockets())
		var what := "a drop at %.2f s" % drop_t
		check_near(ShotCycle.cut_time(plan), entry[1], 1e-9, "%s: the cut" % what)
		check_near(ShotCycle.pocket_leave_time(plan), entry[2], 1e-9, "%s: the pocket view's end" % what)
		check(ShotCycle.pocket_leave_time(plan) - ShotCycle.cut_time(plan) >= ShotCycle.MIN_POCKET_S - 1e-9
				and ShotCycle.pocket_leave_time(plan) >= drop_t + ShotCycle.HOLD_AFTER_DROP_S - 1e-9,
				"%s: a second at least, and 0.7 s after the drop" % what)
		cycle.start(plan, rig.pocket_pose(plan["drop_centre"], plan["from"], plan["middle"], plan["track"]))
		cycle.update(float(entry[1]) + 0.001)
		check_eq(rig.view_name(), "pocket", "%s: at the pocket" % what)
		cycle.update(float(entry[2]) - 0.01)
		check_eq(rig.view_name(), "pocket", "%s: still there just before %.2f s" % [what, entry[2]])
		cycle.update(float(entry[2]) + 0.001)
		check(rig.view_name() == "shot" and rig.is_broadcast() and not rig.is_easing(), "%s: then cut to the full table" % what)
		cycle.end_shot()
		rig.hold(false)
		rig._process(CameraRig.RETURN_S)
	rig.free()


func test_a_ball_that_drops_before_the_usual_cut_is_still_seen_going_in() -> void:
	var rig := _rig()
	rig.follow(Vector2(1.2, 0.5), 0.6)
	var cycle := ShotCycle.new(rig)
	var top_left := _pocket("top_left")
	# Logged drops come as early as 0.13 s; the old cut at 0.2 s missed them.
	var plan := ShotCycle.plan(_pot(top_left, 0.05, 0.15), _balls(), 0, true, _pockets())
	check_eq(plan["sequence"], "pocket", "a drop at 0.15 s gets the pocket camera")
	check(ShotCycle.cut_time(plan) < 0.15 - 1e-9, "cut before it drops: %.2f s" % ShotCycle.cut_time(plan))
	cycle.start(plan, rig.pocket_pose(plan["drop_centre"], plan["from"], plan["middle"], plan["track"]))
	cycle.update(0.11)
	check_eq(rig.view_name(), "pocket", "at the pocket before the drop")
	cycle.update(0.15 + ShotCycle.HOLD_AFTER_DROP_S + 0.05)
	check_eq(rig.view_name(), "pocket", "kept on past 0.7 s after the drop, for its full second")
	cycle.update(ShotCycle.cut_time(plan) + ShotCycle.MIN_POCKET_S + 0.001)
	check(rig.is_broadcast(), "then the full table")
	cycle.end_shot()
	rig.hold(false)
	rig._process(CameraRig.RETURN_S)
	# Sooner still: the cut comes forward to stay ahead of the drop, as far as the strike itself.
	for entry: Array in [[0.12, 0.07], [0.08, 0.03], [0.05, 0.0], [0.02, 0.0]]:
		var early := ShotCycle.plan(_pot(top_left, float(entry[0]) / 2.0, entry[0]), _balls(), 0, true, _pockets())
		check_near(ShotCycle.cut_time(early), entry[1], 1e-9, "a drop at %.2f s: the cut" % entry[0])
		check_near(ShotCycle.pocket_leave_time(early), float(entry[1]) + ShotCycle.MIN_POCKET_S, 1e-9,
				"a drop at %.2f s: a full second at the pocket" % entry[0])
	var at_once := ShotCycle.plan(_pot(top_left, 0.01, 0.02), _balls(), 0, true, _pockets())
	cycle.start(at_once, rig.pocket_pose(at_once["drop_centre"], at_once["from"], at_once["middle"], at_once["track"]))
	cycle.update(0.0)
	check_eq(rig.view_name(), "pocket", "a ball on the lip: at the pocket from the first frame")
	rig.free()


func test_a_shot_with_no_pot_cuts_to_the_full_table() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.1), 0.3)
	var cycle := ShotCycle.new(rig)
	var behind := rig.transform
	var off := ShotCycle.plan(_pot(_pocket("top_left"), 0.4, 1.6), _balls(), 0, false, _pockets())
	for plan: Dictionary in [{"sequence": "broadcast", "reason": "no pot", "pots": [], "cue_potted": false}, off]:
		var what := String(plan["reason"])
		check_near(ShotCycle.cut_time(plan), ShotCycle.CUT_T, 1e-9, "%s: the cut at 0.1 s" % what)
		cycle.start(plan)
		check(rig.view_name() == "shot" and not rig.is_easing() and rig.transform.is_equal_approx(behind),
				"%s: still behind the cue ball for the stroke" % what)
		cycle.update(ShotCycle.CUT_T - 0.01)
		check(rig.transform.is_equal_approx(behind), "%s: until the cut" % what)
		var cut_at := ShotCycle.CUT_T + 0.001
		cycle.update(cut_at)
		check(rig.is_broadcast() and not rig.is_easing() and rig.transform.is_equal_approx(rig.broadcast_pose(CameraRig.FULL_TABLE_YAW)),
				"%s: then cut to the full table, no glide" % what)
		check_eq(rig.camera().fov, CameraRig.BROADCAST_FOV_DEG, "%s: on its long lens" % what)
		cycle.update(1.0)
		cycle.update(3.0)
		check(rig.view_name() == "shot" and rig.transform.is_equal_approx(rig.broadcast_pose(CameraRig.FULL_TABLE_YAW)),
				"%s: no pocket camera, and still" % what)
		check(not cycle.settle_ready(cut_at + ShotCycle.MIN_TV_HOLD_S - 0.01), "%s: not before the full table has held" % what)
		check(cycle.settle_ready(cut_at + ShotCycle.MIN_TV_HOLD_S + 0.01), "%s: the settle once it has" % what)
		cycle.end_shot()
		rig.hold(false)
		rig._process(CameraRig.RETURN_S)
	rig.free()


func test_only_the_swoop_back_moves_the_camera() -> void:
	var rig := _rig()
	var cycle := ShotCycle.new(rig)
	var top_left := _pocket("top_left")
	var pot := ShotCycle.plan(_pot(top_left, 0.4, 1.6), _balls(), 0, true, _pockets())
	# [the plan, how many pictures the camera shows before the swoop]
	for entry: Array in [[pot, 3], [{"sequence": "broadcast", "reason": "no pot", "pots": [], "cue_potted": false}, 2]]:
		var plan: Dictionary = entry[0]
		var what := String(plan["sequence"])
		rig.follow(Vector2(-1.0, 0.1), 0.3)
		rig._process(CameraRig.RETURN_S)
		var pose := Transform3D.IDENTITY
		if plan["sequence"] == "pocket":
			pose = rig.pocket_pose(plan["drop_centre"], plan["from"], plan["middle"], plan["track"])
		cycle.start(plan, pose)
		var pictures: Array[Transform3D] = [rig.transform]
		var blended: Array[String] = []
		var t := 0.0
		while not cycle.settle_ready(t) and t < 10.0:
			cycle.update(t)
			rig._process(DT)
			if rig.is_easing() or Perf._active.has(&"camera"):
				blended.append("%.2f s" % t)
			if not rig.transform.is_equal_approx(pictures[-1]):
				pictures.append(rig.transform)
			t += DT
		check_eq(blended, [] as Array[String], "%s: no blend from the strike to the settle" % what)
		check_eq(pictures.size(), entry[1], "%s: only cuts between still pictures" % what)
		check(pictures[-1].is_equal_approx(rig.broadcast_pose(CameraRig.FULL_TABLE_YAW)), "%s: the last of them the full table" % what)
		cycle.end_shot()
		rig.follow(Vector2(0.4, 0.5), 2.6)
		rig.reset_view()
		rig.hold(false)
		check(rig.is_easing() and Perf._active.has(&"camera"), "%s: the swoop back is the one move" % what)
		var steps: Array[float] = []
		var last := rig.transform.origin
		while rig.is_easing() and steps.size() < 200:
			rig._process(DT)
			steps.append(rig.transform.origin.distance_to(last))
			last = rig.transform.origin
		check(absf(steps.size() - CameraRig.RETURN_S * 60.0) <= 2.0, "%s: RETURN_S long, %d frames" % [what, steps.size()])
		check(steps[0] < steps.max() * 0.1 and steps[-1] < steps.max() * 0.1,
				"%s: eased in and out: %.4f and %.4f m against %.4f at its fastest" % [what, steps[0], steps[-1], steps.max()])
		check(rig.transform.is_equal_approx(rig.pose_for(Vector2(0.4, 0.5), 2.6)), "%s: behind the cue ball on the new aim" % what)
	rig.free()


func test_a_drag_at_the_pocket_cuts_to_the_full_table_to_walk_round_and_a_skip_cancels() -> void:
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.1), 0.3)
	var cycle := ShotCycle.new(rig)
	var plan := ShotCycle.plan(_pot(_pocket("top_left"), 0.4, 1.6), _balls(), 0, true, _pockets())
	var pose := rig.pocket_pose(plan["drop_centre"], plan["from"], false, plan["track"])
	cycle.start(plan, pose)
	cycle.update(0.5)
	check_eq(rig.view_name(), "pocket", "at the pocket")
	check(cycle.walk_round(0.6), "a drag leaves it")
	check(rig.is_broadcast() and not rig.is_easing() and rig.transform.is_equal_approx(rig.broadcast_pose(CameraRig.FULL_TABLE_YAW)),
			"cut to the full table, where a drag walks round")
	check(not cycle.walk_round(0.7), "only once")
	rig.orbit(200.0)
	var walked := rig.transform
	check(not walked.is_equal_approx(rig.broadcast_pose(CameraRig.FULL_TABLE_YAW)) and not rig.is_easing(), "walked round, at once")
	cycle.update(1.6 + ShotCycle.HOLD_AFTER_DROP_S + 0.1)
	check(rig.is_broadcast() and rig.transform.is_equal_approx(walked), "no second cut at the drop")
	check(not cycle.settle_ready(0.6 + ShotCycle.MIN_TV_HOLD_S - 0.01), "the full table holds from the drag")
	check(cycle.settle_ready(0.6 + ShotCycle.MIN_TV_HOLD_S + 0.01), "then settles")
	cycle.end_shot()
	rig.hold(false)
	rig._process(CameraRig.RETURN_S)
	cycle.start(plan, pose)
	check(cycle.walk_round(0.02) and rig.is_broadcast() and not rig.is_easing(), "a drag before the first cut does the same")
	cycle.update(0.5)
	check(rig.is_broadcast(), "and the pocket camera stays away")
	cycle.end_shot()
	rig.hold(false)
	rig._process(CameraRig.RETURN_S)
	cycle.start(plan, pose)
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
		rig.show_broadcast()
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


func test_a_shot_struck_mid_ease_holds_from_the_new_aim() -> void:
	# A harness shot sets the aim and strikes in one frame; the camera mustn't be held at the old aim.
	var rig := _rig()
	rig.follow(Vector2(-1.0, 0.1), 0.3)
	rig.follow(Vector2(-1.0, 0.1), 2.5)
	check(rig.is_easing(), "a big aim change eases")
	rig.finish_ease()
	check(not rig.is_easing() and not Perf._active.has(&"camera"), "ended at once")
	check(rig.transform.is_equal_approx(rig.pose_for(Vector2(-1.0, 0.1), 2.5)), "behind the cue ball on the new aim")
	var behind := rig.transform
	rig.hold(true)
	check(not rig.is_easing() and rig.transform.is_equal_approx(behind), "and the shot holds it there, still")
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
