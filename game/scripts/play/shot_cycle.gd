class_name ShotCycle
extends RefCounted
## The shot cycle (spec 8.5), as in the mobile game KaiC plays: what the camera does from the strike
## until it swoops back behind the cue ball, and the running break.
##
## plan() reads the simulated shot before it plays. The camera stays where it is behind the cue ball
## until CUT_T, then cuts: when an object ball drops within POCKET_CAM_MAX_T, to a camera behind
## that pocket, held until HOLD_AFTER_DROP_S after the drop and at least MIN_POCKET_S, and from
## there to the full-table view; otherwise straight to the full-table view. A ball played at a
## pocket that stays out (attempt()) gets the same pocket camera, timed from when it is nearest the
## pocket, so the cut doesn't give away whether the pot goes in (KaiC). A shot whose cue ball is
## the thing to watch keeps to the full table instead (journey(), and every practice shot): the
## pocket camera would hide the cannon it was played for; so does one that leaves an excellent
## position (excellent()), which the position is worth watching for. The full-table view holds, still,
## until the balls stop and at least MIN_TV_HOLD_S; main.gd then turns the aim and releases the
## rig, whose swoop back behind the cue ball is the cycle's only camera move. The times are from
## KaiC's recording of the reference game.
##
## Times are the cycle's: shot playback time while the shot plays, carried on in real time after
## it ends (a pocket hold can outlast the last ball).

const POCKET_CAM_MAX_T := 4.0
## Just after the tip has met the cue ball: the stroke is seen, then the cut.
const CUT_T := 0.1
## A ball that drops sooner than CUT_T after the strike is still seen going in: the cut to its
## pocket comes this long before the drop.
const CUT_LEAD_S := 0.05
const HOLD_AFTER_DROP_S := 0.7
const MIN_POCKET_S := 1.0
const MIN_TV_HOLD_S := 0.8
## The ball's way into the pocket is taken from where it was this long before the drop, or where it
## was first hit if that was later.
const APPROACH_S := 0.5
## How many places along the watched ball's run, from the cut to the drop, the pocket camera is
## asked to keep in its frame.
const TRACK_POINTS := 8
## A shot that pots nothing was played at a pocket when the struck ball comes this near the
## pocket's drop circle (a ball's centre meeting the cushion two balls short of a corner's jaw is
## about 0.16 m from it), and this much nearer than it started, so a ball knocked away from a
## pocket it lay beside doesn't count.
const ATTEMPT_NEAR_M := 0.2
const ATTEMPT_APPROACH_M := 0.05
## Past this far out again the ball has left the pocket; coming back later is not the attempt.
const ATTEMPT_LEFT_M := 0.3
const ATTEMPT_STEP_S := 1.0 / 60.0
## The cue ball's run is the thing to watch when, after its first ball, it takes this many
## cushions, or goes on into another ball off a cushion or after this much of the table.
const JOURNEY_CUSHIONS := 3
const JOURNEY_CANNON_M := 0.4
const JOURNEY_APART_M := 0.3
const JOURNEY_REASON := "cue ball's journey"
## A shot at a pocket that leaves an excellent position is watched whole from the full table too
## (docs/opponent/README.md, "The game, stage 2"): the cue ball ran this far after its first ball
## and met a cushion, the position left reads this share or more of the most the ball on could be
## worth, and beats by POSITION_BEAT of that most what the same pot struck plain (centre ball, the
## same pace) would have left: the shot was played for position. Over 655 simulated shots at a
## pocket the share alone passed one in two, soft plain pots as often as any (their median share
## 0.87); with the plain shot to beat, one in eight and no plain shot. Starting values, KaiC's to
## judge.
const POSITION_RUN_M := 1.0
const POSITION_SHARE := 0.80
const POSITION_BEAT := 0.03
const POSITION_REASON := "position"
## The reader's chance has no cap below 1 (its error is erf alone), so the most a position can be
## worth is the best value on plus what keeping the break going is worth (NextTarget.BREAK_POINTS).
const POSITION_CHANCE_CAP := 1.0
const COLOURS: Array[String] = ["yellow", "green", "brown", "blue", "pink", "black"]
const VALUES := {"red": 1, "yellow": 2, "green": 3, "brown": 4, "blue": 5, "pink": 6, "black": 7}
## Events in which a ball meets something; transitions and nap don't move it off its line.
const CONTACT_KINDS: Array[String] = ["strike", "ball_ball", "ball_line", "ball_arc"]

