extends TestCase
## TableView: the procedural table drawn from the physics outline, the bought model when its files
## are in the project (those tests pass without checking anything when they aren't, as in CI), and
## the choice between them.

const NOSE_TOLERANCE := 0.0005
# How finely the outline is walked when checking the drawn nose covers it: the jaws closely, the
# straight noses, drawn as one segment each, more coarsely.
const OUTLINE_STEP := 0.002
const NOSE_STEP := 0.05
# The grid a ray is dropped through the table on: fine round each pocket, coarse elsewhere.
const POCKET_GRID := 0.005
const POCKET_REACH := 0.15
const TABLE_GRID := 0.03
const HASH_CELL := 0.02

var _log_start := 0


# The binding's outline once it's configured, else the same Dictionary built from the data file.
func _shape() -> Dictionary:
	var shape := Physics.table_shape()
	if shape.is_empty():
		shape = TableView.shape_from_json(FileAccess.get_file_as_string(Physics.TABLE_SHAPE_PATH))
	check(not shape.is_empty(), "a table shape to build from")
	return shape


func _build(shape: Dictionary, source := TableView.Source.PROCEDURAL, model_path := TableView.MODEL_PATH) -> TableView:
	var table := TableView.new()
	table.source = source
	table.model_path = model_path
	tree.root.add_child(table)
	table.build(shape, OfsTable.dimensions())
	return table


func _mesh(table: TableView) -> ArrayMesh:
	var instance := table.get_node_or_null("Mesh") as MeshInstance3D
	check(instance != null, "the table has its merged mesh")
	return instance.mesh as ArrayMesh if instance != null else null


# False, with a note in the test output, on a machine without the bought files.
func _has_model() -> bool:
	if ResourceLoader.exists(TableView.MODEL_PATH):
		return true
	print("      skipped: %s isn't in this project" % TableView.MODEL_PATH)
	return false


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


