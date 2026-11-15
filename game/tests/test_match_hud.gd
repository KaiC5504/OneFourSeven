extends TestCase
## The match's HUD pieces: what the scoreboard says for a frame view and a match, the referee's
## words and how long they stay, the choice strip's buttons and question, the geometry of
## nomination and its ring, the cue placer through a placement, and that each piece sits inside
## the area it is given and leaves every touch but its buttons' to the table.

const Kit := preload("res://scripts/match/hud/hud_kit.gd")
const MainScript := preload("res://scripts/main.gd")
const FRAME_PATH := "res://data/rules/example_frame_view.json"
const MATCH_PATH := "res://data/rules/example_match.json"
const SAFE := Rect2(124.0, 0.0, 1664.0, 838.0)
const RADIUS := 0.02625
const COLOURS: Array[String] = ["yellow", "green", "brown", "blue", "pink", "black"]
const DECISIONS := {
	"play": "Play",
	"play_again": "Computer plays again",
	"replace": "Replace the balls",
	"accept_rerack": "Re-rack",
	"refuse_rerack": "Play on",
	"play_first": "Play first",
	"play_second": "Computer first",
	"claim_frame": "Claim the frame",
}

var _log_start := 0


func test_scoreboard_says_what_the_frame_and_the_match_give() -> void:
	var board := Scoreboard.new()
	check_eq(board.texts(), {}, "nothing before a frame")
	var frame := _frame()
	board.show_frame(frame, _match(), 0)
	check_eq(board.texts(), {"you": 24, "opponent": 31, "frames_you": 0, "frames_opponent": 1, "best_of": 3,
			"break": 0, "remaining": 43, "required": "", "on": "Free ball", "turn": "you"}, "the example frame")
	frame["break_points"] = 16
	frame["scores"] = [40, 31]
	(frame["view"] as Dictionary)["points_remaining"] = 27
	board.show_frame(frame, _match(), 0)
	check_eq(board.texts()["break"], 16, "the break once there is one")
	check_eq(board.texts()["you"], 40, "the score follows the frame")
	check_eq(board.texts()["remaining"], 27, "and the points remaining")
	var longer := _match()
	(longer["config"] as Dictionary)["best_of"] = 11
	longer["frames"] = [5, 4]
	board.show_frame(frame, longer, 0)
	check_eq([board.texts()["frames_you"], board.texts()["frames_opponent"], board.texts()["best_of"]], [5, 4, 11],
			"the match's frames and length")
	board.free()


func test_on_text_names_the_ball_on() -> void:
	var frame := _frame()
	frame["free_ball"] = false
	check_eq(Scoreboard.on_text(frame), "Red", "red")
	frame["on"] = {"kind": "any_colour"}
	check_eq(Scoreboard.on_text(frame), "Colour", "a colour of choice")
	for colour in COLOURS:
		frame["on"] = {"kind": "colour", "colour": colour}
		check_eq(Scoreboard.on_text(frame), colour.capitalize(), colour)
	check_eq(Scoreboard.on_text({"on": {"kind": "colour", "colour": "blue"}}), "Blue", "blue, in a bare frame")
	frame["free_ball"] = true
	check_eq(Scoreboard.on_text(frame), "Free ball", "a free ball, whatever is on")
	var board := Scoreboard.new()
	board.show_frame(frame, _match(), 0)
	check_eq(board.texts()["on"], "Free ball", "the scoreboard says the same")
	frame["free_ball"] = false
	board.show_frame(frame, _match(), 0)
	check_eq(board.texts()["on"], "Black", "and follows the frame")
	board.free()


func test_snookers_required_says_whose_and_shows_only_when_a_player_requires_them() -> void:
	var board := Scoreboard.new()
	var frame := _frame()
	for case: Array in [[[0, 0], 0, ""], [[20, 0], 0, "You need snookers"], [[0, 5], 0, "Computer needs snookers"],
			[[20, 0], 1, "Computer needs snookers"], [[0, 5], 1, "You need snookers"], [[0, 0], 1, ""]]:
		(frame["view"] as Dictionary)["required"] = case[0]
		board.show_frame(frame, _match(), case[1])
		check_eq(board.texts()["required"], case[2], "required %s, the person player %d" % [case[0], case[1]])
	board.free()


func test_once_the_frame_is_over_only_the_scores_and_frames_stay() -> void:
	var board := Scoreboard.new()
	var frame := _frame()
	frame["break_points"] = 16
	(frame["view"] as Dictionary)["required"] = [0, 20]
	board.show_frame(frame, _match(), 0)
	check(board.texts()["turn"] == "you" and board.texts()["break"] == 16 and not board.texts()["required"].is_empty(),
			"in play: %s" % [board.texts()])
	frame["over"] = true
	board.show_frame(frame, _match(), 0)
	check_eq(board.texts(), {"you": 24, "opponent": 31, "frames_you": 0, "frames_opponent": 1, "best_of": 3,
			"break": 0, "remaining": -1, "required": "", "on": "", "turn": ""}, "over")
	frame["over"] = false
	board.show_frame(frame, _match(), 0)
	check_eq(board.texts()["turn"], "you", "the next frame shows it all again")
	board.free()


func test_the_turn_marker_follows_the_striker_and_who_the_person_is() -> void:
	var board := Scoreboard.new()
	var frame := _frame()
	frame["striker"] = 0
	board.show_frame(frame, _match(), 0)
	check_eq(board.texts()["turn"], "you", "the person, player 0, at the table")
	frame["striker"] = 1
	board.show_frame(frame, _match(), 0)
	check_eq(board.texts()["turn"], "opponent", "the opponent at the table")
	board.show_frame(frame, _match(), 1)
	var said := board.texts()
	check_eq(said["turn"], "you", "the person as player 1, at the table")
	check_eq([said["you"], said["opponent"]], [31, 24], "their score is player 1's")
	check_eq([said["frames_you"], said["frames_opponent"]], [1, 0], "and their frames")
	frame["striker"] = 0
	board.show_frame(frame, _match(), 1)
	check_eq(board.texts()["turn"], "opponent", "player 0 at the table is then the opponent")
	board.free()


