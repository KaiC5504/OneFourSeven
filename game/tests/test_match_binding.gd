extends TestCase
## OfsMatch on the real physics with the embedded referee and stand-in values: configuration, a
## match's strokes, choices, frames and end, the stand-in, what is logged, and every refusal.
## The stand-in's strokes are taken as they come: nothing here depends on how well it plays.

const REFEREE_PATH := "res://data/referee_params.json"
const STANDIN_PATH := "res://data/standin_params.json"
const FRAME_VIEW_PATH := "res://data/rules/example_frame_view.json"
const MATCH_PATH := "res://data/rules/example_match.json"
const SCHEMA_PATH := "res://data/log_schema.json"
const D_RADIUS := 0.292
const BALL_DIAMETER := 0.0525
## Inside the D, clear of the baulk colours.
const IN_THE_D := Vector2(-1.2, 0.1)
const AFTER_FOUL: Array[String] = ["play", "play_again", "replace"]
const JUDGEMENT_KEYS: Array[String] = ["calls", "fouls", "free_ball_used", "miss", "miss_reason", "nominated",
		"penalty", "points", "spotted"]

var _log_start := 0
var _matches := 0


func test_configure_takes_the_real_files() -> void:
	var m := OfsMatch.new()
	check(not m.is_ready(), "not ready before configure")
	_mark_log()
	check(m.configure(Physics.sim, _text(REFEREE_PATH), _text(STANDIN_PATH)), "configure: " + m.last_error())
	check(m.is_ready(), "ready after configure")
	check_eq(m.last_error(), "", "no error")
	check(not m.in_progress(), "no match yet")
	var lines := _logged("rules.configured")
	check_eq(lines.size(), 1, "one rules.configured")
	if lines.size() == 1:
		var file: Dictionary = JSON.parse_string(_text(REFEREE_PATH))
		check_eq(lines[0]["ok"], true, "ok")
		check_eq(lines[0]["problems"], [], "no problems")
		check_eq(lines[0]["values"], file["values"], "the referee's values as loaded")
		check((lines[0]["standin"] as Dictionary).has("max_sims"), "the stand-in's values")
		check_eq(lines[0]["params_hash"], Physics.sim.params_hash(), "and the physics values' hash")
	_check_schema()


func test_physics_values_changed_live_reach_the_match() -> void:
	var table_text := _text("res://data/table_shape.json")
	var physics := OfsPhysics.new()
	var params_text := _text(Params.EMBEDDED_PATH)
	check(physics.configure(params_text, "live-before", table_text), "the physics is configured")
	var m := OfsMatch.new()
	_mark_log()
	check(m.configure(physics, _text(REFEREE_PATH), _text(STANDIN_PATH)), "configure: " + m.last_error())
	check_eq(m.start(_config(3, 0, 147), _match_id()).get("ok"), true, "a match starts")
	for line in _logged("rules.configured") + _logged("frame.started"):
		check_eq(line.get("params_hash"), "live-before", "the values the match was set up with")

	# As the phone link changes them, between strokes: a slower cue and wider gaps in the rack.
	var changed := params_text.replace('"max_cue_speed_m_s": 6.5,', '"max_cue_speed_m_s": 2.0,') \
			.replace('"rack_gap_max_m": 0.0001,', '"rack_gap_max_m": 0.0005,')
	check(changed.contains('"max_cue_speed_m_s": 2.0,') and changed.contains('"rack_gap_max_m": 0.0005,'),
			"both values are in the text changed")
	check(physics.configure(changed, "live-after", table_text), "the new values are taken")
	_mark_log()
	var shot := m.standin_shot(5, {"shot_id": "live-1"})
	check_eq(shot.get("ok"), true, "the stand-in plays: " + str(shot.get("error")))
	check(float(shot.get("strike", {}).get("speed_m_s", 99.0)) <= 2.0, "within the new cap: " + str(shot.get("strike")))
	check_eq(m.decide("concede_frame", 0).get("ok"), true, "frame 1 is conceded")
	check_eq(m.next_frame().get("ok"), true, "frame 2 is racked: " + m.last_error())
	var started := _logged("frame.started")
	check_eq(started.size(), 1, "frame.started")
	if started.size() == 1:
		check_eq(int(started[0]["number"]), 2, "for frame 2")
		check_eq(started[0]["params_hash"], "live-after", "with the values in use now")
	_check_schema()

	var unchanged := OfsPhysics.new()
	check(unchanged.configure(params_text, "live-before", table_text), "the values as they were")
	var before := OfsMatch.new()
	check(before.configure(unchanged, _text(REFEREE_PATH), _text(STANDIN_PATH)), "configure: " + before.last_error())
	check_eq(before.start(_config(3, 0, 147), _match_id()).get("ok"), true, "the same match on them")
	check_eq(before.decide("concede_frame", 0).get("ok"), true, "frame 1 is conceded")
	check_eq(before.next_frame().get("ok"), true, "to frame 2")
	check(before.balls() != m.balls(), "racks frame 2 with other gaps")
	check_eq(before.balls().size(), m.balls().size(), "and the same balls")


func test_a_refusal_inside_the_engine_names_the_stroke() -> void:
	var m := _started(_config(3, 1, 147))
	var id := _last_id()
	_mark_log()
	# Blue can't be declared with red on: the engine says so from inside judge and plays on.
	var answer: Dictionary = _stroke(m, _strike(_aim_at_the_pack(m.balls(), m.cue_ball()), 3.0), 11, "early-blue",
			"blue")[1]
	check_eq(answer.get("ok"), true, "judged: " + str(answer.get("error")))
	var refused := _logged_what("nomination")
	check_eq(refused.size(), 1, "rules.invalid from the engine")
	if refused.size() == 1:
		check_eq(refused[0].get("match_id"), id, "with the match id")
		check_eq(int(refused[0].get("frame", 0)), 1, "the frame")
		check_eq(refused[0].get("shot_id"), "early-blue", "and the stroke")
	_check_schema()
	_mark_log()
	check_eq(m.place_cue(5.0, 5.0).get("ok"), false, "a cue ball off the table is refused")
	for line in _logged("rules.invalid"):
		check_eq(line.get("match_id"), id, "OfsMatch's own refusal carries the match too")
		check(not line.has("shot_id"), "and no stroke")


func test_configure_refusals_keep_what_was_there() -> void:
	var referee := _text(REFEREE_PATH)
	var standin := _text(STANDIN_PATH)
	var cases := {
		"no physics": [null, referee, standin, "physics: null"],
		"physics not configured": [OfsPhysics.new(), referee, standin, "physics: not configured"],
		"referee not JSON": [Physics.sim, "{not json", standin, "referee: not valid JSON"],
		"standin not JSON": [Physics.sim, referee, "", "standin: not valid JSON"],
		"referee without values": [Physics.sim, "{}", standin, "referee: values"],
		"a referee value out of range": [Physics.sim,
				referee.replace('"stalemate_offer_strokes": 12', '"stalemate_offer_strokes": -1'), standin,
				"referee: stalemate_offer_strokes"],
	}
	var fresh := OfsMatch.new()
	_mark_log()
	for name: String in cases:
		var c: Array = cases[name]
		check(not fresh.configure(c[0], c[1], c[2]), name + " is refused")
		check(fresh.last_error().contains(c[3]), "%s: last_error names it: %s" % [name, fresh.last_error()])
		check(not fresh.is_ready(), name + ": still not ready")
	var lines := _logged("rules.configured")
	check_eq(lines.size(), cases.size(), "rules.configured for each attempt")
	for line in lines:
		check_eq(line["ok"], false, "ok false")
		check(not (line["problems"] as Array).is_empty(), "problems listed")
	_check_schema()
	check_eq(fresh.start(_config(1, 0, 1), "never")["ok"], false, "no match on an engine that was never configured")

	var m := _engine()
	check(not m.configure(Physics.sim, "[]", standin), "a bad file after a good one is refused")
	check(m.is_ready(), "and the engine stays ready")
	check_eq(m.start(_config(1, 0, 1), _match_id())["ok"], true, "with the values it had")
	var before := m.match_state()
	_mark_log()
	check(not m.configure(Physics.sim, referee, standin), "not while a match is in progress")
	check(m.last_error().contains("a match is in progress"), "last_error says so: " + m.last_error())
	check_eq(m.match_state(), before, "the match is as it was")
	check_eq(_logged("rules.configured").size(), 1, "the refusal is logged")
	m.abandon()
	check(m.configure(Physics.sim, referee, standin), "again once the match is left: " + m.last_error())


