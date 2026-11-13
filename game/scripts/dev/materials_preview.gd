extends Node3D
## Material test bench for the table's textures (game/textures/README.md): cloth under the canopy
## light, a cushion and rail, pocket leathers, a cue and three balls, seen from cue level (left),
## from above the far corner (top right) and at the cue's butt (bottom right). Dev only; nothing in
## the game loads it.
##
## OFS_MATERIALS_PREVIEW may name a JSON file whose keys override SPEC per section (colours as
## [r, g, b] in sRGB, tiles as metres or [u, v], textures as res:// or absolute image paths), to try
## a texture or a UV scale without editing this file.

const BALL_RADIUS := 0.02625
const CUSHION_HEIGHT := 0.037
const RAIL_HEIGHT := 0.045
const HALF_WIDTH := 0.889
const HALF_LENGTH := 1.25
const CUE_BALL := Vector3(0, BALL_RADIUS, -0.2)
# Layer 20 holds what only the reflection probe sees; layer 2 the balls, which it must not capture.
const PROBE_ONLY_LAYER := 1 << 19
const BALL_LAYER := 1 << 1

const SPEC := {
	"cloth": {
		"color": Color(0.11, 0.42, 0.16),
		"albedo": "res://textures/cloth/cloth_albedo.jpg",
		"normal": "res://textures/cloth/cloth_normal.jpg",
		"roughness_map": "res://textures/cloth/cloth_roughness.jpg",
		"tile": 0.12,
		"normal_scale": 0.3,
		"roughness": 0.85,
		"rim": 0.25,
		"rim_tint": 0.6,
	},
	"rail": {
		"color": Color(1, 1, 1),
		"albedo": "res://textures/rail/rail_albedo.jpg",
		"normal": "res://textures/rail/rail_normal.jpg",
		"roughness_map": "res://textures/rail/rail_roughness.jpg",
		"tile": 2.0,
		"normal_scale": 0.3,
		"roughness": 0.6,
		"clearcoat": 1.0,
		"clearcoat_roughness": 0.08,
	},
	"leather": {
		"color": Color(1, 1, 1),
		"albedo": "res://textures/leather/leather_albedo.jpg",
		"normal": "res://textures/leather/leather_normal.jpg",
		"roughness_map": "res://textures/leather/leather_roughness.jpg",
		"tile": 0.45,
		"normal_scale": 1.0,
		"roughness": 1.0,
	},
	"cue_shaft": {
		"color": Color(1, 1, 1),
		"albedo": "res://textures/cue/ash_albedo.jpg",
		"normal": "res://textures/cue/ash_normal.jpg",
		"roughness_map": "res://textures/cue/ash_roughness.jpg",
		"tile": 1.0,
		"normal_scale": 0.3,
		"roughness": 0.7,
		"clearcoat": 0.6,
		"clearcoat_roughness": 0.2,
	},
	"cue_butt": {
		"color": Color(1, 1, 1),
		"albedo": "res://textures/cue/ebony_albedo.jpg",
		"normal": "res://textures/cue/ebony_normal.jpg",
		"roughness_map": "res://textures/cue/ebony_roughness.jpg",
		"tile": 1.0,
		"normal_scale": 0.3,
		"roughness": 0.6,
		"clearcoat": 1.0,
		"clearcoat_roughness": 0.08,
	},
	"balls": {
		"white": Color(0.93, 0.91, 0.86),
		"red": Color(0.62, 0.03, 0.03),
		"black": Color(0.02, 0.02, 0.02),
		"roughness": 0.2,
		"clearcoat_roughness": 0.02,
	},
	"stage": {
		"hdri": "res://env/studio_small_04_1k.hdr",
		"sky_energy": 0.35,
		"ambient_energy": 0.6,
		"background": Color(0.02, 0.022, 0.025),
		"light_energy": 0.7,
		# Specular of the canopy-sized area light that lights only the balls; 0 turns it off.
		# Not what Stage ships; see Stage.KEY_SPECULAR.
		"ball_highlight": 100.0,
		# "probe": an emissive canopy only a ReflectionProbe sees; "sky": the canopy painted into
		# the sky the balls reflect; "none".
		"canopy": "probe",
		"canopy_size": Vector2(1.0, 2.4),
		"canopy_height": 1.0,
		"canopy_energy": 6.0,
		"exposure": 1.0,
	},
}

var _spec := {}
var _cameras: Array[Camera3D] = []