func test_pause_button_is_pressed_through_the_scoreboard() -> void:
	var board := Scoreboard.new()
	var got: Array = []
	board.pause_pressed.connect(func() -> void: got.append("pause"))
	var pause := board.pause_button()
	check(pause != null, "there is a pause button")
	pause.pressed.emit()
	check_eq(got, ["pause"], "pause_pressed")
	check(pause.size.x >= 88.0 and pause.size.y >= 88.0, "big enough to touch: %s" % pause.size)
	check_eq(pause.focus_mode, Control.FOCUS_NONE, "no keyboard focus")
	board.free()


func test_scoreboard_sits_at_the_top_centre_of_its_area() -> void:
	var board := Scoreboard.new()
	board.auto_place = false
	tree.root.add_child(board)
	board.show_frame(_frame(), _match(), 0)
	await tree.process_frame
	board.place_in(SAFE)
	var covered := board.covered_rect()
	check(SAFE.encloses(covered), "inside the area: %s" % covered)
	check_eq(covered.position.y, 16.0, "at the top, a margin down")
	check(covered.size.y <= 90.0, "one slim strip: %s" % covered)
	check(covered.size.x < 0.6 * SAFE.size.x, "compact: %s" % covered)
	var pause := board.pause_button()
	check_eq(pause.position, covered.position, "the pause button at the strip's left end")
	var strip := board.get_node("Strip") as Control
	check(strip.position.x > pause.position.x + pause.size.x, "the strip to its right")
	check(strip.position.x < SAFE.get_center().x and strip.position.x + strip.size.x > SAFE.get_center().x,
			"the strip across the middle")
	# The frames stand at the middle: the two sides differ only by the names' lengths.
	var left := SAFE.get_center().x - strip.position.x
	var right := strip.position.x + strip.size.x - SAFE.get_center().x
	check(right > left and right - left < 120.0, "the frames in the middle: %f left, %f right" % [left, right])
	board.place_in(SAFE)
	check_eq(board.covered_rect(), covered, "placing again moves nothing")
	var elsewhere := Rect2(300.0, 40.0, 1200.0, 700.0)
	board.place_in(elsewhere)
	check(elsewhere.encloses(board.covered_rect()), "follows another area: %s" % board.covered_rect())
	check_eq(_takes_touches(board), [] as Array[String], "only the pause button takes touches")
	board.queue_free()
	await tree.process_frame


func test_whose_snookers_widens_the_strip_about_the_middle_while_it_shows() -> void:
	var board := Scoreboard.new()
	board.auto_place = false
	tree.root.add_child(board)
	var frame := _frame()
	board.show_frame(frame, _match(), 0)
	board.place_in(SAFE)
	var strip := board.get_node("Strip") as Scoreboard.Strip
	var plain := Rect2(strip.position, strip.size)
	var middle := strip.position.x + strip.centre_x()
	check_near(middle, SAFE.get_center().x, 1.0, "the frames at the middle")
	(frame["view"] as Dictionary)["required"] = [0, 20]
	board.show_frame(frame, _match(), 0)
	var wide := Rect2(strip.position, strip.size)
	check(wide.size.x > plain.size.x, "wider for the words: %s after %s" % [wide, plain])
	check(SAFE.encloses(board.covered_rect()), "still inside the area: %s" % board.covered_rect())
	check_near(strip.position.x + strip.centre_x(), middle, 1.0, "the frames stay at the middle")
	var font := strip.get_theme_default_font()
	var words := font.get_string_size(Scoreboard.OPPONENT_NEEDS_SNOOKERS, HORIZONTAL_ALIGNMENT_LEFT, -1.0,
			Scoreboard.Strip.DETAIL_SIZE).x
	check(wide.size.x - plain.size.x <= words + Scoreboard.Strip.DETAIL_GAP + 1.0, "by no more than the words need")
	check_eq(board.pause_button().position.x + board.pause_button().size.x + Scoreboard.PAUSE_GAP, strip.position.x,
			"the pause button keeps to its end")
	(frame["view"] as Dictionary)["required"] = [0, 0]
	board.show_frame(frame, _match(), 0)
	check_eq(Rect2(strip.position, strip.size), plain, "back as it was once nobody needs them")
	board.queue_free()
	await tree.process_frame


func test_hud_text_is_big_enough_to_read() -> void:
	# 28 units is 30 px in its capitals on the phone (1.5 px a unit).
	for font_size: int in [Scoreboard.Strip.NAME_SIZE, Scoreboard.Strip.SCORE_SIZE, Scoreboard.Strip.FRAMES_SIZE,
			Scoreboard.Strip.BEST_SIZE, Scoreboard.Strip.DETAIL_SIZE, CuePlacer.HINT_FONT_SIZE, CuePlacer.FONT_SIZE,
			ChoiceStrip.FONT_SIZE, RefereeLine.FONT_SIZE]:
		check(font_size >= 28, "no text under 28 units: %d" % font_size)
	check(Scoreboard.Strip.SCORE_SIZE > Scoreboard.Strip.NAME_SIZE, "the scores are the biggest thing on it")
	check(Scoreboard.Strip.DETAIL_BASELINE < Scoreboard.HEIGHT, "both lines inside the strip")


