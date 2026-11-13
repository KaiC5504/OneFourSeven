class_name TableView
extends Node3D
## The table (spec 6, 13.1) drawn from the physics outline, so what you see is what the balls hit:
## cloth with a subtle nap, cushions, curved jaws, pockets, rails, the baulk line, the D and spots.
##
## Everything static is one mesh, one surface per material (the SURFACE_* indices), so the table
## costs four draw calls. Each pocket also gets a Marker3D "Pocket_<name>" at its drop centre on
## the cloth, with metas "pocket" (its index) and "drop_radius".

const SURFACE_CLOTH := 0
const SURFACE_WOOD := 1
const SURFACE_LEATHER := 2
const SURFACE_MARKINGS := 3

## The cushion rubber's top, from the nose back to the wooden rail.
const CUSHION_DEPTH := 0.05
## How far the bed runs on under the cushions, so no edge of it can show.
const BED_UNDER := 0.02
## The leather collar round the back of each pocket; as deep as the cushions, so their backs run
## on round it.
const RIM_WIDTH := CUSHION_DEPTH
## Wood left round a pocket that would otherwise break the rail's outer edge (the middles).
const MIN_WOOD := 0.01
const BUMP_MARGIN := 0.012
## The slate's rounded edge, where the cloth turns down into the pocket.
const FALL := 0.004
const POCKET_DEPTH := 0.085
const BEVEL := 0.005
const APRON_BOTTOM := -0.09
const BODY_INSET := 0.03
const BODY_BOTTOM := -0.7
const MARK_HALF_WIDTH := 0.0015
const MARK_LIFT := 0.0003
const SPOT_RADIUS := 0.004
const ARC_STEP := PI / 48.0
const POCKET_SEGMENTS := 24
const BUMP_SEGMENTS := 12
const RIM_SEGMENTS := 10
const CONTACT_HEIGHT_FALLBACK := 0.03675
const SHAPE_TOLERANCE := 1e-4
const SPOT_COLOURS: Array[String] = ["yellow", "green", "brown", "blue", "pink", "black"]

# UVs are in metres; each material's uv1_scale sets its texture's tile (game/textures/README.md).
const METRES := Vector2.ONE
# Tournament green; the cloth texture's nap runs along U, core x, baulk to top (spec 6.4).
const CLOTH_GREEN := Color(0.11, 0.42, 0.16)
const CLOTH_TILE := 0.12
const RAIL_TILE := 2.0
const LEATHER_TILE := 0.45
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
	var bump_radius := 0.0


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
	for i in _pockets.size():
		_decide_bump(i, rail_width)

	var cloth := Surf.new()
	var wood := Surf.new()
	var leather := Surf.new()
	var marks := Surf.new()
	for section in _sections:
		_add_cushion(cloth, section, contact, rail_height)
		_add_rail(wood, section, rail_height, rail_width)
	_add_bed_and_pockets(cloth, leather, rail_height)
	_add_body(wood, dims, rail_width)
	_add_markings(marks, dims)

	var mesh := ArrayMesh.new()
	cloth.commit(mesh, _cloth_material())
	wood.commit(mesh, _wood_material())
	leather.commit(mesh, _leather_material())
	marks.commit(mesh, _marking_material())
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


func _decide_bump(index: int, rail_width: float) -> void:
	var pocket := _pockets[index]
	pocket.bump_radius = 0.0
	for section in _sections:
		if section.start_pocket != index and section.end_pocket != index:
			continue
		var inside := (pocket.centre - section.nose_point).dot(section.nose_normal) + rail_width
		if inside < pocket.hole_radius + RIM_WIDTH + MIN_WOOD:
			pocket.bump_radius = pocket.hole_radius + RIM_WIDTH + BUMP_MARGIN


## Normals for a profile of (s, z) points that runs from its outer top edge towards the visible
## side, s growing away from it: each normal is (along s, up).
static func _profile_normals(profile: PackedVector2Array) -> PackedVector2Array:
	var out := PackedVector2Array()
	for j in profile.size():
		var t := profile[mini(j + 1, profile.size() - 1)] - profile[maxi(j - 1, 0)]
		out.append(Vector2(t.y, -t.x).normalized())
	return out


## A surface through rows of points, row j at the profile's height z_j; vis[k] is the horizontal
## direction column k faces (towards smaller s).
func _sweep(surf: Surf, rows: Array[PackedVector2Array], profile: PackedVector2Array, vis: PackedVector2Array,
		tile: Vector2, colour: Color = Color.WHITE) -> void:
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
					Vector2(along[k] / tile.x, v / tile.y), colour)
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


