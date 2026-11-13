class_name TableSpace
extends RefCounted
## Maps core table coordinates (core/include/ofs/table_geometry.hpp: metres, +x towards the
## black, +y to the left seen from baulk, z up) to Godot's (metres, Y up). A camera at the
## baulk end looking down -Z sees the table as a player standing at baulk does.


static func core_to_godot(x: float, y: float, height: float = 0.0) -> Vector3:
	return Vector3(-y, height, -x)


static func godot_to_core(v: Vector3) -> Vector2:
	return Vector2(-v.z, -v.x)


## Core direction angle (from +x towards +y) to a Godot yaw about +Y for a node whose forward is -Z.
static func core_angle_to_yaw(angle_rad: float) -> float:
	return angle_rad


## Where the ray through a screen point meets the horizontal plane at `height` above the cloth,
## in core (x, y); null when the ray runs parallel to it or points away.
static func screen_to_core(camera: Camera3D, screen_pos: Vector2, height: float = 0.0) -> Variant:
	var hit: Variant = Plane(Vector3.UP, height).intersects_ray(
			camera.project_ray_origin(screen_pos), camera.project_ray_normal(screen_pos))
	return godot_to_core(hit) if hit != null else null
