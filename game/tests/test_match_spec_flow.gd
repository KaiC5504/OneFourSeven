extends TestCase
## A match's flow against the spec (9.2, 10.5, 11.1, 11.2), through MatchController with a scripted
## engine in place of OfsMatch. Each test plays the table scene's part: start, shot_started and
## shot_finished for every stroke, choose, next_frame. Frames are the binding's frame view
## (res://data/rules/example_frame_view.json) with the fields a case needs changed; nothing here
## knows the rules. The opponent waits no time (pace 0) except where the pause is the point.

const FRAME_VIEW_PATH := "res://data/rules/example_frame_view.json"
const JUDGEMENT_PATH := "res://data/rules/example_judgement.json"
const MATCH_PATH := "res://data/rules/example_match.json"
const STRIKE := {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.0}
## What the engine offers with nothing pending (docs/rules/README.md): conceding, which lives in
## the pause menu and is no prompt.
const IDLE_OPTIONS := [["concede_frame", "concede_match"], ["concede_match"]]
const PERSON := MatchController.PERSON
const OPPONENT := MatchController.OPPONENT


## FakeMatchEngine, except that next_frame refuses unless a frame is waiting to be racked, as
## OfsMatch does (the fake always agrees).
class SpecEngine extends FakeMatchEngine:
	## What next_frame racks, first in first out: {frame, match}.
	var racks: Array[Dictionary] = []

	func next_frame() -> Dictionary:
		calls.append(["next_frame"])
		if racks.is_empty():
			return {"ok": false, "error": "no frame to rack"}
		var rack: Dictionary = racks.pop_front()
		frame_view = (rack["frame"] as Dictionary).duplicate(true)
		ball_list = (frame_view.get("balls", []) as Array).duplicate(true)
		match_view = (rack["match"] as Dictionary).duplicate(true)
		return {"ok": true, "error": "", "frame": frame()}


var _engine: SpecEngine
## Every signal the controller sent, in order: [name, args...].
var _seen: Array[Array] = []
var _nodes: Array[Node] = []


func test_11_1_a_match_starts_with_the_table_the_frame_and_the_first_turn() -> void:
	# spec 11.1: match setup leads into the match
	var m := _match(_frame())
	check(m.start({"best_of": 5, "first_breaker": "you", "seed": 147}), "the match starts")
	check(m.is_active(), "a match is on from start()")
	var table := _last("table_changed")
	check_eq(table.size(), 3, "the table is announced")
	if table.size() == 3:
		check(table[0] == _engine.balls(), "with the frame's balls")
		check_eq(table[1], _engine.cue_ball(), "and the cue ball's place")
		check_eq(table[2], "frame", "as a newly racked frame")
	check(m.balls == _engine.balls(), "the controller's balls are the frame's")
	var frame := _last("frame_changed")
	check(frame.size() == 1 and frame[0] == _engine.frame(), "the frame view is announced")
	check(m.frame() == _engine.frame(), "frame() is the engine's frame view")
	check_eq(_last("turn_started"), [PERSON, true], "then the first turn, here the person's")
	check(_index("turn_started") > _index("table_changed") and _index("turn_started") > _index("frame_changed"),
			"the turn starts once the table and the frame are known: %s" % [_names()])
	check(m.person_to_play(), "the person may play")
	_free_all()


func test_11_1_match_length_and_who_breaks_reach_the_engine() -> void:
	# spec 11.1: match length best of 1 to 11; who breaks first: random, you, or the computer
	var breakers := {"you": [PERSON], "computer": [OPPONENT], "random": [-1, PERSON, OPPONENT]}
	var lengths := MatchSetupScreen.LENGTHS
	for i: int in MatchSetupScreen.BREAKERS.size():
		var who := MatchSetupScreen.BREAKERS[i]
		var best_of := lengths[(i * 2 + 1) % lengths.size()]
		var m := _match(_frame({"striker": OPPONENT if who == "computer" else PERSON}))
		_engine.standin_shots.append(_standin_shot())
		check(m.start({"best_of": best_of, "first_breaker": who}), "%s: the match starts" % who)
		var started := _asked("start")
		check_eq(started.size(), 1, "%s: the engine starts one match" % who)
		if started.size() != 1:
			continue
		var config: Dictionary = started[0][1]
		check_eq(config.get("best_of"), best_of, "%s: the length chosen" % who)
		check(breakers.has(who) and config.get("first_breaker") in (breakers.get(who, []) as Array),
				"%s: the engine is told who breaks: %s" % [who, config.get("first_breaker")])
		check(config.get("seed") is int, "%s: a seed is made when none is given" % who)
		# The owner's ruling (FrameState::concede_any_time): the person may concede a frame at any
		# time, the opponent only when snookers are needed.
		check_eq(config.get("concede_any_time"), [true, false], "%s: who may concede at any time" % who)
	_free_all()


