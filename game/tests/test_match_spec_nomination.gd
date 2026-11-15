extends TestCase
## Nomination against the spec (8.7): the game pre-selects the ball the aim line hits first, a tap
## on any eligible ball changes it, the nominated ball is highlighted and the referee uses it.
## Nomination's choosing is checked on layouts worked out by hand (core metres, the aim measured
## from +x towards +y); MatchController runs on a scripted engine in place of OfsMatch, with frames
## made from the binding's frame view (res://data/rules/example_frame_view.json).

const FRAME_VIEW_PATH := "res://data/rules/example_frame_view.json"
const JUDGEMENT_PATH := "res://data/rules/example_judgement.json"
const MATCH_PATH := "res://data/rules/example_match.json"
const STRIKE := {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": 1.0}
const IDLE_OPTIONS := [["concede_frame", "concede_match"], ["concede_match"]]
const PERSON := MatchController.PERSON
const OPPONENT := MatchController.OPPONENT
## A ball's radius (spec 6.1: 52.5 mm across).
const R := 0.02625
const COLOURS := ["yellow", "green", "brown", "blue", "pink", "black"]

var _engine: FakeMatchEngine
var _seen: Array[Array] = []
var _nodes: Array[Node] = []


func test_8_7_the_first_ball_on_the_aim_line_is_pre_selected() -> void:
	# spec 8.7: the game pre-selects the ball the aim line hits first
	var aim := 0.7
	var cue := Vector2(-0.6, -0.3)
	var brown := cue + 0.5 * Vector2(cos(aim), sin(aim))
	var balls := _balls([["cue", cue.x, cue.y], ["red", 0.9, 0.2], ["brown", brown.x, brown.y], ["black", 1.4605, 0.0]])
	check_eq(Nomination.first_on_line(balls, 0, aim, R), 2, "the brown is the first ball on the line")
	check_eq(Nomination.preselect(balls, 0, aim, _eligible(), R), "brown", "and is pre-selected")
	# Straight along the table from the same place, the red at (0.9, 0.2) is half a metre off the line.
	check_eq(Nomination.preselect(balls, 0, 0.0, _eligible(), R), "", "another aim, another answer")


func test_8_7_a_first_ball_that_is_not_eligible_pre_selects_nothing() -> void:
	# spec 8.7: only an eligible ball can be the nominated one
	# A red in front of the pink on the line y = 0.3.
	var balls := _balls([["cue", -0.8, 0.3], ["pink", 0.4, 0.3], ["red", -0.3, 0.3]])
	check_eq(Nomination.first_on_line(balls, 0, 0.0, R), 2, "the red is hit first")
	check_eq(Nomination.preselect(balls, 0, 0.0, _eligible(), R), "", "so nothing is pre-selected, not the pink behind it")
	# Eligible is what the frame lists: the yellow is a colour but not one of these.
	var later := _eligible(["pink", "black"])
	balls = _balls([["cue", -0.8, 0.3], ["pink", 0.4, 0.3], ["yellow", -0.3, 0.3]])
	check_eq(Nomination.preselect(balls, 0, 0.0, later, R), "", "a colour that is not eligible pre-selects nothing")
	check_eq(Nomination.preselect(balls, 0, 0.0, _eligible(), R), "yellow", "and is pre-selected once it is")
	check_eq(Nomination.preselect(balls, 0, 0.0, _eligible([]), R), "", "with nothing eligible nothing is")


func test_8_7_no_ball_on_the_aim_line_pre_selects_nothing() -> void:
	# spec 8.7
	var balls := _balls([["cue", -0.5, 0.3], ["blue", 0.0, 0.0], ["black", 1.4605, 0.0]])
	# Across the table towards the left cushion: nothing in the way.
	check_eq(Nomination.first_on_line(balls, 0, PI / 2.0, R), -1, "no ball on the line")
	check_eq(Nomination.preselect(balls, 0, PI / 2.0, _eligible(), R), "", "nothing pre-selected")
	check_eq(Nomination.preselect(balls, 0, atan2(-0.3, 0.5), _eligible(), R), "blue", "turned onto the blue, the blue")
	var alone := _balls([["cue", 0.2, -0.4]])
	check_eq(Nomination.first_on_line(alone, 0, 1.0, R), -1, "the cue ball alone hits nothing")
	check_eq(Nomination.preselect(alone, 0, 1.0, _eligible(), R), "", "and pre-selects nothing")


func test_8_7_of_two_balls_on_the_aim_line_the_nearer_is_pre_selected() -> void:
	# spec 8.7: the ball the aim line hits first
	# Blue, pink and black on their spots down the middle, the cue ball behind the blue; listed
	# so that neither the first nor the last entry is the answer by accident.
	var balls := _balls([["black", 1.4605, 0.0], ["pink", 0.89225, 0.0], ["cue", -1.0, 0.0], ["blue", 0.0, 0.0]])
	check_eq(Nomination.first_on_line(balls, 2, 0.0, R), 3, "the blue is reached first")
	check_eq(Nomination.preselect(balls, 2, 0.0, _eligible(), R), "blue", "and is pre-selected")
	# From the top cushion end looking back down the table, the black comes first.
	balls = _balls([["blue", 0.0, 0.0], ["pink", 0.89225, 0.0], ["cue", 1.7, 0.0], ["black", 1.4605, 0.0]])
	check_eq(Nomination.first_on_line(balls, 2, PI, R), 3, "the black is reached first from the other end")
	check_eq(Nomination.preselect(balls, 2, PI, _eligible(), R), "black", "and is pre-selected")


func test_8_7_a_ball_behind_the_cue_ball_is_not_on_the_aim_line() -> void:
	# spec 8.7: the aim line runs forward from the cue ball
	var balls := _balls([["cue", 0.3, 0.0], ["blue", 0.0, 0.0], ["pink", 0.89225, 0.0]])
	check_eq(Nomination.preselect(balls, 0, 0.0, _eligible(), R), "pink", "the pink ahead, not the blue behind")
	check_eq(Nomination.preselect(balls, 0, PI, _eligible(), R), "blue", "turned round, the blue")
	balls = _balls([["cue", 0.3, 0.0], ["blue", 0.0, 0.0]])
	check_eq(Nomination.first_on_line(balls, 0, 0.0, R), -1, "aiming away from the only ball hits nothing")


func test_8_7_the_aim_line_is_as_wide_as_the_cue_ball() -> void:
	# spec 8.7: the ball the cue ball would hit, both taken as circles
	# 45 mm off the line: less than two radii (52.5 mm), so the cue ball clips it.
	var clipped := _balls([["cue", -0.5, 0.0], ["green", 0.2, 0.045]])
	check_eq(Nomination.first_on_line(clipped, 0, 0.0, R), 1, "a ball the cue ball's edge reaches is hit")
	check_eq(Nomination.preselect(clipped, 0, 0.0, _eligible(), R), "green", "and is pre-selected")
	# 60 mm off the line: the cue ball passes it.
	var passed := _balls([["cue", -0.5, 0.0], ["yellow", 0.2, -0.060]])
	check_eq(Nomination.first_on_line(passed, 0, 0.0, R), -1, "a ball the cue ball passes is not")
	check_eq(Nomination.preselect(passed, 0, 0.0, _eligible(), R), "", "and pre-selects nothing")


func test_8_7_the_first_ball_is_the_first_one_touched() -> void:
	# spec 8.7: the ball the aim line hits first
	# The yellow's centre is nearer along the line (0.60 m against 0.62 m) but it sits 50 mm to
	# the side: the cue ball touches the green after 0.568 m and would touch the yellow at 0.584 m.
	var balls := _balls([["cue", 0.0, 0.0], ["yellow", 0.60, 0.05], ["green", 0.62, -0.003]])
	check_eq(Nomination.first_on_line(balls, 0, 0.0, R), 2, "the green is touched first")
	check_eq(Nomination.preselect(balls, 0, 0.0, _eligible(), R), "green", "and is pre-selected")


func test_8_7_a_tap_on_an_eligible_ball_chooses_it() -> void:
	# spec 8.7: the player can tap any eligible ball to change it
	var balls := _fixture_balls()
	for ball: String in COLOURS:
		var place := Nomination.place_of(balls, ball)
		check(place >= 0 and place < balls.size() and balls[place]["ball"] == ball, "the %s is found on the table" % ball)
		check_eq(Nomination.tapped(balls, place, _eligible()), ball, "a tap on the %s chooses it" % ball)


func test_8_7_a_tap_anywhere_else_chooses_nothing() -> void:
	# spec 8.7: any *eligible* ball
	var balls := _fixture_balls()
	check_eq(Nomination.tapped(balls, 1, _eligible()), "", "a red is not eligible")
	check_eq(Nomination.tapped(balls, 0, _eligible()), "", "nor is the cue ball")
	check_eq(Nomination.tapped(balls, 3, _eligible(["blue", "pink", "black"])), "", "nor a colour the frame does not list")
	check_eq(Nomination.tapped(balls, 6, _eligible(["blue", "pink", "black"])), "blue", "one it lists is")
	# No ball under the finger. The black is last in the list, where a place of -1 would land.
	check_eq(Nomination.tapped(balls, -1, _eligible()), "", "a tap on no ball chooses nothing")
	check_eq(Nomination.tapped(balls, balls.size(), _eligible()), "", "nor does a place off the end of the list")
	balls.remove_at(7)
	check_eq(Nomination.place_of(balls, "pink"), -1, "a colour that is off the table has no place")


func test_8_7_the_nominated_ball_is_highlighted() -> void:
	# spec 8.7: the nominated ball is highlighted
	var ring := Nomination.new()
	tree.root.add_child(ring)
	_nodes.append(ring)
	check(not ring.is_marking(), "no ring before a ball is nominated")
	var balls := _fixture_balls()
	var pink: Dictionary = balls[7]
	ring.mark(Vector2(float(pink["x"]), float(pink["y"])))
	check(ring.is_marking(), "the nominated ball is ringed")
	ring.mark(Vector2(0.0, 0.0))
	check(ring.is_marking(), "the ring moves with the nomination")
	ring.mark(null)
	check(not ring.is_marking(), "and goes when there is none")
	_free_all()


func test_8_7_a_colour_of_choice_must_be_nominated_before_the_stroke() -> void:
	# spec 8.7: when the ball on is "a colour" ... the game pre-selects the ball the aim line hits first
	var m := _to_a_colour_of_choice()
	check_eq(_plain(m.nominations()), COLOURS, "the colours the engine lists are the eligible ones")
	check_eq(m.nominated(), "", "none is nominated yet")
	check_eq(m.strike_blocked(), "nominate", "and the stroke waits for one")
	check(m.nominate("blue", "aim"), "the ball the aim line hits first is taken")
	check_eq(m.nominated(), "blue", "and is the nominated ball")
	check_eq(m.strike_blocked(), "", "the stroke may be played")
	check_eq(m.harness_state().get("nominated"), "blue", "the harness sees it")
	_free_all()


func test_8_7_a_tap_changes_the_nominated_ball() -> void:
	# spec 8.7: the player can tap any eligible ball to change it
	var m := _to_a_colour_of_choice()
	check(m.nominate("blue", "aim"), "pre-selected by the aim")
	for ball: String in ["pink", "yellow", "black", "green", "brown", "blue"]:
		check(m.nominate(ball, "tap"), "a tap on the %s is taken" % ball)
		check_eq(m.nominated(), ball, "and the %s is now the nominated ball" % ball)
	_free_all()


func test_8_7_a_ball_that_is_not_eligible_is_refused() -> void:
	# spec 8.7: any *eligible* ball
	var m := _to_a_colour_of_choice()
	check(m.nominate("blue", "tap"), "an eligible ball first")
	for ball: String in ["red", "cue", "purple"]:
		check(not m.nominate(ball, "tap"), "%s is refused" % ball)
		check_eq(m.nominated(), "blue", "and the nomination stays as it was after %s" % ball)
	# Eligible is what the engine lists, not every colour.
	m = _to_a_colour_of_choice(["blue", "pink", "black"])
	check(not m.nominate("yellow", "tap"), "a colour the engine does not list is refused")
	check_eq(m.nominated(), "", "nothing is nominated by it")
	check_eq(m.strike_blocked(), "nominate", "and the stroke still waits")
	check(m.nominate("pink", "tap"), "one it lists is taken")
	_free_all()


func test_8_7_the_referee_uses_the_nominated_ball() -> void:
	# spec 8.7: the referee logic uses it
	var m := _to_a_colour_of_choice()
	check(m.nominate("blue", "aim"), "pre-selected by the aim")
	check(m.nominate("pink", "tap"), "changed by a tap")
	_will_judge(_frame({"scores": [31, 31], "break_points": 7}))
	_stroke(m, "p2")
	var judged := _asked("judge")
	check_eq(judged.size(), 2, "the red and the colour were both judged")
	if judged.size() == 2:
		check_eq((judged[0][2] as Dictionary).get("nominated"), "", "the red needed no nomination and got none")
		check_eq((judged[1][2] as Dictionary).get("nominated"), "pink", "the engine is told the pink, the last choice")
	_free_all()


func test_8_7_no_nomination_open_asks_for_none() -> void:
	# spec 8.7: only when the ball on is "a colour" or a free ball must be nominated
	var m := _match(_frame())
	check(_start(m), "the match starts")
	check_eq(_plain(m.nominations()), [], "nothing is eligible with a red on")
	check_eq(m.nominated(), "", "nothing is nominated")
	check(m.strike_blocked() != "nominate", "and the stroke does not wait for a nomination")
	check_eq(m.strike_blocked(), "", "nothing blocks it")
	check(not m.nominate("blue", "tap"), "a tap on a colour nominates nothing")
	check_eq(m.nominated(), "", "nothing is nominated after it")
	_will_judge(_frame())
	_stroke(m, "p1")
	var judged := _asked("judge")
	check_eq(judged.size(), 1, "the stroke was judged")
	if judged.size() == 1:
		check_eq((judged[0][2] as Dictionary).get("nominated"), "", "with no nomination")
	_free_all()


func test_8_7_each_stroke_starts_with_no_ball_nominated() -> void:
	# spec 8.7: the game pre-selects for the stroke in hand; an old choice is not a declaration
	var m := _to_a_colour_of_choice()
	check(m.nominate("blue", "tap"), "the blue for this stroke")
	_will_judge(_frame({"scores": [30, 31], "break_points": 6}))
	_stroke(m, "p2")
	check_eq(m.nominated(), "", "no nomination is carried to a stroke that has none open")
	check_eq(m.strike_blocked(), "", "which is not blocked")
	_will_judge(_colour_frame())
	_stroke(m, "p3")
	check_eq(m.nominated(), "", "nor to the next colour of choice")
	check_eq(m.strike_blocked(), "nominate", "which waits for its own")
	var judged := _asked("judge")
	check_eq(judged.size(), 3, "three strokes judged")
	if judged.size() == 3:
		check_eq((judged[1][2] as Dictionary).get("nominated"), "blue", "the colour was judged as the blue")
		check_eq((judged[2][2] as Dictionary).get("nominated"), "", "the red after it with no nomination")
	_free_all()


func test_8_7_a_free_ball_is_nominated_the_same_way() -> void:
	# spec 8.7: ... or a free ball must be nominated
	var m := _match(_frame({"striker": OPPONENT}))
	_engine.standin_shots.append(_standin_shot(""))
	check(_start(m), "the match starts")
	await _frames()
	var foul := _fixture(JUDGEMENT_PATH)
	foul.erase("post")
	_will_judge(_fixture(FRAME_VIEW_PATH), "none", foul)
	_stroke(m, "o1")
	# The fixture with the choice made: a free ball, the person to play.
	var free_ball := _fixture(FRAME_VIEW_PATH)
	free_ball["pending"] = "none"
	(free_ball["view"] as Dictionary)["options"] = IDLE_OPTIONS.duplicate(true)
	_engine.decisions.append({"ok": true, "error": "", "calls": [], "event": "none", "frame": free_ball,
			"balls": free_ball["balls"]})
	check(m.choose("play"), "the person takes the free ball")
	check_eq(_plain(m.nominations()), COLOURS, "the balls the engine lists can be the free ball")
	check(not m.nominate("red", "tap"), "a red, on already, cannot")
	check(m.nominate("yellow", "tap"), "a tap on the yellow nominates it")
	check_eq(m.nominated(), "yellow", "the yellow is the free ball")
	_will_judge(_frame({"scores": [25, 31], "break_points": 1}))
	_stroke(m, "p1")
	var judged := _asked("judge")
	check_eq(judged.size(), 2, "the stroke was judged")
	if judged.size() == 2:
		check_eq((judged[1][2] as Dictionary).get("nominated"), "yellow", "and the engine was told the yellow")
	_free_all()


func test_8_7_the_opponents_nominated_ball_reaches_the_referee_too() -> void:
	# spec 8.7: the referee logic uses it; spec 9.1: the same function judges the human and the AI
	var m := _match(_colour_frame({"striker": OPPONENT}))
	_engine.standin_shots.append(_standin_shot("black"))
	check(_start(m), "the match starts")
	await _frames()
	var ready := _last("opponent_ready")
	check(ready.size() == 1 and (ready[0] as Dictionary).get("nominated") == "black", "the opponent's stroke names its colour")
	check(m.nominate("black", "opponent"), "the table scene declares it for the opponent")
	check_eq(m.nominated(), "black", "it is the nominated ball")
	_will_judge(_frame())
	_stroke(m, "o1")
	var judged := _asked("judge")
	check_eq(judged.size(), 1, "the stroke was judged")
	if judged.size() == 1:
		check_eq((judged[0][2] as Dictionary).get("nominated"), "black", "and the engine was told the black")
	_free_all()


func test_8_7_aim_tap_ring_and_referee_on_the_match_table() -> void:
	# spec 8.7, the whole of it, on the balls the controller holds
	var m := _to_a_colour_of_choice()
	var ring := Nomination.new()
	tree.root.add_child(ring)
	_nodes.append(ring)
	# The fixture's cue ball is at (-0.4, 0.1): the line to the blue on its spot meets nothing first.
	var aimed := Nomination.preselect(m.balls, m.cue_ball, atan2(-0.1, 0.4), m.nominations(), R)
	check_eq(aimed, "blue", "aimed at the blue, the blue is pre-selected")
	check(m.nominate(aimed, "aim"), "and taken")
	var tapped := Nomination.tapped(m.balls, Nomination.place_of(m.balls, "pink"), m.nominations())
	check_eq(tapped, "pink", "a tap on the pink chooses the pink")
	check(m.nominate(tapped, "tap"), "and changes the nomination")
	var place := Nomination.place_of(m.balls, m.nominated())
	check(place >= 0 and place < m.balls.size(), "the nominated ball is on the table")
	if place >= 0 and place < m.balls.size():
		ring.mark(Vector2(float(m.balls[place]["x"]), float(m.balls[place]["y"])))
	check(ring.is_marking(), "and is ringed")
	_will_judge(_frame({"scores": [31, 31], "break_points": 7}))
	_stroke(m, "p2")
	var judged := _asked("judge")
	check(judged.size() == 2 and (judged[-1][2] as Dictionary).get("nominated") == "pink", "the referee is told the pink")
	_free_all()


## A layout: [[ball name, x, y], ...] in the order listed.
func _balls(layout: Array) -> Array[Dictionary]:
	var balls: Array[Dictionary] = []
	var reds := 0
	for entry: Array in layout:
		var ball: String = entry[0]
		balls.append({"ball": ball, "index": reds if ball == "red" else 0, "x": float(entry[1]), "y": float(entry[2])})
		if ball == "red":
			reds += 1
	return balls


## The fixture's table: cue, red, red, yellow, green, brown, blue, pink, black.
func _fixture_balls() -> Array[Dictionary]:
	var balls: Array[Dictionary] = []
	for ball: Dictionary in _fixture(FRAME_VIEW_PATH).get("balls", []):
		balls.append(ball)
	return balls


func _eligible(names: Array = COLOURS) -> Array[String]:
	var out: Array[String] = []
	out.assign(names)
	return out


static func _plain(items: Array) -> Array:
	var out: Array = []
	out.append_array(items)
	return out


## The person has potted a red: a colour of choice is on, `colours` the ones the engine lists.
func _to_a_colour_of_choice(colours: Array = COLOURS) -> MatchController:
	var m := _match(_frame())
	check(_start(m), "the match starts")
	_will_judge(_colour_frame({}, colours))
	_stroke(m, "p1")
	return m


func _colour_frame(changes: Dictionary = {}, colours: Array = COLOURS) -> Dictionary:
	var frame := _frame({"on": {"kind": "any_colour"}, "scores": [25, 31], "break_points": 1},
			{"nominations": colours.duplicate(), "on_places": [3, 4, 5, 6, 7, 8]})
	frame.merge(changes, true)
	return frame


func _match(frame: Dictionary) -> MatchController:
	_free_all()
	_seen.clear()
	_engine = FakeMatchEngine.new()
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
	m.opponent_ready.connect(func(shot: Dictionary) -> void: _seen.append(["opponent_ready", shot.duplicate(true)]))
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


func _standin_shot(nominated: String) -> Dictionary:
	return {"ok": true, "error": "", "places_cue": false, "cue": [-0.4, 0.1], "nominated": nominated,
			"strike": {"aim_rad": 0.3, "tip_x": 0.0, "tip_y": -0.2, "elevation_rad": 0.0, "speed_m_s": 2.0},
			"why": "pot", "us": 180}


## Queues the engine's answer to the next stroke: the frame after it and what it brought.
func _will_judge(frame: Dictionary, event: String = "none", judgement: Dictionary = {}) -> void:
	var legal := {"fouls": [], "penalty": 0, "miss": false, "miss_reason": "", "points": 0, "nominated": "",
			"free_ball_used": false, "spotted": [], "calls": []}
	_engine.judgements.append({"ok": true, "error": "", "judgement": judgement if not judgement.is_empty() else legal,
			"event": event, "frame": frame, "balls": frame.get("balls", [])})


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
