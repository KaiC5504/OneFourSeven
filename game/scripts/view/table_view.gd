class_name TableView
extends Node3D
## The table (spec 6, 13.1) drawn from the physics outline, so what you see is what the balls hit:
## cloth with a subtle nap, cushions, jaws, pockets with their nets and leathers, rails, the baulk
## line, the D and spots.
##
## Everything static is one mesh, one surface per material (the SURFACE_* indices), so the table
## costs five draw calls. Each pocket also gets a Marker3D "Pocket_<name>" at its drop centre on
## the cloth, with metas "pocket" (its index) and "drop_radius".

const SURFACE_CLOTH := 0
const SURFACE_WOOD := 1
## The nets hanging in the pockets.
const SURFACE_NET := 2
## The net's surface under its old name, from when the holes were lined in leather; test_main.gd
## still checks it by this name.
const SURFACE_LEATHER := SURFACE_NET
const SURFACE_MARKINGS := 3
## The leather fittings round the back of each pocket.
const SURFACE_FITTINGS := 4

## The cushion's top, from the nose back to the wooden rail: a slim rubber wedge, as on tournament
## tables, the wood taking the rest of the rail's width.
const CUSHION_DEPTH := 0.04
## At a middle pocket the wood ends in a cut square across the rail this far behind the facing, as
## on the real tables: the cushion fills its jaw up to the cut and runs on down the facing this
## wide, instead of jutting into the rail.
const JAW_STRIP := 0.012
## The slate's rounded edge, where the cloth turns down into the pocket, and how far down the
## turned cloth reaches before the net takes over.
const FALL := 0.01
const FALL_DEPTH := 0.016
## The net from its rim down, a bag: each row (its size as a fraction of the hole's, depth below the
## cloth, shade). It sags out in the shade of the fitting's lip and the turned cloth (NET_RIM_SHADE
## at its rim), so from the player's eye its back curves away; then it closes in, its bowl lit, down
## to about 7 cm, where it turns to a dark opening; the dark runs on below where a potted ball comes
## to rest (BallsView.DROP_DEPTH). No row comes closer to a resting potted ball than
## NET_BALL_CLEARANCE.
const NET_ROWS: Array[Vector3] = [
	Vector3(1.1, -0.02, 0.78),
	Vector3(1.06, -0.028, 0.9),
	Vector3(0.98, -0.036, 0.95),
	Vector3(0.88, -0.043, 0.84),
	Vector3(0.78, -0.05, 0.77),
	Vector3(0.7, -0.056, 0.68),
	Vector3(0.62, -0.061, 0.58),
	Vector3(0.54, -0.065, 0.46),
	Vector3(0.45, -0.068, 0.3),
	Vector3(0.38, -0.071, 0.012),
	Vector3(0.28, -0.075, 0.012),
]
const NET_BOTTOM := -0.082
const NET_RIM_SHADE := 0.6
const NET_DARK := Color(0.012, 0.012, 0.012)
const NET_BALL_CLEARANCE := 0.0015
const BALL_RADIUS_FALLBACK := 0.02625
## The net's normals, the bag's own, lean this much further up, so the light from straight above
## reaches into its bowl and it reads as light crochet rather than a dark pit, while its sag under
## the lip stays in shade.
const NET_TILT := 0.35
## How far the net keeps inside the rail's outer edge where it hangs behind a middle pocket's hole.
const NET_CLEARANCE := 0.003
## The fitting's rounded lip into the hole, and how far its leather runs on down inside the hole,
## where the net hangs from it.
const LIP := 0.004
const LIP_FACE := 0.014
const LIP_STEPS := 4
## What's left of a middle pocket's fitting behind its hole, past the lip and the rail's rounded edge.
## The physics' drop circle reaches past the rail's outer edge there, so the hole drawn behind the
## fall is shallower than the circle; the net bellies out behind it under the fitting.
const MIN_CAP := 0.001
## A middle pocket's fitting: a band this wide round the back of its hole, so at its sides it meets
## the wood's square end, as on the real tables.
const BAND := JAW_STRIP
## A corner's horseshoe fitting: this wide round the back of its hole, a little wider than a cushion.
const HORSESHOE_WIDTH := 0.05
const HORSESHOE_SEGMENTS := 16
## The rails' rounded outer corner, mitred where the two rails meet behind each corner's horseshoe.
const CORNER_RADIUS := 0.02
const MITRE_WIDTH := 0.0012
const MITRE_SHADE := 0.3
## The middle fittings wrap over the rail's edge and this far down the apron.
const CAP_WRAP := 0.03
## The fittings' top rises this much above the rail, so the canopy shades them like padded leather;
## it rises over at least DOME_RAMP of leather, so the horseshoe's tapering arms don't spike.
const DOME := 0.004
const DOME_RAMP := 0.02
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
# Multiplies the rail texture, which is already mahogany: darkened a little, kept warm. A darker,
# redder tint read near black from the far, low TV camera.
const RAIL_TINT := Color(0.86, 0.78, 0.7)
# Cream pocket leathers, as on TV tournament tables.
const FITTING_CREAM := Color(0.78, 0.7, 0.55)
# The crochet net, generated (_net_maps): NET_CELLS diamonds of cord across a NET_TILE tile, so
# each diamond is about 12 mm, as on tournament nets. A small tile keeps generating it quick.
const NET_PIXELS := 64
const NET_CELLS := 2
const NET_TILE := 0.025
const NET_CORD := Color(0.94, 0.91, 0.84)
# What shows between the cords: the net's far side and the dark under the table.
const NET_GAP := Color(0.2, 0.18, 0.16)
# Cord and knot half-widths, in cells, and how much each cord swells along its stitches.
const NET_CORD_WIDTH := 0.14
const NET_KNOT_WIDTH := 0.22
const NET_STITCH := 0.25
const NET_STITCHES := 3.0
const NET_RELIEF := 2.5
const TEXTURES := "res://textures/"

