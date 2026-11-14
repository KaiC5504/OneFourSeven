extends TestCase

const NOSE_TOLERANCE := 0.0005
# How finely the outline is walked when checking the drawn nose covers it: the jaws closely, the
# straight noses, drawn as one segment each, more coarsely.
const OUTLINE_STEP := 0.002
const NOSE_STEP := 0.05


# The binding's outline once it's configured, else the same Dictionary built from the data file.
func _shape() -> Dictionary:
	var shape := Physics.table_shape()
	if shape.is_empty():
		shape = TableView.shape_from_json(FileAccess.get_file_as_string(Physics.TABLE_SHAPE_PATH))
	check(not shape.is_empty(), "a table shape to build from")
	return shape


func _build(shape: Dictionary) -> TableView:
	var table := TableView.new()
	tree.root.add_child(table)
	table.build(shape, OfsTable.dimensions())
	return table


func _mesh(table: TableView) -> ArrayMesh:
	var instance := table.get_node_or_null("Mesh") as MeshInstance3D
	check(instance != null, "the table has its merged mesh")
	return instance.mesh as ArrayMesh if instance != null else null


static func _v2(value: Variant) -> Vector2:
	return Vector2(float(value[0]), float(value[1]))


static func _half() -> Vector2:
	var dims := OfsTable.dimensions()
	return Vector2(float(dims["playing_length"]), float(dims["playing_width"])) / 2.0


# Distance from p to the outline's nearest line segment or arc.
static func _outline_distance(shape: Dictionary, p: Vector2) -> float:
	var best := INF
	for line: Dictionary in shape["lines"]:
		var a := _v2(line["p1"])
		var b := _v2(line["p2"])
		best = minf(best, p.distance_to(Geometry2D.get_closest_point_to_segment(p, a, b)))
	for arc: Dictionary in shape["arcs"]:
		var c := _v2(arc["centre"])
		var r := float(arc["radius"])
		var start := float(arc["angle_start"])
		var span := float(arc["angle_end"]) - start
		var along := fposmod((p - c).angle() - start, TAU)
		if along <= span:
			best = minf(best, absf(p.distance_to(c) - r))
		else:
			best = minf(best, minf(p.distance_to(c + Vector2.from_angle(start) * r),
					p.distance_to(c + Vector2.from_angle(start + span) * r)))
	return best


# Points every OUTLINE_STEP along the cushion noses and jaw arcs.
static func _outline_samples(shape: Dictionary) -> PackedVector2Array:
	var out := PackedVector2Array()
	for line: Dictionary in shape["lines"]:
		if String(line["kind"]) != "nose":
			continue
		var a := _v2(line["p1"])
		var b := _v2(line["p2"])
		var steps := ceili(a.distance_to(b) / NOSE_STEP)
		for i in steps + 1:
			out.append(a.lerp(b, float(i) / steps))
	for arc: Dictionary in shape["arcs"]:
		var c := _v2(arc["centre"])
		var r := float(arc["radius"])
		var start := float(arc["angle_start"])
		var span := float(arc["angle_end"]) - start
		var steps := ceili(r * span / OUTLINE_STEP)
		for i in steps + 1:
			out.append(c + Vector2.from_angle(start + span * i / steps) * r)
	return out


# The hole drawn behind the fall: from the drop centre to the pocket's nearest facing.
static func _hole_radius(shape: Dictionary, pocket: Dictionary) -> float:
	var centre := _v2(pocket["drop_centre"])
	var best := INF
	for line: Dictionary in shape["lines"]:
		if String(line["kind"]) == "facing" and String(line["name"]).begins_with(String(pocket["name"])):
			var a := _v2(line["p1"])
			var along := (_v2(line["p2"]) - a).normalized()
			best = minf(best, absf((centre - a).cross(along)))
	return best


static func _flat(v: Vector3) -> Vector2:
	return TableSpace.godot_to_core(v)


func test_shape_from_json_matches_the_binding_layout() -> void:
	var shape := TableView.shape_from_json(FileAccess.get_file_as_string(Physics.TABLE_SHAPE_PATH))
	check_eq((shape["pockets"] as Array).size(), 6, "pockets")
	check_eq((shape["arcs"] as Array).size(), 12, "arcs")
	check_eq((shape["lines"] as Array).size(), 18, "lines")
	check_near(float(shape["rail_width"]), 0.14, 1e-9, "rail width")
	for arc: Dictionary in shape["arcs"]:
		var pocket := int(arc["pocket"])
		var name := String(arc["name"])
		check(name.begins_with(String(shape["pockets"][pocket]["name"])), "arc %s maps to pocket %d" % [name, pocket])
	check(TableView.shape_from_json("not json").is_empty(), "garbage gives an empty shape")


