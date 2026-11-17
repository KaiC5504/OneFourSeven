extends TestCase
## OfsMatch's computer opponent on the real engine and the real files: the levels and exhibition
## tables, a level thinking on its worker thread, taking, cancelling and refusing its answer, and
## its choices. Nothing here depends on how well a level plays.

const REFEREE_PATH := "res://data/referee_params.json"
const STANDIN_PATH := "res://data/standin_params.json"
const BEGINNER_PATH := "res://data/opponent_beginner.json"
const MAXIMUM_PATH := "res://data/opponent_maximum.json"
const EXHIBITIONS_PATH := "res://data/exhibitions.json"
const SCHEMA_PATH := "res://data/log_schema.json"
const LEVELS: Array[String] = ["beginner", "maximum"]
const AFTER_FOUL: Array[String] = ["play", "play_again", "replace"]
const STRIKE_KEYS: Array[String] = ["aim_rad", "elevation_rad", "speed_m_s", "tip_x", "tip_y"]
const THINK_KEYS: Array[String] = ["candidates", "depth", "ms", "sims", "steps", "stopped"]
## Inside the D, clear of the baulk colours.
const IN_THE_D := Vector2(-1.2, 0.1)
## Far past any budget a test gives a level.
const ANSWER_WAIT_MS := 15000

var _log_start := 0
var _matches := 0


func test_configure_opponent_takes_the_real_files() -> void:
	var m := _engine()
	_mark_log()
	check(m.configure_opponent(_level_files(), _text(EXHIBITIONS_PATH)), "configure_opponent: " + m.last_error())
	check_eq(m.last_error(), "", "no error")
	var levels := m.levels()
	var ids: Array = levels.map(func(l: Dictionary) -> String: return l["id"])
	check_eq(ids, ["beginner", "maximum"], "Beginner first, the 147 last")
	for level: Dictionary in levels:
		var keys := level.keys()
		keys.sort()
		check_eq(keys, ["id", "maximum", "name", "tier"], level.get("id", "?") + ": keys")
		var file: Dictionary = _fixture("res://data/opponent_%s.json" % level["id"])
		check_eq(level["name"], file["values"]["name"], level["id"] + ": its name")
		check_eq(level["tier"], int(file["values"]["tier"]), level["id"] + ": its tier, an int")
		check_eq(level["maximum"], file["values"]["maximum"], level["id"] + ": whether it plays for a maximum")
	check_eq(Array(m.exhibitions()), _layout_names(), "the exhibition tables, in the file's order")
	var lines := _logged("rules.configured")
	check_eq(lines.size(), 1, "one rules.configured")
	if lines.size() == 1:
		check_eq(lines[0]["ok"], true, "ok")
		check_eq(lines[0]["problems"], [], "no problems")
		var logged: Array = (lines[0]["levels"] as Array).map(func(l: Dictionary) -> String: return l["id"])
		check_eq(logged, ["beginner", "maximum"], "each level's values")
		check((lines[0]["levels"][0] as Dictionary).has("aim_sigma_rad"), "in full")
		check_eq(lines[0]["exhibitions"], _layout_names(), "and the tables' names")
	_check_schema()