static var _net_cache: Array[ImageTexture] = []

var _pockets: Array[PocketInfo] = []
var _sections: Array[Section] = []
var _bounds := AABB()
var _mesh_instance: MeshInstance3D
var _ball_radius := BALL_RADIUS_FALLBACK


class PocketInfo:
	var name := ""
	var centre := Vector2.ZERO
	var drop_radius := 0.0
	## The hole drawn behind the fall: from its centre to the facings, which touch it.
	var hole_radius := 0.0
	## Out of the table, between the facings.
	var axis := Vector2.ZERO
	## How far the hole drawn behind the fall reaches back along the axis: hole_radius, or less
	## where the rail behind it is narrow (the middles).
	var hole_back := 0.0
	## How far the net may belly out behind the centre below the cloth, inside the rail's outer edge.
	var net_back := 0.0
	## From the centre to where the hole touches the facing of the section before it (ua) and of
	## the one after it (ub), anticlockwise.
	var ua := Vector2.ZERO
	var ub := Vector2.ZERO
	## Where two rails meet, rather than in the middle of one.
	var corner := false
	## The rail's outer edge round the pocket, from the rail before it round to the one after it,
	## with each point's outward direction: under a middle's fitting, or a corner's rounded rail
	## corner, mitred at its middle point.
	var edge := PackedVector2Array()
	var edge_out := PackedVector2Array()
	## A corner's horseshoe: its arms' tips, where each cushion ends on its rail's inner edge, and
	## where the cuts square across the rails from there meet its outer edge (ua's side first).
	var tips := PackedVector2Array()
	var cuts := PackedVector2Array()
	## A corner's rounded outer rail corner, and the mitre's direction out through it.
	var corner_centre := Vector2.ZERO
	var corner_out := Vector2.ZERO


## One cushion and its rail, from one pocket to the next, anticlockwise seen from above.
class Section:
	var name := ""
	## The outline from where the cushion ends at the start pocket round the jaws and along the nose
	## to where it ends at the end pocket; normals point to the play side or the opening. At a corner
	## the cushion ends where its facing reaches the rail's inner edge, at a middle where its facing
	## meets the hole.
	var points := PackedVector2Array()
	var normals := PackedVector2Array()
	## The cushion's back for each outline point, on the rail's inner edge, and how far each point's
	## nose may be rounded back before it reaches it.
	var backs := PackedVector2Array()
	var reach := PackedFloat32Array()
	var start_pocket := -1
	var end_pocket := -1
	## From each pocket's centre towards this section's facing.
	var start_u := Vector2.ZERO
	var end_u := Vector2.ZERO
	## Each facing's direction, out of the table.
	var start_axis := Vector2.ZERO
	var end_axis := Vector2.ZERO
	## Whether the cushion runs straight into the pocket and ends on the rail's inner edge (a corner).
	var start_straight := false
	var end_straight := false
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
	_ball_radius = float(dims.get("ball_radius", BALL_RADIUS_FALLBACK))
	var contact := minf(Physics.param("cushion_contact_height_m", CONTACT_HEIGHT_FALLBACK), rail_height - 0.001)
	for i in _sections.size():
		_shape_pocket(_sections[i], _sections[(i + 1) % _sections.size()], rail_width, problems)
	if not problems.is_empty():
		_fail(problems)
		return

	var cloth := Surf.new()
	var wood := Surf.new()
	var net := Surf.new()
	var marks := Surf.new()
	var fittings := Surf.new()
	for section in _sections:
		_add_cushion(cloth, section, contact, rail_height)
		_add_rail(wood, section, rail_height, rail_width)
	_add_bed_and_pockets(cloth, net, fittings, wood, rail_height, rail_width)
	_add_body(wood, dims, rail_width)
	_add_markings(marks, dims)

	var mesh := ArrayMesh.new()
	cloth.commit(mesh, _cloth_material())
	wood.commit(mesh, _wood_material())
	net.commit(mesh, _net_material())
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
		section.start_axis = start["axis"]
		section.end_axis = end["axis"]
		section.start_straight = start["straight"]
		section.end_straight = end["straight"]
		_append(section, start["end"], start["facing_normal"], section.start_straight)
		var arc_points: PackedVector2Array = start["points"]
		var arc_normals: PackedVector2Array = start["normals"]
		for i in arc_points.size():
			_append(section, arc_points[i], arc_normals[i], section.start_straight)
		_append(section, b, n, false)
		arc_points = end["points"]
		arc_normals = end["normals"]
		for i in range(arc_points.size() - 1, -1, -1):
			_append(section, arc_points[i], arc_normals[i], section.end_straight)
		_append(section, end["end"], end["facing_normal"], section.end_straight)
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