enum Phase { IDLE, STILL, POCKET, TV }

## The running break, and the kind ("red" or "colour") of the last object ball potted in it, or "".
var break_total := 0
var last_potted := ""
## What the break counter shows: break_total plus this shot's pots so far.
var shown_break := 0

var _rig: CameraRig
var _plan := {}
var _pocket_pose := Transform3D.IDENTITY
var _phase := Phase.IDLE
## Cycle times: the first cut, the cut from the pocket to the full-table view, and when the
## full-table view came on.
var _cut_at := 0.0
var _leave_at := 0.0
var _tv_at := INF


func _init(rig: CameraRig = null) -> void:
	_rig = rig


## The camera sequence and the pots of a shot about to play. balls and cue_ball are the table it
## starts from (the state's ball order); pockets as OfsPhysics.table_shape() gives them. Returns
## {"sequence": "pocket" or "broadcast", "reason" ("" for the pocket camera on a pot and "near miss"
## on an attempt that stays out, else "no pot", "cue ball only", "late pot", "setting off",
## JOURNEY_REASON, watch_table's own, or "no pocket" with "problem" saying why), "pots": [{index,
## ball, label, pocket, t, value}] the object
## balls in drop order, "cue_potted"}, and for the ball the sequence is about (the first object
## ball to drop, else the attempt's, else the cue ball's drop): "ball" (label), "index", "pocket",
## "drop_t" (or "near_t" for an attempt, when it is nearest the pocket), "from" (where it came in
## from, core metres), "track" (where it is from the cut to then, latest first), "drop_centre",
## "middle".
## watch_table, when not empty, is why a shot at a pocket is watched from the full table whatever
## the cue ball does: "practice", or "watching" (the person watches the opponent's turn from above
## or walking round, CameraRig.watch()).
## reading, when valid, is what a position is worth to the player at it: (balls, cue_ball, on: the
## rules' form) -> float, negative when it can't be read. With it a shot at a pocket that leaves an
## excellent position (excellent()) is watched from the full table, reason POSITION_REASON. rules
## is the match's frame as the stroke starts ({on, free_ball}, excellent()'s), {} on the free table;
## plain excellent()'s.
static func plan(playback: ShotPlayback, balls: Array[Dictionary], cue_ball: int, pocket_camera: bool,
		pockets: Array, watch_table: String = "", reading: Callable = Callable(), rules: Dictionary = {},
		plain: Callable = Callable()) -> Dictionary:
	var pots: Array[Dictionary] = []
	var cue_drop := {}
	for e: Dictionary in playback.events():
		if String(e.get("kind", "")) != "pocket":
			continue
		var index := int(e.get("a", -1))
		var drop := {"index": index, "pocket": int(e.get("body", -1)), "t": float(e.get("t", 0.0))}
		if index == cue_ball:
			if cue_drop.is_empty():
				cue_drop = drop
			continue
		var ball: Dictionary = balls[index] if index >= 0 and index < balls.size() else {}
		var kind := String(ball.get("ball", ""))
		drop["ball"] = kind
		drop["label"] = label_of(ball) if not ball.is_empty() else "ball %d" % index
		drop["value"] = int(VALUES.get(kind, 0))
		pots.append(drop)
	var out := {"sequence": "broadcast", "reason": "", "pots": pots, "cue_potted": not cue_drop.is_empty()}
	var about := {}
	var tried := attempt(playback, cue_ball, pockets) if pots.is_empty() and pocket_camera else {}
	if not pots.is_empty():
		about = pots[0]
		if float(about["t"]) > POCKET_CAM_MAX_T:
			out["reason"] = "late pot"
		elif not pocket_camera:
			out["reason"] = "setting off"
		else:
			out["sequence"] = "pocket"
	elif not tried.is_empty():
		about = tried
		var at: Dictionary = balls[int(tried["index"])] if int(tried["index"]) < balls.size() else {}
		about["label"] = label_of(at) if not at.is_empty() else "ball %d" % int(tried["index"])
		out["sequence"] = "pocket"
		out["reason"] = "near miss"
	elif not cue_drop.is_empty():
		about = cue_drop.duplicate()
		about["label"] = "cue"
		out["reason"] = "cue ball only"
	else:
		out["reason"] = "no pot"
	# Decided the same for a pot and for an attempt that stays out, so the view gives neither away.
	if out["sequence"] == "pocket":
		var why := watch_table if not watch_table.is_empty() else (JOURNEY_REASON if journey(playback, cue_ball) else "")
		if why.is_empty() and reading.is_valid() and excellent(playback, balls, cue_ball, int(about["index"]), reading, rules, plain):
			why = POSITION_REASON
		if not why.is_empty():
			out["sequence"] = "broadcast"
			out["reason"] = why
	if about.is_empty():
		return out
	var index := int(about["index"])
	var drop_t := float(about["t"])
	out["ball"] = about["label"]
	out["index"] = index
	out["pocket"] = about["pocket"]
	out["drop_t" if tried.is_empty() else "near_t"] = drop_t
	var pocket: Dictionary = {}
	if int(about["pocket"]) >= 0 and int(about["pocket"]) < pockets.size():
		pocket = pockets[int(about["pocket"])]
	if pocket.is_empty() or not pocket.has("drop_centre"):
		if out["sequence"] == "pocket":
			out["sequence"] = "broadcast"
			out["reason"] = "no pocket"
			out["problem"] = "%s dropped into pocket %d, not one of the table shape's %d" % [about["label"],
					int(about["pocket"]), pockets.size()]
		return out
	var centre: Array = pocket["drop_centre"]
	out["drop_centre"] = Vector2(float(centre[0]), float(centre[1]))
	out["middle"] = not bool(pocket.get("corner", true))
	out["from"] = approach_from(playback, index, drop_t)
	out["track"] = track_of(playback, index, cut_time(out), drop_t)
	return out


