class_name Stage
extends Node3D
## The scene around the table (spec 13.1): a plain, softly lit dark backdrop and one overhead
## table light with soft contact shadows. Children: Light (DirectionalLight3D), WorldEnvironment.

const BACKDROP := Color(0.028, 0.03, 0.034)
# A full-size table with its rails, before TableView reports the real bounds.
const DEFAULT_BOUNDS := AABB(Vector3(-1.1, -0.7, -1.95), Vector3(2.2, 0.75, 3.9))
const MIN_SHADOW_DISTANCE := 1.0
const MAX_SHADOW_DISTANCE := 12.0

# The balls and rails reflect the sky: dark everywhere but the canopy over the table, a soft-edged
# rectangle above it, long side along the table. The backdrop itself stays a plain colour.
const SKY_SHADER := """
shader_type sky;

uniform vec3 backdrop : source_color = vec3(0.02, 0.021, 0.024);
uniform vec3 canopy : source_color = vec3(1.0, 0.97, 0.92);
uniform float canopy_energy = 3.0;
uniform vec2 canopy_half = vec2(0.42, 0.9);

void sky() {
	float lit = 0.0;
	if (EYEDIR.y > 0.05) {
		vec2 q = abs(EYEDIR.xz / EYEDIR.y) - canopy_half;
		lit = 1.0 - smoothstep(-0.04, 0.04, max(q.x, q.y));
	}
	COLOR = mix(backdrop, canopy * canopy_energy, lit);
}
"""

var _bounds := DEFAULT_BOUNDS


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

	var sky_material := ShaderMaterial.new()
	var shader := Shader.new()
	shader.code = SKY_SHADER
	sky_material.shader = shader
	var sky := Sky.new()
	sky.sky_material = sky_material
	sky.radiance_size = Sky.RADIANCE_SIZE_128
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = BACKDROP
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.64, 0.68)
	env.ambient_light_energy = 0.3
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.tonemap_white = 6.0
	world.environment = env

	# A few degrees off vertical, so each ball's shadow peeks out from under it as a contact
	# shadow rather than hiding directly beneath.
	light.rotation_degrees = Vector3(-80.0, 10.0, 0.0)
	light.light_energy = 1.3
	light.light_color = Color(1.0, 0.98, 0.94)
	# The canopy is wide, so its shadows are soft; angular distance makes them soften with distance.
	light.light_angular_distance = 2.5
	light.shadow_enabled = true
	light.shadow_blur = 1.5
	light.shadow_bias = 0.03
	light.shadow_normal_bias = 1.2
	# One split over a range fitted to the table spends the whole shadow map on it.
	light.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	light.directional_shadow_max_distance = 6.0
	# The Mobile renderer defaults to hard shadows on phones, which alias into a sawtooth.
	RenderingServer.directional_soft_shadow_filter_set_quality(RenderingServer.SHADOW_QUALITY_SOFT_LOW)


## The table's extent in world space (TableView.bounds()); fit_shadows covers all of it.
func set_table_bounds(bounds: AABB) -> void:
	_bounds = bounds if bounds.has_volume() else DEFAULT_BOUNDS


## Fits the shadow range to what `camera` sees of the table; call when the camera moves.
func fit_shadows(camera: Camera3D) -> void:
	var light := get_node_or_null("Light") as DirectionalLight3D
	if light == null or camera == null:
		return
	var eye := camera.global_position
	var forward := -camera.global_transform.basis.z
	var farthest := 0.0
	for i in 8:
		farthest = maxf(farthest, (_bounds.get_endpoint(i) - eye).dot(forward))
	light.directional_shadow_max_distance = clampf(farthest + 0.1, MIN_SHADOW_DISTANCE, MAX_SHADOW_DISTANCE)
