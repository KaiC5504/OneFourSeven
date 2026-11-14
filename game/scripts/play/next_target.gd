class_name NextTarget
extends RefCounted
## Picks where the aim turns after a shot, as the camera swoops back behind the cue ball: the
## easiest clear pot, preferring the kind of ball that would be on.
##
## Ghost-ball geometry on straight lines. Each preferred ball and pocket gives a target just inside
## the mouth, the ghost ball touching the object ball on the line to it, and the cut between the
## cue ball's path to the ghost and the object ball's path. A pot is clear when no ball comes
## within a ball's width of either path, the ghost is on the cloth, and the object ball's centre
## reaches the pocket's drop circle before touching a nose, jaw or facing.

const VALUES := {"red": 1, "yellow": 2, "green": 3, "brown": 4, "blue": 5, "pink": 6, "black": 7}
const PREFERENCES: Array[String] = ["red", "colour", "any"]
const MAX_CUT_DEG := 75.0
## Middle pockets are narrow: nobody pots along the cushion into one.
const MAX_MIDDLE_APPROACH_DEG := 50.0
# Ease = cos^2(cut) / (1 + CUE_COST * cue travel + OBJECT_COST * object travel), per metre: an aim
# error grows along both paths alike, and any sum under 20/m keeps a 30 degree cut over 1 m (0.25)
# ahead of a 60 degree cut over 0.3 m (0.156).
const CUE_COST_PER_M := 1.0
const OBJECT_COST_PER_M := 1.0
# Aimed at the mouth's centre, an angled pot clips the near jaw, so the target moves towards the
# far jaw by this times the sine of the approach's angle to the pocket's axis. Over the cloth in
# 2.5 cm steps that clears all the corner pots and 98% of the middle pots that the best fixed shift
# up to 3 cm would; the mouth's centre loses about 30% of them (the black off its spot into a top
# corner among them), the drop centre about 64%.
const CORNER_SHIFT_M := 0.025
const MIDDLE_SHIFT_M := 0.02
## Shorter paths than this have no direction.
const MIN_TRAVEL_M := 1.0e-9
# Tolerances as Guides uses them: already touching, arc ends and segment ends.
const TOUCH_EPS := 1.0e-12
const ANGLE_EPS := 1.0e-9
const SEGMENT_EPS := 1.0e-9
# Candidates sort natively as one PackedInt64Array of (QUANT - score * QUANT) << 14 | (7 - value)
# << 11 | candidate, so the easiest comes first, then the higher value, then the earlier ball and
# pocket. A valid table has at most 21 object balls, far below the 2048 candidates 11 bits hold.
const QUANT := 1099511627776.0
const CANDIDATE_MASK := 2047
## Floats stored per candidate: target x, y, ghost x, y, object direction x, y, score.
const CANDIDATE_STRIDE := 7
## Floats stored per pocket: drop centre x, y, drop radius, outward axis x, y, mouth target x, y,
## shift, 1 for a corner.
const POCKET_STRIDE := 9


## balls are FreeTable balls ({"ball", "index", "x", "y"} in core metres); prefer is "red",
## "colour" or "any". Returns {"index", "ball", "pocket", "aim_rad", "cut_deg", "score",
## "reason"}; pocket is -1 and reason "no clear pot" when it aims straight at the nearest
## preferred ball instead. {} when there is no object ball.
static func choose(balls: Array[Dictionary], cue_ball: int, prefer: String) -> Dictionary:
	return choose_on(Physics.table_shape(), OfsTable.dimensions(), balls, cue_ball, prefer)