func test_9_2_every_stroke_is_judged_by_the_one_engine() -> void:
	# spec 9.2: the match UI, the AI and the tests all go through this one engine
	var m := _match(_frame())
	check(_start(m), "the match starts")
	_engine.standin_shots.append(_standin_shot())
	_will_judge(_frame({"striker": OPPONENT}))
	var mine := _record("p1")
	m.shot_started("p1")
	m.shot_finished(mine)
	await _frames()
	_will_judge(_frame())
	var theirs := _record("o1")
	m.shot_started("o1")
	m.shot_finished(theirs)
	var judged := _asked("judge")
	check_eq(judged.size(), 2, "the person's stroke and the opponent's were both judged")
	if judged.size() == 2:
		check(judged[0][1] == mine, "the person's stroke as it was recorded")
		check(judged[1][1] == theirs, "the opponent's stroke as it was recorded")
		check_eq((judged[0][3] as Dictionary).get("shot_id"), "p1", "the person's stroke by its id")
		check_eq((judged[1][3] as Dictionary).get("shot_id"), "o1", "the opponent's stroke by its id")
	_free_all()


func test_9_2_a_judged_stroke_gives_the_frame_after_it_the_calls_and_the_decisions() -> void:
	# spec 9.2: output: the post-shot state, a list of fouls/events with reasons, and the decisions
	# available to the next player. spec 11.1: foul decision prompts.
	var m := _match(_frame({"striker": OPPONENT}))
	_engine.standin_shots.append(_standin_shot())
	check(_start(m), "the match starts")
	await _frames()
	var fouled := _fixture(FRAME_VIEW_PATH)
	var judgement := _foul_judgement()
	_will_judge(fouled, "none", judgement)
	var before := _seen.size()
	var result := _stroke(m, "o1")
	check(result.get("fouls") == judgement.get("fouls") and not result.is_empty(),
			"shot_finished returns the judgement, fouls and reasons included: %s" % [result])
	check_eq(_last("calls_made"), [judgement.get("calls"), "o1"], "the referee's calls, in order, for that stroke")
	var frame := _last("frame_changed")
	check(frame.size() == 1 and frame[0] == fouled, "the frame after the stroke is announced")
	check(m.frame() == fouled, "and is the controller's frame")
	check_eq(_last("table_changed").slice(1), [_engine.cue_ball(), "shot"], "the table after the stroke")
	check_eq(_count("choice_needed", before), 1, "the person is asked once")
	check_eq(_last("choice_needed"), [["play", "play_again", "replace"]], "exactly the engine's options, in its order")
	_free_all()


func test_9_2_nothing_else_happens_until_the_person_chooses() -> void:
	# spec 9: after a foul the incoming player chooses; spec 11.2: only the decisions the game requires
	var before := _seen.size()
	var m: MatchController = await _to_the_persons_choice()
	var since_foul := _names(_index("calls_made", before))
	for name: String in ["turn_started", "opponent_ready", "opponent_chose", "frame_ended", "match_ended"]:
		check(not since_foul.has(name), "no %s before the choice: %s" % [name, since_foul])
	check_eq(m.strike_blocked(), "choice", "no stroke until the choice is made")
	check(not m.person_to_play(), "it is not yet the person's stroke")
	check_eq(m.harness_state().get("waiting"), "choice", "the harness sees the wait")
	var seen := _seen.size()
	var asked := _engine.calls.size()
	await _frames(12)
	check_eq(_names(seen), [], "nothing is announced while the person thinks")
	check_eq(_engine.calls.size(), asked, "and nothing is asked of the engine")
	_free_all()


func test_9_2_a_choice_that_was_not_offered_is_refused() -> void:
	# spec 9.2: the decisions available to the next player, and no others
	var m: MatchController = await _to_the_persons_choice()
	# Queued so that passing the choice straight to the engine would look like success.
	_will_decide(_frame())
	var seen := _seen.size()
	check(not m.choose("accept_rerack"), "a decision the engine did not offer")
	check(not m.choose("concede_frame"), "nor one that belongs to the pause menu")
	check_eq(_asked("decide").size(), 0, "the engine is not asked to apply it")
	check_eq(_names(seen), [], "nothing is announced")
	check_eq(m.strike_blocked(), "choice", "the choice is still to be made")
	_free_all()


func test_9_choosing_to_play_gives_the_person_the_table() -> void:
	# spec 9: the incoming player chooses to play from where the balls lie
	var m: MatchController = await _to_the_persons_choice()
	var played := _frame()
	_will_decide(played)
	var seen := _seen.size()
	check(m.choose("play"), "the choice is taken")
	check_eq(_asked("decide"), [["decide", "play", PERSON]], "the engine applies it for the person")
	check(m.frame() == played, "the frame is the one the engine answered with")
	check_eq(_last("turn_started"), [PERSON, true], "the person's turn")
	check_eq(_count("choice_needed", seen), 0, "they are not asked again")
	check(m.person_to_play(), "the person may play")
	check_eq(m.strike_blocked(), "", "and nothing blocks the stroke")
	_free_all()