func test_scoreboard_shows_what_is_there_when_keys_are_missing() -> void:
	Kit.forget_reported()
	_mark_log()
	var board := Scoreboard.new()
	var frame := _frame()
	frame.erase("scores")
	(frame["view"] as Dictionary).erase("points_remaining")
	var match_state := _match()
	match_state.erase("frames")
	board.show_frame(frame, match_state, 0)
	board.show_frame(frame, match_state, 0)
	var said := board.texts()
	check_eq([said["you"], said["opponent"], said["frames_you"], said["remaining"]], [-1, -1, -1, -1], "blank numbers")
	check_eq([said["best_of"], said["on"], said["turn"]], [3, "Free ball", "you"], "the rest still shows")
	board.show_frame({}, {}, 0)
	check_eq(board.texts()["turn"], "", "an empty frame names nobody")
	var messages := _errors()
	for key: String in ["scores", "points_remaining", "frames", "view", "config", "striker", "on", "free_ball"]:
		check_eq(_count(messages, "\"%s\"" % key), 1, "one app.error naming %s: %s" % [key, messages])
	board.free()


func test_text_for_every_call() -> void:
	var words := {
		"foul": "FOUL 4",
		"miss": "FOUL AND A MISS",
		"free_ball": "FREE BALL",
		"touching_ball": "TOUCHING BALL",
		"warning": "Warning: miss again and the frame is lost",
		"respotted_black": "Black re-spotted",
		"rerack_offer": "Re-rack offered",
		"rerack": "Re-rack",
	}
	for kind: String in words:
		check_eq(RefereeLine.text_for({"kind": kind, "value": 4, "detail": ""}), words[kind], kind)
	check_eq(RefereeLine.text_for({"kind": "foul", "value": 7, "detail": "pot_not_on"}), "FOUL 7", "a foul's value")
	check_eq(RefereeLine.text_for({"kind": "foul", "value": 5.0, "detail": ""}), "FOUL 5", "a value read from JSON")
	check_eq(RefereeLine.text_for({"kind": "foul"}), "FOUL", "a foul with no value")
	var endings := {
		"black": "Frame over",
		"respotted_black": "Frame over",
		"claimed": "Frame claimed",
		"conceded": "Frame conceded",
		"match_conceded": "Match conceded",
		"none": "Frame over",
	}
	for reason: String in endings:
		check_eq(RefereeLine.text_for({"kind": "frame_over", "value": 1, "detail": reason}), endings[reason], reason)
	var awarded := {"kind": "frame_over", "value": 0, "detail": "awarded"}
	check_eq(RefereeLine.text_for(awarded, 0), "Frame awarded to you", "awarded to the person")
	check_eq(RefereeLine.text_for(awarded, 1), "Frame awarded to Computer", "to the opponent")
	check_eq(RefereeLine.text_for({"kind": "frame_over", "value": 1, "detail": "three_misses"}, 0),
			"Three misses: frame awarded to Computer", "three misses")
	check_eq(RefereeLine.text_for({"kind": "frame_over"}), "Frame over", "a frame's end with no reason")
	check_eq(RefereeLine.text_for({"kind": "applause", "value": 0, "detail": ""}), "", "an unknown kind")
	check_eq(RefereeLine.text_for({}), "", "no kind")


func test_calls_are_joined_on_one_line_and_logged() -> void:
	var line := RefereeLine.new()
	check_eq(line.text(), "", "nothing at first")
	var foul := {"kind": "foul", "value": 4, "detail": "no_hit"}
	var miss := {"kind": "miss", "value": 0, "detail": "S3.14(c)"}
	var free_ball := {"kind": "free_ball", "value": 0, "detail": ""}
	_mark_log()
	line.announce([foul, miss, free_ball], 0)
	check_eq(line.text(), "FOUL AND A MISS 4 · FREE BALL", "a foul and its miss are said once")
	var logged: Array[Dictionary] = [{"calls": ["foul", "miss", "free_ball"], "text": "FOUL AND A MISS 4 · FREE BALL"}]
	check_eq(_logged("match.call"), logged, "match.call")
	line.announce([], 0)
	check_eq(line.text(), "FOUL AND A MISS 4 · FREE BALL", "an empty list changes nothing")
	line.announce([{"kind": "foul", "value": 7, "detail": "in_off"}, free_ball], 0)
	check_eq(line.text(), "FOUL 7 · FREE BALL", "a new announcement replaces the old")
	line.announce([{"kind": "touching_ball", "value": 0, "detail": ""}], 0)
	check_eq(line.text(), "TOUCHING BALL", "one call")
	var warned: Array = [foul, miss, {"kind": "warning", "value": 0, "detail": ""}]
	check_eq(RefereeLine.line_for(warned), "FOUL AND A MISS 4 · Warning: miss again and the frame is lost", "line_for is the line")
	line.announce(warned, 0)
	check_eq(line.text(), RefereeLine.line_for(warned), "as announced")
	check_eq(RefereeLine.line_for([miss]), "FOUL AND A MISS", "a miss on its own")
	check_eq(RefereeLine.line_for([{"kind": "rerack_offer"}, {"kind": "rerack"}]), "Re-rack offered · Re-rack", "in order")
	check_eq(_logged("match.call").size(), 4, "one match.call for each announcement that showed")
	line.free()