func test_configure_opponent_refusals_keep_what_was_there() -> void:
	var exhibitions := _text(EXHIBITIONS_PATH)
	var fresh := OfsMatch.new()
	check(not fresh.configure_opponent(_level_files(), exhibitions), "refused before configure")
	check(fresh.last_error().contains("not configured"), "last_error says so: " + fresh.last_error())
	check_eq(fresh.levels(), [], "no levels")

	var m := _opponent()
	var levels := m.levels()
	var cases := {
		"a file under another level's key": [{"beginner": _text(MAXIMUM_PATH)}, exhibitions,
				"the file is level maximum"],
		"a broken file": [{"beginner": "{not json", "maximum": _text(MAXIMUM_PATH)}, exhibitions,
				"beginner: not valid JSON"],
		"a file missing a value": [{"beginner": _text(BEGINNER_PATH).replace('"max_sims"', '"max_simz"')},
				exhibitions, "beginner: max_sims"],
		"no levels": [{}, exhibitions, "levels: none given"],
		"broken exhibitions": [_level_files(), "{\"layouts\": [", "exhibitions: not valid JSON"],
		"an exhibition table missing a ball": [_level_files(),
				JSON.stringify({"schema_version": 1, "layouts": [{"name": "bare", "cue_ball": 0,
						"balls": [{"ball": "cue", "index": 0, "x": 0.0, "y": 0.0}]}]}),
				"exhibitions: bare"],
	}
	_mark_log()
	for name: String in cases:
		var c: Array = cases[name]
		check(not m.configure_opponent(c[0], c[1]), name + " is refused")
		check(m.last_error().contains(c[2]), "%s: last_error names it: %s" % [name, m.last_error()])
		check_eq(m.levels(), levels, name + ": the levels are as they were")
		check_eq(Array(m.exhibitions()), _layout_names(), name + ": and the tables")
	var lines := _logged("rules.configured")
	check_eq(lines.size(), cases.size(), "rules.configured for each attempt")
	for line in lines:
		check_eq(line["ok"], false, "ok false")
		check(not (line["problems"] as Array).is_empty(), "problems listed")
	_check_schema()

	check_eq(m.start(_config(1, 0, 3), _match_id()).get("ok"), true, "a match starts")
	check(not m.configure_opponent(_level_files(), exhibitions), "not while a match is in progress")
	check(m.last_error().contains("a match is in progress"), "last_error says so: " + m.last_error())
	m.abandon()
	check(m.configure_opponent(_level_files(), exhibitions), "again once it is left: " + m.last_error())


func test_start_with_a_level() -> void:
	var m := _opponent()
	for level: String in LEVELS:
		_mark_log()
		var id := _match_id()
		var config := _config(3, 0, 11)
		config["level"] = level
		check_eq(m.start(config, id).get("ok"), true, level + ": starts: " + m.last_error())
		var started := _logged("match.started")
		check_eq(started.size(), 1, level + ": match.started")
		if started.size() == 1:
			check_eq(started[0].get("level"), level, level + ": carries the level")
			check(not started[0].has("exhibition"), level + ": and no exhibition")
		check_eq(_logged("exhibition.started").size(), 0, level + ": no exhibition.started")
		_check_schema()

	var before := m.match_state()
	_mark_log()
	for bad: Variant in ["nobody", 5]:
		var config := _config(3, 0, 11)
		config["level"] = bad
		check_eq(m.start(config, "never").get("ok"), false, "level %s is refused" % str(bad))
		check(m.last_error().contains("level"), "last_error says why: " + m.last_error())
	check_eq(m.match_state(), before, "the match in progress is as it was")
	check_eq(_logged("match.started").size(), 0, "nothing started")
	check_eq(_logged_what("OfsMatch.start").size(), 2, "both refusals logged")

	var none := _config(3, 0, 11)
	none["level"] = ""
	_mark_log()
	check_eq(m.start(none, _match_id()).get("ok"), true, "\"\" is no level")
	var started := _logged("match.started")
	check(started.size() == 1 and not started[0].has("level"), "match.started without one")
	var plain := OfsMatch.new()
	check(plain.configure(Physics.sim, _text(REFEREE_PATH), _text(STANDIN_PATH)), "configure: " + plain.last_error())
	var asked := _config(1, 0, 1)
	asked["level"] = "beginner"
	check_eq(plain.start(asked, "never").get("ok"), false, "a level before configure_opponent is refused")


