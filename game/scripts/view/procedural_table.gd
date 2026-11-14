class_name ProceduralTable
extends RefCounted
## The game's own table, drawn from the physics outline: what TableView shows when the bought model
## isn't there (CI and TestFlight builds never have it). Correct rather than pretty: the cushion noses
## and jaws, the facings and the holes in the bed are exactly the physics' (spec 6.3, 7.5, 7.6).
##
## One mesh, one surface per material. Each cushion is one profile swept along its nose and round
## both jaws to the tips, narrowing there to the width of the pocket's leather. At each pocket a
## leather fitting stands on the physics facings from tip to tip, round a back that touches the far
## edge of the hole; the hole in the bed is the drop circle, the cloth turns down into it and a net
## bag hangs below. Wooden rails fill the rest out to the rail's outer edge, over a plain frame that
## stands on the stage's floor.

## The cushion's top, from the nose back to the wooden rail.
const CUSHION_DEPTH := 0.04
## The cushions are a shade darker than the bed, and their face under the nose darkens further down
## to UNDERCUT_SHADE at the cloth: the cloth material's rim light ignores which way a face turns from
## the light and would light the undercut.
const CUSHION_SHADE := 0.93
const UNDERCUT_SHADE := 0.5
## Profile points closer than this to a neighbour don't set its normal's slope (_profile_normals).
const PROFILE_STEP := 0.0008
## The fitting's leather from its wall back to the wood, and its rounded lip into the pocket.
const BAND := 0.012
const LIP := 0.004
## Darker down the fitting's wall and the cushions' ends, which the light from above barely reaches.
const WALL_SHADE := 0.55
## The cloth turned down into the hole: how deep, and how dark at its foot. The fittings' walls
## reach as deep, so nothing shows under them where the hole runs on under a facing.
const FALL_DEPTH := 0.016
const FALL_SHADE := 0.4
## The net from the turned cloth down, a bag: each row (its size as a fraction of the hole's, depth
## below the cloth, shade). It sags out under the bed, then closes in to a dark opening below where
## a potted ball comes to rest (BallsView.DROP_DEPTH), never closer to that ball than
## NET_BALL_CLEARANCE.
const NET_ROWS: Array[Vector3] = [
	Vector3(1.08, -0.022, 0.78),
	Vector3(1.06, -0.03, 0.9),
	Vector3(0.98, -0.038, 0.95),
	Vector3(0.88, -0.045, 0.84),
	Vector3(0.78, -0.052, 0.77),
	Vector3(0.7, -0.058, 0.68),
	Vector3(0.62, -0.063, 0.58),
	Vector3(0.54, -0.067, 0.46),
	Vector3(0.45, -0.07, 0.3),
	Vector3(0.38, -0.073, 0.012),
	Vector3(0.28, -0.077, 0.012),
]
const NET_BOTTOM := -0.083
## The net's rim, in the shade of the turned cloth.
const NET_RIM_SHADE := 0.6
const NET_DARK := Color(0.012, 0.012, 0.012)
const NET_BALL_CLEARANCE := 0.0015
const BALL_RADIUS_FALLBACK := 0.02625
## The rails' rounded outer corners and top edge, the apron below it and the frame under the apron,
## a plain box down to the floor.
const CORNER_RADIUS := 0.02
const BEVEL := 0.005
const APRON_BOTTOM := -0.09
const BODY_INSET := 0.03
const BODY_BOTTOM := Stage.FLOOR_Y
const BODY_SHADE := Color(0.32, 0.3, 0.3)
const ARC_STEP := PI / 48.0
const HOLE_SEGMENTS := 32
## Round the back of a pocket, even so its middle point lies on the pocket's axis.
const BACK_SEGMENTS := 24
const CORNER_SEGMENTS := 10
const SHAPE_TOLERANCE := 1e-4

# Tournament green under the stage's light; the cloth texture's nap runs along U, core x, baulk to
# top (spec 6.4). UVs are in metres: each material's uv1_scale sets its texture's tile
# (game/textures/README.md).
const CLOTH_GREEN := Color(0.10, 0.505, 0.145)
const CLOTH_TILE := 0.12
const RAIL_TILE := 2.0
const LEATHER_TILE := 0.45
# Multiplies the rail texture, which is already mahogany: darkened a little, kept warm.
const RAIL_TINT := Color(0.86, 0.78, 0.7)
const FITTING_CREAM := Color(0.78, 0.7, 0.55)
# The crochet net, generated (_net_maps): NET_CELLS diamonds of cord across a NET_TILE tile, so
# each diamond is about 12 mm, as on tournament nets. A small tile keeps generating it quick.
const NET_PIXELS := 64
const NET_CELLS := 2
const NET_TILE := 0.025
# Off-white, and the gaps not black: the real nets read soft, not as white mesh on black.
const NET_CORD := Color(0.88, 0.86, 0.8)
const NET_GAP := Color(0.3, 0.28, 0.25)
# Cord and knot half-widths, in cells, and how much each cord swells along its stitches.
const NET_CORD_WIDTH := 0.14
const NET_KNOT_WIDTH := 0.22
const NET_STITCH := 0.25
const NET_STITCHES := 3.0
const NET_RELIEF := 2.5
const TEXTURES := "res://textures/"

static var _net_cache: Array[ImageTexture] = []


## A cushion's end at a pocket: the arc from the nose round to the tip, where the facing runs on.
class Jaw:
	var pocket := -1
	## From the nose's end to the tip, with each point's normal towards the balls.
	var points := PackedVector2Array()
	var normals := PackedVector2Array()
	## The facing's direction from the tip, out of the table.
	var dir := Vector2.ZERO