func test_the_line_goes_after_its_hold() -> void:
	var line := RefereeLine.new()
	tree.root.add_child(line)
	await tree.process_frame
	var label := line.get_node("Line") as Label
	var hold := line.get_node("Hold") as Timer
	check(not label.visible, "nothing shows before a call")
	line.announce([{"kind": "foul", "value": 4, "detail": ""}], 0)
	check(label.visible and label.text == "FOUL 4", "the call shows")
	check(not hold.is_stopped(), "the hold is running")
	check_near(hold.wait_time, RefereeLine.HOLD_SECONDS, 1.0e-9, "for HOLD_SECONDS")
	check_near(hold.time_left, RefereeLine.HOLD_SECONDS, 0.2, "from the announcement")
	await tree.create_timer(0.2).timeout
	line.announce([{"kind": "free_ball", "value": 0, "detail": ""}], 0)
	check_near(hold.time_left, RefereeLine.HOLD_SECONDS, 0.1, "a new call holds afresh")
	hold.stop()
	hold.timeout.emit()
	check_eq(line.text(), "", "the hold over, nothing is said")
	await tree.create_timer(RefereeLine.FADE_SECONDS + 0.25).timeout
	check(not label.visible, "and after a short fade the line is gone")
	line.announce([{"kind": "touching_ball", "value": 0, "detail": ""}], 0)
	check(label.visible and is_equal_approx(label.modulate.a, 1.0), "the next call shows in full")
	hold.stop()
	hold.timeout.emit()
	line.announce([{"kind": "rerack", "value": 0, "detail": ""}], 0)
	check(label.visible and is_equal_approx(label.modulate.a, 1.0), "a call during the fade ends it")
	check_eq(line.text(), "Re-rack", "and is the one showing")
	line.clear()
	check(not label.visible and line.text() == "" and hold.is_stopped(), "clear hides it at once")
	line.queue_free()
	await tree.process_frame


func test_the_line_sits_centred_at_the_top_of_its_area() -> void:
	var line := RefereeLine.new()
	tree.root.add_child(line)
	await tree.process_frame
	var area := Rect2(124.0, 112.0, 1664.0, 726.0)
	line.place_in(area)
	line.announce([{"kind": "foul", "value": 4, "detail": ""}, {"kind": "miss", "value": 0, "detail": ""}], 0)
	var label := line.get_node("Line") as Label
	var rect := Rect2(label.position, label.size)
	check(area.encloses(rect), "inside the area: %s" % rect)
	check_eq(rect.position.y, area.position.y, "at its top")
	check_near(rect.get_center().x, area.get_center().x, 1.0, "centred")
	check(label.get_theme_font_size("font_size") >= 22, "readable")
	line.announce([{"kind": "warning", "value": 0, "detail": ""}], 0)
	check_near(Rect2(label.position, label.size).get_center().x, area.get_center().x, 1.0, "longer words stay centred")
	check_eq(_takes_touches(line), [] as Array[String], "takes no touches")
	line.queue_free()
	await tree.process_frame


func test_a_call_the_line_does_not_know_is_logged_not_shown() -> void:
	Kit.forget_reported()
	_mark_log()
	var line := RefereeLine.new()
	line.announce([{"kind": "applause", "value": 0, "detail": ""}], 0)
	line.announce([{"kind": "applause", "value": 0, "detail": ""}], 0)
	check_eq(line.text(), "", "nothing to say")
	line.announce([{"value": 4}, "foul", {"kind": "free_ball"}], 0)
	check_eq(line.text(), "FREE BALL", "what it does know still shows")
	line.announce([{"kind": "foul"}], 0)
	line.announce([{"kind": "foul"}, {"kind": "frame_over", "value": 0}], 0)
	check_eq(line.text(), "FOUL · Frame over", "a foul with no value, a frame's end with no reason")
	var messages := _errors()
	check_eq(_count(messages, "applause"), 1, "one app.error naming the call: %s" % [messages])
	for key: String in ["kind", "value", "detail"]:
		check_eq(_count(messages, "\"%s\"" % key), 1, "one naming the missing %s: %s" % [key, messages])
	line.free()


func test_label_for_every_decision() -> void:
	for kind: String in DECISIONS:
		check_eq(ChoiceStrip.label_for(kind), DECISIONS[kind], kind)
		check_eq(ChoiceStrip.label_for(kind, _frame_without_free_ball()), DECISIONS[kind], kind + " with a frame")
	var free := _frame()
	check_eq(ChoiceStrip.label_for("play", free), "Take the free ball", "play with a free ball called")
	check_eq(ChoiceStrip.label_for("play_again", free), "Computer plays again", "the others keep their words")
	check_eq(ChoiceStrip.label_for("replace", free), "Replace the balls", "replace too")
	check_eq(ChoiceStrip.label_for("concede_frame"), "", "not a choice the strip offers")
	check_eq(ChoiceStrip.label_for("resign"), "", "an unknown kind")


func test_an_offer_asks_the_question_over_a_button_for_each_option() -> void:
	var strip := ChoiceStrip.new()
	check(not strip.is_offering() and strip.prompt() == "", "nothing offered at first")
	var after_foul: Array[String] = ["play", "play_again", "replace"]
	strip.offer(after_foul, _frame_without_free_ball())
	check(strip.is_offering(), "offering")
	check_eq(strip.prompt(), "Computer fouled: 4 to you", "the question after a foul")
	for kind in after_foul:
		check(strip.button(kind) != null and strip.button(kind).text == DECISIONS[kind], "a button for %s" % kind)
	check(strip.button("accept_rerack") == null, "none for what isn't offered")
	var seven := _frame()
	seven["last_penalty"] = 7
	strip.offer(after_foul, seven)
	check_eq(strip.prompt(), "Computer fouled: 7 to you", "the penalty is the frame's")
	check_eq(strip.button("play").text, "Take the free ball", "the free ball's words")
	var rerack: Array[String] = ["accept_rerack", "refuse_rerack"]
	strip.offer(rerack, _frame())
	check_eq(strip.prompt(), "Re-rack?", "a re-rack")
	check(strip.button("play") == null and strip.button("accept_rerack") != null and strip.button("refuse_rerack") != null,
			"a new offer replaces the old")
	var toss: Array[String] = ["play_first", "play_second"]
	strip.offer(toss, _frame())
	check_eq(strip.prompt(), "You won the toss", "the toss")
	check_eq([strip.button("play_first").text, strip.button("play_second").text], ["Play first", "Computer first"], "its buttons")
	var claim: Array[String] = ["claim_frame"]
	strip.offer(claim, _frame())
	check(not strip.prompt().is_empty() and strip.button("claim_frame").text == "Claim the frame", "claiming the frame")
	strip.free()


