class_name MatchController
extends Node
## One match on the table (spec 9, 11.1): the engine, whose turn it is, what the person may do
## next and the opponent's strokes and choices. The table scene plays the strokes; this node only
## says what happens between them. Every rule lives in the engine (OfsMatch): nothing here knows
## a foul from a pot.
##
## Dictionaries are the JSON forms of core/include/ofs/rules/serialize.hpp: `frame` is the frame
## view (frame_view_json), `match` the match, a call {kind, value, detail}.
##
## A listener may call back in from a signal (strike on opponent_ready, rack the next frame on
## frame_ended): whatever was still to be announced for the state it left is then dropped.

## The balls on the table changed other than by a stroke's own motion. reason: &"frame" (a frame
## was racked), &"shot" (a stroke was judged: colours spotted, the cue ball in hand), &"replaced"
## (balls put back after a miss), &"spotted" (any other choice that moved them: a re-rack accepted
## while the black is re-spotted, S3.17(d), which the engine reports with no event), &"cue" (the
## in-hand cue ball was placed), &"ended" (the match was left).
signal table_changed(balls: Array[Dictionary], cue_ball: int, reason: StringName)
## The frame view after anything changed it; the scoreboard and the controls follow this.
signal frame_changed(frame: Dictionary)
## What the referee announces, in order, after a stroke or a choice (shot_id "" for a choice).
signal calls_made(calls: Array, shot_id: String)
## `player` may play a stroke now. `person` is false for the opponent, whose stroke arrives by
## opponent_ready, and for the person while autoplay plays for them.
signal turn_started(player: int, person: bool)
## The person must choose one of `options` (decision names) before play goes on: the engine's
## options without PAUSE_DECISIONS, in its order.
signal choice_needed(options: Array[String])
## The opponent's stroke, as OfsMatch.standin_shot gives it (from a level, opponent_take's, with
## its "level", "meant" and "think"): the table scene places the cue ball when places_cue, shows
## the cue on the line and plays `strike` exactly, nominating `nominated`. It also carries
## "player" and "shot_id", the id the decision was logged under: strike with that id. An opponent
## that claims or concedes in place of a stroke never comes here: its decision is applied like a
## choice.
signal opponent_ready(shot: Dictionary)
## The opponent chose `kind` (a decision name); it has been applied.
signal opponent_chose(kind: String)
## The engine refused MAX_REFUSALS times in a row, or the opponent's strokes could not be played
## that often: the opponent is no longer asked and the match can't go on. `why` is the last refusal.
signal stalled(why: String)
## A frame ended: its summary (serialize.hpp) and the match. The match went on unless match_ended
## follows.
signal frame_ended(summary: Dictionary, match_state: Dictionary)
signal match_ended(match_state: Dictionary)
## The opponent's level started thinking about its stroke (true), or stopped.
signal opponent_thinking(on: bool)
## Watch a 147 is over: {break_points, maximum (147 made), why: "maximum" | "fouled" | "missed" |
## "left"}. The match is then left (table_changed(&"ended")) unless a listener moved it on.
signal exhibition_ended(result: Dictionary)

const PERSON := 0
const OPPONENT := 1
## Seconds before the opponent acts, at pace 1 (spec 10.5: at least about 0.6 s).
const THINK_SECONDS := 0.9
## A level's stroke: its cue then stands, feathers and strikes (OpponentShow), which makes up the
## rest of the natural pause.
const LEVEL_THINK_SECONDS := 0.3
const CHOICE_SECONDS := 1.2
const REFEREE_PATH := "res://data/referee_params.json"
const STANDIN_PATH := "res://data/standin_params.json"
const LEVEL_IDS: Array[String] = ["beginner", "maximum"]
const LEVEL_PATH := "res://data/opponent_%s.json"
const EXHIBITIONS_PATH := "res://data/exhibitions.json"
## level_name() with no level: the stand-in.
const STANDIN_NAME := "Computer"
## Refusals in a row (an opponent's stroke or choice, or the judging of a stroke) after which the
## opponent is no longer asked: at pace 0 a broken engine would be asked, and logged, every frame.
const MAX_REFUSALS := 5
const BREAKERS := {"random": -1, "you": PERSON, "computer": OPPONENT}
## Offered with a pending choice too, but taken from the pause menu, never the choice strip.
const PAUSE_DECISIONS: Array[String] = ["concede_frame", "concede_match", "claim_frame"]

## Matches started this session, for the match id.
static var _matches := 0

## OfsMatch, or in tests an object with the same methods (tests/support/fake_match_engine.gd).
var engine: Object
## The frame's balls as the table scene's ball list, and the cue ball's place in it.
var balls: Array[Dictionary] = []
var cue_ball := 0
## The stand-in also plays the person's strokes and choices (the harness's whole-match run).
## Changed while the person is waited for, it hands the turn over at once.
var autoplay := false:
	set(value):
		if autoplay == value:
			return
		autoplay = value
		if _active and not _playing and _turn_player == PERSON and _waiting in ["shot", "choice", "opponent"]:
			_moves += 1
			_advance()