## The rail's outer edge from the start pocket to the end pocket, `inset` in from it, with the
## outward direction at each point. A pocket too close to the edge gets a rounded bump round it.
func _outer_path(section: Section, inset: float, rail_width: float, points: PackedVector2Array,
		outward: PackedVector2Array) -> void:
	points.clear()
	outward.clear()
	var n := section.nose_normal
	var line_offset := -(rail_width - inset)
	for end in 2:
		var pocket := _pockets[section.start_pocket if end == 0 else section.end_pocket]
		if pocket.bump_radius > 0.0:
			var rho := pocket.bump_radius - inset
			var h := (pocket.centre - section.nose_point).dot(n) - line_offset
			var foot := pocket.centre - n * h
			var across := sqrt(maxf(rho * rho - h * h, 0.0)) * (1.0 if end == 0 else -1.0)
			var meet := foot + section.nose_dir * across
			var apex_angle := pocket.axis.angle()
			var span := wrapf((meet - pocket.centre).angle() - apex_angle, -PI, PI)
			for i in BUMP_SEGMENTS + 1:
				var f := float(i) / BUMP_SEGMENTS if end == 0 else 1.0 - float(i) / BUMP_SEGMENTS
				var radial := _dir(apex_angle + span * f)
				points.append(pocket.centre + radial * rho)
				outward.append(radial)
		else:
			var t := (line_offset - (pocket.centre - section.nose_point).dot(n)) / pocket.axis.dot(n)
			points.append(pocket.centre + pocket.axis * t)
			outward.append(-n)


## A quarter of a pocket's back, from the section's facing round to the axis (or the reverse).
func _quarter(pocket: PocketInfo, u: Vector2, radius: float, towards_axis: bool) -> PackedVector2Array:
	var out := PackedVector2Array()
	var span := wrapf(pocket.axis.angle() - u.angle(), -PI, PI)
	for i in RIM_SEGMENTS + 1:
		var f := float(i) / RIM_SEGMENTS if towards_axis else 1.0 - float(i) / RIM_SEGMENTS
		out.append(pocket.centre + _dir(u.angle() + span * f) * radius)
	return out


func _add_rail(wood: Surf, section: Section, rail_height: float, rail_width: float) -> void:
	var start := _pockets[section.start_pocket]
	var end := _pockets[section.end_pocket]
	var outline := PackedVector2Array()
	outline.append_array(_quarter(start, section.start_u, start.hole_radius + RIM_WIDTH, false))
	for k in section.points.size():
		outline.append(section.points[k] - section.normals[k] * CUSHION_DEPTH)
	outline.append_array(_quarter(end, section.end_u, end.hole_radius + RIM_WIDTH, true))
	var edge := PackedVector2Array()
	var edge_out := PackedVector2Array()
	_outer_path(section, BEVEL, rail_width, edge, edge_out)
	for k in range(edge.size() - 1, -1, -1):
		outline.append(edge[k])
	var polygon := PackedVector2Array()
	for p in outline:
		if polygon.is_empty() or polygon[polygon.size() - 1].distance_to(p) > 1e-6:
			polygon.append(p)
	if polygon[0].distance_to(polygon[polygon.size() - 1]) < 1e-6:
		polygon.remove_at(polygon.size() - 1)
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

	# The rounded outer edge and the apron below it, in pieces split where the edge turns.
	var profile := PackedVector2Array([Vector2(BEVEL, rail_height), Vector2(0.0016, rail_height - 0.0006),
			Vector2(0.0003, rail_height - 0.0024), Vector2(0.0, rail_height - BEVEL), Vector2(0.0, APRON_BOTTOM)])
	var paths: Array[PackedVector2Array] = []
	var dirs := PackedVector2Array()
	for j in profile.size():
		var points := PackedVector2Array()
		_outer_path(section, profile[j].x, rail_width, points, dirs)
		paths.append(points)
	# Each consecutive pair of points is its own strip, so a corner keeps a crisp edge and a
	# curve stays smooth through shared directions.
	for k in range(1, dirs.size()):
		var rows: Array[PackedVector2Array] = []
		for j in profile.size():
			rows.append(PackedVector2Array([paths[j][k - 1], paths[j][k]]))
		var vis := PackedVector2Array([dirs[k - 1], dirs[k]])
		if dirs[k - 1].dot(dirs[k]) < 0.95:
			# A sharp turn (the bump meeting the straight edge): both ends face the strip's own way.
			var flat := (paths[0][k] - paths[0][k - 1]).normalized()
			var face := Vector2(flat.y, -flat.x)
			if face.dot(dirs[k] + dirs[k - 1]) < 0.0:
				face = -face
			vis = PackedVector2Array([face, face])
		_sweep(wood, rows, profile, vis, METRES)


