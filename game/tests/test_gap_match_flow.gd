extends TestCase
## Gaps the builders' tests left: MatchController on the real OfsMatch (they use a scripted engine)
## across matches and mid-stroke, and OfsMatch refusals around a pending choice and an abandoned
## match. Nothing here depends on how well the stand-in plays.

const REFEREE_PATH := "res://data/referee_params.json"
const STANDIN_PATH := "res://data/standin_params.json"
const IN_THE_D := Vector2(-1.2, 0.1)
const AFTER_FOUL: Array[String] = ["play", "play_again", "replace"]
## Away from every ball, into the baulk cushion: nothing is hit (S3.11(a)(vi)).
const NO_HIT := {"aim_rad": PI, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 0.2}

var _seen: Array[String] = []
var _nodes: Array[Node] = []
var _shots := 0


# spec 11.2: Quit drops the match; the opponent's pending think must not reach the next one.
func test_gap_quit_while_the_opponent_thinks_then_a_new_match() -> void:
	var c := _controller(0.05)
	check(c.start({"best_of": 1, "first_breaker": "computer", "seed": 147}), "the first match starts")
	check_eq(c.waiting(), "opponent", "the opponent is thinking about its break")
	var first_id := c.match_id()
	c.abandon()
	check(not c.is_active(), "left")
	_seen.clear()
	check(c.start({"best_of": 3, "first_breaker": "you", "seed": 148}), "a new match starts")
	# Well past the 0.045 s the old request had left.
	await tree.create_timer(0.25).timeout
	check(not _seen.has("opponent_ready"), "no stroke from the old match's opponent: %s" % [_seen])
	check(c.match_id() != first_id, "a match id of its own")
	check_eq(c.waiting(), "shot", "the person breaks the new match")
	check(c.person_to_play(), "and may play")
	check_eq(int(c.frame()["strokes"]), 0, "nothing has been played in it")
	check_eq(int(c.match_state()["config"]["best_of"]), 3, "the engine holds the new match")
	_free_all()


# spec 11.1, 11.2: a second match in one run starts from nothing the first left.
func test_gap_two_matches_in_one_run_share_nothing() -> void:
	var c := _controller(0.0)
	check(c.start({"best_of": 3, "first_breaker": "you", "seed": 11}), "the first match starts")
	_foul(c)
	check_eq(c.frame()["scores"], [0, 4], "the first match has a score")
	await _frames(4)
	check(int(c.frame()["strokes"]) >= 1, "and a stroke")
	var first_id := c.match_id()
	check(c.start({"best_of": 5, "first_breaker": "you", "seed": 12}), "a second match over it")
	var state := c.match_state()
	check(c.match_id() != first_id, "its own id")
	check_eq(state["frames"], [0, 0], "no frames won")
	check_eq(state["history"], [], "no history")
	check_eq(int(state["frame_number"]), 1, "frame 1")
	check_eq(int(state["config"]["best_of"]), 5, "its own length")
	check_eq(c.frame()["scores"], [0, 0], "no score")
	check_eq(int(c.frame()["strokes"]), 0, "no strokes")
	check_eq(c.frame()["pending"], "none", "nothing pending")
	check_eq(c.balls.size(), 22, "a full rack")
	check_eq(JSON.stringify(c.balls), JSON.stringify(c.engine.balls()), "the controller's balls are the engine's")
	check(c.placing(), "the person places the cue ball for the break")
	check_eq(c.nominated(), "", "nothing declared")
	_free_all()