## Scales the opponent's waits; 0 acts on the next frame.
var pace := 1.0

var _driver := OpponentDriver.new()
## The engine ensure_engine made, as opposed to one a test set.
var _made: Object
var _active := false
var _match_id := ""
var _frame_number := 0
var _frame := {}
var _version := 0
## As harness_state's "waiting".
var _waiting := ""
## Who is waited for.
var _turn_player := PERSON
## Between shot_started and shot_finished.
var _playing := false
var _shot_id := ""
var _placing := false
var _nominated := ""
var _refusals := 0
## Counts every change of what play waits for: an announcement stops when a listener made one.
var _moves := 0
var _nearest_failed := false
## The engine answers' keys found missing, each logged once.
var _missing := {}
var _last_refusal := ""
## The opponent's level id for the match; "" is the stand-in.
var _level := ""
var _level_name := STANDIN_NAME
## The exhibition's table index while Watch a 147 is on; -1 otherwise.
var _exhibition := -1
## The last judged stroke had a penalty: how an exhibition's break ended.
var _last_fouled := false


func _init() -> void:
	_driver.name = "OpponentDriver"
	add_child(_driver)
	_driver.shot_chosen.connect(_on_shot_chosen)
	_driver.choice_made.connect(_on_choice_made)
	_driver.thinking_changed.connect(opponent_thinking.emit)


## Creates and configures the engine from Physics and the embedded parameter files unless a test
## has set one. False, with app.error logged, when it can't be configured.
func ensure_engine() -> bool:
	if engine != null:
		return true
	return _configure()


## True from start() until the match ends or is abandoned.
func is_active() -> bool:
	return _active


## Counts up whenever `balls` changes, as FreeTable.version does: the guides' cache key.
func version() -> int:
	return _version


## "<session>-m<n>", on every log line of the match; "" when none has been started or it was left.
func match_id() -> String:
	return _match_id


## The number of the frame in play, from 1; 0 with no match.
func frame_number() -> int:
	return _frame_number


## What play waits for, as harness_state's "waiting".
func waiting() -> String:
	return _waiting


## config: {best_of: int, first_breaker: "random" | "you" | "computer", seed: int (optional; a
## fresh one when absent), level: a level id of levels() ("" or absent: the stand-in),
## exhibition: an index into the engine's exhibitions() (Watch a 147: best of 1 from that table,
## the opponent at it, the person only watching; the 147's level when none is given)}. Starts
## frame 1 and emits table_changed(&"frame"), frame_changed and the first turn. False when the
## config or the engine refuses. A match still on is left first.
func start(config: Dictionary) -> bool:
	var first: Variant = config.get("first_breaker", "random")
	if not BREAKERS.has(first):
		_error("start: first_breaker %s is not \"random\", \"you\" or \"computer\"" % JSON.stringify(first))
		return false
	var exhibition := -1
	if config.has("exhibition"):
		exhibition = _exhibition_index(config["exhibition"])
		if exhibition < 0:
			return false
	var known: Variant = _level_for(config.get("level", ""), exhibition >= 0)
	if known == null:
		return false
	var level := str(known)
	var engine_config := {
		"best_of": int(config.get("best_of", 1)),
		"first_breaker": int(BREAKERS[first]),
		"seed": int(config["seed"]) if config.has("seed") else Physics.new_seed(),
		"concede_any_time": [true, false],
		"person": PERSON,
	}
	if not level.is_empty():
		engine_config["level"] = level
	if exhibition >= 0:
		engine_config["exhibition"] = exhibition
		engine_config["best_of"] = 1
		engine_config["first_breaker"] = OPPONENT
	var left := not _match_id.is_empty()
	if _exhibition >= 0 and left:
		_log_exhibition_end(_exhibition_result("left"))
	_leave()
	_level = level
	_level_name = _name_of(level)
	_exhibition = exhibition
	if _start(engine_config):
		return true
	_level = ""
	_level_name = STANDIN_NAME
	_exhibition = -1
	# The match that was on is gone all the same.
	if left:
		table_changed.emit(balls, cue_ball, &"ended")
	return false


## Leaves the match with no winner (Quit, spec 11.2); logs through the engine. Also clears a
## finished match away. Emits table_changed(&"ended"); `balls` stay as they were left. An
## exhibition ends first, as "left".
func abandon() -> void:
	if _exhibition >= 0 and not _match_id.is_empty():
		_end_exhibition("left")
		return
	var was_on := not _match_id.is_empty()
	_leave()
	if was_on:
		table_changed.emit(balls, cue_ball, &"ended")


## The frame view; the last frame's while its summary or the match's shows. Not to be changed.
func frame() -> Dictionary:
	return _frame