class Pocket:
	var name := ""
	var centre := Vector2.ZERO
	var drop_radius := 0.0
	var corner := false
	## Out of the table, between the facings.
	var axis := Vector2.ZERO
	## The fitting's wall from the tip of the cushion before the pocket (anticlockwise) down its
	## facing, round the back and up the other facing to the next cushion's tip, with each point's
	## normal into the pocket; and the leather's outer edge, BAND behind it.
	var wall := PackedVector2Array()
	var wall_normals := PackedVector2Array()
	var outer := PackedVector2Array()
	## A corner's rounded rail corner.
	var corner_centre := Vector2.ZERO
	var corner_out := Vector2.ZERO


## One cushion and its rail, from one pocket to the next, anticlockwise seen from above.
class Section:
	var name := ""
	## The cushion's nose from tip to tip, each point's normal towards the balls, and its back
	## `depths` behind: CUSHION_DEPTH along the nose, narrowing round each jaw to the fitting's BAND
	## at the tip, so the cushion's back runs into the leather's.
	var points := PackedVector2Array()
	var normals := PackedVector2Array()
	var depths := PackedFloat32Array()
	var backs := PackedVector2Array()
	var start: Jaw
	var end: Jaw
	var nose_point := Vector2.ZERO
	var nose_normal := Vector2.ZERO
	var nose_dir := Vector2.ZERO


## Vertex lists for one surface, in Godot space.
class Surf:
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	var colours := PackedColorArray()
	var indices := PackedInt32Array()

	func vertex(p: Vector3, n: Vector3, uv: Vector2, colour: Color = Color.WHITE) -> int:
		verts.append(p)
		normals.append(n)
		uvs.append(uv)
		colours.append(colour)
		return verts.size() - 1

	# Godot's front faces wind clockwise; the vertex normals say which side is the front.
	func tri(a: int, b: int, c: int) -> void:
		var face := (verts[b] - verts[a]).cross(verts[c] - verts[a])
		indices.append(a)
		if face.dot(normals[a] + normals[b] + normals[c]) > 0.0:
			indices.append(c)
			indices.append(b)
		else:
			indices.append(b)
			indices.append(c)

	func grid(first: int, rows: int, cols: int) -> void:
		for r in rows - 1:
			for k in cols - 1:
				var i := first + r * cols + k
				tri(i, i + 1, i + cols + 1)
				tri(i, i + cols + 1, i + cols)

	# Tangents from the UVs, which the normal maps need to lie the right way round.
	func commit(mesh: ArrayMesh, material: Material) -> void:
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = verts
		arrays[Mesh.ARRAY_NORMAL] = normals
		arrays[Mesh.ARRAY_TEX_UV] = uvs
		arrays[Mesh.ARRAY_COLOR] = colours
		arrays[Mesh.ARRAY_INDEX] = indices
		var st := SurfaceTool.new()
		st.create_from_arrays(arrays)
		st.generate_tangents()
		st.commit(mesh)
		mesh.surface_set_material(mesh.get_surface_count() - 1, material)


var _pockets: Array[Pocket] = []
var _sections: Array[Section] = []
var _ball_radius := BALL_RADIUS_FALLBACK


## The table's mesh: cloth, wood, nets, `marks` (TableView's markings) and fittings, in TableView's
## SURFACE_* order. shape is OfsPhysics.table_shape(); `contact` is the height of the cushions' noses.
## Null, with what is wrong added to `problems`, when the outline doesn't join up.
func build(shape: Dictionary, ball_radius: float, contact: float, marks: Surf, problems: PackedStringArray) -> ArrayMesh:
	_ball_radius = ball_radius
	var rail_height := float(shape["rail_height"])
	var rail_width := float(shape["rail_width"])
	_assemble(shape, problems)
	if problems.is_empty():
		for i in _sections.size():
			_shape_pocket(_sections[i], _sections[(i + 1) % _sections.size()], rail_width, problems)
	if not problems.is_empty():
		return null

	var cloth := Surf.new()
	var wood := Surf.new()
	var net := Surf.new()
	var fittings := Surf.new()
	_add_playing_area(cloth)
	for i in _sections.size():
		var section := _sections[i]
		_add_cushion(cloth, section, contact, rail_height)
		var outer := _outer_part(section, rail_width)
		_add_flat(cloth, _bed_piece(section, outer), 0.0, Vector2(1.0, 0.0), "the bed behind '%s'" % section.name)
		_add_flat(wood, _rail_piece(section, outer), rail_height, section.nose_dir, "rail '%s'" % section.name)
	for pocket in _pockets:
		_add_fitting(fittings, pocket, rail_height)
		_add_fall(cloth, pocket)
		_add_net(net, pocket)
	_add_rail_edge(wood, rail_height)
	_add_body(wood, rail_width)

	var mesh := ArrayMesh.new()
	cloth.commit(mesh, _cloth_material())
	wood.commit(mesh, _wood_material())
	net.commit(mesh, _net_material())
	marks.commit(mesh, marking_material())
	fittings.commit(mesh, _fitting_material())
	return mesh


static func g(p: Vector2, z: float) -> Vector3:
	return Vector3(-p.y, z, -p.x)


static func v2(value: Variant) -> Vector2:
	return Vector2(float(value[0]), float(value[1]))


static func dir(angle: float) -> Vector2:
	return Vector2(cos(angle), sin(angle))