# spec 10.4: after the person's foul the stand-in chooses through the real engine.
func test_gap_the_stand_ins_choice_after_the_persons_foul_is_applied() -> void:
	var c := _controller(0.0)
	var chose: Array[String] = []
	c.opponent_chose.connect(func(kind: String) -> void: chose.append(kind))
	check(c.start({"best_of": 1, "first_breaker": "you", "seed": 21}), "starts")
	_foul(c)
	check_eq(c.frame()["pending"], "after_foul", "the foul leaves a choice")
	check_eq(c.waiting(), "opponent", "for the opponent")
	check(c.strike_blocked() == "opponent", "and the person may not strike")
	await _frames(4)
	check_eq(chose.size(), 1, "the stand-in chose once")
	if chose.size() == 1:
		check(AFTER_FOUL.has(chose[0]), "one of the options: " + chose[0])
		check_eq(c.frame()["pending"], "none", "it was applied")
		var striker := int(c.frame()["striker"])
		check_eq(striker, MatchController.OPPONENT if chose[0] == "play" else MatchController.PERSON,
				"and the right player is to play")
	_free_all()


# spec 11.1: with a stroke in play nothing is conceded; the stroke is still judged.
func test_gap_no_concession_while_a_stroke_is_in_play() -> void:
	var c := _controller(0.0)
	check(c.start({"best_of": 3, "first_breaker": "you", "seed": 31}), "starts")
	c.place_cue(IN_THE_D)
	c.confirm_cue()
	var shot_id := _next_shot_id()
	var record := Physics.sim.simulate(c.balls, c.cue_ball, NO_HIT, 7, {"shot_id": shot_id, "mode": "match",
			"strike_mode": "harness"})
	check(record != null, "simulated: " + Physics.sim.last_error())
	c.shot_started(shot_id)
	check(not c.concede_match(), "the match is not conceded mid-stroke")
	check(not c.concede_frame(), "nor the frame")
	check(c.is_active(), "the match is on")
	check_eq(c.strike_blocked(), "playing", "the stroke is in play")
	var judgement := c.shot_finished(record)
	check(not judgement.is_empty(), "the stroke is judged")
	check_eq(c.frame()["scores"], [0, 4], "as the foul it was")
	_free_all()


# docs/rules/README.md (S4.2): concede_match only with nothing pending; Quit is the way out then.
func test_gap_binding_concede_match_waits_for_the_pending_choice() -> void:
	var m := _fouled(41)
	var before := m.match_state()
	for player in 2:
		var answer := m.decide("concede_match", player)
		check_eq(answer.get("ok"), false, "player %d may not concede the match with a choice pending" % player)
		check(not m.last_error().is_empty(), "with a reason")
	check_eq(m.match_state(), before, "nothing changed")
	check_eq(m.decide("play", 1).get("ok"), true, "the choice")
	var conceded := m.decide("concede_match", 0)
	check_eq(conceded.get("ok"), true, "then the match may be conceded: %s" % conceded.get("error"))
	check_eq(conceded.get("event"), "match_ended", "which ends it")
	check_eq(int(m.match_state()["winner"]), 1, "for the other player")
	check(not m.in_progress(), "and it is over")


# OfsMatch: an abandoned match refuses every call that would act on it, and start is still open.
func test_gap_binding_an_abandoned_match_refuses_everything() -> void:
	var m := _fouled(42)
	var record := Physics.sim.predict(m.balls(), m.cue_ball(), NO_HIT)
	m.abandon()
	check(not m.in_progress(), "abandoned")
	var state := m.match_state()
	check_eq(state["over"], true, "over")
	check_eq(int(state["winner"]), -1, "with no winner")
	var answers: Dictionary = {
		"place_cue": m.place_cue(IN_THE_D.x, IN_THE_D.y),
		"judge": m.judge(record, {}, {"shot_id": "late"}),
		"decide": m.decide("play", 1),
		"next_frame": m.next_frame(),
		"standin_shot": m.standin_shot(1, {"shot_id": "late"}),
	}
	for name: String in answers:
		var answer: Dictionary = answers[name]
		check_eq(answer.get("ok"), false, name + " is refused")
		check(str(answer.get("error")).contains("abandoned"), "%s: because it was abandoned: %s" % [name,
				answer.get("error")])
	check_eq(m.standin_choice(1, 3), "", "no stand-in choice")
	check_eq(m.nearest_cue_placement(IN_THE_D.x, IN_THE_D.y), {}, "no nearest place")
	check_eq(m.match_state(), state, "the abandoned match is as it was left")
	m.abandon()
	check_eq(m.match_state(), state, "abandoning twice changes nothing")
	check_eq(m.start({"best_of": 1, "first_breaker": 0, "seed": 1, "concede_any_time": [true, false]},
			"after-abandon").get("ok"), true, "a new match starts")
	check(m.in_progress(), "and is on")