func test_start_with_each_kind_of_config() -> void:
	var m := _engine()
	for breaker in 2:
		_mark_log()
		var id := _match_id()
		var config := _config(3, breaker, 147)
		config["person"] = 1 - breaker
		var started := m.start(config, id)
		check_eq(started["ok"], true, "first_breaker %d starts: %s" % [breaker, started.get("error")])
		check_eq(started["error"], "", "no error")
		check_eq(started["match"], m.match_state(), "start hands back match_state()")
		check(m.in_progress(), "in progress")
		check_eq(m.last_error(), "", "last_error is clear")
		var state := m.match_state()
		check_eq(state["breaker"], breaker, "the breaker asked for")
		check_eq(state["frame_number"], 1, "frame 1")
		check_eq(state["frames"], [0, 0], "no frames won")
		check_eq(state["history"], [], "no history")
		check_eq(state["over"], false, "not over")
		check_eq(state["config"], {"best_of": 3, "first_breaker": breaker, "seed": "147",
				"concede_any_time": [true, false]}, "the config, the seed as a decimal string")
		var frame := m.frame()
		check_eq(frame["striker"], breaker, "the breaker plays first")
		check_eq(frame["concede_any_time"], [true, false], "who may concede at any time")
		check_eq(frame["cue_in_hand"], true, "the cue ball is in hand")
		check_eq(frame["on"], {"kind": "red"}, "red is on")
		var match_lines := _logged("match.started")
		check_eq(match_lines.size(), 1, "match.started")
		if match_lines.size() == 1:
			check_eq(match_lines[0]["match_id"], id, "with the match id")
			check_eq(int(match_lines[0]["breaker"]), breaker, "the breaker")
			check_eq(int(match_lines[0]["person"]), 1 - breaker, "and who the person is")
			check_eq(match_lines[0]["config"]["seed"], "147", "the config")
		var frame_lines := _logged("frame.started")
		check_eq(frame_lines.size(), 1, "frame.started")
		if frame_lines.size() == 1:
			check_eq(frame_lines[0]["match_id"], id, "with the match id")
			check_eq(int(frame_lines[0]["frame"]), 1, "the frame number as context")
			check_eq(int(frame_lines[0]["number"]), 1, "and as the frame's number")
			check_eq(frame_lines[0]["seed"], frame["seed"], "the rack's seed")
		_check_context(id)
		_check_schema()

	var drawn := {}
	for seed in range(1, 25):
		check_eq(m.start(_config(1, -1, seed), _match_id())["ok"], true, "lots with seed %d" % seed)
		var breaker := int(m.match_state()["breaker"])
		check_eq(m.frame()["striker"], breaker, "who won the lots breaks")
		drawn[breaker] = true
		check_eq(m.start(_config(1, -1, seed), _match_id())["ok"], true, "the same seed again")
		check_eq(m.match_state()["breaker"], breaker, "draws the same lots")
	check_eq(drawn.size(), 2, "the lots fall both ways over 24 seeds")

	for best_of: int in [1, 35]:
		check_eq(m.start(_config(best_of, 0, 5), _match_id())["ok"], true, "best of %d" % best_of)
	check_eq(m.start(_config(3, 0, -42), _match_id())["ok"], false, "a negative seed is refused")
	check(m.last_error().contains("negative"), "last_error says why: " + m.last_error())
	var past_int64 := _changed(_config(3, 0, 0), "seed", "18446744073709551574")
	check_eq(m.start(past_int64, _match_id())["ok"], true, "a seed past an int64, as a decimal String")
	check_eq(m.match_state()["config"]["seed"], "18446744073709551574", "is taken whole")
	var racked := m.balls()
	var again := _changed(_config(3, 0, 0), "seed", m.match_state()["config"]["seed"])
	check_eq(m.start(again, _match_id())["ok"], true, "the config's own seed goes back in")
	check_eq(m.balls(), racked, "and racks the same balls")
	check_eq(m.start(_changed(_config(3, 0, 0), "seed", "147"), _match_id())["ok"], true, "147 as a String")
	var as_text := m.balls()
	check_eq(m.start(_config(3, 0, 147), _match_id())["ok"], true, "and as an int")
	check_eq(m.balls(), as_text, "are the same seed")
	var from_json: Dictionary = JSON.parse_string(
			'{"best_of": 3, "first_breaker": 1, "seed": 147, "concede_any_time": [false, true]}')
	check_eq(m.start(from_json, _match_id())["ok"], true, "a config read from JSON, its numbers floats")
	check_eq(m.match_state()["config"]["best_of"], 3, "best_of is still an int")
	var balls := m.balls()
	check_eq(m.start(_config(3, 1, 147), _match_id())["ok"], true, "the same seed")
	check_eq(m.balls(), balls, "racks the same balls")
	check_eq(m.start(_config(3, 1, 148), _match_id())["ok"], true, "another seed")
	check(m.balls() != balls, "racks other gaps")


func test_start_over_a_match_in_progress_abandons_it() -> void:
	var m := _engine()
	var first := _match_id()
	check_eq(m.start(_config(3, 0, 1), first)["ok"], true, "the first match")
	_mark_log()
	var second := _match_id()
	check_eq(m.start(_config(5, 1, 2), second)["ok"], true, "a second over it")
	var ended := _logged("match.ended")
	check_eq(ended.size(), 1, "the first is ended")
	if ended.size() == 1:
		check_eq(ended[0]["match_id"], first, "under its own id")
		check_eq(ended[0]["reason"], "abandoned", "as abandoned")
	check_eq(_logged("match.started")[0]["match_id"] if _logged("match.started").size() == 1 else "", second,
			"the second is started")
	check_eq(m.match_state()["config"]["best_of"], 5, "and is the match in play")


func test_start_refusals_leave_the_match_as_it_was() -> void:
	var good := _config(3, 0, 147)
	var cases := {
		"best_of even": [_changed(good, "best_of", 4), "best_of"],
		"best_of 0": [_changed(good, "best_of", 0), "best_of"],
		"best_of 37": [_changed(good, "best_of", 37), "best_of"],
		"best_of past an int": [_changed(good, "best_of", 1 << 40), "best_of"],
		"best_of a string": [_changed(good, "best_of", "three"), "best_of"],
		"best_of missing": [_without(good, "best_of"), "best_of"],
		"first_breaker 2": [_changed(good, "first_breaker", 2), "first_breaker"],
		"first_breaker missing": [_without(good, "first_breaker"), "first_breaker"],
		"seed a fraction": [_changed(good, "seed", 1.5), "seed"],
		"seed negative": [_changed(good, "seed", -1), "seed"],
		"seed a word": [_changed(good, "seed", "many"), "seed"],
		"seed a negative String": [_changed(good, "seed", "-1"), "seed"],
		"seed past a uint64": [_changed(good, "seed", "18446744073709551616"), "seed"],
		"seed missing": [_without(good, "seed"), "seed"],
		"concede_any_time one bool": [_changed(good, "concede_any_time", [true]), "concede_any_time"],
		"concede_any_time numbers": [_changed(good, "concede_any_time", [1, 0]), "concede_any_time"],
		"concede_any_time missing": [_without(good, "concede_any_time"), "concede_any_time"],
		"empty": [{}, "best_of"],
	}
	var fresh := _engine()
	_mark_log()
	for name: String in cases:
		var c: Array = cases[name]
		var answer := fresh.start(c[0], "refused-" + name)
		check_eq(answer.get("ok"), false, name + " is refused")
		check(str(answer.get("error")).contains(c[1]), "%s: the error names the key: %s" % [name, answer.get("error")])
		check_eq(fresh.last_error(), answer.get("error"), name + ": last_error is the error")
		check(not fresh.in_progress(), name + ": no match")
		check_eq(fresh.match_state(), {}, name + ": no state")
	var invalid := _logged("rules.invalid")
	check_eq(invalid.size(), cases.size(), "rules.invalid for each")
	for i in mini(invalid.size(), cases.size()):
		check_eq(invalid[i]["what"], "OfsMatch.start", "the call")
		check(str(invalid[i]["detail"]).contains("refused-" + str(cases.keys()[i])), "with what it was given")
	check_eq(_logged("match.started").size(), 0, "nothing started")
	_check_schema()

	var m := _started(_config(3, 1, 9))
	var before := m.match_state()
	check_eq(m.start(_changed(good, "best_of", 2), "refused-over-a-match")["ok"], false, "refused over a match")
	check(m.in_progress(), "the match goes on")
	check_eq(m.match_state(), before, "exactly as it was")

	_mark_log()
	var odd := good.duplicate()
	odd["person"] = "you"
	check_eq(m.start(odd, _match_id())["ok"], true, "a person that is not a player does not stop the match")
	check_eq(_logged("match.started").size(), 1, "match.started is logged")
	if _logged("match.started").size() == 1:
		check(not _logged("match.started")[0].has("person"), "without it")
	check_eq(_logged("rules.invalid").size(), 1, "and the bad value is logged")


func test_the_ball_list_is_the_opening_rack() -> void:
	var m := _started(_config(3, 0, 147))
	var balls := m.balls()
	var rack := Physics.sim.opening_rack(0)
	check_eq(balls.size(), 22, "22 balls")
	check_eq(m.cue_ball(), 0, "the cue ball first")
	check_eq(balls, m.frame()["balls"], "the frame view lists the same balls")
	check_eq(m.frame()["cue_ball"], m.cue_ball(), "and the same cue ball")
	check_eq(m.match_state()["frame"], m.frame(), "match_state's frame is the frame view")
	for i in mini(balls.size(), rack.size()):
		var b: Dictionary = balls[i]
		check_eq(b.keys(), ["ball", "index", "x", "y"], "ball %d's keys" % i)
		check_eq(b["ball"], rack[i]["ball"], "ball %d is the rack's" % i)
		check_eq(b["index"], rack[i]["index"], "ball %d's index" % i)
		check(b["index"] is int and b["x"] is float and b["y"] is float, "ball %d: an int index, float x and y" % i)
		# Only the seeded gaps between the reds differ from rack to rack.
		check_near(b["x"], rack[i]["x"], 1e-3, "ball %d x" % i)
		check_near(b["y"], rack[i]["y"], 1e-3, "ball %d y" % i)
	check(Physics.sim.predict(balls, m.cue_ball(), _strike(0.0, 1.0)) != null,
			"the physics takes the list as it is: " + Physics.sim.last_error())


