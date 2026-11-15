class_name OpponentDriver
extends Node
## The opponent's side of a match for MatchController: after a natural pause (spec 10.5) it asks
## the engine's stand-in for a stroke or a choice. With autoplay it does the same for the person.
## It never touches the table; MatchController passes its results on.

## The stroke the stand-in chose for `player` (OfsMatch.standin_shot's Dictionary, a refusal
## included), with "shot_id": the id its opponent.decision line was logged under, for the table
## scene to strike with.
signal shot_chosen(player: int, shot: Dictionary)
## The stand-in's choice for `player`, a decision name; "" when it gave none.
signal choice_made(player: int, kind: String)

## The seed of the last request sent to the engine: a refusal is logged with it.
var last_seed := 0

## Null when nothing is waiting.
var _engine: Object
var _player := 0
var _for_choice := false
var _left := 0.0
var _fresh := false


func _init() -> void:
	set_process(false)


## Asks for `player`'s stroke after `seconds`; any earlier request is dropped.
func request_shot(engine: Object, player: int, seconds: float) -> void:
	_request(engine, player, seconds, false)


## Asks for `player`'s choice among the frame's options after `seconds`.
func request_choice(engine: Object, player: int, seconds: float) -> void:
	_request(engine, player, seconds, true)


## Drops whatever is waiting (the match was left, a frame ended).
func cancel() -> void:
	_engine = null
	set_process(false)


func is_waiting() -> bool:
	return _engine != null


func _request(engine: Object, player: int, seconds: float, for_choice: bool) -> void:
	_engine = engine
	_player = player
	_for_choice = for_choice
	_left = maxf(seconds, 0.0)
	_fresh = true
	set_process(engine != null)


func _process(delta: float) -> void:
	# The first frame's delta is time from before the request.
	if _fresh:
		_fresh = false
	else:
		_left -= delta
	if _left > 0.0:
		return
	var engine := _engine
	# Before the signal: a listener may ask again.
	cancel()
	last_seed = Physics.new_seed()
	if _for_choice:
		var choice: Variant = engine.standin_choice(_player, last_seed)
		choice_made.emit(_player, choice if choice is String else "")
		return
	var shot_id := Physics.new_shot_id()
	var answer: Variant = engine.standin_shot(last_seed, {"shot_id": shot_id})
	var shot: Dictionary = (answer as Dictionary).duplicate(true) if answer is Dictionary else {}
	shot["shot_id"] = shot_id
	shot_chosen.emit(_player, shot)
