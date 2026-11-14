class_name TableView
extends Node3D
## The table (spec 6, 13.1). When the bought model is in the project (res://licensed/table.gltf,
## fitted to the playing area by tools/fit_table.py; the physics outline was measured from it) its
## Table, Racks and Chalk are shown under "Model". Without it, as in CI and TestFlight builds,
## ProceduralTable draws the table from the physics outline as one mesh, "Mesh". Which one, and why,
## is logged as table.view.
##
## Either way the baulk line, the D and the spots are drawn on the cloth, each pocket gets a Marker3D
## "Pocket_<name>" at its drop centre on the cloth, with metas "pocket" (its index) and
## "drop_radius", and bounds() is the table's outline from the shape.

## The procedural mesh's surfaces.
const SURFACE_CLOTH := 0
const SURFACE_WOOD := 1
## The nets hanging in the pockets.
const SURFACE_NET := 2
## The net's surface under its old name, from when the holes were lined in leather; test_main.gd
## still checks it by this name.
const SURFACE_LEATHER := SURFACE_NET
const SURFACE_MARKINGS := 3
## The leather fittings round each pocket.
const SURFACE_FITTINGS := 4

## AUTO shows the model when it is there and the run draws something; a headless run never does, so
## every test run sees the same table whether or not the bought files are on the machine.
enum Source { AUTO, PROCEDURAL, MODEL }

const MODEL_PATH := "res://licensed/table.gltf"
const MODEL_PARTS: Array[String] = ["Table", "Racks", "Chalk"]
## Set to "procedural" to keep AUTO off the model, for screenshots of the fallback.
const SOURCE_ENV := "OFS_TABLE"
## The model's net texture carries the gaps between its cords in its alpha. The cords are a texel
## or two wide: cut any higher, they vanish from the smaller mipmaps and only the knots are left.
const NET_MATERIAL := "SnookerNetShader"
const NET_ALPHA_CUT := 0.12
const TABLE_MATERIAL := "SnookerTableShader"
## The model's cloth, wood and leather share one material and one set of textures. The stage's light
## suits them differently: the cloth, a flat pure green, needs the sheen of baize to read as
## tournament green, while the same sheen turns the dark wood grey. This shader draws the model's own
## textures and tells the cloth by its colour: CLOTH_SPECULAR and a rim for its fuzz at grazing
## angles there, TRIM_SPECULAR on the wood and leather.
const CLOTH_SPECULAR := 0.65
const CLOTH_RIM := 0.25
const CLOTH_RIM_TINT := 0.6
const TRIM_SPECULAR := 0.3
const TABLE_SHADER := """
shader_type spatial;
render_mode cull_back;
uniform sampler2D albedo_map : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D normal_map : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
// glTF packing: roughness in green, metallic in blue.
uniform sampler2D rough_metal_map : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float cloth_specular;
uniform float cloth_rim;
uniform float cloth_rim_tint;
uniform float trim_specular;

void fragment() {
	vec3 colour = texture(albedo_map, UV).rgb;
	vec3 rough_metal = texture(rough_metal_map, UV).rgb;
	float cloth = smoothstep(0.03, 0.08, colour.g - max(colour.r, colour.b));
	ALBEDO = colour;
	ROUGHNESS = rough_metal.g;
	METALLIC = rough_metal.b;
	NORMAL_MAP = texture(normal_map, UV).rgb;
	SPECULAR = mix(trim_specular, cloth_specular, cloth);
	RIM = cloth_rim * cloth;
	RIM_TINT = cloth_rim_tint;
}
"""
const MARK_HALF_WIDTH := 0.0015
const MARK_LIFT := 0.0003
const SPOT_RADIUS := 0.004
const CONTACT_HEIGHT_FALLBACK := 0.03675
const SPOT_COLOURS: Array[String] = ["yellow", "green", "brown", "blue", "pink", "black"]

## Which table build() shows.
var source := Source.AUTO
var model_path := MODEL_PATH