func match_state() -> Dictionary:
	if engine == null or _match_id.is_empty():
		return {}
	return _dict(engine.match_state())


## The opponent's levels, [{id, name, tier, maximum}] as the engine lists them; empty when its
## levels could not be configured.
func levels() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	if not ensure_engine():
		return out
	for entry: Variant in _list(engine.levels()):
		if entry is Dictionary:
			out.append(entry)
	return out


## The match's level id; "" for the stand-in.
func level() -> String:
	return _level


## The opponent's name as the game shows it: the level's, STANDIN_NAME for the stand-in.
func level_name() -> String:
	return _level_name


## Watch a 147 is on: the person only watches.
func is_exhibition() -> bool:
	return _exhibition >= 0


## The person may play a stroke now: a match is on, nothing is pending, no stroke is being
## judged, the striker is the person and autoplay is off.
func person_to_play() -> bool:
	return _active and not _playing and _waiting == "shot"


## The person's cue ball is in hand and has not been confirmed in place (spec 8.6).
func placing() -> bool:
	return _placing


## The legal place nearest `xy` for the in-hand cue ball.
func nearest_cue_placement(xy: Vector2) -> Vector2:
	if not _active:
		return xy
	var place := _dict(engine.nearest_cue_placement(xy.x, xy.y))
	if not (place.has("x") and place.has("y")):
		# Asked on every drag: once a match is enough.
		if not _nearest_failed:
			_nearest_failed = true
			_error("engine.nearest_cue_placement(%s, %s) gave %s; the place is used as asked" % [xy.x, xy.y,
					JSON.stringify(place)])
		return xy
	return Vector2(float(place["x"]), float(place["y"]))


## Moves the in-hand cue ball to `xy`; false, the ball where it was, when the engine refuses.
## Emits table_changed(&"cue"). For the opponent's stroke too (its `cue`).
func place_cue(xy: Vector2) -> bool:
	if not _active or _playing:
		_error("place_cue(%s, %s) with %s" % [xy.x, xy.y, "a stroke in play" if _playing else "no match on"], false)
		return false
	var answer := _dict(engine.place_cue(xy.x, xy.y))
	if not bool(answer.get("ok", false)):
		_refused("place_cue", [xy.x, xy.y], answer, false)
		return false
	_frame = _dict(engine.frame())
	_sync_balls()
	table_changed.emit(balls, cue_ball, &"cue")
	return true


## The Confirm button: the cue ball stays where it is and the person aims (spec 8.6).
func confirm_cue() -> void:
	_placing = false


## Back to placing, while the cue ball is still in hand.
func move_cue_again() -> void:
	if person_to_play() and bool(_frame.get("cue_in_hand", false)):
		_placing = true


## The colours the striker may declare now (the frame view's "nominations"); empty when no
## nomination is open: nothing is declared between strokes or once the stroke is struck.
func nominations() -> Array[String]:
	var out: Array[String] = []
	if _stroke_awaited():
		for ball: Variant in _list(_dict(_frame.get("view")).get("nominations")):
			out.append(str(ball))
	return out


## The declared colour, "" for none. It lasts one stroke: shot_finished clears it.
func nominated() -> String:
	return _nominated


## Declares `ball`; how is "aim" (the aim line chose it), "tap", "harness" or "opponent". False
## when it is not one of nominations(). Logs match.nominated when the choice changes.
func nominate(ball: String, how: String) -> bool:
	if not nominations().has(ball):
		return false
	_declare(ball, how)
	return true


## Withdraws the declaration, as when the aim leaves a free ball for a ball on: the engine then
## reads the first ball hit (docs/rules/README.md). False when no nomination is open.
func nominate_none(how: String) -> bool:
	if nominations().is_empty():
		return false
	_declare("", how)
	return true


## Why the person's stroke may not be played yet: "" when it may, else "placing" (the cue ball is
## not confirmed), "nominate" (a colour of choice is on and none is declared; a free ball need not
## be), "choice" (the person has a decision to make), "opponent" (the opponent, or autoplay, is to
## act), "playing" (a stroke is in play) or "inactive" (no frame is in play: no match, or a summary
## is showing).
func strike_blocked() -> String:
	if not _active:
		return "inactive"
	if _playing:
		return "playing"
	match _waiting:
		"shot":
			if _placing:
				return "placing"
			if _nominated.is_empty() and str(_dict(_frame.get("on")).get("kind", "")) == "any_colour":
				return "nominate"
			return ""
		"choice", "opponent", "stalled":
			return _waiting
	return "inactive"


## The table scene struck a stroke with this id from `balls`.
func shot_started(shot_id: String) -> void:
	if not _stroke_awaited():
		_error("shot_started(\"%s\") when no stroke is awaited (%s); it will not be judged" % [shot_id,
				"a stroke is in play" if _playing else "waiting \"%s\"" % _waiting], false)
		return
	_driver.cancel()
	_moves += 1
	_playing = true
	_shot_id = shot_id
	_placing = false
	_waiting = ""