## Adds an outline point and the cushion's back behind it. A straight cushion's back is the rail's
## inner edge square behind the point: offset along a tight knuckle's normals, the back would fold
## over itself. Elsewhere the back is where the point's normal first meets the wood: the rail's inner
## edge, or at a middle pocket the wood's end, cut square across the rail JAW_STRIP behind the facing.
func _append(section: Section, p: Vector2, n: Vector2, straight: bool) -> void:
	if not section.points.is_empty() and section.points[section.points.size() - 1].distance_to(p) < 1e-6:
		return
	section.points.append(p)
	section.normals.append(n)
	var depth := (p - section.nose_point).dot(section.nose_normal) + CUSHION_DEPTH
	var square := n.dot(section.nose_normal)
	if straight:
		section.backs.append(p - section.nose_normal * depth)
		section.reach.append(maxf(depth, 0.0) / maxf(square, 0.1))
		return
	# Each middle pocket's cut, as its facing's point and the direction out of the hole across it.
	var cuts: Array[PackedVector2Array] = []
	for start: bool in [true, false]:
		if not (section.start_straight if start else section.end_straight):
			var pocket := _pockets[section.start_pocket if start else section.end_pocket]
			var u: Vector2 = section.start_u if start else section.end_u
			cuts.append(PackedVector2Array([pocket.centre + u * pocket.hole_radius, u]))
	var width := CUSHION_DEPTH
	if depth > 0.0 and square > 1e-3:
		var on_wood := true
		for cut in cuts:
			on_wood = on_wood and (p - n * (depth / square) - cut[0]).dot(cut[1]) >= JAW_STRIP - 1e-9
		if on_wood:
			width = minf(width, depth / square)
	for cut in cuts:
		var behind := (cut[0] - p).dot(cut[1]) + JAW_STRIP
		var across := n.dot(-cut[1])
		if behind > 0.0 and across > 1e-3:
			var hit := p - n * (behind / across)
			if (hit - section.nose_point).dot(section.nose_normal) + CUSHION_DEPTH <= 1e-9:
				width = minf(width, behind / across)
	section.backs.append(p - n * width)
	section.reach.append(width)


## The jaw at a nose's end: the arc from its facing round to the nose, where the facing meets the
## pocket's hole, and where the cushion ends. Empty, with a problem recorded, when the outline
## doesn't join up.
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
		info.hole_radius = to_hole.length()
		# A corner's facing crosses the rail's inner edge before it reaches the hole: the cushion runs
		# straight in and ends there. A middle's jaw curves on past it.
		var along := (-CUSHION_DEPTH - (jaw_end - nose_end).dot(nose_normal)) / minf(axis.dot(nose_normal), -1e-6)
		var straight := along > 0.0 and along < (t - jaw_end).dot(axis)
		var sign := 1.0 if _dir(near_angle).dot(nose_normal) >= 0.0 else -1.0
		var span := wrapf(near_angle - far_angle, -PI, PI)
		var steps := maxi(2, ceili(absf(span) / ARC_STEP))
		var points := PackedVector2Array()
		var normals := PackedVector2Array()
		for i in steps + 1:
			var radial := _dir(far_angle + span * i / steps)
			points.append(c + radial * r)
			normals.append(radial * sign)
		return {"pocket": pocket, "end": jaw_end + axis * along if straight else t, "straight": straight,
				"u": -to_hole.normalized(), "axis": axis, "facing_normal": to_hole.normalized(),
				"points": points, "normals": normals}
	problems.append("no jaw arc meets nose '%s' at %s" % [nose_name, nose_end])
	return {}


## Sets the pocket between sections a and b apart from the outline: corner or middle, its axis, how
## far back its hole reaches, and what surrounds it on the rails.
func _shape_pocket(a: Section, b: Section, rail_width: float, problems: PackedStringArray) -> void:
	var pocket := _pockets[a.end_pocket]
	pocket.corner = a.nose_normal.dot(b.nose_normal) < 0.5
	pocket.axis = (a.end_axis + b.start_axis).normalized()
	pocket.ua = a.end_u
	pocket.ub = b.start_u
	var room := minf(_inside(a, pocket.centre, rail_width), _inside(b, pocket.centre, rail_width))
	pocket.hole_back = minf(pocket.hole_radius, room - BEVEL - LIP - MIN_CAP)
	pocket.net_back = room - NET_CLEARANCE
	if pocket.hole_back < pocket.hole_radius * 0.4:
		problems.append("pocket '%s' is %.4f m from the rail's outer edge, too close for its hole" % [pocket.name, room])
		return
	if pocket.corner:
		_shape_corner(pocket, a, b, rail_width, room, problems)
		return
	var ea := _seam(pocket, a, true, rail_width, 0.0)
	var eb := _seam(pocket, b, false, rail_width, 0.0)
	pocket.edge = PackedVector2Array([ea[ea.size() - 1], eb[eb.size() - 1]])
	pocket.edge_out = PackedVector2Array([-a.nose_normal, -b.nose_normal])