func _assemble(shape: Dictionary, problems: PackedStringArray) -> void:
	for p: Variant in shape["pockets"]:
		var pocket := Pocket.new()
		pocket.name = String(p["name"])
		pocket.centre = v2(p["drop_centre"])
		pocket.drop_radius = float(p["drop_radius"])
		_pockets.append(pocket)
	var facings: Array[Dictionary] = []
	var noses: Array[Dictionary] = []
	for l: Variant in shape["lines"]:
		var line := {"name": String(l["name"]), "p1": v2(l["p1"]), "p2": v2(l["p2"]), "normal": v2(l["normal"])}
		if String(l["kind"]) == "nose":
			noses.append(line)
		else:
			facings.append(line)
	var arcs: Array[Dictionary] = []
	for a: Variant in shape["arcs"]:
		arcs.append({"name": String(a["name"]), "centre": v2(a["centre"]), "radius": float(a["radius"]),
				"a0": float(a["angle_start"]), "a1": float(a["angle_end"]), "pocket": int(a["pocket"])})
	if noses.is_empty():
		problems.append("no nose lines")
		return
	var unordered: Array[Section] = []
	for nose in noses:
		var a: Vector2 = nose["p1"]
		var b: Vector2 = nose["p2"]
		var n: Vector2 = nose["normal"]
		# Anticlockwise: the balls are on the left going from a to b.
		if Vector2(a.y - b.y, b.x - a.x).dot(n) < 0.0:
			var swap := a
			a = b
			b = swap
		var section := Section.new()
		section.name = String(nose["name"])
		section.nose_point = a
		section.nose_normal = n
		section.nose_dir = (b - a).normalized()
		section.start = _jaw(a, n, arcs, facings, section.name, problems)
		section.end = _jaw(b, n, arcs, facings, section.name, problems)
		if section.start == null or section.end == null:
			continue
		for i in range(section.start.points.size() - 1, -1, -1):
			_append(section, section.start.points[i], section.start.normals[i], float(i) / (section.start.points.size() - 1))
		for i in section.end.points.size():
			_append(section, section.end.points[i], section.end.normals[i], float(i) / (section.end.points.size() - 1))
		unordered.append(section)
	if not problems.is_empty():
		return
	# Each pocket ends one section and starts the next.
	var current := unordered[0]
	while _sections.size() < unordered.size():
		_sections.append(current)
		var next: Section = null
		for s in unordered:
			if s.start.pocket == current.end.pocket:
				next = s
		if next == null or (next == _sections[0] and _sections.size() < unordered.size()):
			problems.append("the cushions don't close round the table after '%s'" % current.name)
			return
		current = next
	if current != _sections[0]:
		problems.append("the cushions don't close round the table")


## Adds a nose point `round_jaw` of the way from the nose's end (0) to the tip (1).
static func _append(section: Section, p: Vector2, n: Vector2, round_jaw: float) -> void:
	if not section.points.is_empty() and section.points[section.points.size() - 1].distance_to(p) < 1e-6:
		return
	var depth := lerpf(CUSHION_DEPTH, BAND, smoothstep(0.0, 1.0, round_jaw))
	section.points.append(p)
	section.normals.append(n)
	section.depths.append(depth)
	section.backs.append(p - n * depth)


## The jaw at a nose's end: its arc and the facing that runs on from the tip. Null, with a problem
## recorded, when the outline doesn't join up.
func _jaw(nose_end: Vector2, nose_normal: Vector2, arcs: Array[Dictionary], facings: Array[Dictionary],
		nose_name: String, problems: PackedStringArray) -> Jaw:
	for arc in arcs:
		var c: Vector2 = arc["centre"]
		var r: float = arc["radius"]
		var near_angle: float
		var far_angle: float
		if (c + dir(arc["a0"]) * r).distance_to(nose_end) < SHAPE_TOLERANCE:
			near_angle = arc["a0"]
			far_angle = arc["a1"]
		elif (c + dir(arc["a1"]) * r).distance_to(nose_end) < SHAPE_TOLERANCE:
			near_angle = arc["a1"]
			far_angle = arc["a0"]
		else:
			continue
		var jaw := Jaw.new()
		jaw.pocket = arc["pocket"]
		if jaw.pocket < 0 or jaw.pocket >= _pockets.size():
			problems.append("arc '%s' names pocket %d of %d" % [arc["name"], jaw.pocket, _pockets.size()])
			return null
		if r <= CUSHION_DEPTH:
			problems.append("arc '%s' is tighter than the cushion is deep" % arc["name"])
			return null
		var tip := c + dir(far_angle) * r
		var found := false
		for f in facings:
			for ends: Array in [["p1", "p2"], ["p2", "p1"]]:
				if (f[ends[0]] as Vector2).distance_to(tip) < SHAPE_TOLERANCE:
					jaw.dir = ((f[ends[1]] as Vector2) - tip).normalized()
					found = true
		if not found:
			problems.append("no facing meets arc '%s'" % arc["name"])
			return null
		var side := 1.0 if dir(near_angle).dot(nose_normal) >= 0.0 else -1.0
		var span := wrapf(far_angle - near_angle, -PI, PI)
		var steps := maxi(2, ceili(absf(span) / ARC_STEP))
		for i in steps + 1:
			var radial := dir(near_angle + span * i / steps)
			jaw.points.append(c + radial * r)
			jaw.normals.append(radial * side)
		return jaw
	problems.append("no jaw arc meets nose '%s' at %s" % [nose_name, nose_end])
	return null


