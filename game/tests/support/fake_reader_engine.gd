class_name FakeReaderEngine
extends RefCounted
## OfsMatch's reader methods with scripted answers, for PositionReader and the table scene.

## read_position's worth, and what reader_take gives once it has been asked `takes_before` times
## (-1: never ready).
var worth := 10.0
var answer := {"ok": true, "error": "", "ball": "", "pocket": -1}
var takes_before := 0
var refuse := false
## Every reader_think: [balls, cue_ball, on, seed]; every read_position: [balls, cue_ball, on].
var thought: Array = []
var reads: Array = []
var cancels := 0
var _thinking := false
var _takes := 0


func configure_reader(_text: String) -> bool:
	return not refuse


func last_error() -> String:
	return "the test refuses the reader" if refuse else ""


func read_position(balls: Array, cue_ball: int, on: Dictionary) -> Dictionary:
	reads.append([balls.duplicate(true), cue_ball, on.duplicate(true)])
	return {"ok": true, "error": "", "worth": worth, "pots": 1, "chance": 0.5, "ball": "", "pocket": -1}


func reader_think(balls: Array, cue_ball: int, on: Dictionary, seed: int) -> bool:
	thought.append([balls.duplicate(true), cue_ball, on.duplicate(true), seed])
	_thinking = true
	_takes = 0
	return true


func reader_thinking() -> bool:
	return _thinking


func reader_take() -> Dictionary:
	if not _thinking or takes_before < 0 or _takes < takes_before:
		_takes += 1
		return {"ok": false, "error": ""}
	_thinking = false
	return answer.duplicate(true)


func reader_cancel() -> void:
	cancels += 1
	_thinking = false