## A corner's horseshoe and rail corner: each cushion ends on its rail's inner edge, and from there
## a cut runs square across the rail to the horseshoe's outer edge.
func _shape_corner(pocket: PocketInfo, a: Section, b: Section, rail_width: float, room: float,
		problems: PackedStringArray) -> void:
	if not (a.end_straight and b.start_straight):
		problems.append("corner pocket '%s': its cushions don't run straight in to their rails' inner edges" % pocket.name)
		return
	var reach := pocket.hole_radius + HORSESHOE_WIDTH
	if reach > room - BEVEL:
		problems.append("corner pocket '%s': its horseshoe reaches past the rail's outer edge" % pocket.name)
		return
	pocket.tips = PackedVector2Array([a.points[a.points.size() - 1], b.points[0]])
	pocket.cuts = PackedVector2Array()
	for side in 2:
		var section := a if side == 0 else b
		var out := -section.nose_normal
		var w := pocket.tips[side] - pocket.centre
		var along := w.dot(out)
		var square := along * along - w.length_squared() + reach * reach
		if square < 0.0 or w.length() >= reach:
			problems.append("corner pocket '%s': a cushion ends outside its horseshoe" % pocket.name)
			return
		pocket.cuts.append(pocket.tips[side] + out * (sqrt(square) - along))
	var na := a.nose_normal
	var nb := b.nose_normal
	var ca := a.nose_point.dot(na) - rail_width
	var cb := b.nose_point.dot(nb) - rail_width
	var det := na.x * nb.y - na.y * nb.x
	var corner := Vector2((ca * nb.y - na.y * cb) / det, (na.x * cb - ca * nb.x) / det)
	pocket.corner_centre = corner + (na + nb) * CORNER_RADIUS
	pocket.corner_out = -(na + nb).normalized()
	pocket.edge = PackedVector2Array()
	pocket.edge_out = PackedVector2Array()
	var start := (-na).angle()
	var span := wrapf((-nb).angle() - start, -PI, PI)
	for i in CORNER_SEGMENTS + 1:
		var d := _dir(start + span * i / CORNER_SEGMENTS)
		pocket.edge.append(pocket.corner_centre + d * CORNER_RADIUS)
		pocket.edge_out.append(d)


## How far p is inside the section's rail's outer edge.
static func _inside(section: Section, p: Vector2, rail_width: float) -> float:
	return (p - section.nose_point).dot(section.nose_normal) + rail_width


## Where a middle pocket's fitting meets the section's wooden rail: from the cushion's back where it
## ends at the pocket (`ending`: the section ends there rather than starts), round a band behind the
## hole, then square out to `inset` in from the rail's outer edge.
func _seam(pocket: PocketInfo, section: Section, ending: bool, rail_width: float, inset: float) -> PackedVector2Array:
	var c := pocket.centre
	var n := section.nose_normal
	var u := section.end_u if ending else section.start_u
	var out := PackedVector2Array([section.backs[section.backs.size() - 1] if ending else section.backs[0]])
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


## Where the section's rail ends at a pocket, from its cushion's back out to BEVEL in from its outer
## edge: round a middle's fitting, or at a corner across the cut, round the horseshoe to the mitre,
## along the mitre and round the rail's corner to its straight edge. side 0 is the rail before the
## pocket, 1 the one after.
func _rail_end(pocket: PocketInfo, section: Section, side: int, rail_width: float) -> PackedVector2Array:
	if not pocket.corner:
		return _seam(pocket, section, side == 0, rail_width, BEVEL)
	var c := pocket.centre
	var reach := pocket.hole_radius + HORSESHOE_WIDTH
	var out := PackedVector2Array([pocket.tips[side], pocket.cuts[side]])
	var from := (pocket.cuts[side] - c).angle()
	var span := wrapf(pocket.axis.angle() - from, -PI, PI)
	for i in range(1, HORSESHOE_SEGMENTS + 1):
		out.append(c + _dir(from + span * i / HORSESHOE_SEGMENTS) * reach)
	var half := int(CORNER_SEGMENTS / 2.0)
	for i in half + 1:
		var d := pocket.corner_out.slerp(-section.nose_normal, float(i) / half)
		out.append(pocket.corner_centre + d * (CORNER_RADIUS - BEVEL))
	return out


## The rail's outer edge path at a pocket, appended to `path`: a middle's fitting's end, or a
## corner's rounded rail corner from its mitre to this rail (ending) or from this rail to its mitre.
static func _edge_part(pocket: PocketInfo, ending: bool, path: PackedVector2Array, outward: PackedVector2Array) -> void:
	if not pocket.corner:
		var k := 0 if ending else pocket.edge.size() - 1
		path.append(pocket.edge[k])
		outward.append(pocket.edge_out[k])
		return
	var half := int(CORNER_SEGMENTS / 2.0)
	var from := 0 if ending else half
	for k in range(from, from + half + 1):
		path.append(pocket.edge[k])
		outward.append(pocket.edge_out[k])


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


## The cushion's cross-section, (s behind the nose, height): its top rising a little from the
## rounded nose to the rail, flush with it, and below the nose the face cut back under it to the
## cloth. From the player's eye that reads as a slim lit nose over a shadow, not a wall; a ball
## against the nose clears the face by about 7 mm at its equator.
func _cushion_profile(contact: float, rail_height: float) -> PackedVector2Array:
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


## The cushion from its back on the rail's inner edge round its nose to the cloth. Where a corner's
## facing closes on the rail's edge, the nose's rounding narrows with it to the cushion's tip.
func _add_cushion(cloth: Surf, section: Section, contact: float, rail_height: float) -> void:
	var profile := _cushion_profile(contact, rail_height)
	var rows: Array[PackedVector2Array] = [section.backs]
	for j in range(1, profile.size()):
		var row := PackedVector2Array()
		for k in section.points.size():
			row.append(section.points[k] - section.normals[k] * minf(profile[j].x, section.reach[k]))
		rows.append(row)
	_sweep(cloth, rows, profile, section.normals, METRES, Color(0.93, 0.93, 0.93))