func test_start_an_exhibition() -> void:
	var m := _opponent()
	var layout: Dictionary = (_fixture(EXHIBITIONS_PATH) as Dictionary)["layouts"][0]
	for person: int in [0, 1, -1]:
		var config := _config(1, 0, 21)
		config["exhibition"] = 0
		config["level"] = "maximum"
		if person >= 0:
			config["person"] = person
		var striker := 1 - person if person >= 0 else 1
		var where := "person %d" % person
		_mark_log()
		var id := _match_id()
		check_eq(m.start(config, id).get("ok"), true, where + ": starts: " + m.last_error())
		var balls := m.balls()
		check_eq(balls.size(), (layout["balls"] as Array).size(), where + ": the layout's balls")
		for i in mini(balls.size(), (layout["balls"] as Array).size()):
			var want: Dictionary = layout["balls"][i]
			var got: Dictionary = balls[i]
			check(got["ball"] == want["ball"] and int(got["index"]) == int(want["index"]) and
					is_equal_approx(float(got["x"]), float(want["x"])) and
					is_equal_approx(float(got["y"]), float(want["y"])),
					"%s: ball %d is %s, got %s" % [where, i, str(want), str(got)])
		check_eq(m.cue_ball(), int(layout["cue_ball"]), where + ": the layout's cue ball")
		var frame := m.frame()
		check_eq(frame["striker"], striker, where + ": the opponent at the table")
		check_eq(frame["on"], {"kind": "red"}, where + ": red on")
		check_eq(frame["cue_in_hand"], false, where + ": the cue ball not in hand")
		check_eq(frame["pending"], "none", where + ": nothing pending")
		check_eq(frame["scores"], [0, 0], where + ": no score")
		var started := _logged("match.started")
		check_eq(started.size(), 1, where + ": match.started")
		if started.size() == 1:
			check_eq(started[0].get("exhibition"), layout["name"], where + ": names the table")
			check_eq(started[0].get("level"), "maximum", where + ": and the level")
			check_eq(int(started[0].get("breaker", -1)), striker, where + ": the opponent breaks the table")
		var shown := _logged("exhibition.started")
		check_eq(shown.size(), 1, where + ": exhibition.started")
		if shown.size() == 1:
			check_eq(shown[0], {"layout": layout["name"], "level": "maximum", "match_id": id}, where + ": its line")
		check_eq(_logged("frame.started").size(), 1, where + ": frame.started")
		_check_schema()

	var before := m.match_state()
	var past_the_last: int = ((_fixture(EXHIBITIONS_PATH) as Dictionary)["layouts"] as Array).size()
	for bad: Array in [[3, 0, "best of 3"], [1, past_the_last, "an index past the last table"], [1, -1, "index -1"],
			[1, "0", "a String"]]:
		var config := _config(bad[0], 0, 21)
		config["exhibition"] = bad[1]
		check_eq(m.start(config, "never").get("ok"), false, bad[2] + " is refused")
		check(m.last_error().contains("exhibition"), bad[2] + ": last_error says why: " + m.last_error())
	check_eq(m.match_state(), before, "the exhibition in progress is as it was")


