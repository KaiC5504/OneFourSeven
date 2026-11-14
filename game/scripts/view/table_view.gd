class_name TableView
extends Node3D
## The table (spec 6, 13.1) drawn from the physics outline, so what you see is what the balls hit:
## cloth with a subtle nap, cushions, curved jaws, pockets, rails, the baulk line, the D and spots.
##
## Everything static is one mesh, one surface per material (the SURFACE_* indices), so the table
## costs five draw calls. Each pocket also gets a Marker3D "Pocket_<name>" at its drop centre on
## the cloth, with metas "pocket" (its index) and "drop_radius".

const SURFACE_CLOTH := 0
const SURFACE_WOOD := 1
## The pockets' lining.
const SURFACE_LEATHER := 2
const SURFACE_MARKINGS := 3
## The cream leather caps over the rail round the back of each pocket.
const SURFACE_FITTINGS := 4

## The cushion rubber's top, from the nose back to the wooden rail.
const CUSHION_DEPTH := 0.05
## How far the bed runs on under the cushions, so no edge of it can show.
const BED_UNDER := 0.02
## The slate's rounded edge, where the cloth turns down into the pocket.
const FALL := 0.004
## The pocket's lining drops this far below the cloth, well past where a potted ball is hidden
## (BallsView.DROP_DEPTH), and turns black on the way down so the hole reads deep from any angle.
const HOLE_DEPTH := 0.16
const LINING_FADE := 0.09
const LINING_TOP := 0.9
## The fitting's rounded lip into the hole.
const LIP := 0.004
## What's left of a middle pocket's fitting behind its hole, past the lip and the rail's rounded edge.
## The physics' drop circle reaches past the rail's outer edge there, so the hole drawn behind the
## fall is shallower than the circle; a potted ball sinks under the lip.
const MIN_CAP := 0.001
## A middle pocket's fitting: a band this wide round the back of its hole.
const BAND := 0.022
## The rail's rounded outer corner under a corner pocket's fitting.
const CORNER_RADIUS := 0.05
## The fittings wrap over the rail's edge and this far down the apron.
const CAP_WRAP := 0.03
## The fittings' top rises this much above the rail, so the canopy shades them like padded leather.
const DOME := 0.004
const DOME_ROWS := 6
const DOME_COLUMNS := 48
## The fittings' edges, the lip and the wrap over the rail, shaded down from the dome's crown: from
## above, under a light straight down, a dome this low barely shades itself.
const FITTING_EDGE_SHADE := 0.72
const BEVEL := 0.005
const APRON_BOTTOM := -0.09
const BODY_INSET := 0.03
const BODY_BOTTOM := -0.7
const MARK_HALF_WIDTH := 0.0015
const MARK_LIFT := 0.0003
const SPOT_RADIUS := 0.004
const ARC_STEP := PI / 48.0
const POCKET_SEGMENTS := 24
const BAND_SEGMENTS := 8
const CORNER_SEGMENTS := 10
const CONTACT_HEIGHT_FALLBACK := 0.03675
const SHAPE_TOLERANCE := 1e-4
const SPOT_COLOURS: Array[String] = ["yellow", "green", "brown", "blue", "pink", "black"]

# UVs are in metres; each material's uv1_scale sets its texture's tile (game/textures/README.md).
const METRES := Vector2.ONE
# Tournament green; the cloth texture's nap runs along U, core x, baulk to top (spec 6.4).
const CLOTH_GREEN := Color(0.09, 0.46, 0.13)
const CLOTH_TILE := 0.12
const RAIL_TILE := 2.0
const LEATHER_TILE := 0.45
# Multiplies the rail texture: dark polished mahogany rather than the scan's orange-brown.
const RAIL_TINT := Color(0.66, 0.5, 0.32)
# Cream pocket leathers, as on TV tournament tables.
const FITTING_CREAM := Color(0.78, 0.7, 0.55)
const TEXTURES := "res://textures/"

var _pockets: Array[PocketInfo] = []
var _sections: Array[Section] = []
var _bounds := AABB()
var _mesh_instance: MeshInstance3D


class PocketInfo:
	var name := ""
	var centre := Vector2.ZERO
	var drop_radius := 0.0
	## Half the gap between the facings: the hole drawn behind the fall.
	var hole_radius := 0.0
	## Out of the table along the facings.
	var axis := Vector2.ZERO
	## How far the hole drawn behind the fall reaches back along the axis: hole_radius, or less
	## where the rail behind it is narrow (the middles).
	var hole_back := 0.0
	## Where two rails meet, rather than in the middle of one.
	var corner := false
	## The rail's outer edge under the fitting, from the rail before the pocket round to the one
	## after it, with each point's outward direction.
	var edge := PackedVector2Array()
	var edge_out := PackedVector2Array()


## One cushion and its rail, from one pocket to the next, anticlockwise seen from above.
class Section:
	var name := ""
	## The outline from the start pocket's back (where its facing meets the hole) round the jaws
	## and along the nose to the end pocket's; normals point to the play side or the opening.
	var points := PackedVector2Array()
	var normals := PackedVector2Array()
	var start_pocket := -1
	var end_pocket := -1
	## From each pocket's centre towards this section's facing.
	var start_u := Vector2.ZERO
	var end_u := Vector2.ZERO
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