## The pocket between sections a and b: its axis, its fitting's wall and its rail corner.
# The back is the circle that touches both facings and the far edge of the drop circle, so the wall
# turns smoothly from each facing round the back with the whole hole in front of it. The hole in the
# bed may run on a few millimetres under the facings, as it does on the bought table.
func _shape_pocket(a: Section, b: Section, rail_width: float, problems: PackedStringArray) -> void:
	var pocket := _pockets[a.end.pocket]
	pocket.corner = a.nose_normal.dot(b.nose_normal) < 0.5
	pocket.axis = (a.end.dir + b.start.dir).normalized()
	var tip_a := a.points[a.points.size() - 1]
	var tip_b := b.points[0]
	var n_a := a.normals[a.normals.size() - 1]
	var n_b := b.normals[0]
	var gap_a := (pocket.centre - tip_a).dot(n_a)
	var gap_b := (pocket.centre - tip_b).dot(n_b)
	var closing := 1.0 + pocket.axis.dot(n_a)
	if absf(gap_a - gap_b) > SHAPE_TOLERANCE or closing < 0.1:
		problems.append("pocket '%s': its facings aren't a matching pair about its axis" % pocket.name)
		return
	var back := (pocket.drop_radius - gap_a) / closing
	var radius := gap_a + back * pocket.axis.dot(n_a)
	var centre := pocket.centre + pocket.axis * back
	var touch_a := centre - n_a * radius
	var touch_b := centre - n_b * radius
	if radius < BAND or (touch_a - tip_a).dot(a.end.dir) < 0.0 or (touch_b - tip_b).dot(b.start.dir) < 0.0:
		problems.append("pocket '%s': no round back fits between its facings behind its hole" % pocket.name)
		return
	pocket.wall = PackedVector2Array([tip_a])
	pocket.wall_normals = PackedVector2Array([n_a])
	for d in _sweep_dirs(-n_a, -n_b, pocket.axis, BACK_SEGMENTS):
		pocket.wall.append(centre + d * radius)
		pocket.wall_normals.append(-d)
	pocket.wall.append(tip_b)
	pocket.wall_normals.append(n_b)
	for k in pocket.wall.size():
		pocket.outer.append(pocket.wall[k] - pocket.wall_normals[k] * BAND)
	var room := minf(_inside(a, centre, rail_width), _inside(b, centre, rail_width))
	if radius + BAND > room - BEVEL:
		problems.append("pocket '%s': its fitting reaches past the rail's outer edge" % pocket.name)
		return
	if pocket.corner:
		var na := a.nose_normal
		var nb := b.nose_normal
		var ca := a.nose_point.dot(na) - rail_width
		var cb := b.nose_point.dot(nb) - rail_width
		var det := na.x * nb.y - na.y * nb.x
		var corner := Vector2((ca * nb.y - na.y * cb) / det, (na.x * cb - ca * nb.x) / det)
		pocket.corner_centre = corner + (na + nb) * CORNER_RADIUS
		pocket.corner_out = -(na + nb).normalized()


## How far p is inside the section's rail's outer edge.
static func _inside(section: Section, p: Vector2, rail_width: float) -> float:
	return (p - section.nose_point).dot(section.nose_normal) + rail_width


## Directions from `from` round to `to` the way that passes `via`, count steps.
static func _sweep_dirs(from: Vector2, to: Vector2, via: Vector2, count: int) -> PackedVector2Array:
	var start := from.angle()
	var turn := fposmod(to.angle() - start, TAU)
	if fposmod(via.angle() - start, TAU) > turn:
		turn -= TAU
	var out := PackedVector2Array()
	for i in count + 1:
		out.append(dir(start + turn * i / count))
	return out


## Where a pocket's axis leaves the rail top, BEVEL in from the outer edge: on the mitre of a corner's
## rounded rail corner, or square behind a middle pocket.
func _axis_end(pocket: Pocket, section: Section, rail_width: float) -> Vector2:
	if pocket.corner:
		return pocket.corner_centre + pocket.corner_out * (CORNER_RADIUS - BEVEL)
	return pocket.centre - section.nose_normal * (_inside(section, pocket.centre, rail_width) - BEVEL)


## The rail top's outer edge behind a section, BEVEL in from the rail's outer edge, from the axis of
## the pocket the section ends at back to the axis of the one it starts at.
func _outer_part(section: Section, rail_width: float) -> PackedVector2Array:
	var out := PackedVector2Array()
	var half := int(CORNER_SEGMENTS / 2.0)
	var end := _pockets[section.end.pocket]
	var start := _pockets[section.start.pocket]
	out.append(_axis_end(end, section, rail_width))
	if end.corner:
		for i in range(1, half + 1):
			out.append(end.corner_centre + end.corner_out.slerp(-section.nose_normal, float(i) / half) * (CORNER_RADIUS - BEVEL))
	if start.corner:
		for i in range(half, 0, -1):
			out.append(start.corner_centre + start.corner_out.slerp(-section.nose_normal, float(i) / half) * (CORNER_RADIUS - BEVEL))
	out.append(_axis_end(start, section, rail_width))
	return out


## The wooden rail top behind a section: from the middle of the back of one pocket's fitting, along
## the cushion's back to the middle of the next, and back along the rail's outer edge.
func _rail_piece(section: Section, outer: PackedVector2Array) -> PackedVector2Array:
	var start := _pockets[section.start.pocket]
	var end := _pockets[section.end.pocket]
	var out := PackedVector2Array()
	var middle := int(start.outer.size() / 2.0)
	for k in range(middle, start.outer.size()):
		out.append(start.outer[k])
	out.append_array(section.backs)
	for k in middle + 1:
		out.append(end.outer[k])
	out.append_array(outer)
	return out


