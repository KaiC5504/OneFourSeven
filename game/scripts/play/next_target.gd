class_name NextTarget
extends RefCounted
## Picks where the aim turns after a shot, as the camera swoops back behind the cue ball: the
## easiest clear pot, preferring the kind of ball that would be on.
##
## Ghost-ball geometry on straight lines. Each preferred ball and pocket gives the line a ball must
## take to drop (the middle of what it can see of the hole between the jaws), the ghost ball touching
## the object ball on that line, and the cut between the cue ball's path to the ghost and the object
## ball's path. A pot is clear when no ball comes within a ball's width of either path, the ghost is
## on the cloth, and the object ball's centre reaches the pocket's drop circle before touching a
## nose, jaw or facing.

const VALUES := {"red": 1, "yellow": 2, "green": 3, "brown": 4, "blue": 5, "pink": 6, "black": 7}
const PREFERENCES: Array[String] = ["red", "colour", "any"]
## The simulator makes every cut to 80 degrees, but the aim that makes one narrows from 0.2 degrees
## wide at 75 to 0.1 at 80, and past 80 some can't be made at all.
const MAX_CUT_DEG := 75.0
## Off a middle pocket's axis. Its mouth closes up from the side: balls still go in cleanly at 75
## degrees, through a few millimetres; to 50 it stays wider than a corner pocket's at its best.
const MAX_MIDDLE_APPROACH_DEG := 50.0
# Ease = cos^2(cut) / (1 + CUE_COST * cue travel + OBJECT_COST * object travel), per metre: an aim
# error grows along both paths alike, and any sum under 20/m keeps a 30 degree cut over 1 m (0.25)
# ahead of a 60 degree cut over 0.3 m (0.156).
const CUE_COST_PER_M := 1.0
const OBJECT_COST_PER_M := 1.0
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
## Per side of a mouth: where the drop circle meets the cushion a ball's radius out (x, y; NAN when
## it doesn't), then that side's jaw as a ball's centre meets it: centre x, y, radius plus a ball's
## (0 without a jaw), and unit vectors from that centre to the two ends, anticlockwise, of the arc a
## ball can touch before the hole.
const MOUTH_SIDE := 9
## Floats stored per pocket (_mouths): drop centre x, y, drop radius, outward axis x, y, 1 for a
## corner, then MOUTH_SIDE floats for the side on the left looking into the pocket and for the right.
const POCKET_STRIDE := 6 + 2 * MOUTH_SIDE


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
	var pocket_data := _mouths(pockets, shape["lines"], shape["arcs"], r)

	var cand_ball := PackedInt32Array()
	var cand_pocket := PackedInt32Array()
	var cand := PackedFloat64Array()
	cand.resize(preferred.size() * pockets.size() * CANDIDATE_STRIDE)
	var keys := PackedInt64Array()
	var line := PackedFloat64Array([0.0, 0.0, 0.0, 0.0])
	for i in preferred:
		var ox := xs[i]
		var oy := ys[i]
		var value := int(VALUES[String(balls[i]["ball"])])
		for p in pockets.size():
			var k := p * POCKET_STRIDE
			if not _pot_line(pocket_data, k, ox, oy, line):
				continue
			var ux := line[0]
			var uy := line[1]
			var length := line[2]
			if pocket_data[k + 5] == 0.0 and ux * pocket_data[k + 3] + uy * pocket_data[k + 4] < cos_max_middle:
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
			cand[j] = ox + ux * length
			cand[j + 1] = oy + uy * length
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