## The stroke has stopped: the engine judges it. Updates balls, then emits calls_made,
## table_changed(&"shot"; &"frame" when the stroke had the frame racked again), frame_changed and
## whichever of turn_started, choice_needed, opponent_ready, frame_ended and match_ended follows.
## Returns the judgement ({} when refused: the balls go back and the same player plays).
func shot_finished(record: OfsShotRecord) -> Dictionary:
	if not _playing:
		if not _stroke_awaited():
			_error("shot_finished when no stroke is in play or awaited (waiting \"%s\"); not judged" % _waiting,
					false)
			return {}
		_error("shot_finished without shot_started; the stroke is judged with no shot id", false)
		_driver.cancel()
	var shot_id := _shot_id
	_playing = false
	_shot_id = ""
	_moves += 1
	var input := {"nominated": _nominated}
	var context := {"shot_id": shot_id}
	_nominated = ""
	var answer := _dict(engine.judge(record, input, context))
	var judged := bool(answer.get("ok", false))
	var judgement := {}
	var event := "none"
	var tells: Array[Callable] = []
	if judged:
		_refusals = 0
		judgement = _expect(answer, "judgement", {}, "judge")
		event = _expect(answer, "event", "none", "judge")
		_last_fouled = int(judgement.get("penalty", 0)) > 0
		_take_frame(answer)
		var calls := _list(judgement.get("calls"))
		if not calls.is_empty():
			tells.append(calls_made.emit.bind(calls, shot_id))
	else:
		_refusals += 1
		_refused("judge", [input, context], answer)
	_sync_balls()
	tells.append(table_changed.emit.bind(balls, cue_ball, &"frame" if event == "reracked" else &"shot"))
	tells.append(frame_changed.emit.bind(_frame))
	if _announce(tells):
		if judged:
			_settle(event)
		else:
			_advance()
	return judgement


## The person's choice among the options offered. False when it is not one of them, and for
## PAUSE_DECISIONS, which go through concede_frame, concede_match and claim_frame.
func choose(kind: String) -> bool:
	var offered: Array[String] = []
	if _active and not _playing:
		offered = _choices(PERSON)
	if not offered.has(kind):
		_error("choose(\"%s\") is not one of the person's options %s" % [kind, JSON.stringify(offered)], false)
		return false
	return _decide(kind, PERSON)


## Racks the next frame after frame_ended; false when there is none to rack.
func next_frame() -> bool:
	if not _active or _waiting != "frame_summary":
		_error("next_frame with no finished frame to follow (waiting \"%s\")" % _waiting, false)
		return false
	var answer := _dict(engine.next_frame())
	if not bool(answer.get("ok", false)):
		_refused("next_frame", [], answer)
		return false
	_moves += 1
	_take_frame(answer)
	_frame_number = int(_expect(_dict(engine.match_state()), "frame_number", _frame_number + 1, "match_state"))
	_sync_balls()
	if _announce([table_changed.emit.bind(balls, cue_ball, &"frame"), frame_changed.emit.bind(_frame)]):
		_advance()
	return true


## Pause menu (spec 11.1). False, with match.refused logged and the engine not asked, when it is
## not open now (open_to_person); false too when the engine refuses it all the same.
func concede_frame() -> bool:
	return _concede("concede_frame")


func concede_match() -> bool:
	return _concede("concede_match")


## The person claims the frame (S2.1(b): only the black left and more than seven ahead), as
## concede_frame.
func claim_frame() -> bool:
	return _concede("claim_frame")


## Whether the person may take `kind` (concede_frame, concede_match, claim_frame) now: the frame
## view offers it to them and no stroke is in play.
func open_to_person(kind: String) -> bool:
	return _closed(kind).is_empty()


## The harness's part of the state dump: {active, match_id, frame, match, nominated, placing,
## blocked, autoplay, waiting: "shot" | "choice" | "opponent" | "frame_summary" | "match_summary"
## | "", level, exhibition, thinking}. "opponent" is also the stand-in acting for the person under
## autoplay.
func harness_state() -> Dictionary:
	return {"active": _active, "match_id": _match_id, "frame": _frame, "match": match_state(),
			"nominated": _nominated, "placing": _placing, "blocked": strike_blocked(), "autoplay": autoplay,
			"waiting": _waiting, "level": _level, "exhibition": is_exhibition(), "thinking": _driver.is_thinking()}


