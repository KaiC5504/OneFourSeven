class_name Stage
extends Node3D
## The scene around the table (spec 13.1; game/textures/README.md): TV snooker's dark arena with a
## light over the table. The cameras see a plain dark backdrop; the balls and rails show a faint glow
## from the canopy, the balls the green cloth round their lower halves, and the dark arena elsewhere.
## Children: Light (the key light, straight down with soft contact shadows), WorldEnvironment, and
## once set up the Canopy, the Reflections probe, the Floor the table stands on and, when the bought
## model is in the project (res://licensed/lamp.gltf), its Lamp over the table.
##
## The reflection probe captures the table and the canopy twice (capture_reflections): in the
## first frame drawn, and again once the renderer has stopped compiling pipelines. Whatever moves
## stays out of it on its own layer, or the balls would reflect a stale table.

## The table, which the probe captures.
const TABLE_LAYER := 1
## The balls: left out of the probe (a ball in its own probe reflects itself as a dark blob).
const BALL_LAYER := 1 << 1
## The cue, the guides and anything else that moves.
const MOVING_LAYER := 1 << 2
## What stands round the table: the lamp and the floor. The cameras see it;
## the probe leaves it out and doesn't light it.
const SCENERY_LAYER := 1 << 3
## The canopy, which only the probe sees.
const PROBE_ONLY_LAYER := 1 << 19
## What the probe lights. The cue moves through the box and would mirror the cloth: green ebony and
## olive brass.
const PROBE_RECEIVERS := TABLE_LAYER | BALL_LAYER