func _add_rail(wood: Surf, section: Section, rail_height: float, rail_width: float) -> void:
	var start := _pockets[section.start_pocket]
	var end := _pockets[section.end_pocket]
	var start_end := _rail_end(start, section, 1, rail_width)
	var outline := PackedVector2Array()
	for k in range(start_end.size() - 1, -1, -1):
		outline.append(start_end[k])
	outline.append_array(section.backs)
	outline.append_array(_rail_end(end, section, 0, rail_width))
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

	# The rounded outer edge and the apron below it, from one middle fitting or mitre to the next.
	var path := PackedVector2Array()
	var outward := PackedVector2Array()
	_edge_part(start, false, path, outward)
	_edge_part(end, true, path, outward)
	_add_edge(wood, path, outward, _edge_profile(rail_height, APRON_BOTTOM))


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


func _add_bed_and_pockets(cloth: Surf, net: Surf, fittings: Surf, wood: Surf, rail_height: float,
		rail_width: float) -> void:
	# The net hangs from the fitting's lip behind the hole, as the lining did.
	var back_top := rail_height - LIP - LIP_FACE
	for i in _sections.size():
		var section := _sections[i]
		var pocket := _pockets[section.end_pocket]
		_add_fall(cloth, pocket)
		_add_net(net, pocket, back_top)
		if pocket.corner:
			_add_horseshoe(fittings, pocket, rail_height, back_top)
			_add_mitre(wood, pocket, rail_height)
		else:
			_add_fitting(fittings, wood, pocket, section, _sections[(i + 1) % _sections.size()], rail_height, rail_width)
	_add_bed(cloth)


## The bed: under the cushions back to the rails' inner edges, with each pocket's hole cut out to
## where the cloth starts turning down at the fall.
func _add_bed(cloth: Surf) -> void:
	var outline := PackedVector2Array()
	for section in _sections:
		outline.append_array(section.backs)
	var pieces: Array[PackedVector2Array] = [_cleaned(outline)]
	for pocket in _pockets:
		var disc := _fall_dirs(pocket)
		for k in disc.size():
			disc[k] = pocket.centre + disc[k] * (pocket.hole_radius + FALL)
		# Round the back, well clear of the bed, to close the disc.
		for d in _sweep_dirs(pocket.ub, pocket.ua, pocket.axis, POCKET_SEGMENTS):
			disc.append(pocket.centre + d * (pocket.hole_radius + FALL))
		var cut: Array[PackedVector2Array] = []
		for piece in pieces:
			cut.append_array(Geometry2D.clip_polygons(piece, _cleaned(disc)))
		pieces = cut
	for piece in pieces:
		var triangles := Geometry2D.triangulate_polygon(piece)
		if triangles.is_empty():
			Log.error("app", "app.error", {"kind": "message",
					"message": "table_view: a piece of the bed doesn't triangulate (%d points of %d pieces)" % [piece.size(), pieces.size()]})
			continue
		var first := cloth.verts.size()
		for p in piece:
			cloth.vertex(_g(p, 0.0), Vector3.UP, Vector2(p.x, p.y))
		for i in range(0, triangles.size(), 3):
			cloth.tri(first + triangles[i], first + triangles[i + 1], first + triangles[i + 2])


## Directions from `from` round to `to` the way that passes `via`, count steps.
static func _sweep_dirs(from: Vector2, to: Vector2, via: Vector2, count: int) -> PackedVector2Array:
	var start := from.angle()
	var turn := fposmod(to.angle() - start, TAU)
	if fposmod(via.angle() - start, TAU) > turn:
		turn -= TAU
	var out := PackedVector2Array()
	for i in count + 1:
		out.append(_dir(start + turn * i / count))
	return out


## Directions from the pocket's centre round the front of its hole, the table side, from where it
## touches one facing to the other.
static func _fall_dirs(pocket: PocketInfo) -> PackedVector2Array:
	return _sweep_dirs(pocket.ua, pocket.ub, -pocket.axis, POCKET_SEGMENTS)


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


## The hole's edge behind the fall, from where it touches the facing before the pocket round the
## back: a corner's circle, or a middle's half-ellipse.
static func _back_ring(pocket: PocketInfo) -> PackedVector2Array:
	if not pocket.corner:
		return _back(pocket, pocket.ua, 0.0)
	var out := PackedVector2Array()
	for d in _sweep_dirs(pocket.ua, pocket.ub, pocket.axis, POCKET_SEGMENTS):
		out.append(pocket.centre + d * pocket.hole_radius)
	return out


## The cloth turning down over the slate's rounded edge round the front of the hole.
func _add_fall(cloth: Surf, pocket: PocketInfo) -> void:
	var profile := PackedVector2Array()
	for i in 5:
		var angle := PI / 2.0 * i / 4.0
		profile.append(Vector2(FALL * (1.0 - sin(angle)), -FALL * (1.0 - cos(angle))))
	profile.append(Vector2(0.0, -FALL_DEPTH))
	var front := _fall_dirs(pocket)
	_sweep(cloth, _radial_rows(pocket.centre, front, profile, pocket.hole_radius), profile, _negated(front), METRES)


