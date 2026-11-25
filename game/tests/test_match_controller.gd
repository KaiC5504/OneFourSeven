extends TestCase
## MatchController and OpponentDriver against a scripted engine: what is announced, in what order,
## and what is asked of the engine, for every way a match moves on. No rule is tested here.

const FRAME_VIEW := "res://data/rules/example_frame_view.json"
const JUDGEMENT := "res://data/rules/example_judgement.json"
const MATCH := "res://data/rules/example_match.json"
const COLOURS: Array[String] = ["yellow", "green", "brown", "blue", "pink", "black"]
## A colour of the striker's choice is on: one must be declared.
const CHOICE_OF_COLOUR := {"on": {"kind": "any_colour"}, "nominations": COLOURS}
const CONFIG := {"best_of": 3, "first_breaker": "you", "seed": 147}


## FakeMatchEngine, which records nothing of configure and never refuses next_frame.
class ScriptedEngine extends FakeMatchEngine:
	var configured: Array[Array] = []
	var next_frame_ok := true
	## What nearest_cue_placement answers; the place asked when null.
	var nearest: Variant = null

	func configure(physics: Object, referee_json: String, standin_json: String) -> bool:
		configured.append([physics, referee_json, standin_json])
		return super(physics, referee_json, standin_json)

	func next_frame() -> Dictionary:
		if not next_frame_ok:
			calls.append(["next_frame"])
			return {"ok": false, "error": "the frame is not over"}
		return super()

	func nearest_cue_placement(x: float, y: float) -> Dictionary:
		if nearest is Dictionary:
			return nearest
		return super(x, y)


## A controller that makes its own engine, as the game's does, from `template`.
class OwnEngine extends MatchController:
	var template: Object

	func _new_engine() -> Object:
		return template


var _events: Array[Array] = []
var _log_start := 0


func test_start_configures_the_match_and_announces_the_first_turn() -> void:
	var e := _engine(_turn(0))
	var c := _controller(e)
	_mark_log()
	check(not c.is_active(), "no match before start")
	check_eq(c.match_id(), "", "no match id before start")
	var version := c.version()
	check(c.start({"best_of": 5, "first_breaker": "computer", "seed": 147}), "start")
	var asked := _last_call(e, "start")
	check_eq(asked[1], {"best_of": 5, "first_breaker": 1, "seed": 147, "concede_any_time": [true, false], "person": 0},
			"the engine's config")
	check_eq(asked[2], c.match_id(), "the engine is given the match id")
	check(c.match_id().begins_with(Log.session() + "-m"), "match id is <session>-m<n>: " + c.match_id())
	check(c.is_active(), "active")
	check_eq(_names(), ["table_changed:frame", "frame_changed", "turn_started"], "start's signals")
	check_eq(_events[0], ["table_changed", &"frame", e.ball_list, 0], "the rack")
	check_eq(_events[1], ["frame_changed", e.frame_view], "the frame view")
	check_eq(_events[2], ["turn_started", 0, true], "the person to play")
	check_eq(c.balls, _typed(e.ball_list), "balls from the engine")
	check_eq(c.cue_ball, 0, "cue ball")
	check_eq(c.version(), version + 1, "version counts the new balls")
	check_eq(c.frame(), e.frame_view, "frame()")
	check_eq(c.match_state(), e.match_state(), "match_state()")
	check(c.person_to_play(), "the person may play")
	var turns := _logged("match.turn")
	check_eq(turns.size(), 1, "match.turn once")
	if turns.size() == 1:
		check_eq(turns[0]["waiting"], "shot", "waiting")
		check_eq(int(turns[0]["player"]), 0, "player")
		check_eq(turns[0]["person"], true, "person")
		check_eq(turns[0]["match_id"], c.match_id(), "match_id on the line")
		check_eq(int(turns[0]["frame"]), 2, "the frame number on the line")
	c.free()


func test_start_maps_who_breaks_and_makes_a_seed() -> void:
	var e := _engine(_turn(0))
	var c := _controller(e)
	var ids: Array[String] = []
	for first: String in ["random", "you", "computer"]:
		check(c.start({"best_of": 1, "first_breaker": first}), "start with " + first)
		var config: Dictionary = _last_call(e, "start")[1]
		check_eq(config["first_breaker"], {"random": -1, "you": 0, "computer": 1}[first], first)
		check(config["seed"] is int and int(config["seed"]) >= 0, "a seed is made when none is given")
		ids.append(c.match_id())
	check(ids[0] != ids[1] and ids[1] != ids[2], "each match has its own id")
	check_eq(_count(e, "abandon"), 2, "a match still on is left before the next starts")
	check(c.start({"best_of": 1}), "no first_breaker")
	check_eq(_last_call(e, "start")[1]["first_breaker"], -1, "random by default")

	_mark_log()
	var starts := _count(e, "start")
	var id := c.match_id()
	check(not c.start({"best_of": 1, "first_breaker": "me"}), "an unknown breaker is refused")
	check_eq(_count(e, "start"), starts, "and the engine is not asked")
	check(c.is_active() and c.match_id() == id, "the match in progress stays")
	check_eq(_errors_with("first_breaker").size(), 1, "logged")

	e.ready = false
	_events.clear()
	check(not c.start(CONFIG), "a start the engine refuses")
	check_eq(_names(), ["table_changed:ended"], "still leaves the match that was on, and says so")
	check(not c.is_active(), "no match")
	c.free()


func test_start_refused_by_the_engine() -> void:
	var e := _engine(_turn(0))
	e.ready = false
	e.error = "the referee is away"
	var c := _controller(e)
	_mark_log()
	check(not c.start(CONFIG), "start is false")
	check(not c.is_active(), "no match")
	check_eq(c.match_id(), "", "no match id")
	check_eq(_names(), [] as Array[String], "nothing announced")
	check_eq(c.harness_state()["active"], false, "harness: not active")
	check_eq(c.strike_blocked(), "inactive", "blocked: inactive")
	var errors := _errors_with("engine.start(")
	check_eq(errors.size(), 1, "app.error names the call")
	if errors.size() == 1:
		var message := String(errors[0]["message"])
		check(message.contains("\"best_of\":3") and message.contains("not ready"),
				"with its arguments and the error: " + message)
		check(message.contains("last_error: the referee is away"), "and last_error: " + message)
	c.free()


func test_legal_stroke_is_judged_and_the_turn_goes_on() -> void:
	var e := _engine(_turn(0))
	var c := _started(e)
	check_eq(c.strike_blocked(), "", "the person may strike")
	c.shot_started("s-1")
	check_eq(c.strike_blocked(), "playing", "blocked while the stroke is in play")
	check(not c.person_to_play(), "not to play while it is judged")
	check_eq(c.harness_state()["waiting"], "", "waiting for nobody")
	var after := _turn(0, {"scores": [25, 31], "break_points": 1})
	var moved := e.ball_list.duplicate(true)
	moved[0]["x"] = 0.25
	moved.remove_at(1)
	e.judgements.append(_judged(after, _legal(), {"balls": moved}))
	var version := c.version()
	_mark_log()
	var judgement := c.shot_finished(null)
	check_eq(judgement, _legal(), "the judgement is returned")
	check_eq(_last_call(e, "judge"), ["judge", null, {"nominated": ""}, {"shot_id": "s-1"}], "what judge was asked")
	check_eq(_names(), ["table_changed:shot", "frame_changed", "turn_started"], "signals after a legal stroke")
	check_eq(_events[0], ["table_changed", &"shot", moved, 0], "the balls as judged")
	check_eq(_events[1], ["frame_changed", after], "the frame after it")
	check_eq(_events[2], ["turn_started", 0, true], "the striker stays")
	check_eq(c.balls, _typed(moved), "balls")
	check_eq(c.version(), version + 1, "version")
	check_eq(c.strike_blocked(), "", "and may strike again")
	var turns := _logged("match.turn")
	check_eq(turns.size(), 1, "match.turn for the new wait")
	c.free()


func test_foul_gives_the_person_a_choice() -> void:
	var e := _engine(_turn(1))
	e.standin_shots.append(_standin_shot())
	var c := _controller(e)
	c.start(CONFIG)
	check_eq(_names(), ["table_changed:frame", "frame_changed", "turn_started"], "the opponent's turn")
	check_eq(_events[2], ["turn_started", 1, false], "not the person's")
	check_eq(c.strike_blocked(), "opponent", "blocked: opponent")
	check_eq(_count(e, "standin_shot"), 0, "the stand-in is not asked in the same call")
	_events.clear()
	await tree.process_frame
	check_eq(_names(), ["opponent_ready"], "its stroke arrives on the next frame")
	var shot: Dictionary = _events[0][1]
	check_eq(int(shot["player"]), 1, "with the player")
	check_eq(shot["strike"], _standin_shot()["strike"], "and the strike as the stand-in gave it")
	var asked := _last_call(e, "standin_shot")
	check(asked[1] is int, "a seed")
	check_eq(asked[2], {"shot_id": shot["shot_id"]}, "the stroke's id is the one the stand-in logged")
	_events.clear()

	c.shot_started(shot["shot_id"])
	var fouled := _json(FRAME_VIEW)
	# The concessions are the pause menu's, offered to a decider who may concede at any time.
	fouled["view"]["options"] = [["play", "play_again", "replace", "concede_frame", "concede_match"], []]
	var judgement := _json(JUDGEMENT)
	judgement.erase("post")
	e.judgements.append(_judged(fouled, judgement))
	_mark_log()
	c.shot_finished(null)
	check_eq(_names(), ["calls_made", "table_changed:shot", "frame_changed", "choice_needed"], "signals after the foul")
	check_eq(_events[0], ["calls_made", judgement["calls"], shot["shot_id"]], "the referee's calls with the shot id")
	check_eq(_events[3], ["choice_needed", ["play", "play_again", "replace"]], "the options offered, not the pause menu's")
	check(c.open_to_person("concede_frame") and c.open_to_person("concede_match"), "which stay open to the person")
	check_eq(c.strike_blocked(), "choice", "blocked: choice")
	check_eq(c.harness_state()["waiting"], "choice", "waiting: choice")
	check_eq(c.nominations(), [] as Array[String], "no nomination while a choice is pending")
	check(not c.person_to_play(), "not to play yet")
	var turns := _logged("match.turn")
	check(turns.size() == 1 and turns[0]["waiting"] == "choice" and turns[0]["person"] == true, "match.turn: choice")
	_events.clear()

	check(not c.choose("accept_rerack"), "a choice not offered is refused")
	check_eq(_count(e, "decide"), 0, "without asking the engine")
	check_eq(_errors_with("choose(").size(), 1, "and logged")

	var replaced: Array = (fouled["original"]["balls"] as Array).duplicate(true)
	e.decisions.append(_decided(_turn(1), [], "none", {"balls": replaced}))
	check(c.choose("replace"), "replace")
	check_eq(_last_call(e, "decide"), ["decide", "replace", 0], "decide for the person")
	check_eq(_names(), ["table_changed:replaced", "frame_changed", "turn_started"], "signals after replace")
	check_eq(_events[0], ["table_changed", &"replaced", replaced, 0], "the balls put back")
	check_eq(_events[2], ["turn_started", 1, false], "the offender plays again")
	c.free()