const BACKDROP := Color(0.02, 0.022, 0.025)
const HDRI_PATH := "res://env/studio_small_04_1k.hdr"
const SKY_ENERGY := 0.35
const AMBIENT_ENERGY := 0.7
const KEY_ENERGY := 0.55
## The canopy's size seen from the cloth: soft contact shadows.
const KEY_ANGULAR_DISTANCE := 25.0
## The canopy glows brightest in its middle and fades to nothing at its edges, so the balls and the
## lacquered rails show a soft glow instead of a lamp's hard outline.
const CANOPY_SIZE := Vector2(1.0, 1.6)
const CANOPY_HEIGHT := 1.0
const CANOPY_ENERGY := 0.4
## The probe's box reaches this far past the playing area on each side.
const PROBE_MARGIN := 0.2
## The default 1 m fade is wider than the box is tall and would leave the balls near zero.
const PROBE_BLEND := 0.02
## The first capture can miss meshes whose pipelines are still compiling (the cloth then reflects
## black), so the probe captures again once this many frames in a row have compiled nothing, or
## after RECAPTURE_LATEST_FRAMES whatever is compiling. Waiting for the compiles keeps the second
## capture off the frames that already stall, which on the phone take seconds. Frames, not time:
## the phone's first frame alone takes longer than any sensible timeout.
const SETTLED_FRAMES := 3
const RECAPTURE_LATEST_FRAMES := 120
## A faint, soft highlight on the balls. The README's canopy-sized area light read as a hard white rectangle.
const KEY_SPECULAR := 0.32
# A full-size table with its rails, before TableView reports the real bounds.
const DEFAULT_BOUNDS := AABB(Vector3(-1.1, -0.7, -1.95), Vector3(2.2, 0.75, 3.9))
const PLAYING_FALLBACK := Vector2(3.569, 1.778)
const BALL_RADIUS_FALLBACK := 0.02625
const MIN_SHADOW_DISTANCE := 1.0
const MAX_SHADOW_DISTANCE := 12.0
## The floor under the bought table's legs, below the cloth (game/licensed/fit.json, bed_z); the
## procedural table's frame comes down to it too.
const FLOOR_Y := -0.8208
## The floor is lit round the table and fades into the backdrop between these distances from the
## table's centre, so it has no edge and no horizon.
const FLOOR_FADE_FROM := 2.5
const FLOOR_FADE_TO := 7.0
const FLOOR_SIZE := 16.0
const FLOOR_COLOUR := Color(0.16, 0.15, 0.14)
## The table's shadow on the floor is drawn by the floor itself, from the table's outline: as soft
## as the canopy's size makes it at that depth (the Mobile renderer's shadow maps have one fixed,
## narrow blur), and the shadow range stays fitted to the table top. Under the table the floor keeps
## FLOOR_SHADE of its light, and the shadow's edge is FLOOR_PENUMBRA wide.
const FLOOR_SHADE := 0.22
const FLOOR_PENUMBRA := 0.36
const LAMP_PATH := "res://licensed/lamp.gltf"
const LAMP_PART := "Lamp"
## The lamp hangs from 0.79 m above the cloth. A camera below LAMP_SHOWN_BELOW sees all of it; it
## fades as the camera rises and is gone by LAMP_GONE_ABOVE, still under the lamp, so it never comes
## between a camera and the table.
const LAMP_SHOWN_BELOW := 0.55
const LAMP_GONE_ABOVE := 0.70
const LAMP_METAL := "PoolTableLightsShader"
const LAMP_SHADE := "PoolTableLightsGlassShader"
## The fit turned the shades' glass into plain opaque paint; as lit glass they are pale and glow a
## little from the bulbs inside.
const SHADE_COLOUR := Color(0.86, 0.84, 0.78)
const SHADE_GLOW := Color(1.0, 0.93, 0.8)
const SHADE_GLOW_ENERGY := 0.2
const SHADE_ROUGHNESS := 0.35
const BULB_ENERGY := 3.0
## The bar and fittings are brass in the model's textures, but fully metallic: all they would show
## is the dark arena. Part metal, their own colour takes the key light.
const LAMP_METALLIC := 0.3
## Seen from the table the brass is lit from below, by the cloth, which no light here stands for:
## a faint glow of its own instead (added to the bulbs' emission, so scaled by BULB_ENERGY).
const BRASS_BOUNCE := Color(0.012, 0.011, 0.006)
const FLOOR_SHADER := """
shader_type spatial;
render_mode cull_back, shadows_disabled;
uniform vec3 colour : source_color;
uniform vec3 backdrop : source_color;
uniform float fade_from;
uniform float fade_to;
uniform vec2 table_centre;
uniform vec2 table_half;
uniform float shade;
uniform float penumbra;
varying vec2 at;

void vertex() {
	at = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xz;
}

void fragment() {
	float gone = smoothstep(fade_from, fade_to, length(at));
	vec2 past = abs(at - table_centre) - table_half;
	float outside = length(max(past, vec2(0.0))) + min(max(past.x, past.y), 0.0);
	float lit = mix(shade, 1.0, smoothstep(-0.5 * penumbra, 0.5 * penumbra, outside));
	ALBEDO = colour * lit * (1.0 - gone);
	ROUGHNESS = 1.0;
	SPECULAR = 0.0;
	// A grain under one step of the screen, or the slow fade shows as rings.
	float grain = fract(sin(dot(FRAGCOORD.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
	EMISSION = backdrop * gone + vec3(grain * 0.0012);
}
"""

## Whether setup() hangs the lamp when its file is there. Off by itself in a headless run, which
## draws nothing (as TableView.Source.AUTO).
var lamp_enabled := DisplayServer.get_name() != "headless"
var lamp_path := LAMP_PATH

var _bounds := DEFAULT_BOUNDS
var _lamp: MeshInstance3D
var _floor_material: ShaderMaterial
var _backdrop: Environment
var _probe: ReflectionProbe
var _capturing := false
var _recaptured := false
var _capture_frames := 0
var _compilations := -1
var _quiet_frames := 0