func test_frame_view_and_match_have_the_documented_shape() -> void:
	var fouled := _fouled(147)
	var m: OfsMatch = fouled["match"]
	var frame := m.frame()
	_check_shape(frame, _fixture(FRAME_VIEW_PATH), "frame")
	_check_numbers(frame, "frame")
	check(not (frame["original"]["balls"] as Array).is_empty(), "the position before the foul is kept")
	check_eq(frame["view"]["options"], [[], AFTER_FOUL], "the view's options")
	check_eq(frame["view"]["leader"], 1, "the view's leader")

	check_eq(m.decide("play", 1)["ok"], true, "the choice")
	check_eq(m.decide("concede_frame", 0)["ok"], true, "a frame conceded")
	check_eq(m.next_frame()["ok"], true, "the next racked")
	var state := m.match_state()
	var want: Variant = _fixture(MATCH_PATH)
	check_eq((state["history"] as Array).size(), 1, "one frame in the history")
	_check_numbers(state, "match")
	# The fixture's frame is the plain frame; match_state's is the frame view.
	var plain := state.duplicate(true)
	(plain["frame"] as Dictionary).erase("view")
	_check_shape(plain, want, "match")
	_check_shape(state["frame"], _fixture(FRAME_VIEW_PATH), "match.frame")


func test_placing_the_cue_ball_in_on_and_outside_the_d() -> void:
	var m := _started(_config(3, 0, 147))
	var id := _last_id()
	var listed: Dictionary = m.balls()[m.cue_ball()]
	var line_x: float = listed["x"]
	_mark_log()
	var placed := m.place_cue(IN_THE_D.x, IN_THE_D.y)
	check_eq(placed, {"ok": true, "error": "", "x": IN_THE_D.x, "y": IN_THE_D.y}, "inside the D")
	check_eq(_cue(m), IN_THE_D, "the ball list has it there")
	check_eq(m.frame()["balls"][m.cue_ball()]["x"], IN_THE_D.x, "the frame view too")
	check_eq(m.frame()["cue_in_hand"], true, "still in hand: it may be moved again")
	check_eq(m.last_error(), "", "no error")

	check_eq(m.place_cue(line_x, -0.096)["ok"], true, "on the baulk line")
	check_eq(_cue(m), Vector2(line_x, -0.096), "placed there")
	var edge := m.nearest_cue_placement(-2.0, 0.05)
	check(edge.has("x") and edge.has("y"), "a nearest place for a point behind the D")
	var edge_xy := Vector2(edge.get("x", 0.0), edge.get("y", 0.0))
	check_near(edge_xy.distance_to(Vector2(line_x, 0.0)), D_RADIUS, 1e-4, "which is on the D's arc")
	check_eq(m.place_cue(edge_xy.x, edge_xy.y)["ok"], true, "and is a legal place")
	check_eq(m.nearest_cue_placement(IN_THE_D.x, IN_THE_D.y), {"x": IN_THE_D.x, "y": IN_THE_D.y},
			"a legal place is its own nearest")

	var before := m.match_state()
	var outside := {
		"the middle of the table": Vector2(0.0, 0.0),
		"past the baulk line": Vector2(line_x + 1e-4, -0.096),
		"behind the arc": Vector2(edge_xy.x - 1e-3, edge_xy.y),
		"on the brown": Vector2(line_x, 0.0),
		"against the brown": Vector2(line_x - BALL_DIAMETER + 1e-4, 0.0),
		"nowhere": Vector2(NAN, 0.0),
	}
	for name: String in outside:
		var xy: Vector2 = outside[name]
		var refused := m.place_cue(xy.x, xy.y)
		check_eq(refused.get("ok"), false, name + " is refused")
		check(not str(refused.get("error")).is_empty(), name + ": with a reason")
		check_eq(m.last_error(), refused.get("error"), name + ": last_error is the reason")
		if not is_nan(xy.x):
			check_eq(Vector2(refused.get("x", 0.0), refused.get("y", 0.0)), xy, name + ": the place as asked")
			var nearest := m.nearest_cue_placement(xy.x, xy.y)
			check(nearest != {"x": xy.x, "y": xy.y}, name + ": its nearest legal place is elsewhere")
	check_eq(m.match_state(), before, "the cue ball stays where it was")
	check_eq(m.nearest_cue_placement(NAN, 0.0), {}, "no nearest place for a point that is not one")

	var lines := _logged("rules.cue_placed")
	check_eq(lines.size(), 3 + outside.size() - 1, "rules.cue_placed for each but the one that is no point")
	var accepted := 0
	for line in lines:
		check_eq(line["match_id"], id, "with the match id")
		check_eq(int(line["frame"]), 1, "and the frame")
		check_eq(int(line["player"]), 0, "by the striker")
		if line["ok"]:
			accepted += 1
		else:
			check(not str(line["error"]).is_empty(), "a refusal says why")
	check_eq(accepted, 3, "three were accepted")
	check_eq(_logged_what("OfsMatch.place_cue").size(), outside.size(), "each refusal is also rules.invalid")
	_check_schema()

	check_eq(m.place_cue(IN_THE_D.x, IN_THE_D.y)["ok"], true, "back inside")
	var answer: Dictionary = _stroke(m, _strike(0.0, 1.0), 3, "place-then-play")[1]
	check_eq(answer.get("ok"), true, "a stroke from there")
	if not bool(m.frame()["cue_in_hand"]):
		var late := m.place_cue(IN_THE_D.x, IN_THE_D.y)
		check_eq(late["ok"], false, "a cue ball in play can't be placed")
		check(str(late["error"]).contains("not in hand") or str(late["error"]).contains("decision"),
				"and the error says why: " + str(late["error"]))


func test_a_break_off_is_simulated_and_judged() -> void:
	var m := _started(_config(3, 1, 147))
	var id := _last_id()
	var before := m.balls()
	var strike := _strike(_aim_at_the_pack(before, m.cue_ball()), 3.0)
	_mark_log()
	var played := _stroke(m, strike, 11, "break-off")
	var record: OfsShotRecord = played[0]
	var answer: Dictionary = played[1]
	check_eq(answer.get("ok"), true, "judged: " + str(answer.get("error")))
	if record == null or not bool(answer.get("ok", false)):
		return
	check_eq(answer.keys(), ["ok", "error", "judgement", "event", "frame"], "the answer's keys")
	check_eq(answer["error"], "", "no error")
	check_eq(m.last_error(), "", "last_error is clear")
	check_eq(answer["event"], "none", "one stroke ends nothing")
	check_eq(answer["frame"], m.frame(), "the frame handed back is frame()")
	check_eq(m.balls(), m.frame()["balls"], "balls() is the frame's balls")
	var judgement: Dictionary = answer["judgement"]
	var keys := judgement.keys()
	keys.sort()
	check_eq(keys, JUDGEMENT_KEYS, "the judgement without its post")
	_check_numbers(judgement, "judgement")
	_check_numbers(answer["frame"], "frame")

	var frame := m.frame()
	var fouls: Array = judgement["fouls"]
	var points: int = judgement["points"]
	var penalty: int = judgement["penalty"]
	check_eq(frame["strokes"], 1, "one stroke played")
	check_eq(frame["stats"][1]["shots"], 1, "by the breaker")
	check_eq(frame["cue_in_hand"], _potted(record, m.cue_ball()), "in hand again only after an in-off")
	if fouls.is_empty():
		check_eq(penalty, 0, "no penalty without a foul")
		check_eq(frame["scores"], [0, points], "the breaker scored what was potted")
		check_eq(frame["striker"], 1 if points > 0 else 0, "and plays on only after a pot")
		check_eq(frame["pending"], "none", "nothing to decide")
	else:
		check(penalty >= 4 and penalty == int(fouls[0]["value"]), "the penalty is the first foul's: %d" % penalty)
		check_eq(points, 0, "a foul scores nothing")
		check_eq(frame["scores"], [penalty, 0], "the penalty goes to the other player")
		check_eq(frame["pending"], "after_foul", "who then chooses")
		check_eq(frame["decider"], 0, "the non-offender")
		check_eq(frame["view"]["options"][0].slice(0, 2), ["play", "play_again"], "play or have them play again")
	_check_balls_after(record, before, m, judgement)

	var decisions := _logged("rules.decision")
	check_eq(decisions.size(), 1, "one rules.decision")
	if decisions.size() == 1:
		var line := decisions[0]
		check_eq(line["match_id"], id, "with the match id")
		check_eq(int(line["frame"]), 1, "the frame")
		check_eq(line["shot_id"], "break-off", "and the shot id")
		check_eq(int(line["judgement"]["penalty"]), penalty, "the judgement's penalty")
		check_eq(int(line["judgement"]["points"]), points, "and points")
		check_eq((line["pre"]["balls"] as Array).size(), before.size(), "the frame before the stroke")
		check_eq(int(line["after"]["striker"]), int(frame["striker"]), "the striker after it")
	_check_context(id)
	_check_schema()

	var again := _started(_config(3, 1, 147))
	var replayed: Dictionary = _stroke(again, strike, 11, "break-off-again")[1]
	check_eq(replayed.get("judgement"), judgement, "the same stroke in the same match is judged the same")
	check_eq(again.frame(), frame, "and leaves the same frame")


