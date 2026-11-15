extends TestCase
## Ball in hand against the spec (8.6): the cue ball is placed by dragging it within the D, only
## ever at a legal place, and a Confirm button commits it. CuePlacer is fed drags as the table scene
## feeds them; MatchController runs on a scripted engine in place of OfsMatch, with frames made
## from the binding's frame view (res://data/rules/example_frame_view.json). The legal places are
## the engine's: here a stand-in for it clamps to the D of spec 6.2.

const FRAME_VIEW_PATH := "res://data/rules/example_frame_view.json"
const JUDGEMENT_PATH := "res://data/rules/example_judgement.json"
const MATCH_PATH := "res://data/rules/example_match.json"
const STRIKE := {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.0}
const IDLE_OPTIONS := [["concede_frame", "concede_match"], ["concede_match"]]
const PERSON := MatchController.PERSON
const OPPONENT := MatchController.OPPONENT
const D_CENTRE := Vector2(-1.0475, 0.0)
const D_RADIUS := 0.292
## Where the engine lists a cue ball in hand: somewhere legal in the D.
const SUGGESTED := Vector2(-1.25, 0.05)
const SAFE := Rect2(124.0, 0.0, 1664.0, 838.0)


## FakeMatchEngine with two things OfsMatch does that it leaves out: the nearest legal place is
## not always the place asked for, and a cue ball that is not in hand cannot be placed.
class SpecEngine extends FakeMatchEngine:
	var nearest := Callable()

	func nearest_cue_placement(x: float, y: float) -> Dictionary:
		if not nearest.is_valid():
			return super(x, y)
		var legal: Vector2 = nearest.call(Vector2(x, y))
		return {"x": legal.x, "y": legal.y}

	func place_cue(x: float, y: float) -> Dictionary:
		if not bool(frame_view.get("cue_in_hand", false)):
			calls.append(["place_cue", x, y])
			return {"ok": false, "error": "the cue ball is not in hand", "x": x, "y": y}
		var answer := super(x, y)
		# The frame view lists the balls too.
		if bool(answer["ok"]):
			frame_view["balls"] = ball_list.duplicate(true)
		return answer


var _engine: SpecEngine
var _seen: Array[Array] = []
var _nodes: Array[Node] = []


func test_8_6_a_drag_shows_the_cue_ball_only_where_it_was_told_is_legal() -> void:
	# spec 8.6: placement is limited to legal spots
	var placer := _placer()
	var asked: Array[Vector2] = []
	var moved: Array[Vector2] = []
	var only_legal_place := Vector2(-1.3, 0.07)
	placer.moved.connect(func(xy: Vector2) -> void: moved.append(xy))
	var rule := func(xy: Vector2) -> Vector2:
		asked.append(xy)
		return only_legal_place
	placer.begin(rule)
	check(placer.is_placing(), "placing once begun")
	for xy: Vector2 in [Vector2(0.5, 0.3), Vector2(-1.2, -0.1), Vector2(1.6, -0.8)]:
		var before := moved.size()
		placer.drag(xy)
		check(asked.size() > 0 and asked[-1] == xy, "the drag at %s is put to the legal-place rule" % xy)
		check(moved.size() > before, "the drag at %s moves the cue ball" % xy)
	for xy: Vector2 in moved:
		check_eq(xy, only_legal_place, "the cue ball shows only at the place the rule gave")
	_free_all()


func test_8_6_a_drag_anywhere_on_the_table_keeps_the_cue_ball_in_the_d() -> void:
	# spec 8.6: the cue ball is placed by dragging it within the D; inside or on the D
	var placer := _placer()
	var moved: Array[Vector2] = []
	placer.moved.connect(func(xy: Vector2) -> void: moved.append(xy))
	placer.begin(_into_d)
	var drags: Array[Vector2] = [Vector2(-1.2, 0.1), Vector2(0.5, 0.3), Vector2(-1.7, 0.0), Vector2(-1.0, 0.6),
			Vector2(1.4605, 0.0), Vector2(-1.1, -0.8)]
	for xy: Vector2 in drags:
		var before := moved.size()
		placer.drag(xy)
		check(moved.size() == before + 1 and moved[-1].is_equal_approx(_into_d(xy)),
				"a drag at %s shows the cue ball at the legal place nearest it" % xy)
	for xy: Vector2 in moved:
		check(xy.x <= D_CENTRE.x + 1e-9 and xy.distance_to(D_CENTRE) <= D_RADIUS + 1e-9, "%s is inside or on the D" % xy)
	check(moved.size() > 0 and moved[0].is_equal_approx(Vector2(-1.2, 0.1)), "a place already in the D is kept as it is")
	_free_all()