func setup() -> void:
	var light := get_node_or_null("Light") as DirectionalLight3D
	if light == null:
		light = DirectionalLight3D.new()
		light.name = "Light"
		add_child(light)
	var world := get_node_or_null("WorldEnvironment") as WorldEnvironment
	if world == null:
		world = WorldEnvironment.new()
		world.name = "WorldEnvironment"
		add_child(world)

	# The dim studio lights the scene and is what the cue, outside the probe, reflects; the cameras
	# show the plain backdrop instead (dress_camera).
	var sky := Sky.new()
	sky.radiance_size = Sky.RADIANCE_SIZE_128
	var panorama := PanoramaSkyMaterial.new()
	panorama.panorama = _load_texture(HDRI_PATH)
	panorama.energy_multiplier = SKY_ENERGY
	sky.sky_material = panorama
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.background_color = BACKDROP
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = AMBIENT_ENERGY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.tonemap_exposure = 1.0
	world.environment = env
	_backdrop = env.duplicate()
	_backdrop.background_mode = Environment.BG_COLOR

	light.rotation = Vector3(-PI / 2.0, 0.0, 0.0)
	light.light_energy = KEY_ENERGY
	light.light_angular_distance = KEY_ANGULAR_DISTANCE
	light.light_specular = KEY_SPECULAR
	light.shadow_enabled = true
	# One split over a range fitted to the table spends the whole shadow map on it.
	light.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	light.directional_shadow_max_distance = 6.0
	# The Mobile renderer defaults to hard shadows on phones, which alias into a sawtooth.
	RenderingServer.directional_soft_shadow_filter_set_quality(RenderingServer.SHADOW_QUALITY_SOFT_LOW)

	_add_canopy()
	_add_floor()
	_add_lamp()


## The camera sees the plain backdrop, and everything but the canopy.
func dress_camera(camera: Camera3D) -> void:
	if _backdrop != null:
		camera.environment = _backdrop
	camera.cull_mask &= ~PROBE_ONLY_LAYER
	if camera.is_inside_tree():
		_fade_lamp(camera.global_position.y)


## Captures the table and canopy into the balls' reflections in the next frame drawn, and again
## once pipeline compiling has settled: call once the table is built, and after any change to its
## materials.
func capture_reflections() -> void:
	if _probe == null:
		return
	_probe.update_mode = ReflectionProbe.UPDATE_ONCE
	_recaptured = false
	_capture_frames = 0
	_compilations = -1
	_quiet_frames = 0
	if not _capturing:
		_capturing = true
		# Idle frames aren't drawn, and a capture needs one.
		Perf.set_active(&"reflection_capture", true)
	set_process(true)


## The table's extent in world space (TableView.bounds()): fit_shadows covers all of it, and the
## floor draws its shadow.
func set_table_bounds(bounds: AABB) -> void:
	_bounds = bounds if bounds.has_volume() else DEFAULT_BOUNDS
	_shade_floor()


func _shade_floor() -> void:
	if _floor_material == null:
		return
	var centre := _bounds.get_center()
	_floor_material.set_shader_parameter("table_centre", Vector2(centre.x, centre.z))
	_floor_material.set_shader_parameter("table_half", Vector2(_bounds.size.x, _bounds.size.z) / 2.0)


## Fits the shadow range to what `camera` sees of the table, and shows or hides the lamp for where
## the camera stands; call when the camera moves.
func fit_shadows(camera: Camera3D) -> void:
	var light := get_node_or_null("Light") as DirectionalLight3D
	if light == null or camera == null:
		return
	var eye := camera.global_position
	_fade_lamp(eye.y)
	var forward := -camera.global_transform.basis.z
	var farthest := 0.0
	for i in 8:
		farthest = maxf(farthest, (_bounds.get_endpoint(i) - eye).dot(forward))
	light.directional_shadow_max_distance = clampf(farthest + 0.1, MIN_SHADOW_DISTANCE, MAX_SHADOW_DISTANCE)


func _ready() -> void:
	set_process(_capturing)


func _process(_delta: float) -> void:
	if not _capturing:
		set_process(false)
		return
	_capture_frames += 1
	var compiled := pipeline_compilations()
	if compiled != _compilations:
		_compilations = compiled
		_quiet_frames = 0
	else:
		_quiet_frames += 1
	if _recaptured:
		# The frame after the one that drew the second capture.
		_capturing = false
		Perf.set_active(&"reflection_capture", false)
		set_process(false)
	elif _quiet_frames >= SETTLED_FRAMES or _capture_frames >= RECAPTURE_LATEST_FRAMES:
		_probe.update_mode = ReflectionProbe.UPDATE_ONCE
		_recaptured = true


