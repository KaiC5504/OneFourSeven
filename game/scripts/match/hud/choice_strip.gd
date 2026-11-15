class_name ChoiceStrip
extends Control
## The rule decisions the game requires (spec 11.1, 11.2): after the opponent's foul (play, put
## them back in, replace the balls), a re-rack offer, the toss for a re-spotted black. A row of
## buttons low in the safe area, one per option, with one line saying what is being asked. It is
## not modal: the table stays visible, and nothing else is tappable through the buttons.
##
## Contract skeleton (M3): the signatures and signal are fixed; the look and the body are the
## builder's.

## A decision name from the options offered.
signal chosen(kind: String)


## Offers `options` (decision names, in order); `frame` is the frame view, for the words (a free
## ball called, the penalty). Replaces any offer showing.
func offer(_options: Array[String], _frame: Dictionary) -> void:
	pass


func dismiss() -> void:
	pass


func is_offering() -> bool:
	return false


## The button for a decision name; null when it isn't offered.
func button(_kind: String) -> Button:
	return null


## The question shown above the buttons, "" when nothing is offered.
func prompt() -> String:
	return ""


## Puts the strip centred along the bottom of `area` (canvas units).
func place_in(_area: Rect2) -> void:
	pass


## The canvas rect it covers while offering, else an empty rect.
func covered_rect() -> Rect2:
	return Rect2()


## A decision's button text: play "Play", play_again "Put them back in", replace "Replace the
## balls", accept_rerack "Re-rack", refuse_rerack "Play on", play_first "Play first", play_second
## "Opponent first", claim_frame "Claim the frame". `frame` picks "Take the free ball" for play
## when a free ball was called. "" for an unknown kind.
static func label_for(_kind: String, _frame: Dictionary = {}) -> String:
	return ""