## The lines of `event` logged since _mark_log, read from the session file.
func _logged_lines(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append(parsed)
	return found


static func _v2(value: Variant) -> Vector2:
	return Vector2(float(value[0]), float(value[1]))


static func _half() -> Vector2:
	var dims := OfsTable.dimensions()
	return Vector2(float(dims["playing_length"]), float(dims["playing_width"])) / 2.0


static func _contact(shape: Dictionary) -> float:
	return minf(Physics.param("cushion_contact_height_m", TableView.CONTACT_HEIGHT_FALLBACK), float(shape["rail_height"]) - 0.001)


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


static func _flat(v: Vector3) -> Vector2:
	return TableSpace.godot_to_core(v)


# A surface's triangles, flat: three core points each.
static func _triangles(mesh: ArrayMesh, surface: int, only_at_height := NAN) -> PackedVector2Array:
	var arrays := mesh.surface_get_arrays(surface)
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	var out := PackedVector2Array()
	for i in range(0, indices.size(), 3):
		var a := verts[indices[i]]
		var b := verts[indices[i + 1]]
		var c := verts[indices[i + 2]]
		if not is_nan(only_at_height) and (absf(a.y - only_at_height) > 1e-6 or absf(b.y - only_at_height) > 1e-6
				or absf(c.y - only_at_height) > 1e-6):
			continue
		out.append(_flat(a))
		out.append(_flat(b))
		out.append(_flat(c))
	return out


static func _covered(triangles: PackedVector2Array, p: Vector2) -> bool:
	for i in range(0, triangles.size(), 3):
		if Geometry2D.point_is_inside_triangle(p, triangles[i], triangles[i + 1], triangles[i + 2]):
			return true
	return false


func test_shape_from_json_matches_the_binding_layout() -> void:
	var text := FileAccess.get_file_as_string(Physics.TABLE_SHAPE_PATH)
	var shape := TableView.shape_from_json(text)
	var file: Dictionary = JSON.parse_string(text)
	check_eq((shape["pockets"] as Array).size(), 6, "pockets")
	check_eq((shape["arcs"] as Array).size(), 12, "arcs")
	check_eq((shape["lines"] as Array).size(), 18, "lines")
	check_near(float(shape["rail_width"]), float(file["rail_width_m"]), 1e-12, "rail width")
	check_near(float(shape["rail_height"]), float(file["rail_height_m"]), 1e-12, "rail height")
	for arc: Dictionary in shape["arcs"]:
		var pocket := int(arc["pocket"])
		var name := String(arc["name"])
		check(name.begins_with(String(shape["pockets"][pocket]["name"])), "arc %s maps to pocket %d" % [name, pocket])
	check(TableView.shape_from_json("not json").is_empty(), "garbage gives an empty shape")


func _check_markers(table: TableView, shape: Dictionary) -> void:
	var markers := 0
	for child in table.get_children():
		if child.name.begins_with("Pocket_"):
			markers += 1
	check_eq(markers, (shape["pockets"] as Array).size(), "pocket markers")
	for i in (shape["pockets"] as Array).size():
		var pocket: Dictionary = shape["pockets"][i]
		var marker := table.get_node_or_null("Pocket_" + String(pocket["name"])) as Node3D
		check(marker != null, "marker for %s" % pocket["name"])
		if marker == null:
			continue
		var at := TableSpace.godot_to_core(marker.position)
		var want := _v2(pocket["drop_centre"])
		check(at.distance_to(want) < 1e-5 and absf(marker.position.y) < 1e-9,
				"%s marker at %s, drop centre %s on the cloth" % [pocket["name"], marker.position, want])
		check_eq(int(marker.get_meta("pocket", -1)), i, "%s index meta" % pocket["name"])
		check_near(float(marker.get_meta("drop_radius", 0.0)), float(pocket["drop_radius"]), 1e-9, "%s drop radius meta" % pocket["name"])


func _check_bounds(table: TableView, shape: Dictionary) -> void:
	var reach := _half() + Vector2.ONE * float(shape["rail_width"])
	var bounds := table.bounds()
	check_near(bounds.end.x, reach.y, 1e-6, "the left rail's outer edge")
	check_near(bounds.position.x, -reach.y, 1e-6, "the right rail's outer edge")
	check_near(bounds.end.z, reach.x, 1e-6, "the baulk rail's outer edge")
	check_near(bounds.position.z, -reach.x, 1e-6, "the top rail's outer edge")
	check_near(bounds.end.y, float(shape["rail_height"]), 1e-6, "the rails' top")
	check_near(bounds.position.y, Stage.FLOOR_Y, 1e-6, "the floor the table stands on")


func test_one_pocket_marker_at_each_drop_centre() -> void:
	var shape := _shape()
	var table := _build(shape)
	_check_markers(table, shape)
	table.free()


func test_bounds_are_the_rails_outer_edges_down_to_the_floor() -> void:
	var shape := _shape()
	var table := _build(shape)
	_check_bounds(table, shape)
	var mesh := _mesh(table)
	if mesh != null:
		var drawn := mesh.get_aabb()
		var bounds := table.bounds()
		check(drawn.position.is_equal_approx(bounds.position) and drawn.size.x <= bounds.size.x + 1e-5
				and drawn.size.z <= bounds.size.z + 1e-5 and absf(drawn.size.x - bounds.size.x) < 1e-4
				and absf(drawn.size.z - bounds.size.z) < 1e-4,
				"the procedural table fills its outline: drawn %s, bounds %s" % [drawn, bounds])
	table.free()


func test_the_procedural_table_is_one_mesh_of_five_surfaces() -> void:
	var table := _build(_shape())
	check_eq(table.shown(), "procedural", "what is shown")
	var mesh := _mesh(table)
	var drawn := 0
	for child in table.get_children():
		if child is GeometryInstance3D:
			drawn += 1
			check_eq((child as VisualInstance3D).layers, Stage.TABLE_LAYER, "on the table's layer")
	check_eq(drawn, 1, "one mesh instance")
	check(table.get_node_or_null("Model") == null, "no model")
	if mesh != null:
		check_eq(mesh.get_surface_count(), TableView.SURFACE_FITTINGS + 1, "cloth, wood, nets, markings and fittings")
		for surface in mesh.get_surface_count():
			check((mesh.surface_get_arrays(surface)[Mesh.ARRAY_INDEX] as PackedInt32Array).size() > 0, "surface %d has triangles" % surface)
	table.free()


# The drawn nose is a polyline at the contact height: each of its vertices lies on the outline, and
# every point of every nose line and jaw arc lies on one of its segments. Checking the outline's
# points against the nearest vertex instead would fail on any arc whose middle falls between two
# vertices.
func test_cushion_noses_follow_the_outline() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var contact := _contact(shape)
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
# it at rail height.
func test_cushions_are_slim_undercut_wedges() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var rail_height := float(shape["rail_height"])
	var contact := _contact(shape)
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
			check_near(top.x, ProceduralTable.CUSHION_DEPTH, 1e-5, "%s: the cushion's top is CUSHION_DEPTH wide" % name)
			check(absf(top.y - rail_height) < 0.001, "%s: the cushion's back is flush with the rail: %f" % [name, top.y])
			check(absf(wood_starts - top.x) < 1e-4, "%s: the wood starts where the cushion's top ends: %f" % [name, wood_starts])
	check_eq(sections, 12, "cushion cross-sections checked")
	table.free()


# The hole in the bed is the physics' drop circle, no smaller and no larger.
func test_the_bed_is_cut_out_at_each_drop_circle() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var bed := _triangles(mesh, TableView.SURFACE_CLOTH, 0.0)
	check(bed.size() > 0, "the bed is drawn at the cloth's height")
	for pocket: Dictionary in shape["pockets"]:
		var centre := _v2(pocket["drop_centre"])
		var radius := float(pocket["drop_radius"])
		var open := 0
		var closed := 0
		for i in 48:
			var d := Vector2.from_angle(TAU * i / 48.0)
			if _covered(bed, centre + d * (radius - 0.0015)):
				closed += 1
			if not _covered(bed, centre + d * (radius + 0.0015)):
				open += 1
		check_eq(closed, 0, "%s: bed inside its drop circle" % pocket["name"])
		check_eq(open, 0, "%s: bed missing just outside its drop circle" % pocket["name"])
		check(not _covered(bed, centre), "%s: the hole is open at its centre" % pocket["name"])
	table.free()


# Seen from straight above, something is drawn everywhere inside the rails' outer edge: no gap
# between the bed, the cushions, the fittings, the rails and the nets in the holes.
func test_nothing_shows_through_the_table_from_above() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var reach := _half() + Vector2.ONE * (float(shape["rail_width"]) - 0.001)
	var cells := {}
	var triangles := PackedVector2Array()
	for surface: int in [TableView.SURFACE_CLOTH, TableView.SURFACE_WOOD, TableView.SURFACE_NET, TableView.SURFACE_FITTINGS]:
		triangles.append_array(_triangles(mesh, surface))
	for i in range(0, triangles.size(), 3):
		var lo := triangles[i].min(triangles[i + 1]).min(triangles[i + 2])
		var hi := triangles[i].max(triangles[i + 1]).max(triangles[i + 2])
		for cx in range(floori(lo.x / HASH_CELL), floori(hi.x / HASH_CELL) + 1):
			for cy in range(floori(lo.y / HASH_CELL), floori(hi.y / HASH_CELL) + 1):
				var key := Vector2i(cx, cy)
				if not cells.has(key):
					cells[key] = []
				(cells[key] as Array).append(i)
	# Every point a little off its grid, clear of the lines the mesh's own edges run along.
	var off := Vector2(0.37, 0.41)
	var samples := PackedVector2Array()
	for ix in range(floori(-reach.x / TABLE_GRID), ceili(reach.x / TABLE_GRID)):
		for iy in range(floori(-reach.y / TABLE_GRID), ceili(reach.y / TABLE_GRID)):
			samples.append((Vector2(ix, iy) + off) * TABLE_GRID)
	var steps := roundi(POCKET_REACH / POCKET_GRID)
	for pocket: Dictionary in shape["pockets"]:
		var centre := _v2(pocket["drop_centre"])
		for ix in range(-steps, steps + 1):
			for iy in range(-steps, steps + 1):
				samples.append(centre + (Vector2(ix, iy) + off) * POCKET_GRID)
	var holes := PackedVector2Array()
	var checked := 0
	for p in samples:
		if absf(p.x) > reach.x or absf(p.y) > reach.y:
			continue
		# The rails' outer corners are rounded.
		if absf(p.x) > reach.x - ProceduralTable.CORNER_RADIUS and absf(p.y) > reach.y - ProceduralTable.CORNER_RADIUS:
			continue
		checked += 1
		var hit := false
		var here: Array = cells.get(Vector2i(floori(p.x / HASH_CELL), floori(p.y / HASH_CELL)), [])
		for i: int in here:
			if Geometry2D.point_is_inside_triangle(p, triangles[i], triangles[i + 1], triangles[i + 2]):
				hit = true
				break
		if not hit:
			holes.append(p)
	check(checked > 20000, "points checked: %d" % checked)
	check_eq(holes.size(), 0, "points over nothing, the first at %s" % (str(holes[0]) if not holes.is_empty() else "none"))
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
	var ball := float(OfsTable.dimensions()["ball_radius"])
	var rim := -ProceduralTable.FALL_DEPTH - 1e-4
	# The crochet itself is light: brighter than the cloth, nothing like a black lining.
	var material := mesh.surface_get_material(TableView.SURFACE_NET) as StandardMaterial3D
	var image: Image = material.albedo_texture.get_image() if material != null and material.albedo_texture != null else null
	check(image != null, "the net has its crochet texture")
	if image != null:
		var total := 0.0
		for y in image.get_height():
			for x in image.get_width():
				total += image.get_pixel(x, y).get_luminance()
		var mean := total / (image.get_width() * image.get_height())
		check(mean > 0.5 and mean > ProceduralTable.CLOTH_GREEN.get_luminance(), "the crochet reads light: %f" % mean)
	for pocket: Dictionary in shape["pockets"]:
		var name := String(pocket["name"])
		var centre := _v2(pocket["drop_centre"])
		var radius := float(pocket["drop_radius"])
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
			if flat.distance_to(centre) > radius * 1.5:
				continue
			count += 1
			deepest = minf(deepest, v.y)
			closest = minf(closest, v.distance_to(potted))
			if v.y >= -ProceduralTable.FALL_DEPTH - 1e-6:
				rim_shade = maxf(rim_shade, shades[i].v)
				check_near(flat.distance_to(centre), radius, 1e-5, "%s: the net hangs from the hole's edge" % name)
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
		check(widest > radius and bottom_ring < 0.5 * widest,
				"%s's net sags out under the bed (%f) and closes to under half that (%f)" % [name, widest, bottom_ring])
	table.free()


# Each pocket's leather stands on the physics' facings: its wall starts at each cushion's tip, runs
# down the facing line, and nowhere comes in front of a facing, where the balls go. It keeps inside
# the rails' outer edge, behind the nose lines, and sits flush with the rails.
func test_fittings_stand_on_the_physics_facings() -> void:
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
	check_near(highest, rail_height, 1e-6, "the fittings are flush with the rails")
	for pocket: Dictionary in shape["pockets"]:
		var name := String(pocket["name"])
		var centre := _v2(pocket["drop_centre"])
		var radius := float(pocket["drop_radius"])
		# The wall's line: the fitting's vertices where its lip turns down into the pocket.
		var wall := PackedVector2Array()
		for v in fittings:
			if absf(v.y - (rail_height - ProceduralTable.LIP)) < 1e-6 and _flat(v).distance_to(centre) < 0.2:
				wall.append(_flat(v))
		check(wall.size() >= 20, "%s has its fitting's wall (%d vertices along it)" % [name, wall.size()])
		# Each facing from its cushion's tip: the tip, its direction out of the table, its normal
		# into the pocket.
		var facings: Array[PackedVector2Array] = []
		var axis := Vector2.ZERO
		for line: Dictionary in shape["lines"]:
			if String(line["kind"]) == "facing" and String(line["name"]).begins_with(name):
				var tip := _v2(line["p1"])
				var along := (_v2(line["p2"]) - tip).normalized()
				facings.append(PackedVector2Array([tip, along, _v2(line["normal"])]))
				axis += along
		check_eq(facings.size(), 2, "%s's facings" % name)
		axis = axis.normalized()
		for facing in facings:
			var at_tip := false
			var down_the_facing := 0
			var behind := 0
			var in_the_mouth := 0
			for p in wall:
				at_tip = at_tip or p.distance_to(facing[0]) < 1e-5
				if absf((p - facing[0]).dot(facing[2])) < 1e-5 and (p - facing[0]).dot(facing[1]) > 0.005:
					down_the_facing += 1
				if (p - facing[0]).dot(facing[2]) < -1e-5:
					behind += 1
				if (p - facing[0]).dot(axis) < -1e-5:
					in_the_mouth += 1
			check(at_tip, "%s: the wall starts at the cushion's tip %s" % [name, facing[0]])
			check(down_the_facing > 0, "%s: the wall runs down the facing from %s" % [name, facing[0]])
			check_eq(behind, 0, "%s: wall vertices behind the facing from %s" % [name, facing[0]])
			check_eq(in_the_mouth, 0, "%s: wall vertices in the mouth, before the tip %s" % [name, facing[0]])
		var furthest_back := -INF
		for p in wall:
			furthest_back = maxf(furthest_back, (p - centre).dot(axis))
		check_near(furthest_back, radius, 1e-4, "%s: the wall's back touches the far edge of the hole" % name)
	table.free()


func test_a_broken_shape_draws_nothing() -> void:
	var table := _build({"lines": [], "arcs": [], "pockets": [], "rail_height": 0.04, "rail_width": 0.14})
	check(table.get_node_or_null("Mesh") == null, "no mesh")
	check(not table.bounds().has_volume(), "no bounds")
	check_eq(table.shown(), "", "nothing shown")
	table.free()


func test_a_headless_run_draws_the_procedural_table_and_says_why() -> void:
	var shape := _shape()
	_mark_log()
	var table := _build(shape, TableView.Source.AUTO)
	check_eq(table.shown(), "procedural", "what is shown")
	check_eq(table.reason(), "headless", "why")
	check(table.get_node_or_null("Mesh") != null and table.get_node_or_null("Model") == null, "the procedural mesh alone")
	var lines := _logged_lines("table.view")
	check_eq(lines.size(), 1, "table.view logged once a build")
	if lines.size() == 1:
		check_eq(lines[0]["level"], "info", "level")
		check_eq(lines[0]["data"], {"table": "procedural", "reason": "headless"}, "data")
	table.free()


func test_forcing_the_procedural_table() -> void:
	var shape := _shape()
	_mark_log()
	var table := _build(shape, TableView.Source.PROCEDURAL)
	check_eq(table.shown(), "procedural", "what is shown")
	check_eq(table.reason(), "forced", "why")
	var lines := _logged_lines("table.view")
	check_eq(lines.size(), 1, "table.view logged once")
	if lines.size() == 1:
		check_eq(lines[0]["data"], {"table": "procedural", "reason": "forced"}, "data")
	table.free()


func test_without_the_model_file_the_procedural_table_is_drawn() -> void:
	var shape := _shape()
	_mark_log()
	var table := _build(shape, TableView.Source.MODEL, "res://licensed/no_such_table.gltf")
	check_eq(table.shown(), "procedural", "what is shown")
	check_eq(table.reason(), "no_model", "why")
	check(table.get_node_or_null("Mesh") != null, "the procedural mesh")
	_check_markers(table, shape)
	var lines := _logged_lines("table.view")
	check_eq(lines.size(), 1, "table.view logged once")
	if lines.size() == 1:
		check_eq(lines[0]["level"], "info", "a missing model is the ordinary case in CI")
	check_eq(_logged_lines("app.error").size(), 0, "no error or warning")
	table.free()


func test_the_model_is_shown_when_it_is_in_the_project() -> void:
	if not _has_model():
		return
	var shape := _shape()
	_mark_log()
	var table := _build(shape, TableView.Source.MODEL)
	check_eq(table.shown(), "model", "what is shown")
	check_eq(table.reason(), "found", "why")
	check(table.get_node_or_null("Mesh") == null, "no procedural mesh")
	var rail_height := float(shape["rail_height"])
	for part in TableView.MODEL_PARTS:
		var instance := table.get_node_or_null("Model/" + part) as MeshInstance3D
		check(instance != null, "the model's %s" % part)
		if instance != null:
			check(instance.transform.is_equal_approx(Transform3D.IDENTITY), "%s sits in the game's frame as fitted" % part)
	var body := table.get_node_or_null("Model/Table") as MeshInstance3D
	var racks := table.get_node_or_null("Model/Racks") as MeshInstance3D
	if body != null and racks != null:
		check_eq(body.layers, Stage.TABLE_LAYER, "the table on the layer the probe captures")
		check_eq(racks.layers, Stage.TABLE_LAYER, "the racks with it")
		check_eq(racks.cast_shadow, GeometryInstance3D.SHADOW_CASTING_SETTING_OFF, "the racks, under the table, cast no shadow")
		# The fit puts the cloth at 0 and the rails where the outline has them.
		var drawn := body.get_aabb()
		var bounds := table.bounds()
		check_near(drawn.end.y, rail_height, 0.001, "the model's rail top")
		check_near(drawn.position.y, Stage.FLOOR_Y, 0.001, "the model's feet on the floor")
		check_near(drawn.end.z, bounds.end.z, 0.002, "the model's baulk rail against the outline")
		check(absf(drawn.end.x - bounds.end.x) < 0.01, "the model's side rail against the outline (its middle leathers stand a little proud): %f, %f" % [drawn.end.x, bounds.end.x])
		var shaded := 0
		var nets := 0
		for surface in body.mesh.get_surface_count():
			var material := body.get_surface_override_material(surface)
			if material is ShaderMaterial:
				shaded += 1
				for map: String in ["albedo_map", "normal_map", "rough_metal_map"]:
					check((material as ShaderMaterial).get_shader_parameter(map) is Texture2D, "the table's %s" % map)
			elif material is BaseMaterial3D and material.resource_name == TableView.NET_MATERIAL:
				nets += 1
				check_eq((material as BaseMaterial3D).transparency, BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR, "the nets cut by their alpha")
				check_eq((material as BaseMaterial3D).cull_mode, BaseMaterial3D.CULL_DISABLED, "the nets seen from both sides")
		check_eq(shaded, 1, "the cloth, wood and leather surface drawn by the table shader")
		check_eq(nets, 1, "the nets' surface")
	# Our own markings, just above the model's bed.
	var markings := table.get_node_or_null("Markings") as MeshInstance3D
	check(markings != null, "the markings over the model's bed")
	if markings != null:
		var box := markings.get_aabb()
		check_near(box.position.y, TableView.MARK_LIFT, 1e-6, "the markings' height above the cloth")
		check_near(box.size.y, 0.0, 1e-4, "the markings are flat")
		check_near(box.size.x, float(OfsTable.dimensions()["playing_width"]), 1e-4, "the baulk line from cushion to cushion")
		check_eq(markings.cast_shadow, GeometryInstance3D.SHADOW_CASTING_SETTING_OFF, "the markings cast no shadow")
	_check_markers(table, shape)
	_check_bounds(table, shape)
	var lines := _logged_lines("table.view")
	check_eq(lines.size(), 1, "table.view logged once")
	if lines.size() == 1:
		check_eq(lines[0]["level"], "info", "level")
		check_eq(lines[0]["data"], {"table": "model", "reason": "found"}, "data")
	table.free()


func test_both_tables_give_the_game_the_same_outline() -> void:
	if not _has_model():
		return
	var shape := _shape()
	var model := _build(shape, TableView.Source.MODEL)
	var procedural := _build(shape, TableView.Source.PROCEDURAL)
	check(model.bounds().is_equal_approx(procedural.bounds()), "bounds: %s, %s" % [model.bounds(), procedural.bounds()])
	for pocket: Dictionary in shape["pockets"]:
		var a := model.get_node_or_null("Pocket_" + String(pocket["name"])) as Node3D
		var b := procedural.get_node_or_null("Pocket_" + String(pocket["name"])) as Node3D
		check(a != null and b != null and a.position.is_equal_approx(b.position), "%s marker" % pocket["name"])
	model.free()
	procedural.free()


# A model file without the table's parts (here the lamp's) is logged and the procedural table drawn.
func test_a_model_without_its_parts_falls_back_and_is_logged() -> void:
	if not _has_model() or not ResourceLoader.exists(Stage.LAMP_PATH):
		return
	var shape := _shape()
	_mark_log()
	var table := _build(shape, TableView.Source.MODEL, Stage.LAMP_PATH)
	check_eq(table.shown(), "procedural", "what is shown")
	check_eq(table.reason(), "part_missing", "why")
	check(table.get_node_or_null("Mesh") != null and table.get_node_or_null("Model") == null, "the procedural mesh alone")
	_check_markers(table, shape)
	var lines := _logged_lines("table.view")
	check_eq(lines.size(), 1, "table.view logged once")
	if lines.size() == 1:
		check_eq(lines[0]["level"], "warn", "level")
		var data: Dictionary = lines[0]["data"]
		check_eq(data.get("table"), "procedural", "table")
		check_eq(data.get("reason"), "part_missing", "reason")
		check(String(data.get("detail", "")).contains("Table") and String(data.get("detail", "")).contains(Stage.LAMP_PATH),
				"the detail names the file and the missing mesh: %s" % data.get("detail"))
	table.free()