func test_8_6_lifting_the_finger_drops_the_cue_ball_at_a_legal_place() -> void:
	# spec 8.6: placement is limited to legal spots
	var placer := _placer()
	var dropped: Array[Vector2] = []
	placer.dropped.connect(func(xy: Vector2) -> void: dropped.append(xy))
	placer.begin(_into_d)
	placer.drag(Vector2(0.2, 0.2))
	placer.drop(Vector2(0.4, 0.5))
	check_eq(dropped.size(), 1, "one drop")
	if dropped.size() == 1:
		check(dropped[0].is_equal_approx(_into_d(Vector2(0.4, 0.5))), "at the legal place nearest the finger: %s" % dropped[0])
		check(dropped[0].distance_to(D_CENTRE) <= D_RADIUS + 1e-9, "which is in the D")
	check(placer.is_placing(), "still placing: only Confirm commits")
	_free_all()


func test_8_6_the_cue_ball_follows_a_drag_only_while_placing() -> void:
	# spec 8.6: ball in hand is the only time the cue ball is dragged
	var placer := _placer()
	var moved: Array[Vector2] = []
	placer.moved.connect(func(xy: Vector2) -> void: moved.append(xy))
	check(not placer.is_placing(), "not placing before it begins")
	placer.drag(Vector2(-1.2, 0.0))
	check_eq(moved.size(), 0, "a drag before placing begins moves nothing")
	placer.begin(_into_d)
	placer.drag(Vector2(-1.2, 0.0))
	check_eq(moved.size(), 1, "a drag while placing moves the cue ball")
	placer.end()
	check(not placer.is_placing(), "not placing once it ends")
	placer.drag(Vector2(-1.3, 0.1))
	check_eq(moved.size(), 1, "a drag after the cue ball left the hand moves nothing")
	check(placer.button("confirm") == null and placer.button("move") == null, "and no button is left showing")
	_free_all()


func test_8_6_a_confirm_button_commits_the_position() -> void:
	# spec 8.6: a confirm button commits the position
	var placer := _placer()
	var got: Array[String] = []
	var moved: Array[Vector2] = []
	placer.confirmed.connect(func() -> void: got.append("confirmed"))
	placer.move_requested.connect(func() -> void: got.append("move"))
	placer.moved.connect(func(xy: Vector2) -> void: moved.append(xy))
	check(placer.button("confirm") == null, "no Confirm button when the cue ball is not in hand")
	placer.begin(_into_d)
	await tree.process_frame
	var confirm := placer.button("confirm")
	check(confirm != null and confirm.is_visible_in_tree() and not confirm.disabled, "a Confirm button shows while placing")
	placer.drag(Vector2(-1.2, 0.1))
	check_eq(got, [], "dragging confirms nothing")
	_press(confirm, "Confirm")
	check_eq(got, ["confirmed"], "the button confirms, once")
	check(not placer.is_placing(), "the cue ball no longer follows a drag")
	var before := moved.size()
	placer.drag(Vector2(-1.3, -0.1))
	check_eq(moved.size(), before, "a drag after Confirm moves nothing")
	check(placer.button("confirm") == null, "the Confirm button has gone")
	# The contract's way back while the cue ball is still in hand.
	_press(placer.button("move"), "Move")
	check_eq(got, ["confirmed", "move"], "Move asks to place again")
	_free_all()


