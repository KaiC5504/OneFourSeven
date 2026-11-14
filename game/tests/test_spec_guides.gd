extends TestCase
## Guide lines checked against the spec (8.4) and straight-line geometry.

const R := 0.02625
const TOP_NOSE_X := 1.7845

var _now := 0.0


func _clock() -> float:
	return _now


## OfsPhysics.table_shape()'s format, built from res://data/table_shape.json: arcs name their
## pocket by its index in the file's pocket order.
func _shape() -> Dictionary:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string("res://data/table_shape.json"))
	check(parsed is Dictionary, "res://data/table_shape.json loads (run scripts/sync_data.py)")
	if not parsed is Dictionary:
		return {}
	var file: Dictionary = parsed
	var pocket_index := {}
	var pockets: Array = []
	for p: Dictionary in file["pockets"]:
		pocket_index[p["name"]] = pockets.size()
		pockets.append({"name": p["name"], "corner": p["corner"], "drop_centre": p["drop_centre"], "drop_radius": p["drop_radius"]})
	var lines: Array = []
	for l: Dictionary in file["lines"]:
		lines.append({"name": l["name"], "kind": l["kind"], "p1": l["p1"], "p2": l["p2"], "normal": l["normal"]})
	var arcs: Array = []
	for a: Dictionary in file["arcs"]:
		arcs.append({
			"name": a["name"], "centre": a["centre"], "radius": a["radius"], "angle_start": a["angle_start"],
			"angle_end": a["angle_end"], "pocket": int(pocket_index[a["pocket"]]),
		})
	return {"lines": lines, "arcs": arcs, "pockets": pockets, "rail_height": file["rail_height_m"], "rail_width": file["rail_width_m"]}


func _ball(kind: String, x: float, y: float, index: int = 0) -> Dictionary:
	return {"ball": kind, "index": index, "x": x, "y": y}


func _check_contact(c: Dictionary, kind: String, index: int, ghost: Vector2, distance: float, object_dir: Vector2) -> void:
	check_eq(c.get("kind"), kind, "kind")
	check_eq(c.get("index"), index, "index")
	var g: Vector2 = c.get("ghost", Vector2.INF)
	check_near(g.x, ghost.x, 1e-6, "ghost x")
	check_near(g.y, ghost.y, 1e-6, "ghost y")
	check_near(float(c.get("distance", -1.0)), distance, 1e-6, "distance")
	var d: Vector2 = c.get("object_dir", Vector2.INF)
	# Vector2 is single precision.
	check_near(d.x, object_dir.x, 1e-5, "object_dir x")
	check_near(d.y, object_dir.y, 1e-5, "object_dir y")


func test_first_contact_straight_on() -> void:
	# spec 8.4 level 1: the ghost ball at the geometric contact position
	var balls: Array[Dictionary] = [_ball("cue", -0.5, 0.0), _ball("red", 0.5, 0.0)]
	var c := Guides.first_contact(balls, 0, 0.0, _shape(), R)
	_check_contact(c, "ball", 1, Vector2(0.5 - 2.0 * R, 0.0), 1.0 - 2.0 * R, Vector2(1.0, 0.0))


func test_first_contact_half_ball() -> void:
	# spec 8.4 level 2: the object ball's direction is the line of centres
	var balls: Array[Dictionary] = [_ball("cue", -0.5, 0.0), _ball("red", 0.5, R)]
	var c := Guides.first_contact(balls, 0, 0.0, _shape(), R)
	var ghost_x := 0.5 - sqrt(3.0) * R
	_check_contact(c, "ball", 1, Vector2(ghost_x, 0.0), ghost_x + 0.5, Vector2(sqrt(3.0) / 2.0, 0.5))


func test_first_contact_takes_the_nearer_ball_and_ignores_one_behind() -> void:
	# spec 8.4: the first contact along the aim line
	var balls: Array[Dictionary] = [
		_ball("red", -0.8, 0.0, 0),
		_ball("cue", -0.5, 0.0),
		_ball("red", 0.6, 0.0, 1),
		_ball("red", 0.0, 2.0 * R + 0.001, 2),
		_ball("red", 0.3, 0.0, 3),
	]
	var c := Guides.first_contact(balls, 1, 0.0, _shape(), R)
	_check_contact(c, "ball", 4, Vector2(0.3 - 2.0 * R, 0.0), 0.8 - 2.0 * R, Vector2(1.0, 0.0))


func test_first_contact_cushion_with_no_balls() -> void:
	# spec 8.4: the aim line runs to the first contact, here the top cushion's nose
	var balls: Array[Dictionary] = [_ball("cue", 0.0, 0.0)]
	var c := Guides.first_contact(balls, 0, 0.0, _shape(), R)
	_check_contact(c, "cushion", -1, Vector2(TOP_NOSE_X - R, 0.0), TOP_NOSE_X - R, Vector2.ZERO)