func test_foul_gives_the_opponent_a_choice() -> void:
	var e := _engine(_turn(0))
	var c := _started(e)
	c.shot_started("s-1")
	var fouled := _turn(1, {"pending": "after_foul", "decider": 1, "offender": 0,
			"options": [[], ["play", "play_again"]]})
	e.judgements.append(_judged(fouled, _legal()))
	e.standin_choices.append("play_again")
	var free_ball := [{"kind": "free_ball", "value": 0, "detail": ""}]
	e.decisions.append(_decided(_turn(0), free_ball))
	_mark_log()
	c.shot_finished(null)
	check_eq(_names(), ["table_changed:shot", "frame_changed"], "nothing asked of the person")
	check_eq(c.strike_blocked(), "opponent", "blocked: opponent")
	check_eq(c.harness_state()["waiting"], "opponent", "waiting: opponent")
	check_eq(_count(e, "standin_choice"), 0, "the stand-in is not asked in the same call")
	var turns := _logged("match.turn")
	check(turns.size() == 1 and turns[0]["waiting"] == "opponent" and int(turns[0]["player"]) == 1
			and turns[0]["person"] == false, "match.turn: opponent")
	_events.clear()
	await _frames(2)
	var asked := _last_call(e, "standin_choice")
	check(asked.size() == 3 and asked[1] == 1 and asked[2] is int, "the stand-in chooses for player 1 with a seed")
	check_eq(_last_call(e, "decide"), ["decide", "play_again", 1], "its choice is applied")
	check_eq(_names(), ["opponent_chose", "calls_made", "frame_changed", "turn_started"], "signals after its choice")
	check_eq(_events[0], ["opponent_chose", "play_again"], "what it chose")
	check_eq(_events[1], ["calls_made", free_ball, ""], "a choice's calls have no shot id")
	check_eq(_events[3], ["turn_started", 0, true], "the person plays again")
	c.free()


func test_stand_in_choice_that_is_not_offered_falls_back_to_the_first_option() -> void:
	var e := _engine(_turn(1, {"pending": "rerack", "decider": 1, "options": [[], ["accept_rerack", "refuse_rerack"]]}))
	e.standin_choices.append("play")
	e.decisions.append(_decided(_turn(1)))
	e.standin_shots.append(_standin_shot())
	_mark_log()
	var c := _started(e)
	await _frames(2)
	check_eq(_last_call(e, "decide"), ["decide", "accept_rerack", 1], "the first option is taken")
	var errors := _errors_with("standin_choice(1, ")
	check_eq(errors.size(), 1, "logged")
	if errors.size() == 1:
		check(String(errors[0]["message"]).contains("\"play\""), "with what it gave: " + String(errors[0]["message"]))
		check_eq(errors[0]["match_id"], c.match_id(), "match_id on app.error")
	check(_names().has("opponent_chose"), "and announced")
	c.free()


func test_frame_end_waits_for_next_frame() -> void:
	var e := _engine(_turn(0))
	var c := _started(e)
	c.shot_started("s-1")
	var over := _turn(0, {"over": true, "winner": 0, "end_reason": "black"})
	var call := [{"kind": "frame_over", "value": 0, "detail": "black"}]
	var summary: Dictionary = (e.match_view["history"][0] as Dictionary).duplicate(true)
	summary["number"] = 2
	(e.match_view["history"] as Array).append(summary)
	e.judgements.append(_judged(over, _legal(call), {"event": "frame_ended"}))
	_mark_log()
	c.shot_finished(null)
	check_eq(_names(), ["calls_made", "table_changed:shot", "frame_changed", "frame_ended"],
			"signals at the frame's end")
	check_eq(_events[3], ["frame_ended", summary, e.match_state()], "the last summary and the match")
	check(c.is_active(), "the match goes on")
	check_eq(c.harness_state()["waiting"], "frame_summary", "waiting: frame_summary")
	check_eq(c.strike_blocked(), "inactive", "no frame in play")
	check(not c.person_to_play(), "nobody to play")
	var turns := _logged("match.turn")
	check(turns.size() == 1 and turns[0]["waiting"] == "frame_summary", "match.turn: frame_summary")
	await _frames(2)
	check_eq(_names().size(), 4, "nothing more until next_frame")
	_events.clear()

	var racked := _turn(1, {"cue_in_hand": true, "scores": [0, 0]})
	e.frame_view = racked
	e.match_view["frame_number"] = 3
	e.standin_shots.append(_standin_shot())
	_mark_log()
	var version := c.version()
	e.ball_list[0]["y"] = 0.05
	check(c.next_frame(), "next_frame")
	check_eq(_count(e, "next_frame"), 1, "the engine racks it")
	check_eq(_names(), ["table_changed:frame", "frame_changed", "turn_started"], "signals for the new frame")
	check_eq(_events[1], ["frame_changed", racked], "its frame view")
	check_eq(_events[2], ["turn_started", 1, false], "the other player breaks")
	check_eq(c.version(), version + 1, "version")
	turns = _logged("match.turn")
	check(turns.size() == 1 and int(turns[0]["frame"]) == 3, "later lines carry the new frame's number")
	check(not c.next_frame(), "no frame to rack while one is in play")
	check_eq(_count(e, "next_frame"), 1, "and the engine is not asked")
	c.free()


func test_match_end() -> void:
	var e := _engine(_turn(0))
	var c := _started(e)
	c.shot_started("s-1")
	var over := _turn(0, {"over": true, "winner": 0, "end_reason": "black"})
	e.match_view["over"] = true
	e.match_view["winner"] = 0
	e.judgements.append(_judged(over, _legal(), {"event": "match_ended"}))
	_mark_log()
	c.shot_finished(null)
	check_eq(_names(), ["table_changed:shot", "frame_changed", "frame_ended", "match_ended"],
			"signals at the match's end")
	check_eq(_events[2], ["frame_ended", e.match_view["history"][0], e.match_state()], "the last frame's summary first")
	check_eq(_events[3], ["match_ended", e.match_state()], "then the match")
	check(not c.is_active(), "no longer active")
	check_eq(c.harness_state()["waiting"], "match_summary", "waiting: match_summary")
	check_eq(c.strike_blocked(), "inactive", "blocked: inactive")
	check_eq(c.frame(), over, "the last frame stays to be read")
	check_eq(c.match_state(), e.match_state(), "and the match")
	var turns := _logged("match.turn")
	check(turns.size() == 1 and turns[0]["waiting"] == "match_summary", "match.turn: match_summary")
	check(not c.next_frame(), "no next frame")
	check(not c.concede_match(), "nothing to concede")
	_events.clear()
	c.abandon()
	check_eq(_names(), ["table_changed:ended"], "leaving the summary clears the table")
	check_eq(_count(e, "abandon"), 0, "a finished match is not abandoned in the engine")
	check_eq(c.match_state(), {}, "no match")
	check_eq(c.harness_state()["waiting"], "", "waiting for nothing")
	c.free()


func test_rerack_by_a_stroke_and_by_agreement() -> void:
	var e := _engine(_turn(0))
	var c := _started(e)
	c.shot_started("s-1")
	var racked := _turn(0, {"cue_in_hand": true, "scores": [0, 0]})
	var call := [{"kind": "rerack", "value": 0, "detail": ""}]
	var rack := e.ball_list.duplicate(true)
	rack[0]["x"] = -1.2
	e.judgements.append(_judged(racked, _legal(call), {"event": "reracked", "balls": rack}))
	c.shot_finished(null)
	check_eq(_names(), ["calls_made", "table_changed:frame", "frame_changed", "turn_started"],
			"a stroke that restarts the frame")
	check(c.is_active() and c.placing(), "the breaker is in hand again")
	c.confirm_cue()

	c.shot_started("s-2")
	var offered := _turn(0, {"pending": "rerack", "decider": 0, "options": [["accept_rerack", "refuse_rerack"], []]})
	e.judgements.append(_judged(offered, _legal([{"kind": "rerack_offer", "value": 0, "detail": ""}])))
	c.shot_finished(null)
	check_eq(_events.back(), ["choice_needed", ["accept_rerack", "refuse_rerack"]],
			"the offer is the person's to answer")
	_events.clear()
	rack[0]["x"] = -1.1
	e.decisions.append(_decided(racked, call, "reracked", {"balls": rack}))
	check(c.choose("accept_rerack"), "accept")
	check_eq(_names(), ["calls_made", "table_changed:frame", "frame_changed", "turn_started"],
			"a choice that restarts the frame")
	check_eq(_events[1], ["table_changed", &"frame", rack, 0], "the new rack")
	c.free()


func test_ball_in_hand() -> void:
	var e := _engine(_turn(0, {"cue_in_hand": true}))
	var c := _started(e)
	check(c.placing(), "placing from the start of the turn")
	check_eq(c.strike_blocked(), "placing", "blocked: placing")
	e.nearest = {"x": -1.25, "y": 0.125}
	check_eq(c.nearest_cue_placement(Vector2(-1.5, 0.5)), Vector2(-1.25, 0.125), "the engine's nearest legal place")
	e.nearest = null
	check_eq(c.nearest_cue_placement(Vector2(-1.5, 0.5)), Vector2(-1.5, 0.5), "a legal place is kept")
	_events.clear()
	var version := c.version()
	check(c.place_cue(Vector2(-1.25, 0.125)), "place_cue")
	check_eq(_last_call(e, "place_cue"), ["place_cue", -1.25, 0.125], "goes to the engine")
	check_eq(_names(), ["table_changed:cue"], "table_changed(cue)")
	check_eq(c.balls[0]["x"], -1.25, "the cue ball moved")
	check_eq(c.version(), version + 1, "version")
	check(c.placing(), "still placing")

	_mark_log()
	_events.clear()
	e.cue_legal = false
	check(not c.place_cue(Vector2(0.5, 0.0)), "an illegal place is refused")
	check_eq(_names(), [] as Array[String], "nothing announced")
	check_eq(c.balls[0]["x"], -1.25, "the cue ball where it was")
	check_eq(_errors_with("engine.place_cue(0.5, 0").size(), 1, "logged with the place")
	e.cue_legal = true

	c.confirm_cue()
	check(not c.placing(), "confirmed")
	check_eq(c.strike_blocked(), "", "the person may strike")
	c.move_cue_again()
	check(c.placing(), "move again while still in hand")
	c.confirm_cue()
	c.shot_started("s-1")
	check(not c.place_cue(Vector2(-1.25, 0.0)), "not while a stroke is in play")
	e.judgements.append(_judged(_turn(0), _legal()))
	c.shot_finished(null)
	check(not c.placing(), "not in hand after the stroke")
	c.move_cue_again()
	check(not c.placing(), "nothing to move when the cue ball is not in hand")
	c.free()