func test_9_choosing_play_again_puts_the_opponent_back_in() -> void:
	# spec 9: ... or to have the offender play again
	var m: MatchController = await _to_the_persons_choice()
	_will_decide(_frame({"striker": OPPONENT}))
	_engine.standin_shots.append(_standin_shot())
	var seen := _seen.size()
	check(m.choose("play_again"), "the choice is taken")
	check_eq(_asked("decide"), [["decide", "play_again", PERSON]], "the engine applies it for the person")
	check_eq(_last("turn_started"), [OPPONENT, false], "the opponent's turn")
	check(not m.person_to_play(), "the person may not play")
	await _frames()
	check_eq(_count("opponent_ready", seen), 1, "and the opponent's stroke follows")
	_free_all()


func test_9_1_every_decision_left_with_the_person_is_asked_with_exactly_its_options() -> void:
	# spec 9: after a foul, the toss for a re-spotted black; spec 9.1: a re-rack offer
	for case: Array in _decisions():
		var pending: String = case[0]
		var options: Array = case[1]
		var m := _match(_frame({"striker": OPPONENT}))
		_engine.standin_shots.append(_standin_shot())
		check(_start(m), "%s: the match starts" % pending)
		await _frames()
		_will_judge(_frame({"pending": pending, "decider": PERSON, "striker": PERSON, "offender": OPPONENT},
				{"options": [options, []]}))
		var before := _seen.size()
		_stroke(m, "o1")
		check_eq(_count("choice_needed", before), 1, "%s: the person is asked once" % pending)
		check_eq(_last("choice_needed"), [options], "%s: exactly the engine's options" % pending)
		check_eq(_count("turn_started", before), 0, "%s: no turn until they choose" % pending)
		check_eq(m.strike_blocked(), "choice", "%s: no stroke until they choose" % pending)
	_free_all()


func test_10_4_after_the_persons_foul_the_opponent_chooses_and_the_person_is_not_asked() -> void:
	# spec 10.4: after a foul by the player the opponent decides: play on, put the player back in...
	var m := _match(_frame())
	check(_start(m), "the match starts")
	_engine.standin_choices.append("play_again")
	_will_decide(_frame())
	_will_judge(_frame({"pending": "after_foul", "decider": OPPONENT, "striker": OPPONENT, "offender": PERSON,
			"last_penalty": 4, "scores": [24, 35]}, {"options": [[], ["play", "play_again"]]}), "none", _foul_judgement())
	var before := _seen.size()
	_stroke(m, "p1")
	check(not m.person_to_play(), "the person waits for the opponent's choice")
	check(m.strike_blocked() != "", "and may not strike meanwhile")
	await _frames()
	check_eq(_count("choice_needed", before), 0, "the person is asked nothing")
	check_eq(_last("opponent_chose"), ["play_again"], "the opponent's choice is announced")
	var chosen := _asked("standin_choice")
	check(chosen.size() == 1 and chosen[0][1] == OPPONENT, "the stand-in chose for the opponent: %s" % [chosen])
	check_eq(_asked("decide"), [["decide", "play_again", OPPONENT]], "and the engine applied it for the opponent")
	check_eq(_last("turn_started"), [PERSON, true], "the person is put back in")
	check(m.person_to_play(), "the person may play")
	_free_all()


func test_10_4_the_opponent_may_take_the_table_itself() -> void:
	# spec 10.4: after a foul by the player: play on
	var m := _match(_frame())
	check(_start(m), "the match starts")
	_engine.standin_choices.append("play")
	_engine.standin_shots.append(_standin_shot())
	_will_decide(_frame({"striker": OPPONENT}))
	_will_judge(_frame({"pending": "after_foul", "decider": OPPONENT, "striker": OPPONENT, "offender": PERSON},
			{"options": [[], ["play", "play_again"]]}), "none", _foul_judgement())
	var before := _seen.size()
	_stroke(m, "p1")
	await _frames(12)
	check_eq(_count("choice_needed", before), 0, "the person is asked nothing")
	check_eq(_last("opponent_chose"), ["play"], "the opponent's choice is announced")
	check_eq(_last("turn_started"), [OPPONENT, false], "the opponent's turn")
	check_eq(_count("opponent_ready", before), 1, "and its stroke follows")
	_free_all()


func test_10_4_the_opponent_answers_every_decision_left_with_it() -> void:
	# spec 10.4: re-rack offers and the re-spotted black are the opponent's to answer too
	for case: Array in _decisions():
		var pending: String = case[0]
		var options: Array = case[1]
		var answer: String = options[1]
		var m := _match(_frame())
		check(_start(m), "%s: the match starts" % pending)
		_engine.standin_choices.append(answer)
		_will_decide(_frame())
		_will_judge(_frame({"pending": pending, "decider": OPPONENT, "striker": OPPONENT, "offender": PERSON},
				{"options": [[], options]}))
		var before := _seen.size()
		_stroke(m, "p1")
		await _frames()
		check_eq(_count("choice_needed", before), 0, "%s: the person is asked nothing" % pending)
		check_eq(_last("opponent_chose"), [answer], "%s: the opponent's answer is announced" % pending)
		check_eq(_asked("decide"), [["decide", answer, OPPONENT]], "%s: and applied for the opponent" % pending)
	_free_all()