func _ready() -> void:
	_spec = _load_spec()
	# The views render at the window's pixel size, not the stretched UI's.
	get_window().content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED
	# A viewer, not the game: keep drawing so the probe's second capture shows up.
	Perf.set_active(&"materials_preview", true)
	_build_table()
	_build_cue()
	_build_balls()
	_build_views()
	_build_stage()


func _load_spec() -> Dictionary:
	var spec := SPEC.duplicate(true)
	var path := OS.get_environment("OFS_MATERIALS_PREVIEW")
	if path.is_empty():
		return spec
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not parsed is Dictionary:
		push_warning("materials preview: %s is not a JSON object" % path)
		return spec
	for section: String in parsed:
		var values: Dictionary = parsed[section]
		for key: String in values:
			var value: Variant = values[key]
			var old: Variant = spec[section].get(key)
			if old is Color and value is Array:
				value = Color(value[0], value[1], value[2])
			elif old is Vector2 and value is Array:
				value = Vector2(value[0], value[1])
			spec[section][key] = value
	return spec


func _build_stage() -> void:
	var stage: Dictionary = _spec["stage"]
	# The world shows the dim room as its background so the reflection probe captures it around
	# the canopy; the cameras override that with the plain backdrop.
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.background_color = stage["background"]
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = stage["ambient_energy"]
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.tonemap_exposure = stage["exposure"]
	var sky := Sky.new()
	sky.radiance_size = Sky.RADIANCE_SIZE_256
	var hdri := _texture(stage["hdri"])
	if stage["canopy"] == "sky":
		sky.sky_material = _canopy_sky(hdri, stage)
	else:
		var panorama := PanoramaSkyMaterial.new()
		panorama.panorama = hdri
		panorama.energy_multiplier = stage["sky_energy"]
		sky.sky_material = panorama
	env.sky = sky
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)
	var backdrop: Environment = env.duplicate()
	backdrop.background_mode = Environment.BG_COLOR
	for camera: Camera3D in _cameras:
		camera.environment = backdrop

	var height: float = stage["canopy_height"]
	var size: Vector2 = stage["canopy_size"]
	var sun := DirectionalLight3D.new()
	sun.rotation.x = -PI / 2
	sun.light_energy = stage["light_energy"]
	# The canopy's angular size seen from the cloth: soft contact shadows.
	sun.light_angular_distance = 25.0
	# Highlights come from the canopy (probe and ball highlight); a sun would add a dot.
	sun.light_specular = 0.0 if stage["canopy"] != "none" else 1.0
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	sun.directional_shadow_max_distance = 6.0
	add_child(sun)
	var highlight: float = stage["ball_highlight"]
	if highlight > 0.0:
		# The Mobile renderer's probe range is too low for the canopy to read white on a black
		# ball, so an area light the canopy's size adds it analytically, on the balls only.
		# Mostly specular: the sun already lights them.
		# Not what Stage ships; see Stage.KEY_SPECULAR.
		var area := AreaLight3D.new()
		area.position = Vector3(0, height, 0)
		area.rotation.x = -PI / 2
		area.area_size = size
		area.area_range = height + 1.0
		area.area_normalize_energy = false
		area.light_energy = 0.05
		area.light_specular = highlight
		area.light_cull_mask = BALL_LAYER
		add_child(area)

	if stage["canopy"] == "probe":
		var canopy := MeshInstance3D.new()
		var quad := PlaneMesh.new()
		quad.size = size
		quad.flip_faces = true
		canopy.mesh = quad
		canopy.position = Vector3(0, height, 0)
		canopy.layers = PROBE_ONLY_LAYER
		var glow := StandardMaterial3D.new()
		glow.albedo_color = Color.BLACK
		glow.emission_enabled = true
		glow.emission = Color.WHITE
		glow.emission_energy_multiplier = stage["canopy_energy"]
		glow.cull_mode = BaseMaterial3D.CULL_DISABLED
		canopy.material_override = glow
		add_child(canopy)
		var probe := ReflectionProbe.new()
		probe.size = Vector3(2 * HALF_WIDTH + 0.4, height + 0.1, 2 * HALF_LENGTH + 0.4)
		probe.position = Vector3(0, (height + 0.1) / 2 - 0.05, 0)
		probe.origin_offset = Vector3(0, BALL_RADIUS - probe.position.y, 0)
		probe.box_projection = true
		# The default 1 m fade is wider than the box is tall and would leave the balls near zero.
		probe.blend_distance = 0.02
		probe.cull_mask = 1 | PROBE_ONLY_LAYER
		probe.update_mode = ReflectionProbe.UPDATE_ONCE
		add_child(probe)
		# A capture in the first frames can miss meshes whose pipelines are still compiling (the
		# cloth then reflects black), so capture again once the scene has settled.
		await get_tree().create_timer(0.5).timeout
		probe.update_mode = ReflectionProbe.UPDATE_ONCE