func test_one_pocket_marker_at_each_drop_centre() -> void:
	var shape := _shape()
	var table := _build(shape)
	var markers: Array[Node] = []
	for child in table.get_children():
		if child.name.begins_with("Pocket_"):
			markers.append(child)
	check_eq(markers.size(), (shape["pockets"] as Array).size(), "pocket markers")
	for i in (shape["pockets"] as Array).size():
		var pocket: Dictionary = shape["pockets"][i]
		var marker := table.get_node_or_null("Pocket_" + String(pocket["name"])) as Node3D
		check(marker != null, "marker for %s" % pocket["name"])
		if marker == null:
			continue
		var at := TableSpace.godot_to_core(marker.position)
		var want := _v2(pocket["drop_centre"])
		check(at.distance_to(want) < 1e-5, "%s marker at %s, drop centre %s" % [pocket["name"], at, want])
		check_eq(int(marker.get_meta("pocket", -1)), i, "%s index meta" % pocket["name"])
	table.free()


func test_each_pocket_holds_a_net_bag_light_above_a_dark_opening() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var arrays := mesh.surface_get_arrays(TableView.SURFACE_NET)
	var net: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var shades: PackedColorArray = arrays[Mesh.ARRAY_COLOR]
	var cloth: PackedVector3Array = mesh.surface_get_arrays(TableView.SURFACE_CLOTH)[Mesh.ARRAY_VERTEX]
	var ball := float(OfsTable.dimensions()["ball_radius"])
	var rim := -TableView.FALL_DEPTH - 1e-4
	# The crochet itself is light: brighter than the cloth, nothing like the black lining it replaced.
	var material := mesh.surface_get_material(TableView.SURFACE_NET) as StandardMaterial3D
	var image: Image = material.albedo_texture.get_image() if material != null and material.albedo_texture != null else null
	check(image != null, "the net has its crochet texture")
	if image != null:
		var total := 0.0
		for y in image.get_height():
			for x in image.get_width():
				total += image.get_pixel(x, y).get_luminance()
		var mean := total / (image.get_width() * image.get_height())
		check(mean > 0.5 and mean > TableView.CLOTH_GREEN.get_luminance(), "the crochet reads light: %f" % mean)
	for pocket: Dictionary in shape["pockets"]:
		var name := String(pocket["name"])
		var centre := _v2(pocket["drop_centre"])
		var reach := float(pocket["drop_radius"]) + 0.01
		var count := 0
		var deepest := 0.0
		var deepest_lit := 0.0
		var rim_shade := 0.0
		var brightest := 0.0
		var opening_shade := 1.0
		var closest := INF
		# How far the bag reaches from the pocket's centre at each depth below its rim, by 0.1 mm.
		var widths := {}
		# Where BallsView leaves a potted ball, at the end of its drop.
		var potted := TableSpace.core_to_godot(centre.x, centre.y, ball - BallsView.DROP_DEPTH)
		for i in net.size():
			var v := net[i]
			var flat := _flat(v)
			if flat.distance_to(centre) > reach:
				continue
			count += 1
			deepest = minf(deepest, v.y)
			closest = minf(closest, v.distance_to(potted))
			if v.y >= -TableView.FALL_DEPTH - 1e-6:
				rim_shade = maxf(rim_shade, shades[i].v)
			brightest = maxf(brightest, shades[i].v)
			if shades[i].v > 0.1:
				deepest_lit = minf(deepest_lit, v.y)
			if flat.distance_to(centre) < 1e-4:
				opening_shade = minf(opening_shade, shades[i].v)
			if v.y < rim:
				var depth := roundi(v.y * 1e4)
				widths[depth] = maxf(float(widths.get(depth, 0.0)), flat.distance_to(centre))
		check(count > 40, "%s has its net (%d vertices)" % [name, count])
		check(deepest_lit < -0.05 and deepest_lit > -0.075,
				"%s's net is lit down to 5-7.5 cm, a bag rather than a pit: %f" % [name, deepest_lit])
		check(deepest < -BallsView.DROP_DEPTH and deepest > -0.1,
				"%s's net's dark opening runs on just below where a potted ball comes to rest: %f" % [name, deepest])
		check(brightest > 0.9, "%s's net is light where the light reaches into it: %f" % [name, brightest])
		check(rim_shade > 0.4 and rim_shade < brightest, "%s's net is shaded under its rim, %f against %f" % [name, rim_shade, brightest])
		check(opening_shade < 0.05, "%s's net has a dark opening at its bottom centre: %f" % [name, opening_shade])
		check(closest >= ball - 1e-4, "%s's net holds a potted ball clear of it: %f" % [name, closest])
		# A bag: from where it sags widest under the rim it narrows all the way down.
		var depths := widths.keys()
		depths.sort()
		depths.reverse()
		var widest := 0.0
		var narrower := true
		var previous := INF
		for depth: int in depths:
			var width := float(widths[depth])
			if width >= widest:
				widest = width
				previous = width
				continue
			narrower = narrower and width <= previous + 1e-6
			previous = width
		var bottom_ring := float(widths[depths[depths.size() - 2]]) if depths.size() > 1 else INF
		check(narrower, "%s's net narrows with depth below its widest" % name)
		check(widest > _hole_radius(shape, pocket) and bottom_ring < 0.5 * widest,
				"%s's net sags out under the rim (%f) and closes to under half that (%f)" % [name, widest, bottom_ring])
		# No cloth at the cloth's level inside the hole.
		var hole := _hole_radius(shape, pocket)
		var inside := 0
		for v in cloth:
			if absf(v.y) < 1e-6 and _flat(v).distance_to(centre) < hole - 0.001:
				inside += 1
		check_eq(inside, 0, "%s cloth vertices inside the hole" % name)
	table.free()