func test_10_5_the_opponents_stroke_arrives_by_opponent_ready_after_a_pause() -> void:
	# spec 10.5: a natural pause before the AI strikes, so even instant decisions don't feel robotic
	var m := _match(_frame({"striker": OPPONENT}))
	var shot := _standin_shot("blue")
	_engine.standin_shots.append(shot)
	check(_start(m), "the match starts")
	check_eq(_last("turn_started"), [OPPONENT, false], "the opponent's turn, not the person's")
	check_eq(_count("opponent_ready"), 0, "its stroke does not arrive in the call that started the turn")
	check(not m.person_to_play(), "the person may not play")
	check_eq(m.strike_blocked(), "opponent", "and their stroke is blocked as the opponent's")
	await _frames()
	check_eq(_count("opponent_ready"), 1, "the stroke arrives once the pause is over")
	var ready := _last("opponent_ready")
	if ready.size() == 1:
		var got: Dictionary = ready[0]
		for key: String in ["strike", "nominated", "places_cue", "cue"]:
			check_eq(got.get(key), shot[key], "the stand-in's stroke as the engine gave it: %s" % key)
	check_eq(_asked("standin_shot").size(), 1, "the engine's stand-in was asked once")
	check_eq(m.strike_blocked(), "opponent", "it is still the opponent's stroke")
	check_eq(m.harness_state().get("waiting"), "opponent", "the harness sees whose stroke it is")
	_free_all()


func test_10_5_a_turn_that_passes_to_the_opponent_pauses_too() -> void:
	# spec 10.5
	var m := _match(_frame())
	check(_start(m), "the match starts")
	_engine.standin_shots.append(_standin_shot())
	_will_judge(_frame({"striker": OPPONENT}))
	var before := _seen.size()
	_stroke(m, "p1")
	check_eq(_last("turn_started"), [OPPONENT, false], "the opponent's turn")
	check_eq(_count("opponent_ready", before), 0, "its stroke does not arrive in the call that judged the person's")
	await _frames()
	check_eq(_count("opponent_ready", before), 1, "it arrives after the pause")
	_free_all()


func test_10_5_the_pause_is_at_least_0_6_s_at_full_pace() -> void:
	# spec 10.5: a natural pause of at least ~0.6 s
	check(MatchController.THINK_SECONDS >= 0.6, "the thinking time is at least 0.6 s: %s" % MatchController.THINK_SECONDS)
	var m := _match(_frame({"striker": OPPONENT}))
	m.pace = 1.0
	_engine.standin_shots.append(_standin_shot())
	var began := Time.get_ticks_msec()
	var started := _start(m)
	check(started, "the match starts")
	while started and _count("opponent_ready") == 0 and Time.get_ticks_msec() - began < 4000:
		await tree.process_frame
	var waited := float(Time.get_ticks_msec() - began) / 1000.0
	check_eq(_count("opponent_ready"), 1, "the stroke arrives")
	# A frame's worth under 0.6: the timer counts from the frame the turn began in.
	check(waited >= 0.55, "after at least about 0.6 s: %.3f s" % waited)
	_free_all()


func test_10_5_the_persons_turn_is_never_played_for_them() -> void:
	# spec 10.5, 11.1: the opponent plays only its own strokes
	var m := _match(_frame())
	check(_start(m), "the match starts")
	await _frames(12)
	check_eq(_count("opponent_ready"), 0, "no stroke arrives on the person's turn")
	check_eq(_asked("standin_shot").size(), 0, "the stand-in is not asked for one")
	check(m.person_to_play(), "the person may still play")
	_free_all()


func test_11_2_no_prompt_appears_when_no_decision_is_pending() -> void:
	# spec 11.2: no interruptions; no pop-ups beyond the rule decisions the game requires
	var m := _match(_frame())
	check(_start(m), "the match starts")
	await _frames()
	_engine.standin_shots.append(_standin_shot())
	_will_judge(_frame({"striker": OPPONENT}))
	_stroke(m, "p1")
	await _frames()
	_will_judge(_frame())
	_stroke(m, "o1")
	await _frames()
	check_eq(_count("choice_needed"), 0, "the person is never asked to choose")
	check_eq(_count("opponent_chose"), 0, "nor does the opponent choose anything")
	check_eq(_asked("decide").size() + _asked("standin_choice").size(), 0, "no decision is put to the engine")
	var turns: Array = []
	for event: Array in _seen:
		if event[0] == "turn_started":
			turns.append(event.slice(1))
	check_eq(turns, [[PERSON, true], [OPPONENT, false], [PERSON, true]], "play simply passes from one to the other")
	check_eq(m.strike_blocked(), "", "and the person plays on")
	_free_all()