## The pocket a shot was played at: the first ball the cue ball hits, before it meets another ball,
## coming within ATTEMPT_NEAR_M of a pocket's drop circle and ATTEMPT_APPROACH_M nearer than it
## started, by POCKET_CAM_MAX_T (later, like a late pot, is watched from the full table). {} when
## there is none, else {"index", "pocket", "t"}: the ball, the first such pocket, and when the ball
## is nearest it on that visit.
static func attempt(playback: ShotPlayback, cue_ball: int, pockets: Array) -> Dictionary:
	var target := -1
	var from_t := 0.0
	var to_t := playback.duration()
	for e: Dictionary in playback.events():
		if String(e.get("kind", "")) != "ball_ball":
			continue
		var a := int(e.get("a", -1))
		var b := int(e.get("b", -1))
		if target < 0:
			if a == cue_ball or b == cue_ball:
				target = b if a == cue_ball else a
				from_t = float(e.get("t", 0.0))
		elif a == target or b == target:
			to_t = float(e.get("t", 0.0))
			break
	if target < 0:
		return {}
	var centres := PackedVector2Array()
	var radii := PackedFloat64Array()
	for pocket: Variant in pockets:
		var centre: Variant = (pocket as Dictionary).get("drop_centre") if pocket is Dictionary else null
		var usable := centre is Array and (centre as Array).size() >= 2
		centres.append(Vector2(float(centre[0]), float(centre[1])) if usable else Vector2(INF, INF))
		radii.append(float((pocket as Dictionary).get("drop_radius", 0.0)) if usable else 0.0)
	var count := centres.size()
	var started := PackedFloat64Array()
	var nearest := PackedFloat64Array()
	var nearest_t := PackedFloat64Array()
	var left := PackedByteArray()
	started.resize(count)
	nearest.resize(count)
	nearest_t.resize(count)
	left.resize(count)
	nearest.fill(INF)
	# One step past the limit, so a ball still closing in at the limit reads as late.
	to_t = minf(to_t, POCKET_CAM_MAX_T + 2.0 * ATTEMPT_STEP_S)
	var o := target * ShotPlayback.STRIDE
	var steps := maxi(ceili((to_t - from_t) / ATTEMPT_STEP_S), 1)
	for k in steps + 1:
		var t := minf(from_t + k * ATTEMPT_STEP_S, to_t)
		var state := playback.state_at(t)
		if o + ShotPlayback.Y >= state.size():
			return {}
		var at := Vector2(state[o + ShotPlayback.X], state[o + ShotPlayback.Y])
		for p in count:
			var gap := at.distance_to(centres[p]) - radii[p]
			if k == 0:
				started[p] = gap
			if left[p] != 0:
				continue
			if gap <= ATTEMPT_NEAR_M:
				if gap < nearest[p]:
					nearest[p] = gap
					nearest_t[p] = t
			elif gap > ATTEMPT_LEFT_M and nearest[p] < INF:
				left[p] = 1
	var best := -1
	for p in count:
		var played_at := nearest[p] < INF and started[p] - nearest[p] >= ATTEMPT_APPROACH_M and nearest_t[p] <= POCKET_CAM_MAX_T
		if played_at and (best < 0 or nearest_t[p] < nearest_t[best]):
			best = p
	return {} if best < 0 else {"index": target, "pocket": best, "t": nearest_t[best]}


