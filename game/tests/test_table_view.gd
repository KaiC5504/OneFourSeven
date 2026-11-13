extends TestCase

const NOSE_TOLERANCE := 0.0005


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


func test_each_pocket_is_a_leather_hole_round_its_drop_centre() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	check_eq(mesh.get_surface_count(), 4, "cloth, wood, leather and markings surfaces")
	var leather: PackedVector3Array = mesh.surface_get_arrays(TableView.SURFACE_LEATHER)[Mesh.ARRAY_VERTEX]
	var cloth: PackedVector3Array = mesh.surface_get_arrays(TableView.SURFACE_CLOTH)[Mesh.ARRAY_VERTEX]
	for pocket: Dictionary in shape["pockets"]:
		var centre := _v2(pocket["drop_centre"])
		var reach := float(pocket["drop_radius"]) + 0.003
		var below := 0
		var deepest := 0.0
		for v in leather:
			if TableSpace.godot_to_core(v).distance_to(centre) <= reach and v.y < -0.01:
				below += 1
				deepest = minf(deepest, v.y)
		check(below > 40, "%s has a leather drop below the cloth (%d vertices)" % [pocket["name"], below])
		check(deepest < -0.05, "%s is deep enough to swallow a ball: %f" % [pocket["name"], deepest])
		# No cloth at the cloth's level inside the hole.
		var inside := 0
		for v in cloth:
			if absf(v.y) < 1e-6 and TableSpace.godot_to_core(v).distance_to(centre) < float(pocket["drop_radius"]) - 0.002:
				inside += 1
		check_eq(inside, 0, "%s cloth vertices inside the hole" % pocket["name"])
	table.free()


func test_cushion_noses_follow_the_outline() -> void:
	var shape := _shape()
	var table := _build(shape)
	var mesh := _mesh(table)
	if mesh == null:
		table.free()
		return
	var contact := Physics.param("cushion_contact_height_m", TableView.CONTACT_HEIGHT_FALLBACK)
	var noses := PackedVector2Array()
	var cloth: PackedVector3Array = mesh.surface_get_arrays(TableView.SURFACE_CLOTH)[Mesh.ARRAY_VERTEX]
	for v in cloth:
		if absf(v.y - contact) < 1e-6:
			noses.append(TableSpace.godot_to_core(v))
	check(noses.size() > 100, "nose vertices found: %d" % noses.size())
	var worst := 0.0
	for p in noses:
		worst = maxf(worst, _outline_distance(shape, p))
	check(worst <= NOSE_TOLERANCE, "every nose vertex within 0.5 mm of the outline; worst %.3f mm" % (worst * 1000.0))
	# Every nose line and jaw arc is covered end to end.
	var ends: Array[Vector2] = []
	for line: Dictionary in shape["lines"]:
		if String(line["kind"]) == "nose":
			ends.append(_v2(line["p1"]))
			ends.append(_v2(line["p2"]))
	for arc: Dictionary in shape["arcs"]:
		var c := _v2(arc["centre"])
		var r := float(arc["radius"])
		for key: String in ["angle_start", "angle_end"]:
			ends.append(c + Vector2.from_angle(float(arc[key])) * r)
		var middle := (float(arc["angle_start"]) + float(arc["angle_end"])) / 2.0
		ends.append(c + Vector2.from_angle(middle) * r)
	for e in ends:
		var nearest := INF
		for p in noses:
			nearest = minf(nearest, p.distance_to(e))
		check(nearest <= NOSE_TOLERANCE, "a nose vertex at %s (nearest %.3f mm)" % [e, nearest * 1000.0])
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