func test_11_1_after_a_frame_a_summary_is_announced_and_play_waits() -> void:
	# spec 11.1: after each frame, a short summary
	var before := _seen.size()
	var m := _to_a_frame_summary()
	check_eq(_count("frame_ended", before), 1, "the frame's end is announced once")
	var ended := _last("frame_ended")
	if ended.size() == 2:
		check(ended[0] == _summary(2, PERSON, [69, 31]), "with the frame's summary: %s" % [ended[0]])
		check_eq((ended[1] as Dictionary).get("frames"), [1, 1], "and the match as it stands")
	check_eq(_count("match_ended", before), 0, "the match goes on")
	check(m.is_active(), "a match is still on")
	var since := _names(_index("frame_ended", before))
	for name: String in ["turn_started", "choice_needed", "opponent_ready", "opponent_chose"]:
		check(not since.has(name), "no %s after the frame ended: %s" % [name, since])
	check(not m.person_to_play(), "nobody plays")
	check(m.strike_blocked() != "", "a stroke is blocked")
	check_eq(m.harness_state().get("waiting"), "frame_summary", "the harness sees the summary")
	var seen := _seen.size()
	var asked := _engine.calls.size()
	await _frames(12)
	check_eq(_names(seen), [], "nothing more is announced until next_frame()")
	check_eq(_engine.calls.size(), asked, "and nothing is asked of the engine")
	check_eq(_asked("next_frame").size(), 0, "the next frame is not racked by itself")
	_free_all()


func test_11_1_next_frame_racks_the_frame_after_the_summary() -> void:
	# spec 11.1: after each frame a summary, then the next frame
	var m := _to_a_frame_summary()
	var next := _frame({"striker": OPPONENT, "scores": [0, 0], "break_points": 0})
	_engine.racks.append({"frame": next, "match": _match_view({"frames": [1, 1], "frame_number": 3, "breaker": OPPONENT,
			"history": [_summary(1, OPPONENT, [41, 63]), _summary(2, PERSON, [69, 31])]})})
	_engine.standin_shots.append(_standin_shot())
	var before := _seen.size()
	check(m.next_frame(), "the next frame is racked")
	check_eq(_asked("next_frame").size(), 1, "by the engine")
	check(m.frame() == next, "the frame is the new one")
	var table := _last("table_changed")
	check(table.size() == 3 and table[2] == "frame" and table[0] == _engine.balls(), "the table is announced as a new frame")
	check(_index("frame_changed", before) >= 0, "the frame view is announced")
	check_eq(_last("turn_started"), [OPPONENT, false], "the first turn is the one the engine's frame gives")
	await _frames()
	check_eq(_count("opponent_ready", before), 1, "and play goes on")
	_free_all()


func test_11_1_no_frame_is_racked_while_one_is_in_play() -> void:
	# spec 11.1: a summary after each frame, not before
	var m := _match(_frame())
	check(_start(m), "the match starts")
	var seen := _seen.size()
	check(not m.next_frame(), "there is no frame to rack")
	check_eq(_names(seen), [], "nothing is announced")
	check(m.person_to_play(), "the person plays on")
	_free_all()


func test_11_1_the_matchs_end_is_announced_once() -> void:
	# spec 11.1: at the end, a match summary
	var history := [_summary(1, OPPONENT, [41, 63]), _summary(2, PERSON, [70, 25])]
	var m := _match(_frame({"scores": [62, 31]}), _match_view({"frames": [1, 1], "frame_number": 3, "history": history}))
	check(_start(m), "the match starts")
	_will_judge(_frame({"over": true, "winner": PERSON, "end_reason": "black", "scores": [69, 31]}), "match_ended")
	_engine.match_view = _match_view({"frames": [2, 1], "frame_number": 3, "over": true, "winner": PERSON,
			"history": history + [_summary(3, PERSON, [69, 31])]})
	var before := _seen.size()
	_stroke(m, "p1")
	check_eq(_count("match_ended", before), 1, "the match's end is announced")
	var ended := _last("match_ended")
	if ended.size() == 1:
		var state: Dictionary = ended[0]
		check(state.get("over") == true and state.get("winner") == PERSON, "with the finished match: %s" % [state.get("winner")])
		check_eq(state.get("frames"), [2, 1], "and its frames")
	check_eq(_count("frame_ended", before), 1, "the last frame gets its summary too")
	check(_index("frame_ended", before) >= 0 and _index("frame_ended", before) < _index("match_ended", before),
			"the frame's end comes first: %s" % [_names(before)])
	check(not m.is_active(), "no match is on any more")
	check(not m.person_to_play(), "nobody plays")
	check_eq(m.strike_blocked(), "inactive", "a stroke is blocked for want of a match")
	await _frames(12)
	check(not m.next_frame(), "there is no frame left to rack")
	await _frames()
	check_eq(_count("match_ended", before), 1, "the end is announced once only")
	check_eq(_count("frame_ended", before), 1, "and so is the last frame's")
	for name: String in ["turn_started", "choice_needed", "opponent_ready", "opponent_chose"]:
		check_eq(_count(name, before), 0, "no %s after the match" % name)
	_free_all()