func test_opponent_in_hand_places_through_the_controller() -> void:
	var e := _engine(_turn(1, {"cue_in_hand": true}))
	var shot := _standin_shot()
	shot["places_cue"] = true
	shot["cue"] = [-1.125, -0.0625]
	e.standin_shots.append(shot)
	var c := _started(e)
	check(not c.placing(), "the person places nothing for the opponent")
	await _frames(2)
	check_eq(_names(), ["opponent_ready"], "its stroke")
	var given: Dictionary = _events[0][1]
	check_eq(given["cue"], [-1.125, -0.0625], "with its place for the cue ball")
	check(c.place_cue(Vector2(given["cue"][0], given["cue"][1])), "the table scene places it")
	check_eq(_names().back(), "table_changed:cue", "table_changed(cue)")
	check_eq(c.balls[0]["y"], -0.0625, "placed")
	c.free()


func test_nearest_placement_without_an_answer() -> void:
	var e := _engine(_turn(0, {"cue_in_hand": true}))
	var c := _controller(e)
	check_eq(c.nearest_cue_placement(Vector2(0.5, 0.25)), Vector2(0.5, 0.25), "no match: the place as asked")
	check(not c.place_cue(Vector2(0.5, 0.25)), "no match: nothing to place")
	c.start(CONFIG)
	e.nearest = {}
	_mark_log()
	check_eq(c.nearest_cue_placement(Vector2(0.5, 0.25)), Vector2(0.5, 0.25), "an empty answer: the place as asked")
	c.nearest_cue_placement(Vector2(0.5, 0.5))
	check_eq(_errors_with("nearest_cue_placement").size(), 1, "logged once, not on every drag")
	c.free()


func test_nomination() -> void:
	var e := _engine(_turn(0, CHOICE_OF_COLOUR))
	var c := _started(e)
	check_eq(c.nominations(), COLOURS, "the frame view's nominations")
	check_eq(c.nominated(), "", "nothing declared")
	check_eq(c.strike_blocked(), "nominate", "blocked: nominate")
	_mark_log()
	check(not c.nominate("red", "tap"), "a ball not eligible is refused")
	check(not c.nominate("", "aim"), "so is no ball")
	check(c.nominate("blue", "tap"), "blue")
	check_eq(c.nominated(), "blue", "declared")
	check_eq(c.strike_blocked(), "", "the person may strike")
	check(c.nominate("blue", "aim"), "blue again")
	check(c.nominate("pink", "aim"), "pink")
	var lines := _logged("match.nominated")
	check_eq(lines.size(), 2, "match.nominated only when the choice changes")
	if lines.size() == 2:
		check(lines[0]["ball"] == "blue" and lines[0]["how"] == "tap", "the first")
		check(lines[1]["ball"] == "pink" and lines[1]["how"] == "aim", "the second")
		check(lines[1]["match_id"] == c.match_id() and int(lines[1]["frame"]) == 2, "with match_id and frame")

	c.shot_started("s-1")
	check_eq(c.nominations(), [] as Array[String], "nothing to declare once struck")
	check(not c.nominate("black", "tap"), "the declaration can't change after the strike")
	e.judgements.append(_judged(_turn(0, CHOICE_OF_COLOUR), _legal()))
	c.shot_finished(null)
	check_eq(_last_call(e, "judge")[2], {"nominated": "pink"}, "the referee is given the declared colour")
	check_eq(c.nominated(), "", "the declaration lasts one stroke")
	check_eq(c.strike_blocked(), "nominate", "and must be made again")

	_mark_log()
	check(c.nominate("green", "tap"), "green")
	check(c.nominate_none("aim"), "withdrawn")
	check_eq(c.nominated(), "", "nothing declared")
	check_eq(c.strike_blocked(), "nominate", "a colour of choice still needs one")
	check(c.nominate_none("aim"), "withdrawn again")
	var withdrawn := _logged("match.nominated")
	check(withdrawn.size() == 2 and withdrawn[1]["ball"] == "", "the withdrawal is logged, once")

	c.nominate("black", "tap")
	c.shot_started("s-2")
	e.judgements.append(_judged(_json(FRAME_VIEW), _legal()))
	c.shot_finished(null)
	check_eq(c.harness_state()["waiting"], "choice", "a stroke that leaves a choice")
	check_eq(c.nominated(), "", "clears the declaration too")
	e.decisions.append(_decided(_turn(0)))
	c.choose("play_again")
	check_eq(c.strike_blocked(), "", "no nomination open: not blocked")
	check(not c.nominate("blue", "tap"), "and nothing to nominate")
	check(not c.nominate_none("tap"), "or to withdraw")
	c.free()


func test_a_free_ball_need_not_be_nominated() -> void:
	var e := _engine(_turn(0, {"free_ball": true, "nominations": COLOURS}))
	var c := _started(e)
	check_eq(c.nominations(), COLOURS, "any colour may be the free ball")
	check_eq(c.strike_blocked(), "", "but a ball on may be played with nothing declared")
	check(c.nominate("blue", "aim"), "the blue as the free ball")
	check(c.nominate_none("aim"), "the aim left it for a ball on")
	check_eq(c.strike_blocked(), "", "still free to strike")
	c.shot_started("s-1")
	e.judgements.append(_judged(_turn(0), _legal()))
	c.shot_finished(null)
	check_eq(_last_call(e, "judge")[2], {"nominated": ""}, "judged with no declaration")
	c.free()


func test_the_opponents_nomination() -> void:
	var e := _engine(_turn(1, {"nominations": COLOURS}))
	var shot := _standin_shot()
	shot["nominated"] = "black"
	e.standin_shots.append(shot)
	var c := _started(e)
	await _frames(2)
	check(c.nominate("black", "opponent"), "the table scene declares the opponent's colour")
	c.shot_started("s-1")
	e.judgements.append(_judged(_turn(1), _legal()))
	c.shot_finished(null)
	check_eq(_last_call(e, "judge")[2], {"nominated": "black"}, "and the referee is given it")
	c.free()


func test_strike_blocked_reasons() -> void:
	var e := _engine(_turn(0, {"cue_in_hand": true, "on": {"kind": "any_colour"}, "nominations": COLOURS}))
	var c := _controller(e)
	check_eq(c.strike_blocked(), "inactive", "no match")
	check_eq(c.harness_state()["blocked"], "inactive", "in the harness state too")
	c.start(CONFIG)
	check_eq(c.strike_blocked(), "placing", "the cue ball is not confirmed")
	c.confirm_cue()
	check_eq(c.strike_blocked(), "nominate", "then a colour must be declared")
	c.nominate("brown", "tap")
	check_eq(c.strike_blocked(), "", "then the stroke may be played")
	c.shot_started("s-1")
	check_eq(c.strike_blocked(), "playing", "a stroke in play")
	e.judgements.append(_judged(_json(FRAME_VIEW), _legal()))
	c.shot_finished(null)
	check_eq(c.strike_blocked(), "choice", "a decision is the person's")
	e.decisions.append(_decided(_turn(1)))
	c.choose("play_again")
	check_eq(c.strike_blocked(), "opponent", "the opponent's turn")
	e.decisions.append(_decided(_turn(1, {"over": true}), [], "frame_ended"))
	check(c.concede_frame(), "conceding while the opponent thinks")
	check_eq(c.strike_blocked(), "inactive", "a frame summary showing")
	c.free()


func test_autoplay_plays_and_chooses_for_the_person() -> void:
	var e := _engine(_turn(0, {"cue_in_hand": true}))
	e.standin_shots.append(_standin_shot())
	var c := _controller(e)
	c.autoplay = true
	_mark_log()
	c.start(CONFIG)
	check_eq(_names(), ["table_changed:frame", "frame_changed", "turn_started"], "start")
	check_eq(_events[2], ["turn_started", 0, false], "the person's turn is not played by hand")
	check(not c.placing(), "the stand-in places inside its stroke")
	check(not c.person_to_play(), "the person is not to play")
	check_eq(c.strike_blocked(), "opponent", "blocked: opponent")
	check_eq(c.harness_state()["waiting"], "opponent", "waiting: opponent")
	check_eq(c.harness_state()["autoplay"], true, "autoplay in the harness state")
	var turns := _logged("match.turn")
	check(turns.size() == 1 and int(turns[0]["player"]) == 0 and turns[0]["person"] == false
			and turns[0]["waiting"] == "opponent", "match.turn says the stand-in acts for player 0")
	await _frames(2)
	check_eq(_names().back(), "opponent_ready", "the stand-in's stroke for the person")
	var shot: Dictionary = _events.back()[1]
	check_eq(int(shot["player"]), 0, "for player 0")
	_events.clear()

	c.shot_started(shot["shot_id"])
	e.judgements.append(_judged(_json(FRAME_VIEW), _legal()))
	e.standin_choices.append("play")
	e.decisions.append(_decided(_turn(0)))
	e.standin_shots.append(_standin_shot())
	c.shot_finished(null)
	check_eq(_names(), ["table_changed:shot", "frame_changed"], "the person is not asked to choose")
	_events.clear()
	await _frames(3)
	check_eq(_last_call(e, "standin_choice")[1], 0, "the stand-in chooses for player 0")
	check_eq(_last_call(e, "decide"), ["decide", "play", 0], "and it is applied for them")
	check_eq(_names(), ["frame_changed", "turn_started", "opponent_ready"],
			"no opponent_chose for the person's own choice, and it plays on")
	c.free()


func test_autoplay_switched_during_the_persons_turn() -> void:
	var e := _engine(_turn(0))
	e.standin_shots.append(_standin_shot())
	var c := _started(e)
	_events.clear()
	c.autoplay = true
	check_eq(_names(), ["turn_started"], "the turn is handed over")
	check_eq(_events[0], ["turn_started", 0, false], "to the stand-in")
	_events.clear()
	c.autoplay = false
	check_eq(_events, [["turn_started", 0, true]] as Array[Array], "and handed back")
	await _frames(3)
	check_eq(_count(e, "standin_shot"), 0, "the stand-in's wait was dropped")
	check(c.person_to_play(), "the person plays")

	c.shot_started("s-1")
	e.judgements.append(_judged(_json(FRAME_VIEW), _legal()))
	c.shot_finished(null)
	check_eq(_names().back(), "choice_needed", "a choice for the person")
	e.standin_choices.append("play")
	e.decisions.append(_decided(_turn(0)))
	c.autoplay = true
	await _frames(2)
	check_eq(_last_call(e, "decide"), ["decide", "play", 0], "switched on mid-choice, the stand-in chooses")
	c.free()


func test_abandon_cancels_the_wait() -> void:
	var e := _engine(_turn(1))
	e.standin_shots.append(_standin_shot())
	var c := _started(e)
	_events.clear()
	c.abandon()
	check_eq(_names(), ["table_changed:ended"], "table_changed(ended)")
	check_eq(_count(e, "abandon"), 1, "the engine's match is abandoned")
	check(not c.is_active(), "not active")
	check_eq(c.frame(), {}, "no frame")
	check_eq(c.match_id(), "", "no match id")
	var state := c.harness_state()
	check(state["active"] == false and state["waiting"] == "" and state["blocked"] == "inactive", "harness state")
	await _frames(3)
	check_eq(_count(e, "standin_shot"), 0, "the stand-in is never asked")
	check_eq(_names(), ["table_changed:ended"], "and nothing more is announced")
	c.abandon()
	check_eq(_names().size(), 1, "leaving twice says nothing")

	e.frame_view = _turn(1, {"pending": "toss", "decider": 1, "options": [[], ["play_first", "play_second"]]})
	e.standin_choices.append("play_first")
	c.start(CONFIG)
	c.abandon()
	await _frames(3)
	check_eq(_count(e, "standin_choice"), 0, "nor asked to choose")
	c.free()