func test_8_6_after_the_opponents_in_off_the_person_places_the_cue_ball() -> void:
	# spec 8.6: after an in-off, the cue ball is placed by dragging it within the D
	var m: MatchController = await _to_the_persons_cue_in_hand()
	check_eq(_last("turn_started"), [PERSON, true], "the person's turn")
	check(m.placing(), "the cue ball is theirs to place")
	check_eq(m.strike_blocked(), "placing", "and no stroke until it is confirmed")
	_free_all()


func test_8_6_the_break_off_is_placed_the_same_way() -> void:
	# spec 9: break-off from the D
	var m := _match(_in_hand(_frame()))
	check(_start(m), "the match starts")
	check(m.placing(), "the breaker places the cue ball")
	check_eq(m.strike_blocked(), "placing", "and no stroke until it is confirmed")
	_free_all()


func test_8_6_the_stroke_waits_for_the_confirm() -> void:
	# spec 8.6: a confirm button commits the position
	var m := _match(_in_hand(_frame()))
	check(_start(m), "the match starts")
	check(m.place_cue(Vector2(-1.2, 0.1)), "the cue ball is put down")
	check(m.placing(), "putting it down does not commit it")
	check_eq(m.strike_blocked(), "placing", "the stroke still waits")
	check(m.place_cue(Vector2(-1.3, -0.05)), "it may be moved again")
	check(m.placing(), "and is still not committed")
	m.confirm_cue()
	check(not m.placing(), "Confirm commits the position")
	check_eq(m.strike_blocked(), "", "and the person aims and plays")
	check_eq(_cue_xy(m), Vector2(-1.3, -0.05), "from where the cue ball was last put")
	_free_all()


func test_8_6_confirming_without_a_drag_keeps_the_place_the_engine_suggested() -> void:
	# spec 8.6: a confirm button commits the position
	var m := _match(_in_hand(_frame()))
	check(_start(m), "the match starts")
	m.confirm_cue()
	check(not m.placing(), "committed")
	check_eq(m.strike_blocked(), "", "the person plays")
	check_eq(_cue_xy(m), SUGGESTED, "from the place it was listed at")
	_free_all()


func test_8_6_a_legal_place_moves_the_cue_ball() -> void:
	# spec 8.6: the cue ball is placed by dragging it
	var m := _match(_in_hand(_frame()))
	check(_start(m), "the match starts")
	var version := m.version()
	var before := _seen.size()
	check(m.place_cue(Vector2(-1.2, 0.1)), "a legal place is taken")
	var placed := _asked("place_cue")
	check(placed.size() == 1 and Vector2(float(placed[0][1]), float(placed[0][2])) == Vector2(-1.2, 0.1),
			"the engine is asked for that place: %s" % [placed])
	check_eq(_cue_xy(m), Vector2(-1.2, 0.1), "the cue ball is there")
	var table := _last_from("table_changed", before)
	check(table.size() == 3 and table[2] == "cue", "the table is told the cue ball was placed: %s" % [_names(before)])
	if table.size() == 3:
		check_eq(_cue_in(table[0], table[1]), Vector2(-1.2, 0.1), "with the cue ball at its new place")
	check(m.version() > version, "the balls count as changed")
	_free_all()


func test_8_6_a_place_the_engine_refuses_leaves_the_cue_ball_where_it_was() -> void:
	# spec 8.6: placement is limited to legal spots: inside or on the D, not overlapping balls
	var m := _match(_in_hand(_frame()))
	check(_start(m), "the match starts")
	check(m.place_cue(Vector2(-1.2, 0.1)), "a legal place first")
	_engine.cue_legal = false
	var before := _seen.size()
	# On top of the brown: only the engine knows.
	check(not m.place_cue(Vector2(-1.0475, 0.0)), "the place the engine refuses is not taken")
	check_eq(_asked("place_cue").size(), 2, "the engine was asked")
	check_eq(_cue_xy(m), Vector2(-1.2, 0.1), "the cue ball stays where it was")
	for event: Array in _seen.slice(before):
		if event[0] == "table_changed":
			check_eq(_cue_in(event[1], event[2]), Vector2(-1.2, 0.1), "the table is not shown it anywhere else")
	check(m.placing(), "still placing")
	_free_all()