func _start(engine_config: Dictionary) -> bool:
	# Its own engine takes the physics in use now: the parameters can change between matches.
	if (engine == null or engine == _made) and not _configure():
		return false
	_matches += 1
	_match_id = "%s-m%d" % [Log.session(), _matches]
	var answer := _dict(engine.start(engine_config, _match_id))
	if not bool(answer.get("ok", false)):
		_refused("start", [engine_config, _match_id], answer)
		_match_id = ""
		return false
	_moves += 1
	_active = true
	_refusals = 0
	_nearest_failed = false
	_last_fouled = false
	var state := _dict(engine.match_state())
	_frame_number = int(_expect(state, "frame_number", 1, "match_state"))
	_frame = _dict(engine.frame())
	_sync_balls()
	if _announce([table_changed.emit.bind(balls, cue_ball, &"frame"), frame_changed.emit.bind(_frame)]):
		_advance()
	return true


func _new_engine() -> Object:
	return OfsMatch.new()


func _configure() -> bool:
	var problems: PackedStringArray = []
	var texts: Array[String] = []
	for path: String in [REFEREE_PATH, STANDIN_PATH]:
		var text := FileAccess.get_file_as_string(path)
		if text.is_empty():
			problems.append("%s is missing or empty (%s); run scripts/sync_data.py" % [path,
					error_string(FileAccess.get_open_error())])
		texts.append(text)
	var made: Object = _made if _made != null else _new_engine()
	if problems.is_empty() and not bool(made.configure(Physics.sim, texts[0], texts[1])):
		problems.append("configure refused the physics (%s), %s or %s: %s" % [
				"parameters " + Physics.sim.params_hash() if Physics.is_ready() else "not configured",
				REFEREE_PATH, STANDIN_PATH, str(made.last_error())])
	if not problems.is_empty():
		_error("no match engine: " + "; ".join(problems))
		return false
	_made = made
	engine = made
	_configure_opponent(made)
	return true


## The levels and the exhibition tables. Refused, the matches are the stand-in's: levels() is empty.
func _configure_opponent(made: Object) -> void:
	var problems: PackedStringArray = []
	var texts := {}
	for id in LEVEL_IDS:
		var path := LEVEL_PATH % id
		texts[id] = FileAccess.get_file_as_string(path)
		if String(texts[id]).is_empty():
			problems.append("%s is missing or empty (%s)" % [path, error_string(FileAccess.get_open_error())])
	var exhibitions := FileAccess.get_file_as_string(EXHIBITIONS_PATH)
	if exhibitions.is_empty():
		problems.append("%s is missing or empty (%s)" % [EXHIBITIONS_PATH, error_string(FileAccess.get_open_error())])
	if problems.is_empty() and not bool(made.configure_opponent(texts, exhibitions)):
		problems.append("configure_opponent refused %s or %s: %s" % [", ".join(texts.keys().map(
				func(id: String) -> String: return LEVEL_PATH % id)), EXHIBITIONS_PATH, str(made.last_error())])
	if not problems.is_empty():
		_error("no opponent levels, the stand-in plays: %s; run scripts/sync_data.py if a file is missing"
				% "; ".join(problems))


## Forgets the match without a word: the engine's is abandoned when it was still on.
func _leave() -> void:
	_driver.cancel()
	if _active and engine != null:
		engine.abandon()
	_moves += 1
	_active = false
	_match_id = ""
	_frame_number = 0
	_frame = {}
	_waiting = ""
	_playing = false
	_shot_id = ""
	_placing = false
	_nominated = ""
	_level = ""
	_level_name = STANDIN_NAME
	_exhibition = -1


## A level id that may be played: "" for the stand-in, the 147's for an exhibition given none;
## null, with app.error logged, when it can't be.
func _level_for(given: Variant, exhibition: bool) -> Variant:
	if not (given is String or given is StringName):
		_error("start: level %s is not a level id" % JSON.stringify(given))
		return null
	var id := str(given)
	if id.is_empty() and not exhibition:
		return id
	var known := levels()
	var ids: Array[String] = []
	for entry in known:
		ids.append(str(entry.get("id", "")))
	if id.is_empty() and exhibition:
		for entry in known:
			if bool(entry.get("maximum", false)):
				return str(entry.get("id", ""))
		_error("start: an exhibition needs the 147's level and none is listed (levels %s)" % JSON.stringify(ids))
		return null
	if id.is_empty() or ids.has(id):
		return id
	if known.is_empty() and not exhibition:
		# configure_opponent was refused and logged: the match goes on with the stand-in.
		_error("start: level \"%s\" asked for with no levels configured; the stand-in plays" % id)
		return ""
	_error("start: level \"%s\" is not one of %s" % [id, JSON.stringify(ids)])
	return null


## An exhibition table's index in the engine's list; -1, with app.error logged, when there is none.
func _exhibition_index(given: Variant) -> int:
	var count := -1
	if ensure_engine():
		count = (engine.exhibitions() as PackedStringArray).size()
	var index := int(given) if (given is int or (given is float and is_finite(given) and given == floorf(given))) else -1
	if index < 0 or index >= count:
		_error("start: exhibition %s is not an index into the %d exhibition tables" % [JSON.stringify(given),
				maxi(count, 0)])
		return -1
	return index