func test_11_1_conceding_the_frame_ends_it_with_a_summary() -> void:
	# spec 11.1: the pause menu's Concede frame; spec 9: conceding a frame
	var m := _match(_frame(), _match_view({"frames": [0, 0], "frame_number": 1, "history": []}))
	check(_start(m), "the match starts")
	var summary := _summary(1, OPPONENT, [24, 31], "conceded")
	_will_decide(_frame({"over": true, "winner": OPPONENT, "end_reason": "conceded"}), "frame_ended",
			[{"kind": "frame_over", "value": OPPONENT, "detail": "conceded"}])
	_engine.match_view = _match_view({"frames": [0, 1], "frame_number": 1, "history": [summary]})
	var before := _seen.size()
	check(m.concede_frame(), "the frame is conceded")
	check_eq(_asked("decide"), [["decide", "concede_frame", PERSON]], "by the person, through the engine")
	check_eq(_count("frame_ended", before), 1, "the frame's end is announced")
	var ended := _last("frame_ended")
	check(ended.size() == 2 and ended[0] == summary, "with its summary")
	check_eq(_count("match_ended", before), 0, "the match goes on")
	check(m.is_active(), "a match is still on")
	check_eq(_count("turn_started", before), 0, "and waits for next_frame()")
	_free_all()


func test_11_1_conceding_the_match_ends_it_once() -> void:
	# spec 11.1: the pause menu's Concede match; spec 9: conceding the match
	var m := _match(_frame(), _match_view({"frames": [0, 0], "frame_number": 1, "history": []}))
	check(_start(m), "the match starts")
	_will_decide(_frame({"over": true, "winner": OPPONENT, "end_reason": "match_conceded"}), "match_ended",
			[{"kind": "frame_over", "value": OPPONENT, "detail": "match_conceded"}])
	_engine.match_view = _match_view({"frames": [0, 1], "frame_number": 1, "over": true, "winner": OPPONENT,
			"history": [_summary(1, OPPONENT, [24, 31], "match_conceded")]})
	var before := _seen.size()
	check(m.concede_match(), "the match is conceded")
	check_eq(_asked("decide"), [["decide", "concede_match", PERSON]], "by the person, through the engine")
	await _frames()
	check_eq(_count("match_ended", before), 1, "the match's end is announced once")
	var ended := _last("match_ended")
	check(ended.size() == 1 and (ended[0] as Dictionary).get("winner") == OPPONENT, "to the opponent")
	check(not m.is_active(), "no match is on any more")
	check_eq(_count("turn_started", before), 0, "and nobody plays")
	_free_all()


func test_11_1_a_concession_the_engine_refuses_changes_nothing() -> void:
	# spec 11.1; the engine decides when a frame may be conceded
	var m := _match(_frame({}, {"options": [["concede_match"], ["concede_match"]]}))
	check(_start(m), "the match starts")
	var seen := _seen.size()
	check(not m.concede_frame(), "the frame is not conceded")
	check(not m.concede_match(), "nor the match, with the engine refusing")
	check_eq(_names(seen), [], "nothing is announced")
	check(m.is_active() and m.person_to_play(), "play goes on")
	_free_all()


func test_11_2_abandoning_discards_the_match() -> void:
	# spec 11.2: quitting mid-match: the match is discarded. Owner: no resume.
	var m := _match(_frame({"striker": OPPONENT}))
	_engine.standin_shots.append(_standin_shot())
	check(_start(m), "the match starts")
	check(m.is_active(), "a match is on")
	var before := _seen.size()
	m.abandon()
	check(not m.is_active(), "no match is on once abandoned")
	check_eq(_asked("abandon").size(), 1, "the engine is told")
	for name: String in _names(before):
		check(name == "table_changed" or name == "frame_changed", "abandoning announces no result, turn or prompt: %s" % name)
	var seen := _seen.size()
	await _frames(12)
	check_eq(_names(seen), [], "nothing is announced afterwards; the opponent's waiting stroke is dropped")
	check_eq(_asked("standin_shot").size(), 0, "and the stand-in is never asked for it")
	check(not m.person_to_play(), "nobody plays")
	check_eq(m.strike_blocked(), "inactive", "a stroke is blocked for want of a match")
	_will_judge(_frame())
	check_eq(_stroke(m, "late"), {}, "a stroke that stops afterwards is not judged")
	check_eq(_asked("judge").size(), 0, "the engine is not asked to")
	_will_decide(_frame())
	check(not m.choose("play") and not m.concede_frame(), "no decision is taken")
	check(not m.next_frame(), "no frame is racked")
	check_eq(_names(seen), [], "and still nothing is announced")
	# No resume: the next match is a new one.
	_set_frame(_frame())
	check(_start(m), "a new match starts")
	check_eq(_asked("start").size(), 2, "from the beginning")
	check_eq(_last("table_changed").slice(1), [_engine.cue_ball(), "frame"], "with a frame racked")
	_free_all()