## Whether the cue ball's run after its first ball is the thing to watch: it goes on into another
## ball off a cushion or after JOURNEY_CANNON_M of travel, or takes JOURNEY_CUSHIONS cushions.
## A cushion met again within JOURNEY_APART_M (running along it, or rattling in a jaw) is the same
## cushion. False once the cue ball is potted.
static func journey(playback: ShotPlayback, cue_ball: int) -> bool:
	var first := -1
	var first_at := Vector2.ZERO
	var cushions := 0
	var cushion_at := Vector2.ZERO
	var o := cue_ball * ShotPlayback.STRIDE
	for e: Dictionary in playback.events():
		var kind := String(e.get("kind", ""))
		var a := int(e.get("a", -1))
		var b := int(e.get("b", -1))
		var cannon := kind == "ball_ball" and (a == cue_ball or b == cue_ball)
		var cushion := a == cue_ball and (kind == "ball_line" or kind == "ball_arc")
		if a == cue_ball and kind == "pocket":
			return false
		if not cannon and not (cushion and first >= 0):
			continue
		var state := playback.state_at(float(e.get("t", 0.0)))
		if o + ShotPlayback.Y >= state.size():
			return false
		var at := Vector2(state[o + ShotPlayback.X], state[o + ShotPlayback.Y])
		if cushion:
			if cushions == 0 or at.distance_to(cushion_at) >= JOURNEY_APART_M:
				cushions += 1
				cushion_at = at
			if cushions >= JOURNEY_CUSHIONS:
				return true
			continue
		var other := b if a == cue_ball else a
		if first < 0:
			first = other
			first_at = at
		elif other != first and (cushions > 0 or at.distance_to(first_at) >= JOURNEY_CANNON_M):
			return true
	return false


## Whether a shot played at a pocket with ball `played` leaves an excellent position: the cue ball
## runs POSITION_RUN_M or more after its first ball and meets a cushion on the way, and the
## position, read as if `played` had gone in (it and every ball that dropped off the table, the
## ball on that would follow), is worth POSITION_SHARE or more of the most that ball on could be,
## and POSITION_BEAT of that most more than the same pot struck plain would leave. Judged the same
## whether `played` went in or stayed out. reading as plan() takes it; rules, in a match, the frame
## as the stroke starts ({on, free_ball}): the ball on that follows and a colour put back on its
## spot are then the rules' (left_as_potted()). plain, when valid, is (played) -> the worth the
## plain stroke leaves, read the same way (plain_worth()), negative when it can't be said; without
## it there is nothing to beat.
static func excellent(playback: ShotPlayback, balls: Array[Dictionary], cue_ball: int, played: int,
		reading: Callable, rules: Dictionary = {}, plain: Callable = Callable()) -> bool:
	var run := cue_run(playback, cue_ball, POSITION_RUN_M)
	if float(run["metres"]) < POSITION_RUN_M or not bool(run["cushion"]):
		return false
	var left := left_as_potted(playback, balls, cue_ball, played, rules)
	if left.is_empty():
		return false
	var on: Dictionary = left["on"]
	var most := most_worth(left["balls"], on)
	if most <= 0.0:
		return false
	var worth: Variant = reading.call(left["balls"], int(left["cue_ball"]), on)
	if not (worth is float or worth is int) or float(worth) < POSITION_SHARE * most:
		return false
	if not plain.is_valid():
		return true
	var plain_left: Variant = plain.call(played)
	if not (plain_left is float or plain_left is int) or float(plain_left) < 0.0:
		return true
	return float(worth) >= float(plain_left) + POSITION_BEAT * most