## table_shape.json's text as OfsPhysics.table_shape() returns it, for when the binding isn't
## configured (the look preview and tests). Empty when the text isn't a table shape.
static func shape_from_json(text: String) -> Dictionary:
	var json := JSON.new()
	if json.parse(text) != OK or not json.data is Dictionary:
		return {}
	var data: Dictionary = json.data
	if not (data.has("lines") and data.has("arcs") and data.has("pockets")):
		return {}
	var pocket_index := {}
	var pockets: Array[Dictionary] = []
	for p: Dictionary in data["pockets"]:
		pocket_index[String(p["name"])] = pockets.size()
		pockets.append({"name": p["name"], "corner": p["corner"], "drop_centre": p["drop_centre"],
				"drop_radius": p["drop_radius"]})
	var arcs: Array[Dictionary] = []
	for a: Dictionary in data["arcs"]:
		arcs.append({"name": a["name"], "centre": a["centre"], "radius": a["radius"],
				"angle_start": a["angle_start"], "angle_end": a["angle_end"],
				"pocket": int(pocket_index.get(String(a["pocket"]), -1))})
	var lines: Array[Dictionary] = []
	for l: Dictionary in data["lines"]:
		lines.append({"name": l["name"], "kind": l["kind"], "p1": l["p1"], "p2": l["p2"], "normal": l["normal"]})
	return {"lines": lines, "arcs": arcs, "pockets": pockets,
			"rail_height": data["rail_height_m"], "rail_width": data["rail_width_m"]}


## shape is OfsPhysics.table_shape(); dims is OfsTable.dimensions().
func build(shape: Dictionary, dims: Dictionary) -> void:
	for child in get_children():
		remove_child(child)
		child.queue_free()
	_mesh_instance = null
	var problems := PackedStringArray()
	for key: String in ["lines", "arcs", "pockets", "rail_height", "rail_width"]:
		if not shape.has(key):
			problems.append("shape has no '%s'" % key)
	for key: String in ["playing_length", "playing_width", "baulk_line_x", "d_radius"]:
		if not dims.has(key):
			problems.append("dims has no '%s'" % key)
	if problems.is_empty():
		_assemble(shape, problems)
	if not problems.is_empty():
		_fail(problems)
		return
	var rail_height := float(shape["rail_height"])
	var rail_width := float(shape["rail_width"])
	var contact := minf(Physics.param("cushion_contact_height_m", CONTACT_HEIGHT_FALLBACK), rail_height - 0.001)
	for i in _sections.size():
		_shape_pocket(_sections[i], _sections[(i + 1) % _sections.size()], rail_width, problems)
	if not problems.is_empty():
		_fail(problems)
		return

	var cloth := Surf.new()
	var wood := Surf.new()
	var lining := Surf.new()
	var marks := Surf.new()
	var fittings := Surf.new()
	for section in _sections:
		_add_cushion(cloth, section, contact, rail_height)
		_add_rail(wood, section, rail_height, rail_width)
	_add_bed_and_pockets(cloth, lining, fittings, wood, contact, rail_height, rail_width)
	_add_body(wood, dims, rail_width)
	_add_markings(marks, dims)

	var mesh := ArrayMesh.new()
	cloth.commit(mesh, _cloth_material())
	wood.commit(mesh, _wood_material())
	lining.commit(mesh, _lining_material())
	marks.commit(mesh, _marking_material())
	fittings.commit(mesh, _fitting_material())
	_mesh_instance = MeshInstance3D.new()
	_mesh_instance.name = "Mesh"
	_mesh_instance.mesh = mesh
	add_child(_mesh_instance)
	_bounds = mesh.get_aabb()
	for i in _pockets.size():
		var marker := Marker3D.new()
		marker.name = "Pocket_" + _pockets[i].name
		marker.position = _g(_pockets[i].centre, 0.0)
		marker.set_meta("pocket", i)
		marker.set_meta("drop_radius", _pockets[i].drop_radius)
		add_child(marker)


## The built table's extent in this node's space (empty before a build).
func bounds() -> AABB:
	return _bounds


func _fail(problems: PackedStringArray) -> void:
	_sections.clear()
	_pockets.clear()
	_bounds = AABB()
	Log.error("app", "app.error", {"kind": "message",
			"message": "table_view: the table isn't drawn: " + "; ".join(problems)})


static func _g(p: Vector2, z: float) -> Vector3:
	return Vector3(-p.y, z, -p.x)


static func _v2(value: Variant) -> Vector2:
	return Vector2(float(value[0]), float(value[1]))


static func _dir(angle: float) -> Vector2:
	return Vector2(cos(angle), sin(angle))