## The net hanging in the hole, from the turned cloth at the front and the fitting's lip at the back
## (back_top), a bag curving in to its dark opening: the crochet shaded under its rim, light in its
## bowl and darker as it closes, and never closer to a resting potted ball than NET_BALL_CLEARANCE.
func _add_net(net: Surf, pocket: PocketInfo, back_top: float) -> void:
	var c := pocket.centre
	var ring := PackedVector2Array()
	var tops := PackedFloat32Array()
	for d in _fall_dirs(pocket):
		ring.append(c + d * pocket.hole_radius)
		tops.append(-FALL_DEPTH)
	var back := _back_ring(pocket)
	for k in range(back.size() - 1, -1, -1):
		ring.append(back[k])
		tops.append(back_top)
	ring.append(ring[0])
	tops.append(tops[0])
	var columns := ring.size()
	var dirs := PackedVector2Array()
	var along := PackedFloat32Array()
	var u := 0.0
	for k in columns:
		dirs.append((ring[k] - c).normalized())
		if k > 0:
			u += ring[k].distance_to(ring[k - 1])
		along.append(u)
	# A whole number of tiles round the ring, so the crochet closes without a seam.
	var wrap := maxf(1.0, roundf(u / NET_TILE)) * NET_TILE / u
	for k in columns:
		along[k] *= wrap
	var rows: Array[PackedVector3Array] = []
	var shades := PackedFloat32Array([NET_RIM_SHADE])
	var top := PackedVector3Array()
	for k in columns:
		top.append(_g(ring[k], tops[k]))
	rows.append(top)
	var potted := _ball_radius - BallsView.DROP_DEPTH
	for row in NET_ROWS:
		var below := row.y - potted
		var ball := sqrt(maxf(0.0, _ball_radius * _ball_radius - below * below))
		var across := maxf(pocket.hole_radius * row.x, ball + NET_BALL_CLEARANCE)
		var points := PackedVector3Array()
		for k in columns:
			points.append(_g(c + dirs[k] * _hole_reach(pocket, dirs[k], across, minf(across, pocket.net_back)), row.y))
		rows.append(points)
		shades.append(row.z)
	var first := net.verts.size()
	var down := PackedFloat32Array()
	down.resize(columns)
	var last_row := rows.size() - 1
	for j in rows.size():
		for k in columns:
			if j > 0:
				# As the net closes, its diamonds shrink the same way down as round, as in a real
				# net, rather than thinning into rings.
				var closing := 0.5 * (NET_ROWS[j - 1].x + (NET_ROWS[j - 2].x if j > 1 else 1.0))
				down[k] += rows[j][k].distance_to(rows[j - 1][k]) / closing
			# The ring closes on itself: its last column is its first.
			var circling := rows[j][k + 1 if k + 1 < columns else 1] - rows[j][k - 1 if k > 0 else columns - 2]
			var sag := rows[mini(j + 1, last_row)][k] - rows[maxi(j - 1, 0)][k]
			var n := circling.cross(sag)
			if n.dot(_g(-dirs[k], 0.0)) < 0.0:
				n = -n
			n = (n.normalized() + Vector3.UP * NET_TILT).normalized()
			net.vertex(rows[j][k], n, Vector2(along[k], down[k]), Color(shades[j], shades[j], shades[j]))
	net.grid(first, rows.size(), columns)
	var last := rows[rows.size() - 1]
	var middle := net.vertex(_g(c, NET_BOTTOM), Vector3.UP, Vector2.ZERO, NET_DARK)
	for k in columns:
		net.vertex(last[k], Vector3.UP, Vector2(along[k], 0.0), NET_DARK)
	for k in columns - 1:
		net.tri(middle, middle + 1 + k, middle + 2 + k)


## How far from the pocket's centre, in direction d, an egg `across` wide reaches: round in front,
## `back` deep behind.
static func _hole_reach(pocket: PocketInfo, d: Vector2, across: float, back: float) -> float:
	var along := d.dot(pocket.axis)
	if along <= 0.0 or back >= across:
		return across
	var side := d.dot(pocket.axis.orthogonal())
	return 1.0 / sqrt(pow(side / across, 2.0) + pow(along / back, 2.0))