## choose on a given table: shape as OfsPhysics.table_shape(), dims as OfsTable.dimensions().
## Bad input (a malformed ball list, cue_ball not the cue ball, an unknown prefer, no shape) logs
## app.error and returns {}.
static func choose_on(shape: Dictionary, dims: Dictionary, balls: Array[Dictionary], cue_ball: int,
		prefer: String) -> Dictionary:
	var problem := _input_problem(shape, dims, balls, cue_ball, prefer)
	if not problem.is_empty():
		Log.warn("app", "app.error", {"kind": "message", "message": "NextTarget: %s; the aim stays where it was" % problem})
		return {}
	var r := float(dims["ball_radius"])
	var half_x := float(dims["playing_length"]) / 2.0 - r + FreeTable.OUTSIDE_SLACK
	var half_y := float(dims["playing_width"]) / 2.0 - r + FreeTable.OUTSIDE_SLACK
	var n := balls.size()
	var xs := PackedFloat64Array()
	var ys := PackedFloat64Array()
	xs.resize(n)
	ys.resize(n)
	var on_table := PackedByteArray()
	on_table.resize(n)
	var objects: Array[int] = []
	var preferred: Array[int] = []
	for i in n:
		var b := balls[i]
		xs[i] = float(b["x"])
		ys[i] = float(b["y"])
		on_table[i] = 1 if int(b.get("pocket", -1)) < 0 else 0
		if i == cue_ball or on_table[i] == 0:
			continue
		objects.append(i)
		var red := String(b["ball"]) == "red"
		if prefer == "any" or (prefer == "red" and red) or (prefer == "colour" and not red):
			preferred.append(i)
	if objects.is_empty():
		return {}
	if preferred.is_empty():
		preferred = objects

	var cx := xs[cue_ball]
	var cy := ys[cue_ball]
	var pockets: Array = shape["pockets"]
	var cos_max_cut := cos(deg_to_rad(MAX_CUT_DEG))
	var cos_max_middle := cos(deg_to_rad(MAX_MIDDLE_APPROACH_DEG))
	var pocket_data := PackedFloat64Array()
	for pocket: Dictionary in pockets:
		var centre: Array = pocket["drop_centre"]
		var dcx := float(centre[0])
		var dcy := float(centre[1])
		var drop_r := float(pocket["drop_radius"])
		var corner := bool(pocket["corner"])
		var ax := signf(dcx) * sqrt(0.5) if corner else 0.0
		var ay := signf(dcy) * sqrt(0.5) if corner else signf(dcy)
		# The target is the drop circle's near edge, drop_set_back_m behind the narrowest point.
		pocket_data.append_array(PackedFloat64Array([dcx, dcy, drop_r, ax, ay, dcx - ax * drop_r, dcy - ay * drop_r,
				CORNER_SHIFT_M if corner else MIDDLE_SHIFT_M, 1.0 if corner else 0.0]))

	var cand_ball := PackedInt32Array()
	var cand_pocket := PackedInt32Array()
	var cand := PackedFloat64Array()
	cand.resize(preferred.size() * pockets.size() * CANDIDATE_STRIDE)
	var keys := PackedInt64Array()
	for i in preferred:
		var ox := xs[i]
		var oy := ys[i]
		var value := int(VALUES[String(balls[i]["ball"])])
		for p in pockets.size():
			var k := p * POCKET_STRIDE
			var ax := pocket_data[k + 3]
			var ay := pocket_data[k + 4]
			var tx := pocket_data[k + 5]
			var ty := pocket_data[k + 6]
			var ux := tx - ox
			var uy := ty - oy
			var length := sqrt(ux * ux + uy * uy)
			if length < MIN_TRAVEL_M:
				continue
			ux /= length
			uy /= length
			# The part of the path across the axis has length sin(approach angle).
			var along := ux * ax + uy * ay
			tx += (ux - along * ax) * pocket_data[k + 7]
			ty += (uy - along * ay) * pocket_data[k + 7]
			ux = tx - ox
			uy = ty - oy
			length = sqrt(ux * ux + uy * uy)
			ux /= length
			uy /= length
			if pocket_data[k + 8] == 0.0 and ux * ax + uy * ay < cos_max_middle:
				continue
			var gx := ox - 2.0 * r * ux
			var gy := oy - 2.0 * r * uy
			if absf(gx) > half_x or absf(gy) > half_y:
				continue
			var vx := gx - cx
			var vy := gy - cy
			var travel := sqrt(vx * vx + vy * vy)
			var cos_cut := (vx * ux + vy * uy) / travel if travel > MIN_TRAVEL_M else 1.0
			if cos_cut < cos_max_cut:
				continue
			var score := cos_cut * cos_cut / (1.0 + CUE_COST_PER_M * travel + OBJECT_COST_PER_M * length)
			var c := cand_ball.size()
			keys.append((int(QUANT - score * QUANT) << 14) | ((7 - value) << 11) | c)
			cand_ball.append(i)
			cand_pocket.append(p)
			var j := c * CANDIDATE_STRIDE
			cand[j] = tx
			cand[j + 1] = ty
			cand[j + 2] = gx
			cand[j + 3] = gy
			cand[j + 4] = ux
			cand[j + 5] = uy
			cand[j + 6] = score
	keys.sort()

	var lines := _packed_lines(shape["lines"])
	var arcs := _packed_arcs(shape["arcs"], r)
	var min_d2 := 4.0 * r * r
	for key in keys:
		var c := key & CANDIDATE_MASK
		var i := cand_ball[c]
		var p := cand_pocket[c]
		var j := c * CANDIDATE_STRIDE
		var tx := cand[j]
		var ty := cand[j + 1]
		var gx := cand[j + 2]
		var gy := cand[j + 3]
		var ux := cand[j + 4]
		var uy := cand[j + 5]
		if _path_blocked(xs, ys, on_table, cue_ball, i, cx, cy, gx, gy, min_d2) \
				or _path_blocked(xs, ys, on_table, cue_ball, i, xs[i], ys[i], tx, ty, min_d2):
			continue
		var k := p * POCKET_STRIDE
		if not _reaches_pocket(xs[i], ys[i], ux, uy, pocket_data[k], pocket_data[k + 1], pocket_data[k + 2], lines, arcs, r):
			continue
		var vx := gx - cx
		var vy := gy - cy
		if vx * vx + vy * vy < MIN_TRAVEL_M * MIN_TRAVEL_M:
			vx = ux
			vy = uy
		return {"index": i, "ball": FreeTable._ball_label(balls[i]), "pocket": p, "aim_rad": atan2(vy, vx),
				"cut_deg": rad_to_deg(atan2(absf(vx * uy - vy * ux), vx * ux + vy * uy)), "score": cand[j + 6],
				"reason": ""}

	var nearest := preferred[0]
	var nearest_d2 := INF
	for i in preferred:
		var d2 := (xs[i] - cx) * (xs[i] - cx) + (ys[i] - cy) * (ys[i] - cy)
		if d2 < nearest_d2:
			nearest = i
			nearest_d2 = d2
	return {"index": nearest, "ball": FreeTable._ball_label(balls[nearest]), "pocket": -1,
			"aim_rad": atan2(ys[nearest] - cy, xs[nearest] - cx), "cut_deg": 0.0, "score": 0.0, "reason": "no clear pot"}


