class_name PositionReader
extends RefCounted
## The opponent's evaluation handed to the game (spec 8.5, docs/opponent/README.md "The game,
## stage 2"): a reader level with the owner's own fitted error says what a position is worth to
## him and which pot it would play. It has an OfsMatch of its own, configured as MatchController
## configures the match's, so it reads the free table and a match alike and never touches the
## match's frame. Not configured, it answers nothing and the game keeps to the geometric pick.

const LEVEL_PATH := "res://data/opponent_person.json"
## OfsMatch's reader thinks to a 400 ms clock and then answers with what it has; past that and
## this much more, an answer that has not come is not coming.
const CLOCK_MS := 400
const GRACE_MS := 200

## OfsMatch, or in tests an object with the same reader methods.
var engine: Object
var _configured := false
var _logged := false
## The answer taken from the engine and not yet used.
var _answer := {}
## Why the last thinking ended with no answer (reader_take's error); "" otherwise.
var _failure := ""
var _asked_ms := 0


## Makes and configures the engine unless one was given. False, logged once, when it can't be.
func configure(given: Object = null) -> bool:
	if _configured:
		return true
	var problems: PackedStringArray = []
	var texts: Array[String] = []
	for path: String in [MatchController.REFEREE_PATH, MatchController.STANDIN_PATH, LEVEL_PATH]:
		var text := FileAccess.get_file_as_string(path)
		if text.is_empty():
			problems.append("%s is missing or empty (%s); run scripts/sync_data.py" % [path,
					error_string(FileAccess.get_open_error())])
		texts.append(text)
	var made: Object = given if given != null else (OfsMatch.new() if ClassDB.class_exists("OfsMatch") else null)
	if made == null:
		problems.append("no OfsMatch: the extension is not loaded")
	elif problems.is_empty():
		if given == null and not bool(made.configure(Physics.sim, texts[0], texts[1])):
			problems.append("configure refused the physics (%s), %s or %s: %s" % [
					"parameters " + Physics.sim.params_hash() if Physics.is_ready() else "not configured",
					MatchController.REFEREE_PATH, MatchController.STANDIN_PATH, str(made.last_error())])
		elif not bool(made.configure_reader(texts[2])):
			problems.append("configure_reader refused %s: %s" % [LEVEL_PATH, str(made.last_error())])
	if not problems.is_empty():
		_fail("no position reader, the next ball is picked by geometry alone: " + "; ".join(problems))
		return false
	engine = made
	_configured = true
	return true


func is_ready() -> bool:
	return _configured


## What the position is worth to the person: OfsMatch.read_position's answer, or {} when there is
## no reader or the read failed (the engine has logged why).
func read(balls: Array, cue_ball: int, on: Dictionary) -> Dictionary:
	if not _configured:
		return {}
	var got: Variant = engine.read_position(balls, cue_ball, on)
	if not got is Dictionary or not bool((got as Dictionary).get("ok", false)):
		return {}
	return got


## Starts the reader thinking about the position a shot will leave; any earlier answer is dropped.
func think(balls: Array, cue_ball: int, on: Dictionary, seed: int) -> bool:
	_answer = {}
	_failure = ""
	_asked_ms = Time.get_ticks_msec()
	if not _configured:
		return false
	return bool(engine.reader_think(balls, cue_ball, on, seed))


## Takes the answer if it has come. Cheap enough for every frame of a shot.
func poll() -> void:
	if not _configured or not _answer.is_empty() or not bool(engine.reader_thinking()):
		return
	var got: Variant = engine.reader_take()
	if not got is Dictionary:
		_failure = "reader_take gave %s, not a Dictionary" % type_string(typeof(got))
	elif bool((got as Dictionary).get("ok", false)):
		_answer = got
	elif not String((got as Dictionary).get("error", "")).is_empty():
		# The engine has logged it; the thinking is spent.
		_failure = String(got["error"])


## The reader's pot ({ball, pocket, chance, worth, leave, stopped, fallback}; ball "" when it would
## not pot), or {} when it has not answered yet. Used once: a second call gives {}.
func answer() -> Dictionary:
	poll()
	var out := _answer
	_answer = {}
	return out


## Asked and still thinking: its answer may yet come (poll()).
func thinking() -> bool:
	return _configured and _answer.is_empty() and bool(engine.reader_thinking())


## Thinking past its clock and the grace after it.
func overdue() -> bool:
	return thinking() and Time.get_ticks_msec() - _asked_ms > CLOCK_MS + GRACE_MS


## Since think(), in milliseconds.
func asked_ms_ago() -> int:
	return Time.get_ticks_msec() - _asked_ms


func failure() -> String:
	return _failure


func cancel() -> void:
	_answer = {}
	if _configured and bool(engine.reader_thinking()):
		engine.reader_cancel()


func _fail(message: String) -> void:
	if _logged:
		return
	_logged = true
	Log.warn("app", "app.error", {"kind": "message", "message": "PositionReader: " + message})