func test_each_button_chooses_once_and_the_strip_goes() -> void:
	var strip := ChoiceStrip.new()
	var got: Array = []
	strip.chosen.connect(func(kind: String) -> void: got.append([kind, strip.is_offering()]))
	var all: Array[String] = []
	all.assign(DECISIONS.keys())
	for kind in all:
		strip.offer(all, _frame())
		strip.button(kind).pressed.emit()
		check_eq(got, [[kind, false]], "%s is chosen, the strip already gone" % kind)
		check(not strip.is_offering() and strip.button(kind) == null, "%s: nothing left to press" % kind)
		got.clear()
	strip.free()


func test_dismiss_takes_the_offer_away() -> void:
	var strip := ChoiceStrip.new()
	var got: Array = []
	strip.chosen.connect(func(kind: String) -> void: got.append(kind))
	var options: Array[String] = ["play", "play_again"]
	strip.offer(options, _frame())
	strip.dismiss()
	check(not strip.is_offering(), "not offering")
	check_eq(strip.prompt(), "", "no question")
	check(strip.button("play") == null, "no buttons")
	check_eq(strip.covered_rect(), Rect2(), "covers nothing")
	check_eq(got, [], "nothing chosen")
	var none: Array[String] = []
	strip.offer(none, _frame())
	check(not strip.is_offering(), "an empty offer shows nothing")
	strip.free()


func test_the_strip_sits_low_and_centred_in_its_area() -> void:
	var strip := ChoiceStrip.new()
	tree.root.add_child(strip)
	await tree.process_frame
	# Between the spin ball and the power cue, even about the middle of the screen.
	var area := Rect2(392.0, 0.0, 1128.0, 838.0)
	strip.place_in(area)
	check_eq(strip.covered_rect(), Rect2(), "covers nothing until it offers")
	var options: Array[String] = ["play", "play_again", "replace"]
	strip.offer(options, _frame())
	var covered := strip.covered_rect()
	check(area.encloses(covered), "inside the area with the longest words: %s" % covered)
	# The panel lays its buttons out at the end of the frame.
	await tree.process_frame
	check_eq(strip.covered_rect(), covered, "and stays where the offer put it")
	check_eq(covered.end.y, area.end.y, "along its bottom")
	check_near(covered.get_center().x, area.get_center().x, 1.0, "centred")
	check(covered.size.y < 0.25 * area.size.y, "low: %s" % covered)
	for kind in options:
		var b := strip.button(kind)
		check(b.size.x >= 88.0 and b.size.y >= 88.0, "%s is big enough to touch: %s" % [kind, b.size])
		check_eq(b.focus_mode, Control.FOCUS_NONE, "%s takes no keyboard focus" % kind)
		check(covered.encloses(Rect2(b.global_position, b.size)), "%s is inside the strip" % kind)
		check(b.get_theme_font_size("font_size") >= 22, "%s is readable" % kind)
	var prompt := strip.get_node("Panel").find_child("Prompt", true, false) as Label
	check(prompt != null and prompt.text == strip.prompt(), "the question is a line of its own")
	check(prompt.get_theme_font_size("font_size") >= 22, "and readable")
	check(prompt.global_position.y + prompt.size.y <= strip.button("play").global_position.y, "above the buttons")
	var two: Array[String] = ["accept_rerack", "refuse_rerack"]
	strip.offer(two, _frame())
	check_near(strip.covered_rect().get_center().x, area.get_center().x, 1.0, "a shorter row stays centred")
	check(strip.covered_rect().size.x < covered.size.x, "and is narrower")
	check_eq(_takes_touches(strip), [] as Array[String], "only the buttons take touches")
	strip.queue_free()
	await tree.process_frame


func test_the_strip_copes_with_a_decision_or_a_frame_it_does_not_know() -> void:
	Kit.forget_reported()
	_mark_log()
	var strip := ChoiceStrip.new()
	var options: Array[String] = ["play", "resign"]
	strip.offer(options, {})
	strip.offer(options, {})
	check(strip.button("play") != null and strip.button("resign") == null, "the known decision is offered alone")
	check_eq(strip.prompt(), "Computer fouled", "the question without the penalty")
	var messages := _errors()
	check_eq(_count(messages, "resign"), 1, "one app.error naming the decision: %s" % [messages])
	for key: String in ["last_penalty", "free_ball"]:
		check_eq(_count(messages, "\"%s\"" % key), 1, "one naming the missing %s: %s" % [key, messages])
	strip.free()