func test_a_level_thinks_and_its_stroke_is_played() -> void:
	for level: String in LEVELS:
		var m := _opponent()
		var config := _config(3, 1, 31)
		config["level"] = level
		var id := _match_id()
		check_eq(m.start(config, id).get("ok"), true, "starts")
		check_eq(m.opponent_take(), {}, level + ": nothing before it is asked")
		check(not m.opponent_thinking(), level + ": not thinking")
		_mark_log()
		var shot_id := "think-" + level
		var asked := Time.get_ticks_usec()
		check(m.opponent_think(level, 5, {"shot_id": shot_id}, {}), level + ": thinks: " + m.last_error())
		check(m.opponent_thinking(), level + ": thinking")
		check(not m.opponent_think(level, 6, {"shot_id": "again"}, {}), level + ": a second think is refused")
		check(m.last_error().contains("thinking") or m.last_error().contains("not been taken"),
				level + ": last_error says why: " + m.last_error())
		# Until the search is built a level answers in well under a frame, so this may already have it.
		var early := m.opponent_take()
		if early.is_empty():
			check(m.opponent_thinking(), level + ": still thinking when taken too early")
		var answer := early if not early.is_empty() else await _answer(m)
		var waited_ms := (Time.get_ticks_usec() - asked) / 1000.0
		check(not m.opponent_thinking(), level + ": done once taken")
		check_eq(m.opponent_take(), {}, level + ": and taken once")
		check_eq(answer.get("ok"), true, level + ": an answer: " + str(answer.get("error")))
		var keys := answer.keys()
		keys.sort()
		check_eq(keys, ["cue", "error", "level", "meant", "nominated", "ok", "places_cue", "strike", "think", "us",
				"why"], level + ": keys")
		check_eq(answer.get("level"), level, level + ": names the level")
		for key: String in ["strike", "meant"]:
			var strike: Dictionary = answer.get(key, {})
			var got := strike.keys()
			got.sort()
			check_eq(got, STRIKE_KEYS, "%s: %s's keys" % [level, key])
		var think: Dictionary = answer.get("think", {})
		var got_think := think.keys()
		got_think.sort()
		check_eq(got_think, THINK_KEYS, level + ": think's keys")
		check(think.get("stopped") in ["done", "sims", "steps", "time"], level + ": how it stopped: " + str(think))
		check(float(think.get("ms", -1.0)) >= 0.0 and float(think["ms"]) <= waited_ms + 1.0,
				"%s: its own time, %s ms, within the %.1f ms waited" % [level, str(think.get("ms")), waited_ms])
		var judged := _play(m, answer, shot_id)
		check_eq(judged.get("ok"), true, level + ": played and judged: " + str(judged.get("error")))

		var thought := _logged("opponent.think")
		check_eq(thought.size(), 1, level + ": opponent.think once, the refused one not")
		if thought.size() == 1:
			check_eq(thought[0]["level"], level, level + ": think: the level")
			check_eq(int(thought[0]["player"]), 1, level + ": the striker")
			check_eq(thought[0]["seed"], "5", level + ": the seed as a decimal string")
			check_eq(thought[0]["shot_id"], shot_id, level + ": the shot")
			check_eq(thought[0]["match_id"], id, level + ": the match")
			check_eq(int(thought[0]["frame"]), 1, level + ": the frame")
			check_eq(thought[0]["budget"], {"max_sims": 0.0, "max_steps": 0.0, "max_ms": 1500.0},
					level + ": the default budget")
		var decided := _logged("opponent.decision")
		check_eq(decided.size(), 1, level + ": opponent.decision")
		if decided.size() == 1:
			var line := decided[0]
			check_eq(line["kind"], "shot", level + ": a stroke")
			check_eq(line["level"], level, level + ": by the level")
			check_eq(line["seed"], "5", level + ": with the seed")
			check_eq(line["shot_id"], shot_id, level + ": and the shot")
			check_eq(line["match_id"], id, level + ": in the match")
			check((line["meant"] as Dictionary).has("speed_m_s"), level + ": the stroke meant")
			check_eq((line["think"] as Dictionary).get("stopped"), think.get("stopped"), level + ": how it thought")
			check_eq((line["why"] as Dictionary).get("level"), level, level + ": why names the level")
		check_eq(_logged("app.error").size(), 0, level + ": nothing threw")
		_check_schema()