## The leather cap over the rail behind a middle pocket (between sections a and b): a low dome from
## the cushions' ends round the back of the hole, rolling over a lip into the hole and over the rail's
## edge a little way down the apron.
func _add_fitting(fittings: Surf, wood: Surf, pocket: PocketInfo, a: Section, b: Section,
		rail_height: float, rail_width: float) -> void:
	var c := pocket.centre
	var u := a.end_u
	# The seam starts along the cushion's end, which the band doesn't reach.
	var seam_a := _seam(pocket, a, true, rail_width, BEVEL)
	var seam_b := _seam(pocket, b, false, rail_width, BEVEL)
	var outer := PackedVector2Array()
	for k in range(1, seam_a.size()):
		outer.append(seam_a[k])
	for k in pocket.edge.size():
		outer.append(pocket.edge[k] - pocket.edge_out[k] * BEVEL)
	for k in range(seam_b.size() - 1, 0, -1):
		outer.append(seam_b[k])
	_add_dome(fittings, c, u, pocket.axis, 0.0, PI, _back(pocket, u, LIP), _cleaned(outer), rail_height, true)

	# Where each cushion ends: the fitting's leather down its end to the turned cloth.
	var edge_shade := Color(FITTING_EDGE_SHADE, FITTING_EDGE_SHADE, FITTING_EDGE_SHADE)
	var ends: Array[PackedVector2Array] = [PackedVector2Array([a.points[a.points.size() - 1], a.backs[a.backs.size() - 1]]),
			PackedVector2Array([b.points[0], b.backs[0]])]
	for end in ends:
		var near := end[0]
		var far := end[1]
		var width := near.distance_to(far)
		var n := _g(-pocket.axis, 0.0)
		var first := fittings.vertex(_g(near, -FALL_DEPTH), n, Vector2.ZERO, edge_shade)
		fittings.vertex(_g(far, -FALL_DEPTH), n, Vector2(width, 0.0), edge_shade)
		fittings.vertex(_g(far, rail_height), n, Vector2(width, rail_height + FALL_DEPTH), edge_shade)
		fittings.vertex(_g(near, rail_height), n, Vector2(0.0, rail_height + FALL_DEPTH), edge_shade)
		fittings.tri(first, first + 1, first + 2)
		fittings.tri(first, first + 2, first + 3)

	var lip := PackedVector2Array()
	for i in 5:
		var angle := PI / 2.0 * i / 4.0
		lip.append(Vector2(LIP * (1.0 - sin(angle)), rail_height - LIP + LIP * cos(angle)))
	lip.append(Vector2(0.0, rail_height - LIP - LIP_FACE))
	var rows: Array[PackedVector2Array] = []
	var shades := PackedColorArray()
	for p in lip:
		rows.append(_back(pocket, u, p.x))
		shades.append(edge_shade.darkened(0.6 * (rail_height - p.y) / (LIP + LIP_FACE)))
	_sweep(fittings, rows, lip, _back_inward(pocket, u), METRES, Color.WHITE, shades)

	_add_edge(fittings, pocket.edge, pocket.edge_out, _edge_profile(rail_height, rail_height - CAP_WRAP), edge_shade)
	_add_edge(wood, pocket.edge, pocket.edge_out,
			PackedVector2Array([Vector2(0.0, rail_height - CAP_WRAP), Vector2(0.0, APRON_BOTTOM)]))


## A corner's horseshoe: padded leather round the back of the hole, in a cut-out where the rails
## meet. Its arms run up the facings to the tips, where the cushions end, and its lip rolls into the
## hole round the back, growing from nothing at the tips.
func _add_horseshoe(fittings: Surf, pocket: PocketInfo, rail_height: float, back_top: float) -> void:
	var c := pocket.centre
	var r := pocket.hole_radius
	# The inner edge, tip to tip: up the first facing, round the back, down the other. The hole's
	# edge where it touches each facing comes twice, where the leather's foot steps from the turned
	# cloth up to the net.
	var inner := PackedVector2Array([pocket.tips[0], c + pocket.ua * r])
	var sizes := PackedFloat32Array([0.0, LIP])
	var outward := PackedVector2Array([pocket.ua, pocket.ua])
	var feet := PackedFloat32Array([-FALL_DEPTH, -FALL_DEPTH])
	for p in _back_ring(pocket):
		inner.append(p)
		sizes.append(LIP)
		outward.append((p - c).normalized())
		feet.append(back_top)
	inner.append_array(PackedVector2Array([c + pocket.ub * r, pocket.tips[1]]))
	sizes.append_array(PackedFloat32Array([LIP, 0.0]))
	outward.append_array(PackedVector2Array([pocket.ub, pocket.ub]))
	feet.append_array(PackedFloat32Array([-FALL_DEPTH, -FALL_DEPTH]))

	var edge_shade := Color(FITTING_EDGE_SHADE, FITTING_EDGE_SHADE, FITTING_EDGE_SHADE)
	var first := fittings.verts.size()
	var columns := inner.size()
	for i in LIP_STEPS + 2:
		var angle := PI / 2.0 * mini(i, LIP_STEPS) / LIP_STEPS
		var along := 0.0
		for k in columns:
			if k > 0:
				along += inner[k].distance_to(inner[k - 1])
			var s := sizes[k] * (1.0 - sin(angle))
			var z := rail_height - sizes[k] * (1.0 - cos(angle)) if i <= LIP_STEPS else feet[k]
			var n := Vector3.UP * cos(angle) - _g(outward[k], 0.0) * sin(angle)
			var shade := edge_shade.darkened(minf(0.6, 0.6 * (rail_height - z) / (LIP + LIP_FACE)))
			fittings.vertex(_g(inner[k] + outward[k] * s, z), n.normalized(), Vector2(along, rail_height - z), shade)
	fittings.grid(first, LIP_STEPS + 2, columns)

	var lip_top := PackedVector2Array()
	for k in columns:
		lip_top.append(inner[k] + outward[k] * sizes[k])
	var reach := r + HORSESHOE_WIDTH
	var outer := PackedVector2Array([pocket.tips[0]])
	for d in _sweep_dirs((pocket.cuts[0] - c).normalized(), (pocket.cuts[1] - c).normalized(), pocket.axis, HORSESHOE_SEGMENTS * 2):
		outer.append(c + d * reach)
	outer.append(pocket.tips[1])
	var across := pocket.axis.orthogonal()
	var ends := PackedFloat32Array()
	for p in pocket.tips:
		ends.append(atan2((p - c).dot(across), (p - c).dot(pocket.axis)))
	_add_dome(fittings, c, pocket.axis, across, minf(ends[0], ends[1]), maxf(ends[0], ends[1]),
			_cleaned(lip_top), outer, rail_height, false)