func test_8_6_the_nearest_legal_place_is_the_engines() -> void:
	# spec 8.6: limited to legal spots; spec 9.2: the match UI goes through the one engine
	var m := _match(_in_hand(_frame()))
	_engine.nearest = _into_d
	check(_start(m), "the match starts")
	for xy: Vector2 in [Vector2(0.5, 0.3), Vector2(-1.7, 0.0), Vector2(-1.0, 0.6), Vector2(-1.2, 0.1)]:
		check(m.nearest_cue_placement(xy).is_equal_approx(_into_d(xy)), "%s goes to the engine's nearest legal place" % xy)
	_free_all()


func test_8_6_drag_drop_and_confirm_place_the_cue_ball_for_the_stroke() -> void:
	# spec 8.6, the whole of it: dragged within the D, limited to legal spots, committed by Confirm
	var m := _match(_in_hand(_frame()))
	_engine.nearest = _into_d
	check(_start(m), "the match starts")
	var placer := _placer()
	# The table scene's wiring.
	placer.dropped.connect(func(xy: Vector2) -> void: m.place_cue(xy))
	placer.confirmed.connect(m.confirm_cue)
	placer.move_requested.connect(m.move_cue_again)
	placer.begin(m.nearest_cue_placement)
	await tree.process_frame
	var finger := Vector2(0.3, 0.45)
	placer.drag(finger)
	placer.drop(finger)
	var placed := _asked("place_cue")
	check_eq(placed.size(), 1, "the drop is committed to the engine")
	if placed.size() == 1:
		check(Vector2(float(placed[0][1]), float(placed[0][2])).is_equal_approx(_into_d(finger)), "at the legal place: %s" % [placed[0]])
	var cue: Variant = _cue_xy(m)
	check(cue is Vector2 and (cue as Vector2).is_equal_approx(_into_d(finger)), "the cue ball is in the D: %s" % [cue])
	check(m.placing(), "not committed before Confirm")
	_press(placer.button("confirm"), "Confirm")
	check(not m.placing(), "Confirm commits it")
	check_eq(m.strike_blocked(), "", "and the person plays")
	_press(placer.button("move"), "Move")
	check(m.placing(), "Move goes back to placing while the cue ball is in hand")
	check_eq(m.strike_blocked(), "placing", "and the stroke waits again")
	_free_all()


func test_8_6_once_the_stroke_is_played_the_cue_ball_is_no_longer_in_hand() -> void:
	# spec 8.6: ball in hand lasts until the stroke
	var m := _match(_in_hand(_frame()))
	check(_start(m), "the match starts")
	check(m.place_cue(Vector2(-1.2, 0.1)), "placed")
	m.confirm_cue()
	_will_judge(_frame({"break_points": 1, "scores": [25, 31]}))
	_stroke(m, "p1")
	check(not m.placing(), "nothing to place for the next stroke")
	m.move_cue_again()
	check(not m.placing(), "nor can placing be asked for again")
	check(not m.place_cue(Vector2(-1.3, 0.0)), "a cue ball in play cannot be placed")
	check(_cue_xy(m) != Vector2(-1.3, 0.0), "and stays where the stroke left it")
	check_eq(m.strike_blocked(), "", "the person plays on")
	_free_all()


func test_8_6_the_opponents_break_off_never_puts_the_person_into_placing() -> void:
	# spec 8.6, 10.5: the opponent places its own cue ball
	var m := _match(_in_hand(_frame({"striker": OPPONENT})))
	var shot := _standin_shot()
	shot["places_cue"] = true
	shot["cue"] = [-1.2, 0.1]
	_engine.standin_shots.append(shot)
	check(_start(m), "the match starts")
	check(not m.placing(), "the person is not placing on the opponent's turn")
	check_eq(m.strike_blocked(), "opponent", "their stroke is blocked as the opponent's, not for placing")
	check_eq(m.harness_state().get("placing"), false, "the harness sees no placing either")
	await _frames()
	check_eq(_last("opponent_ready").size(), 1, "the opponent's stroke arrives")
	check(not m.placing(), "still not placing once the opponent is ready")
	# The table scene puts the opponent's cue ball where its stroke says, by the same legal-place rule.
	check(m.place_cue(Vector2(-1.2, 0.1)), "the opponent's cue ball is placed for it")
	check_eq(_asked("place_cue").size(), 1, "through the engine")
	check_eq(_cue_xy(m), Vector2(-1.2, 0.1), "where its stroke says")
	check(not m.placing(), "which still leaves the person nothing to place")
	check_eq(m.strike_blocked(), "opponent", "it is the opponent's stroke throughout")
	_free_all()