static func _input_problem(shape: Dictionary, dims: Dictionary, balls: Array[Dictionary], cue_ball: int,
		prefer: String) -> String:
	if not PREFERENCES.has(prefer):
		return "prefer \"%s\" is not red, colour or any" % prefer
	var problems := FreeTable.ball_list_problems(balls, cue_ball)
	if not problems.is_empty():
		return "malformed balls (%d of them, cue_ball %d): %s" % [balls.size(), cue_ball, "; ".join(problems)]
	if not (dims.get("ball_radius") is float and dims.get("playing_length") is float and dims.get("playing_width") is float) \
			or not float(dims["ball_radius"]) > 0.0:
		return "no table dimensions (got %s)" % JSON.stringify(dims)
	var pockets: Variant = shape.get("pockets")
	if not pockets is Array or (pockets as Array).is_empty() or not shape.get("lines") is Array \
			or not shape.get("arcs") is Array:
		return "no table shape (Physics not configured?)"
	for pocket: Variant in pockets:
		if not pocket is Dictionary or not (pocket as Dictionary).has_all(["drop_centre", "drop_radius", "corner"]):
			return "a pocket lacks drop_centre, drop_radius or corner: %s" % JSON.stringify(pocket)
	return ""


## Whether a ball other than skip_a and skip_b comes within sqrt(min_d2) of the segment a to b.
static func _path_blocked(xs: PackedFloat64Array, ys: PackedFloat64Array, on_table: PackedByteArray, skip_a: int,
		skip_b: int, ax: float, ay: float, bx: float, by: float, min_d2: float) -> bool:
	var dx := bx - ax
	var dy := by - ay
	var length2 := dx * dx + dy * dy
	for j in xs.size():
		if j == skip_a or j == skip_b or on_table[j] == 0:
			continue
		var wx := xs[j] - ax
		var wy := ys[j] - ay
		var s := clampf((wx * dx + wy * dy) / length2, 0.0, 1.0) if length2 > 0.0 else 0.0
		var ex := wx - s * dx
		var ey := wy - s * dy
		if ex * ex + ey * ey < min_d2:
			return true
	return false