func test_an_in_off_brings_the_cue_ball_back_in_hand() -> void:
	var m := _started(_config(3, 0, 147))
	check_eq(m.place_cue(IN_THE_D.x, IN_THE_D.y).get("ok"), true, "the cue ball is placed")
	var before := m.balls()
	# The corner pocket on the cue ball's side of baulk, with nothing between them.
	var pocket := Vector2.ZERO
	for p: Dictionary in Physics.table_shape()["pockets"]:
		var centre := Vector2(p["drop_centre"][0], p["drop_centre"][1])
		if centre.x < 0.0 and centre.y > 0.0 and centre.x < pocket.x:
			pocket = centre
	var played := _stroke(m, _strike((pocket - IN_THE_D).angle(), 2.5), 5, "in-off")
	var record: OfsShotRecord = played[0]
	var answer: Dictionary = played[1]
	check_eq(answer.get("ok"), true, "judged: " + str(answer.get("error")))
	if record == null or not bool(answer.get("ok", false)):
		return
	check(_potted(record, m.cue_ball()), "the cue ball went in")
	var judgement: Dictionary = answer["judgement"]
	var kinds: Array[String] = []
	for f: Dictionary in judgement["fouls"]:
		kinds.append(f["kind"])
	check(kinds.has("in_off"), "an in-off is among the fouls: %s" % [kinds])
	check_eq(judgement["penalty"], 4, "four away")
	var frame := m.frame()
	check_eq(frame["scores"], [0, 4], "scored to the other player")
	check_eq(frame["cue_in_hand"], true, "the cue ball is in hand")
	check_eq(m.balls().size(), 22, "and still listed")
	_check_balls_after(record, before, m, judgement)
	var listed := _cue(m)
	check_eq(m.nearest_cue_placement(listed.x, listed.y), {"x": listed.x, "y": listed.y},
			"where it is listed is a legal place")

	check_eq(m.decide("play", 1).get("ok"), true, "the other player takes the table")
	check_eq(m.frame()["cue_in_hand"], true, "with the cue ball in hand")
	_mark_log()
	check_eq(m.place_cue(IN_THE_D.x, IN_THE_D.y).get("ok"), true, "and places it: " + m.last_error())
	var lines := _logged("rules.cue_placed")
	check_eq(int(lines[0]["player"]) if lines.size() == 1 else -1, 1, "rules.cue_placed names them")
	check_eq(_stroke(m, _strike(0.0, 1.0), 6, "from-the-d")[1].get("ok"), true, "then plays from there")
	check_eq(m.frame()["strokes"], 2, "the second stroke of the frame")


func test_a_foul_offers_its_choices_and_each_is_applied() -> void:
	var fouled := _fouled(147)
	var m: OfsMatch = fouled["match"]
	var judgement: Dictionary = fouled["judgement"]
	check_eq(judgement["fouls"][0]["kind"] if not (judgement["fouls"] as Array).is_empty() else "", "no_hit",
			"nothing was hit")
	check_eq(judgement["penalty"], 4, "four away")
	check_eq(judgement["miss"], true, "a miss, with the reds in plain view")
	var calls: Array[String] = []
	for c: Dictionary in judgement["calls"]:
		calls.append(c["kind"])
	check_eq(calls.slice(0, 2), ["foul", "miss"], "FOUL AND A MISS is called")
	_check_numbers(judgement, "judgement")
	var frame := m.frame()
	check_eq(frame["scores"], [0, 4], "the penalty is scored")
	check_eq(frame["pending"], "after_foul", "a choice is pending")
	check_eq(frame["decider"], 1, "for the non-offender")
	check_eq(frame["offender"], 0, "the offender is known")
	check_eq(frame["view"]["options"], [[], AFTER_FOUL], "play, play again or replace")

	var after_stroke := m.balls()
	_mark_log()
	var play := m.decide("play", 1)
	check_eq(play.get("ok"), true, "play: " + str(play.get("error")))
	check_eq(play.keys(), ["ok", "error", "calls", "event", "frame"], "the answer's keys")
	check(play.get("calls") is Array, "calls is a list")
	check_eq(play.get("event"), "none", "nothing ends")
	check_eq(play.get("frame"), m.frame(), "the frame handed back is frame()")
	check_eq(m.frame()["striker"], 1, "the non-offender plays")
	check_eq(m.frame()["pending"], "none", "nothing is pending")
	check_eq(m.balls(), after_stroke, "from the balls as they lie")
	check_eq(m.last_error(), "", "no error")
	var chosen := _logged("rules.choice")
	check_eq(chosen.size(), 1, "one rules.choice")
	if chosen.size() == 1:
		check_eq(chosen[0]["match_id"], fouled["id"], "with the match id")
		check_eq(int(chosen[0]["frame"]), 1, "the frame")
		check_eq(chosen[0]["kind"], "play", "the choice")
		check_eq(int(chosen[0]["player"]), 1, "who made it")
		check_eq(chosen[0]["ok"], true, "taken")
		check_eq(chosen[0]["options"], AFTER_FOUL, "among what was offered")
	_check_context(fouled["id"])
	_check_schema()

	fouled = _fouled(147)
	m = fouled["match"]
	check_eq(m.decide("play_again", 1).get("ok"), true, "play again")
	check_eq(m.frame()["striker"], 0, "the offender plays again")
	check_eq(m.frame()["pending"], "none", "nothing is pending")
	check_eq(m.balls(), after_stroke, "from the balls as they lie")

	fouled = _fouled(147)
	m = fouled["match"]
	var replace := m.decide("replace", 1)
	check_eq(replace.get("ok"), true, "replace: " + str(replace.get("error")))
	check_eq(m.balls(), fouled["before"], "the balls are back exactly where they were")
	check_eq(m.cue_ball(), 0, "the cue ball's place too")
	check_eq(m.frame()["striker"], 0, "the offender plays again")
	check_eq(m.frame()["cue_in_hand"], true, "from the D, as before")
	check_eq(m.frame()["scores"], [0, 4], "the penalty stands")
	check_eq(m.frame()["pending"], "none", "nothing is pending")
	var second: Dictionary = _stroke(m, _strike(PI, 0.2), 8, "foul-again")[1]
	check_eq(second.get("ok"), true, "the replaced position is played from")
	check_eq(m.frame()["scores"], [0, 8], "and a second miss costs four more")


func test_a_third_miss_ends_the_match_from_the_stroke() -> void:
	var m := _started(_config(1, 0, 147))
	var id := _last_id()
	check_eq(m.place_cue(IN_THE_D.x, IN_THE_D.y).get("ok"), true, "the cue ball is placed")
	var last := {}
	for miss in range(1, 4):
		if miss == 3:
			_mark_log()
		last = _stroke(m, _strike(PI, 0.2), 20 + miss, "miss-%d" % miss)[1]
		check_eq(last.get("ok"), true, "miss %d is judged: %s" % [miss, last.get("error")])
		if miss == 3:
			break
		check_eq(m.frame()["miss_run"], miss, "%d in a row with a red in full view" % miss)
		var replaced := m.decide("replace", 1)
		check_eq(replaced.get("ok"), true, "replaced after miss %d: %s" % [miss, replaced.get("error")])
		var warned := false
		for c: Dictionary in replaced.get("calls", []):
			warned = warned or c["kind"] == "warning"
		check_eq(warned, miss == 2, "the warning comes with the second replacement")
	check_eq(last.get("event"), "match_ended", "the third miss loses a one-frame match")
	var frame := m.frame()
	check_eq(frame["over"], true, "the frame is over")
	check_eq(frame["winner"], 1, "to the other player")
	check_eq(frame["end_reason"], "three_misses", "for three misses")
	check(not m.in_progress(), "the match is over")
	check_eq(m.match_state()["winner"], 1, "and won")
	check_eq(last.get("frame"), frame, "the frame handed back is the finished one")
	var frames := _logged("frame.ended")
	check_eq(frames.size(), 1, "frame.ended")
	if frames.size() == 1:
		check_eq(frames[0]["match_id"], id, "with the match id")
		check_eq(frames[0]["shot_id"], "miss-3", "and the stroke that ended it")
		check_eq(frames[0]["summary"]["reason"], "three_misses", "the summary's reason")
	var matches := _logged("match.ended")
	check_eq(matches.size(), 1, "match.ended")
	if matches.size() == 1:
		check_eq(matches[0]["reason"], "frames", "won on frames")
		check_eq(matches[0]["shot_id"], "miss-3", "by that stroke")
	_check_context(id)
	_check_schema()