func _name_of(level_id: String) -> String:
	if level_id.is_empty():
		return STANDIN_NAME
	for entry in levels():
		if str(entry.get("id", "")) == level_id:
			return str(entry.get("name", level_id))
	return level_id


func _concede(kind: String) -> bool:
	var why := _closed(kind)
	if not why.is_empty():
		Log.info("match", "match.refused", {"kind": kind, "why": why, "match_id": _match_id, "frame": _frame_number})
		return false
	return _decide(kind, PERSON, false)


## Why the person may not take `kind` now; "" when they may.
func _closed(kind: String) -> String:
	if not _active:
		return "no match is on"
	if _playing:
		return "a stroke is in play"
	if _waiting == "frame_summary":
		# Between frames a match is left by Quit.
		return "no frame is in play (a frame's summary is showing)"
	var offered := _options(PERSON)
	if not offered.has(kind):
		return "not offered now (pending \"%s\", offered %s)" % [_pending(), JSON.stringify(offered)]
	return ""


## Applies `kind` for `player` and announces what it brought. `severe`: a refusal is an engine
## fault rather than an answer the caller asked for.
func _decide(kind: String, player: int, severe: bool = true) -> bool:
	var answer := _dict(engine.decide(kind, player))
	if not bool(answer.get("ok", false)):
		_refused("decide", [kind, player], answer, severe)
		return false
	_moves += 1
	_refusals = 0
	_take_frame(answer)
	var moved := _sync_balls()
	var event: String = _expect(answer, "event", "none", "decide")
	var tells: Array[Callable] = []
	if player != PERSON:
		tells.append(opponent_chose.emit.bind(kind))
	var calls := _list(answer.get("calls"))
	if not calls.is_empty():
		tells.append(calls_made.emit.bind(calls, ""))
	if event == "reracked":
		tells.append(table_changed.emit.bind(balls, cue_ball, &"frame"))
	elif kind == "replace":
		tells.append(table_changed.emit.bind(balls, cue_ball, &"replaced"))
	elif moved:
		# No event says so: a re-rack accepted while the black is re-spotted (S3.17(d)).
		tells.append(table_changed.emit.bind(balls, cue_ball, &"spotted"))
	tells.append(frame_changed.emit.bind(_frame))
	if _announce(tells):
		_settle(event)
	return true


## After a stroke or a choice changed the frame: `event` is the engine's match event.
func _settle(event: String) -> void:
	var frame_over := event == "frame_ended" or bool(_frame.get("over", false))
	if event != "match_ended" and not frame_over:
		_advance()
		return
	_driver.cancel()
	_placing = false
	_nominated = ""
	var state := _dict(engine.match_state())
	var history := _list(state.get("history"))
	var match_over := event == "match_ended" or bool(state.get("over", false))
	if _exhibition >= 0:
		# A finished match is not abandoned in the engine.
		if match_over:
			_active = false
		_end_exhibition(_exhibition_why())
		return
	var tells: Array[Callable] = []
	if history.is_empty():
		_error("frame %d ended (event \"%s\") with no summary in the match's history" % [_frame_number, event])
	tells.append(frame_ended.emit.bind(_dict(history.back()) if not history.is_empty() else {}, state))
	if match_over:
		_active = false
		tells.append(match_ended.emit.bind(state))
	_wait("match_summary" if match_over else "frame_summary", PERSON, true)
	_announce(tells)


## Says who is to act in the frame as it stands, and sets the opponent going when it is its part.
## In an exhibition the person's turn ends it.
func _advance() -> void:
	_driver.cancel()
	_placing = false
	if _pending() != "none":
		var decider := int(_expect(_frame, "decider", 0, "frame"))
		if _exhibition >= 0 and decider == PERSON:
			_end_exhibition(_exhibition_why())
			return
		var offered := _choices(decider)
		if offered.is_empty():
			_error("the frame is pending \"%s\" for player %d, who is offered nothing" % [_pending(), decider])
		if decider == PERSON and not autoplay:
			_wait("choice", decider, true)
			choice_needed.emit(offered)
		else:
			_wait("opponent", decider, false)
			_ask_opponent()
		return
	_nominated = ""
	var striker := int(_expect(_frame, "striker", 0, "frame"))
	if _exhibition >= 0 and striker == PERSON:
		_end_exhibition(_exhibition_why())
		return
	var by_hand := striker == PERSON and not autoplay
	_placing = by_hand and bool(_frame.get("cue_in_hand", false))
	_wait("shot" if by_hand else "opponent", striker, by_hand)
	if not by_hand:
		_ask_opponent()
	turn_started.emit(striker, by_hand)