func test_11_2_quit_discards_the_match_only_after_the_confirming_press() -> void:
	# spec 11.2: quitting mid-match asks to confirm, and the match is discarded
	var m := _match(_frame())
	check(_start(m), "the match starts")
	var menu := PauseMenu.new()
	menu.auto_place = false
	tree.root.add_child(menu)
	_nodes.append(menu)
	menu.quit_confirmed.connect(m.abandon)
	menu.open(true)
	_press(menu.button("quit"), "Quit")
	check(m.is_active(), "Quit alone leaves the match on")
	_press(menu.button("cancel"), "Cancel")
	check(m.is_active(), "Cancel leaves the match on")
	check_eq(_asked("abandon").size(), 0, "the engine has been told nothing")
	_press(menu.button("quit"), "Quit")
	_press(menu.button("confirm"), "the confirming press")
	check(not m.is_active(), "the confirming press discards it")
	check_eq(_asked("abandon").size(), 1, "the engine is told once")
	_free_all()


# The cases a decision can be pending in, each with what the engine would offer.
func _decisions() -> Array:
	return [
		["after_foul", ["play", "play_again"]],
		["rerack", ["accept_rerack", "refuse_rerack"]],
		["toss", ["play_first", "play_second"]],
	]


## A match brought to the person's choice after the opponent's foul and a miss (the fixture as it
## stands).
func _to_the_persons_choice() -> MatchController:
	var m := _match(_frame({"striker": OPPONENT}))
	_engine.standin_shots.append(_standin_shot())
	check(_start(m), "the match starts")
	await _frames()
	_will_judge(_fixture(FRAME_VIEW_PATH), "none", _foul_judgement())
	_stroke(m, "o1")
	return m


## A best of three at one frame all, the person having just won the second on the black.
func _to_a_frame_summary() -> MatchController:
	var m := _match(_frame({"scores": [62, 31]}))
	check(_start(m), "the match starts")
	_will_judge(_frame({"over": true, "winner": PERSON, "end_reason": "black", "scores": [69, 31]}), "frame_ended")
	_engine.match_view = _match_view({"frames": [1, 1],
			"history": [_summary(1, OPPONENT, [41, 63]), _summary(2, PERSON, [69, 31])]})
	_stroke(m, "p1")
	return m


func _match(frame: Dictionary, match_view: Dictionary = {}) -> MatchController:
	_free_all()
	_seen.clear()
	_engine = SpecEngine.new()
	_set_frame(frame)
	_engine.match_view = match_view if not match_view.is_empty() else _match_view()
	var m := MatchController.new()
	m.engine = _engine
	m.pace = 0.0
	tree.root.add_child(m)
	_nodes.append(m)
	_watch(m)
	return m


func _set_frame(frame: Dictionary) -> void:
	_engine.frame_view = frame
	_engine.ball_list = (frame.get("balls", []) as Array).duplicate(true)
	_engine.cue_ball_place = int(frame.get("cue_ball", 0))


## Starts a best of three with whoever the engine's frame has at the table breaking.
func _start(m: MatchController) -> bool:
	var person := int(_engine.frame_view.get("striker", PERSON)) == PERSON
	return m.start({"best_of": 3, "first_breaker": "you" if person else "computer", "seed": 147})


## The fixture's frame in ordinary play: the person to strike, red on, nothing pending. `changes`
## replace frame keys and `view` keys of its "view".
func _frame(changes: Dictionary = {}, view: Dictionary = {}) -> Dictionary:
	var frame := _fixture(FRAME_VIEW_PATH)
	frame.merge({"striker": PERSON, "pending": "none", "free_ball": false, "last_miss": false}, true)
	frame.merge(changes, true)
	var shown: Dictionary = frame.get("view", {})
	shown.merge({"nominations": [], "options": IDLE_OPTIONS.duplicate(true)}, true)
	shown.merge(view, true)
	frame["view"] = shown
	return frame


## The fixture's match without its frame (the engine adds the frame in play): a best of three, the
## opponent one frame up.
func _match_view(changes: Dictionary = {}) -> Dictionary:
	var view := _fixture(MATCH_PATH)
	view.erase("frame")
	view.merge(changes, true)
	return view


## A frame summary made from the fixture's.
func _summary(number: int, winner: int, scores: Array, reason: String = "black") -> Dictionary:
	var history: Array = _fixture(MATCH_PATH).get("history", [{}])
	var summary: Dictionary = history[0]
	summary.merge({"number": number, "winner": winner, "scores": scores, "reason": reason}, true)
	return summary


## The fixture's judgement as OfsMatch.judge hands it over: FOUL AND A MISS, a free ball.
func _foul_judgement() -> Dictionary:
	var judgement := _fixture(JUDGEMENT_PATH)
	judgement.erase("post")
	return judgement


func _standin_shot(nominated: String = "") -> Dictionary:
	return {"ok": true, "error": "", "places_cue": false, "cue": [-0.4, 0.1], "nominated": nominated,
			"strike": {"aim_rad": 0.3, "tip_x": 0.0, "tip_y": -0.2, "elevation_rad": 0.0, "speed_m_s": 2.0},
			"why": "pot", "us": 180}