func _assemble(shape: Dictionary, problems: PackedStringArray) -> void:
	_pockets.clear()
	_sections.clear()
	for p: Variant in shape["pockets"]:
		var info := PocketInfo.new()
		info.name = String(p["name"])
		info.centre = _v2(p["drop_centre"])
		info.drop_radius = float(p["drop_radius"])
		_pockets.append(info)
	var facings: Array[Dictionary] = []
	var noses: Array[Dictionary] = []
	for l: Variant in shape["lines"]:
		var line := {"name": String(l["name"]), "p1": _v2(l["p1"]), "p2": _v2(l["p2"]), "normal": _v2(l["normal"])}
		if String(l["kind"]) == "nose":
			noses.append(line)
		else:
			facings.append(line)
	var arcs: Array[Dictionary] = []
	for a: Variant in shape["arcs"]:
		arcs.append({"name": String(a["name"]), "centre": _v2(a["centre"]), "radius": float(a["radius"]),
				"a0": float(a["angle_start"]), "a1": float(a["angle_end"]), "pocket": int(a["pocket"])})
	if noses.is_empty():
		problems.append("no nose lines")
		return
	var unordered: Array[Section] = []
	for nose in noses:
		var a: Vector2 = nose["p1"]
		var b: Vector2 = nose["p2"]
		var n: Vector2 = nose["normal"]
		if Vector2(a.y - b.y, b.x - a.x).dot(n) < 0.0:
			var swap := a
			a = b
			b = swap
		var start := _jaw(a, n, arcs, facings, String(nose["name"]), problems)
		var end := _jaw(b, n, arcs, facings, String(nose["name"]), problems)
		if start.is_empty() or end.is_empty():
			continue
		var section := Section.new()
		section.name = String(nose["name"])
		section.nose_point = a
		section.nose_normal = n
		section.nose_dir = (b - a).normalized()
		section.start_pocket = int(start["pocket"])
		section.end_pocket = int(end["pocket"])
		section.start_u = start["u"]
		section.end_u = end["u"]
		_append(section, start["t"], start["facing_normal"])
		var arc_points: PackedVector2Array = start["points"]
		var arc_normals: PackedVector2Array = start["normals"]
		for i in arc_points.size():
			_append(section, arc_points[i], arc_normals[i])
		_append(section, b, n)
		arc_points = end["points"]
		arc_normals = end["normals"]
		for i in range(arc_points.size() - 1, -1, -1):
			_append(section, arc_points[i], arc_normals[i])
		_append(section, end["t"], end["facing_normal"])
		unordered.append(section)
	if not problems.is_empty():
		return
	# Anticlockwise, each pocket ends one section and starts the next.
	var current := unordered[0]
	while _sections.size() < unordered.size():
		_sections.append(current)
		var next: Section = null
		for s in unordered:
			if s.start_pocket == current.end_pocket:
				next = s
		if next == null or (next == _sections[0] and _sections.size() < unordered.size()):
			problems.append("the cushions don't close round the table after '%s'" % current.name)
			return
		current = next
	if current != _sections[0]:
		problems.append("the cushions don't close round the table")


func _append(section: Section, p: Vector2, n: Vector2) -> void:
	if not section.points.is_empty() and section.points[section.points.size() - 1].distance_to(p) < 1e-6:
		return
	section.points.append(p)
	section.normals.append(n)


## The jaw at a nose's end: the arc from its facing round to the nose, and where the facing meets
## the pocket's hole. Empty, with a problem recorded, when the outline doesn't join up.
func _jaw(nose_end: Vector2, nose_normal: Vector2, arcs: Array[Dictionary], facings: Array[Dictionary],
		nose_name: String, problems: PackedStringArray) -> Dictionary:
	for arc in arcs:
		var c: Vector2 = arc["centre"]
		var r: float = arc["radius"]
		var a0: float = arc["a0"]
		var a1: float = arc["a1"]
		var near_angle: float
		var far_angle: float
		if (c + _dir(a0) * r).distance_to(nose_end) < SHAPE_TOLERANCE:
			near_angle = a0
			far_angle = a1
		elif (c + _dir(a1) * r).distance_to(nose_end) < SHAPE_TOLERANCE:
			near_angle = a1
			far_angle = a0
		else:
			continue
		var pocket: int = arc["pocket"]
		if pocket < 0 or pocket >= _pockets.size():
			problems.append("arc '%s' names pocket %d of %d" % [arc["name"], pocket, _pockets.size()])
			return {}
		var jaw_end := c + _dir(far_angle) * r
		var facing := {}
		var rail_end := Vector2.ZERO
		for f in facings:
			if (f["p1"] as Vector2).distance_to(jaw_end) < SHAPE_TOLERANCE:
				facing = f
				rail_end = f["p2"]
			elif (f["p2"] as Vector2).distance_to(jaw_end) < SHAPE_TOLERANCE:
				facing = f
				rail_end = f["p1"]
		if facing.is_empty():
			problems.append("no facing meets arc '%s'" % arc["name"])
			return {}
		var info := _pockets[pocket]
		var axis := (rail_end - jaw_end).normalized()
		var t := jaw_end + axis * (info.centre - jaw_end).dot(axis)
		var to_hole := info.centre - t
		if to_hole.length() < 1e-4 or (t - jaw_end).dot(axis) <= 0.0:
			problems.append("pocket '%s' isn't behind facing '%s'" % [info.name, facing["name"]])
			return {}
		# The two facings of a pocket give its axis and hole; they agree for a well-formed shape.
		info.axis = axis
		info.hole_radius = to_hole.length()
		var sign := 1.0 if _dir(near_angle).dot(nose_normal) >= 0.0 else -1.0
		var span := wrapf(near_angle - far_angle, -PI, PI)
		var steps := maxi(2, ceili(absf(span) / ARC_STEP))
		var points := PackedVector2Array()
		var normals := PackedVector2Array()
		for i in steps + 1:
			var radial := _dir(far_angle + span * i / steps)
			points.append(c + radial * r)
			normals.append(radial * sign)
		return {"pocket": pocket, "t": t, "u": -to_hole.normalized(), "facing_normal": to_hole.normalized(),
				"points": points, "normals": normals}
	problems.append("no jaw arc meets nose '%s' at %s" % [nose_name, nose_end])
	return {}