## The bed behind a section's nose line, out under the cushion and rail, between the axes of its two
## pockets and round the near half of each one's hole. With the playing area these pieces tile the
## bed with the drop circles left out.
func _bed_piece(section: Section, outer: PackedVector2Array) -> PackedVector2Array:
	var start := _pockets[section.start.pocket]
	var end := _pockets[section.end.pocket]
	var out := PackedVector2Array([_axis_foot(start, section)])
	out.append(_axis_foot(end, section))
	var half := int(HOLE_SEGMENTS / 2.0)
	for d in _sweep_dirs(-end.axis, end.axis, -section.nose_dir, half):
		out.append(end.centre + d * end.drop_radius)
	out.append_array(outer)
	for d in _sweep_dirs(start.axis, -start.axis, section.nose_dir, half):
		out.append(start.centre + d * start.drop_radius)
	return out


## Where a pocket's axis crosses the edge of the playing area: the corner point, or the nose line.
static func _axis_foot(pocket: Pocket, section: Section) -> Vector2:
	var behind := (pocket.centre - section.nose_point).dot(section.nose_normal)
	return pocket.centre - pocket.axis * (behind / pocket.axis.dot(section.nose_normal))


## The bed inside the nose lines, where no hole reaches.
func _add_playing_area(cloth: Surf) -> void:
	# Every pocket's foot is a vertex, the middles' too, so the pieces behind share whole edges with it.
	var feet := PackedVector2Array()
	for section in _sections:
		feet.append(_axis_foot(_pockets[section.end.pocket], section))
	_add_flat(cloth, feet, 0.0, Vector2(1.0, 0.0), "the playing area")


## A flat polygon at height z, its texture's U along `along`.
func _add_flat(surf: Surf, outline: PackedVector2Array, z: float, along: Vector2, what: String) -> void:
	var polygon := _cleaned(outline)
	var triangles := Geometry2D.triangulate_polygon(polygon)
	if triangles.is_empty():
		Log.error("app", "app.error", {"kind": "message",
				"message": "procedural_table: %s doesn't triangulate (%d points)" % [what, polygon.size()]})
		return
	var first := surf.verts.size()
	for p in polygon:
		surf.vertex(g(p, z), Vector3.UP, Vector2(p.dot(along), p.dot(along.orthogonal())))
	for i in range(0, triangles.size(), 3):
		surf.tri(first + triangles[i], first + triangles[i + 1], first + triangles[i + 2])


static func _cleaned(points: PackedVector2Array) -> PackedVector2Array:
	var out := PackedVector2Array()
	for p in points:
		if out.is_empty() or out[out.size() - 1].distance_to(p) > 1e-6:
			out.append(p)
	if out.size() > 1 and out[0].distance_to(out[out.size() - 1]) < 1e-6:
		out.remove_at(out.size() - 1)
	return out


## Normals for a profile of (s, z) points that runs from its outer top edge towards the visible
## side, s growing away from it: each normal is (along s, up). Neighbours closer than PROFILE_STEP
## are stepped over, so a sliver of a step doesn't turn a flat top on its side.
static func _profile_normals(profile: PackedVector2Array) -> PackedVector2Array:
	var out := PackedVector2Array()
	var last := profile.size() - 1
	for j in profile.size():
		var next := j
		while next < last and profile[next].distance_to(profile[j]) < PROFILE_STEP:
			next += 1
		var previous := j
		while previous > 0 and profile[previous].distance_to(profile[j]) < PROFILE_STEP:
			previous -= 1
		var t := profile[next] - profile[previous]
		out.append(Vector2(t.y, -t.x).normalized())
	return out


## A profile swept along a path: row j is the path moved profile[j].x against `vis`, the horizontal
## direction each of its points faces, at height profile[j].y. shades, when given, colours each row;
## reach, when given, is how far back each path point's profile may go.
func _sweep(surf: Surf, path: PackedVector2Array, vis: PackedVector2Array, profile: PackedVector2Array,
		colour: Color = Color.WHITE, shades := PackedColorArray(), reach := PackedFloat32Array()) -> void:
	var first := surf.verts.size()
	var normals := _profile_normals(profile)
	var along := PackedFloat32Array()
	var u := 0.0
	for k in path.size():
		if k > 0:
			u += path[k].distance_to(path[k - 1])
		along.append(u)
	var v := 0.0
	for j in profile.size():
		if j > 0:
			v += profile[j].distance_to(profile[j - 1])
		for k in path.size():
			var h := -vis[k] * normals[j].x
			var back := profile[j].x if reach.is_empty() else minf(profile[j].x, reach[k])
			surf.vertex(g(path[k] - vis[k] * back, profile[j].y), Vector3(-h.y, normals[j].y, -h.x).normalized(),
					Vector2(along[k], v), colour if shades.is_empty() else shades[j])
	surf.grid(first, profile.size(), path.size())


## A profile's cross-section as a flat end face at `at`, facing `out`: the profile closed down to
## `bottom` behind its deepest point.
func _add_end(surf: Surf, at: Vector2, vis: Vector2, out: Vector2, profile: PackedVector2Array, bottom: float,
		colour: Color, what: String) -> void:
	var outline := profile.duplicate()
	var deepest := 0.0
	for p in profile:
		deepest = maxf(deepest, p.x)
	outline.append(Vector2(deepest, bottom))
	if absf(profile[profile.size() - 1].y - bottom) > 1e-6:
		outline.append(Vector2(profile[profile.size() - 1].x, bottom))
	outline = _cleaned(outline)
	var triangles := Geometry2D.triangulate_polygon(outline)
	if triangles.is_empty():
		Log.error("app", "app.error", {"kind": "message", "message": "procedural_table: %s doesn't triangulate" % what})
		return
	var normal := g(out, 0.0)
	var first := surf.verts.size()
	for p in outline:
		surf.vertex(g(at - vis * p.x, p.y), normal, p, colour)
	for i in range(0, triangles.size(), 3):
		surf.tri(first + triangles[i], first + triangles[i + 1], first + triangles[i + 2])