## The table scene could not play the opponent's stroke `shot_id` (opponent_ready's): malformed,
## or refused by the physics. It counts as a refusal and the opponent is asked again, until
## MAX_REFUSALS stalls the match.
func opponent_failed(shot_id: String, why: String) -> void:
	if not _stroke_awaited() or _waiting != "opponent":
		_error("opponent_failed(\"%s\") when no opponent's stroke is awaited (waiting \"%s\"%s); ignored" % [shot_id,
				_waiting, ", a stroke in play" if _playing else ""], false)
		return
	_refusals += 1
	_last_refusal = "%s's stroke %s could not be played: %s" % [_who(_turn_player), shot_id, why]
	_ask_opponent()


## Asks a stalled opponent once more from a clean count; it stalls again if it still can't play.
func retry_standin() -> void:
	if not _active or _waiting != "stalled":
		return
	_refusals = 0
	_wait("opponent", _turn_player, false)
	_ask_opponent()


## The level that plays for `player`: the match's for the opponent; the person under autoplay is
## the stand-in's.
func _level_of(player: int) -> String:
	return _level if player == OPPONENT else ""


## "the stand-in" or "the level \"<id>\"", for the logs.
func _who(player: int) -> String:
	var level_id := _level_of(player)
	return "the stand-in" if level_id.is_empty() else "the level \"%s\"" % level_id


func _ask_opponent() -> void:
	if _refusals >= MAX_REFUSALS:
		_error("%d refusals in a row, the last: %s; %s is not asked again and the match can't go on"
				% [_refusals, _last_refusal, _who(_turn_player)])
		_wait("stalled", _turn_player, false)
		stalled.emit(_last_refusal)
		return
	if _pending() != "none":
		_driver.request_choice(engine, _turn_player, CHOICE_SECONDS * pace, _level_of(_turn_player))
	else:
		var level_id := _level_of(_turn_player)
		var think := THINK_SECONDS if level_id.is_empty() else LEVEL_THINK_SECONDS
		_driver.request_shot(engine, _turn_player, think * pace, level_id)


func _on_shot_chosen(player: int, shot: Dictionary) -> void:
	if not _stroke_awaited() or _waiting != "opponent" or player != _turn_player:
		_error("%s's stroke %s for player %d came when none is asked for (waiting \"%s\" for player %d%s); dropped"
				% [_who(player), shot.get("shot_id", ""), player, _waiting, _turn_player,
				", a stroke in play" if _playing else ""], false)
		return
	if not bool(shot.get("ok", false)):
		_refusals += 1
		var args: Array = [_driver.last_seed, {"shot_id": shot.get("shot_id", "")}]
		if _driver.last_call != "standin_shot":
			args.push_front(_level_of(player))
		_refused(_driver.last_call, args, shot)
		_ask_opponent()
		return
	var decision: Variant = shot.get("decision")
	if decision is String and not (decision as String).is_empty():
		# It claims or concedes in place of a stroke.
		if not _decide(decision, player):
			_refusals += 1
			_ask_opponent()
		return
	shot["player"] = player
	opponent_ready.emit(shot)


func _on_choice_made(player: int, kind: String) -> void:
	if not _active or _playing or _waiting != "opponent" or _pending() == "none" or player != _turn_player:
		_error("%s's choice \"%s\" for player %d came when none is asked for (waiting \"%s\" for player %d, pending \"%s\"%s); dropped"
				% [_who(player), kind, player, _waiting, _turn_player, _pending(), ", a stroke in play" if _playing else ""],
				false)
		return
	var offered := _options(player)
	var chosen := kind
	var asked := _choice_call(player)
	if not offered.has(kind):
		offered = _choices(player)
		if offered.is_empty():
			_error("engine.%s gave \"%s\" with nothing offered; the match waits" % [asked, kind])
			return
		# The first option offered is the plain one (play, accept, play first): the match goes on.
		chosen = offered[0]
		_error("engine.%s gave \"%s\", not one of %s; taking \"%s\"" % [asked, kind, JSON.stringify(offered), chosen])
	if not _decide(chosen, player):
		_refusals += 1
		_ask_opponent()


## The engine call the last choice for `player` was asked with, as the logs name it.
func _choice_call(player: int) -> String:
	var level_id := _level_of(player)
	if level_id.is_empty():
		return "standin_choice(%d, %d)" % [player, _driver.last_seed]
	return "opponent_choice(\"%s\", %d, %d)" % [level_id, player, _driver.last_seed]


## How the exhibition's break ended, short of being left.
func _exhibition_why() -> String:
	if int(_exhibition_result("").get("break_points", 0)) >= 147:
		return "maximum"
	return "fouled" if _last_fouled else "missed"


## {break_points, maximum, why, strokes}: the break is the opponent's only one in the frame, which
## its highest_break holds once over and break_points while it runs.
func _exhibition_result(why: String) -> Dictionary:
	var stats := _list(_frame.get("stats"))
	var made := 0
	if OPPONENT < stats.size():
		made = int(_dict(stats[OPPONENT]).get("highest_break", 0))
	if int(_frame.get("striker", -1)) == OPPONENT:
		made = maxi(made, int(_frame.get("break_points", 0)))
	return {"break_points": made, "maximum": made >= 147, "why": why, "strokes": int(_frame.get("strokes", 0))}