## Sets the pocket between sections a and b apart from the outline: corner or middle, how far
## back its hole reaches, and the rail's outer edge under its fitting.
func _shape_pocket(a: Section, b: Section, rail_width: float, problems: PackedStringArray) -> void:
	var pocket := _pockets[a.end_pocket]
	pocket.corner = a.nose_normal.dot(b.nose_normal) < 0.5
	var room := minf(_inside(a, pocket.centre, rail_width), _inside(b, pocket.centre, rail_width))
	pocket.hole_back = minf(pocket.hole_radius, room - BEVEL - LIP - MIN_CAP)
	if pocket.hole_back < pocket.hole_radius * 0.4:
		problems.append("pocket '%s' is %.4f m from the rail's outer edge, too close for its hole" % [pocket.name, room])
		return
	var ea := _seam(pocket, a, a.end_u, rail_width, 0.0)
	var eb := _seam(pocket, b, b.start_u, rail_width, 0.0)
	pocket.edge = PackedVector2Array([ea[ea.size() - 1]])
	pocket.edge_out = PackedVector2Array([-a.nose_normal])
	if pocket.corner:
		var na := a.nose_normal
		var nb := b.nose_normal
		var ca := a.nose_point.dot(na) - rail_width
		var cb := b.nose_point.dot(nb) - rail_width
		var det := na.x * nb.y - na.y * nb.x
		var square := Vector2((ca * nb.y - na.y * cb) / det, (na.x * cb - ca * nb.x) / det)
		var centre := square + (na + nb) * CORNER_RADIUS
		var start := (-na).angle()
		var span := wrapf((-nb).angle() - start, -PI, PI)
		for i in CORNER_SEGMENTS + 1:
			var d := _dir(start + span * i / CORNER_SEGMENTS)
			pocket.edge.append(centre + d * CORNER_RADIUS)
			pocket.edge_out.append(d)
	pocket.edge.append(eb[eb.size() - 1])
	pocket.edge_out.append(-b.nose_normal)


## How far p is inside the section's rail's outer edge.
static func _inside(section: Section, p: Vector2, rail_width: float) -> float:
	return (p - section.nose_point).dot(section.nose_normal) + rail_width


## Where the pocket's fitting meets the section's wooden rail: from the cushion's back where it
## ends at the pocket out to `inset` in from the rail's outer edge, square to it. A corner's runs
## straight across the rail; a middle's follows a band round the back of the hole.
func _seam(pocket: PocketInfo, section: Section, u: Vector2, rail_width: float, inset: float) -> PackedVector2Array:
	var c := pocket.centre
	var n := section.nose_normal
	var out := PackedVector2Array([c + u * (pocket.hole_radius + CUSHION_DEPTH)])
	if not pocket.corner:
		var across := pocket.hole_radius + BAND
		var along := pocket.hole_back + BAND
		var reach := (_inside(section, c, rail_width) - BEVEL) / (-n.dot(pocket.axis))
		var end := asin(clampf(reach / along, 0.0, 1.0))
		for i in BAND_SEGMENTS + 1:
			var theta := end * i / BAND_SEGMENTS
			out.append(c + u * cos(theta) * across + pocket.axis * sin(theta) * along)
	var last := out[out.size() - 1]
	out.append(last - n * (_inside(section, last, rail_width) - inset))
	return out


## Normals for a profile of (s, z) points that runs from its outer top edge towards the visible
## side, s growing away from it: each normal is (along s, up).
static func _profile_normals(profile: PackedVector2Array) -> PackedVector2Array:
	var out := PackedVector2Array()
	for j in profile.size():
		var t := profile[mini(j + 1, profile.size() - 1)] - profile[maxi(j - 1, 0)]
		out.append(Vector2(t.y, -t.x).normalized())
	return out


## A surface through rows of points, row j at the profile's height z_j; vis[k] is the horizontal
## direction column k faces (towards smaller s). shades, when given, colours each row.
func _sweep(surf: Surf, rows: Array[PackedVector2Array], profile: PackedVector2Array, vis: PackedVector2Array,
		tile: Vector2, colour: Color = Color.WHITE, shades := PackedColorArray()) -> void:
	var first := surf.verts.size()
	var normals := _profile_normals(profile)
	var along := PackedFloat32Array()
	var u := 0.0
	for k in rows[0].size():
		if k > 0:
			u += rows[0][k].distance_to(rows[0][k - 1])
		along.append(u)
	var v := 0.0
	for j in profile.size():
		if j > 0:
			v += profile[j].distance_to(profile[j - 1])
		for k in rows[j].size():
			var h := -vis[k] * normals[j].x
			surf.vertex(_g(rows[j][k], profile[j].y), Vector3(-h.y, normals[j].y, -h.x).normalized(),
					Vector2(along[k] / tile.x, v / tile.y), colour if shades.is_empty() else shades[j])
	surf.grid(first, profile.size(), rows[0].size())


func _cushion_profile(contact: float, rail_height: float) -> PackedVector2Array:
	return PackedVector2Array([
		Vector2(CUSHION_DEPTH, rail_height),
		Vector2(0.012, rail_height - 0.0004),
		Vector2(0.0045, minf(contact + 0.0022, rail_height - 0.0006)),
		Vector2(0.0012, minf(contact + 0.0012, rail_height - 0.0008)),
		Vector2(0.0, contact),
		Vector2(0.0012, contact - 0.006),
		Vector2(0.0045, contact * 0.5),
		Vector2(0.010, 0.0),
	])


