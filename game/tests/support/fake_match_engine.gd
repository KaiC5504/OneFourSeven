class_name FakeMatchEngine
extends RefCounted
## OfsMatch's methods with scripted answers, for tests of the match flow that must not depend on
## the rules. A test sets `frame_view`, `match_view` and `ball_list`, queues what judge and decide
## answer next, and reads `calls` for what was asked of the engine.

## Every call made, in order: [method name, args...].
var calls: Array[Array] = []
var ready := true
var started := false
var over := false
var frame_view := {}
var match_view := {}
var ball_list: Array = []
var cue_ball_place := 0
## Answers judge() hands out, first in first out; each is the whole Dictionary judge returns
## ({ok, judgement, event, frame}). The frame of each answer becomes frame_view.
var judgements: Array[Dictionary] = []
## The same for decide(): {ok, calls, event, frame}.
var decisions: Array[Dictionary] = []
## What standin_shot returns next ({ok, places_cue, cue, nominated, strike, why, us}).
var standin_shots: Array[Dictionary] = []
var standin_choices: Array[String] = []
## When false, configure_opponent refuses.
var opponent_ready := true
## Level ids whose file configure_opponent refuses, as the engine does a malformed one.
var refused_levels: Array[String] = []
## What levels() answers; configure_opponent keeps only the ids it was given.
var level_list: Array = [
	{"id": "beginner", "name": "Beginner", "tier": 1, "maximum": false},
	{"id": "club", "name": "Club", "tier": 2, "maximum": false},
	{"id": "amateur", "name": "Amateur", "tier": 3, "maximum": false},
	{"id": "pro", "name": "Pro", "tier": 4, "maximum": false},
	{"id": "world", "name": "World class", "tier": 5, "maximum": false},
	{"id": "maximum", "name": "The 147", "tier": 0, "maximum": true},
]
var exhibition_names: Array[String] = ["open 1"]
## What opponent_take returns next, in standin_shot's form plus "level", "meant" and "think".
var opponent_shots: Array[Dictionary] = []
var opponent_choices: Array[String] = []
## How many times opponent_take answers {} (still thinking) before it hands the stroke over.
var think_polls := 0
var _thinking := false
var _polls_left := 0
## When false, place_cue refuses.
var cue_legal := true
var error := ""
var _left_out: Array = []


func configure(_physics: Object, _referee_json: String, _standin_json: String) -> bool:
	calls.append(["configure"])
	return ready


func is_ready() -> bool:
	return ready


func start(config: Dictionary, match_id: String) -> Dictionary:
	calls.append(["start", config, match_id])
	if not ready:
		return {"ok": false, "error": "not ready"}
	started = true
	over = false
	return {"ok": true, "error": "", "match": match_state()}


func in_progress() -> bool:
	return started and not over


func abandon() -> void:
	calls.append(["abandon"])
	started = false


func match_state() -> Dictionary:
	var out := match_view.duplicate(true)
	out["frame"] = frame()
	return out


func frame() -> Dictionary:
	return frame_view.duplicate(true)


func balls() -> Array:
	return ball_list.duplicate(true)


func cue_ball() -> int:
	return cue_ball_place


func place_cue(x: float, y: float) -> Dictionary:
	calls.append(["place_cue", x, y])
	if not cue_legal:
		return {"ok": false, "error": "not a legal place", "x": x, "y": y}
	if cue_ball_place >= 0 and cue_ball_place < ball_list.size():
		ball_list[cue_ball_place]["x"] = x
		ball_list[cue_ball_place]["y"] = y
	return {"ok": true, "error": "", "x": x, "y": y}


func nearest_cue_placement(x: float, y: float) -> Dictionary:
	return {"x": x, "y": y}


func judge(record: Object, input: Dictionary, context: Dictionary) -> Dictionary:
	calls.append(["judge", record, input, context])
	if judgements.is_empty():
		return {"ok": false, "error": "no judgement queued"}
	return _take(judgements)


func decide(kind: String, player: int) -> Dictionary:
	calls.append(["decide", kind, player])
	if decisions.is_empty():
		return {"ok": false, "error": "no decision queued"}
	return _take(decisions)


func next_frame() -> Dictionary:
	calls.append(["next_frame"])
	return {"ok": true, "error": "", "frame": frame()}


func standin_shot(seed: int, context: Dictionary) -> Dictionary:
	calls.append(["standin_shot", seed, context])
	if standin_shots.is_empty():
		return {"ok": false, "error": "no stand-in shot queued"}
	return standin_shots.pop_front()


func standin_choice(player: int, seed: int) -> String:
	calls.append(["standin_choice", player, seed])
	if standin_choices.is_empty():
		return ""
	return standin_choices.pop_front()


func configure_opponent(levels: Dictionary, _exhibitions_json: String) -> bool:
	calls.append(["configure_opponent", levels.keys()])
	for id: Variant in levels:
		if refused_levels.has(str(id)):
			error = "%s: aim_sigma_rad must be positive" % id
			return false
	if not opponent_ready:
		return false
	# As the engine, each call replaces the levels: one left out before can come back.
	var known := level_list + _left_out
	level_list = known.filter(func(level: Dictionary) -> bool: return levels.has(level["id"]))
	_left_out = known.filter(func(level: Dictionary) -> bool: return not levels.has(level["id"]))
	# By tier with the 147 (tier 0) last, as OfsMatch.levels().
	level_list.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return (int(a["tier"]) if int(a["tier"]) > 0 else 1000) < (int(b["tier"]) if int(b["tier"]) > 0 else 1000))
	return true


func levels() -> Array:
	return level_list.duplicate(true)


func exhibitions() -> PackedStringArray:
	return PackedStringArray(exhibition_names)


func opponent_think(level: String, seed: int, context: Dictionary, budget: Dictionary) -> bool:
	calls.append(["opponent_think", level, seed, context, budget])
	if opponent_shots.is_empty() or _thinking:
		error = "no level stroke queued" if opponent_shots.is_empty() else "still thinking"
		return false
	_thinking = true
	_polls_left = think_polls
	return true


func opponent_thinking() -> bool:
	return _thinking


func opponent_take() -> Dictionary:
	if not _thinking:
		return {}
	if _polls_left > 0:
		_polls_left -= 1
		return {}
	calls.append(["opponent_take"])
	_thinking = false
	return opponent_shots.pop_front()


func opponent_cancel(why: String) -> void:
	calls.append(["opponent_cancel", why])
	_thinking = false


func opponent_choice(level: String, player: int, seed: int) -> String:
	calls.append(["opponent_choice", level, player, seed])
	if opponent_choices.is_empty():
		return ""
	return opponent_choices.pop_front()


func last_error() -> String:
	return error


## The methods called so far, by name.
func called() -> Array[String]:
	var out: Array[String] = []
	for call in calls:
		out.append(String(call[0]))
	return out


func _take(queue: Array[Dictionary]) -> Dictionary:
	var answer: Dictionary = queue.pop_front()
	if answer.has("frame"):
		frame_view = (answer["frame"] as Dictionary).duplicate(true)
	if answer.has("balls"):
		ball_list = (answer["balls"] as Array).duplicate(true)
	if String(answer.get("event", "")) == "match_ended":
		over = true
	return answer