func test_a_stalemate_racks_the_frame_again() -> void:
	# Two quiet strokes bring the offer and one more after an objection the re-rack; with 10 m of
	# travel allowed every stroke that pots nothing is quiet. A foul stroke counts for neither.
	var referee := _text(REFEREE_PATH).replace('"stalemate_offer_strokes": 12', '"stalemate_offer_strokes": 2')
	referee = referee.replace('"stalemate_quiet_travel_m": 0.6', '"stalemate_quiet_travel_m": 10.0')
	referee = referee.replace('"stalemate_grace_strokes": 6', '"stalemate_grace_strokes": 1')
	for agreed: bool in [true, false]:
		var m := OfsMatch.new()
		check(m.configure(Physics.sim, referee, _text(STANDIN_PATH)), "configure: " + m.last_error())
		var id := _match_id()
		check_eq(m.start(_config(3, 0, 147), id).get("ok"), true, "started")
		var first_rack := m.balls()
		check_eq(m.place_cue(IN_THE_D.x, IN_THE_D.y).get("ok"), true, "the cue ball is placed")
		check_eq(_stroke(m, _strike(PI, 0.2), 7, "foul-1")[1].get("ok"), true, "a stroke that hits nothing")
		check_eq(m.decide("play_again", 1).get("ok"), true, "the offender plays again")
		check_eq(_stroke(m, _strike(0.0, 1.5), 8, "quiet-1")[1].get("ok"), true, "a roll up to the pack")
		var roll: Dictionary = _stroke(m, _strike(_aim_at_nearest_red(m), 0.5), 10, "quiet-2")[1]
		check_eq(roll.get("ok"), true, "and another: " + str(roll.get("error")))
		var frame := m.frame()
		if frame.get("pending") != "rerack":
			check(false, "a re-rack is offered after two quiet strokes; the roll was judged %s" % [roll.get("judgement")])
			return
		var decider: int = frame["decider"]
		var answers := ["accept_rerack", "refuse_rerack"]
		if decider == 0:
			# Player 0 may concede at any time, so also while the offer is theirs to answer.
			answers += ["concede_frame", "concede_match"]
		check_eq(frame["view"]["options"][decider], answers, "the offer's answers")
		var scores: Array = frame["scores"]
		check(scores != [0, 0], "the frame has a score to lose")
		_mark_log()
		var answer := {}
		if agreed:
			check_eq(m.decide("accept_rerack", decider).get("ok"), true, "one player accepts")
			check_eq(m.frame()["decider"], 1 - decider, "and the other is asked")
			answer = m.decide("accept_rerack", 1 - decider)
			var choices := _logged("rules.choice")
			check_eq(choices.size(), 2, "both choices are logged")
			if choices.size() == 2:
				check_eq(choices[1]["after"]["scores"], [float(scores[0]), float(scores[1])],
						"the second with the frame as the choice left it, not the new rack")
		else:
			check_eq(m.decide("refuse_rerack", decider).get("ok"), true, "the offer is refused")
			check_eq(m.frame()["pending"], "none", "and play goes on")
			answer = _stroke(m, _strike(_aim_at_nearest_red(m), 0.5), 9, "quiet-3")[1]
		check_eq(answer.get("ok"), true, "then: " + str(answer.get("error")))
		check_eq(answer.get("event"), "reracked", "the frame is racked again")
		var state := m.match_state()
		check_eq(state["reracks"], 1, "one re-rack")
		check_eq(state["frame_number"], 1, "of the same frame")
		check_eq(state["breaker"], 0, "with the same breaker")
		check_eq(state["history"], [], "nothing is counted")
		frame = m.frame()
		check_eq(answer.get("frame"), frame, "the frame handed back is the new one")
		check_eq(frame["scores"], [0, 0], "scores nil")
		check_eq(frame["strokes"], 0, "no strokes")
		check_eq(frame["striker"], 0, "the breaker to play")
		check_eq(frame["cue_in_hand"], true, "from the D")
		check_eq(frame["pending"], "none", "nothing pending")
		check_eq(frame["concede_any_time"], [true, false], "who may concede is kept")
		check_eq(m.balls().size(), 22, "all the balls")
		check(m.balls() != first_rack, "racked afresh, with other gaps")
		var racked := _logged("frame.started")
		check_eq(racked.size(), 1, "frame.started")
		if racked.size() == 1:
			check_eq(racked[0]["match_id"], id, "with the match id")
			check_eq(int(racked[0]["frame"]), 1, "the frame")
			check_eq(int(racked[0]["number"]), 1, "its number")
			check_eq(int(racked[0]["reracks"]), 1, "and that it is a re-rack")
			check_eq(racked[0]["seed"], frame["seed"], "the new rack's seed")
			check_eq(racked[0].get("shot_id", ""), "" if agreed else "quiet-3", "the stroke that brought it, if one did")
		_check_context(id)
		_check_schema()


func test_choices_that_are_not_open_are_refused() -> void:
	var fouled := _fouled(148)
	var m: OfsMatch = fouled["match"]
	var before := m.match_state()
	var cases := {
		"the offender choosing": ["replace", 0, true],
		"an answer to no offer": ["accept_rerack", 1, true],
		"a player who is not one": ["play", 7, true],
		"no such decision": ["play_on", 1, false],
		"a player past an int": ["play", 1 << 40, false],
	}
	_mark_log()
	for name: String in cases:
		var c: Array = cases[name]
		var answer := m.decide(c[0], c[1])
		check_eq(answer.get("ok"), false, name + " is refused")
		check(not str(answer.get("error")).is_empty(), name + ": with a reason")
		check_eq(m.last_error(), answer.get("error"), name + ": last_error is the reason")
	check_eq(m.match_state(), before, "the match is exactly as it was")
	var choices := _logged("rules.choice")
	check_eq(choices.size(), 3, "rules.choice for those the referee was asked")
	for line in choices:
		check_eq(line["ok"], false, "refused")
		check(not str(line["error"]).is_empty(), "with why")
	check_eq(_logged_what("OfsMatch.decide").size(), cases.size(), "rules.invalid for each")
	_check_schema()

	_mark_log()
	check_eq(m.judge(_any_record(m), {"nominated": ""}, {"shot_id": "pending"}).get("ok"), false,
			"no stroke is judged while a choice is pending")
	check(m.last_error().contains("decision"), "the error says a decision comes first: " + m.last_error())
	check_eq(m.standin_shot(1, {"shot_id": "pending"}).get("ok"), false, "nor chosen by the stand-in")
	check_eq(m.place_cue(IN_THE_D.x, IN_THE_D.y).get("ok"), false, "nor is the cue ball placed")
	check_eq(m.next_frame().get("ok"), false, "nor the next frame racked")
	check_eq(m.match_state(), before, "the match is still as it was")
	check_eq(_logged("rules.decision").size(), 0, "nothing was judged")
	check_eq(m.decide("play", 1).get("ok"), true, "the choice is still there to make")


func test_a_record_from_other_balls_is_refused() -> void:
	var m := _started(_config(3, 0, 147))
	var before := m.match_state()
	var balls := m.balls()
	var strike := _strike(_aim_at_the_pack(balls, m.cue_ball()), 2.0)
	var moved := balls.duplicate(true)
	moved[0]["y"] = float(moved[0]["y"]) + 0.01
	var fewer := balls.duplicate(true)
	fewer.pop_back()
	var other_balls := Physics.sim.simulate(moved, 0, strike, 1, _context("moved"))
	var fewer_balls := Physics.sim.simulate(fewer, 0, strike, 1, _context("fewer"))
	var right := Physics.sim.simulate(balls, 0, strike, 1, _context("right"))
	check(other_balls != null and fewer_balls != null and right != null, "simulated")
	var cases := {
		"a cue ball that was elsewhere": [other_balls, {"nominated": ""}, "not simulated from"],
		"a ball too few": [fewer_balls, {"nominated": ""}, "not simulated from"],
		"no record": [null, {"nominated": ""}, "no shot record"],
		"a ball that does not exist": [right, {"nominated": "purple"}, "nominated"],
		"a nomination that is no name": [right, {"nominated": 5}, "nominated"],
	}
	_mark_log()
	for name: String in cases:
		var c: Array = cases[name]
		var answer := m.judge(c[0], c[1], {"shot_id": "refused-" + name})
		check_eq(answer.get("ok"), false, name + " is refused")
		check(str(answer.get("error")).contains(c[2]), "%s: the error says why: %s" % [name, answer.get("error")])
		check_eq(m.last_error(), answer.get("error"), name + ": last_error is the error")
	check_eq(m.match_state(), before, "the match is exactly as it was")
	check_eq(_logged("rules.decision").size(), 0, "nothing was judged")
	var invalid := _logged_what("OfsMatch.judge")
	check_eq(invalid.size(), cases.size(), "rules.invalid for each")
	for i in mini(invalid.size(), cases.size()):
		check(str(invalid[i]["detail"]).contains("refused-" + str(cases.keys()[i])), "with the shot it was given")
	_check_schema()

	check_eq(m.judge(right, {}, {"shot_id": "right"}).get("ok"), true, "the stroke from these balls is judged")
	check_eq(m.frame()["strokes"], 1, "once")
	var twice := m.judge(right, {}, {"shot_id": "right"})
	check_eq(twice.get("ok"), false, "and not a second time: the balls have moved on")
	check_eq(m.frame()["strokes"], 1, "still one stroke")