func _add_cushion(cloth: Surf, section: Section, contact: float, rail_height: float) -> void:
	var profile := _cushion_profile(contact, rail_height)
	var rows: Array[PackedVector2Array] = []
	for j in profile.size():
		var row := PackedVector2Array()
		for k in section.points.size():
			row.append(section.points[k] - section.normals[k] * profile[j].x)
		rows.append(row)
	_sweep(cloth, rows, profile, section.normals, METRES, Color(0.93, 0.93, 0.93))


func _add_rail(wood: Surf, section: Section, rail_height: float, rail_width: float) -> void:
	var start := _pockets[section.start_pocket]
	var end := _pockets[section.end_pocket]
	var start_seam := _seam(start, section, section.start_u, rail_width, BEVEL)
	var outline := PackedVector2Array()
	for k in range(start_seam.size() - 1, -1, -1):
		outline.append(start_seam[k])
	for k in section.points.size():
		outline.append(section.points[k] - section.normals[k] * CUSHION_DEPTH)
	outline.append_array(_seam(end, section, section.end_u, rail_width, BEVEL))
	var polygon := _cleaned(outline)
	var triangles := Geometry2D.triangulate_polygon(polygon)
	if triangles.is_empty():
		Log.error("app", "app.error", {"kind": "message",
				"message": "table_view: rail '%s' doesn't triangulate (%d points)" % [section.name, polygon.size()]})
	else:
		var first := wood.verts.size()
		for p in polygon:
			wood.vertex(_g(p, rail_height), Vector3.UP,
					Vector2(p.dot(section.nose_dir), p.dot(section.nose_normal)))
		for i in range(0, triangles.size(), 3):
			wood.tri(first + triangles[i], first + triangles[i + 1], first + triangles[i + 2])

	# The rounded outer edge and the apron below it, straight from one fitting to the next.
	var n := section.nose_normal
	var path := PackedVector2Array([start.edge[start.edge.size() - 1], end.edge[0]])
	_add_edge(wood, path, PackedVector2Array([-n, -n]), _edge_profile(rail_height, APRON_BOTTOM))


## The rail's rounded top edge, from BEVEL in on the top down the apron to `bottom`.
static func _edge_profile(rail_height: float, bottom: float) -> PackedVector2Array:
	return PackedVector2Array([Vector2(BEVEL, rail_height), Vector2(0.0016, rail_height - 0.0006),
			Vector2(0.0003, rail_height - 0.0024), Vector2(0.0, rail_height - BEVEL), Vector2(0.0, bottom)])


## A profile swept along a smooth path on the rail's outer edge, each profile point its s in from it.
func _add_edge(surf: Surf, path: PackedVector2Array, outward: PackedVector2Array, profile: PackedVector2Array,
		colour: Color = Color.WHITE) -> void:
	var rows: Array[PackedVector2Array] = []
	for p in profile:
		var row := PackedVector2Array()
		for k in path.size():
			row.append(path[k] - outward[k] * p.x)
		rows.append(row)
	_sweep(surf, rows, profile, outward, METRES, colour)


static func _cleaned(points: PackedVector2Array) -> PackedVector2Array:
	var out := PackedVector2Array()
	for p in points:
		if out.is_empty() or out[out.size() - 1].distance_to(p) > 1e-6:
			out.append(p)
	if out.size() > 1 and out[0].distance_to(out[out.size() - 1]) < 1e-6:
		out.remove_at(out.size() - 1)
	return out


## The pocket lining's shade at height z: dark leather at the top, black LINING_FADE down.
static func _lining_shade(z: float) -> Color:
	var shade := LINING_TOP * clampf(1.0 + minf(z, 0.0) / LINING_FADE, 0.0, 1.0)
	return Color(shade, shade, shade)


func _add_bed_and_pockets(cloth: Surf, lining: Surf, fittings: Surf, wood: Surf, contact: float,
		rail_height: float, rail_width: float) -> void:
	var bed := PackedVector2Array()
	var fall_profile := PackedVector2Array([Vector2(FALL, 0.0), Vector2(0.0012, -0.0008), Vector2(0.0, -0.004), Vector2(0.0, -0.008)])
	for i in _sections.size():
		var section := _sections[i]
		var next := _sections[(i + 1) % _sections.size()]
		for k in section.points.size():
			bed.append(section.points[k] - section.normals[k] * BED_UNDER)
		var pocket := _pockets[section.end_pocket]
		for d in _front_dirs(pocket, section.end_u):
			bed.append(pocket.centre + d * (pocket.hole_radius + FALL))
		_add_hole(cloth, lining, pocket, section.end_u, fall_profile, contact, rail_height)
		_add_fitting(fittings, wood, pocket, section, next, contact, rail_height, rail_width)

	var cleaned := _cleaned(bed)
	var triangles := Geometry2D.triangulate_polygon(cleaned)
	if triangles.is_empty():
		Log.error("app", "app.error", {"kind": "message",
				"message": "table_view: the bed doesn't triangulate (%d points)" % cleaned.size()})
		return
	var first := cloth.verts.size()
	for p in cleaned:
		cloth.vertex(_g(p, 0.0), Vector3.UP, Vector2(p.x, p.y))
	for i in range(0, triangles.size(), 3):
		cloth.tri(first + triangles[i], first + triangles[i + 1], first + triangles[i + 2])


