extends TestCase
## SpinAim (spec 8.2; KaiC, 2026-11-15): side must not move the pot. Played through the real
## simulator.

const R := 0.02625
const CUE := Vector2(-0.8, 0.0)


## A cue ball aimed along +x at a red `distance` away, cut by `cut_deg`.
func _table(distance: float, cut_deg: float) -> Array[Dictionary]:
	var cut := deg_to_rad(cut_deg)
	var ghost := CUE + Vector2(distance, 0.0)
	var red := ghost + Vector2.from_angle(cut) * 2.0 * R
	var balls: Array[Dictionary] = [
		{"ball": "cue", "index": 0, "x": CUE.x, "y": CUE.y},
		{"ball": "red", "index": 0, "x": red.x, "y": red.y},
	]
	return balls


func _strike(tip_x: float, tip_y: float, speed: float, aim := 0.0) -> Dictionary:
	return {"aim_rad": aim, "tip_x": tip_x, "tip_y": tip_y, "elevation_rad": 0.0, "speed_m_s": speed}


## The direction the red leaves in, and where the cue ball was then.
func _first_hit(balls: Array[Dictionary], strike: Dictionary) -> Dictionary:
	return SpinAim._outcome(Physics.predict, balls, 0, strike, CUE)


func _forced(balls: Array[Dictionary], strike: Dictionary) -> float:
	return Physics.min_cue_elevation(balls, 0, strike)


func test_side_no_longer_moves_the_pot() -> void:
	for case: Array in [[1.5, 0.0, 0.1, 0.0, 2.0], [1.5, 30.0, 0.05, 0.0, 1.5], [0.5, 30.0, 0.2, -0.2, 1.5],
			[2.5, 0.0, -0.1, 0.3, 4.0], [1.5, 50.0, -0.2, -0.3, 4.0]]:
		var balls := _table(case[0], case[1])
		var strike := _strike(case[2], case[3], case[4])
		var plain := _strike(0.0, case[3], case[4])
		var want := float(_first_hit(balls, plain)["direction"])
		var raw := angle_difference(want, float(_first_hit(balls, strike)["direction"]))
		var solved := SpinAim.solve(Physics.predict, balls, 0, strike, _forced(balls, strike))
		var allowed := strike.duplicate()
		allowed["aim_rad"] = solved["aim_rad"]
		var left := angle_difference(want, float(_first_hit(balls, allowed)["direction"]))
		var label := "%s m, %s deg cut, tip (%s, %s) at %s m/s" % case
		check(absf(raw) > deg_to_rad(0.5), "%s: as aimed, side sends the red %.2f deg off" % [label, rad_to_deg(raw)])
		check(absf(left) < deg_to_rad(0.03), "%s: allowed for, %.3f deg off" % [label, rad_to_deg(left)])
		check_eq(solved["target"], "ball", "%s: matched on the ball" % label)
		check(int(solved["sims"]) <= 9, "%s: %d predictions" % [label, solved["sims"]])
		check_near(float(solved["aim_rad"]), float(solved["turned_rad"]), 1e-12, "%s: the turn is the aim's" % label)


func test_no_side_is_left_alone() -> void:
	var balls := _table(1.5, 30.0)
	var strike := _strike(0.0, -0.3, 2.0, 0.01)
	var solved := SpinAim.solve(Physics.predict, balls, 0, strike, _forced(balls, strike))
	check_eq(solved["aim_rad"], 0.01, "the aim")
	check_eq(solved["sims"], 0, "nothing predicted")
	check_eq(solved["reason"], "no side", "why")


func test_a_cue_raised_on_purpose_is_left_alone() -> void:
	# A swerve shot: the curve is what the player is playing for.
	var balls := _table(1.5, 0.0)
	var strike := _strike(0.2, 0.0, 2.0)
	strike["elevation_rad"] = deg_to_rad(15.0)
	var solved := SpinAim.solve(Physics.predict, balls, 0, strike, _forced(balls, strike))
	check_eq(solved["aim_rad"], 0.0, "the aim")
	check_eq(solved["reason"], "cue raised on purpose", "why")
	# Raised only as far as the table forces it, the allowance stands.
	strike["elevation_rad"] = _forced(balls, strike)
	solved = SpinAim.solve(Physics.predict, balls, 0, strike, _forced(balls, strike))
	check_eq(solved["target"], "ball", "at the forced minimum it is allowed for")


func test_with_no_ball_in_the_way_the_cue_ball_reaches_the_same_cushion_point() -> void:
	var balls: Array[Dictionary] = [{"ball": "cue", "index": 0, "x": CUE.x, "y": CUE.y}]
	var strike := _strike(0.2, 0.0, 1.5, 0.3)
	var want: Vector2 = _first_hit(balls, _strike(0.0, 0.0, 1.5, 0.3))["at"]
	var raw: Vector2 = _first_hit(balls, strike)["at"]
	var solved := SpinAim.solve(Physics.predict, balls, 0, strike, _forced(balls, strike))
	var allowed := strike.duplicate()
	allowed["aim_rad"] = solved["aim_rad"]
	var got: Vector2 = _first_hit(balls, allowed)["at"]
	check(raw.distance_to(want) > 0.005, "as aimed, side lands %.1f mm along the cushion" % (raw.distance_to(want) * 1000.0))
	check(got.distance_to(want) < 0.0005, "allowed for, %.2f mm" % (got.distance_to(want) * 1000.0))
	check_eq(solved["target"], "cushion", "matched on the cushion")