func test_stroke_reported_when_none_is_awaited() -> void:
	var e := _engine(_json(FRAME_VIEW))
	var c := _started(e)
	check_eq(c.harness_state()["waiting"], "choice", "a choice is pending")
	_mark_log()
	c.shot_started("s-1")
	check_eq(c.strike_blocked(), "choice", "the stroke is not taken")
	check_eq(c.shot_finished(null), {}, "nor judged")
	check_eq(_count(e, "judge"), 0, "the engine is not asked")
	check_eq(_errors_with("shot_started(\"s-1\")").size(), 1, "shot_started logged")
	check_eq(_errors_with("shot_finished").size(), 1, "shot_finished logged")
	c.abandon()
	check_eq(c.shot_finished(null), {}, "a stroke still rolling when the match was left is dropped")
	c.free()


func test_stroke_finished_without_being_started_is_still_judged() -> void:
	var e := _engine(_turn(1))
	e.standin_shots.append(_standin_shot())
	var c := _started(e)
	e.judgements.append(_judged(_turn(0), _legal()))
	_mark_log()
	check_eq(c.shot_finished(null), _legal(), "judged")
	check_eq(_last_call(e, "judge")[3], {"shot_id": ""}, "with no shot id")
	check_eq(_names(), ["table_changed:shot", "frame_changed", "turn_started"], "and play goes on")
	check_eq(_errors_with("without shot_started").size(), 1, "logged")
	await _frames(3)
	check_eq(_count(e, "standin_shot"), 0, "the opponent's wait for that stroke was dropped")
	c.free()


func test_judge_refusal_leaves_the_turn_with_the_same_player() -> void:
	var e := _engine(_turn(0, {"cue_in_hand": true}))
	e.error = "the record is not from this position"
	var c := _started(e)
	c.confirm_cue()
	c.shot_started("s-9")
	_mark_log()
	check_eq(c.shot_finished(null), {}, "no judgement")
	check_eq(_names(), ["table_changed:shot", "frame_changed", "turn_started"],
			"the table and the turn are given again")
	check_eq(_events[0], ["table_changed", &"shot", e.ball_list, 0], "the balls as the engine has them")
	check_eq(_events[2], ["turn_started", 0, true], "the same player")
	check(c.person_to_play() and c.placing(), "who may play, in hand as before")
	var errors := _errors_with("engine.judge(")
	check_eq(errors.size(), 1, "app.error names the call")
	if errors.size() == 1:
		var message := String(errors[0]["message"])
		check(message.contains("\"shot_id\":\"s-9\"") and message.contains("\"nominated\":\"\""),
				"its arguments: " + message)
		check(message.contains("no judgement queued") and message.contains("the record is not from this position"),
				"the error and last_error: " + message)
		check(errors[0]["match_id"] == c.match_id() and int(errors[0]["frame"]) == 2, "match_id and frame")
	c.free()


func test_decide_refusal_keeps_the_choice_open() -> void:
	var e := _engine(_json(FRAME_VIEW))
	var c := _started(e)
	_events.clear()
	_mark_log()
	check(not c.choose("play"), "the engine refuses")
	check_eq(_names(), [] as Array[String], "nothing announced")
	check_eq(c.harness_state()["waiting"], "choice", "the choice is still the person's")
	check_eq(_errors_with("engine.decide(\"play\", 0) refused: no decision queued").size(), 1, "logged")
	e.decisions.append(_decided(_turn(0)))
	check(c.choose("play"), "and can be made again")
	check_eq(_names(), ["frame_changed", "turn_started"], "play goes on")
	c.free()


func test_next_frame_refusal() -> void:
	var e := _engine(_turn(0))
	var c := _started(e)
	e.decisions.append(_decided(_turn(0, {"over": true}), [], "frame_ended"))
	check(c.concede_frame(), "the frame is conceded")
	e.next_frame_ok = false
	_events.clear()
	_mark_log()
	check(not c.next_frame(), "the engine refuses")
	check_eq(_names(), [] as Array[String], "nothing announced")
	check_eq(c.harness_state()["waiting"], "frame_summary", "still at the summary")
	check_eq(_errors_with("engine.next_frame() refused: the frame is not over").size(), 1, "logged")
	c.free()


func test_stand_in_stroke_refusals_stop_after_a_few() -> void:
	var e := _engine(_turn(1))
	_mark_log()
	var c := _started(e)
	await _frames(MatchController.MAX_REFUSALS + 4)
	check_eq(_count(e, "standin_shot"), MatchController.MAX_REFUSALS, "asked again, then no more")
	check_eq(_names(), ["stalled"] as Array[String], "no stroke is announced, the turn is not announced again, and the match stalls")
	var errors := _errors_with("engine.standin_shot(")
	# The last is named again where the stand-in is given up on.
	check_eq(errors.size(), MatchController.MAX_REFUSALS + 1, "each refusal logged")
	if not errors.is_empty():
		check(String(errors[0]["message"]).contains("no stand-in shot queued")
				and String(errors[0]["message"]).contains("shot_id"), "with the seed, the shot id and the error: "
				+ String(errors[0]["message"]))
	check_eq(_errors_with("is not asked again").size(), 1, "and that it gave up")
	check_eq(c.harness_state()["waiting"], "stalled", "the match says it stalled")
	check_eq(c.strike_blocked(), "stalled", "and nobody may strike")
	c.retry_standin()
	check_eq(c.harness_state()["waiting"], "opponent", "resuming asks the stand-in again")
	await _frames(MatchController.MAX_REFUSALS + 4)
	check_eq(_count(e, "standin_shot"), 2 * MatchController.MAX_REFUSALS, "as often as before")
	check_eq(c.harness_state()["waiting"], "stalled", "and it stalls again when that fails too")
	c.free()


func test_stand_in_choice_refusals_stop_after_a_few() -> void:
	var e := _engine(_turn(1, {"pending": "after_foul", "decider": 1, "options": [[], ["play", "play_again"]]}))
	_mark_log()
	var c := _started(e)
	await _frames(MatchController.MAX_REFUSALS + 4)
	check_eq(_count(e, "decide"), MatchController.MAX_REFUSALS, "asked again, then no more")
	check_eq(_errors_with("engine.decide(\"play\", 1)").size(), MatchController.MAX_REFUSALS + 1,
			"each refusal logged, and the last again as the stand-in is given up on")
	check_eq(_names(), ["stalled"] as Array[String], "nothing announced but the stall")
	c.free()


func test_a_judged_stroke_clears_the_refusals() -> void:
	var e := _engine(_turn(1))
	_mark_log()
	var c := _started(e)
	# One short of the limit, a stroke the referee takes, then as many again.
	for i in 2 * MatchController.MAX_REFUSALS - 1:
		e.standin_shots.append(_standin_shot())
		await _frames(2)
		check_eq(_names().back(), "opponent_ready", "stroke %d arrives" % i)
		c.shot_started("s-%d" % i)
		if i == MatchController.MAX_REFUSALS - 1:
			e.judgements.append(_judged(_turn(1), _legal()))
		c.shot_finished(null)
	check_eq(_errors_with("is not asked again").size(), 0, "the stand-in was never given up on")

	e.standin_shots.append(_standin_shot())
	await _frames(2)
	c.shot_started("s-last")
	c.shot_finished(null)
	check_eq(_errors_with("is not asked again").size(), 1, "strokes the referee keeps refusing end the asking too")
	var asked := _count(e, "standin_shot")
	e.standin_shots.append(_standin_shot())
	await _frames(3)
	check_eq(_count(e, "standin_shot"), asked, "no further stroke is asked for")
	check_eq(_names().back(), "turn_started", "the turn stays the opponent's")
	c.free()


func test_concede() -> void:
	var e := _engine(_turn(1))
	e.standin_shots.append(_standin_shot())
	var c := _started(e)
	_events.clear()
	var call := [{"kind": "frame_over", "value": 1, "detail": "conceded"}]
	e.decisions.append(_decided(_turn(1, {"over": true, "winner": 1}), call, "frame_ended"))
	check(c.concede_frame(), "concede_frame")
	check_eq(_last_call(e, "decide"), ["decide", "concede_frame", 0], "decided for the person")
	check_eq(_names(), ["calls_made", "frame_changed", "frame_ended"], "the frame ends")
	await _frames(3)
	check_eq(_count(e, "standin_shot"), 0, "the opponent's wait was dropped")
	e.frame_view = _turn(0)
	c.next_frame()
	_events.clear()

	c.shot_started("s-1")
	_mark_log()
	check(not c.concede_frame() and not c.concede_match(), "not while a stroke is in play")
	check_eq(_count(e, "decide"), 1, "the engine is not asked")
	check_eq(_refused_with("a stroke is in play").size(), 2, "logged as refused")
	check(_logged("app.error").is_empty(), "with no error")
	e.judgements.append(_judged(_turn(0), _legal()))
	c.shot_finished(null)
	_events.clear()

	_mark_log()
	check(not c.concede_frame(), "the engine may refuse")
	check_eq(_errors_with("engine.decide(\"concede_frame\", 0) refused").size(), 1, "logged")
	check(c.person_to_play(), "and play goes on")

	var summary: Dictionary = (e.match_view["history"][0] as Dictionary).duplicate(true)
	summary["number"] = 3
	summary["reason"] = "match_conceded"
	(e.match_view["history"] as Array).append(summary)
	e.decisions.append(_decided(_turn(0, {"over": true, "end_reason": "match_conceded"}), [], "match_ended"))
	check(c.concede_match(), "concede_match")
	check_eq(_names(), ["frame_changed", "frame_ended", "match_ended"], "the last frame's summary, then the match")
	check_eq(_events[1][1], summary, "the frame as the engine summarised it")
	check(not c.is_active(), "over")
	c.free()


func test_nothing_is_conceded_at_a_frame_summary() -> void:
	var e := _engine(_turn(0))
	var c := _started(e)
	var over := _turn(0, {"over": true, "winner": 1, "options": [[], []]})
	e.decisions.append(_decided(over, [], "frame_ended"))
	check(c.concede_frame(), "the frame is conceded")
	check_eq(c.harness_state()["waiting"], "frame_summary", "its summary shows")
	_events.clear()
	_mark_log()
	var asked := _count(e, "decide")
	check(not c.concede_match(), "the match can't be conceded between frames")
	check(not c.concede_frame(), "nor the frame that is over")
	check_eq(_count(e, "decide"), asked, "the engine, which offers nothing then, is not asked")
	check_eq(_names(), [] as Array[String], "nothing is announced")
	check_eq(_refused_with("no frame is in play").size(), 2, "each is logged as refused with why")
	check_eq(c.harness_state()["waiting"], "frame_summary", "the summary still shows")
	c.abandon()
	check_eq(_names(), ["table_changed:ended"], "the match is left by abandoning it")
	c.free()


