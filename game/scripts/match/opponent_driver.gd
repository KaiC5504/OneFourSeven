class_name OpponentDriver
extends Node
## The opponent's side of a match for MatchController: after a natural pause (spec 10.5) it asks
## the engine for a stroke or a choice, from a level or, with no level, the stand-in. A level
## thinks on the engine's worker thread from the moment it is asked, so the pause is its thinking
## time. With autoplay it does the same for the person. It never touches the table;
## MatchController passes its results on.

## The stroke chosen for `player` (OfsMatch.standin_shot's Dictionary, or opponent_take's with
## its "level", "meant" and "think"; a refusal included), with "shot_id": the id its
## opponent.decision line was logged under, for the table scene to strike with.
signal shot_chosen(player: int, shot: Dictionary)
## The choice for `player`, a decision name; "" when it gave none.
signal choice_made(player: int, kind: String)
## A level started thinking about a stroke (true), or the stroke was handed over or dropped.
signal thinking_changed(on: bool)

## A level's stroke stops on the clock here: spec 10.6 allows 1.5 s from asking to striking, and
## the cue's stand and feather on the line (OpponentShow) take the rest.
const LEVEL_MAX_MS := 800.0
## Why thinking was dropped, as opponent.cancelled's "why" names it.
const CANCEL_WHYS: Array[String] = ["left", "conceded", "claimed", "replaced"]

## The seed of the last request sent to the engine: a refusal is logged with it.
var last_seed := 0
## The engine method the last request went to ("standin_shot", "opponent_think", "opponent_take",
## "standin_choice", "opponent_choice"): a refusal names it.
var last_call := ""

## Null when nothing is waiting.
var _engine: Object
var _player := 0
var _level := ""
var _for_choice := false
var _left := 0.0
var _fresh := false
var _shot_id := ""
var _thinking := false
## opponent_take's answer, held until the pause is over.
var _answer := {}
## Why opponent_think refused; "" when it did not.
var _think_refused := ""


func _init() -> void:
	set_process(false)


## Asks for `player`'s stroke; it is handed over after `seconds` at the soonest. `level` is a level
## id; "" asks the stand-in. Any earlier request is dropped.
func request_shot(engine: Object, player: int, seconds: float, level := "") -> void:
	_request(engine, player, seconds, false, level)
	if engine == null or level.is_empty():
		return
	last_seed = Physics.new_seed()
	_shot_id = Physics.new_shot_id()
	last_call = "opponent_think"
	if bool(engine.opponent_think(level, last_seed, {"shot_id": _shot_id}, {"max_ms": LEVEL_MAX_MS})):
		_thinking = true
		thinking_changed.emit(true)
	else:
		_think_refused = str(engine.last_error())
		if _think_refused.is_empty():
			_think_refused = "opponent_think refused with no last_error"


## Asks for `player`'s choice among the frame's options after `seconds`.
func request_choice(engine: Object, player: int, seconds: float, level := "") -> void:
	_request(engine, player, seconds, true, level)


## Drops whatever is waiting, the thinking included; `why` (one of CANCEL_WHYS) is what the
## engine logs for the thinking dropped.
func cancel(why := "left") -> void:
	var engine := _engine
	_engine = null
	set_process(false)
	if _thinking:
		_thinking = false
		if engine != null:
			if not CANCEL_WHYS.has(why):
				Log.warn("app", "app.error", {"kind": "message", "message":
						"OpponentDriver: cancel(\"%s\") is not one of %s; the engine is told \"left\"" % [why, CANCEL_WHYS]})
				why = "left"
			engine.opponent_cancel(why)
			# Already taken, the stroke is no longer the engine's to cancel: without this line its
			# logged decision would read as one that was played.
			if bool(_answer.get("ok", false)):
				Log.info("opponent", "opponent.cancelled", {"level": _level, "why": why, "player": _player,
						"seed": str(last_seed), "shot_id": _shot_id})
		thinking_changed.emit(false)


func is_waiting() -> bool:
	return _engine != null


## A level's stroke was asked for and has not been handed over.
func is_thinking() -> bool:
	return _thinking


func _request(engine: Object, player: int, seconds: float, for_choice: bool, level: String) -> void:
	cancel("replaced")
	_engine = engine
	_player = player
	_level = level
	_for_choice = for_choice
	_left = maxf(seconds, 0.0)
	_fresh = true
	_shot_id = ""
	_answer = {}
	_think_refused = ""
	set_process(engine != null)


func _process(delta: float) -> void:
	# Becoming ready turns processing back on: with nothing asked there is no engine to ask.
	if _engine == null:
		set_process(false)
		return
	# The first frame's delta is time from before the request.
	if _fresh:
		_fresh = false
	else:
		_left -= delta
	if _thinking and _answer.is_empty():
		var taken: Variant = _engine.opponent_take()
		last_call = "opponent_take"
		if taken is Dictionary and not (taken as Dictionary).is_empty():
			_answer = (taken as Dictionary).duplicate(true)
		elif not bool(_engine.opponent_thinking()):
			# Nothing to wait for any more: without this the opponent would never play.
			_answer = {"ok": false, "error": "opponent_take gave nothing and the level is no longer thinking (%s)"
					% str(_engine.last_error())}
	if _left > 0.0 or (_thinking and _answer.is_empty()):
		return
	if not _level.is_empty() and not _for_choice:
		_hand_over()
		return
	var engine := _engine
	# Before the signal: a listener may ask again.
	cancel()
	last_seed = Physics.new_seed()
	if _for_choice:
		var choice: Variant
		if _level.is_empty():
			last_call = "standin_choice"
			choice = engine.standin_choice(_player, last_seed)
		else:
			last_call = "opponent_choice"
			choice = engine.opponent_choice(_level, _player, last_seed)
		choice_made.emit(_player, choice if choice is String else "")
		return
	var shot_id := Physics.new_shot_id()
	last_call = "standin_shot"
	var answer: Variant = engine.standin_shot(last_seed, {"shot_id": shot_id})
	var shot: Dictionary = (answer as Dictionary).duplicate(true) if answer is Dictionary else {}
	shot["shot_id"] = shot_id
	shot_chosen.emit(_player, shot)


## A level's stroke, or its refusal to think, once the pause is over.
func _hand_over() -> void:
	var shot := _answer
	if not _think_refused.is_empty():
		shot = {"ok": false, "error": _think_refused}
	shot["shot_id"] = _shot_id
	var was_thinking := _thinking
	_thinking = false
	_engine = null
	set_process(false)
	if was_thinking:
		thinking_changed.emit(false)
	shot_chosen.emit(_player, shot)