func test_twelve_strokes_alternating_levels_are_never_refused() -> void:
	var m := _opponent()
	var config := _config(3, 0, 41)
	config["level"] = "beginner"
	check_eq(m.start(config, _match_id()).get("ok"), true, "starts")
	_mark_log()
	var strokes := 0
	var steps := 0
	while strokes < 12 and steps < 60:
		steps += 1
		var level := LEVELS[strokes % 2]
		var where := "step %d (stroke %d, %s)" % [steps, strokes, level]
		if not m.in_progress():
			check_eq(m.start(config, _match_id()).get("ok"), true, where + ": another match")
			continue
		var frame := m.frame()
		if bool(frame["over"]):
			check_eq(m.next_frame().get("ok"), true, where + ": the next frame: " + m.last_error())
			continue
		if frame["pending"] != "none":
			var decider: int = frame["decider"]
			var kind := m.opponent_choice(level, decider, 500 + steps)
			check((frame["view"]["options"][decider] as Array).has(kind), "%s: the choice %s is offered" % [where, kind])
			check_eq(m.decide(kind, decider).get("ok"), true, where + ": and applied: " + m.last_error())
			continue
		var shot_id := "twelve-%d" % steps
		check(m.opponent_think(level, 700 + steps, {"shot_id": shot_id}, {}), where + ": thinks: " + m.last_error())
		var answer := await _answer(m)
		check_eq(answer.get("ok"), true, where + ": answers: " + str(answer.get("error")))
		if answer.has("decision"):
			check_eq(m.decide(answer["decision"], frame["striker"]).get("ok"), true, where + ": its decision applies")
			continue
		var judged := _play(m, answer, shot_id)
		check_eq(judged.get("ok"), true, where + ": played and judged: " + str(judged.get("error")))
		strokes += 1
		if not failures.is_empty():
			return
	check_eq(strokes, 12, "twelve strokes in %d steps" % steps)
	check_eq(_logged_what("OfsMatch.opponent_take").size(), 0, "none refused")
	check_eq(_logged("app.error").size(), 0, "nothing threw")
	_check_schema()


func test_cancel_drops_the_answer() -> void:
	var m := _started_with("beginner")
	_mark_log()
	m.opponent_cancel("nothing to stop")
	check_eq(_logged("opponent.cancelled").size(), 0, "cancelling nothing logs nothing")
	check(m.opponent_think("beginner", 3, {"shot_id": "cancelled"}, {}), "thinks: " + m.last_error())
	m.opponent_cancel("the test")
	check(not m.opponent_thinking(), "not thinking once cancelled")
	check_eq(m.opponent_take(), {}, "and no answer")
	for i in 3:
		await tree.process_frame
	check_eq(m.opponent_take(), {}, "nor later")
	var lines := _logged("opponent.cancelled")
	check_eq(lines.size(), 1, "opponent.cancelled")
	if lines.size() == 1:
		check_eq(lines[0]["level"], "beginner", "the level")
		check_eq(lines[0]["why"], "the test", "why")
		check(float(lines[0]["ms"]) >= 0.0, "how long it had thought")
		check_eq(lines[0]["match_id"], _last_id(), "the match")
	check(m.opponent_think("beginner", 3, {"shot_id": "after"}, {}), "a new think works: " + m.last_error())
	var answer := await _answer(m)
	check_eq(answer.get("ok"), true, "and answers: " + str(answer.get("error")))

	check(m.opponent_think("beginner", 4, {"shot_id": "abandoned"}, {}), "thinks: " + m.last_error())
	m.abandon()
	check(not m.opponent_thinking(), "abandon stops it")
	var config := _config(1, 0, 5)
	config["level"] = "beginner"
	check_eq(m.start(config, _match_id()).get("ok"), true, "a new match")
	check(m.opponent_think("beginner", 5, {"shot_id": "replaced"}, {}), "thinks: " + m.last_error())
	check_eq(m.start(config, _match_id()).get("ok"), true, "another new match")
	check(not m.opponent_thinking(), "start stops it")
	var whys: Array = _logged("opponent.cancelled").map(func(l: Dictionary) -> String: return l["why"])
	check_eq(whys, ["the test", "the match was abandoned", "a new match"], "each cancellation logged")
	_check_schema()