func test_the_stand_in_claims_or_concedes_in_place_of_a_stroke() -> void:
	var e := _engine(_turn(1))
	e.standin_shots.append({"ok": true, "error": "", "decision": "concede_frame", "why": {"kind": "hopeless"}, "us": 80})
	var over := _turn(1, {"over": true, "winner": 0})
	e.decisions.append(_decided(over, [{"kind": "frame_over", "value": 0, "detail": "conceded"}], "frame_ended"))
	var c := _started(e)
	await _frames(2)
	check_eq(_last_call(e, "decide"), ["decide", "concede_frame", 1], "its decision is applied for it")
	check_eq(_names(), ["opponent_chose", "calls_made", "frame_changed", "frame_ended"], "and announced as a choice")
	check_eq(_events[0], ["opponent_chose", "concede_frame"], "what it chose")
	check(not _names().has("opponent_ready"), "no stroke is handed to the table")
	check_eq(c.harness_state()["waiting"], "frame_summary", "the frame is over")

	# For the person, under autoplay, and refused by the engine: asked again, then left alone.
	var again := _engine(_turn(0))
	var d := _controller(again)
	d.autoplay = true
	for i in MatchController.MAX_REFUSALS + 2:
		again.standin_shots.append({"ok": true, "error": "", "decision": "claim_frame", "why": {}, "us": 1})
	_mark_log()
	d.start(CONFIG)
	await _frames(MatchController.MAX_REFUSALS + 4)
	check_eq(_last_call(again, "decide"), ["decide", "claim_frame", 0], "decided for the person")
	check_eq(_count(again, "decide"), MatchController.MAX_REFUSALS, "a refused decision is asked for again, a few times")
	check(not _names().has("opponent_ready") and not _names().has("opponent_chose"), "nothing announced")
	check_eq(_errors_with("engine.decide(\"claim_frame\", 0)").size(), MatchController.MAX_REFUSALS + 1,
			"each refusal logged, and the last again as the stand-in is given up on")
	c.free()
	d.free()


func test_a_choice_that_moves_the_balls_says_so() -> void:
	# S3.17(d): a re-rack accepted while the black is re-spotted racks nothing new; the engine
	# answers with no event and the balls set again.
	var offer := _turn(0, {"pending": "rerack", "decider": 0, "respotted_black": true,
			"options": [["accept_rerack", "refuse_rerack"], []]})
	var e := _engine(offer)
	var c := _started(e)
	var set_again := e.ball_list.duplicate(true)
	set_again[0]["x"] = -1.2
	e.decisions.append(_decided(_turn(0, {"cue_in_hand": true}), [], "none", {"balls": set_again}))
	var version := c.version()
	check(c.choose("accept_rerack"), "the re-rack is accepted")
	check_eq(_names(), ["table_changed:spotted", "frame_changed", "turn_started"], "the balls set again are announced")
	check_eq(_events[0], ["table_changed", &"spotted", set_again, 0], "as they now lie")
	check_eq(c.version(), version + 1, "and counted")
	_events.clear()
	e.decisions.append(_decided(_turn(0)))
	c.shot_started("s-1")
	e.judgements.append(_judged(_json(FRAME_VIEW), _legal()))
	c.shot_finished(null)
	_events.clear()
	c.choose("play")
	check_eq(_names(), ["frame_changed", "turn_started"], "a choice that moves nothing announces no table")
	c.free()


func test_an_idle_controller_asks_nothing() -> void:
	var e := _engine(_turn(1))
	var c := _controller(e)
	await _frames(3)
	check_eq(e.calls.size(), 0, "with no match started the engine is left alone")
	var bare := MatchController.new()
	tree.root.add_child(bare)
	await _frames(3)
	check(not (bare.get_node("OpponentDriver") as OpponentDriver).is_processing(), "and with no engine the driver sleeps")
	bare.free()
	c.free()


func test_frame_end_without_a_summary_is_logged() -> void:
	var e := _engine(_turn(0))
	e.match_view["history"] = []
	var c := _started(e)
	c.shot_started("s-1")
	e.judgements.append(_judged(_turn(0, {"over": true}), _legal(), {"event": "frame_ended"}))
	_mark_log()
	c.shot_finished(null)
	check_eq(_events.back(), ["frame_ended", {}, e.match_state()], "frame_ended with an empty summary")
	check_eq(_errors_with("no summary").size(), 1, "logged")
	c.free()


func test_harness_state() -> void:
	var e := _engine(_turn(0, {"cue_in_hand": true, "on": {"kind": "any_colour"}, "nominations": COLOURS}))
	var c := _started(e)
	c.confirm_cue()
	c.nominate("blue", "harness")
	var state := c.harness_state()
	check_eq(state, {"active": true, "match_id": c.match_id(), "frame": e.frame_view, "match": e.match_state(),
			"nominated": "blue", "placing": false, "blocked": "", "autoplay": false, "waiting": "shot", "level": "",
			"exhibition": false, "thinking": false}, "harness_state")
	c.free()
	var idle := MatchController.new()
	check_eq(idle.harness_state(), {"active": false, "match_id": "", "frame": {}, "match": {}, "nominated": "",
			"placing": false, "blocked": "inactive", "autoplay": false, "waiting": "", "level": "", "exhibition": false,
			"thinking": false}, "with no match")
	idle.free()

	var thinking := _engine(_turn(1))
	thinking.opponent_shots.append(_level_shot())
	thinking.think_polls = 100
	c = _controller(thinking)
	c.start(_with(CONFIG, {"level": "beginner"}))
	state = c.harness_state()
	check(state["level"] == "beginner" and state["thinking"] == true and state["exhibition"] == false,
			"a level thinking: %s" % [state])
	c.free()


func test_a_listener_that_moves_the_match_on_ends_the_announcement() -> void:
	var e := _engine(_turn(0))
	var c := _started(e)
	c.table_changed.connect(func(_balls: Array[Dictionary], _cue: int, reason: StringName) -> void:
		if reason == &"shot":
			c.abandon())
	c.shot_started("s-1")
	e.judgements.append(_judged(_turn(0), _legal()))
	c.shot_finished(null)
	check_eq(_names(), ["table_changed:shot", "table_changed:ended"], "nothing announced for the match that was left")
	c.free()

	e = _engine(_turn(0))
	c = _started(e)
	c.frame_ended.connect(func(_summary: Dictionary, _state: Dictionary) -> void:
		e.frame_view = _turn(0)
		c.next_frame())
	c.shot_started("s-1")
	e.judgements.append(_judged(_turn(0, {"over": true}), _legal(), {"event": "frame_ended"}))
	c.shot_finished(null)
	check_eq(_names(), ["table_changed:shot", "frame_changed", "frame_ended", "table_changed:frame", "frame_changed",
			"turn_started"], "a listener may rack the next frame from frame_ended")
	check_eq(c.harness_state()["waiting"], "shot", "and the new frame's turn stands")
	c.free()

	e = _engine(_turn(0))
	c = _started(e)
	c.frame_ended.connect(func(_summary: Dictionary, _state: Dictionary) -> void: c.abandon())
	c.shot_started("s-1")
	e.judgements.append(_judged(_turn(0, {"over": true}), _legal(), {"event": "match_ended"}))
	c.shot_finished(null)
	check_eq(_names(), ["table_changed:shot", "frame_changed", "frame_ended", "table_changed:ended"],
			"match_ended is not announced for a match already left")
	c.free()


func test_the_wait_scales_with_pace() -> void:
	var e := _engine(_turn(1))
	e.standin_shots.append(_standin_shot())
	var c := _controller(e)
	c.pace = 1.0
	var began := Time.get_ticks_msec()
	c.start(CONFIG)
	await _frames(3)
	# A PC busy enough to take that long over three frames proves nothing either way.
	if Time.get_ticks_msec() - began < int(MatchController.THINK_SECONDS * 1000.0) - 100:
		check_eq(_count(e, "standin_shot"), 0, "at pace 1 the opponent takes its time")
	c.abandon()
	e.calls.clear()
	e.standin_shots.clear()
	e.standin_shots.append(_standin_shot())
	c.pace = 0.2
	began = Time.get_ticks_msec()
	c.start(CONFIG)
	while _count(e, "standin_shot") == 0 and Time.get_ticks_msec() - began < 5000:
		await tree.process_frame
	var waited := Time.get_ticks_msec() - began
	check_eq(_count(e, "standin_shot"), 1, "at pace 0.2 it plays")
	check(waited >= int(MatchController.THINK_SECONDS * 0.2 * 1000.0) - 60,
			"after THINK_SECONDS * pace (waited %d ms)" % waited)
	c.free()


func test_ensure_engine() -> void:
	var e := _engine(_turn(0))
	var c := _controller(e)
	check(c.ensure_engine(), "a test's engine is taken as it is")
	check_eq(e.configured.size(), 0, "and not configured")
	c.free()

	var own := OwnEngine.new()
	own.template = e
	_mark_log()
	check(own.ensure_engine(), "its own engine")
	check_eq(own.engine, e, "is kept")
	check_eq(e.configured.size(), 1, "and configured")
	if e.configured.size() == 1:
		check_eq(e.configured[0][0], Physics.sim, "with the game's physics")
		check_eq(e.configured[0][1], FileAccess.get_file_as_string(MatchController.REFEREE_PATH),
				"the referee's values")
		check_eq(e.configured[0][2], FileAccess.get_file_as_string(MatchController.STANDIN_PATH), "the stand-in's")
		check(not String(e.configured[0][1]).is_empty() and not String(e.configured[0][2]).is_empty(),
				"both files exist")
	check(own.ensure_engine(), "again")
	check_eq(e.configured.size(), 1, "is not configured twice")
	check(own.start(CONFIG), "start")
	check_eq(e.configured.size(), 2, "each match takes the physics in use at its start")
	check_eq(_logged("app.error").size(), 0, "nothing went wrong")
	own.free()

	var refusing := ScriptedEngine.new()
	refusing.ready = false
	refusing.error = "push_contact_time_s must be positive"
	own = OwnEngine.new()
	own.template = refusing
	_mark_log()
	check(not own.ensure_engine(), "a refused configuration")
	check(own.engine == null, "leaves no engine")
	var errors := _errors_with("no match engine")
	check_eq(errors.size(), 1, "app.error")
	if errors.size() == 1:
		var message := String(errors[0]["message"])
		check(message.contains("push_contact_time_s must be positive"), "with last_error: " + message)
		check(message.contains(MatchController.REFEREE_PATH), "and what was refused: " + message)
	check(not own.start(CONFIG), "and no match starts")
	check_eq(_count(refusing, "start"), 0, "the engine is not asked to")
	own.free()

	var real := MatchController.new()
	check_eq(real.ensure_engine(), real.engine != null, "the game's engine is kept only when it configures")
	real.free()