func test_8_6_the_persons_in_off_gives_the_opponent_the_cue_ball_not_the_person() -> void:
	# spec 8.6, 10.4
	var m := _match(_frame())
	check(_start(m), "the match starts")
	_engine.standin_choices.append("play")
	_engine.standin_shots.append(_standin_shot())
	_will_decide(_in_hand(_frame({"striker": OPPONENT})))
	_will_judge(_in_hand(_frame({"pending": "after_foul", "decider": OPPONENT, "striker": OPPONENT, "offender": PERSON},
			{"options": [[], ["play", "play_again"]]})), "none", _in_off_judgement())
	_stroke(m, "p1")
	check(not m.placing(), "the person is not placing while the opponent decides")
	await _frames(12)
	check_eq(_last("turn_started"), [OPPONENT, false], "the opponent's turn")
	check(not m.placing(), "the person is not placing on the opponent's turn")
	check(m.strike_blocked() != "placing", "nor blocked for placing: %s" % m.strike_blocked())
	_free_all()


func test_8_6_putting_the_opponent_back_in_leaves_the_cue_ball_with_them() -> void:
	# spec 8.6, 9: the offender plays again, from hand
	var m: MatchController = await _to_the_persons_choice_after_an_in_off()
	_will_decide(_in_hand(_frame({"striker": OPPONENT})))
	_engine.standin_shots.append(_standin_shot())
	check(m.choose("play_again"), "the opponent is put back in")
	check(not m.placing(), "the person is not placing")
	check(m.strike_blocked() != "placing", "nor blocked for placing: %s" % m.strike_blocked())
	_free_all()


## The opponent has gone in-off and the person is asked what to do about it.
func _to_the_persons_choice_after_an_in_off() -> MatchController:
	var m := _match(_frame({"striker": OPPONENT}))
	_engine.standin_shots.append(_standin_shot())
	check(_start(m), "the match starts")
	await _frames()
	_will_judge(_in_hand(_frame({"pending": "after_foul", "decider": PERSON, "striker": PERSON, "offender": OPPONENT,
			"last_penalty": 4, "scores": [28, 31]}, {"options": [["play", "play_again"], []]})), "none", _in_off_judgement())
	_stroke(m, "o1")
	check_eq(_last("choice_needed"), [["play", "play_again"]], "the person is asked, with exactly the engine's options")
	return m


## ...and has chosen to play: their turn, the cue ball in hand.
func _to_the_persons_cue_in_hand() -> MatchController:
	var m: MatchController = await _to_the_persons_choice_after_an_in_off()
	_will_decide(_in_hand(_frame()))
	check(m.choose("play"), "the person takes the table")
	return m


func _placer() -> CuePlacer:
	var placer := CuePlacer.new()
	tree.root.add_child(placer)
	_nodes.append(placer)
	placer.place_in(SAFE)
	return placer


## A stand-in for the engine's nearest legal place: on or behind the baulk line and within the D.
func _into_d(xy: Vector2) -> Vector2:
	var p := Vector2(minf(xy.x, D_CENTRE.x), xy.y)
	var out := p - D_CENTRE
	if out.length() > D_RADIUS:
		p = D_CENTRE + out.normalized() * D_RADIUS
	return p


## Where the controller has the cue ball; null when its balls don't hold one.
func _cue_xy(m: MatchController) -> Variant:
	return _cue_in(m.balls, m.cue_ball)


