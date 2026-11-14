class_name NextTarget
extends RefCounted
## Picks where the aim turns after a shot, as the camera swoops back behind the cue ball: the
## easiest clear pot, preferring the kind of ball that would be on.


## balls are FreeTable balls ({"ball", "index", "x", "y"} in core metres); prefer is "red",
## "colour" or "any". Returns {"index", "ball", "pocket", "aim_rad", "cut_deg", "score",
## "reason"}; pocket is -1 and reason "no clear pot" when it aims straight at the nearest
## preferred ball instead. {} when there is no object ball.
static func choose(balls: Array[Dictionary], cue_ball: int, prefer: String) -> Dictionary:
	return {}