func _log_exhibition_end(result: Dictionary) -> void:
	Log.info("match", "exhibition.ended", {"break_points": result["break_points"], "maximum": result["maximum"],
			"why": result["why"], "strokes": result["strokes"], "match_id": _match_id})


## Logs exhibition.ended, emits exhibition_ended and leaves the match, unless a listener did.
func _end_exhibition(why: String) -> void:
	_driver.cancel()
	var result := _exhibition_result(why)
	if int(result["strokes"]) == 0 and why != "left":
		_error("the exhibition ended before the opponent struck: player %d to act, pending \"%s\"" % [
				int(_frame.get("striker", -1)), _pending()])
	_log_exhibition_end(result)
	_exhibition = -1
	result.erase("strokes")
	var mine := _moves
	exhibition_ended.emit(result)
	if mine == _moves:
		abandon()


func _declare(ball: String, how: String) -> void:
	if ball == _nominated:
		return
	_nominated = ball
	Log.info("match", "match.nominated", _fields({"ball": ball, "how": how}))


## A stroke may be struck: a frame is in play with nothing pending and no stroke on its way.
func _stroke_awaited() -> bool:
	return _active and not _playing and _waiting in ["shot", "opponent"] and _pending() == "none"


func _pending() -> String:
	return str(_frame.get("pending", "none"))


func _options(player: int) -> Array[String]:
	var out: Array[String] = []
	var all := _list(_dict(_frame.get("view")).get("options"))
	if player >= 0 and player < all.size():
		for kind: Variant in _list(all[player]):
			out.append(str(kind))
	return out


## The options of the pending choice itself: _options without PAUSE_DECISIONS, order kept.
func _choices(player: int) -> Array[String]:
	var out: Array[String] = []
	for kind in _options(player):
		if not PAUSE_DECISIONS.has(kind):
			out.append(kind)
	return out


func _take_frame(answer: Dictionary) -> void:
	var given := _dict(answer.get("frame"))
	_frame = given if not given.is_empty() else _dict(engine.frame())


## Takes the engine's balls; true when they are not the ones held.
func _sync_balls() -> bool:
	var now: Array[Dictionary] = []
	for ball: Variant in _list(engine.balls()):
		now.append(_dict(ball))
	var place := int(engine.cue_ball())
	if now == balls and place == cue_ball:
		return false
	balls = now
	cue_ball = place
	_version += 1
	return true


func _wait(waiting: String, player: int, person: bool) -> void:
	_waiting = waiting
	_turn_player = player
	Log.info("match", "match.turn", _fields({"player": player, "person": person, "waiting": waiting}))


## Emits each in turn; false as soon as a listener has moved the match on, when the rest is stale.
func _announce(emits: Array[Callable]) -> bool:
	var mine := _moves
	for emit: Callable in emits:
		emit.call()
		if mine != _moves:
			return false
	return true


## An engine answer with ok false: what was asked, with what, and why.
func _refused(method: String, args: Array, answer: Dictionary, severe: bool = true) -> void:
	var given: PackedStringArray = []
	for arg: Variant in args:
		given.append(JSON.stringify(arg))
	_last_refusal = "engine.%s(%s) refused: %s (last_error: %s)" % [method, ", ".join(given),
			str(answer.get("error", "no error given")), str(engine.last_error())]
	_error(_last_refusal, severe)


## answer[key] when it is there as the engine's JSON forms promise; else `fallback`, with
## app.error logged the first time that key is missing from what `from` gave.
func _expect(answer: Dictionary, key: String, fallback: Variant, from: String) -> Variant:
	var value: Variant = answer.get(key)
	if typeof(value) == typeof(fallback):
		return value
	if fallback is int and value is float and is_finite(value):
		return int(value)
	var said := "%s.%s" % [from, key]
	if not _missing.has(said):
		_missing[said] = true
		_error("engine.%s() gave no usable \"%s\" (%s); taking %s" % [from, key, JSON.stringify(value),
				JSON.stringify(fallback)])
	return fallback


func _error(message: String, severe: bool = true) -> void:
	var data := _fields({"kind": "message", "message": "MatchController: " + message})
	if severe:
		Log.error("app", "app.error", data)
	else:
		Log.warn("app", "app.error", data)


func _fields(data: Dictionary) -> Dictionary:
	if not _match_id.is_empty():
		data["match_id"] = _match_id
		data["frame"] = _frame_number
	return data


static func _dict(value: Variant) -> Dictionary:
	return value if value is Dictionary else {}


static func _list(value: Variant) -> Array:
	return value if value is Array else []