func test_an_answer_for_a_frame_that_changed_is_refused() -> void:
	var m := _started_with("beginner")
	_mark_log()
	check(m.opponent_think("beginner", 9, {"shot_id": "stale"}, {}), "thinks: " + m.last_error())
	var shot := m.standin_shot(9, {"shot_id": "between"})
	check_eq(shot.get("ok"), true, "the stand-in plays meanwhile: " + str(shot.get("error")))
	check_eq(_play(m, shot, "between").get("ok"), true, "and its stroke is judged")
	check(m.opponent_thinking(), "judging did not stop the thinking")
	var answer := await _answer(m)
	check_eq(answer.get("ok"), false, "the answer is refused")
	check(str(answer.get("error")).contains("frame changed"), "because the frame changed: " + str(answer.get("error")))
	check(m.last_error().contains("frame changed"), "last_error says so")
	check(not m.opponent_thinking(), "the answer is spent")
	var refused: Array[Dictionary] = []
	for line in _lines():
		if line.get("event") == "opponent.decision" and line.get("level") == "warn":
			refused.append(line["data"])
	check_eq(refused.size(), 1, "opponent.decision at warn")
	if refused.size() == 1:
		check_eq(refused[0].get("level"), "beginner", "for the level")
		check(str(refused[0].get("error")).contains("frame changed"), "with the error")
	check_eq(_logged_what("OfsMatch.opponent_take").size(), 1, "and rules.invalid")
	_check_schema()
	if not m.frame()["over"] and m.frame()["pending"] == "none":
		check(m.opponent_think("beginner", 10, {"shot_id": "fresh"}, {}), "a new think works: " + m.last_error())
		check_eq((await _answer(m)).get("ok"), true, "and is taken")


func test_the_same_seed_and_frame_give_the_same_strike() -> void:
	for level: String in LEVELS:
		var m := _started_with(level)
		var answers: Array[Dictionary] = []
		for i in 2:
			check(m.opponent_think(level, 42, {"shot_id": "same-%d" % i}, {"max_ms": 0}), "thinks: " + m.last_error())
			answers.append(await _answer(m))
		check_eq(answers[0].get("ok"), true, level + ": answers")
		check_eq(answers[1].get("strike"), answers[0].get("strike"), level + ": the same strike")
		check_eq(answers[1].get("meant"), answers[0].get("meant"), level + ": meant the same")
		check_eq(answers[1].get("cue"), answers[0].get("cue"), level + ": from the same place")


func test_the_147_says_when_the_maximum_is_on() -> void:
	var m := _opponent()
	var config := _config(1, 0, 51)
	config["level"] = "maximum"
	config["exhibition"] = 0
	config["person"] = 0
	check_eq(m.start(config, _match_id()).get("ok"), true, "an exhibition starts: " + m.last_error())
	_mark_log()
	for i in 2:
		check(m.opponent_think("maximum", 60 + i, {"shot_id": "on-%d" % i}, {}), "thinks: " + m.last_error())
		check_eq((await _answer(m)).get("ok"), true, "answers")
	var lines := _logged("opponent.maximum")
	check_eq(lines.size(), 1, "opponent.maximum once in the frame")
	if lines.size() == 1:
		check_eq(lines[0]["state"], "on", "on")
		check_eq(int(lines[0]["player"]), 1, "for the 147 at the table")
		check_eq(int(lines[0]["break_points"]), 0, "no break yet")
		check_eq(int(lines[0]["reds"]), 15, "fifteen reds")
		check_eq(lines[0]["shot_id"], "on-0", "the stroke it came on at")

	var beginner := _opponent()
	config["level"] = "beginner"
	check_eq(beginner.start(config, _match_id()).get("ok"), true, "Beginner on the same table")
	_mark_log()
	check(beginner.opponent_think("beginner", 60, {"shot_id": "b"}, {}), "thinks: " + beginner.last_error())
	check_eq((await _answer(beginner)).get("ok"), true, "answers")
	check_eq(_logged("opponent.maximum").size(), 0, "a level that doesn't play for one says nothing")
	_check_schema()