## The joint where the two rails meet behind a corner's horseshoe: each rail's grain turns there, and
## a fine dark line marks the mitre from the horseshoe to the rail's outer corner.
func _add_mitre(wood: Surf, pocket: PocketInfo, rail_height: float) -> void:
	var from := pocket.centre + pocket.axis * (pocket.hole_radius + HORSESHOE_WIDTH)
	var to := pocket.corner_centre + pocket.corner_out * (CORNER_RADIUS - BEVEL)
	var side := (to - from).normalized().orthogonal() * MITRE_WIDTH / 2.0
	var shade := Color(MITRE_SHADE, MITRE_SHADE, MITRE_SHADE)
	var first := wood.vertex(_g(from - side, rail_height + MARK_LIFT), Vector3.UP, from - side, shade)
	wood.vertex(_g(from + side, rail_height + MARK_LIFT), Vector3.UP, from + side, shade)
	wood.vertex(_g(to + side, rail_height + MARK_LIFT), Vector3.UP, to + side, shade)
	wood.vertex(_g(to - side, rail_height + MARK_LIFT), Vector3.UP, to - side, shade)
	wood.tri(first, first + 1, first + 2)
	wood.tri(first, first + 2, first + 3)


## A fitting's padded top, from `inner_line` (the lip's top edge) out to `outer_line`, flush with the
## rail along both and rising DOME between, shaded darker towards them. Its columns run out from
## `centre` at angles from lo to hi, measured from x_dir towards y_dir, one through every corner of
## either line so none is cut. With `walls`, its two ends stand on the cushions' ends.
func _add_dome(surf: Surf, centre: Vector2, x_dir: Vector2, y_dir: Vector2, lo: float, hi: float,
		inner_line: PackedVector2Array, outer_line: PackedVector2Array, rail_height: float, walls: bool) -> void:
	var c := centre
	var angles := PackedFloat64Array()
	var count := ceili(DOME_COLUMNS * (hi - lo) / PI)
	for i in count + 1:
		angles.append(lerpf(lo, hi, float(i) / count))
	for line: PackedVector2Array in [inner_line, outer_line]:
		for p in line:
			angles.append(clampf(atan2((p - c).dot(y_dir), (p - c).dot(x_dir)), lo, hi))
	angles.sort()
	var inner := PackedVector2Array()
	var outer := PackedVector2Array()
	var previous := -INF
	for angle in angles:
		if angle - previous < 1e-6:
			continue
		previous = angle
		var d := x_dir * cos(angle) + y_dir * sin(angle)
		var low_end := angle - lo < hi - angle
		inner.append(_ray_hit(c, d, inner_line, low_end))
		outer.append(_ray_hit(c, d, outer_line, low_end))
	var grid: Array[PackedVector3Array] = []
	for j in DOME_ROWS + 1:
		var f := float(j) / DOME_ROWS
		var row := PackedVector3Array()
		for k in inner.size():
			var rise := DOME * sin(PI * f) * minf(1.0, inner[k].distance_to(outer[k]) / DOME_RAMP)
			row.append(_g(inner[k].lerp(outer[k], f), rail_height + rise))
		grid.append(row)
	var first := surf.verts.size()
	var last := inner.size() - 1
	for j in grid.size():
		var shade := FITTING_EDGE_SHADE + (1.0 - FITTING_EDGE_SHADE) * sqrt(sin(PI * j / DOME_ROWS))
		for k in inner.size():
			var along := grid[j][mini(k + 1, last)] - grid[j][maxi(k - 1, 0)]
			var across := grid[mini(j + 1, DOME_ROWS)][k] - grid[maxi(j - 1, 0)][k]
			var n := along.cross(across)
			n = Vector3.UP if n.length() < 1e-9 else n.normalized()
			if n.y < 0.0:
				n = -n
			var p := grid[j][k]
			surf.vertex(p, n, Vector2(-p.z, -p.x), Color(shade, shade, shade))
	surf.grid(first, grid.size(), inner.size())
	if not walls:
		return
	var edge_shade := Color(FITTING_EDGE_SHADE, FITTING_EDGE_SHADE, FITTING_EDGE_SHADE)
	var facing := _g(-y_dir, 0.0)
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


## Where a ray from c along d first crosses the line; the line's end on the ray's side when it misses.
static func _ray_hit(c: Vector2, d: Vector2, line: PackedVector2Array, low_end: bool) -> Vector2:
	var hit := line[0] if low_end else line[line.size() - 1]
	var nearest := INF
	for k in range(1, line.size()):
		var at: Variant = Geometry2D.segment_intersects_segment(c, c + d, line[k - 1], line[k])
		if at != null and (at as Vector2).distance_to(c) < nearest:
			nearest = (at as Vector2).distance_to(c)
			hit = at
	return hit


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
				"message": "table_view: %s didn't load; the table is drawn without it" % path, "file": path})
	return texture


func _marking_material() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.46, 0.52, 0.46)
	m.roughness = 1.0
	m.metallic_specular = 0.1
	return m