func test_driver_asks_on_a_later_frame() -> void:
	var e := _engine(_turn(1))
	e.standin_shots.append(_standin_shot())
	e.standin_choices.append("play")
	var driver := OpponentDriver.new()
	var shots: Array[Array] = []
	var choices: Array[Array] = []
	driver.shot_chosen.connect(func(player: int, shot: Dictionary) -> void: shots.append([player, shot]))
	driver.choice_made.connect(func(player: int, kind: String) -> void: choices.append([player, kind]))
	tree.root.add_child(driver)
	check(not driver.is_waiting(), "nothing waiting")
	driver.request_shot(e, 1, 0.0)
	check(driver.is_waiting(), "waiting")
	check_eq(_count(e, "standin_shot"), 0, "never in the same call")
	await _frames(2)
	check(not driver.is_waiting(), "done")
	check_eq(shots.size(), 1, "shot_chosen")
	if shots.size() == 1:
		var asked := _last_call(e, "standin_shot")
		check_eq(shots[0][0], 1, "for the player")
		check_eq(asked[1], driver.last_seed, "the seed is kept for the logs")
		check_eq(asked[2], {"shot_id": shots[0][1]["shot_id"]}, "the shot carries the id the stand-in was given")
		check(String(shots[0][1]["shot_id"]).begins_with(Log.session()), "a shot id of this session")

	driver.request_shot(e, 1, 0.0)
	driver.request_choice(e, 0, 0.0)
	await _frames(2)
	check_eq(_count(e, "standin_shot"), 1, "a later request drops the earlier one")
	check_eq(choices, [[0, "play"]] as Array[Array], "choice_made")

	driver.request_choice(e, 0, 0.0)
	driver.cancel()
	check(not driver.is_waiting(), "cancelled")
	await _frames(2)
	check_eq(_count(e, "standin_choice"), 1, "a cancelled request never asks")

	driver.request_shot(e, 1, 0.0)
	await _frames(2)
	check_eq(shots.size(), 2, "a refusal is passed on")
	if shots.size() == 2:
		check(shots[1][1]["ok"] == false and shots[1][1].has("shot_id"), "as the engine gave it, with the id")
	driver.free()


func test_a_level_plays_the_opponents_strokes() -> void:
	var e := _engine(_turn(1))
	e.opponent_shots.append(_level_shot())
	e.think_polls = 2
	var c := _controller(e)
	check(c.start(_with(CONFIG, {"level": "beginner"})), "start with a level")
	check_eq(_last_call(e, "start")[1], {"best_of": 3, "first_breaker": 0, "seed": 147, "concede_any_time": [true, false],
			"person": 0, "level": "beginner"}, "the engine is given the level")
	check_eq(c.level(), "beginner", "level()")
	check_eq(c.level_name(), "Beginner", "level_name() is the level's name")
	check_eq(_names(), ["table_changed:frame", "frame_changed", "opponent_thinking", "turn_started"],
			"the level starts thinking as the turn is announced")
	var think := _last_call(e, "opponent_think")
	check(think[1] == "beginner" and think[2] is int and (think[3] as Dictionary).has("shot_id")
			and think[4] == {"max_ms": OpponentDriver.LEVEL_MAX_MS},
			"opponent_think(level, seed, {shot_id}, {max_ms}): %s" % [think])
	_events.clear()
	await _frames(6)
	check_eq(_names(), ["opponent_thinking", "opponent_ready"], "thinking stops and its stroke arrives")
	if _names().size() == 2:
		check_eq(_events[0], ["opponent_thinking", false], "thinking off")
		var shot: Dictionary = _events[1][1]
		check_eq(shot["shot_id"], think[3]["shot_id"], "the id it thought under")
		check_eq(int(shot["player"]), 1, "for the opponent")
		for key: String in ["level", "meant", "think", "strike"]:
			check_eq(shot.get(key), _level_shot()[key], "keeps the engine's %s" % key)
	check_eq(_count(e, "standin_shot"), 0, "the stand-in is not asked")
	check(not c.harness_state()["thinking"], "no longer thinking")
	c.free()


func test_a_levels_choice_and_the_persons_autoplay() -> void:
	var e := _engine(_turn(1, {"pending": "after_foul", "decider": 1, "options": [[], ["play", "play_again"]]}))
	e.opponent_choices.append("play_again")
	e.decisions.append(_decided(_turn(0)))
	var c := _controller(e)
	c.start(_with(CONFIG, {"level": "maximum"}))
	await _frames(2)
	var asked := _last_call(e, "opponent_choice")
	check(asked[1] == "maximum" and asked[2] == 1 and asked[3] is int, "the level chooses: %s" % [asked])
	check_eq(_count(e, "standin_choice"), 0, "not the stand-in")
	check_eq(_last_call(e, "decide"), ["decide", "play_again", 1], "its choice is applied")
	check_eq(c.level_name(), "The 147", "the 147's name")

	e.standin_shots.append(_standin_shot())
	c.autoplay = true
	await _frames(2)
	check_eq(_count(e, "standin_shot"), 1, "autoplay for the person is the stand-in")
	check_eq(_count(e, "opponent_think"), 0, "never the level")
	c.free()


func test_no_level_is_the_stand_in() -> void:
	var e := _engine(_turn(1))
	e.standin_shots.append(_standin_shot())
	var c := _controller(e)
	check(c.start(CONFIG), "start")
	check(not (_last_call(e, "start")[1] as Dictionary).has("level"), "the engine is given no level")
	check_eq(c.level(), "", "level()")
	check_eq(c.level_name(), MatchController.STANDIN_NAME, "level_name()")
	check(c.start(_with(CONFIG, {"level": ""})), "an empty level")
	await _frames(2)
	check_eq(_count(e, "opponent_think"), 0, "no level thinks")
	check_eq(_count(e, "standin_shot"), 1, "the stand-in plays")
	check_eq(_names().back(), "opponent_ready", "its stroke arrives")
	c.free()


func test_an_unknown_level_is_refused() -> void:
	var e := _engine(_turn(0))
	var c := _started(e)
	var id := c.match_id()
	var starts := _count(e, "start")
	_mark_log()
	check(not c.start(_with(CONFIG, {"level": "expert"})), "an id not listed")
	check(not c.start(_with(CONFIG, {"level": 3})), "not an id at all")
	check_eq(_count(e, "start"), starts, "the engine is not asked")
	check(c.is_active() and c.match_id() == id, "the match in progress stays")
	var errors := _errors_with("start: level")
	check_eq(errors.size(), 2, "each logged")
	if errors.size() == 2:
		check(String(errors[0]["message"]).contains("\"expert\"") and String(errors[0]["message"]).contains("beginner"),
				"naming it and the levels: " + String(errors[0]["message"]))
	check_eq(c.levels(), _typed(e.level_list), "levels() as the engine lists them")
	c.free()


func test_the_opponents_levels_are_configured_with_the_engine() -> void:
	var e := _engine(_turn(0))
	var own := OwnEngine.new()
	own.template = e
	_mark_log()
	check(own.ensure_engine(), "the engine")
	var asked := _last_call(e, "configure_opponent")
	check_eq(asked[1], MatchController.LEVEL_IDS, "configure_opponent with each level's file")
	for id in MatchController.LEVEL_IDS:
		check(not FileAccess.get_file_as_string(MatchController.LEVEL_PATH % id).is_empty(), "%s's file exists" % id)
	check(not FileAccess.get_file_as_string(MatchController.EXHIBITIONS_PATH).is_empty(), "so does the exhibitions'")
	check_eq(_logged("app.error").size(), 0, "nothing went wrong")
	check_eq(own.levels().map(func(level: Dictionary) -> String: return level["id"]), MatchController.LEVEL_IDS,
			"levels(): the six in tier order, the 147 last")
	own.free()

	var refusing := _engine(_turn(1))
	refusing.opponent_ready = false
	refusing.level_list = []
	refusing.error = "maximum: aim_sigma_rad must be positive"
	refusing.standin_shots.append(_standin_shot())
	own = OwnEngine.new()
	own.template = refusing
	tree.root.add_child(own)
	own.pace = 0.0
	_mark_log()
	check(own.start(_with(CONFIG, {"level": "beginner"})), "the match still starts")
	var errors := _errors_with("no opponent levels")
	check(errors.size() >= 1, "the refusal is logged")
	if not errors.is_empty():
		check(String(errors[0]["message"]).contains("aim_sigma_rad must be positive"), "with last_error: "
				+ String(errors[0]["message"]))
	check_eq(_errors_with("the stand-in plays").size(), errors.size() + 1, "and the level asked for is not played")
	check_eq(own.level(), "", "the stand-in's match")
	check_eq(own.levels(), [] as Array[Dictionary], "no levels")
	check(not (_last_call(refusing, "start")[1] as Dictionary).has("level"), "the engine is given none")
	await _frames(2)
	check_eq(_count(refusing, "standin_shot"), 1, "the stand-in plays")
	check_eq(_count(refusing, "opponent_think"), 0, "no level is asked")
	own.free()


func test_a_missing_level_file_leaves_the_other_five() -> void:
	var dir := "user://test_levels"
	DirAccess.make_dir_recursive_absolute(dir)
	for id in MatchController.LEVEL_IDS:
		DirAccess.remove_absolute("%s/opponent_%s.json" % [dir, id])
		if id != "club":
			var file := FileAccess.open("%s/opponent_%s.json" % [dir, id], FileAccess.WRITE)
			file.store_string(FileAccess.get_file_as_string(MatchController.LEVEL_PATH % id))
			file.close()
	var e := _engine(_turn(0))
	var own := OwnEngine.new()
	own.template = e
	own.level_path = dir + "/opponent_%s.json"
	_mark_log()
	check(own.ensure_engine(), "the engine")
	var five: Array[String] = ["beginner", "amateur", "pro", "world", "maximum"]
	check_eq(_last_call(e, "configure_opponent")[1], five, "configured with the five files there")
	check_eq(own.levels().map(func(level: Dictionary) -> String: return level["id"]), five, "levels() lists those five")
	var errors := _errors_with("left out of the picker")
	check_eq(errors.size(), 1, "the missing one is logged once")
	if errors.size() == 1:
		check(String(errors[0]["message"]).contains("opponent_club.json"), "naming its file: " + String(errors[0]["message"]))
	check(not own.start(_with(CONFIG, {"level": "club"})), "and it can't be asked for")
	check_eq(_errors_with("start: level \"club\" is not one of").size(), 1, "which is logged")
	check_eq(_count(e, "start"), 0, "the engine is not asked")
	check(own.start(_with(CONFIG, {"level": "amateur"})), "another still plays")
	own.free()
	for id in MatchController.LEVEL_IDS:
		DirAccess.remove_absolute("%s/opponent_%s.json" % [dir, id])
	DirAccess.remove_absolute(dir)