func test_opponent_choice_after_a_foul() -> void:
	var m := _started_with("beginner", 0)
	check_eq(m.place_cue(IN_THE_D.x, IN_THE_D.y).get("ok"), true, "the cue ball is placed")
	var record := Physics.sim.simulate(m.balls(), m.cue_ball(), _strike(PI, 0.2), 7, _context("foul"))
	check(record != null, "a stroke that hits nothing")
	check_eq(m.judge(record, {"nominated": ""}, {"shot_id": "foul"}).get("ok"), true, "is judged")
	check_eq(m.frame().get("pending"), "after_foul", "and leaves a choice")
	_mark_log()
	for level: String in LEVELS:
		var kind := m.opponent_choice(level, 1, 77)
		check(AFTER_FOUL.has(kind), "%s: one of the options: \"%s\"" % [level, kind])
		check_eq(m.opponent_choice(level, 1, 77), kind, level + ": the same seed chooses the same")
	var lines := _logged("opponent.decision")
	check_eq(lines.size(), 4, "opponent.decision each time")
	for line in lines:
		check_eq(line["kind"], "choice", "a choice")
		check(line["level"] in LEVELS, "by a level")
		check_eq(int(line["player"]), 1, "by the decider")
		check_eq(line["options"], AFTER_FOUL, "among what was offered")
		check_eq(line["seed"], "77", "with the seed")
		check(not line.has("shot_id"), "no stroke")
	check_eq(m.frame()["pending"], "after_foul", "choosing applies nothing")
	check_eq(m.opponent_choice("nobody", 1, 77), "", "an unknown level chooses nothing")
	check(m.last_error().contains("nobody"), "last_error says so: " + m.last_error())
	check_eq(m.opponent_choice("", 1, 77), "", "nor no level")
	check_eq(m.opponent_choice("beginner", 0, 77), "", "nor for the player with nothing to choose")
	check(not m.opponent_think("beginner", 1, {"shot_id": "pending"}, {}), "no stroke while a choice is pending")
	check(m.last_error().contains("decision to make"), "last_error says so: " + m.last_error())
	_check_schema()


func test_opponent_think_refusals() -> void:
	var unready := OfsMatch.new()
	check(not unready.opponent_think("beginner", 1, {}, {}), "refused before configure")
	check(unready.last_error().contains("not configured"), "last_error says so: " + unready.last_error())
	var m := _opponent()
	check(not m.opponent_think("beginner", 1, {}, {}), "refused before a match")
	check(m.last_error().contains("no match"), "last_error says so: " + m.last_error())
	var config := _config(1, 0, 61)
	check_eq(m.start(config, _match_id()).get("ok"), true, "starts")
	_mark_log()
	var cases := {
		"an unknown level": ["nobody", {}, "nobody"],
		"no level": ["", {}, "not configured"],
		"a negative max_sims": ["beginner", {"max_sims": -1}, "max_sims"],
		"max_steps not a number": ["beginner", {"max_steps": "many"}, "max_steps"],
		"max_ms not a number": ["beginner", {"max_ms": "soon"}, "max_ms"],
		"max_ms NaN": ["beginner", {"max_ms": NAN}, "max_ms"],
	}
	for name: String in cases:
		var c: Array = cases[name]
		check(not m.opponent_think(c[0], 1, {"shot_id": name}, c[1]), name + " is refused")
		check(m.last_error().contains(c[2]), "%s: last_error names it: %s" % [name, m.last_error()])
		check(not m.opponent_thinking(), name + ": nothing thinking")
	check_eq(_logged_what("OfsMatch.opponent_think").size(), cases.size(), "each logged as rules.invalid")
	check_eq(_logged("opponent.think").size(), 0, "and none thought")
	_check_schema()
	for budget: Dictionary in [{"max_sims": 5, "max_steps": 7, "max_ms": 250.5}, {"max_ms": -3}]:
		_mark_log()
		check(m.opponent_think("beginner", 1, {"shot_id": "budget"}, budget), "takes %s: %s" % [budget, m.last_error()])
		var thought := _logged("opponent.think")
		if thought.size() == 1:
			var want := {"max_sims": float(budget.get("max_sims", 0)), "max_steps": float(budget.get("max_steps", 0)),
					"max_ms": maxf(float(budget["max_ms"]), 0.0)}
			check_eq(thought[0]["budget"], want, "the budget as it is used")
		check_eq((await _answer(m)).get("ok"), true, "and answers")