## Queues the engine's answer to the next stroke: the frame after it and what it brought.
func _will_judge(frame: Dictionary, event: String = "none", judgement: Dictionary = {}) -> void:
	var legal := {"fouls": [], "penalty": 0, "miss": false, "miss_reason": "", "points": 0, "nominated": "",
			"free_ball_used": false, "spotted": [], "calls": []}
	_engine.judgements.append({"ok": true, "error": "", "judgement": judgement if not judgement.is_empty() else legal,
			"event": event, "frame": frame, "balls": frame.get("balls", [])})


## Queues the engine's answer to the next decision.
func _will_decide(frame: Dictionary, event: String = "none", calls: Array = []) -> void:
	_engine.decisions.append({"ok": true, "error": "", "calls": calls, "event": event, "frame": frame,
			"balls": frame.get("balls", [])})


## One stroke as the table scene reports it: struck, then stopped.
func _stroke(m: MatchController, shot_id: String) -> Dictionary:
	m.shot_started(shot_id)
	return m.shot_finished(_record(shot_id))


## A real record of a stroke from the engine's balls, carrying `shot_id`.
func _record(shot_id: String) -> OfsShotRecord:
	var balls: Array[Dictionary] = []
	for ball: Dictionary in _engine.ball_list:
		balls.append(ball)
	var record: OfsShotRecord = Physics.sim.simulate(balls, _engine.cue_ball_place, STRIKE, 147,
			{"shot_id": shot_id, "mode": "test", "strike_mode": "harness"})
	check(record != null, "a shot record for %s" % shot_id)
	return record


## Long enough for whatever is due "on the next frame" at pace 0.
func _frames(count: int = 6) -> void:
	for i: int in count:
		await tree.process_frame


## A press as the person makes it, whether the menu listens to pressed or toggled.
func _press(button: BaseButton, what: String) -> void:
	check(button != null, "%s: there is such a button" % what)
	if button == null:
		return
	check(not button.disabled, "%s can be pressed" % what)
	if button.toggle_mode:
		button.button_pressed = true
	button.pressed.emit()


func _watch(m: MatchController) -> void:
	m.table_changed.connect(_note3.bind("table_changed"))
	m.frame_changed.connect(_note1.bind("frame_changed"))
	m.calls_made.connect(_note2.bind("calls_made"))
	m.turn_started.connect(_note2.bind("turn_started"))
	m.choice_needed.connect(_note1.bind("choice_needed"))
	m.opponent_ready.connect(_note1.bind("opponent_ready"))
	m.opponent_chose.connect(_note1.bind("opponent_chose"))
	m.frame_ended.connect(_note2.bind("frame_ended"))
	m.match_ended.connect(_note1.bind("match_ended"))


func _note1(a: Variant, name: String) -> void:
	_seen.append([name, _copy(a)])


func _note2(a: Variant, b: Variant, name: String) -> void:
	_seen.append([name, _copy(a), _copy(b)])


func _note3(a: Variant, b: Variant, c: Variant, name: String) -> void:
	_seen.append([name, _copy(a), _copy(b), _copy(c)])


## The signals seen from place `from` on, by name.
func _names(from: int = 0) -> Array[String]:
	var out: Array[String] = []
	for i: int in range(maxi(from, 0), _seen.size()):
		out.append(String(_seen[i][0]))
	return out


func _count(name: String, from: int = 0) -> int:
	return _names(from).count(name)


## The place of the first `name` at or after `from`; -1 when there is none.
func _index(name: String, from: int = 0) -> int:
	for i: int in range(maxi(from, 0), _seen.size()):
		if _seen[i][0] == name:
			return i
	return -1


## The arguments of the last `name` seen; empty when there was none.
func _last(name: String) -> Array:
	for i: int in range(_seen.size() - 1, -1, -1):
		if _seen[i][0] == name:
			return _seen[i].slice(1)
	return []


## The calls made to the engine's `method`, in order: [method, args...].
func _asked(method: String) -> Array[Array]:
	var out: Array[Array] = []
	for call: Array in _engine.calls:
		if call[0] == method:
			out.append(call)
	return out


func _free_all() -> void:
	for node: Node in _nodes:
		if is_instance_valid(node):
			node.free()
	_nodes.clear()


func _fixture(path: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	check(parsed is Dictionary, "%s is there (scripts/sync_data.py copies it)" % path)
	if not parsed is Dictionary:
		return {}
	return _whole(parsed)


## JSON.parse_string makes every number a float; the binding hands whole numbers over as ints.
static func _whole(value: Variant, key: String = "") -> Variant:
	if value is Dictionary:
		var out := {}
		for k: String in (value as Dictionary):
			out[k] = _whole((value as Dictionary)[k], k)
		return out
	if value is Array:
		var items: Array = []
		for item: Variant in (value as Array):
			items.append(_whole(item, key))
		return items
	if value is float and key != "x" and key != "y":
		return int(value)
	return value


## What a signal carried, as it was then: typed arrays as plain ones, names as Strings.
static func _copy(value: Variant) -> Variant:
	if value is Dictionary:
		return (value as Dictionary).duplicate(true)
	if value is Array:
		var items: Array = []
		for item: Variant in (value as Array):
			items.append(_copy(item))
		return items
	if value is StringName:
		return String(value)
	return value