## Directions from the pocket's centre round its front, the table side, from u to -u.
static func _front_dirs(pocket: PocketInfo, u: Vector2) -> PackedVector2Array:
	var out := PackedVector2Array()
	for s in POCKET_SEGMENTS + 1:
		var a := PI * s / POCKET_SEGMENTS
		out.append(u * cos(a) - pocket.axis * sin(a))
	return out


## The hole's back from u round to -u, `s` out from its edge: half an ellipse, as deep as the rail allows.
static func _back(pocket: PocketInfo, u: Vector2, s: float) -> PackedVector2Array:
	var out := PackedVector2Array()
	for k in POCKET_SEGMENTS + 1:
		var a := PI * k / POCKET_SEGMENTS
		out.append(pocket.centre + u * cos(a) * (pocket.hole_radius + s) + pocket.axis * sin(a) * (pocket.hole_back + s))
	return out


## The back's inward directions, matching _back's points.
static func _back_inward(pocket: PocketInfo, u: Vector2) -> PackedVector2Array:
	var out := PackedVector2Array()
	for k in POCKET_SEGMENTS + 1:
		var a := PI * k / POCKET_SEGMENTS
		out.append(-(u * cos(a) / pocket.hole_radius + pocket.axis * sin(a) / pocket.hole_back).normalized())
	return out


## The cloth turning down over the fall, and the lined hole below it: straight walls round the
## front (under the fall) and the back (under the fitting's lip), closed at the bottom.
func _add_hole(cloth: Surf, lining: Surf, pocket: PocketInfo, u: Vector2, fall_profile: PackedVector2Array,
		contact: float, rail_height: float) -> void:
	var c := pocket.centre
	var r := pocket.hole_radius
	var front := _front_dirs(pocket, u)
	_sweep(cloth, _radial_rows(c, front, fall_profile, r), fall_profile, _negated(front), METRES)

	var front_profile := PackedVector2Array([Vector2(0.0, fall_profile[fall_profile.size() - 1].y)])
	var back_profile := PackedVector2Array([Vector2(0.0, rail_height - LIP - 0.004), Vector2(0.0, 0.0)])
	for z: float in [-0.02, -0.045, -0.07, -HOLE_DEPTH]:
		front_profile.append(Vector2(0.0, z))
		back_profile.append(Vector2(0.0, z))
	_sweep(lining, _radial_rows(c, front, front_profile, r), front_profile, _negated(front), METRES,
			Color.WHITE, _shades(front_profile))
	var back_rows: Array[PackedVector2Array] = []
	for p in back_profile:
		back_rows.append(_back(pocket, u, 0.0))
	_sweep(lining, back_rows, back_profile, _back_inward(pocket, u), METRES, Color.WHITE, _shades(back_profile))

	var ring := PackedVector2Array()
	for d in front:
		ring.append(c + d * r)
	var back := _back(pocket, u, 0.0)
	for k in range(back.size() - 1, -1, -1):
		ring.append(back[k])
	ring = _cleaned(ring)
	var middle := lining.vertex(_g(c, -HOLE_DEPTH), Vector3.UP, Vector2.ZERO, Color.BLACK)
	for p in ring:
		lining.vertex(_g(p, -HOLE_DEPTH), Vector3.UP, p - c, Color.BLACK)
	for k in ring.size():
		lining.tri(middle, middle + 1 + k, middle + 1 + (k + 1) % ring.size())

	# Under each cushion's nose where it stops at the hole, facing back along the facing to the table.
	for side: Vector2 in [u, -u]:
		var near := c + side * r
		var far := c + side * (r + CUSHION_DEPTH)
		var n := _g(-pocket.axis, 0.0)
		var shade := _lining_shade(0.0)
		var first := lining.vertex(_g(near, -0.008), n, Vector2.ZERO, shade)
		lining.vertex(_g(far, -0.008), n, Vector2(CUSHION_DEPTH, 0.0), shade)
		lining.vertex(_g(far, contact), n, Vector2(CUSHION_DEPTH, contact), shade)
		lining.vertex(_g(near, contact), n, Vector2(0.0, contact), shade)
		lining.tri(first, first + 1, first + 2)
		lining.tri(first, first + 2, first + 3)


static func _shades(profile: PackedVector2Array) -> PackedColorArray:
	var out := PackedColorArray()
	for p in profile:
		out.append(_lining_shade(p.y))
	return out