## The cushion's cross-section, (s behind the nose, height): its top rising a little from the
## rounded nose to the rail, flush with it, and below the nose the face cut back under it to the
## cloth. A ball against the nose clears the face by about 7 mm at its equator.
static func cushion_profile(contact: float, rail_height: float) -> PackedVector2Array:
	return PackedVector2Array([
		Vector2(CUSHION_DEPTH, rail_height),
		Vector2(0.024, rail_height - 0.0006),
		Vector2(0.010, rail_height - 0.0011),
		Vector2(0.0045, minf(contact + 0.0018, rail_height - 0.0014)),
		Vector2(0.0015, minf(contact + 0.0011, rail_height - 0.0018)),
		Vector2(0.0, contact),
		Vector2(0.0013, contact - 0.0012),
		Vector2(0.0042, contact - 0.0026),
		Vector2(0.0075, contact * 0.78),
		Vector2(0.0156, contact * 0.4),
		Vector2(0.024, 0.0),
	])


## The cushion, one profile from tip to tip. At each tip it is as deep as the fitting that takes
## over there, whose end closes it.
func _add_cushion(cloth: Surf, section: Section, contact: float, rail_height: float) -> void:
	var profile := cushion_profile(contact, rail_height)
	var shades := PackedColorArray()
	for p in profile:
		var shade := CUSHION_SHADE * lerpf(1.0, UNDERCUT_SHADE, clampf((contact - p.y) / contact, 0.0, 1.0))
		shades.append(Color(shade, shade, shade))
	_sweep(cloth, section.points, section.normals, profile, Color.WHITE, shades, section.depths)


## The fitting's cross-section, (s behind its wall, height): its flat top, the lip rounding over into
## the pocket and the wall down past the turned cloth.
static func fitting_profile(rail_height: float) -> PackedVector2Array:
	var out := PackedVector2Array([Vector2(BAND, rail_height)])
	for i in 5:
		var angle := PI / 2.0 * i / 4.0
		out.append(Vector2(LIP * (1.0 - sin(angle)), rail_height - LIP + LIP * cos(angle)))
	out.append(Vector2(0.0, -FALL_DEPTH))
	return out


## The leather fitting: a band standing on the facings and round the back of the pocket, its wall
## the physics' facing from each cushion's tip.
func _add_fitting(fittings: Surf, pocket: Pocket, rail_height: float) -> void:
	var profile := fitting_profile(rail_height)
	var shades := PackedColorArray()
	for p in profile:
		var shade := lerpf(WALL_SHADE, 1.0, clampf((p.y - (rail_height - LIP)) / LIP, 0.0, 1.0))
		shades.append(Color(shade, shade, shade))
	_sweep(fittings, pocket.wall, pocket.wall_normals, profile, Color.WHITE, shades)
	# Its two ends, under the cushions' undercut noses.
	var end_shade := Color(WALL_SHADE, WALL_SHADE, WALL_SHADE)
	var last := pocket.wall.size() - 1
	var into_a := (pocket.wall[0] - pocket.wall[1]).normalized()
	var into_b := (pocket.wall[last] - pocket.wall[last - 1]).normalized()
	_add_end(fittings, pocket.wall[0], pocket.wall_normals[0], into_a, profile, -FALL_DEPTH, end_shade,
			"fitting '%s' start" % pocket.name)
	_add_end(fittings, pocket.wall[last], pocket.wall_normals[last], into_b, profile, -FALL_DEPTH, end_shade,
			"fitting '%s' end" % pocket.name)


## Directions round a hole, closed: the last is the first again.
static func _ring_dirs() -> PackedVector2Array:
	var out := PackedVector2Array()
	for k in HOLE_SEGMENTS + 1:
		out.append(dir(TAU * (k % HOLE_SEGMENTS) / HOLE_SEGMENTS))
	return out


## The cloth turned down round the edge of the hole, darkening to its foot.
func _add_fall(cloth: Surf, pocket: Pocket) -> void:
	var dirs := _ring_dirs()
	var first := cloth.verts.size()
	for row in 2:
		var shade := 1.0 if row == 0 else FALL_SHADE
		var along := 0.0
		for k in dirs.size():
			if k > 0:
				along += TAU * pocket.drop_radius / HOLE_SEGMENTS
			cloth.vertex(g(pocket.centre + dirs[k] * pocket.drop_radius, -FALL_DEPTH * row), g(-dirs[k], 0.0),
					Vector2(along, FALL_DEPTH * row), Color(shade, shade, shade))
	cloth.grid(first, 2, dirs.size())