## Pipelines the renderer has compiled so far, of every kind.
static func pipeline_compilations() -> int:
	var rs := RenderingServer
	return (rs.get_rendering_info(rs.RENDERING_INFO_PIPELINE_COMPILATIONS_CANVAS)
			+ rs.get_rendering_info(rs.RENDERING_INFO_PIPELINE_COMPILATIONS_MESH)
			+ rs.get_rendering_info(rs.RENDERING_INFO_PIPELINE_COMPILATIONS_SURFACE)
			+ rs.get_rendering_info(rs.RENDERING_INFO_PIPELINE_COMPILATIONS_DRAW)
			+ rs.get_rendering_info(rs.RENDERING_INFO_PIPELINE_COMPILATIONS_SPECIALIZATION))


func _exit_tree() -> void:
	if _capturing:
		_capturing = false
		Perf.set_active(&"reflection_capture", false)


## How much of the lamp a camera `height` above the cloth sees: 1 all of it, 0 none.
static func lamp_shown(height: float) -> float:
	return 1.0 - smoothstep(LAMP_SHOWN_BELOW, LAMP_GONE_ABOVE, height)


func _fade_lamp(height: float) -> void:
	if _lamp == null:
		return
	var shown := lamp_shown(height)
	_lamp.visible = shown > 0.0
	_lamp.transparency = 1.0 - shown


func _drop(child: String) -> void:
	var node := get_node_or_null(child)
	if node != null:
		remove_child(node)
		node.queue_free()


## A plain dark floor for the table to stand on, with the table's soft shadow.
func _add_floor() -> void:
	_drop("Floor")
	var ground := MeshInstance3D.new()
	ground.name = "Floor"
	var plane := PlaneMesh.new()
	plane.size = Vector2(FLOOR_SIZE, FLOOR_SIZE)
	ground.mesh = plane
	ground.position = Vector3(0.0, FLOOR_Y, 0.0)
	ground.layers = SCENERY_LAYER
	ground.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var shader := Shader.new()
	shader.code = FLOOR_SHADER
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("colour", FLOOR_COLOUR)
	material.set_shader_parameter("backdrop", BACKDROP)
	material.set_shader_parameter("fade_from", FLOOR_FADE_FROM)
	material.set_shader_parameter("fade_to", FLOOR_FADE_TO)
	material.set_shader_parameter("shade", FLOOR_SHADE)
	material.set_shader_parameter("penumbra", FLOOR_PENUMBRA)
	ground.material_override = material
	add_child(ground)
	_floor_material = material
	_shade_floor()


## Hangs the bought lamp over the table when it is in the project. It lights nothing and casts no
## shadow: the key light and the canopy stand for it.
func _add_lamp() -> void:
	_drop("Lamp")
	_lamp = null
	if not lamp_enabled or not ResourceLoader.exists(lamp_path):
		return
	var scene := load(lamp_path) as PackedScene
	var model: Node = scene.instantiate() if scene != null else null
	var lamp: MeshInstance3D = model.get_node_or_null(LAMP_PART) as MeshInstance3D if model != null else null
	if lamp == null:
		var wrong := "isn't a scene Godot can load; import the project again"
		if model != null:
			wrong = "has no mesh named %s; run tools/fit_table.py again" % LAMP_PART
			model.free()
		Log.warn("app", "app.error", {"kind": "message", "file": lamp_path,
				"message": "stage: no lamp over the table: %s %s" % [lamp_path, wrong]})
		return
	model.remove_child(lamp)
	model.free()
	lamp.owner = null
	lamp.layers = SCENERY_LAYER
	lamp.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	for surface in lamp.mesh.get_surface_count():
		var material := lamp.mesh.surface_get_material(surface) as BaseMaterial3D
		if material == null:
			continue
		# The imported materials are shared with every other load of the scene: tune copies.
		material = material.duplicate()
		material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
		match material.resource_name:
			LAMP_METAL:
				material.emission_energy_multiplier = BULB_ENERGY
				material.emission_operator = BaseMaterial3D.EMISSION_OP_ADD
				material.emission = BRASS_BOUNCE
				material.metallic = LAMP_METALLIC
			LAMP_SHADE:
				material.albedo_texture = null
				material.vertex_color_use_as_albedo = false
				material.albedo_color = SHADE_COLOUR
				material.roughness_texture = null
				material.roughness = SHADE_ROUGHNESS
				material.emission_enabled = true
				material.emission = SHADE_GLOW
				material.emission_energy_multiplier = SHADE_GLOW_ENERGY
		lamp.set_surface_override_material(surface, material)
	_lamp = lamp
	add_child(lamp)