func _add_bed_and_pockets(cloth: Surf, leather: Surf, rail_height: float) -> void:
	var bed := PackedVector2Array()
	var fall_profile := PackedVector2Array([Vector2(FALL, 0.0), Vector2(0.0012, -0.0008), Vector2(0.0, -0.004), Vector2(0.0, -0.008)])
	var bowl := PackedVector2Array([Vector2(0.0, -0.008), Vector2(-0.05, -0.03), Vector2(-0.2, -0.06),
			Vector2(-0.5, -0.08), Vector2(-1.0, -POCKET_DEPTH)])
	var rim := PackedVector2Array([Vector2(RIM_WIDTH, rail_height), Vector2(0.004, rail_height),
			Vector2(0.0012, rail_height - 0.0008), Vector2(0.0, rail_height - 0.004), Vector2(0.0, 0.0)])
	for i in _sections.size():
		var section := _sections[i]
		var next := _sections[(i + 1) % _sections.size()]
		for k in section.points.size():
			bed.append(section.points[k] - section.normals[k] * BED_UNDER)
		var pocket := _pockets[section.end_pocket]
		var r := pocket.hole_radius
		var a0 := section.end_u.angle()
		var turn := 1.0 if _dir(a0 + PI / 2.0).dot(-pocket.axis) > 0.0 else -1.0
		var front := PackedVector2Array()
		for s in POCKET_SEGMENTS + 1:
			front.append(_dir(a0 + turn * PI * s / POCKET_SEGMENTS))
		var back := PackedVector2Array()
		for s in POCKET_SEGMENTS + 1:
			back.append(_dir(a0 - turn * PI * s / POCKET_SEGMENTS))
		for d in front:
			bed.append(pocket.centre + d * (r + FALL))

		# The cloth turns down over the fall; below it, and round the back from the rim top, the
		# leather drops into a closed bowl.
		_sweep(cloth, _radial_rows(pocket.centre, front, fall_profile, r), fall_profile, _negated(front), METRES)
		var front_bowl := _bowl_profile(bowl, r)
		_sweep(leather, _radial_rows(pocket.centre, front, front_bowl, 0.0), front_bowl, _negated(front), METRES)
		var back_profile := PackedVector2Array()
		for p in rim:
			back_profile.append(Vector2(r + p.x, p.y))
		back_profile.append_array(front_bowl)
		_sweep(leather, _radial_rows(pocket.centre, back, back_profile, 0.0), back_profile, _negated(back), METRES)

		# The rim's ends, where the cushions stop, face back along the facings to the table.
		for u: Vector2 in [section.end_u, next.start_u]:
			var inner := pocket.centre + u * r
			var outer := pocket.centre + u * (r + RIM_WIDTH)
			var n := _g(-pocket.axis, 0.0)
			var first := leather.vertex(_g(inner, 0.0), n, Vector2.ZERO)
			leather.vertex(_g(outer, 0.0), n, Vector2.ZERO)
			leather.vertex(_g(outer, rail_height), n, Vector2.ZERO)
			leather.vertex(_g(inner, rail_height), n, Vector2.ZERO)
			leather.tri(first, first + 1, first + 2)
			leather.tri(first, first + 2, first + 3)

	var cleaned := PackedVector2Array()
	for p in bed:
		if cleaned.is_empty() or cleaned[cleaned.size() - 1].distance_to(p) > 1e-6:
			cleaned.append(p)
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


## The bowl's profile for a hole of radius r: the template's s is a fraction of r, from r inwards.
static func _bowl_profile(template: PackedVector2Array, r: float) -> PackedVector2Array:
	var out := PackedVector2Array()
	for p in template:
		out.append(Vector2(r * (1.0 + p.x), p.y))
	return out


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
	# The body under the rails is darker through its vertex colour.
	m.vertex_color_use_as_albedo = true
	m.clearcoat_enabled = true
	# A satin lacquer: a mirror finish showed the canopy as a hard white streak along the rails.
	m.clearcoat = 0.35
	m.clearcoat_roughness = 0.3
	return m


func _leather_material() -> StandardMaterial3D:
	return _textured("leather/leather", LEATHER_TILE, 1.0, 1.0)


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