func test_freeing_the_engine_while_it_thinks() -> void:
	_mark_log()
	var m := _started_with("maximum")
	check(m.opponent_think("maximum", 1, {"shot_id": "freed"}, {"max_ms": 0}), "thinks: " + m.last_error())
	m = null
	for i in 5:
		await tree.process_frame
	var whys: Array = _logged("opponent.cancelled").map(func(l: Dictionary) -> String: return l["why"])
	check_eq(whys, ["the match engine was freed"], "the thinking is stopped with it")
	check_eq(_logged("app.error").size(), 0, "nothing threw")


## A configured engine without the opponent's files.
func _engine() -> OfsMatch:
	var m := OfsMatch.new()
	check(m.configure(Physics.sim, _text(REFEREE_PATH), _text(STANDIN_PATH)), "configure: " + m.last_error())
	return m


## A configured engine with both levels and the exhibition tables.
func _opponent() -> OfsMatch:
	var m := _engine()
	check(m.configure_opponent(_level_files(), _text(EXHIBITIONS_PATH)), "configure_opponent: " + m.last_error())
	return m


## A best of 3 against `level` with `breaker` to break.
func _started_with(level: String, breaker: int = 1) -> OfsMatch:
	var m := _opponent()
	var config := _config(3, breaker, 71)
	config["level"] = level
	var started := m.start(config, _match_id())
	check_eq(started.get("ok"), true, "start: " + str(started.get("error")))
	return m


func _level_files() -> Dictionary:
	return {"beginner": _text(BEGINNER_PATH), "maximum": _text(MAXIMUM_PATH)}


func _layout_names() -> Array:
	return ((_fixture(EXHIBITIONS_PATH) as Dictionary)["layouts"] as Array).map(
			func(l: Dictionary) -> String: return l["name"])


## Polls opponent_take each frame until the answer is there.
func _answer(m: OfsMatch) -> Dictionary:
	var deadline := Time.get_ticks_msec() + ANSWER_WAIT_MS
	while Time.get_ticks_msec() < deadline:
		var got := m.opponent_take()
		if not got.is_empty():
			return got
		await tree.process_frame
	check(false, "no answer within %d ms" % ANSWER_WAIT_MS)
	return {}


## Plays an opponent's or the stand-in's stroke as the game does: the cue ball placed when it says
## so, the strike simulated and judged. judge's answer.
func _play(m: OfsMatch, shot: Dictionary, shot_id: String) -> Dictionary:
	if bool(shot.get("places_cue", false)):
		var cue: Array = shot["cue"]
		check_eq(m.place_cue(cue[0], cue[1]).get("ok"), true, shot_id + ": the cue ball is placed: " + m.last_error())
	var record := Physics.sim.simulate(m.balls(), m.cue_ball(), shot.get("strike", {}), 4000, _context(shot_id))
	check(record != null, shot_id + ": simulated: " + Physics.sim.last_error())
	if record == null:
		return {}
	return m.judge(record, {"nominated": shot.get("nominated", "")}, {"shot_id": shot_id})


func _match_id() -> String:
	_matches += 1
	return _last_id()


func _last_id() -> String:
	return "opponent-binding-m%d" % _matches


func _config(best_of: int, first_breaker: int, seed: int) -> Dictionary:
	return {"best_of": best_of, "first_breaker": first_breaker, "seed": seed, "concede_any_time": [true, false]}


func _strike(aim: float, speed: float) -> Dictionary:
	return {"aim_rad": aim, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, "speed_m_s": speed}


func _context(shot_id: String) -> Dictionary:
	return {"shot_id": shot_id, "mode": "match", "strike_mode": "opponent"}


func _text(path: String) -> String:
	return FileAccess.get_file_as_string(path)


func _fixture(path: String) -> Variant:
	return JSON.parse_string(_text(path))


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


func _logged(event: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for line in _lines():
		if line.get("event") == event:
			found.append(line.get("data", {}))
	return found


func _logged_what(what: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for data in _logged("rules.invalid"):
		if data.get("what") == what:
			found.append(data)
	return found


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