func test_first_on_line_is_the_first_ball_the_cue_ball_would_touch() -> void:
	var cue := _ball("cue", 0.0, 0.0)
	# Along +x. A centre 0.0524 m off the line is inside two radii (0.0525 m); 0.0526 m is outside.
	var inside: Array[Dictionary] = [cue, _ball("red", 0.5, 0.0526), _ball("blue", 1.0, 0.0524)]
	check_eq(Nomination.first_on_line(inside, 0, 0.0, RADIUS), 2, "the nearer ball is just off the line, the further just on it")
	var other_side: Array[Dictionary] = [cue, _ball("red", 0.5, -0.0526), _ball("blue", 1.0, -0.0524)]
	check_eq(Nomination.first_on_line(other_side, 0, 0.0, RADIUS), 2, "the same on the other side")
	var behind: Array[Dictionary] = [cue, _ball("pink", -0.3, 0.0)]
	check_eq(Nomination.first_on_line(behind, 0, 0.0, RADIUS), -1, "a ball behind the cue ball is not on the line")
	check_eq(Nomination.first_on_line(behind, 0, PI, RADIUS), 1, "turned round, it is")
	# Black's centre is 0.99 m along the line but 0.05 m off it: the cue ball touches it after
	# 0.99 - sqrt(0.0525^2 - 0.05^2) = 0.9740 m; pink, dead ahead at 1 m, after 1 - 0.0525 = 0.9475 m.
	var graze: Array[Dictionary] = [cue, _ball("black", 0.99, 0.05), _ball("pink", 1.0, 0.0)]
	check_eq(Nomination.first_on_line(graze, 0, 0.0, RADIUS), 2, "the first touched, not the nearest centre")
	graze[2] = _ball("pink", 1.03, 0.0)
	check_eq(Nomination.first_on_line(graze, 0, 0.0, RADIUS), 1, "moved back 3 cm, the pink is touched second")
	# From (-0.4, 0.1) the blue on its spot is at atan2(-0.1, 0.4); the pink is 0.86 m off that line.
	var table: Array[Dictionary] = [_ball("red", 0.9, 0.2), _ball("cue", -0.4, 0.1), _ball("blue", 0.0, 0.0),
			_ball("pink", 0.89225, 0.0)]
	check_eq(Nomination.first_on_line(table, 1, atan2(-0.1, 0.4), RADIUS), 2, "aimed at the blue, the cue ball second in the list")
	check_eq(Nomination.first_on_line(table, 1, atan2(0.1, 1.3), RADIUS), 0, "aimed at the red")
	check_eq(Nomination.first_on_line(table, 1, PI / 2.0, RADIUS), -1, "aimed at nothing")
	check_eq(Nomination.first_on_line(table, 7, 0.0, RADIUS), -1, "no such cue ball")
	var none: Array[Dictionary] = []
	check_eq(Nomination.first_on_line(none, 0, 0.0, RADIUS), -1, "an empty table")


func test_preselect_takes_the_ball_on_the_line_only_when_it_may_be_declared() -> void:
	var balls: Array[Dictionary] = [_ball("cue", 0.0, 0.0), _ball("red", 0.5, 0.0), _ball("blue", 1.0, 0.0), _ball("black", 0.0, 0.8)]
	check_eq(Nomination.preselect(balls, 0, 0.0, COLOURS, RADIUS), "", "a red in front of the blue: nothing eligible is first")
	check_eq(Nomination.preselect(balls, 0, PI / 2.0, COLOURS, RADIUS), "black", "the black, straight up the line")
	check_eq(Nomination.preselect(balls, 0, PI, COLOURS, RADIUS), "", "no ball on the line")
	balls.remove_at(1)
	check_eq(Nomination.preselect(balls, 0, 0.0, COLOURS, RADIUS), "blue", "the red gone, the blue")
	var only_pink: Array[String] = ["pink"]
	check_eq(Nomination.preselect(balls, 0, 0.0, only_pink, RADIUS), "", "the blue when only the pink may be declared")
	# A free ball: a red may be declared too.
	var any: Array[String] = ["red", "yellow", "green", "brown", "blue", "pink", "black"]
	var with_red: Array[Dictionary] = [_ball("cue", 0.0, 0.0), _ball("red", 0.5, 0.0), _ball("blue", 1.0, 0.0)]
	check_eq(Nomination.preselect(with_red, 0, 0.0, any, RADIUS), "red", "a red when it may be declared")


func test_a_tap_declares_an_eligible_ball() -> void:
	var balls: Array[Dictionary] = [_ball("cue", 0.0, 0.0), _ball("red", 0.5, 0.0), _ball("blue", 1.0, 0.0), _ball("black", 0.0, 0.8)]
	check_eq(Nomination.tapped(balls, 2, COLOURS), "blue", "the blue")
	check_eq(Nomination.tapped(balls, 3, COLOURS), "black", "the black")
	check_eq(Nomination.tapped(balls, 1, COLOURS), "", "a red is not a colour")
	check_eq(Nomination.tapped(balls, 0, COLOURS), "", "nor the cue ball")
	check_eq(Nomination.tapped(balls, -1, COLOURS), "", "no ball tapped")
	check_eq(Nomination.tapped(balls, 9, COLOURS), "", "a place off the list")
	var none: Array[String] = []
	check_eq(Nomination.tapped(balls, 2, none), "", "nothing to declare")
	check_eq(Nomination.place_of(balls, "blue"), 2, "the blue's place")
	check_eq(Nomination.place_of(balls, "black"), 3, "the black's")
	check_eq(Nomination.place_of(balls, "red"), 1, "the first red's")
	check_eq(Nomination.place_of(balls, "pink"), -1, "the pink is off the table")
	check_eq(Nomination.place_of(balls, ""), -1, "no colour")