## The leather cap over the rail behind the pocket (between sections a and b): a low dome from the
## cushions' ends round the back of the hole, rolling over a lip into the hole and over the rail's
## edge a little way down the apron.
func _add_fitting(fittings: Surf, wood: Surf, pocket: PocketInfo, a: Section, b: Section, contact: float,
		rail_height: float, rail_width: float) -> void:
	var c := pocket.centre
	var r := pocket.hole_radius
	var u := a.end_u
	# A middle's seam starts along the cushion's end, which its band doesn't reach.
	var skip := 0 if pocket.corner else 1
	var seam_a := _seam(pocket, a, u, rail_width, BEVEL)
	var seam_b := _seam(pocket, b, b.start_u, rail_width, BEVEL)
	var outer := PackedVector2Array()
	for k in range(skip, seam_a.size()):
		outer.append(seam_a[k])
	for k in pocket.edge.size():
		outer.append(pocket.edge[k] - pocket.edge_out[k] * BEVEL)
	for k in range(seam_b.size() - 1, skip - 1, -1):
		outer.append(seam_b[k])
	_add_dome(fittings, pocket, u, _cleaned(outer), rail_height)

	# Where each cushion ends: the fitting's leather above the nose, the hole's lining under it.
	var edge_shade := Color(FITTING_EDGE_SHADE, FITTING_EDGE_SHADE, FITTING_EDGE_SHADE)
	for side: Vector2 in [u, -u]:
		var near := c + side * r
		var far := c + side * (r + CUSHION_DEPTH)
		var n := _g(-pocket.axis, 0.0)
		var first := fittings.vertex(_g(near, contact), n, Vector2.ZERO, edge_shade)
		fittings.vertex(_g(far, contact), n, Vector2(CUSHION_DEPTH, 0.0), edge_shade)
		fittings.vertex(_g(far, rail_height), n, Vector2(CUSHION_DEPTH, rail_height - contact), edge_shade)
		fittings.vertex(_g(near, rail_height), n, Vector2(0.0, rail_height - contact), edge_shade)
		fittings.tri(first, first + 1, first + 2)
		fittings.tri(first, first + 2, first + 3)

	var lip := PackedVector2Array()
	for i in 5:
		var angle := PI / 2.0 * i / 4.0
		lip.append(Vector2(LIP * (1.0 - sin(angle)), rail_height - LIP + LIP * cos(angle)))
	lip.append(Vector2(0.0, rail_height - LIP - 0.004))
	var rows: Array[PackedVector2Array] = []
	var shades := PackedColorArray()
	for p in lip:
		rows.append(_back(pocket, u, p.x))
		shades.append(edge_shade.darkened(0.6 * (rail_height - p.y) / (LIP + 0.004)))
	_sweep(fittings, rows, lip, _back_inward(pocket, u), METRES, Color.WHITE, shades)

	_add_edge(fittings, pocket.edge, pocket.edge_out, _edge_profile(rail_height, rail_height - CAP_WRAP), edge_shade)
	_add_edge(wood, pocket.edge, pocket.edge_out,
			PackedVector2Array([Vector2(0.0, rail_height - CAP_WRAP), Vector2(0.0, APRON_BOTTOM)]))


## The fitting's top: a low dome from the lip round the back of the hole out to `outer_line`, the
## rest of its outline from one cushion's end to the other, flush with the rail along both and shaded
## darker towards them. Its rows run out from the pocket's centre, one through every corner of
## `outer_line` so none is cut.
func _add_dome(surf: Surf, pocket: PocketInfo, u: Vector2, outer_line: PackedVector2Array, rail_height: float) -> void:
	var c := pocket.centre
	var angles := PackedFloat64Array()
	for i in DOME_COLUMNS + 1:
		angles.append(PI * i / DOME_COLUMNS)
	for p in outer_line:
		angles.append(clampf(atan2((p - c).dot(pocket.axis), (p - c).dot(u)), 0.0, PI))
	angles.sort()
	var semi_across := pocket.hole_radius + LIP
	var semi_along := pocket.hole_back + LIP
	var inner := PackedVector2Array()
	var outer := PackedVector2Array()
	var previous := -1.0
	for angle in angles:
		if angle - previous < 1e-6:
			continue
		previous = angle
		var d := u * cos(angle) + pocket.axis * sin(angle)
		inner.append(c + d / sqrt(pow(cos(angle) / semi_across, 2.0) + pow(sin(angle) / semi_along, 2.0)))
		var hit := outer_line[0] if angle < PI / 2.0 else outer_line[outer_line.size() - 1]
		var nearest := INF
		for k in range(1, outer_line.size()):
			var at: Variant = Geometry2D.segment_intersects_segment(c, c + d, outer_line[k - 1], outer_line[k])
			if at != null and (at as Vector2).distance_to(c) < nearest:
				nearest = (at as Vector2).distance_to(c)
				hit = at
		outer.append(hit)
	var grid: Array[PackedVector3Array] = []
	for j in DOME_ROWS + 1:
		var f := float(j) / DOME_ROWS
		var row := PackedVector3Array()
		for k in inner.size():
			row.append(_g(inner[k].lerp(outer[k], f), rail_height + DOME * sin(PI * f)))
		grid.append(row)
	var first := surf.verts.size()
	var last := inner.size() - 1
	for j in grid.size():
		var shade := FITTING_EDGE_SHADE + (1.0 - FITTING_EDGE_SHADE) * sqrt(sin(PI * j / DOME_ROWS))
		for k in inner.size():
			var along := grid[j][mini(k + 1, last)] - grid[j][maxi(k - 1, 0)]
			var across := grid[mini(j + 1, DOME_ROWS)][k] - grid[maxi(j - 1, 0)][k]
			var n := along.cross(across).normalized()
			if n.y < 0.0:
				n = -n
			var p := grid[j][k]
			surf.vertex(p, n, Vector2(-p.z, -p.x), Color(shade, shade, shade))
	surf.grid(first, grid.size(), inner.size())
	# The dome's ends, standing on the cushions' ends.
	var edge_shade := Color(FITTING_EDGE_SHADE, FITTING_EDGE_SHADE, FITTING_EDGE_SHADE)
	var facing := _g(-pocket.axis, 0.0)
	for k: int in [0, last]:
		first = surf.verts.size()
		for j in grid.size():
			var top := grid[j][k]
			surf.vertex(top, facing, Vector2(-top.z, top.y), edge_shade)
			surf.vertex(Vector3(top.x, rail_height, top.z), facing, Vector2(-top.z, rail_height), edge_shade)
		for j in DOME_ROWS:
			var i := first + j * 2
			surf.tri(i, i + 1, i + 3)
			surf.tri(i, i + 3, i + 2)