func test_the_table_is_one_mesh_of_five_surfaces() -> void:
	var table := _build(_shape())
	var mesh := _mesh(table)
	var drawn := 0
	for child in table.get_children():
		if child is GeometryInstance3D:
			drawn += 1
	check_eq(drawn, 1, "one mesh instance")
	if mesh != null:
		check(mesh.get_surface_count() <= 5, "at most five draw calls: %d" % mesh.get_surface_count())
		check_eq(mesh.get_surface_count(), TableView.SURFACE_FITTINGS + 1, "cloth, wood, nets, markings and fittings")
	table.free()


func test_each_pocket_has_a_fitting_behind_the_cushions_inside_the_rail() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var half := _half()
	var rail_width := float(shape["rail_width"])
	var rail_height := float(shape["rail_height"])
	var fittings: PackedVector3Array = mesh.surface_get_arrays(TableView.SURFACE_FITTINGS)[Mesh.ARRAY_VERTEX]
	var in_play := 0
	var outside := 0
	var highest := -INF
	for v in fittings:
		var p := _flat(v)
		if absf(p.x) < half.x - 1e-6 and absf(p.y) < half.y - 1e-6:
			in_play += 1
		if absf(p.x) > half.x + rail_width + 1e-6 or absf(p.y) > half.y + rail_width + 1e-6:
			outside += 1
		highest = maxf(highest, v.y)
	check_eq(in_play, 0, "fitting vertices in front of the cushion noses")
	check_eq(outside, 0, "fitting vertices outside the rail's outer edge")
	check(highest > rail_height and highest < rail_height + 0.01, "the fittings sit just proud of the rail: %f" % highest)
	for pocket: Dictionary in shape["pockets"]:
		var centre := _v2(pocket["drop_centre"])
		var near := 0
		var on_edge := 0
		for v in fittings:
			var p := _flat(v)
			if p.distance_to(centre) < 0.2:
				near += 1
				if absf(absf(p.y) - (half.y + rail_width)) < 1e-6 or absf(absf(p.x) - (half.x + rail_width)) < 1e-6:
					on_edge += 1
		check(near > 100, "%s has its fitting (%d vertices)" % [pocket["name"], near])
		if not bool(pocket["corner"]):
			check(on_edge > 0, "%s's fitting wraps over the rail's outer edge" % pocket["name"])
	table.free()