## Each pocket's mouth as a ball's centre meets it, POCKET_STRIDE floats a pocket. The part of the
## drop circle a centre can reach runs between the two points where the circle meets a jaw or
## facing a ball's radius out, either side of the point nearest the table.
static func _mouths(pockets: Array, lines: Array, arcs: Array, r: float) -> PackedFloat64Array:
	var index_of := {}
	for p in pockets.size():
		index_of[(pockets[p] as Dictionary).get("name", "")] = p
	# Each jaw a ball's radius out: its pocket, centre x, y, radius, start and sweep.
	var jaws := PackedFloat64Array()
	for arc: Dictionary in arcs:
		var of: Variant = arc.get("pocket")
		var centre: Array = arc["centre"]
		var start := float(arc["angle_start"])
		# The binding names an arc's pocket by index, the shape file by name.
		var pocket := float(of) if of is int or of is float else float(index_of.get(of, -1))
		jaws.append_array([pocket, float(centre[0]), float(centre[1]), float(arc["radius"]) + r, start,
				float(arc["angle_end"]) - start])
	# Each facing a ball's radius out: its start x, y and its run x, y.
	var facings := PackedFloat64Array()
	for line: Dictionary in lines:
		if line.get("kind", "") != "facing":
			continue
		var p1: Array = line["p1"]
		var p2: Array = line["p2"]
		var normal: Array = line["normal"]
		facings.append_array([float(p1[0]) + float(normal[0]) * r, float(p1[1]) + float(normal[1]) * r,
				float(p2[0]) - float(p1[0]), float(p2[1]) - float(p1[1])])

	var out := PackedFloat64Array()
	out.resize(pockets.size() * POCKET_STRIDE)
	for p in pockets.size():
		var pocket: Dictionary = pockets[p]
		var centre: Array = pocket["drop_centre"]
		var dcx := float(centre[0])
		var dcy := float(centre[1])
		var drop_r := float(pocket["drop_radius"])
		var corner := bool(pocket["corner"])
		var ax := signf(dcx) * sqrt(0.5) if corner else 0.0
		var ay := signf(dcy) * sqrt(0.5) if corner else signf(dcy)
		var k := p * POCKET_STRIDE
		out[k] = dcx
		out[k + 1] = dcy
		out[k + 2] = drop_r
		out[k + 3] = ax
		out[k + 4] = ay
		out[k + 5] = 1.0 if corner else 0.0
		# How far round the drop circle from its point nearest the table each side's meeting point is.
		var nearest := PackedFloat64Array([INF, INF])
		var met_on_jaw := PackedByteArray([0, 0])
		for side in 2:
			out[k + 6 + side * MOUTH_SIDE] = NAN
			out[k + 6 + side * MOUTH_SIDE + 4] = 0.0
		for j in range(0, jaws.size(), 6):
			if int(jaws[j]) != p:
				continue
			var jx := jaws[j + 1]
			var jy := jaws[j + 2]
			var rho := jaws[j + 3]
			var b := k + 6 + (0 if ax * (jy - dcy) - ay * (jx - dcx) > 0.0 else 1) * MOUTH_SIDE
			out[b + 2] = jx
			out[b + 3] = jy
			out[b + 4] = rho
			out[b + 5] = jaws[j + 4]
			out[b + 6] = jaws[j + 5]
			var apart := sqrt((jx - dcx) * (jx - dcx) + (jy - dcy) * (jy - dcy))
			if apart < MIN_TRAVEL_M or apart > rho + drop_r or apart < absf(rho - drop_r):
				continue
			var along := (apart * apart + drop_r * drop_r - rho * rho) / (2.0 * apart)
			var across := sqrt(maxf(drop_r * drop_r - along * along, 0.0))
			for sign: float in [1.0, -1.0]:
				var x := dcx + (along * (jx - dcx) - sign * across * (jy - dcy)) / apart
				var y := dcy + (along * (jy - dcy) + sign * across * (jx - dcx)) / apart
				var past := fposmod(atan2(y - jy, x - jx) - jaws[j + 4], TAU)
				if (past <= jaws[j + 5] + ANGLE_EPS or past >= TAU - ANGLE_EPS) and _meets(out, k, x, y, nearest):
					met_on_jaw[0 if ax * (y - dcy) - ay * (x - dcx) > 0.0 else 1] = 1
		for f in range(0, facings.size(), 4):
			var x1 := facings[f]
			var y1 := facings[f + 1]
			var ex := facings[f + 2]
			var ey := facings[f + 3]
			var e2 := ex * ex + ey * ey
			var half_b := (x1 - dcx) * ex + (y1 - dcy) * ey
			var disc := half_b * half_b - e2 * ((x1 - dcx) * (x1 - dcx) + (y1 - dcy) * (y1 - dcy) - drop_r * drop_r)
			if e2 <= 0.0 or disc < 0.0:
				continue
			for sign: float in [1.0, -1.0]:
				var s := (-half_b + sign * sqrt(disc)) / e2
				if s < -SEGMENT_EPS or s > 1.0 + SEGMENT_EPS:
					continue
				var x := x1 + s * ex
				var y := y1 + s * ey
				if _meets(out, k, x, y, nearest):
					met_on_jaw[0 if ax * (y - dcy) - ay * (x - dcx) > 0.0 else 1] = 0
		# A jaw can only be touched before the hole: its arc stops where the drop circle crosses it.
		for side in 2:
			var b := k + 6 + side * MOUTH_SIDE
			if out[b + 4] <= 0.0:
				continue
			var start := out[b + 5]
			var sweep := out[b + 6]
			if met_on_jaw[side] != 0:
				var at := clampf(fposmod(atan2(out[b + 1] - out[b + 3], out[b] - out[b + 2]) - start + ANGLE_EPS, TAU) - ANGLE_EPS,
						0.0, sweep)
				var sx := out[b + 2] + out[b + 4] * cos(start) - dcx
				var sy := out[b + 3] + out[b + 4] * sin(start) - dcy
				if sx * sx + sy * sy > drop_r * drop_r:
					sweep = at
				else:
					start += at
					sweep -= at
			out[b + 5] = cos(start)
			out[b + 6] = sin(start)
			out[b + 7] = cos(start + sweep)
			out[b + 8] = sin(start + sweep)
	return out