## Rows round a centre: row j at radius base + profile[j].x along each direction.
static func _radial_rows(centre: Vector2, dirs: PackedVector2Array, profile: PackedVector2Array, base: float) -> Array[PackedVector2Array]:
	var rows: Array[PackedVector2Array] = []
	for p in profile:
		var row := PackedVector2Array()
		for d in dirs:
			row.append(centre + d * (base + p.x))
		rows.append(row)
	return rows


static func _negated(dirs: PackedVector2Array) -> PackedVector2Array:
	var out := PackedVector2Array()
	for d in dirs:
		out.append(-d)
	return out


## The frame under the rails, set in from the apron and darker.
func _add_body(wood: Surf, dims: Dictionary, rail_width: float) -> void:
	var half := Vector2(float(dims["playing_length"]) / 2.0, float(dims["playing_width"]) / 2.0) \
			+ Vector2.ONE * (rail_width - BODY_INSET)
	var dark := Color(0.32, 0.3, 0.3)
	var corners: Array[Vector2] = [Vector2(half.x, half.y), Vector2(-half.x, half.y), Vector2(-half.x, -half.y), Vector2(half.x, -half.y)]
	for i in 4:
		var a := corners[i]
		var b := corners[(i + 1) % 4]
		var along := (b - a).normalized()
		var out := Vector2(along.y, -along.x)
		if out.dot(a + b) < 0.0:
			out = -out
		var n := _g(out, 0.0)
		var length := a.distance_to(b)
		var first := wood.vertex(_g(a, APRON_BOTTOM), n, Vector2(0.0, 0.0), dark)
		wood.vertex(_g(b, APRON_BOTTOM), n, Vector2(length, 0.0), dark)
		wood.vertex(_g(b, BODY_BOTTOM), n, Vector2(length, APRON_BOTTOM - BODY_BOTTOM), dark)
		wood.vertex(_g(a, BODY_BOTTOM), n, Vector2(0.0, APRON_BOTTOM - BODY_BOTTOM), dark)
		wood.tri(first, first + 1, first + 2)
		wood.tri(first, first + 2, first + 3)


func _add_markings(marks: Surf, dims: Dictionary) -> void:
	var baulk_x := float(dims["baulk_line_x"])
	var half_width := float(dims["playing_width"]) / 2.0
	var d_radius := float(dims["d_radius"])
	var first := marks.vertex(_g(Vector2(baulk_x - MARK_HALF_WIDTH, -half_width), MARK_LIFT), Vector3.UP, Vector2.ZERO)
	marks.vertex(_g(Vector2(baulk_x + MARK_HALF_WIDTH, -half_width), MARK_LIFT), Vector3.UP, Vector2.ZERO)
	marks.vertex(_g(Vector2(baulk_x + MARK_HALF_WIDTH, half_width), MARK_LIFT), Vector3.UP, Vector2.ZERO)
	marks.vertex(_g(Vector2(baulk_x - MARK_HALF_WIDTH, half_width), MARK_LIFT), Vector3.UP, Vector2.ZERO)
	marks.tri(first, first + 1, first + 2)
	marks.tri(first, first + 2, first + 3)
	# The D: a semicircle on the baulk side, centred on the middle of the baulk line.
	var segments := 96
	first = marks.verts.size()
	for i in segments + 1:
		var d := _dir(PI / 2.0 + PI * i / segments)
		marks.vertex(_g(Vector2(baulk_x, 0.0) + d * (d_radius - MARK_HALF_WIDTH), MARK_LIFT), Vector3.UP, Vector2.ZERO)
		marks.vertex(_g(Vector2(baulk_x, 0.0) + d * (d_radius + MARK_HALF_WIDTH), MARK_LIFT), Vector3.UP, Vector2.ZERO)
	for i in segments:
		var k := first + i * 2
		marks.tri(k, k + 1, k + 3)
		marks.tri(k, k + 3, k + 2)
	for colour in SPOT_COLOURS:
		var spot := OfsTable.spot(colour)
		if spot.is_empty():
			Log.warn("app", "app.error", {"kind": "message", "message": "table_view: OfsTable.spot(%s) is empty; no spot drawn" % colour})
			continue
		var centre := Vector2(float(spot["x"]), float(spot["y"]))
		var middle := marks.vertex(_g(centre, MARK_LIFT), Vector3.UP, Vector2.ZERO)
		for i in 17:
			marks.vertex(_g(centre + _dir(TAU * i / 16.0) * SPOT_RADIUS, MARK_LIFT), Vector3.UP, Vector2.ZERO)
		for i in 16:
			marks.tri(middle, middle + 1 + i, middle + 2 + i)


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
	m.clearcoat = 0.35
	m.clearcoat_roughness = 0.3
	return m


func _lining_material() -> StandardMaterial3D:
	var m := _textured("leather/leather", LEATHER_TILE, 1.0, 1.0)
	m.vertex_color_use_as_albedo = true
	# Next to no sheen, so nothing in the hole catches the canopy and it stays dark all the way down.
	m.metallic_specular = 0.1
	return m


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
				"message": "table_view: %s didn't load; the table is drawn without it" % path, "file": path})
	return texture


func _marking_material() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.46, 0.52, 0.46)
	m.roughness = 1.0
	m.metallic_specular = 0.1
	return m
