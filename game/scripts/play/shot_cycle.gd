class_name ShotCycle
extends RefCounted
## The shot cycle (spec 8.5), as in the mobile game KaiC plays: what the camera does from the strike
## until it swoops back behind the cue ball, and the running break.
##
## plan() reads the simulated shot before it plays. When an object ball drops within
## POCKET_CAM_MAX_T, the camera stays behind the cue ball until CUT_T, cuts to a camera behind that
## pocket, holds until HOLD_AFTER_DROP_S after the drop, then rises to the TV view over RISE_S.
## Otherwise it rises to the TV view at the strike (CameraRig.BLEND_S). Either way the TV view holds
## until the balls stop and at least MIN_TV_HOLD_S; main.gd then turns the aim and releases the rig.
##
## Times are the cycle's: shot playback time while the shot plays, carried on in real time after
## it ends (a pocket hold or a rise can outlast the last ball).

const POCKET_CAM_MAX_T := 4.0
## Just after the tip has met the cue ball: the stroke is seen, then the pot.
const CUT_T := 0.2
const HOLD_AFTER_DROP_S := 0.7
const RISE_S := 1.0
const MIN_TV_HOLD_S := 0.4
## The ball's way into the pocket is taken from where it was this long before the drop, or where it
## was first hit if that was later.
const APPROACH_S := 0.5
const VALUES := {"red": 1, "yellow": 2, "green": 3, "brown": 4, "blue": 5, "pink": 6, "black": 7}
## Events in which a ball meets something; transitions and nap don't move it off its line.
const CONTACT_KINDS: Array[String] = ["strike", "ball_ball", "ball_line", "ball_arc"]

enum Phase { IDLE, STILL, POCKET, RISE, TV }

## The running break, and the kind ("red" or "colour") of the last object ball potted in it, or "".
var break_total := 0
var last_potted := ""
## What the break counter shows: break_total plus this shot's pots so far.
var shown_break := 0

var _rig: CameraRig
var _plan := {}
var _pocket_pose := Transform3D.IDENTITY
var _phase := Phase.IDLE
## When the TV view is (or will be) reached, cycle time.
var _tv_at := 0.0


func _init(rig: CameraRig = null) -> void:
	_rig = rig


## The camera sequence and the pots of a shot about to play. balls and cue_ball are the table it
## starts from (the state's ball order); pockets as OfsPhysics.table_shape() gives them. Returns
## {"sequence": "pocket" or "broadcast", "reason" ("" for pocket, else "no pot", "cue ball only",
## "late pot", "setting off", or "no pocket" with "problem" saying why), "pots": [{index, ball, label, pocket, t, value}] the object balls
## in drop order, "cue_potted"}, and for the drop the sequence is about (the first object ball's,
## else the cue ball's): "ball" (label), "index", "pocket", "drop_t", "from" (where it came in
## from, core metres), "drop_centre", "middle".
static func plan(playback: ShotPlayback, balls: Array[Dictionary], cue_ball: int, pocket_camera: bool,
		pockets: Array) -> Dictionary:
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
	if not pots.is_empty():
		about = pots[0]
		if float(about["t"]) > POCKET_CAM_MAX_T:
			out["reason"] = "late pot"
		elif not pocket_camera:
			out["reason"] = "setting off"
		else:
			out["sequence"] = "pocket"
	elif not cue_drop.is_empty():
		about = cue_drop.duplicate()
		about["label"] = "cue"
		out["reason"] = "cue ball only"
	else:
		out["reason"] = "no pot"
	if about.is_empty():
		return out
	var index := int(about["index"])
	var drop_t := float(about["t"])
	out["ball"] = about["label"]
	out["index"] = index
	out["pocket"] = about["pocket"]
	out["drop_t"] = drop_t
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
	return out


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
	if shot_plan.get("sequence") == "pocket":
		_phase = Phase.STILL
		_tv_at = INF
		if _rig != null:
			_rig.hold(true, true)
	else:
		_phase = Phase.RISE
		_tv_at = CameraRig.BLEND_S
		if _rig != null:
			_rig.hold(true)



## Moves the camera on to where the cycle is at time t.
func update(t: float) -> void:
	if _phase == Phase.STILL and t >= CUT_T:
		_phase = Phase.POCKET
		if _rig != null:
			_rig.show_pocket(_pocket_pose)
	if _phase == Phase.POCKET and t >= float(_plan.get("drop_t", 0.0)) + HOLD_AFTER_DROP_S:
		_rise(t)
	if _phase == Phase.RISE and t >= _tv_at:
		_phase = Phase.TV


## A drag before the TV view: up to it now, to walk round. True when the camera was still or at the
## pocket.
func walk_round(t: float) -> bool:
	if _phase != Phase.STILL and _phase != Phase.POCKET:
		return false
	_rise(t)
	return true


func _rise(t: float) -> void:
	_phase = Phase.RISE
	_tv_at = t + RISE_S
	if _rig != null:
		_rig.show_broadcast(RISE_S)


## True once the TV view has held MIN_TV_HOLD_S, or the cycle was cancelled; the balls' own stop is
## the caller's to wait for.
func settle_ready(t: float) -> bool:
	return _phase == Phase.IDLE or (_phase == Phase.TV or _phase == Phase.RISE) and t >= _tv_at + MIN_TV_HOLD_S


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


## What NextTarget.choose should prefer next: a colour after a red, a red after a colour while reds
## remain, and a red to start a break while there are any.
func prefer(balls: Array[Dictionary]) -> String:
	if last_potted == "red":
		return "colour"
	for b: Dictionary in balls:
		if b.get("ball") == "red":
			return "red"
	return "colour"