func test_each_corner_has_a_horseshoe_behind_its_hole_in_mitred_rails() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var half := _half()
	var rail_width := float(shape["rail_width"])
	var rail_height := float(shape["rail_height"])
	var fittings: PackedVector3Array = mesh.surface_get_arrays(TableView.SURFACE_FITTINGS)[Mesh.ARRAY_VERTEX]
	var wood: PackedVector3Array = mesh.surface_get_arrays(TableView.SURFACE_WOOD)[Mesh.ARRAY_VERTEX]
	for pocket: Dictionary in shape["pockets"]:
		if not bool(pocket["corner"]):
			continue
		var name := String(pocket["name"])
		var centre := _v2(pocket["drop_centre"])
		var signs := Vector2(signf(centre.x), signf(centre.y))
		var axis := (centre - half * signs).normalized()
		var across := axis.orthogonal()
		var outer_corner := (half + Vector2.ONE * rail_width) * signs
		var hole := _hole_radius(shape, pocket)
		var band := hole + TableView.HORSESHOE_WIDTH
		var inner_edge := half + Vector2.ONE * TableView.CUSHION_DEPTH
		var count := 0
		var widest := 0.0
		var in_front := 0
		var furthest_back := -INF
		var sides := Vector2.ZERO
		var by_the_corner := 0
		for v in fittings:
			var p := _flat(v)
			if p.distance_to(centre) > 0.25:
				continue
			count += 1
			widest = maxf(widest, p.distance_to(centre))
			if absf(p.x) < inner_edge.x - 1e-6 and absf(p.y) < inner_edge.y - 1e-6:
				in_front += 1
			furthest_back = maxf(furthest_back, (p - centre).dot(axis))
			sides = Vector2(minf(sides.x, (p - centre).dot(across)), maxf(sides.y, (p - centre).dot(across)))
			if p.distance_to(outer_corner) < 0.05:
				by_the_corner += 1
		check(count > 100, "%s has its horseshoe (%d vertices)" % [name, count])
		check(widest < band + 1e-4, "%s's horseshoe is a band round the hole, %f from its centre at most" % [name, widest])
		# Its arms run up the facings from where the cushions end on the rails' inner edges.
		check_eq(in_front, 0, "%s's horseshoe vertices in front of both rails' inner edges" % name)
		check(furthest_back > band - 0.005, "%s's horseshoe wraps round the back of the hole: %f" % [name, furthest_back])
		check(sides.x < -hole and sides.y > hole, "%s's horseshoe reaches round both sides: %s" % [name, sides])
		check_eq(by_the_corner, 0, "%s's fitting vertices at the rail's outer corner" % name)
		# Wood at the rail's outer corner, and the mitre's joint on the diagonal from the horseshoe to it.
		var corner_wood := 0
		var mitre := 0
		for v in wood:
			var p := _flat(v)
			if p.distance_to(outer_corner) < 0.01:
				corner_wood += 1
			if v.y > rail_height and absf((p - centre).dot(across)) < 0.001 and (p - centre).dot(axis) > band - 1e-4:
				mitre += 1
		check(corner_wood > 0, "%s's rails meet in wood at the outer corner" % name)
		check(mitre >= 2, "%s's rails are mitred on the diagonal behind the horseshoe (%d joint vertices)" % [name, mitre])
	table.free()


func test_the_cushions_run_straight_into_the_corners() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var half := _half() + Vector2.ONE * TableView.CUSHION_DEPTH
	var cloth: PackedVector3Array = mesh.surface_get_arrays(TableView.SURFACE_CLOTH)[Mesh.ARRAY_VERTEX]
	for pocket: Dictionary in shape["pockets"]:
		if not bool(pocket["corner"]):
			continue
		var centre := _v2(pocket["drop_centre"])
		var beyond := 0
		var cushion := 0
		for v in cloth:
			var p := _flat(v)
			if v.y < 0.001 or p.distance_to(centre) > 0.2:
				continue
			cushion += 1
			if absf(p.x) > half.x + 1e-6 or absf(p.y) > half.y + 1e-6:
				beyond += 1
		check(cushion > 50, "%s has its cushions" % pocket["name"])
		check_eq(beyond, 0, "%s cushion vertices past the rail's inner edge" % pocket["name"])
	table.free()


func test_every_rail_top_is_drawn() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var rail_height := float(shape["rail_height"])
	var rail_width := float(shape["rail_width"])
	var arrays := mesh.surface_get_arrays(TableView.SURFACE_WOOD)
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	var area := 0.0
	for i in range(0, indices.size(), 3):
		var a := verts[indices[i]]
		var b := verts[indices[i + 1]]
		var c := verts[indices[i + 2]]
		if absf(a.y - rail_height) < 1e-6 and absf(b.y - rail_height) < 1e-6 and absf(c.y - rail_height) < 1e-6:
			area += (b - a).cross(c - a).length() / 2.0
	# The rails' band round the table, less the fittings and the rounded edge.
	var inner := (_half() + Vector2.ONE * TableView.CUSHION_DEPTH) * 2.0
	var outer := (_half() + Vector2.ONE * rail_width) * 2.0
	var band := outer.x * outer.y - inner.x * inner.y
	check(area > 0.8 * band and area < band, "the rail tops cover %.3f of %.3f m2" % [area, band])
	table.free()