## Keeps (x, y) as its side's meeting point of pocket k when it is nearer the drop circle's point
## nearest the table than the one held; true when kept.
static func _meets(mouths: PackedFloat64Array, k: int, x: float, y: float, nearest: PackedFloat64Array) -> bool:
	var ax := mouths[k + 3]
	var ay := mouths[k + 4]
	var rx := x - mouths[k]
	var ry := y - mouths[k + 1]
	var left := ax * ry - ay * rx
	var side := 0 if left > 0.0 else 1
	var round_from_front := absf(atan2(left, -(ax * rx + ay * ry)))
	if round_from_front >= nearest[side]:
		return false
	nearest[side] = round_from_front
	mouths[k + 6 + side * MOUTH_SIDE] = x
	mouths[k + 6 + side * MOUTH_SIDE + 1] = y
	return true


## The line a ball at o must take into pocket k of `mouths`: the middle of the directions along
## which its centre reaches the reachable part of the drop circle without touching a jaw first, so
## it has the same room for error either side. Fills line with the unit direction x, y, the travel
## to the drop circle and half the width of those directions in radians; false when there are none.
static func _pot_line(mouths: PackedFloat64Array, k: int, ox: float, oy: float, line: PackedFloat64Array) -> bool:
	var dcx := mouths[k]
	var dcy := mouths[k + 1]
	var drop_r := mouths[k + 2]
	var wx := dcx - ox
	var wy := dcy - oy
	var dist2 := wx * wx + wy * wy
	if dist2 <= drop_r * drop_r:
		return false
	var dist := sqrt(dist2)
	var ex := wx / dist
	var ey := wy / dist
	# Directions are kept as the tangent of their angle from the line to the drop centre, positive
	# to the left: they all lie within a quarter turn of it.
	var hi := drop_r / sqrt(dist2 - drop_r * drop_r)
	var lo := -hi
	for side in 2:
		var b := k + 6 + side * MOUTH_SIDE
		var s := 1.0 if side == 0 else -1.0
		var bound := hi if side == 0 else lo
		var px := mouths[b]
		if not is_nan(px):
			var py := mouths[b + 1]
			if (px - dcx) * (ox - dcx) + (py - dcy) * (oy - dcy) >= drop_r * drop_r:
				# The meeting point faces the ball: nothing of the hole shows beyond it.
				bound = (ex * (py - oy) - ey * (px - ox)) / (ex * (px - ox) + ey * (py - oy))
			elif s * (ex * (py - dcy) - ey * (px - dcx)) <= 0.0:
				# It is round the back on the other side: the ball sees none of the reachable part.
				return false
		var rho := mouths[b + 4]
		if rho > 0.0:
			var jx := mouths[b + 2] - ox
			var jy := mouths[b + 3] - oy
			var away := sqrt(jx * jx + jy * jy)
			var sin_half := minf(rho / away, 1.0)
			var cos_half := sqrt(1.0 - sin_half * sin_half)
			# The line that just clears the jaw on the hole's side of it, and where it touches.
			var tx := (jx * cos_half + s * jy * sin_half) / away
			var ty := (jy * cos_half - s * jx * sin_half) / away
			var qx := tx * away * cos_half - jx
			var qy := ty * away * cos_half - jy
			if mouths[b + 5] * qy - mouths[b + 6] * qx >= -ANGLE_EPS * rho and qx * mouths[b + 8] - qy * mouths[b + 7] >= -ANGLE_EPS * rho:
				var ahead := ex * tx + ey * ty
				var left := ex * ty - ey * tx
				if ahead > 0.0:
					bound = minf(bound, left / ahead) if side == 0 else maxf(bound, left / ahead)
				elif s * left <= 0.0:
					# It clears the jaw only by turning away from the hole.
					return false
		if side == 0:
			hi = bound
		else:
			lo = bound
	if hi - lo <= ANGLE_EPS:
		return false
	# The bisector of the two bounding directions, in the frame of the line to the drop centre.
	var lo_scale := 1.0 / sqrt(1.0 + lo * lo)
	var hi_scale := 1.0 / sqrt(1.0 + hi * hi)
	var mx := lo_scale + hi_scale
	var my := lo * lo_scale + hi * hi_scale
	var m := sqrt(mx * mx + my * my)
	mx /= m
	my /= m
	line[0] = ex * mx - ey * my
	line[1] = ey * mx + ex * my
	line[2] = dist * mx - sqrt(maxf(drop_r * drop_r - dist2 * my * my, 0.0))
	line[3] = 0.5 * atan2((hi - lo) * lo_scale * hi_scale, (1.0 + lo * hi) * lo_scale * hi_scale)
	return true


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