## What the stroke `strike` would leave struck plain, the centre of the cue ball at the same pace,
## read as excellent() reads the stroke played: predict is (balls, cue_ball, strike) ->
## OfsShotRecord or null, elevation_min (balls, cue_ball, strike) -> the lowest the cue may be.
## Negative when it can't be simulated or read.
static func plain_worth(predict: Callable, elevation_min: Callable, balls: Array[Dictionary], cue_ball: int,
		strike: Dictionary, played: int, reading: Callable, rules: Dictionary = {}) -> float:
	var plain := strike.duplicate()
	plain["tip_x"] = 0.0
	plain["tip_y"] = 0.0
	var lowest: Variant = elevation_min.call(balls, cue_ball, plain) if elevation_min.is_valid() else 0.0
	plain["elevation_rad"] = maxf(0.0, float(lowest)) if lowest is float or lowest is int else 0.0
	var record: Variant = predict.call(balls, cue_ball, plain)
	if not record is OfsShotRecord:
		return -1.0
	var playback := ShotPlayback.from_record(record)
	var left := left_as_potted(playback, balls, cue_ball, played, rules)
	if left.is_empty():
		return -1.0
	var worth: Variant = reading.call(left["balls"], int(left["cue_ball"]), left["on"])
	return float(worth) if worth is float or worth is int else -1.0


## How far the cue ball runs after it first meets a ball, and whether it meets a cushion in that
## run: {"metres", "cushion"}. {"metres": 0.0, "cushion": false} when it meets no ball. Measured
## only as far as `need` when a cushion is met, and not at all when none is: excellent() asks no
## more.
static func cue_run(playback: ShotPlayback, cue_ball: int, need: float = INF) -> Dictionary:
	var from_t := -1.0
	var cushion := false
	# When it came to rest for good: nothing after moves it.
	var rest_t := -1.0
	for e: Dictionary in playback.events():
		var kind := String(e.get("kind", ""))
		var a := int(e.get("a", -1))
		var b := int(e.get("b", -1))
		if a == cue_ball or b == cue_ball:
			rest_t = float(e.get("t", 0.0)) if kind == "transition" and e.get("to") == "stationary" else -1.0
		if from_t < 0.0:
			if kind == "ball_ball" and (a == cue_ball or b == cue_ball):
				from_t = float(e.get("t", 0.0))
		elif a == cue_ball and (kind == "ball_line" or kind == "ball_arc"):
			cushion = true
	if from_t < 0.0 or not cushion and need < INF:
		return {"metres": 0.0, "cushion": cushion}
	var o := cue_ball * ShotPlayback.STRIDE
	var end_t := playback.duration() if rest_t < from_t else minf(playback.duration(), rest_t)
	var steps := maxi(ceili((end_t - from_t) / ATTEMPT_STEP_S), 1)
	var metres := 0.0
	var last := Vector2.INF
	for k in steps + 1:
		var state := playback.state_at(minf(from_t + k * ATTEMPT_STEP_S, end_t))
		if o + ShotPlayback.Y >= state.size():
			break
		var at := Vector2(state[o + ShotPlayback.X], state[o + ShotPlayback.Y])
		if last != Vector2.INF:
			metres += at.distance_to(last)
		last = at
		if metres >= need:
			break
	return {"metres": metres, "cushion": cushion}