## The net hanging in the hole from the turned cloth, a bag curving in to its dark opening: light in
## its bowl, darker as it closes, and never closer to a resting potted ball than NET_BALL_CLEARANCE.
func _add_net(net: Surf, pocket: Pocket) -> void:
	var dirs := _ring_dirs()
	var columns := dirs.size()
	# A whole number of tiles round the ring, so the crochet closes without a seam.
	var round := TAU * pocket.drop_radius
	var tile := maxf(1.0, roundf(round / NET_TILE)) * NET_TILE / round
	var rows: Array[PackedVector3Array] = []
	var shades := PackedFloat32Array([NET_RIM_SHADE])
	var sizes := PackedFloat32Array([1.0])
	var top := PackedVector3Array()
	for d in dirs:
		top.append(g(pocket.centre + d * pocket.drop_radius, -FALL_DEPTH))
	rows.append(top)
	var potted := _ball_radius - BallsView.DROP_DEPTH
	for row in NET_ROWS:
		var below := row.y - potted
		var ball := sqrt(maxf(0.0, _ball_radius * _ball_radius - below * below))
		var across := maxf(pocket.drop_radius * row.x, ball + NET_BALL_CLEARANCE)
		var points := PackedVector3Array()
		for d in dirs:
			points.append(g(pocket.centre + d * across, row.y))
		rows.append(points)
		shades.append(row.z)
		sizes.append(across / pocket.drop_radius)
	var first := net.verts.size()
	var last_row := rows.size() - 1
	var down := 0.0
	for j in rows.size():
		if j > 0:
			# As the net closes, its diamonds shrink the same way down as round, as in a real net.
			down += rows[j][0].distance_to(rows[j - 1][0]) / (0.5 * (sizes[j] + sizes[j - 1]))
		# The bag's section, (out from the centre, up), from the row above to the row below.
		var above := maxi(j - 1, 0)
		var below := mini(j + 1, last_row)
		var slope := Vector2((sizes[below] - sizes[above]) * pocket.drop_radius, rows[below][0].y - rows[above][0].y)
		var inward := Vector2(slope.y, -slope.x).normalized()
		for k in columns:
			var h := dirs[k] * inward.x
			net.vertex(rows[j][k], Vector3(-h.y, inward.y, -h.x), Vector2(round * k / HOLE_SEGMENTS * tile, down),
					Color(shades[j], shades[j], shades[j]))
	net.grid(first, rows.size(), columns)
	var last := rows[rows.size() - 1]
	var middle := net.vertex(g(pocket.centre, NET_BOTTOM), Vector3.UP, Vector2.ZERO, NET_DARK)
	for k in columns:
		net.vertex(last[k], Vector3.UP, Vector2(float(k) / HOLE_SEGMENTS, 0.0), NET_DARK)
	for k in columns - 1:
		net.tri(middle, middle + 1 + k, middle + 2 + k)


## The rails' rounded top edge and the apron below it, once round the table.
func _add_rail_edge(wood: Surf, rail_height: float) -> void:
	var path := PackedVector2Array()
	var outward := PackedVector2Array()
	for i in _sections.size():
		var pocket := _pockets[_sections[i].end.pocket]
		if not pocket.corner:
			continue
		var from := -_sections[i].nose_normal
		var to := -_sections[(i + 1) % _sections.size()].nose_normal
		for k in CORNER_SEGMENTS + 1:
			var d := from.slerp(to, float(k) / CORNER_SEGMENTS)
			path.append(pocket.corner_centre + d * CORNER_RADIUS)
			outward.append(d)
	if path.is_empty():
		return
	path.append(path[0])
	outward.append(outward[0])
	var profile := PackedVector2Array([Vector2(BEVEL, rail_height), Vector2(0.0016, rail_height - 0.0006),
			Vector2(0.0003, rail_height - 0.0024), Vector2(0.0, rail_height - BEVEL), Vector2(0.0, APRON_BOTTOM)])
	_sweep(wood, path, outward, profile)


## The frame under the rails, set in from the apron and darker.
func _add_body(wood: Surf, rail_width: float) -> void:
	var corners := PackedVector2Array()
	for section in _sections:
		var pocket := _pockets[section.end.pocket]
		if pocket.corner:
			corners.append(_axis_foot(pocket, section) + pocket.corner_out * (rail_width - BODY_INSET) * sqrt(2.0))
	for i in corners.size():
		var a := corners[i]
		var b := corners[(i + 1) % corners.size()]
		var along := (b - a).normalized()
		var out := Vector2(along.y, -along.x)
		if out.dot(a + b) < 0.0:
			out = -out
		var n := g(out, 0.0)
		var length := a.distance_to(b)
		var first := wood.vertex(g(a, APRON_BOTTOM), n, Vector2(0.0, 0.0), BODY_SHADE)
		wood.vertex(g(b, APRON_BOTTOM), n, Vector2(length, 0.0), BODY_SHADE)
		wood.vertex(g(b, BODY_BOTTOM), n, Vector2(length, APRON_BOTTOM - BODY_BOTTOM), BODY_SHADE)
		wood.vertex(g(a, BODY_BOTTOM), n, Vector2(0.0, APRON_BOTTOM - BODY_BOTTOM), BODY_SHADE)
		wood.tri(first, first + 1, first + 2)
		wood.tri(first, first + 2, first + 3)


func _cloth_material() -> StandardMaterial3D:
	var m := _textured("cloth/cloth", CLOTH_TILE, 0.85, 0.3)
	m.albedo_color = CLOTH_GREEN
	# The cushions are a shade darker than the bed through their vertex colour.
	m.vertex_color_use_as_albedo = true
	# Rim light stands in for the cloth's fuzz catching the light at grazing angles.
	m.rim_enabled = true
	m.rim = 0.25
	m.rim_tint = 0.6
	return m


func _wood_material() -> StandardMaterial3D:
	var m := _textured("rail/rail", RAIL_TILE, 0.6, 0.3)
	m.albedo_color = RAIL_TINT
	# The body under the rails is darker through its vertex colour.
	m.vertex_color_use_as_albedo = true
	m.clearcoat_enabled = true
	# A satin lacquer: a mirror finish showed the canopy as a hard white streak along the rails.
	m.clearcoat = 0.2
	m.clearcoat_roughness = 0.3
	m.metallic_specular = 0.3
	return m