## Whether a ball of radius r sent from o along the unit (ux, uy) reaches the drop circle before
## any cushion line or arc, as Guides.first_contact's "pocket".
static func _reaches_pocket(ox: float, oy: float, ux: float, uy: float, dcx: float, dcy: float, drop_r: float,
		lines: PackedFloat64Array, arcs: PackedFloat64Array, r: float) -> bool:
	var to_pocket := _ray_circle(ox - dcx, oy - dcy, ux, uy, drop_r)
	if to_pocket == INF:
		return false
	# Lines: x1, y1, ex, ey, |e|^2, nx, ny.
	for k in range(0, lines.size(), 7):
		var heading := ux * lines[k + 5] + uy * lines[k + 6]
		if heading >= 0.0:
			continue
		var height := (ox - lines[k]) * lines[k + 5] + (oy - lines[k + 1]) * lines[k + 6]
		if height < 0.0:
			continue
		var t := maxf((height - r) / -heading, 0.0)
		if t >= to_pocket:
			continue
		var s := ((ox + t * ux - lines[k]) * lines[k + 2] + (oy + t * uy - lines[k + 1]) * lines[k + 3]) / lines[k + 4]
		if s >= -SEGMENT_EPS and s <= 1.0 + SEGMENT_EPS:
			return false
	# Arcs: centre x, y, radius + r, angle_start, angle_end - angle_start.
	for k in range(0, arcs.size(), 5):
		var t := _ray_circle(ox - arcs[k], oy - arcs[k + 1], ux, uy, arcs[k + 2])
		if t >= to_pocket:
			continue
		var past := fposmod(atan2(oy + t * uy - arcs[k + 1], ox + t * ux - arcs[k]) - arcs[k + 3], TAU)
		if past <= arcs[k + 4] + ANGLE_EPS or past >= TAU - ANGLE_EPS:
			return false
	return true


## Distance along the unit ray (dx, dy) until its origin, at w from a circle's centre, reaches
## `radius`; INF when it never does, 0 when already there and heading inwards.
static func _ray_circle(wx: float, wy: float, dx: float, dy: float, radius: float) -> float:
	var b := wx * dx + wy * dy
	var c := wx * wx + wy * wy - radius * radius
	if c <= TOUCH_EPS:
		return 0.0 if b < 0.0 else INF
	var disc := b * b - c
	if b >= 0.0 or disc < 0.0:
		return INF
	return -b - sqrt(disc)


static func _packed_lines(lines: Array) -> PackedFloat64Array:
	var out := PackedFloat64Array()
	for line: Dictionary in lines:
		var p1: Array = line["p1"]
		var p2: Array = line["p2"]
		var normal: Array = line["normal"]
		var ex := float(p2[0]) - float(p1[0])
		var ey := float(p2[1]) - float(p1[1])
		out.append_array([float(p1[0]), float(p1[1]), ex, ey, ex * ex + ey * ey, float(normal[0]), float(normal[1])])
	return out


static func _packed_arcs(arcs: Array, r: float) -> PackedFloat64Array:
	var out := PackedFloat64Array()
	for arc: Dictionary in arcs:
		var centre: Array = arc["centre"]
		var start := float(arc["angle_start"])
		out.append_array([float(centre[0]), float(centre[1]), float(arc["radius"]) + r, start, float(arc["angle_end"]) - start])
	return out
