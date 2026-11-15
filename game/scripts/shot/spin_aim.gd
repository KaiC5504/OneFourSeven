class_name SpinAim
extends RefCounted
## The cue action allows for side (spec 8.2; KaiC, 2026-11-15: a little spin must not cost the
## pot). Side squirts the cue ball off the aim, swerves it under the cue's forced elevation and
## throws the object ball: 5% of side sent a straight 1.5 m pot 3.7 degrees wide. As a player
## does, the aim is turned so the shot does what the same strike without side does: the first
## ball hit leaves in the same direction, or with no ball in the way the cue ball reaches the
## same point of the cushion. Side then only shows after the contact, off cushions and in the
## throw of later balls. Top and screw are left alone, and so is a cue the player raised above
## the forced minimum, which is a swerve shot played on purpose.

## Below this much side (ball radii) there is nothing to allow for.
const MIN_SIDE := 1.0e-4
## A cue this far above its forced minimum was raised on purpose.
const DELIBERATE_RAD := 0.009
## Bearing error of the cue ball's arrival that ends the first pass.
const BEARING_TOL_RAD := 1.0e-5
## Direction error of the object ball that ends the second: 0.01 degrees.
const DIRECTION_TOL_RAD := 2.0e-4
## The second pass's first step moves the contact about this far across the object ball.
const PROBE_M := 0.0003
const MAX_STEP_RAD := 0.02
const BEARING_PASSES := 3
const DIRECTION_PASSES := 4


## predictor is Physics.predict's shape: (balls, cue_ball, strike) -> OfsShotRecord or null.
## Returns {"aim_rad", "turned_rad", "sims", "target" ("ball", "cushion" or "" when nothing was
## allowed for), "residual_rad" (what is left of the error it worked on), "reason" (why nothing
## was done, else "")}.
static func solve(predictor: Callable, balls: Array[Dictionary], cue_ball: int, strike: Dictionary,
		elevation_min_rad: float) -> Dictionary:
	var aim := float(strike.get("aim_rad", 0.0))
	var out := {"aim_rad": aim, "turned_rad": 0.0, "sims": 0, "target": "", "residual_rad": 0.0, "reason": ""}
	if absf(float(strike.get("tip_x", 0.0))) < MIN_SIDE:
		out["reason"] = "no side"
		return out
	if float(strike.get("elevation_rad", 0.0)) > elevation_min_rad + DELIBERATE_RAD:
		out["reason"] = "cue raised on purpose"
		return out
	if cue_ball < 0 or cue_ball >= balls.size():
		out["reason"] = "no cue ball"
		return out
	var plain := strike.duplicate()
	plain["tip_x"] = 0.0
	var start := Vector2(float(balls[cue_ball]["x"]), float(balls[cue_ball]["y"]))
	var want := _outcome(predictor, balls, cue_ball, plain, start)
	out["sims"] = 1
	if want.is_empty():
		out["reason"] = "nothing to match"
		return out
	var target := String(want["kind"])

	# First the cue ball's arrival: squirt and swerve are an error in its bearing, and the turn
	# that removes one is the error itself.
	var turn := 0.0
	var got := {}
	for i in BEARING_PASSES:
		got = _outcome(predictor, balls, cue_ball, _turned(strike, aim + turn), start)
		out["sims"] += 1
		if got.is_empty():
			out["reason"] = "the shot with side hits nothing"
			return out
		var error := angle_difference(float(want["bearing"]), float(got["bearing"]))
		out["residual_rad"] = error
		if absf(error) < BEARING_TOL_RAD or i == BEARING_PASSES - 1:
			break
		turn -= error
	out["target"] = "cushion"
	out["turned_rad"] = turn
	if target != "ball" or not _same_ball(want, got):
		out["aim_rad"] = wrapf(aim + turn, -PI, PI)
		return out

	# Then the throw: side also turns the object ball off its line, so the contact moves until
	# that ball leaves as it does without side.
	var t0 := turn
	var f0 := angle_difference(float(want["direction"]), float(got["direction"]))
	var best_turn := t0
	var best := absf(f0)
	var t1 := t0 + PROBE_M / maxf(start.distance_to(want["at"]), 0.05)
	for i in DIRECTION_PASSES:
		if best < DIRECTION_TOL_RAD:
			break
		got = _outcome(predictor, balls, cue_ball, _turned(strike, aim + t1), start)
		out["sims"] += 1
		if got.is_empty() or not _same_ball(want, got):
			break
		var f1 := angle_difference(float(want["direction"]), float(got["direction"]))
		if absf(f1) < best:
			best = absf(f1)
			best_turn = t1
		if is_equal_approx(f1, f0):
			break
		var step := clampf(-f1 * (t1 - t0) / (f1 - f0), -MAX_STEP_RAD, MAX_STEP_RAD)
		t0 = t1
		f0 = f1
		t1 += step
	out["target"] = "ball"
	out["turned_rad"] = best_turn
	out["residual_rad"] = best
	out["aim_rad"] = wrapf(aim + best_turn, -PI, PI)
	return out


static func _turned(strike: Dictionary, aim_rad: float) -> Dictionary:
	var turned := strike.duplicate()
	turned["aim_rad"] = wrapf(aim_rad, -PI, PI)
	return turned


static func _same_ball(want: Dictionary, got: Dictionary) -> bool:
	return got["kind"] == "ball" and int(got["ball"]) == int(want["ball"])


## What the cue ball first runs into: {"kind": "ball" or "cushion", "ball" (the ball hit, else -1),
## "at" (the cue ball's centre then), "bearing" (of that point from where it started), "direction"
## (the hit ball's, as it leaves)}. Empty for a refused shot, a miscue or a cue ball that meets
## nothing.
static func _outcome(predictor: Callable, balls: Array[Dictionary], cue_ball: int, strike: Dictionary,
		start: Vector2) -> Dictionary:
	var record: Variant = predictor.call(balls, cue_ball, strike)
	if record == null or bool(record.strike_outcome().get("miscue", false)):
		return {}
	for entry: Variant in record.events():
		var event: Dictionary = entry
		var kind := String(event["kind"])
		var a := int(event["a"])
		var b := int(event["b"])
		var hit := -1
		if kind == "ball_ball" and (a == cue_ball or b == cue_ball):
			hit = b if a == cue_ball else a
		elif not (a == cue_ball and (kind == "ball_line" or kind == "ball_arc" or kind == "pocket")):
			continue
		var state: PackedFloat64Array = record.state_at(float(event["t"]))
		var stride := ShotPlayback.STRIDE
		var at := Vector2(state[cue_ball * stride], state[cue_ball * stride + 1])
		var result := {"kind": "ball" if hit >= 0 else "cushion", "ball": hit, "at": at,
				"bearing": (at - start).angle(), "direction": 0.0}
		if hit >= 0:
			result["direction"] = Vector2(state[hit * stride + 2], state[hit * stride + 3]).angle()
		return result
	return {}