func test_the_rails_outer_edge_runs_straight_past_the_middles() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var half_width := float(OfsTable.dimensions()["playing_width"]) / 2.0 + float(shape["rail_width"])
	check_near(table.bounds().end.x, half_width, 1e-4, "the left rail's outer edge")
	check_near(-table.bounds().position.x, half_width, 1e-4, "the right rail's outer edge")
	for surface: int in [TableView.SURFACE_WOOD, TableView.SURFACE_FITTINGS, TableView.SURFACE_NET]:
		var widest := 0.0
		for v: Vector3 in mesh.surface_get_arrays(surface)[Mesh.ARRAY_VERTEX]:
			widest = maxf(widest, absf(TableSpace.godot_to_core(v).y))
		check(widest <= half_width + 1e-6, "surface %d stays inside the side rails: %f" % [surface, widest])
	table.free()


# The drawn nose is a polyline at the contact height: each of its vertices lies on the outline, and
# every point of every nose line and jaw arc lies on one of its segments. Checking the outline's
# points against the nearest vertex instead would fail on any arc whose middle falls between two
# vertices: half a step from the nearest on the 1 in corner knuckle (0.77 mm).
func test_cushion_noses_follow_the_outline() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var contact := minf(Physics.param("cushion_contact_height_m", TableView.CONTACT_HEIGHT_FALLBACK),
			float(shape["rail_height"]) - 0.001)
	var arrays := mesh.surface_get_arrays(TableView.SURFACE_CLOTH)
	var cloth: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	var on_nose := {}
	var worst := 0.0
	for i in cloth.size():
		if absf(cloth[i].y - contact) < 1e-6:
			on_nose[i] = true
			worst = maxf(worst, _outline_distance(shape, _flat(cloth[i])))
	check(on_nose.size() > 100, "nose vertices found: %d" % on_nose.size())
	check(worst <= NOSE_TOLERANCE, "every nose vertex within 0.5 mm of the outline; worst %.3f mm" % (worst * 1000.0))
	var segments := PackedVector2Array()
	for t in range(0, indices.size(), 3):
		for e in 3:
			var a := indices[t + e]
			var b := indices[t + (e + 1) % 3]
			if on_nose.has(a) and on_nose.has(b) and a < b:
				segments.append(_flat(cloth[a]))
				segments.append(_flat(cloth[b]))
	var uncovered := 0
	var farthest := 0.0
	for p in _outline_samples(shape):
		var nearest := INF
		for s in range(0, segments.size(), 2):
			nearest = minf(nearest, p.distance_to(Geometry2D.get_closest_point_to_segment(p, segments[s], segments[s + 1])))
		farthest = maxf(farthest, nearest)
		if nearest > NOSE_TOLERANCE:
			uncovered += 1
	check_eq(uncovered, 0, "points of the noses and jaws the drawn nose misses (farthest %.3f mm)" % (farthest * 1000.0))
	table.free()