func test_the_ring_marks_the_declared_ball() -> void:
	var nomination := Nomination.new()
	tree.root.add_child(nomination)
	var ring := nomination.get_node("Ring") as MeshInstance3D
	check(not nomination.is_marking() and not ring.visible, "no ring at first")
	nomination.mark(Vector2(0.5, -0.2))
	check(nomination.is_marking() and ring.visible, "the ring shows")
	var at := TableSpace.core_to_godot(0.5, -0.2)
	check_near(ring.global_position.x, at.x, 1.0e-6, "at the ball, across")
	check_near(ring.global_position.z, at.z, 1.0e-6, "and along")
	check(ring.global_position.y > 0.0 and ring.global_position.y < 0.01, "lying on the cloth: %f" % ring.global_position.y)
	check_eq(ring.layers, Stage.MOVING_LAYER, "on the moving layer, out of the reflections")
	var torus := ring.mesh as TorusMesh
	# The guides' ghost-ball ring is a line one ball radius round.
	check(torus != null and torus.inner_radius > 1.5 * RADIUS, "a ring well outside the ball and the ghost ring: %f" % torus.inner_radius)
	check(torus.outer_radius - torus.inner_radius > 0.3 * RADIUS, "a broad band, not a line")
	var material := torus.material as StandardMaterial3D
	check_eq(material.shading_mode, BaseMaterial3D.SHADING_MODE_UNSHADED, "unshaded")
	var selection: Color = MainScript.SELECTION_COLOUR
	check(absf(material.albedo_color.b - selection.b) > 0.3, "not the edit selection's colour: %s" % material.albedo_color)
	nomination.mark(Vector2(-1.0, 0.3))
	check_near(ring.global_position.z, TableSpace.core_to_godot(-1.0, 0.3).z, 1.0e-6, "it follows a new declaration")
	nomination.mark(null)
	check(not nomination.is_marking() and not ring.visible, "null hides it")
	nomination.queue_free()
	await tree.process_frame


func test_the_ring_is_in_the_declared_balls_colour() -> void:
	Kit.forget_reported()
	_mark_log()
	var nomination := Nomination.new()
	tree.root.add_child(nomination)
	var ring := nomination.get_node("Ring") as MeshInstance3D
	var material := (ring.mesh as TorusMesh).material as StandardMaterial3D
	var rims: Array[MeshInstance3D] = [nomination.get_node("RimInside"), nomination.get_node("RimOutside")]
	var white := Color(1.0, 1.0, 1.0)
	var wrong := Color(1.0, 0.16, 0.12)
	for colour in COLOURS:
		nomination.mark(Vector2(0.2, 0.1), colour)
		var shown := material.albedo_color
		check_eq(shown, Nomination.COLOURS[colour], colour)
		# The guides are white on a ball that may be played, red on one that may not.
		check(_distance(shown, white) > 0.3 and _distance(shown, wrong) > 0.3, "%s reads apart from the guides: %s" % [colour, shown])
		var rimmed := colour == "black" or colour == "brown"
		for rim in rims:
			check_eq(rim.visible, rimmed, "%s: %s" % [colour, "a lighter rim" if rimmed else "no rim"])
			check_near(rim.global_position.x, ring.global_position.x, 1.0e-6, "%s: the rim with the ring" % colour)
			if rimmed:
				var edge := ((rim.mesh as TorusMesh).material as StandardMaterial3D).albedo_color
				check(edge.get_luminance() > shown.get_luminance() + 0.25, "%s: the rim is lighter: %s" % [colour, edge])
				check(_distance(edge, white) > 0.3, "%s: but not the ghost ring's white: %s" % [colour, edge])
	check((rims[0].mesh as TorusMesh).outer_radius <= (ring.mesh as TorusMesh).inner_radius
			and (rims[1].mesh as TorusMesh).inner_radius >= (ring.mesh as TorusMesh).outer_radius, "the rims edge the band")
	nomination.mark(null)
	check(not rims[0].visible and not rims[1].visible, "null hides the rims too")
	nomination.mark(Vector2(0.2, 0.1), "purple")
	nomination.mark(Vector2(0.2, 0.1), "purple")
	check(nomination.is_marking(), "a ball with no colour is still ringed")
	check_eq(_count(_errors(), "purple"), 1, "and said once in the log")
	nomination.queue_free()
	await tree.process_frame


func test_the_cue_placer_through_a_placement() -> void:
	var placer := CuePlacer.new()
	var got: Array = []
	placer.moved.connect(func(xy: Vector2) -> void: got.append(["moved", xy]))
	placer.dropped.connect(func(xy: Vector2) -> void: got.append(["dropped", xy]))
	placer.confirmed.connect(func() -> void: got.append(["confirmed"]))
	placer.move_requested.connect(func() -> void: got.append(["move_requested"]))
	# A stand-in for the D: places are kept to a box behind the baulk line.
	var nearest := func(xy: Vector2) -> Vector2:
		return Vector2(clampf(xy.x, -1.3, -1.05), clampf(xy.y, -0.25, 0.25))
	check(not placer.is_placing(), "not placing before it begins")
	check(placer.button("confirm") == null and placer.button("move") == null, "no buttons")
	placer.drag(Vector2(0.0, 0.0))
	placer.drop(Vector2(0.0, 0.0))
	check_eq(got, [], "a drag before it begins does nothing")

	placer.begin(nearest)
	check(placer.is_placing(), "placing")
	check(placer.button("confirm") != null and placer.button("move") == null, "Confirm shows, Move doesn't")
	placer.drag(Vector2(0.4, 0.1))
	placer.drag(Vector2(-1.2, 0.6))
	placer.drop(Vector2(-1.2, 0.1))
	check_eq(got, [["moved", Vector2(-1.05, 0.1)], ["moved", Vector2(-1.2, 0.25)], ["dropped", Vector2(-1.2, 0.1)]],
			"each place is the nearest legal one")
	got.clear()

	placer.button("confirm").pressed.emit()
	check_eq(got, [["confirmed"]], "Confirm")
	check(not placer.is_placing(), "no longer placing")
	check(placer.button("confirm") == null and placer.button("move") != null, "Move shows in its place")
	placer.drag(Vector2(-1.1, 0.0))
	placer.drop(Vector2(-1.1, 0.0))
	check_eq(got, [["confirmed"]], "a drag after Confirm does nothing")
	got.clear()

	placer.button("move").pressed.emit()
	check_eq(got, [["move_requested"]], "Move")
	check(placer.is_placing() and placer.button("confirm") != null and placer.button("move") == null, "back to placing")
	placer.drag(Vector2(-1.1, 0.0))
	check_eq(got, [["move_requested"], ["moved", Vector2(-1.1, 0.0)]], "and the drag moves the ball again")
	got.clear()

	placer.end()
	check(not placer.is_placing(), "ended")
	check(placer.button("confirm") == null and placer.button("move") == null, "nothing to press")
	check_eq(placer.covered_rect(), Rect2(), "covers nothing")
	placer.drag(Vector2(-1.1, 0.0))
	check_eq(got, [], "and takes no drag")
	placer.free()