var _bounds := AABB()
var _shown := ""
var _reason := ""


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
	_bounds = AABB()
	_shown = ""
	_reason = ""
	var problems := PackedStringArray()
	for key: String in ["lines", "arcs", "pockets", "rail_height", "rail_width"]:
		if not shape.has(key):
			problems.append("shape has no '%s'" % key)
	for key: String in ["playing_length", "playing_width", "baulk_line_x", "d_radius"]:
		if not dims.has(key):
			problems.append("dims has no '%s'" % key)
	if not problems.is_empty():
		_fail(problems)
		return
	var marks := ProceduralTable.Surf.new()
	_add_markings(marks, dims)

	var detail := ""
	_reason = _why_not_the_model()
	if _reason.is_empty():
		detail = _show_model()
		if detail.is_empty():
			_shown = "model"
			_reason = "found"
			var mesh := ArrayMesh.new()
			marks.commit(mesh, ProceduralTable.marking_material())
			var markings := MeshInstance3D.new()
			markings.name = "Markings"
			markings.mesh = mesh
			markings.layers = Stage.TABLE_LAYER
			markings.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			add_child(markings)
	if _shown.is_empty():
		var rail_height := float(shape["rail_height"])
		var contact := minf(Physics.param("cushion_contact_height_m", CONTACT_HEIGHT_FALLBACK), rail_height - 0.001)
		var ball_radius := float(dims.get("ball_radius", ProceduralTable.BALL_RADIUS_FALLBACK))
		var mesh := ProceduralTable.new().build(shape, ball_radius, contact, marks, problems)
		if mesh == null:
			if not detail.is_empty():
				problems.append(detail)
			_fail(problems)
			return
		_shown = "procedural"
		var instance := MeshInstance3D.new()
		instance.name = "Mesh"
		instance.mesh = mesh
		instance.layers = Stage.TABLE_LAYER
		add_child(instance)

	var reach := Vector2(float(dims["playing_length"]), float(dims["playing_width"])) / 2.0 \
			+ Vector2.ONE * float(shape["rail_width"])
	_bounds = AABB(Vector3(-reach.y, Stage.FLOOR_Y, -reach.x),
			Vector3(2.0 * reach.y, float(shape["rail_height"]) - Stage.FLOOR_Y, 2.0 * reach.x))
	var index := 0
	for p: Variant in shape["pockets"]:
		var centre := ProceduralTable.v2(p["drop_centre"])
		var marker := Marker3D.new()
		marker.name = "Pocket_" + String(p["name"])
		marker.position = ProceduralTable.g(centre, 0.0)
		marker.set_meta("pocket", index)
		marker.set_meta("drop_radius", float(p["drop_radius"]))
		add_child(marker)
		index += 1

	var data := {"table": _shown, "reason": _reason}
	if detail.is_empty():
		Log.info("table", "table.view", data)
	else:
		# The model is in the project and didn't come up: someone should hear of it.
		data["detail"] = detail
		Log.warn("table", "table.view", data)


## The table's outline in this node's space, from the rails' outer edges down to the floor it
## stands on (empty before a build): the same box whichever table is shown.
func bounds() -> AABB:
	return _bounds


## "model" or "procedural" (empty when nothing is drawn), and why: "found" for the model; for the
## procedural table "no_model", "forced", "headless", or "load_failed", "part_missing" and
## "texture_missing" when the model is in the project and didn't come up.
func shown() -> String:
	return _shown


func reason() -> String:
	return _reason


func _fail(problems: PackedStringArray) -> void:
	_bounds = AABB()
	_shown = ""
	Log.error("app", "app.error", {"kind": "message",
			"message": "table_view: the table isn't drawn: " + "; ".join(problems)})


## Empty when build() should try the model.
func _why_not_the_model() -> String:
	if source == Source.PROCEDURAL:
		return "forced"
	if source == Source.AUTO:
		if OS.get_environment(SOURCE_ENV) == "procedural":
			return "forced"
		if DisplayServer.get_name() == "headless":
			return "headless"
	if not ResourceLoader.exists(model_path):
		return "no_model"
	return ""