# OfsMatch.start twice with one config: the same rack and state, whatever the first match did.
func test_gap_binding_start_twice_gives_the_same_frame() -> void:
	var m := _fouled(43)
	var config := {"best_of": 3, "first_breaker": 0, "seed": 43, "concede_any_time": [true, false]}
	var first := m.start(config, "again-1")
	var second := m.start(config, "again-2")
	check_eq(first.get("ok"), true, "started")
	check_eq(second.get("ok"), true, "started again")
	check_eq(first.get("match"), second.get("match"), "the same match state")
	check_eq(int(m.frame()["strokes"]), 0, "no strokes carried over")
	check_eq(m.frame()["scores"], [0, 0], "no score carried over")
	check_eq(m.frame()["original"]["balls"], [], "no kept position")


func _controller(pace: float) -> MatchController:
	var c := MatchController.new()
	c.pace = pace
	c.opponent_ready.connect(func(_shot: Dictionary) -> void: _seen.append("opponent_ready"))
	c.turn_started.connect(func(_player: int, _person: bool) -> void: _seen.append("turn_started"))
	c.choice_needed.connect(func(_options: Array[String]) -> void: _seen.append("choice_needed"))
	tree.root.add_child(c)
	_nodes.append(c)
	return c


## The person, breaking from IN_THE_D, plays a stroke that hits nothing.
func _foul(c: MatchController) -> void:
	check(c.place_cue(IN_THE_D), "the cue ball is placed")
	c.confirm_cue()
	check_eq(c.strike_blocked(), "", "the person may strike")
	var shot_id := _next_shot_id()
	var record := Physics.sim.simulate(c.balls, c.cue_ball, NO_HIT, 7, {"shot_id": shot_id, "mode": "match",
			"strike_mode": "harness"})
	check(record != null, "simulated: " + Physics.sim.last_error())
	c.shot_started(shot_id)
	var judgement := c.shot_finished(record)
	check(not judgement.is_empty(), "judged")


func _fouled(seed: int) -> OfsMatch:
	var m := OfsMatch.new()
	check(m.configure(Physics.sim, FileAccess.get_file_as_string(REFEREE_PATH),
			FileAccess.get_file_as_string(STANDIN_PATH)), "configure: " + m.last_error())
	check_eq(m.start({"best_of": 3, "first_breaker": 0, "seed": seed, "concede_any_time": [true, false]},
			"gap-%d" % seed).get("ok"), true, "started")
	check_eq(m.place_cue(IN_THE_D.x, IN_THE_D.y).get("ok"), true, "placed")
	var record := Physics.sim.simulate(m.balls(), m.cue_ball(), NO_HIT, 7, {"shot_id": "gap-foul-%d" % seed,
			"mode": "match", "strike_mode": "harness"})
	check_eq(m.judge(record, {"nominated": ""}, {"shot_id": "gap-foul-%d" % seed}).get("ok"), true, "judged")
	check_eq(m.frame()["pending"], "after_foul", "a choice is pending")
	return m


func _next_shot_id() -> String:
	_shots += 1
	return "gap-shot-%d" % _shots


func _frames(count: int) -> void:
	for i in count:
		await tree.process_frame


func _free_all() -> void:
	for node in _nodes:
		if is_instance_valid(node):
			node.free()
	_nodes.clear()