## The table as the shot leaves it with `played` potted too: {"balls" (where each ball stops,
## those that dropped and `played` left out, in the list's order), "cue_ball" (its place there),
## "on" (the rules' form of the ball on that follows)}. {} when the cue ball dropped or nothing
## would be on. With rules (plan()'s, in a match) the ball on is the rules' and a colour they
## would put back goes on its spot (on_by_rules()); without, the free table's order (on_after()).
static func left_as_potted(playback: ShotPlayback, balls: Array[Dictionary], cue_ball: int, played: int,
		rules: Dictionary = {}) -> Dictionary:
	var gone := {played: true}
	for e: Dictionary in playback.events():
		if String(e.get("kind", "")) == "pocket":
			gone[int(e.get("a", -1))] = true
	if gone.has(cue_ball) or played < 0 or played >= balls.size():
		return {}
	var state := playback.state_at(playback.duration())
	var left: Array[Dictionary] = []
	var cue := -1
	for i in balls.size():
		if gone.has(i):
			continue
		var o := i * ShotPlayback.STRIDE
		if o + ShotPlayback.Y >= state.size():
			return {}
		if i == cue_ball:
			cue = left.size()
		left.append({"ball": String(balls[i]["ball"]), "index": int(balls[i]["index"]), "x": state[o + ShotPlayback.X],
				"y": state[o + ShotPlayback.Y]})
	var name := String(balls[played].get("ball", ""))
	var on := {}
	if rules.is_empty():
		on = on_after(name, left)
	else:
		var after := on_by_rules(name, rules, left)
		if not String(after["respot"]).is_empty():
			var at: Variant = respot_place(String(after["respot"]), left)
			if at is Vector2:
				left.append({"ball": String(after["respot"]), "index": int(balls[played]["index"]), "x": (at as Vector2).x,
						"y": (at as Vector2).y})
		on = on_by_rules(name, rules, left)["on"]
	if on.is_empty() or cue < 0:
		return {}
	return {"balls": left, "cue_ball": cue, "on": on}


## As the rules have it once `played` (a ball's name) has gone in from a frame whose ball on was
## rules.on (the rules' form) with rules.free_ball: {"on": the ball on that follows, or {} with
## none, among `left`; "respot": the colour that goes back on its spot, or ""}. A free ball counts
## as the ball on and goes back; in the colours the ball on then stays on. A colour goes back while
## reds remain and when it follows the last red. Fouls are read as pots all the same: the view must
## not tell them apart.
static func on_by_rules(played: String, rules: Dictionary, left: Array[Dictionary]) -> Dictionary:
	var on_now: Variant = rules.get("on", {})
	var kind := String((on_now as Dictionary).get("kind", "")) if on_now is Dictionary else ""
	var reds := left.any(func(b: Dictionary) -> bool: return b.get("ball") == "red")
	var colour := COLOURS.has(played)
	if bool(rules.get("free_ball", false)) and colour:
		if kind == "red":
			return {"on": on_of("colour", left), "respot": played}
		return {"on": (on_now as Dictionary).duplicate() if kind == "colour" else on_of("lowest", left), "respot": played}
	if not colour:
		return {"on": on_of("colour", left), "respot": ""}
	if kind == "colour":
		return {"on": on_of("lowest", left), "respot": ""}
	return {"on": on_of("red" if reds else "lowest", left), "respot": played}


## Where the rules put a colour back: its own spot, or when that is taken the highest one free.
## null when every spot is taken.
static func respot_place(colour: String, balls: Array[Dictionary]) -> Variant:
	var clear := 2.0 * float(OfsTable.dimensions().get("ball_radius", 0.02625))
	var tries: Array[String] = [colour]
	for i in range(COLOURS.size() - 1, -1, -1):
		if COLOURS[i] != colour:
			tries.append(COLOURS[i])
	for name: String in tries:
		var spot: Dictionary = OfsTable.spot(name)
		if spot.is_empty():
			continue
		var at := Vector2(float(spot["x"]), float(spot["y"]))
		if balls.all(func(b: Dictionary) -> bool: return at.distance_to(Vector2(float(b["x"]), float(b["y"]))) >= clear):
			return at
	return null


## The ball on after `potted` (a ball's name) goes in, the rules' form: a colour after a red, a red
## after a colour while any remain, then the colours in order. {} with no ball left to be on.
static func on_after(potted: String, balls: Array[Dictionary]) -> Dictionary:
	if potted == "red":
		return on_of("colour", balls)
	return on_of("red" if balls.any(func(b: Dictionary) -> bool: return b.get("ball") == "red") else "lowest", balls)