func test_conceding_frames_to_the_end_of_the_match() -> void:
	var m := _started(_config(3, 0, 147))
	var id := _last_id()
	check_eq(m.frame()["view"]["options"], [["concede_frame", "concede_match"], []],
			"only the player allowed to may concede")
	check_eq(m.decide("concede_frame", 1).get("ok"), false, "the other may not")
	check_eq(m.next_frame().get("ok"), false, "no next frame while one is in play")
	check(m.last_error().contains("not over"), "the error says so: " + m.last_error())

	_mark_log()
	var conceded := m.decide("concede_frame", 0)
	check_eq(conceded.get("ok"), true, "conceded: " + str(conceded.get("error")))
	check_eq(conceded.get("event"), "frame_ended", "the frame ends")
	check_eq(conceded.get("calls"), [{"kind": "frame_over", "value": 1, "detail": "conceded"}], "and is called")
	var frame := m.frame()
	check_eq(frame["over"], true, "the frame is over")
	check_eq(frame["winner"], 1, "won by the other player")
	check_eq(frame["end_reason"], "conceded", "by concession")
	check_eq(frame["view"]["options"], [[], []], "nothing is open in a finished frame")
	var state := m.match_state()
	check_eq(state["frames"], [0, 1], "one frame to them")
	check_eq(state["over"], false, "the match goes on")
	check(m.in_progress(), "in progress")
	var history: Array = state["history"]
	check_eq(history.size(), 1, "the frame is summarised")
	if history.size() == 1:
		check_eq(history[0]["number"], 1, "frame 1")
		check_eq(history[0]["breaker"], 0, "its breaker")
		check_eq(history[0]["winner"], 1, "its winner")
		check_eq(history[0]["reason"], "conceded", "its reason")
	var ended := _logged("frame.ended")
	check_eq(ended.size(), 1, "frame.ended")
	if ended.size() == 1:
		check_eq(ended[0]["match_id"], id, "with the match id")
		check_eq(int(ended[0]["frame"]), 1, "the frame")
		check_eq(int(ended[0]["summary"]["winner"]), 1, "and its summary")
	check_eq(_logged("match.ended").size(), 0, "the match has not ended")

	var over := m.match_state()
	check_eq(m.judge(_any_record(m), {}, {"shot_id": "late"}).get("ok"), false, "no stroke in a finished frame")
	check(m.last_error().contains("next_frame"), "the error says what to do: " + m.last_error())
	check_eq(m.standin_shot(1, {"shot_id": "late"}).get("ok"), false, "nor a stand-in's")
	check_eq(m.decide("concede_frame", 0).get("ok"), false, "nor a second concession")
	check_eq(m.place_cue(IN_THE_D.x, IN_THE_D.y).get("ok"), false, "nor a cue ball placed")
	check_eq(m.standin_choice(0, 1), "", "nor a choice")
	check_eq(m.match_state(), over, "the match is as it was")

	_mark_log()
	var next := m.next_frame()
	check_eq(next.get("ok"), true, "the next frame: " + str(next.get("error")))
	check_eq(next.keys(), ["ok", "error", "frame"], "the answer's keys")
	check_eq(next.get("frame"), m.frame(), "the frame handed back is frame()")
	state = m.match_state()
	check_eq(state["frame_number"], 2, "frame 2")
	check_eq(state["breaker"], 1, "the other player breaks")
	check_eq(state["reracks"], 0, "not racked again yet")
	frame = m.frame()
	check_eq(frame["striker"], 1, "and plays first")
	check_eq(frame["over"], false, "a frame in play")
	check_eq(frame["scores"], [0, 0], "from nil")
	check_eq(frame["strokes"], 0, "no strokes")
	check_eq(frame["cue_in_hand"], true, "the cue ball in hand")
	check_eq(frame["concede_any_time"], [true, false], "who may concede is kept")
	check_eq(m.balls().size(), 22, "all the balls")
	var racked := _logged("frame.started")
	check_eq(racked.size(), 1, "frame.started")
	if racked.size() == 1:
		check_eq(racked[0]["match_id"], id, "with the match id")
		check_eq(int(racked[0]["frame"]), 2, "the new frame as context")
		check_eq(int(racked[0]["number"]), 2, "its number")
		check_eq(int(racked[0]["breaker"]), 1, "its breaker")
	_check_schema()

	_mark_log()
	var last := m.decide("concede_frame", 0)
	check_eq(last.get("ok"), true, "conceded again")
	check_eq(last.get("event"), "match_ended", "which ends a best of three")
	check(not m.in_progress(), "no longer in progress")
	state = m.match_state()
	check_eq(state["over"], true, "over")
	check_eq(state["winner"], 1, "won")
	check_eq(state["frames"], [0, 2], "two frames to nil")
	check_eq((state["history"] as Array).size(), 2, "both frames in the history")
	check_eq(_logged("frame.ended").size(), 1, "frame.ended")
	var finished := _logged("match.ended")
	check_eq(finished.size(), 1, "match.ended")
	if finished.size() == 1:
		check_eq(finished[0]["match_id"], id, "with the match id")
		check_eq(int(finished[0]["frame"]), 2, "the last frame")
		check_eq(int(finished[0]["winner"]), 1, "the winner")
		check_eq(finished[0]["frames"], [0.0, 2.0], "the frames")
		check_eq(int(finished[0]["frames_played"]), 2, "how many were played")
		check_eq(finished[0]["reason"], "frames", "won on frames")
	_check_context(id)
	_check_schema()

	check_eq(m.next_frame().get("ok"), false, "no frame after the match")
	check(m.last_error().contains("the match is over"), "the error says so: " + m.last_error())
	check_eq(m.decide("concede_match", 0).get("ok"), false, "nothing to concede")
	check_eq(m.judge(_any_record(m), {}, {"shot_id": "late"}).get("ok"), false, "no stroke")
	check_eq(m.match_state(), state, "the finished match stays as it ended")
	check_eq(m.start(_config(1, 0, 3), _match_id()).get("ok"), true, "and another can be started")


func test_conceding_the_match() -> void:
	var m := _started(_config(5, 0, 147))
	_mark_log()
	check_eq(m.decide("concede_match", 1).get("ok"), false, "not open to the player who needs no snookers")
	_mark_log()
	var conceded := m.decide("concede_match", 0)
	check_eq(conceded.get("ok"), true, "conceded: " + str(conceded.get("error")))
	check_eq(conceded.get("event"), "match_ended", "the match ends at once")
	check(not m.in_progress(), "no longer in progress")
	var state := m.match_state()
	check_eq(state["over"], true, "over")
	check_eq(state["winner"], 1, "the other player wins")
	check_eq(state["history"][0]["reason"] if (state["history"] as Array).size() == 1 else "", "match_conceded",
			"the frame in play is closed as conceded with the match")
	check_eq(_logged("frame.ended").size(), 1, "frame.ended")
	var finished := _logged("match.ended")
	check_eq(finished.size(), 1, "match.ended")
	if finished.size() == 1:
		check_eq(finished[0]["reason"], "conceded", "as conceded")
		check_eq(int(finished[0]["winner"]), 1, "with its winner")
	_check_schema()


func test_abandon_ends_the_match_with_no_winner() -> void:
	var m := _engine()
	_mark_log()
	m.abandon()
	check_eq(_lines().size(), 0, "nothing to abandon before a match")
	var id := _match_id()
	check_eq(m.start(_config(3, 0, 147), id).get("ok"), true, "started")
	check_eq(_stroke(m, _strike(PI, 0.2), 7, "before-quitting")[1].get("ok"), true, "a stroke")
	var frame := m.frame()
	_mark_log()
	m.abandon()
	check(not m.in_progress(), "no longer in progress")
	var state := m.match_state()
	check_eq(state.get("over"), true, "the match is over")
	check_eq(state.get("winner"), -1, "with no winner")
	check_eq(state.get("frames"), [0, 0], "and no frames")
	check_eq(m.frame(), frame, "the frame stays readable as it was left")
	check_eq(m.balls(), frame["balls"], "its balls too")
	var ended := _logged("match.ended")
	check_eq(ended.size(), 1, "match.ended")
	if ended.size() == 1:
		check_eq(ended[0]["match_id"], id, "with the match id")
		check_eq(int(ended[0]["frame"]), 1, "the frame")
		check_eq(ended[0]["reason"], "abandoned", "as abandoned")
		check_eq(int(ended[0]["winner"]), -1, "no winner")
		check_eq(int(ended[0]["frames_played"]), 0, "no frame finished")
	_check_schema()

	_mark_log()
	m.abandon()
	check_eq(_lines().size(), 0, "abandoning again does nothing")
	check_eq(m.decide("play", 1).get("ok"), false, "no choice in an abandoned match")
	check(m.last_error().contains("abandoned"), "the error says so: " + m.last_error())
	check_eq(m.judge(_any_record(m), {}, {"shot_id": "late"}).get("ok"), false, "no stroke")
	check_eq(m.next_frame().get("ok"), false, "no next frame")
	check_eq(m.match_state(), state, "it stays as it was left")
	check_eq(m.start(_config(3, 0, 148), _match_id()).get("ok"), true, "a new match starts")
	check(m.in_progress(), "and is in progress")