func _canopy_sky(hdri: Texture2D, stage: Dictionary) -> ShaderMaterial:
	var shader := Shader.new()
	shader.code = """
shader_type sky;
uniform sampler2D panorama : filter_linear, repeat_enable;
uniform float energy = 1.0;
uniform vec2 half_size = vec2(0.5, 1.2);
uniform float height = 1.0;
uniform float canopy_energy = 6.0;

void sky() {
	vec3 c = texture(panorama, SKY_COORDS).rgb * energy;
	if (EYEDIR.y > 0.0) {
		vec2 p = EYEDIR.xz / EYEDIR.y * height;
		if (abs(p.x) < half_size.x && abs(p.y) < half_size.y) {
			c = vec3(canopy_energy);
		}
	}
	COLOR = c;
}
"""
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("panorama", hdri)
	material.set_shader_parameter("energy", stage["sky_energy"])
	material.set_shader_parameter("half_size", stage["canopy_size"] / 2.0)
	material.set_shader_parameter("height", stage["canopy_height"])
	material.set_shader_parameter("canopy_energy", stage["canopy_energy"])
	return material


func _build_table() -> void:
	var cloth := _material("cloth")
	var plane := MeshInstance3D.new()
	# Turned so u runs down the table, along the nap.
	plane.mesh = _box(Vector3(2 * HALF_LENGTH, 0.01, 2 * HALF_WIDTH))
	plane.rotation.y = PI / 2
	plane.position = Vector3(0, -0.005, 0)
	plane.material_override = cloth
	add_child(plane)

	var rail := _material("rail")
	var cushion_width := 0.05
	var rail_width := 0.11
	var rail_depth := RAIL_HEIGHT + 0.12
	var rail_y := RAIL_HEIGHT - rail_depth / 2
	# Cushions and rails along both sides and across the far end.
	for side: int in 3:
		var along := 2 * HALF_LENGTH if side < 2 else 2 * HALF_WIDTH + 2 * (cushion_width + rail_width)
		var cushion := MeshInstance3D.new()
		var rail_block := MeshInstance3D.new()
		cushion.mesh = _box(Vector3(along, CUSHION_HEIGHT, cushion_width))
		rail_block.mesh = _box(Vector3(along, rail_depth, rail_width))
		cushion.material_override = cloth
		rail_block.material_override = rail
		if side < 2:
			var x_sign := -1.0 if side == 0 else 1.0
			cushion.rotation.y = PI / 2
			rail_block.rotation.y = PI / 2
			cushion.position = Vector3(x_sign * (HALF_WIDTH + cushion_width / 2), CUSHION_HEIGHT / 2, 0)
			rail_block.position = Vector3(x_sign * (HALF_WIDTH + cushion_width + rail_width / 2), rail_y, 0)
		else:
			cushion.position = Vector3(0, CUSHION_HEIGHT / 2, -HALF_LENGTH - cushion_width / 2)
			rail_block.position = Vector3(0, rail_y, -HALF_LENGTH - cushion_width - rail_width / 2)
		add_child(cushion)
		add_child(rail_block)

	var leather := _material("leather")
	# Pocket leathers at the two far corners: a drop over the corner and a flat shield.
	var drop := MeshInstance3D.new()
	drop.mesh = _lathe(0.06, 0.075, 0.075, 48, true)
	drop.position = Vector3(-HALF_WIDTH - cushion_width, 0.0, -HALF_LENGTH - cushion_width)
	drop.material_override = leather
	add_child(drop)
	var shield := MeshInstance3D.new()
	shield.mesh = _box(Vector3(0.24, 0.05, 0.16))
	shield.position = Vector3(HALF_WIDTH - 0.2, RAIL_HEIGHT + 0.025, -HALF_LENGTH - cushion_width - rail_width / 2)
	shield.material_override = leather
	add_child(shield)