## prefer's kind of ball as the rules' form of the ball on: "red", "colour" (a colour of choice)
## or "lowest" (the colour of least value on the table). {} when there is no such ball.
static func on_of(kind: String, balls: Array[Dictionary]) -> Dictionary:
	match kind:
		"red":
			return {"kind": "red"} if balls.any(func(b: Dictionary) -> bool: return b.get("ball") == "red") else {}
		"colour", "lowest":
			var lowest := ""
			for b: Dictionary in balls:
				var name := String(b.get("ball", ""))
				if COLOURS.has(name) and (lowest.is_empty() or COLOURS.find(name) < COLOURS.find(lowest)):
					lowest = name
			if lowest.is_empty():
				return {}
			return {"kind": "any_colour"} if kind == "colour" else {"kind": "colour", "colour": lowest}
	return {}


## The most a position with `on` (the rules' form) could be worth: a certain pot of the best ball
## on, and the break going on.
static func most_worth(balls: Array[Dictionary], on: Dictionary) -> float:
	var best := 0
	match String(on.get("kind", "")):
		"red":
			best = 1
		"colour":
			best = int(VALUES.get(String(on.get("colour", "")), 0))
		"any_colour":
			for b: Dictionary in balls:
				if COLOURS.has(String(b.get("ball", ""))):
					best = maxi(best, int(VALUES[String(b["ball"])]))
	return POSITION_CHANCE_CAP * (best + NextTarget.BREAK_POINTS) if best > 0 else 0.0


## Where ball `index` was APPROACH_S before dropping at drop_t, or when it was first hit if later.
static func approach_from(playback: ShotPlayback, index: int, drop_t: float) -> Vector2:
	var from_t := maxf(drop_t - APPROACH_S, 0.0)
	for e: Dictionary in playback.events():
		var t := float(e.get("t", 0.0))
		if t >= drop_t:
			break
		if CONTACT_KINDS.has(String(e.get("kind", ""))) and (int(e.get("a", -1)) == index or int(e.get("b", -1)) == index):
			from_t = maxf(from_t, t)
			break
	var state := playback.state_at(from_t)
	var o := index * ShotPlayback.STRIDE
	if o + ShotPlayback.Y >= state.size():
		return Vector2(NAN, NAN)
	return Vector2(state[o + ShotPlayback.X], state[o + ShotPlayback.Y])


## Where ball `index` is at TRACK_POINTS times from from_t to to_t, the latest first.
static func track_of(playback: ShotPlayback, index: int, from_t: float, to_t: float) -> PackedVector2Array:
	var track := PackedVector2Array()
	var o := index * ShotPlayback.STRIDE
	for k in TRACK_POINTS:
		var state := playback.state_at(lerpf(to_t, from_t, float(k) / (TRACK_POINTS - 1)))
		if o + ShotPlayback.Y < state.size():
			track.append(Vector2(state[o + ShotPlayback.X], state[o + ShotPlayback.Y]))
	return track


## When the camera first cuts on a shot's plan: CUT_T, or sooner to catch a drop that comes before
## it.
static func cut_time(shot_plan: Dictionary) -> float:
	if shot_plan.get("sequence") != "pocket":
		return CUT_T
	return clampf(watched_until(shot_plan, CUT_T) - CUT_LEAD_S, 0.0, CUT_T)


## When the pocket camera of a "pocket" plan gives way to the full-table view.
static func pocket_leave_time(shot_plan: Dictionary) -> float:
	return maxf(watched_until(shot_plan, 0.0) + HOLD_AFTER_DROP_S, cut_time(shot_plan) + MIN_POCKET_S)


## When the ball a plan is about drops, or on an attempt that stays out is nearest its pocket.
static func watched_until(shot_plan: Dictionary, otherwise: float) -> float:
	return float(shot_plan.get("drop_t", shot_plan.get("near_t", otherwise)))


static func label_of(ball: Dictionary) -> String:
	return "red %d" % int(ball.get("index", 0)) if ball.get("ball") == "red" else String(ball.get("ball", ""))