func _add_canopy() -> void:
	for old: String in ["Canopy", "Reflections"]:
		_drop(old)
	var dims := OfsTable.dimensions()
	var playing := Vector2(float(dims.get("playing_length", PLAYING_FALLBACK.x)),
			float(dims.get("playing_width", PLAYING_FALLBACK.y)))
	var ball_radius := float(dims.get("ball_radius", BALL_RADIUS_FALLBACK))

	var canopy := MeshInstance3D.new()
	canopy.name = "Canopy"
	var quad := PlaneMesh.new()
	# Godot's x runs across the table and z along it (TableSpace).
	quad.size = CANOPY_SIZE
	quad.flip_faces = true
	canopy.mesh = quad
	canopy.position = Vector3(0.0, CANOPY_HEIGHT, 0.0)
	canopy.layers = PROBE_ONLY_LAYER
	canopy.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var glow := StandardMaterial3D.new()
	glow.albedo_color = Color.BLACK
	glow.emission_enabled = true
	glow.emission = Color.WHITE
	glow.emission_energy_multiplier = CANOPY_ENERGY
	glow.emission_texture = _canopy_falloff()
	glow.cull_mode = BaseMaterial3D.CULL_DISABLED
	canopy.material_override = glow
	add_child(canopy)

	_probe = ReflectionProbe.new()
	_probe.name = "Reflections"
	_probe.size = Vector3(playing.y + 2.0 * PROBE_MARGIN, CANOPY_HEIGHT + 0.1, playing.x + 2.0 * PROBE_MARGIN)
	_probe.position = Vector3(0.0, (CANOPY_HEIGHT + 0.1) / 2.0 - 0.05, 0.0)
	_probe.origin_offset = Vector3(0.0, ball_radius - _probe.position.y, 0.0)
	# Box projection cut the canopy's glow into hard-edged shards on balls near the cushions.
	_probe.box_projection = false
	# Leaves the studio's bright panels out of the capture: around the canopy the balls and rails
	# reflect the dark arena (project.godot's default clear colour; Godot's grey made the rails milky).
	_probe.interior = true
	_probe.blend_distance = PROBE_BLEND
	_probe.cull_mask = TABLE_LAYER | PROBE_ONLY_LAYER
	_probe.reflection_mask = PROBE_RECEIVERS
	_probe.update_mode = ReflectionProbe.UPDATE_ONCE
	add_child(_probe)


static func _canopy_falloff() -> GradientTexture2D:
	var gradient := Gradient.new()
	gradient.interpolation_mode = Gradient.GRADIENT_INTERPOLATE_CUBIC
	gradient.offsets = PackedFloat32Array([0.0, 0.3, 0.65, 1.0])
	gradient.colors = PackedColorArray([Color.WHITE, Color(0.6, 0.6, 0.6), Color(0.15, 0.15, 0.15), Color.BLACK])
	var texture := GradientTexture2D.new()
	texture.gradient = gradient
	texture.fill = GradientTexture2D.FILL_RADIAL
	texture.fill_from = Vector2(0.5, 0.5)
	texture.fill_to = Vector2(1.0, 0.5)
	texture.width = 64
	texture.height = 64
	return texture


static func _load_texture(path: String) -> Texture2D:
	var texture := load(path) as Texture2D
	if texture == null:
		Log.error("app", "app.error", {"kind": "message", "message": "stage: %s didn't load; the balls reflect nothing round the canopy" % path, "file": path})
	return texture