func test_the_cue_placer_sits_low_and_centred_and_shrinks_to_move() -> void:
	var placer := CuePlacer.new()
	tree.root.add_child(placer)
	await tree.process_frame
	var area := Rect2(392.0, 0.0, 1128.0, 838.0)
	placer.place_in(area)
	check_eq(placer.covered_rect(), Rect2(), "covers nothing until the cue ball is in hand")
	check(not (placer.get_node("Panel") as Control).visible, "and shows nothing")
	placer.begin(func(xy: Vector2) -> Vector2: return xy)
	var placing := placer.covered_rect()
	check(area.encloses(placing), "inside the area: %s" % placing)
	# The panel lays the hint and the button out at the end of the frame.
	await tree.process_frame
	check_eq(placer.covered_rect(), placing, "and stays where begin put it")
	check_eq(placing.end.y, area.end.y, "along its bottom")
	check_near(placing.get_center().x, area.get_center().x, 1.0, "centred")
	var hint := placer.get_node("Panel").find_child("Hint", true, false) as Label
	check(hint.visible and hint.text == "Drag the cue ball in the D", "the hint")
	check(hint.get_theme_font_size("font_size") >= 22, "readable")
	var confirm := placer.button("confirm")
	check_eq(confirm.text, "Confirm", "Confirm")
	check(confirm.size.x >= 88.0 and confirm.size.y >= 88.0, "big enough to touch: %s" % confirm.size)
	check(placing.encloses(Rect2(confirm.global_position, confirm.size)), "inside the control")
	check(hint.global_position.x + hint.size.x <= confirm.global_position.x, "the hint beside it")

	confirm.pressed.emit()
	var placed := placer.covered_rect()
	await tree.process_frame
	check_eq(placer.covered_rect(), placed, "Confirm leaves it placed at once")
	var move := placer.button("move")
	check_eq(move.text, "Move", "Move")
	check(not hint.visible, "the hint has gone")
	check(move.size.x >= 88.0 and move.size.y >= 88.0, "still big enough to touch: %s" % move.size)
	check(placed.size.x < 0.5 * placing.size.x, "small: %s after %s" % [placed, placing])
	check(placed.encloses(Rect2(move.global_position, move.size)), "inside the control")
	check(area.encloses(placed) and placed.end.y == area.end.y, "still along the bottom: %s" % placed)
	# Centred, it would sit on the butt of the cue.
	check_eq(placed.end.x, area.end.x, "at the area's right end")
	check(placed.position.x > area.get_center().x + 0.25 * area.size.x, "well clear of the middle: %s" % placed)
	for id: String in ["move"]:
		check_eq(placer.button(id).focus_mode, Control.FOCUS_NONE, "%s takes no keyboard focus" % id)
	check_eq(_takes_touches(placer), [] as Array[String], "only the buttons take touches")
	move.pressed.emit()
	await tree.process_frame
	check_eq(placer.covered_rect(), placing, "Move puts the hint and Confirm back in the middle")
	placer.end()
	check(not (placer.get_node("Panel") as Control).visible, "end hides it")
	placer.queue_free()
	await tree.process_frame


func test_the_cue_placer_keeps_the_drag_when_no_legal_place_comes_back() -> void:
	Kit.forget_reported()
	_mark_log()
	var placer := CuePlacer.new()
	var got: Array = []
	placer.moved.connect(func(xy: Vector2) -> void: got.append(xy))
	placer.begin(func(_xy: Vector2) -> Variant: return null)
	placer.drag(Vector2(-1.1, 0.05))
	placer.drag(Vector2(-1.2, 0.05))
	check_eq(got, [Vector2(-1.1, 0.05), Vector2(-1.2, 0.05)], "the ball goes where it is dragged")
	check_eq(_count(_errors(), "CuePlacer"), 1, "said once in the log")
	placer.free()


func _frame() -> Dictionary:
	return JSON.parse_string(FileAccess.get_file_as_string(FRAME_PATH))


func _frame_without_free_ball() -> Dictionary:
	var frame := _frame()
	frame["free_ball"] = false
	return frame


func _match() -> Dictionary:
	return JSON.parse_string(FileAccess.get_file_as_string(MATCH_PATH))


func _ball(kind: String, x: float, y: float) -> Dictionary:
	return {"ball": kind, "index": 0, "x": x, "y": y}


# The controls under `piece`, itself included, that would take a touch and are not buttons.
func _takes_touches(piece: Control) -> Array[String]:
	var found: Array[String] = []
	var controls := piece.find_children("*", "Control", true, false)
	controls.append(piece)
	for node in controls:
		var control := node as Control
		if control.mouse_filter != Control.MOUSE_FILTER_IGNORE and not control is BaseButton:
			found.append(str(piece.get_path_to(control)))
	return found


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


func _logged(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append((parsed as Dictionary).get("data", {}))
	return found


func _errors() -> PackedStringArray:
	var messages := PackedStringArray()
	for data in _logged("app.error"):
		messages.append(str(data.get("message", "")))
	return messages


static func _distance(a: Color, b: Color) -> float:
	return Vector3(a.r - b.r, a.g - b.g, a.b - b.b).length()


func _count(messages: PackedStringArray, part: String) -> int:
	var count := 0
	for message in messages:
		if message.contains(part):
			count += 1
	return count