## The pocket camera's pose as shot.camera logs it: [x, y, z, pitch_rad, yaw_rad], Godot units.
static func pose_fields(pose: Transform3D) -> Array:
	var euler := pose.basis.get_euler()
	return [pose.origin.x, pose.origin.y, pose.origin.z, euler.x, euler.y]


## Starts the camera on a shot's plan (plan()); pocket_pose is the pocket camera for a "pocket"
## sequence.
func start(shot_plan: Dictionary, pocket_pose := Transform3D.IDENTITY) -> void:
	_plan = shot_plan
	_pocket_pose = pocket_pose
	shown_break = break_total
	_phase = Phase.STILL
	_cut_at = cut_time(shot_plan)
	_leave_at = pocket_leave_time(shot_plan)
	_tv_at = INF
	if _rig != null:
		_rig.hold(true)


## Cuts the camera on to where the cycle is at time t.
func update(t: float) -> void:
	if _phase == Phase.STILL and t >= _cut_at:
		if _plan.get("sequence") == "pocket":
			_phase = Phase.POCKET
			if _rig != null:
				_rig.show_pocket(_pocket_pose)
		else:
			_full_table(t)
	if _phase == Phase.POCKET and t >= _leave_at:
		_full_table(t)


## A drag before the full-table view: cut to it now, to walk round. True when the camera was still
## or at the pocket.
func walk_round(t: float) -> bool:
	if _phase != Phase.STILL and _phase != Phase.POCKET:
		return false
	_full_table(t)
	return true


func _full_table(t: float) -> void:
	_phase = Phase.TV
	_tv_at = t
	if _rig != null:
		_rig.show_broadcast()


## True once the full-table view has been on for MIN_TV_HOLD_S, or the cycle was cancelled; the
## balls' own stop is the caller's to wait for.
func settle_ready(t: float) -> bool:
	return _phase == Phase.IDLE or _phase == Phase.TV and t >= _tv_at + MIN_TV_HOLD_S


## A skip: no more of the sequence; the settle can come at once.
func cancel() -> void:
	_phase = Phase.IDLE


func phase() -> Phase:
	return _phase


## A potted object ball's value reached the counter (its drop played).
func count(value: int) -> void:
	shown_break += value


## The shot's result for the break, once its balls have settled: {"potted" (every label, the cue
## ball's too, in drop order), "points", "break"}. A shot that pots no object ball, or the cue
## ball, ends the break.
func end_shot() -> Dictionary:
	var potted: Array[String] = []
	var points := 0
	var last := ""
	for pot: Dictionary in _plan.get("pots", []):
		potted.append(String(pot["label"]))
		points += int(pot["value"])
		last = "red" if pot["ball"] == "red" else "colour"
	if _plan.get("cue_potted", false):
		potted.append("cue")
	if points == 0 or _plan.get("cue_potted", false) or last.is_empty():
		reset_break()
	else:
		break_total += points
		last_potted = last
		shown_break = break_total
	_phase = Phase.IDLE
	return {"potted": potted, "points": points, "break": break_total}


func reset_break() -> void:
	break_total = 0
	last_potted = ""
	shown_break = 0


## What NextTarget.choose should keep to next, as the game's order has it: a colour after a red, a
## red after a colour while reds remain and to start a break, and once the reds are gone the
## colours in order, lowest first.
func prefer(balls: Array[Dictionary]) -> String:
	return _prefer_from(last_potted, balls)


## What prefer() will say once the shot `shot_plan` is about has ended (end_shot) on `balls_after`:
## known at the strike, as the simulator knows every pot.
static func prefer_after(shot_plan: Dictionary, balls_after: Array[Dictionary]) -> String:
	var last := ""
	for pot: Dictionary in shot_plan.get("pots", []):
		last = "red" if pot["ball"] == "red" else "colour"
	if bool(shot_plan.get("cue_potted", false)):
		last = ""
	return _prefer_from(last, balls_after)


static func _prefer_from(last: String, balls: Array[Dictionary]) -> String:
	if last == "red":
		return "colour"
	for b: Dictionary in balls:
		if b.get("ball") == "red":
			return "red"
	return "lowest"