func test_forty_strokes_by_the_standin() -> void:
	var m := _engine()
	_mark_log()
	var ids: Array[String] = []
	var strokes := 0
	var steps := 0
	while strokes < 40 and steps < 400:
		steps += 1
		if not m.in_progress():
			ids.append(_match_id())
			check_eq(m.start(_config(35, -1, 1000 + steps), ids.back()).get("ok"), true, "a match starts")
			continue
		var frame := m.frame()
		var where := "step %d (frame %d, stroke %d)" % [steps, int(m.match_state()["frame_number"]), strokes]
		if bool(frame["over"]):
			check_eq(m.next_frame().get("ok"), true, where + ": the next frame is racked: " + m.last_error())
			continue
		if frame["pending"] != "none":
			var decider: int = frame["decider"]
			var kind := m.standin_choice(decider, 3000 + steps)
			check((frame["view"]["options"][decider] as Array).has(kind),
					"%s: the stand-in's choice \"%s\" is one offered" % [where, kind])
			check_eq(m.decide(kind, decider).get("ok"), true, where + ": and is applied: " + m.last_error())
			continue
		var shot_id := "standin-%d" % steps
		var shot := m.standin_shot(2000 + steps, {"shot_id": shot_id})
		check_eq(shot.get("ok"), true, where + ": a stand-in stroke: " + str(shot.get("error")))
		check(shot.get("why") is Dictionary and float(shot.get("us", -1.0)) >= 0.0, where + ": with why and how long")
		if shot.has("decision"):
			check(not shot.has("strike"), where + ": a decision comes with no strike")
			check_eq(m.decide(shot["decision"], frame["striker"]).get("ok"), true,
					where + ": the stand-in's decision is applied: " + m.last_error())
			continue
		var keys := shot.keys()
		keys.sort()
		check_eq(keys, ["cue", "error", "nominated", "ok", "places_cue", "strike", "us", "why"], where + ": keys")
		if bool(shot.get("places_cue", false)):
			var cue: Array = shot["cue"]
			check_eq(m.place_cue(cue[0], cue[1]).get("ok"), true, where + ": its cue ball is placed: " + m.last_error())
		var record := Physics.sim.simulate(m.balls(), m.cue_ball(), shot.get("strike", {}), 4000 + steps,
				_context(shot_id, "opponent"))
		check(record != null, where + ": its strike is simulated: " + Physics.sim.last_error())
		var answer := m.judge(record, {"nominated": shot.get("nominated", "")}, {"shot_id": shot_id})
		check_eq(answer.get("ok"), true, where + ": and judged: " + str(answer.get("error")))
		check_eq(m.last_error(), "", where + ": no error")
		strokes += 1
		if not failures.is_empty():
			return
	check_eq(strokes, 40, "forty strokes played in %d steps" % steps)
	check_eq(_logged("rules.decision").size(), 40, "each judged once")
	check_eq(_logged("app.error").size(), 0, "nothing threw")
	var shots := 0
	for line in _logged("opponent.decision"):
		check(ids.has(line["match_id"]), "opponent.decision carries its match")
		check(line.get("seed") is String and str(line["seed"]).is_valid_int(), "and the seed it was made from")
		check(line.get("why") is Dictionary, "and why")
		if line["kind"] == "shot":
			shots += 1
			check(str(line["shot_id"]).begins_with("standin-"), "and its shot")
			check((line["strike"] as Dictionary).has("speed_m_s"), "with the strike")
		else:
			check(not line.has("shot_id"), "a choice strikes nothing: no shot_id")
	check_eq(shots, 40, "an opponent.decision for each stroke")
	for id in ids:
		_check_context(id)
	_check_schema()


func test_standin_choice_for_a_pending_decision() -> void:
	var fouled := _fouled(149)
	var m: OfsMatch = fouled["match"]
	_mark_log()
	var kind := m.standin_choice(1, 77)
	check(AFTER_FOUL.has(kind), "one of the options: \"%s\"" % kind)
	check_eq(m.last_error(), "", "no error")
	check_eq(m.standin_choice(1, 77), kind, "the same seed chooses the same")
	check_eq(m.frame()["pending"], "after_foul", "choosing applies nothing")
	var lines := _logged("opponent.decision")
	check_eq(lines.size(), 2, "opponent.decision each time")
	for line in lines:
		check_eq(line["kind"], "choice", "a choice")
		check_eq(int(line["player"]), 1, "by the decider")
		check_eq(line["choice"], kind, "what was chosen")
		check_eq(line["options"], AFTER_FOUL, "among what was offered")
		check_eq(line["match_id"], fouled["id"], "with the match id")
		check_eq(int(line["frame"]), 1, "and the frame")
		check_eq(line.get("seed"), "77", "the seed, to make it again")
		check(not line.has("shot_id"), "no stroke")
		var why: Dictionary = line.get("why", {})
		check(why.get("rule") in AFTER_FOUL, "why names the rule it took: " + str(why))
		check_eq(why.get("options"), AFTER_FOUL, "what it weighed")
		check(why.has("scores") and why.has("points_remaining"), "with the scores")
	check_eq(m.standin_choice(1, -1), kind, "a negative seed is the uint64 of its bits")
	var last: Array[Dictionary] = _logged("opponent.decision")
	check_eq(last.back().get("seed") if not last.is_empty() else "", "18446744073709551615", "as logged")

	_mark_log()
	check_eq(m.standin_choice(0, 77), "", "nothing for the player with nothing to choose")
	check(m.last_error().contains("nothing is open"), "last_error says so: " + m.last_error())
	check_eq(m.standin_choice(2, 77), "", "nor for a player who is not one")
	check_eq(_logged_what("OfsMatch.standin_choice").size(), 2, "both logged")
	_check_schema()
	check_eq(m.decide(kind, 1).get("ok"), true, "the choice is applied")
	check_eq(m.frame()["pending"], "none", "and play goes on")


func test_nothing_errors_before_configure_or_start() -> void:
	var record := Physics.sim.predict(Physics.sim.opening_rack(1), 0, _strike(0.0, 1.0))
	for configured: bool in [false, true]:
		var m := _engine() if configured else OfsMatch.new()
		var why := "no match has been started" if configured else "not configured"
		_mark_log()
		check_eq(m.is_ready(), configured, "is_ready")
		check(not m.in_progress(), "not in progress")
		check_eq(m.match_state(), {}, "no match state")
		check_eq(m.frame(), {}, "no frame")
		check_eq(m.balls(), [], "no balls")
		check_eq(m.cue_ball(), 0, "no cue ball")
		check_eq(m.last_error(), "", "no error from asking")
		m.abandon()
		check_eq(_lines().size(), 0, "none of that is logged")
		var answers: Array[Dictionary] = [
			m.place_cue(IN_THE_D.x, IN_THE_D.y),
			m.judge(record, {"nominated": ""}, {"shot_id": "early"}),
			m.judge(null, {}, {}),
			m.decide("concede_match", 0),
			m.next_frame(),
			m.standin_shot(1, {"shot_id": "early"}),
		]
		for answer in answers:
			check_eq(answer.get("ok"), false, "refused")
			check(str(answer.get("error")).contains(why), "because %s: %s" % [why, answer.get("error")])
		check(m.last_error().contains(why), "last_error says so too")
		check_eq(m.nearest_cue_placement(IN_THE_D.x, IN_THE_D.y), {}, "no nearest place")
		check_eq(m.standin_choice(0, 1), "", "no choice")
		check_eq(_logged("app.error").size(), 0, "nothing threw")
		check(_logged("rules.invalid").size() >= answers.size() + 1, "each refusal is logged")
		_check_schema()
		check(not m.in_progress(), "still no match")
		check_eq(m.match_state(), {}, "and no state")


## A configured engine.
func _engine() -> OfsMatch:
	var m := OfsMatch.new()
	check(m.configure(Physics.sim, _text(REFEREE_PATH), _text(STANDIN_PATH)), "configure: " + m.last_error())
	return m


func _started(config: Dictionary) -> OfsMatch:
	var m := _engine()
	var answer := m.start(config, _match_id())
	check_eq(answer.get("ok"), true, "start: " + str(answer.get("error")))
	return m


func _match_id() -> String:
	_matches += 1
	return _last_id()


func _last_id() -> String:
	return "binding-test-m%d" % _matches


## Player 0 may concede a frame at any time, as the person may; player 1 only when the rules allow.
func _config(best_of: int, first_breaker: int, seed: int) -> Dictionary:
	return {"best_of": best_of, "first_breaker": first_breaker, "seed": seed, "concede_any_time": [true, false]}


func _changed(config: Dictionary, key: String, value: Variant) -> Dictionary:
	var out := config.duplicate(true)
	out[key] = value
	return out


func _without(config: Dictionary, key: String) -> Dictionary:
	var out := config.duplicate(true)
	out.erase(key)
	return out


