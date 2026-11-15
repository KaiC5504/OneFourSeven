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
## Contract skeleton (M3): the signatures and signals are fixed; the bodies are the builder's.

## The balls on the table changed other than by a stroke's own motion. reason: &"frame" (a frame
## was racked), &"shot" (a stroke was judged: colours spotted, the cue ball in hand), &"replaced"
## (balls put back after a miss), &"cue" (the in-hand cue ball was placed), &"ended" (the match
## was left).
signal table_changed(balls: Array[Dictionary], cue_ball: int, reason: StringName)
## The frame view after anything changed it; the scoreboard and the controls follow this.
signal frame_changed(frame: Dictionary)
## What the referee announces, in order, after a stroke or a choice.
signal calls_made(calls: Array, shot_id: String)
## `player` may play a stroke now. `person` is false for the opponent, whose stroke arrives by
## opponent_ready.
signal turn_started(player: int, person: bool)
## The person must choose one of `options` (decision names) before play goes on.
signal choice_needed(options: Array[String])
## The opponent's stroke, as OfsMatch.standin_shot gives it: the table scene places the cue ball
## when places_cue, shows the cue on the line and plays `strike` exactly, nominating `nominated`.
signal opponent_ready(shot: Dictionary)
## The opponent chose `kind` (a decision name); it has been applied.
signal opponent_chose(kind: String)
## A frame ended: its summary (serialize.hpp) and the match. The match went on unless match_ended
## follows.
signal frame_ended(summary: Dictionary, match_state: Dictionary)
signal match_ended(match_state: Dictionary)

const PERSON := 0
const OPPONENT := 1
## Seconds before the opponent acts, at pace 1 (spec 10.5: at least about 0.6 s).
const THINK_SECONDS := 0.9
const CHOICE_SECONDS := 1.2

## OfsMatch, or in tests an object with the same methods (tests/support/fake_match_engine.gd).
var engine: Object
## The frame's balls as the table scene's ball list, and the cue ball's place in it.
var balls: Array[Dictionary] = []
var cue_ball := 0
## The stand-in also plays the person's strokes and choices (the harness's whole-match run).
var autoplay := false
## Scales the opponent's waits; 0 acts on the next frame.
var pace := 1.0


## Creates and configures the engine from Physics and the embedded parameter files unless a test
## has set one. False, with app.error logged, when it can't be configured.
func ensure_engine() -> bool:
	return engine != null


## True from start() until the match ends or is abandoned.
func is_active() -> bool:
	return false


## Counts up whenever `balls` changes, as FreeTable.version does: the guides' cache key.
func version() -> int:
	return 0


## config: {best_of: int, first_breaker: "random" | "you" | "computer", seed: int (optional; a
## fresh one when absent)}. Starts frame 1 and emits table_changed(&"frame"), frame_changed and
## the first turn. False when the engine refuses.
func start(_config: Dictionary) -> bool:
	return false


## Leaves the match with no winner (Quit, spec 11.2); logs through the engine.
func abandon() -> void:
	pass


func frame() -> Dictionary:
	return {}


func match_state() -> Dictionary:
	return {}


## The person may play a stroke now: a match is on, nothing is pending, no stroke is being
## judged and the striker is the person (or autoplay is off and it is their turn).
func person_to_play() -> bool:
	return false


## The striker's cue ball is in hand and has not been confirmed in place (spec 8.6).
func placing() -> bool:
	return false


## The legal place nearest `xy` for the in-hand cue ball.
func nearest_cue_placement(xy: Vector2) -> Vector2:
	return xy


## Moves the in-hand cue ball to `xy`; false, the ball where it was, when the engine refuses.
## Emits table_changed(&"cue").
func place_cue(_xy: Vector2) -> bool:
	return false


## The Confirm button: the cue ball stays where it is and the person aims (spec 8.6).
func confirm_cue() -> void:
	pass


## Back to placing, while the cue ball is still in hand.
func move_cue_again() -> void:
	pass


## The colours the striker may declare now (the frame view's "nominations"); empty when no
## nomination is open.
func nominations() -> Array[String]:
	return []


## The declared colour, "" for none.
func nominated() -> String:
	return ""


## Declares `ball`; how is "aim" (the aim line chose it), "tap", "harness" or "opponent". False
## when it is not one of nominations(). Logs match.nominated when the choice changes.
func nominate(_ball: String, _how: String) -> bool:
	return false


## Why the person's stroke may not be played yet: "" when it may, else "placing" (the cue ball is
## not confirmed), "nominate" (a colour must be declared and none is), "choice" (a decision is
## pending), "opponent" or "inactive".
func strike_blocked() -> String:
	return "inactive"


## The table scene struck a stroke with this id from `balls`.
func shot_started(_shot_id: String) -> void:
	pass


## The stroke has stopped: the engine judges it. Updates balls, then emits calls_made,
## table_changed(&"shot"), frame_changed and whichever of turn_started, choice_needed,
## opponent_ready, frame_ended and match_ended follows. Returns the judgement ({} when refused).
func shot_finished(_record: OfsShotRecord) -> Dictionary:
	return {}


## The person's choice among the options offered. False when it is not one of them.
func choose(_kind: String) -> bool:
	return false


## Racks the next frame after frame_ended; false when there is none to rack.
func next_frame() -> bool:
	return false


## Pause menu (spec 11.1). False when the engine refuses (not allowed now).
func concede_frame() -> bool:
	return false


func concede_match() -> bool:
	return false


## The harness's part of the state dump: {active, frame, match, nominated, placing, blocked,
## autoplay, waiting: "shot" | "choice" | "opponent" | "frame_summary" | "match_summary" | ""}.
func harness_state() -> Dictionary:
	return {"active": false}
