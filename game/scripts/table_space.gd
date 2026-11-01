class_name TableSpace
extends RefCounted
## Maps core table coordinates (core/include/ofs/table_geometry.hpp: metres, +x towards the
## black, +y to the left seen from baulk, z up) to Godot's (metres, Y up). A camera at the
## baulk end looking down -Z sees the table as a player standing at baulk does.


static func core_to_godot(x: float, y: float, height: float = 0.0) -> Vector3:
	return Vector3(-y, height, -x)


static func godot_to_core(v: Vector3) -> Vector2:
	return Vector2(-v.z, -v.x)