func _build_cue() -> void:
	var cue := Node3D.new()
	# Behind the cue ball, aimed down the table, butt raised a little.
	cue.position = CUE_BALL + Vector3(0, 0, BALL_RADIUS + 0.03)
	cue.rotation.x = -PI / 2 - deg_to_rad(5.0)
	add_child(cue)
	var tip := MeshInstance3D.new()
	var tip_mesh := CylinderMesh.new()
	tip_mesh.top_radius = 0.0049
	tip_mesh.bottom_radius = 0.0049
	tip_mesh.height = 0.006
	tip.mesh = tip_mesh
	tip.position.y = -0.003
	tip.material_override = _plain(Color(0.16, 0.22, 0.3), 0.85)
	cue.add_child(tip)
	var ferrule := MeshInstance3D.new()
	var ferrule_mesh := CylinderMesh.new()
	ferrule_mesh.top_radius = 0.0049
	ferrule_mesh.bottom_radius = 0.005
	ferrule_mesh.height = 0.018
	ferrule.mesh = ferrule_mesh
	ferrule.position.y = -0.006 - 0.009
	var brass := _plain(Color(0.78, 0.6, 0.3), 0.3)
	brass.metallic = 1.0
	ferrule.material_override = brass
	cue.add_child(ferrule)
	var shaft := MeshInstance3D.new()
	shaft.mesh = _lathe(0.95, 0.005, 0.0105, 32, false)
	shaft.position.y = -0.024
	shaft.rotation.x = PI
	shaft.material_override = _material("cue_shaft")
	cue.add_child(shaft)
	var butt := MeshInstance3D.new()
	butt.mesh = _lathe(0.5, 0.0105, 0.0145, 32, true)
	butt.position.y = -0.024 - 0.95
	butt.rotation.x = PI
	butt.material_override = _material("cue_butt")
	cue.add_child(butt)


func _build_balls() -> void:
	var balls: Dictionary = _spec["balls"]
	var placed := {
		"white": CUE_BALL,
		"red": Vector3(-0.3, BALL_RADIUS, -0.6),
		"black": Vector3(-0.55, BALL_RADIUS, -0.9),
	}
	var sphere := SphereMesh.new()
	sphere.radius = BALL_RADIUS
	sphere.height = 2 * BALL_RADIUS
	sphere.radial_segments = 64
	sphere.rings = 32
	for ball_name: String in placed:
		var ball := MeshInstance3D.new()
		ball.mesh = sphere
		ball.position = placed[ball_name]
		ball.layers = BALL_LAYER
		var material := _plain(balls[ball_name], balls["roughness"])
		material.clearcoat_enabled = true
		material.clearcoat = 1.0
		material.clearcoat_roughness = balls["clearcoat_roughness"]
		ball.material_override = material
		add_child(ball)
	if balls.get("mirror", false):
		# A chrome ball shows exactly what the balls reflect.
		var mirror := MeshInstance3D.new()
		mirror.mesh = sphere
		mirror.position = Vector3(-0.4, BALL_RADIUS, -0.85)
		mirror.layers = BALL_LAYER
		var chrome := _plain(Color(0.95, 0.95, 0.95), 0.0)
		chrome.metallic = 1.0
		mirror.material_override = chrome
		add_child(mirror)


func _build_views() -> void:
	var row := HBoxContainer.new()
	row.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	row.add_theme_constant_override("separation", 0)
	add_child(row)
	var right := VBoxContainer.new()
	right.add_theme_constant_override("separation", 0)
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	# Cue level on the left; the far corner from above and the cue's butt on the right.
	var views := [
		[row, CUE_BALL + Vector3(0.03, 0.15, 0.63), CUE_BALL + Vector3(-0.1, -BALL_RADIUS, -0.65), 50.0],
		[right, Vector3(-0.38, 0.36, -0.62), Vector3(-0.8, 0.0, -1.18), 45.0],
		[right, Vector3(0.16, 0.2, 0.68), Vector3(0.0, 0.11, 1.0), 40.0],
	]
	for view: Array in views:
		var container := SubViewportContainer.new()
		container.stretch = true
		container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		container.size_flags_vertical = Control.SIZE_EXPAND_FILL
		(view[0] as Container).add_child(container)
		if view[0] == row:
			row.add_child(right)
		var viewport := SubViewport.new()
		viewport.msaa_3d = Viewport.MSAA_4X
		container.add_child(viewport)
		var camera := Camera3D.new()
		camera.fov = view[3]
		camera.cull_mask = 0xFFFFF & ~PROBE_ONLY_LAYER
		viewport.add_child(camera)
		camera.look_at_from_position(view[1], view[2])
		_cameras.append(camera)