## The ball at `place` of a ball list as a point; null when there is none.
func _cue_in(balls: Array, place: int) -> Variant:
	if place < 0 or place >= balls.size():
		return null
	var cue: Dictionary = balls[place]
	return Vector2(float(cue.get("x", NAN)), float(cue.get("y", NAN)))


func _match(frame: Dictionary) -> MatchController:
	_free_all()
	_seen.clear()
	_engine = SpecEngine.new()
	_engine.frame_view = frame
	_engine.ball_list = (frame.get("balls", []) as Array).duplicate(true)
	_engine.cue_ball_place = int(frame.get("cue_ball", 0))
	_engine.match_view = _fixture(MATCH_PATH)
	_engine.match_view.erase("frame")
	var m := MatchController.new()
	m.engine = _engine
	m.pace = 0.0
	tree.root.add_child(m)
	_nodes.append(m)
	_watch(m)
	return m


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


## `frame` with the cue ball in hand, listed where the engine would suggest.
func _in_hand(frame: Dictionary) -> Dictionary:
	frame["cue_in_hand"] = true
	var balls: Array = frame.get("balls", [])
	var cue := int(frame.get("cue_ball", 0))
	if cue < balls.size():
		(balls[cue] as Dictionary).merge({"x": SUGGESTED.x, "y": SUGGESTED.y}, true)
	return frame


## The cue ball in a pocket, as OfsMatch.judge hands the judgement over.
func _in_off_judgement() -> Dictionary:
	var judgement := _fixture(JUDGEMENT_PATH)
	judgement.erase("post")
	judgement.merge({"fouls": [{"kind": "in_off", "clause": "S3.11(a)(vii)", "value": 4, "ball": -1}], "miss": false,
			"miss_reason": "", "calls": [{"kind": "foul", "value": 4, "detail": "in_off"}]}, true)
	return judgement


func _standin_shot() -> Dictionary:
	return {"ok": true, "error": "", "places_cue": false, "cue": [-0.4, 0.1], "nominated": "",
			"strike": {"aim_rad": 0.3, "tip_x": 0.0, "tip_y": -0.2, "elevation_rad": 0.0, "speed_m_s": 2.0},
			"why": "pot", "us": 180}


## Queues the engine's answer to the next stroke: the frame after it and what it brought.
func _will_judge(frame: Dictionary, event: String = "none", judgement: Dictionary = {}) -> void:
	var legal := {"fouls": [], "penalty": 0, "miss": false, "miss_reason": "", "points": 0, "nominated": "",
			"free_ball_used": false, "spotted": [], "calls": []}
	_engine.judgements.append({"ok": true, "error": "", "judgement": judgement if not judgement.is_empty() else legal,
			"event": event, "frame": frame, "balls": frame.get("balls", [])})


## Queues the engine's answer to the next decision.
func _will_decide(frame: Dictionary) -> void:
	_engine.decisions.append({"ok": true, "error": "", "calls": [], "event": "none", "frame": frame,
			"balls": frame.get("balls", [])})


## One stroke as the table scene reports it: struck, then stopped.
func _stroke(m: MatchController, shot_id: String) -> Dictionary:
	var balls: Array[Dictionary] = []
	for ball: Dictionary in _engine.ball_list:
		balls.append(ball)
	var record: OfsShotRecord = Physics.sim.simulate(balls, _engine.cue_ball_place, STRIKE, 147,
			{"shot_id": shot_id, "mode": "test", "strike_mode": "harness"})
	check(record != null, "a shot record for %s" % shot_id)
	m.shot_started(shot_id)
	return m.shot_finished(record)


## Long enough for whatever is due "on the next frame" at pace 0.
func _frames(count: int = 6) -> void:
	for i: int in count:
		await tree.process_frame


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
	for i: int in range(from, _seen.size()):
		out.append(String(_seen[i][0]))
	return out


## The arguments of the last `name` seen; empty when there was none.
func _last(name: String) -> Array:
	return _last_from(name, 0)


func _last_from(name: String, from: int) -> Array:
	for i: int in range(_seen.size() - 1, from - 1, -1):
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