func test_a_level_file_the_engine_refuses_leaves_the_other_five() -> void:
	var e := _engine(_turn(0))
	e.refused_levels = ["pro"]
	var own := OwnEngine.new()
	own.template = e
	_mark_log()
	check(own.ensure_engine(), "the engine")
	var five: Array[String] = ["beginner", "club", "amateur", "world", "maximum"]
	check_eq(_last_call(e, "configure_opponent")[1], five, "configured at last with the five it takes")
	check_eq(own.levels().map(func(level: Dictionary) -> String: return level["id"]), five, "levels() lists those five")
	var errors := _errors_with("left out of the picker")
	check_eq(errors.size(), 1, "the refusal is logged once")
	if errors.size() == 1:
		var message := String(errors[0]["message"])
		check(message.contains("opponent_pro.json") and message.contains("aim_sigma_rad must be positive")
				and not message.contains("opponent_club.json"), "naming its file and why, and only it: " + message)
	check_eq(_errors_with("the stand-in plays").size(), 0, "the levels still play")
	check(not own.start(_with(CONFIG, {"level": "pro"})), "the refused one can't be asked for")
	check_eq(_errors_with("start: level \"pro\" is not one of").size(), 1, "which is logged")
	check(own.start(_with(CONFIG, {"level": "world"})), "World class still plays")
	check_eq(own.level_name(), "World class", "named")
	own.free()


func test_a_match_starts_at_each_of_the_six_levels() -> void:
	var names := {"beginner": "Beginner", "club": "Club", "amateur": "Amateur", "pro": "Pro", "world": "World class",
			"maximum": "The 147"}
	for id in MatchController.LEVEL_IDS:
		var e := _engine(_turn(0))
		var c := _controller(e)
		_mark_log()
		check(c.start(_with(CONFIG, {"level": id})), "%s: the match starts" % id)
		check_eq((_last_call(e, "start")[1] as Dictionary).get("level"), id, "%s: the engine is given the level" % id)
		check_eq(c.level(), id, "%s: level()" % id)
		check_eq(c.level_name(), names[id], "%s: level_name()" % id)
		check_eq(c.harness_state()["level"], id, "%s: in the harness's state" % id)
		check_eq(_logged("app.error").size(), 0, "%s: nothing went wrong" % id)
		c.free()


func test_a_levels_refusals_stall_the_match() -> void:
	var e := _engine(_turn(1))
	e.error = "no level stroke queued"
	_mark_log()
	var c := _controller(e)
	c.start(_with(CONFIG, {"level": "beginner"}))
	await _frames(MatchController.MAX_REFUSALS + 4)
	check_eq(_count(e, "opponent_think"), MatchController.MAX_REFUSALS, "asked again, then no more")
	var errors := _errors_with("engine.opponent_think(\"beginner\", ")
	check_eq(errors.size(), MatchController.MAX_REFUSALS + 1, "each refusal logged with the level and seed")
	if not errors.is_empty():
		check(String(errors[0]["message"]).contains("no level stroke queued"), "and why: " + String(errors[0]["message"]))
	check_eq(_errors_with("the level \"beginner\" is not asked again").size(), 1, "giving up names the level")
	check_eq(c.harness_state()["waiting"], "stalled", "stalled")
	e.opponent_shots.append(_level_shot())
	c.retry_standin()
	await _frames(3)
	check_eq(_names().back(), "opponent_ready", "a retry asks the level again")
	c.free()


func test_the_pause_holds_a_finished_think() -> void:
	var e := _engine(_turn(1))
	e.opponent_shots.append(_level_shot())
	var c := _controller(e)
	c.start(_with(CONFIG, {"level": "beginner"}))
	# As main's pause menu holds the match.
	c.process_mode = Node.PROCESS_MODE_DISABLED
	_events.clear()
	await _frames(4)
	check_eq(_names(), [] as Array[String], "nothing is handed over while held")
	check_eq(_count(e, "opponent_take"), 0, "the answer stays in the engine")
	check(c.harness_state()["thinking"], "still thinking as far as the game knows")
	c.process_mode = Node.PROCESS_MODE_INHERIT
	await _frames(3)
	check_eq(_names(), ["opponent_thinking", "opponent_ready"], "resumed, the stroke comes")
	c.free()


func test_leaving_while_a_level_thinks_cancels_it() -> void:
	var e := _engine(_turn(1))
	e.opponent_shots.append(_level_shot())
	e.think_polls = 100
	var c := _controller(e)
	c.start(_with(CONFIG, {"level": "beginner"}))
	await _frames(2)
	_events.clear()
	c.abandon()
	check_eq(_last_call(e, "opponent_cancel"), ["opponent_cancel", "left"], "the thinking is stopped")
	check_eq(_names(), ["opponent_thinking", "table_changed:ended"], "thinking off, then the table")
	await _frames(3)
	check_eq(_count(e, "opponent_take"), 0, "nothing is taken")
	check_eq(_names().size(), 2, "or announced")
	check_eq(c.level(), "", "the level goes with the match")
	c.free()


func test_exhibition_starts_from_its_table() -> void:
	var e := _engine(_turn(1))
	e.opponent_shots.append(_level_shot("maximum"))
	var c := _controller(e)
	check(c.start({"exhibition": 0, "level": "maximum", "seed": 9}), "start")
	check_eq(_last_call(e, "start")[1], {"best_of": 1, "first_breaker": 1, "seed": 9, "concede_any_time": [true, false],
			"person": 0, "level": "maximum", "exhibition": 0}, "best of 1 from that table with the opponent at it")
	check(c.is_exhibition(), "is_exhibition()")
	check(c.harness_state()["exhibition"] == true and c.harness_state()["level"] == "maximum", "in the harness state")
	check_eq(_events[3], ["turn_started", 1, false], "the opponent plays")
	await _frames(2)
	check_eq(_names().back(), "opponent_ready", "its stroke")
	check(c.start({"exhibition": 0}), "with no level")
	check_eq(_last_call(e, "start")[1]["level"], "maximum", "the 147's")
	_mark_log()
	var starts := _count(e, "start")
	check(not c.start({"exhibition": 1, "level": "maximum"}), "an index past the tables")
	check(not c.start({"exhibition": "open 1", "level": "maximum"}), "a name, not an index")
	check(not c.start({"exhibition": 0, "level": "nobody"}), "an unknown level")
	check_eq(_count(e, "start"), starts, "the engine is not asked")
	check_eq(_errors_with("start: exhibition").size(), 2, "logged")
	c.free()


func test_exhibition_ends_when_the_break_is_missed() -> void:
	var e := _engine(_turn(1, {"strokes": 0}))
	e.opponent_shots.append(_level_shot("maximum"))
	var c := _controller(e)
	c.start({"exhibition": 0, "level": "maximum"})
	await _frames(2)
	var shot: Dictionary = _events.back()[1]
	c.shot_started(shot["shot_id"])
	var id := c.match_id()
	e.judgements.append(_judged(_turn(0, {"stats": _stats(40), "strokes": 21}), _legal()))
	_events.clear()
	_mark_log()
	c.shot_finished(null)
	check_eq(_names(), ["table_changed:shot", "frame_changed", "exhibition_ended", "table_changed:ended"],
			"the person's turn ends it")
	check_eq(_events[2][1], {"break_points": 40, "maximum": false, "why": "missed"}, "the break and why")
	_check_ended(e, c, id, {"break_points": 40, "maximum": false, "why": "missed", "strokes": 21})
	check_eq(_count(e, "abandon"), 1, "the engine's match is abandoned")
	c.free()


func test_exhibition_ends_with_a_foul() -> void:
	var e := _engine(_turn(1))
	e.opponent_shots.append(_level_shot("maximum"))
	var c := _controller(e)
	c.start({"exhibition": 0, "level": "maximum"})
	await _frames(2)
	c.shot_started("s-1")
	var fouled := _turn(0, {"pending": "after_foul", "decider": 0, "offender": 1, "options": [["play", "play_again"], []],
			"stats": _stats(56), "strokes": 30})
	var judgement := _legal()
	judgement["penalty"] = 4
	judgement["points"] = 0
	e.judgements.append(_judged(fouled, judgement))
	var id := c.match_id()
	_events.clear()
	_mark_log()
	c.shot_finished(null)
	check_eq(_names(), ["table_changed:shot", "frame_changed", "exhibition_ended", "table_changed:ended"],
			"the person is not asked to choose")
	_check_ended(e, c, id, {"break_points": 56, "maximum": false, "why": "fouled", "strokes": 30})
	c.free()


func test_exhibition_ends_with_the_maximum() -> void:
	var e := _engine(_turn(1, {"break_points": 140}))
	e.opponent_shots.append(_level_shot("maximum"))
	var c := _controller(e)
	c.start({"exhibition": 0, "level": "maximum"})
	await _frames(2)
	c.shot_started("s-36")
	var over := _turn(1, {"over": true, "winner": 1, "end_reason": "black", "break_points": 0, "stats": _stats(147),
			"strokes": 36})
	e.match_view["over"] = true
	e.judgements.append(_judged(over, _legal(), {"event": "match_ended"}))
	var id := c.match_id()
	_events.clear()
	_mark_log()
	c.shot_finished(null)
	check_eq(_names(), ["table_changed:shot", "frame_changed", "exhibition_ended", "table_changed:ended"],
			"no frame or match summary")
	_check_ended(e, c, id, {"break_points": 147, "maximum": true, "why": "maximum", "strokes": 36})
	check_eq(_count(e, "abandon"), 0, "a finished match is not abandoned in the engine")
	c.free()


func test_exhibition_left_mid_break() -> void:
	var e := _engine(_turn(1, {"break_points": 33, "stats": _stats(0), "strokes": 12}))
	e.opponent_shots.append(_level_shot("maximum"))
	e.think_polls = 100
	var c := _controller(e)
	c.start({"exhibition": 0, "level": "maximum"})
	var id := c.match_id()
	_events.clear()
	_mark_log()
	c.abandon()
	check_eq(_names(), ["opponent_thinking", "exhibition_ended", "table_changed:ended"], "quit")
	_check_ended(e, c, id, {"break_points": 33, "maximum": false, "why": "left", "strokes": 12})
	check_eq(_last_call(e, "opponent_cancel"), ["opponent_cancel", "left"], "its thinking stopped")
	c.abandon()
	check_eq(_names().size(), 3, "leaving again says nothing")
	check_eq(_logged("exhibition.ended").size(), 1, "and logs nothing")
	c.free()


func test_exhibition_never_waits_for_the_person() -> void:
	var e := _engine(_turn(0, {"strokes": 0}))
	var c := _controller(e)
	_mark_log()
	check(c.start({"exhibition": 0}), "the engine puts the person at the table")
	check(not _names().has("turn_started"), "no turn for the person")
	check_eq(_names().back(), "table_changed:ended", "the exhibition is over at once")
	check_eq(_errors_with("before the opponent struck").size(), 1, "which is logged")
	check_eq(_logged("exhibition.ended").size(), 1, "exhibition.ended")
	c.free()