func test_first_contact_pocket() -> void:
	# spec 8.4 with the outline of spec 6.3: an aim from the table centre into the top-left pocket,
	# at the point where its two cushions' nose lines cross, the middle of its mouth. (Aimed at the
	# drop centre, deeper in, the ball clips the left jaw.)
	var shape := _shape()
	var corner := Vector2.ZERO
	for line: Dictionary in shape.get("lines", []):
		if line["name"] == "top":
			corner.x = float(line["p1"][0])
		elif line["name"] == "left_top":
			corner.y = float(line["p1"][1])
	check(corner.x > 0.0 and corner.y > 0.0, "the top and left noses are in the shape")
	var aim := corner.angle()
	var balls: Array[Dictionary] = [_ball("cue", 0.0, 0.0)]
	var c := Guides.first_contact(balls, 0, aim, shape, R)
	check_eq(c.get("kind"), "pocket", "kind")
	check_eq(c.get("index"), -1, "index")
	check_eq(c.get("object_dir"), Vector2.ZERO, "object_dir")
	var g: Vector2 = c.get("ghost", Vector2.INF)
	var along := Vector2(cos(aim), sin(aim))
	check_near(along.cross(g), 0.0, 1e-6, "ghost on the aim line")
	check_near(float(c.get("distance", -1.0)), g.length(), 1e-6, "distance to the ghost")
	if shape.is_empty():
		return
	var top_left: Dictionary = shape["pockets"][0]
	check_eq(top_left["name"], "top_left", "pocket 0")
	var drop := Vector2(float(top_left["drop_centre"][0]), float(top_left["drop_centre"][1]))
	check(g.distance_to(drop) < 0.1, "ghost at the top-left pocket: %s" % g)


func _setup() -> ShotSetup:
	var setup := ShotSetup.new()
	setup.set_aim(0.02)
	setup.set_tip(0.1, -0.3)
	setup.set_elevation(0.2)
	setup.set_elevation_min(0.05)
	setup.set_speed(2.5, 0.4)
	return setup


func _balls() -> Array[Dictionary]:
	var balls: Array[Dictionary] = [_ball("cue", -0.5, 0.0), _ball("red", 0.5, 0.02)]
	return balls


func _guides(calls: Array[Dictionary]) -> Guides:
	var guides := Guides.new()
	guides.clock = _clock
	guides.predictor = func(_balls: Array, _cue: int, strike: Dictionary) -> OfsShotRecord:
		calls.append(strike.duplicate())
		return null
	tree.root.add_child(guides)
	return guides


func _frames(n: int) -> void:
	for _i: int in n:
		await tree.process_frame


func test_level_3_predictions_are_throttled() -> void:
	# spec 8.4: recomputed only when something changes, a handful of simulations per second
	var calls: Array[Dictionary] = []
	var guides := _guides(calls)
	var setup := ShotSetup.new()
	var balls := _balls()
	_now = 100.0
	# A second of aiming with a change every 20 ms.
	for _i: int in 50:
		setup.add_aim(0.001)
		guides.show_for(balls, 0, setup, 3)
		_now += 0.02
		await tree.process_frame
	check(calls.size() >= 1, "aiming predicts")
	check(calls.size() <= Guides.PREDICT_MAX_PER_S + 1, "at most %d a second plus the deferred one: %d" % [Guides.PREDICT_MAX_PER_S, calls.size()])

	# The last change was deferred; once the throttle allows, it is predicted.
	_now += 1.0
	await _frames(3)
	guides.show_for(balls, 0, setup, 3)
	await _frames(3)
	check(not calls.is_empty(), "predicted")
	if not calls.is_empty():
		var last: Dictionary = calls.back()
		check_near(float(last.get("aim_rad", INF)), setup.aim_rad, 1e-12, "the latest aim is predicted")
		# spec 8.4: finger-stroke mode before any stroke uses a medium pace.
		check_near(float(last.get("speed_m_s", INF)), setup.guide_pace_m_s(), 1e-12, "at the guide pace")

	var settled := calls.size()
	for _i: int in 30:
		_now += 0.1
		guides.show_for(balls, 0, setup, 3)
		await tree.process_frame
	check_eq(calls.size(), settled, "no prediction without a change")
	guides.free()


func test_levels_0_to_2_never_simulate() -> void:
	# spec 8.4: levels 0-2 are straight-line geometry only
	var calls: Array[Dictionary] = []
	var guides := _guides(calls)
	var setup := ShotSetup.new()
	var balls := _balls()
	for level: int in [0, 1, 2]:
		for _i: int in 5:
			setup.add_aim(0.01)
			guides.show_for(balls, 0, setup, level)
			_now += 0.5
			await tree.process_frame
	check_eq(calls.size(), 0, "no simulations")
	guides.free()


func test_guides_never_alter_the_setup() -> void:
	# spec 8.4: guides never alter the shot, in every mode
	var calls: Array[Dictionary] = []
	var guides := _guides(calls)
	var balls := _balls()
	for mode: String in [ShotSetup.STROKE, ShotSetup.PULL]:
		for level: int in [0, 1, 2, 3]:
			var setup := _setup()
			setup.set_strike_mode(mode)
			var before := setup.snapshot()
			var log_before := setup.log_fields()
			var changes: Array[StringName] = []
			setup.changed.connect(func(what: StringName) -> void: changes.append(what))
			for _i: int in 3:
				guides.show_for(balls, 0, setup, level)
				_now += 1.0
				await tree.process_frame
			check_eq(setup.snapshot(), before, "%s level %d: snapshot" % [mode, level])
			check_eq(setup.log_fields(), log_before, "%s level %d: every field" % [mode, level])
			check_eq(changes.size(), 0, "%s level %d: no change signalled" % [mode, level])
	guides.hide_all()
	guides.free()