func _material(section: String) -> StandardMaterial3D:
	var spec: Dictionary = _spec[section]
	var material := StandardMaterial3D.new()
	material.albedo_color = spec["color"]
	if spec.get("albedo", "") != "":
		material.albedo_texture = _texture(spec["albedo"])
	if spec.get("normal", "") != "":
		material.normal_enabled = true
		material.normal_texture = _texture(spec["normal"])
		material.normal_scale = spec.get("normal_scale", 1.0)
	material.roughness = spec.get("roughness", 1.0)
	if spec.get("roughness_map", "") != "":
		material.roughness_texture = _texture(spec["roughness_map"])
		material.roughness_texture_channel = BaseMaterial3D.TEXTURE_CHANNEL_RED
	var tile: Variant = spec.get("tile", 1.0)
	if tile is Array:
		material.uv1_scale = Vector3(1.0 / tile[0], 1.0 / tile[1], 1.0)
	else:
		material.uv1_scale = Vector3.ONE / float(tile)
	if spec.get("clearcoat", 0.0) > 0.0:
		material.clearcoat_enabled = true
		material.clearcoat = spec["clearcoat"]
		material.clearcoat_roughness = spec.get("clearcoat_roughness", 0.1)
	if spec.get("rim", 0.0) > 0.0:
		material.rim_enabled = true
		material.rim = spec["rim"]
		material.rim_tint = spec.get("rim_tint", 0.5)
	return material


func _plain(color: Color, roughness: float) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = roughness
	return material


func _texture(path: String) -> Texture2D:
	if path.begins_with("res://"):
		return load(path)
	var image := Image.load_from_file(path)
	if image == null:
		push_warning("materials preview: cannot load %s" % path)
		return null
	image.generate_mipmaps()
	return ImageTexture.create_from_image(image)


## A box whose UVs are in metres: u along x (or z on the end faces), v along the other axis.
static func _box(size: Vector3) -> ArrayMesh:
	var h := size / 2
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	# Each face: normal, the in-plane axis drawn as u ("right" seen from outside), and v ("up").
	var faces := [
		[Vector3.UP, Vector3.RIGHT, Vector3.FORWARD],
		[Vector3.DOWN, Vector3.RIGHT, Vector3.BACK],
		[Vector3.BACK, Vector3.RIGHT, Vector3.UP],
		[Vector3.FORWARD, Vector3.LEFT, Vector3.UP],
		[Vector3.RIGHT, Vector3.FORWARD, Vector3.UP],
		[Vector3.LEFT, Vector3.BACK, Vector3.UP],
	]
	for face: Array in faces:
		var n: Vector3 = face[0]
		var a: Vector3 = face[1]
		var b: Vector3 = face[2]
		var centre := n * h
		var ea := (a * h).abs().length()
		var eb := (b * h).abs().length()
		var bl := centre - a * ea - b * eb
		var tl := centre - a * ea + b * eb
		var tr := centre + a * ea + b * eb
		var br := centre + a * ea - b * eb
		var corners: Array[Vector3] = [bl, tl, tr, bl, tr, br]
		for p: Vector3 in corners:
			var u := p.z if absf(n.x) > 0.5 else p.x
			var v := p.z if absf(n.y) > 0.5 else p.y
			st.set_normal(n)
			st.set_uv(Vector2(u, v))
			st.add_vertex(p)
	st.generate_tangents()
	return st.commit()


## A tapered tube along +y from radius r0 at 0 to r1 at length, UVs in metres: u along the
## length, v around it, so wood grain drawn along u follows the cue.
static func _lathe(length: float, r0: float, r1: float, sides: int, cap: bool) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var slope := (r0 - r1) / length
	var mid := (r0 + r1) / 2
	for i: int in sides:
		var a0 := TAU * i / sides
		var a1 := TAU * (i + 1) / sides
		# Seen from outside, "right" is the lower angle.
		var quad: Array = [[a1, 0.0], [a1, length], [a0, length], [a1, 0.0], [a0, length], [a0, 0.0]]
		for corner: Array in quad:
			var angle: float = corner[0]
			var y: float = corner[1]
			var r := lerpf(r0, r1, y / length)
			st.set_normal(Vector3(cos(angle), slope, sin(angle)).normalized())
			st.set_uv(Vector2(y, angle * mid))
			st.add_vertex(Vector3(cos(angle) * r, y, sin(angle) * r))
		if cap:
			var fan: Array = [[0.0, 0.0], [a0, r1], [a1, r1]]
			for corner: Array in fan:
				var angle: float = corner[0]
				var r: float = corner[1]
				st.set_normal(Vector3.UP)
				st.set_uv(Vector2(cos(angle) * r, sin(angle) * r))
				st.add_vertex(Vector3(cos(angle) * r, length, sin(angle) * r))
	st.generate_tangents()
	return st.commit()