# The cushion's cross-section where each straight nose starts and ends: nothing in front of the nose,
# the face below it cut back under it towards the cloth, and a slim top back to the wood, flush with
# it at rail height. The rails' outer edges are rail_width behind the noses whatever the cushion
# gives up (test_rails_reach_rail_width_behind_the_noses).
func test_cushions_are_slim_undercut_wedges() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var rail_height := float(shape["rail_height"])
	var contact := minf(Physics.param("cushion_contact_height_m", TableView.CONTACT_HEIGHT_FALLBACK), rail_height - 0.001)
	var cloth: PackedVector3Array = mesh.surface_get_arrays(TableView.SURFACE_CLOTH)[Mesh.ARRAY_VERTEX]
	var wood: PackedVector3Array = mesh.surface_get_arrays(TableView.SURFACE_WOOD)[Mesh.ARRAY_VERTEX]
	var sections := 0
	for line: Dictionary in shape["lines"]:
		if String(line["kind"]) != "nose":
			continue
		var n := _v2(line["normal"])
		for end: Vector2 in [_v2(line["p1"]), _v2(line["p2"])]:
			var name := "%s at %s" % [line["name"], end]
			var nose := 0
			var in_front := 0
			var shallowest_undercut := INF
			var lowest_face := Vector2(0.0, INF)
			var top := Vector2(-INF, 0.0)
			for v in cloth:
				var p := _flat(v) - end
				if absf(p.dot(n.orthogonal())) > 1e-5 or v.y < 1e-4:
					continue
				var behind := -p.dot(n)
				if behind > 0.06 or behind < -0.01:
					continue
				if absf(v.y - contact) < 1e-6:
					nose += 1
				if behind < -1e-4:
					in_front += 1
				if v.y < contact - 0.002:
					shallowest_undercut = minf(shallowest_undercut, behind)
					if v.y < lowest_face.y:
						lowest_face = Vector2(behind, v.y)
				if v.y > contact and behind > top.x:
					top = Vector2(behind, v.y)
			var wood_starts := INF
			for v in wood:
				var p := _flat(v) - end
				if absf(p.dot(n.orthogonal())) < 1e-5 and absf(v.y - rail_height) < 1e-6 and absf(p.dot(n)) < 0.2:
					wood_starts = minf(wood_starts, -p.dot(n))
			sections += 1
			check(nose > 0, "%s: the cushion's nose is drawn at the contact height" % name)
			check_eq(in_front, 0, "%s: cushion vertices in front of the nose" % name)
			check(shallowest_undercut > 0.001, "%s: the face below the nose is cut back under it: %f" % [name, shallowest_undercut])
			check(lowest_face.x > 0.012, "%s: the undercut deepens towards the cloth, %f behind at %f" % [name, lowest_face.x, lowest_face.y])
			check(top.x >= 0.036 and top.x <= 0.044, "%s: the cushion's top is a slim 36-44 mm: %f" % [name, top.x])
			check(absf(top.y - rail_height) < 0.001, "%s: the cushion's back is flush with the rail: %f" % [name, top.y])
			check(absf(wood_starts - top.x) < 1e-4, "%s: the wood starts where the cushion's top ends: %f" % [name, wood_starts])
	check_eq(sections, 12, "cushion cross-sections checked")
	table.free()


# At a middle pocket the cushion fills its jaw up to the wood's square end and runs on down the
# facing a strip wide; it doesn't jut into the rail.
func test_the_middle_cushions_curve_into_their_pockets() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var half := _half()
	var cloth: PackedVector3Array = mesh.surface_get_arrays(TableView.SURFACE_CLOTH)[Mesh.ARRAY_VERTEX]
	for pocket: Dictionary in shape["pockets"]:
		if bool(pocket["corner"]):
			continue
		var centre := _v2(pocket["drop_centre"])
		var gap := _hole_radius(shape, pocket)
		var inner_edge := half.y + TableView.CUSHION_DEPTH
		var in_the_rail := 0
		var outside_the_strip := 0
		for v in cloth:
			var p := _flat(v)
			if v.y < 1e-3 or p.distance_to(centre) > 0.15 or absf(p.y) <= inner_edge + 1e-6:
				continue
			in_the_rail += 1
			if absf(p.x - centre.x) > gap + TableView.JAW_STRIP + 1e-4:
				outside_the_strip += 1
		check(in_the_rail > 20, "%s's cushions run down its facings (%d vertices)" % [pocket["name"], in_the_rail])
		check_eq(outside_the_strip, 0, "%s cushion vertices in the rail past the wood's square end" % pocket["name"])
	table.free()


func test_rails_reach_rail_width_behind_the_noses() -> void:
	var shape := _shape()
	var table := _build(shape)
	var dims := OfsTable.dimensions()
	var bounds := table.bounds()
	var half_length := float(dims["playing_length"]) / 2.0 + float(shape["rail_width"])
	check_near(-bounds.position.z, half_length, 0.001, "baulk end of the rails")
	check_near(bounds.end.z, half_length, 0.001, "top end of the rails")
	check(bounds.end.y > float(shape["rail_height"]) - 1e-4, "rail top at rail height: %f" % bounds.end.y)
	table.free()


func test_a_broken_shape_draws_nothing() -> void:
	var table := _build({"lines": [], "arcs": [], "pockets": [], "rail_height": 0.04, "rail_width": 0.14})
	check(table.get_node_or_null("Mesh") == null, "no mesh")
	check(not table.bounds().has_volume(), "no bounds")
	table.free()