## Adds the model under "Model". On failure nothing is added, _reason says why in a word and the
## returned text names what was wrong.
func _show_model() -> String:
	var scene := load(model_path) as PackedScene
	var model: Node = scene.instantiate() if scene != null else null
	if model == null:
		_reason = "load_failed"
		return "%s isn't a scene Godot can load; import the project again" % model_path
	var missing := PackedStringArray()
	var untextured := PackedStringArray()
	for part in MODEL_PARTS:
		var instance := model.get_node_or_null(part) as MeshInstance3D
		if instance == null or instance.mesh == null:
			missing.append(part)
			continue
		# A texture that failed to import leaves its material bare, and the table white.
		for surface in instance.mesh.get_surface_count():
			var material := instance.mesh.surface_get_material(surface) as BaseMaterial3D
			if material == null or material.albedo_texture == null:
				untextured.append("%s surface %d" % [part, surface])
	if not missing.is_empty():
		model.free()
		_reason = "part_missing"
		return "%s has no mesh named %s; run tools/fit_table.py again" % [model_path, ", ".join(missing)]
	if not untextured.is_empty():
		model.free()
		_reason = "texture_missing"
		return "%s came without its colour texture on %s; import the project again" % [model_path, ", ".join(untextured)]
	model.name = "Model"
	for part in MODEL_PARTS:
		var instance := model.get_node(part) as MeshInstance3D
		instance.layers = Stage.TABLE_LAYER
		for surface in instance.mesh.get_surface_count():
			var material := instance.mesh.surface_get_material(surface) as BaseMaterial3D
			if material.resource_name == TABLE_MATERIAL and material.normal_texture != null and material.roughness_texture != null:
				instance.set_surface_override_material(surface, _table_material(material))
				continue
			# The imported materials are shared with every other load of the scene: tune copies.
			material = material.duplicate()
			material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
			# The model's materials come in double-sided, and a double-sided surface is black in the
			# reflection probe's capture: the balls then reflect black where the cloth should be.
			material.cull_mode = BaseMaterial3D.CULL_BACK
			if material.resource_name == NET_MATERIAL:
				# A net is seen from both sides, and nothing reflects it.
				material.cull_mode = BaseMaterial3D.CULL_DISABLED
				material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
				material.alpha_scissor_threshold = NET_ALPHA_CUT
			instance.set_surface_override_material(surface, material)
	# Under the table, in its shadow already.
	var racks := model.get_node("Racks") as MeshInstance3D
	racks.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(model)
	return ""


## TABLE_SHADER drawing the imported table material's textures.
static func _table_material(imported: BaseMaterial3D) -> ShaderMaterial:
	var shader := Shader.new()
	shader.code = TABLE_SHADER
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("albedo_map", imported.albedo_texture)
	material.set_shader_parameter("normal_map", imported.normal_texture)
	material.set_shader_parameter("rough_metal_map", imported.roughness_texture)
	material.set_shader_parameter("cloth_specular", CLOTH_SPECULAR)
	material.set_shader_parameter("cloth_rim", CLOTH_RIM)
	material.set_shader_parameter("cloth_rim_tint", CLOTH_RIM_TINT)
	material.set_shader_parameter("trim_specular", TRIM_SPECULAR)
	return material


func _add_markings(marks: ProceduralTable.Surf, dims: Dictionary) -> void:
	var baulk_x := float(dims["baulk_line_x"])
	var half_width := float(dims["playing_width"]) / 2.0
	var d_radius := float(dims["d_radius"])
	var first := marks.vertex(ProceduralTable.g(Vector2(baulk_x - MARK_HALF_WIDTH, -half_width), MARK_LIFT), Vector3.UP, Vector2.ZERO)
	marks.vertex(ProceduralTable.g(Vector2(baulk_x + MARK_HALF_WIDTH, -half_width), MARK_LIFT), Vector3.UP, Vector2.ZERO)
	marks.vertex(ProceduralTable.g(Vector2(baulk_x + MARK_HALF_WIDTH, half_width), MARK_LIFT), Vector3.UP, Vector2.ZERO)
	marks.vertex(ProceduralTable.g(Vector2(baulk_x - MARK_HALF_WIDTH, half_width), MARK_LIFT), Vector3.UP, Vector2.ZERO)
	marks.tri(first, first + 1, first + 2)
	marks.tri(first, first + 2, first + 3)
	# The D: a semicircle on the baulk side, centred on the middle of the baulk line.
	var segments := 96
	first = marks.verts.size()
	for i in segments + 1:
		var d := ProceduralTable.dir(PI / 2.0 + PI * i / segments)
		marks.vertex(ProceduralTable.g(Vector2(baulk_x, 0.0) + d * (d_radius - MARK_HALF_WIDTH), MARK_LIFT), Vector3.UP, Vector2.ZERO)
		marks.vertex(ProceduralTable.g(Vector2(baulk_x, 0.0) + d * (d_radius + MARK_HALF_WIDTH), MARK_LIFT), Vector3.UP, Vector2.ZERO)
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
		var middle := marks.vertex(ProceduralTable.g(centre, MARK_LIFT), Vector3.UP, Vector2.ZERO)
		for i in 17:
			marks.vertex(ProceduralTable.g(centre + ProceduralTable.dir(TAU * i / 16.0) * SPOT_RADIUS, MARK_LIFT), Vector3.UP, Vector2.ZERO)
		for i in 16:
			marks.tri(middle, middle + 1 + i, middle + 2 + i)
