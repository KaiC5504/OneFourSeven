extends Control
## Dev preview of one of the M3 screens over a plain dark green background, laid out in the
## iPhone 17 Pro Max's landscape safe area whatever the window is:
##   godot --path game --resolution 1912x880 res://scenes/dev/screens_preview.tscn -- --mute --screen=pause
## --screen is home (the default), setup, pause, pause_confirm, frame_summary or match_summary.
## The summaries are filled from the example match; --frames=<n> makes the match summary's match
## n frames long and --person=1 shows either summary from the other chair.

const MATCH_PATH := "res://data/rules/example_match.json"
const SCREENS: Array[String] = ["home", "setup", "pause", "pause_confirm", "frame_summary", "match_summary"]
## The phone in landscape: 440 pt tall, safe insets 62 pt left and right (Dynamic Island) and
## 21 pt at the bottom (home indicator).
const PHONE_HEIGHT_PT := 440.0
const INSET_SIDE_PT := 62.0
const INSET_BOTTOM_PT := 21.0

var _screen: Control


func _ready() -> void:
	var background := ColorRect.new()
	background.color = Color(0.05, 0.27, 0.15)
	background.set_anchors_preset(PRESET_FULL_RECT)
	add_child(background)
	var name_ := _argument("--screen", "home")
	if not SCREENS.has(name_):
		Log.warn("app", "app.error", {"kind": "message",
				"message": "screens_preview: no screen '%s'; known: %s" % [name_, ", ".join(SCREENS)]})
		name_ = "home"
	var match_state := _example_match()
	var person := clampi(_argument("--person", "0").to_int(), 0, 1)
	match name_:
		"home":
			_screen = HomeScreen.new()
		"setup":
			_screen = MatchSetupScreen.new()
		"pause", "pause_confirm":
			_screen = PauseMenu.new()
		"frame_summary":
			_screen = FrameSummary.new()
		"match_summary":
			_screen = MatchSummary.new()
	_screen.set("auto_place", false)
	add_child(_screen)
	match name_:
		"home", "setup":
			_screen.call("open")
		"pause":
			(_screen as PauseMenu).open(true)
		"pause_confirm":
			(_screen as PauseMenu).open(true)
			(_screen as PauseMenu).button("quit").pressed.emit()
		"frame_summary":
			var history: Array = match_state.get("history", [])
			(_screen as FrameSummary).show_summary(history[0] if not history.is_empty() else {}, match_state, person)
		"match_summary":
			(_screen as MatchSummary).show_summary(
					_finished(match_state, maxi(_argument("--frames", "3").to_int(), 1)), person)
	get_viewport().size_changed.connect(_place)
	_place()


func _place() -> void:
	var canvas := get_viewport_rect().size
	var unit := canvas.y / PHONE_HEIGHT_PT
	_screen.call("place_in", Rect2(INSET_SIDE_PT * unit, 0.0, canvas.x - 2.0 * INSET_SIDE_PT * unit,
			canvas.y - INSET_BOTTOM_PT * unit))


func _argument(flag: String, fallback: String) -> String:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with(flag + "="):
			return arg.substr(flag.length() + 1)
	return fallback


func _example_match() -> Dictionary:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(MATCH_PATH))
	if parsed is Dictionary:
		return parsed
	Log.warn("app", "app.error", {"kind": "message", "file": MATCH_PATH,
			"message": "screens_preview: %s isn't a JSON object (run scripts/sync_data.py)" % MATCH_PATH})
	return {}


## The example match played out to `frames` frames: its finished frame and the one in play, over
## and over, two to the first player and then two as they stand.
func _finished(match_state: Dictionary, frames: int) -> Dictionary:
	var out := match_state.duplicate(true)
	var in_play: Dictionary = out.get("frame", {})
	var sources: Array = out.get("history", [])
	sources.append({"scores": in_play.get("scores", [0, 0]), "stats": in_play.get("stats", [{}, {}])})
	var history: Array = []
	var won := [0, 0]
	for i in frames:
		var frame: Dictionary = (sources[i % sources.size()] as Dictionary).duplicate(true)
		var scores: Array = frame.get("scores", [0, 0])
		var stats: Array = frame.get("stats", [{}, {}])
		if i % 4 < 2:
			scores.reverse()
			stats.reverse()
		frame["number"] = i + 1
		frame["winner"] = 0 if int(scores[0]) > int(scores[1]) else 1
		frame["reason"] = "black"
		won[int(frame["winner"])] += 1
		history.append(frame)
	out["history"] = history
	out["frames"] = won
	out["over"] = true
	out["winner"] = 0 if won[0] > won[1] else 1
	return out
