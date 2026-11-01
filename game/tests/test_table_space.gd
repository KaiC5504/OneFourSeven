extends TestCase


func test_round_trip() -> void:
	for p: Vector2 in [Vector2(0.89225, 0.0), Vector2(-1.0475, -0.292), Vector2(1.2, 0.7), Vector2.ZERO]:
		var back := TableSpace.godot_to_core(TableSpace.core_to_godot(p.x, p.y, 0.02625))
		check_near(back.x, p.x, 1e-6, "x of %s" % p)
		check_near(back.y, p.y, 1e-6, "y of %s" % p)


func test_height_is_godot_y() -> void:
	check_near(TableSpace.core_to_godot(0.3, -0.2, 0.05).y, 0.05, 1e-6, "height")


func test_towards_black_is_godot_minus_z() -> void:
	var v := TableSpace.core_to_godot(1.0, 0.0, 0.0)
	check_eq(v, Vector3(0.0, 0.0, -1.0), "core +x")


func test_left_from_baulk_is_godot_minus_x() -> void:
	# Seen from baulk (camera looking down -Z), core +y is to the left, Godot -X.
	var v := TableSpace.core_to_godot(0.0, 1.0, 0.0)
	check_eq(v, Vector3(-1.0, 0.0, 0.0), "core +y")