func test_conceding_or_claiming_while_a_level_thinks_cancels_it_saying_which() -> void:
	for case: Array in [["concede_frame", "conceded"], ["concede_match", "conceded"], ["claim_frame", "claimed"]]:
		var kind: String = case[0]
		var e := _engine(_turn(1, {"options": [[kind], []]}))
		e.opponent_shots.append(_level_shot())
		e.think_polls = 100
		var c := _controller(e)
		c.start(_with(CONFIG, {"level": "beginner"}))
		check(c.harness_state()["thinking"], "%s: the level thinks" % kind)
		e.decisions.append(_decided(_turn(1, {"over": true}), [], "frame_ended"))
		check(c.call(kind), "%s taken" % kind)
		check_eq(_last_call(e, "opponent_cancel"), ["opponent_cancel", case[1]], "%s: the thinking dropped as %s" % [kind, case[1]])
		check_eq(_count(e, "opponent_cancel"), 1, "%s: once" % kind)
		c.free()

	# Refused all the same: the opponent is asked again, or it would never play.
	var e := _engine(_turn(1, {"options": [["concede_frame"], []]}))
	e.opponent_shots.append(_level_shot())
	e.think_polls = 100
	var c := _controller(e)
	c.start(_with(CONFIG, {"level": "beginner"}))
	check(not c.concede_frame(), "the engine refuses the concession")
	check_eq(_count(e, "opponent_think"), 2, "the level is asked again")
	check(c.harness_state()["thinking"] and c.waiting() == "opponent", "and thinks on")
	c.free()


func test_an_exhibition_offers_nothing_to_concede_or_claim() -> void:
	var e := _engine(_turn(1, {"options": [["concede_frame", "concede_match", "claim_frame"], []]}))
	e.opponent_shots.append(_level_shot("maximum"))
	e.think_polls = 100
	var c := _controller(e)
	c.start({"exhibition": 0, "level": "maximum"})
	_mark_log()
	for kind: String in MatchController.PAUSE_DECISIONS:
		check(not c.open_to_person(kind), "%s is not open" % kind)
		check(not c.call(kind), "%s is refused" % kind)
	check_eq(_count(e, "decide"), 0, "the engine is not asked")
	check_eq(_count(e, "opponent_cancel"), 0, "the 147 thinks on")
	var refused := _logged("match.refused")
	check_eq(refused.size(), 3, "each refusal logged")
	for line in refused:
		check(String(line["why"]).contains("only watches"), "saying why: %s" % [line])
	check_eq(_errors_with("").size(), 0, "with no app.error")
	check(c.is_exhibition() and c.is_active(), "the exhibition goes on")
	c.free()


func test_the_judged_break_outlasts_the_frame_an_exhibition_clears() -> void:
	var e := _engine(_turn(1, {"break_points": 140}))
	e.opponent_shots.append(_level_shot("maximum"))
	var c := _controller(e)
	c.start({"exhibition": 0, "level": "maximum"})
	await _frames(2)
	check_eq(c.judged_break(), 0, "nothing judged yet")
	c.shot_started("s-36")
	var over := _turn(1, {"over": true, "winner": 1, "end_reason": "black", "break_points": 147, "stats": _stats(147),
			"strokes": 36})
	e.match_view["over"] = true
	e.judgements.append(_judged(over, _legal(), {"event": "match_ended"}))
	c.shot_finished(null)
	check_eq(c.frame(), {}, "the exhibition has ended and its frame is gone")
	check_eq(c.judged_break(), 147, "the break as judged stays")
	c.free()

	var m := _started(_engine(_turn(0, {"break_points": 0})))
	m.shot_started("s-1")
	(m.engine as ScriptedEngine).judgements.append(_judged(_turn(0, {"break_points": 7}), _legal()))
	m.shot_finished(null)
	check_eq(m.judged_break(), 7, "a match's is the frame's")
	m.shot_started("s-2")
	m.shot_finished(null)
	check_eq(m.judged_break(), 7, "a refused judgement leaves the frame's as it was")
	m.free()


## The exhibition has ended and logged so, the match left.
func _check_ended(e: ScriptedEngine, c: MatchController, id: String, logged: Dictionary) -> void:
	var result: Dictionary = logged.duplicate()
	result.erase("strokes")
	var ended: Array = _events.filter(func(event: Array) -> bool: return event[0] == "exhibition_ended")
	check(ended.size() == 1 and ended[0][1] == result, "exhibition_ended %s: %s" % [result, ended])
	var lines := _logged("exhibition.ended")
	check_eq(lines.size(), 1, "exhibition.ended once")
	if lines.size() == 1:
		var expected := logged.duplicate()
		expected["match_id"] = id
		var line := lines[0]
		check(int(line["break_points"]) == int(expected["break_points"]) and line["maximum"] == expected["maximum"]
				and line["why"] == expected["why"] and int(line["strokes"]) == int(expected["strokes"])
				and line["match_id"] == id, "the line %s, expected %s" % [line, expected])
	check(not c.is_active() and not c.is_exhibition(), "left")
	check_eq(c.match_id(), "", "no match")
	for name: String in ["frame_ended", "match_ended", "turn_started", "choice_needed"]:
		check(not _names().has(name), "no %s" % name)
	check_eq(e.called().count("opponent_think"), 1, "the opponent was asked once")


func _engine(frame: Dictionary) -> ScriptedEngine:
	var e := ScriptedEngine.new()
	e.frame_view = frame
	e.match_view = _json(MATCH)
	e.match_view.erase("frame")
	e.ball_list = (frame["balls"] as Array).duplicate(true)
	return e


## A controller on `e`, in the tree, acting on the next frame, with every signal recorded.
func _controller(e: ScriptedEngine) -> MatchController:
	var c := MatchController.new()
	c.engine = e
	c.pace = 0.0
	_events.clear()
	c.table_changed.connect(func(balls: Array[Dictionary], cue: int, reason: StringName) -> void:
		_events.append(["table_changed", reason, balls, cue]))
	c.frame_changed.connect(func(frame: Dictionary) -> void: _events.append(["frame_changed", frame]))
	c.calls_made.connect(func(calls: Array, shot_id: String) -> void: _events.append(["calls_made", calls, shot_id]))
	c.turn_started.connect(func(player: int, person: bool) -> void: _events.append(["turn_started", player, person]))
	c.choice_needed.connect(func(options: Array[String]) -> void: _events.append(["choice_needed", options]))
	c.opponent_ready.connect(func(shot: Dictionary) -> void: _events.append(["opponent_ready", shot]))
	c.opponent_chose.connect(func(kind: String) -> void: _events.append(["opponent_chose", kind]))
	c.frame_ended.connect(func(summary: Dictionary, state: Dictionary) -> void:
		_events.append(["frame_ended", summary, state]))
	c.match_ended.connect(func(state: Dictionary) -> void: _events.append(["match_ended", state]))
	c.stalled.connect(func(why: String) -> void: _events.append(["stalled", why]))
	c.opponent_thinking.connect(func(on: bool) -> void: _events.append(["opponent_thinking", on]))
	c.exhibition_ended.connect(func(result: Dictionary) -> void: _events.append(["exhibition_ended", result]))
	tree.root.add_child(c)
	return c


## The same with a match started and what start announced dropped.
func _started(e: ScriptedEngine) -> MatchController:
	var c := _controller(e)
	check(c.start(CONFIG), "the match starts")
	_events.clear()
	return c


## The signals so far by name, table_changed with its reason.
func _names() -> Array[String]:
	var out: Array[String] = []
	for event in _events:
		out.append("table_changed:%s" % event[1] if event[0] == "table_changed" else String(event[0]))
	return out


func _frames(count: int) -> void:
	for i in count:
		await tree.process_frame


## The example frame view with nothing pending and `striker` to play; `changes` replaces frame
## keys, or the view's for "options" and "nominations".
func _turn(striker: int, changes: Dictionary = {}) -> Dictionary:
	var frame := _json(FRAME_VIEW)
	frame["pending"] = "none"
	frame["striker"] = striker
	frame["free_ball"] = false
	frame["view"]["nominations"] = []
	frame["view"]["options"] = [["concede_frame", "concede_match"], []]
	for key: String in changes:
		if key == "options" or key == "nominations":
			frame["view"][key] = changes[key]
		else:
			frame[key] = changes[key]
	return frame


## A stroke with no foul, and the calls given.
func _legal(calls: Array = []) -> Dictionary:
	return {"fouls": [], "penalty": 0, "miss": false, "miss_reason": "", "points": 1, "nominated": "",
			"free_ball_used": false, "spotted": [], "calls": calls}


func _judged(frame: Dictionary, judgement: Dictionary, more: Dictionary = {}) -> Dictionary:
	var answer := {"ok": true, "error": "", "judgement": judgement, "event": "none", "frame": frame}
	answer.merge(more, true)
	return answer


func _decided(frame: Dictionary, calls: Array = [], event: String = "none", more: Dictionary = {}) -> Dictionary:
	var answer := {"ok": true, "error": "", "calls": calls, "event": event, "frame": frame}
	answer.merge(more, true)
	return answer


func _standin_shot() -> Dictionary:
	return {"ok": true, "error": "", "places_cue": false, "cue": [], "nominated": "",
			"strike": {"aim_rad": 0.5, "tip_x": 0.0, "tip_y": 0.1, "elevation_rad": 0.05, "speed_m_s": 2.5},
			"why": {"kind": "pot"}, "us": 120}


## A level's stroke as opponent_take gives it.
func _level_shot(level := "beginner") -> Dictionary:
	var shot := _standin_shot()
	shot["level"] = level
	shot["meant"] = {"aim_rad": 0.51, "tip_x": 0.0, "tip_y": 0.1, "elevation_rad": 0.05, "speed_m_s": 2.4}
	shot["think"] = {"candidates": 4, "sims": 12, "steps": 900, "depth": 1, "stopped": "done", "ms": 3.5}
	return shot


func _with(base: Dictionary, more: Dictionary) -> Dictionary:
	var out := base.duplicate(true)
	out.merge(more, true)
	return out


## Frame stats with the opponent's highest break.
func _stats(highest: int) -> Array:
	var stats: Array = (_json(FRAME_VIEW)["stats"] as Array).duplicate(true)
	stats[MatchController.OPPONENT]["highest_break"] = highest
	return stats


func _typed(list: Array) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for ball: Dictionary in list:
		out.append(ball)
	return out


func _json(path: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not (parsed is Dictionary):
		check(false, "%s is missing or not an object; run scripts/sync_data.py" % path)
		return {}
	return parsed


func _last_call(e: FakeMatchEngine, method: String) -> Array:
	for i in range(e.calls.size() - 1, -1, -1):
		if e.calls[i][0] == method:
			return e.calls[i]
	check(false, "%s was never called" % method)
	return [method, null, null, null]


func _count(e: FakeMatchEngine, method: String) -> int:
	return e.called().count(method)


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


## The data of each `event` line logged since _mark_log, read from the session file.
func _logged(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append((parsed as Dictionary).get("data", {}))
	return found


## The app.error lines since _mark_log whose message contains `text`.
func _refused_with(text: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for data in _logged("match.refused"):
		if String(data.get("why", "")).contains(text):
			found.append(data)
	return found


func _errors_with(text: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for data in _logged("app.error"):
		if String(data.get("message", "")).contains(text):
			found.append(data)
	return found
