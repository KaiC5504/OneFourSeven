class_name OpponentDriver
extends Node
## The opponent's side of a match for MatchController: after a natural pause (spec 10.5) it asks
## the engine's stand-in for a stroke or a choice. With autoplay it does the same for the person.
## It never touches the table; MatchController passes its results on.
##
## Contract skeleton (M3): the builder of MatchController owns this file too.

## The stroke the stand-in chose for `player` (OfsMatch.standin_shot's Dictionary).
signal shot_chosen(player: int, shot: Dictionary)
## The stand-in's choice for `player`, a decision name.
signal choice_made(player: int, kind: String)


## Asks for `player`'s stroke after `seconds`; any earlier request is dropped.
func request_shot(_engine: Object, _player: int, _seconds: float) -> void:
	pass


## Asks for `player`'s choice among the frame's options after `seconds`.
func request_choice(_engine: Object, _player: int, _seconds: float) -> void:
	pass


## Drops whatever is waiting (the match was left, a frame ended).
func cancel() -> void:
	pass


func is_waiting() -> bool:
	return false
