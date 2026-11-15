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
## When false, place_cue refuses.
var cue_legal := true
var error := ""


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