func _strike(aim: float, speed: float) -> Dictionary:
	return {"aim_rad": aim, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": speed}


## The aim from the cue ball at the centre of the red nearest it.
func _aim_at_nearest_red(m: OfsMatch) -> float:
	var balls := m.balls()
	var cue: Dictionary = balls[m.cue_ball()]
	var from := Vector2(cue["x"], cue["y"])
	var to := from
	var nearest := INF
	for b: Dictionary in balls:
		var at := Vector2(b["x"], b["y"])
		if b["ball"] == "red" and from.distance_to(at) < nearest:
			nearest = from.distance_to(at)
			to = at
	return (to - from).angle()


func _context(shot_id: String, strike_mode: String = "harness") -> Dictionary:
	return {"shot_id": shot_id, "mode": "match", "strike_mode": strike_mode}


## Plays `strike` from the match's balls and has it judged: [the record, judge's answer].
func _stroke(m: OfsMatch, strike: Dictionary, seed: int, shot_id: String, nominated: String = "") -> Array:
	var record := Physics.sim.simulate(m.balls(), m.cue_ball(), strike, seed, _context(shot_id))
	check(record != null, shot_id + " is simulated: " + Physics.sim.last_error())
	return [record, m.judge(record, {"nominated": nominated}, {"shot_id": shot_id})]


## A match in which player 0, breaking from IN_THE_D, has rolled the cue ball towards the baulk
## cushion and hit nothing: {match, id, before (the balls before the stroke), judgement}.
func _fouled(seed: int) -> Dictionary:
	var m := _started(_config(3, 0, seed))
	check_eq(m.place_cue(IN_THE_D.x, IN_THE_D.y).get("ok"), true, "the cue ball is placed")
	var before := m.balls()
	var answer: Dictionary = _stroke(m, _strike(PI, 0.2), 7, "foul")[1]
	check_eq(answer.get("ok"), true, "the foul stroke is judged: " + str(answer.get("error")))
	check_eq(m.frame().get("pending"), "after_foul", "and leaves a choice")
	return {"match": m, "id": _last_id(), "before": before, "judgement": answer.get("judgement", {})}


## Some shot record, for calls that must refuse before they look at it.
func _any_record(m: OfsMatch) -> OfsShotRecord:
	return Physics.sim.predict(m.balls(), m.cue_ball(), _strike(0.0, 1.0))


func _cue(m: OfsMatch) -> Vector2:
	var cue: Dictionary = m.balls()[m.cue_ball()]
	return Vector2(cue["x"], cue["y"])


## The aim from the cue ball at the red furthest up the table on its side.
func _aim_at_the_pack(balls: Array, cue_ball: int) -> float:
	var cue: Dictionary = balls[cue_ball]
	var target := {}
	for b: Dictionary in balls:
		if b["ball"] != "red" or signf(b["y"]) != signf(cue["y"]):
			continue
		if target.is_empty() or float(b["x"]) > float(target["x"]) + 1e-3 or (
				absf(float(b["x"]) - float(target["x"])) <= 1e-3 and absf(b["y"]) > absf(target["y"])):
			target = b
	check(not target.is_empty(), "a red to aim at")
	if target.is_empty():
		return 0.0
	return atan2(float(target["y"]) - float(cue["y"]), float(target["x"]) - float(cue["x"]))


func _potted(record: OfsShotRecord, ball: int) -> bool:
	return int(record.final_balls()[ball]["pocket"]) >= 0


## The balls the match holds after `record` was judged, against where the record left them: a ball
## still on the table lies where it stopped, a potted red is gone, a potted colour is on a spot and
## a potted cue ball is back in hand.
func _check_balls_after(record: OfsShotRecord, before: Array, m: OfsMatch, judgement: Dictionary) -> void:
	var now := {}
	for b: Dictionary in m.balls():
		now["%s %d" % [b["ball"], b["index"]]] = b
	var spotted := {}
	for b: Dictionary in judgement["spotted"]:
		spotted["%s %d" % [b["ball"], b["index"]]] = b
	var final := record.final_balls()
	var reds_potted := 0
	for i in final.size():
		var f: Dictionary = final[i]
		var name := "%s %d" % [f["ball"], f["index"]]
		check_eq(f["ball"], before[i]["ball"], "the record's ball %d is the frame's" % i)
		if int(f["pocket"]) < 0:
			check(now.has(name), name + " is still on the table")
			if now.has(name):
				check_eq(now[name]["x"], f["x"], name + " x is where it stopped")
				check_eq(now[name]["y"], f["y"], name + " y is where it stopped")
			check(not spotted.has(name), name + " was not spotted")
		elif f["ball"] == "red":
			reds_potted += 1
			check(not now.has(name), name + " was potted and is gone")
		elif f["ball"] == "cue":
			check(now.has(name), "the potted cue ball is listed, in hand")
		else:
			check(now.has(name) and spotted.has(name), name + " was potted and is spotted")
			if now.has(name) and spotted.has(name):
				check_eq(now[name], spotted[name], name + " lies where the judgement says it was spotted")
	check_eq(m.balls().size(), before.size() - reds_potted, "only potted reds leave the table")
	check_eq(m.balls()[m.cue_ball()]["ball"], "cue", "cue_ball() is the cue ball's place")


func _text(path: String) -> String:
	return FileAccess.get_file_as_string(path)


func _fixture(path: String) -> Variant:
	return JSON.parse_string(_text(path))


## `got` against a fixture read from JSON: the same keys, values of the same kind. A list is
## compared entry by entry with the fixture's first, when both have entries.
func _check_shape(got: Variant, want: Variant, where: String) -> void:
	if want is Dictionary:
		if not got is Dictionary:
			check(false, "%s: want a Dictionary, got %s" % [where, type_string(typeof(got))])
			return
		var got_keys: Array = (got as Dictionary).keys()
		var want_keys: Array = (want as Dictionary).keys()
		got_keys.sort()
		want_keys.sort()
		check_eq(got_keys, want_keys, where + ": keys")
		for key: String in want:
			if (got as Dictionary).has(key):
				_check_shape(got[key], want[key], where + "." + key)
	elif want is Array:
		if not got is Array:
			check(false, "%s: want an Array, got %s" % [where, type_string(typeof(got))])
			return
		if not (want as Array).is_empty():
			for i in (got as Array).size():
				_check_shape(got[i], want[0], "%s[%d]" % [where, i])
	elif want is float:
		check(got is int or got is float, "%s: want a number, got %s" % [where, type_string(typeof(got))])
	else:
		check_eq(typeof(got), typeof(want), where + ": type")


## Every number is an int, as the game indexes with them, but a position's x and y.
func _check_numbers(value: Variant, where: String, key: String = "") -> void:
	if value is Dictionary:
		for k: String in value:
			_check_numbers(value[k], where + "." + k, k)
	elif value is Array:
		for i in (value as Array).size():
			_check_numbers(value[i], "%s[%d]" % [where, i], key)
	elif value is float:
		check(key == "x" or key == "y", where + " is a float")
	elif value is int:
		check(key != "x" and key != "y", where + " is an int")


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


## Every line logged since _mark_log, read from the session file.
func _lines() -> Array[Dictionary]:
	OfsLog.flush()
	var out: Array[Dictionary] = []
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary:
			out.append(parsed)
		else:
			check(false, "a log line that is not JSON: " + line.left(120))
	return out


## The data of the events logged since _mark_log.
func _logged(event: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for line in _lines():
		if line.get("event") == event:
			found.append(line.get("data", {}))
	return found


## The rules.invalid lines since _mark_log for one call.
func _logged_what(what: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for data in _logged("rules.invalid"):
		if data.get("what") == what:
			found.append(data)
	return found


## Every line a match logs carries its id and frame number; a stroke's lines its shot id.
func _check_context(match_id: String) -> void:
	for line in _lines():
		var event := str(line.get("event"))
		var data: Dictionary = line.get("data", {})
		if not str(line.get("subsystem")) in ["rules", "match", "opponent"] or data.get("match_id") != match_id:
			continue
		check(data.get("frame") is float and float(data["frame"]) >= 1.0, event + " carries the frame number")
		if event == "rules.decision" or (event == "opponent.decision" and data.get("kind") == "shot"):
			check(not str(data.get("shot_id", "")).is_empty(), event + " carries the shot id")
	for event: String in ["rules.decision", "rules.choice", "rules.cue_placed", "match.started", "match.ended",
			"frame.started", "frame.ended", "opponent.decision"]:
		for data in _logged(event):
			check(data.has("match_id") and data.has("frame"), event + " carries match_id and frame")


## Every line since _mark_log against log_schema.json, as tools/check_logs.py reads it.
func _check_schema() -> void:
	var events: Dictionary = (_fixture(SCHEMA_PATH) as Dictionary)["events"]
	for line in _lines():
		var event := str(line.get("event"))
		if not events.has(event):
			check(false, "an event the schema does not have: " + event)
			continue
		var spec: Dictionary = events[event]
		check_eq(line.get("subsystem"), spec["subsystem"], event + ": subsystem")
		check((spec["levels"] as Array).has(line.get("level")), "%s: level %s" % [event, line.get("level")])
		var data: Dictionary = line.get("data", {})
		var fields: Dictionary = spec["fields"]
		for key: String in data:
			check(fields.has(key), "undeclared field %s on %s" % [key, event])
		for field: String in fields:
			if not data.has(field):
				check(not bool(fields[field].get("required", false)), "%s: missing %s" % [event, field])
				continue
			check(_fits(data[field], fields[field].get("type", "any")),
					"%s: %s is not %s: %s" % [event, field, fields[field].get("type"), str(data[field]).left(80)])


func _fits(value: Variant, type: String) -> bool:
	match type:
		"integer":
			return value is float and value == floorf(value)
		"number":
			return value is float
		"string":
			return value is String
		"boolean":
			return value is bool
		"object":
			return value is Dictionary
		"array":
			return value is Array
	return true