func _net_material() -> StandardMaterial3D:
	var maps := _net_maps()
	var m := StandardMaterial3D.new()
	m.albedo_texture = maps[0]
	m.normal_enabled = true
	m.normal_texture = maps[1]
	m.normal_scale = 1.0
	# Cotton cord, as matt as the cloth.
	m.roughness = 1.0
	m.roughness_texture = _texture("cloth/cloth_roughness.jpg")
	m.roughness_texture_channel = BaseMaterial3D.TEXTURE_CHANNEL_RED
	m.metallic_specular = 0.2
	# Shaded darker down to its opening through the vertex colour.
	m.vertex_color_use_as_albedo = true
	m.uv1_scale = Vector3(1.0 / NET_TILE, 1.0 / NET_TILE, 1.0)
	m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	return m


## The crochet net's albedo and normal map, generated once: diamonds of round cord with a knot where
## the cords cross, light cord over the dark below the table.
static func _net_maps() -> Array[ImageTexture]:
	if not _net_cache.is_empty():
		return _net_cache
	var size := NET_PIXELS
	var heights := PackedFloat32Array()
	heights.resize(size * size)
	for y in size:
		for x in size:
			var u := (x + 0.5) * NET_CELLS / size
			var v := (y + 0.5) * NET_CELLS / size
			var cord_a := absf(fposmod(u + v + 0.5, 1.0) - 0.5) / sqrt(2.0)
			var cord_b := absf(fposmod(u - v + 0.5, 1.0) - 0.5) / sqrt(2.0)
			var knot := minf(Vector2(fposmod(u + 0.5, 1.0) - 0.5, fposmod(v + 0.5, 1.0) - 0.5).length(),
					Vector2(fposmod(u, 1.0) - 0.5, fposmod(v, 1.0) - 0.5).length())
			# Crochet stitches: each cord swells and narrows along its length.
			var stitch_a := 1.0 + NET_STITCH * cos(TAU * NET_STITCHES * (u - v))
			var stitch_b := 1.0 + NET_STITCH * cos(TAU * NET_STITCHES * (u + v))
			var h := maxf(maxf(_bump(cord_a, NET_CORD_WIDTH * stitch_a), _bump(cord_b, NET_CORD_WIDTH * stitch_b)),
					_bump(knot, NET_KNOT_WIDTH))
			heights[y * size + x] = h
	var albedo := PackedByteArray()
	var normal := PackedByteArray()
	albedo.resize(size * size * 3)
	normal.resize(size * size * 3)
	for y in size:
		for x in size:
			var h := heights[y * size + x]
			var colour := NET_GAP.lerp(NET_CORD * (0.7 + 0.3 * h), clampf(h * 4.0, 0.0, 1.0))
			var dx := heights[y * size + (x + 1) % size] - heights[y * size + (x + size - 1) % size]
			var dy := heights[((y + 1) % size) * size + x] - heights[((y + size - 1) % size) * size + x]
			# OpenGL style, green up: up in the image is towards row 0.
			var n := Vector3(-dx * NET_RELIEF, dy * NET_RELIEF, 1.0).normalized() * 0.5 + Vector3.ONE * 0.5
			var i := (y * size + x) * 3
			albedo[i] = colour.r8
			albedo[i + 1] = colour.g8
			albedo[i + 2] = colour.b8
			normal[i] = int(n.x * 255.0)
			normal[i + 1] = int(n.y * 255.0)
			normal[i + 2] = int(n.z * 255.0)
	var albedo_image := Image.create_from_data(size, size, false, Image.FORMAT_RGB8, albedo)
	albedo_image.generate_mipmaps()
	var normal_image := Image.create_from_data(size, size, false, Image.FORMAT_RGB8, normal)
	normal_image.generate_mipmaps(true)
	_net_cache = [ImageTexture.create_from_image(albedo_image), ImageTexture.create_from_image(normal_image)]
	return _net_cache


## A round cord's height across it: 1 on its centre line, 0 at `width` out.
static func _bump(distance: float, width: float) -> float:
	return sqrt(maxf(0.0, 1.0 - pow(distance / width, 2.0)))


func _fitting_material() -> StandardMaterial3D:
	var m := _textured("leather/leather", LEATHER_TILE, 0.9, 0.6)
	# The leather scan's grain and sheen, in cream rather than its dark brown.
	m.albedo_texture = null
	m.albedo_color = FITTING_CREAM
	m.vertex_color_use_as_albedo = true
	m.clearcoat_enabled = true
	m.clearcoat = 0.25
	m.clearcoat_roughness = 0.4
	return m


## A material from <stem>_albedo, _normal and _roughness.jpg under TEXTURES, tiled every `tile`
## metres. A map that doesn't load is logged and left out.
func _textured(stem: String, tile: float, roughness: float, normal_scale: float) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_texture = _texture(stem + "_albedo.jpg")
	var normal := _texture(stem + "_normal.jpg")
	if normal != null:
		m.normal_enabled = true
		m.normal_texture = normal
		m.normal_scale = normal_scale
	m.roughness = roughness
	m.roughness_texture = _texture(stem + "_roughness.jpg")
	m.roughness_texture_channel = BaseMaterial3D.TEXTURE_CHANNEL_RED
	m.uv1_scale = Vector3(1.0 / tile, 1.0 / tile, 1.0)
	m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	return m


static func _texture(file: String) -> Texture2D:
	var path := TEXTURES + file
	var texture := load(path) as Texture2D
	if texture == null:
		Log.error("app", "app.error", {"kind": "message",
				"message": "procedural_table: %s didn't load; the table is drawn without it" % path, "file": path})
	return texture


## The baulk line, D and spots: pale lines on the cloth.
static func marking_material() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.46, 0.52, 0.46)
	m.roughness = 1.0
	m.metallic_specular = 0.1
	return m
