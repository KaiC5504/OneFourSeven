extends TestCase
## The match's screens against the spec (11.1, 11.2): match setup, the scoreboard, the pause menu,
## the decision prompt, the referee's line and the two summaries, each on its own, shown the
## engine's Dictionaries (res://data/rules/) and pressed as the person presses them.

const FRAME_VIEW_PATH := "res://data/rules/example_frame_view.json"
const JUDGEMENT_PATH := "res://data/rules/example_judgement.json"
const MATCH_PATH := "res://data/rules/example_match.json"
const SAFE := Rect2(124.0, 0.0, 1664.0, 838.0)
const IDLE_OPTIONS := [["concede_frame", "concede_match"], ["concede_match"]]
## Every decision the engine has a name for (core/include/ofs/rules/types.hpp).
const DECISIONS := ["play", "play_again", "replace", "accept_rerack", "refuse_rerack", "play_first", "play_second",
		"claim_frame", "concede_frame", "concede_match"]

var _nodes: Array[Node] = []


# First in the file: the setup screen remembers its choices for the session, so the default only
# shows before anything here has pressed one.
func test_11_1_who_breaks_first_is_random_by_default() -> void:
	# spec 11.1: who breaks first: random, you, or the computer (default random)
	var screen := _setup()
	check_eq(screen.config().get("first_breaker"), "random", "random until the person chooses")
	var sent: Array[Dictionary] = []
	screen.start_pressed.connect(func(config: Dictionary) -> void: sent.append(config))
	_press(screen.control("start"), "Start")
	check(sent.size() == 1 and sent[0].get("first_breaker") == "random", "Start with nothing chosen sends random: %s" % [sent])
	_free_all()


func test_11_1_match_setup_offers_exactly_best_of_1_3_5_7_9_and_11() -> void:
	# spec 11.1: match length: best of 1, 3, 5, 7, 9 or 11 frames
	var lengths := [1, 3, 5, 7, 9, 11]
	check_eq(_plain(MatchSetupScreen.LENGTHS), lengths, "the lengths")
	var screen := _setup()
	for n: int in range(0, 16):
		var control := screen.control("best_of:%d" % n)
		if n in lengths:
			check(control != null and control.is_visible_in_tree(), "best of %d is offered" % n)
		else:
			check(control == null, "best of %d is not offered" % n)
	for n: int in [11, 1, 7, 3, 9, 5]:
		_press(screen.control("best_of:%d" % n), "best of %d" % n)
		check_eq(screen.config().get("best_of"), n, "best of %d is what Start would send" % n)
	_free_all()


func test_11_1_match_setup_offers_random_you_and_the_computer_to_break() -> void:
	# spec 11.1: who breaks first: random, you, or the computer
	var breakers := ["random", "you", "computer"]
	check_eq(_plain(MatchSetupScreen.BREAKERS), breakers, "the breakers")
	var screen := _setup()
	for who: String in breakers:
		var control := screen.control("first_breaker:%s" % who)
		check(control != null and control.is_visible_in_tree(), "%s is offered" % who)
	for who: String in ["opponent", "winner", "loser", "alternate"]:
		check(screen.control("first_breaker:%s" % who) == null, "%s is not offered" % who)
	for who: String in ["computer", "you", "random", "you"]:
		_press(screen.control("first_breaker:%s" % who), who)
		check_eq(screen.config().get("first_breaker"), who, "%s is what Start would send" % who)
	_free_all()


func test_11_1_start_sends_the_length_and_the_breaker_chosen() -> void:
	# spec 11.1: match setup
	var screen := _setup()
	var sent: Array[Dictionary] = []
	var backs := [0]
	screen.start_pressed.connect(func(config: Dictionary) -> void: sent.append(config))
	screen.back_pressed.connect(func() -> void: backs[0] += 1)
	_press(screen.control("best_of:7"), "best of 7")
	_press(screen.control("first_breaker:computer"), "the computer breaks")
	check_eq(sent.size(), 0, "choosing starts nothing")
	_press(screen.control("start"), "Start")
	check_eq(sent.size(), 1, "Start starts once")
	if sent.size() == 1:
		check_eq(sent[0].get("best_of"), 7, "with the length chosen")
		check_eq(sent[0].get("first_breaker"), "computer", "and the breaker chosen")
	check_eq(backs[0], 0, "Start is not Back")
	_press(screen.control("back"), "Back")
	check(backs[0] == 1 and sent.size() == 1, "Back goes back and starts nothing")
	_free_all()


func test_11_1_the_scoreboard_shows_both_scores_frames_break_remaining_and_the_ball_on() -> void:
	# spec 11.1: a minimal scoreboard: both scores, frames, current break, points remaining, the ball on
	var board := _scoreboard()
	var frame := _fixture(FRAME_VIEW_PATH)
	var match_view := _fixture(MATCH_PATH)
	board.show_frame(frame, match_view, 0)
	_check_texts(board.texts(), {"you": 24, "opponent": 31, "frames_you": 0, "frames_opponent": 1, "best_of": 3,
			"break": 0, "remaining": 43, "required": "", "on": "Free ball", "turn": "you"}, "the fixture, as player 0")
	board.show_frame(frame, match_view, 1)
	_check_texts(board.texts(), {"you": 31, "opponent": 24, "frames_you": 1, "frames_opponent": 0, "best_of": 3,
			"break": 0, "remaining": 43, "required": "", "on": "Free ball", "turn": "opponent"}, "the fixture, as player 1")
	_free_all()


func test_11_1_the_scoreboard_follows_the_break_and_the_points_remaining() -> void:
	# spec 11.1: current break, points remaining
	var board := _scoreboard()
	var match_view := _fixture(MATCH_PATH)
	board.show_frame(_frame({"break_points": 23, "scores": [47, 31]}, {"points_remaining": 27}), match_view, 0)
	_check_texts(board.texts(), {"you": 47, "opponent": 31, "break": 23, "remaining": 27, "turn": "you"}, "the person on a break")
	board.show_frame(_frame({"striker": 1, "break_points": 12, "scores": [24, 43]}, {"points_remaining": 35}), match_view, 0)
	_check_texts(board.texts(), {"you": 24, "opponent": 43, "break": 12, "remaining": 35, "turn": "opponent"},
			"the opponent on a break")
	board.show_frame(_frame({"striker": 1, "break_points": 0, "scores": [24, 43]}, {"points_remaining": 35}), match_view, 0)
	check_eq(board.texts().get("break"), 0, "no break once it has ended")
	_free_all()


func test_11_1_the_scoreboard_names_the_ball_on() -> void:
	# spec 11.1: ... and the ball on
	var board := _scoreboard()
	var match_view := _fixture(MATCH_PATH)
	var cases := [
		[{"kind": "red"}, false, "Red"],
		[{"kind": "any_colour"}, false, "Colour"],
		[{"kind": "colour", "colour": "yellow"}, false, "Yellow"],
		[{"kind": "colour", "colour": "green"}, false, "Green"],
		[{"kind": "colour", "colour": "brown"}, false, "Brown"],
		[{"kind": "colour", "colour": "blue"}, false, "Blue"],
		[{"kind": "colour", "colour": "pink"}, false, "Pink"],
		[{"kind": "colour", "colour": "black"}, false, "Black"],
		[{"kind": "red"}, true, "Free ball"],
		[{"kind": "colour", "colour": "green"}, true, "Free ball"],
	]
	for case: Array in cases:
		var frame := _frame({"on": case[0], "free_ball": case[1]})
		var what := "%s%s" % [case[0], " with a free ball" if case[1] else ""]
		check_eq(Scoreboard.on_text(frame), case[2], "%s in words" % what)
		board.show_frame(frame, match_view, 0)
		check_eq(board.texts().get("on"), case[2], "%s on the scoreboard" % what)
	_free_all()


func test_11_1_snookers_required_shows_only_when_relevant() -> void:
	# spec 11.1: "snookers required" when relevant
	var board := _scoreboard()
	var match_view := _fixture(MATCH_PATH)
	board.show_frame(_frame(), match_view, 0)
	check_eq(board.texts().get("required"), "", "nothing while nobody needs penalty points")
	board.show_frame(_frame({"scores": [20, 63]}, {"points_remaining": 35, "required": [8, 0], "leader": 1}), match_view, 0)
	var mine := str(board.texts().get("required", ""))
	check(mine.to_lower().contains("snooker"), "said when the person needs them: '%s'" % mine)
	board.show_frame(_frame({"scores": [63, 20]}, {"points_remaining": 35, "required": [0, 8], "leader": 0}), match_view, 0)
	var theirs := str(board.texts().get("required", ""))
	check(theirs.to_lower().contains("snooker"), "said when the opponent needs them: '%s'" % theirs)
	board.show_frame(_frame(), match_view, 0)
	check_eq(board.texts().get("required"), "", "gone again once nobody does")
	_free_all()


func test_11_1_the_scoreboard_carries_the_pause_button() -> void:
	# spec 11.1: a pause button
	var board := _scoreboard()
	board.show_frame(_frame(), _fixture(MATCH_PATH), 0)
	var presses := [0]
	board.pause_pressed.connect(func() -> void: presses[0] += 1)
	var button := board.pause_button()
	check(button != null and button.is_visible_in_tree(), "the pause button shows")
	_press(button, "Pause")
	check_eq(presses[0], 1, "and pauses")
	# Minimal: a drag on the table under it must still reach the table.
	check_eq(board.mouse_filter, Control.MOUSE_FILTER_IGNORE, "the scoreboard itself takes no touches")
	_free_all()


func test_11_1_the_pause_menu_has_resume_concede_frame_concede_match_settings_and_quit() -> void:
	# spec 11.1: a pause button with Resume, Concede frame, Concede match, Settings and Quit
	var menu := _pause()
	menu.open(true)
	check(menu.visible, "the menu opens")
	var words := {"resume": ["resume"], "concede_frame": ["concede", "frame"], "concede_match": ["concede", "match"],
			"settings": ["settings"], "quit": ["quit"]}
	for id: String in words:
		var button := menu.button(id)
		check(button != null and button.is_visible_in_tree(), "%s is on the menu" % id)
		if button == null:
			continue
		check(not button.disabled, "%s can be pressed" % id)
		for word: String in words[id]:
			check(button.text.to_lower().contains(word), "%s says so: '%s'" % [id, button.text])
	check_eq(menu.confirming(), "", "nothing waits to be confirmed")
	check(menu.button("confirm") == null and menu.button("cancel") == null, "and no confirmation shows")
	menu.close()
	check(not menu.visible, "the menu closes")
	_free_all()


func test_11_1_resume_and_settings_act_at_once() -> void:
	# spec 11.1: Resume, Settings
	var menu := _pause()
	menu.open(true)
	var got: Array[String] = []
	menu.resume_pressed.connect(func() -> void: got.append("resume"))
	menu.settings_pressed.connect(func() -> void: got.append("settings"))
	_press(menu.button("settings"), "Settings")
	check_eq(got, ["settings"], "Settings opens the settings")
	_press(menu.button("resume"), "Resume")
	check_eq(got, ["settings", "resume"], "Resume resumes")
	check_eq(menu.confirming(), "", "neither asks to confirm")
	_free_all()


func test_11_2_quitting_asks_to_confirm() -> void:
	# spec 11.2: quitting mid-match asks to confirm
	var menu := _pause()
	menu.open(true)
	var quits := [0]
	menu.quit_confirmed.connect(func() -> void: quits[0] += 1)
	_press(menu.button("quit"), "Quit")
	check_eq(quits[0], 0, "Quit alone does not quit")
	check_eq(menu.confirming(), "quit", "it asks first")
	check(menu.button("confirm") != null and menu.button("cancel") != null, "with a way on and a way back")
	_press(menu.button("cancel"), "Cancel")
	check_eq(quits[0], 0, "Cancel does not quit")
	check_eq(menu.confirming(), "", "Cancel returns to the menu")
	check(menu.visible and menu.button("resume") != null, "which is still open")
	check(menu.button("confirm") == null, "with no confirmation left showing")
	_press(menu.button("quit"), "Quit again")
	_press(menu.button("confirm"), "the confirming press")
	check_eq(quits[0], 1, "the confirming press quits, once")
	# A question left open must not be the first thing the next pause shows.
	menu.close()
	menu.open(true)
	_press(menu.button("quit"), "Quit")
	menu.close()
	menu.open(true)
	check_eq(menu.confirming(), "", "a menu opened again asks nothing")
	check_eq(quits[0], 1, "and nothing more was quit")
	_free_all()


func test_11_1_conceding_asks_to_confirm_as_well() -> void:
	# spec 11.1: Concede frame, Concede match; a concession cannot be taken back
	for id: String in ["concede_frame", "concede_match"]:
		var menu := _pause()
		menu.open(true)
		var got: Array[String] = []
		menu.concede_frame_confirmed.connect(func() -> void: got.append("concede_frame"))
		menu.concede_match_confirmed.connect(func() -> void: got.append("concede_match"))
		menu.quit_confirmed.connect(func() -> void: got.append("quit"))
		_press(menu.button(id), id)
		check_eq(got, [], "%s: nothing before the confirming press" % id)
		check_eq(menu.confirming(), id, "%s: it asks first" % id)
		_press(menu.button("cancel"), "Cancel")
		check_eq(got, [], "%s: Cancel concedes nothing" % id)
		check_eq(menu.confirming(), "", "%s: Cancel returns to the menu" % id)
		_press(menu.button(id), id)
		_press(menu.button("confirm"), "the confirming press")
		check_eq(got, [id], "%s: the confirming press concedes, once, and nothing else" % id)
	_free_all()


func test_11_1_concede_frame_is_offered_only_when_the_engine_allows_it() -> void:
	# spec 9: conceding a frame; the engine says when
	var menu := _pause()
	menu.open(false)
	var button := menu.button("concede_frame")
	check(button != null and button.disabled, "Concede frame is off when the engine does not offer it")
	var other := menu.button("concede_match")
	check(other != null and not other.disabled, "Concede match stays on")
	menu.close()
	menu.open(true)
	button = menu.button("concede_frame")
	check(button != null and not button.disabled, "and Concede frame is on when it does")
	_free_all()


func test_11_1_the_foul_prompt_offers_exactly_the_decisions_given() -> void:
	# spec 11.1: foul decision prompts; spec 9: play from where the balls lie or have the offender play again
	var strip := _strip()
	var frame := _fixture(FRAME_VIEW_PATH)
	var chosen: Array[String] = []
	strip.chosen.connect(func(kind: String) -> void: chosen.append(kind))
	var cases := [["play", "play_again"], ["play", "play_again", "replace"], ["accept_rerack", "refuse_rerack"],
			["play_first", "play_second"]]
	for options: Array in cases:
		strip.offer(_typed(options), frame)
		check(strip.is_offering(), "%s: the prompt shows" % [options])
		check(strip.prompt() != "", "%s: it says what is being asked" % [options])
		for kind: String in DECISIONS:
			var button := strip.button(kind)
			if kind in options:
				check(button != null and button.text != "" and button.text == ChoiceStrip.label_for(kind, frame),
						"%s is offered in words" % kind)
			else:
				check(button == null, "%s is not offered among %s" % [kind, options])
		var pick: String = options[-1]
		chosen.clear()
		_press(strip.button(pick), pick)
		check_eq(chosen, [pick], "%s: pressing %s chooses it and nothing else" % [options, pick])
	strip.dismiss()
	check(not strip.is_offering(), "dismissed, nothing is asked")
	check_eq(strip.prompt(), "", "nothing is said")
	check(strip.button("play") == null, "and no button is left")
	_free_all()


func test_11_1_the_prompts_buttons_say_what_they_do() -> void:
	# spec 9: after a foul, the free ball, the miss rule's replacing, the re-rack, the re-spotted black
	var plain := _frame()
	var words := {"play": "Play", "play_again": "Put them back in", "replace": "Replace the balls",
			"accept_rerack": "Re-rack", "refuse_rerack": "Play on", "play_first": "Play first",
			"play_second": "Opponent first", "claim_frame": "Claim the frame"}
	for kind: String in words:
		check_eq(ChoiceStrip.label_for(kind, plain), words[kind], kind)
	check_eq(ChoiceStrip.label_for("play", _fixture(FRAME_VIEW_PATH)), "Take the free ball", "play when a free ball was called")
	check_eq(ChoiceStrip.label_for("no_such_decision", plain), "", "an unknown decision has no words")
	_free_all()


func test_11_2_the_prompt_shows_only_when_asked_and_sits_low() -> void:
	# spec 11.2: no pop-ups beyond the rule decisions the game requires
	var strip := _strip()
	check(not strip.is_offering(), "nothing is asked until a decision is needed")
	check(not strip.covered_rect().has_area(), "and nothing is covered")
	strip.offer(_typed(["play", "play_again", "replace"]), _fixture(FRAME_VIEW_PATH))
	await tree.process_frame
	await tree.process_frame
	var rect := strip.covered_rect()
	check(rect.has_area(), "an offer covers some of the screen")
	check(SAFE.grow(0.5).encloses(rect), "inside the safe area: %s" % rect)
	check(rect.position.y >= SAFE.get_center().y, "and low, clear of the middle of the table: %s" % rect)
	strip.dismiss()
	check(not strip.covered_rect().has_area(), "dismissed, it covers nothing")
	_free_all()


func test_11_2_the_referees_calls_are_a_line_not_a_pop_up() -> void:
	# spec 11.2: no interruptions
	var line := RefereeLine.new()
	tree.root.add_child(line)
	_nodes.append(line)
	line.place_in(SAFE)
	check_eq(line.text(), "", "nothing before a call")
	var calls: Array = _fixture(JUDGEMENT_PATH).get("calls", [])
	line.announce(calls, 0)
	var said := line.text()
	check(said.contains("FOUL AND A MISS") and said.contains("FREE BALL"), "the calls are said in words: '%s'" % said)
	check(not said.contains("\n"), "on one line")
	if line.has_method("line_for"):
		check_eq(said, line.call("line_for", calls, 0), "the line is what line_for makes of the calls")
	else:
		check(false, "RefereeLine.line_for(calls, person) is missing")
	check_eq(line.mouse_filter, Control.MOUSE_FILTER_IGNORE, "it takes no touches")
	check_eq(line.find_children("*", "BaseButton", true, false).size(), 0, "and has nothing to dismiss")
	line.announce([], 0)
	check_eq(line.text(), said, "no calls change nothing")
	line.clear()
	check_eq(line.text(), "", "cleared, it says nothing")
	_free_all()


func test_11_1_the_frame_summary_shows_the_score_highest_break_and_pot_success() -> void:
	# spec 11.1: after each frame, a short summary (frame score, highest break, pot success)
	var card := _frame_summary()
	var match_view := _fixture(MATCH_PATH)
	var summary := _first_frame(match_view)
	card.show_summary(summary, match_view, 0)
	check(card.visible, "the summary shows")
	var mine := card.texts()
	_check_texts(mine, {"score_you": 41, "score_opponent": 63, "break_you": 16, "break_opponent": 31,
			"pots_you": "11 of 17", "pots_opponent": "16 of 20", "frames_you": 0, "frames_opponent": 1,
			"reason": "On the black"}, "the fixture's first frame, as player 0")
	check(str(mine.get("title", "")) != "", "it says how the frame went")
	card.show_summary(summary, match_view, 1)
	var theirs := card.texts()
	_check_texts(theirs, {"score_you": 63, "score_opponent": 41, "break_you": 31, "break_opponent": 16,
			"pots_you": "16 of 20", "pots_opponent": "11 of 17", "frames_you": 1, "frames_opponent": 0,
			"reason": "On the black"}, "the same frame, as player 1")
	check(str(theirs.get("title", "")) != "" and theirs.get("title") != mine.get("title"), "winner and loser are told differently")
	_free_all()


func test_11_1_pot_success_is_pots_of_attempts() -> void:
	# spec 11.1: pot success; the lead's ruling: pots of pot attempts, "0 of 0" with none
	var card := _frame_summary()
	var match_view := _fixture(MATCH_PATH)
	var summary := _first_frame(match_view)
	var stats: Array = summary.get("stats", [{}, {}])
	(stats[0] as Dictionary).merge({"shots": 9, "pots": 0, "pot_attempts": 0}, true)
	(stats[1] as Dictionary).merge({"shots": 30, "pots": 7, "pot_attempts": 12}, true)
	card.show_summary(summary, match_view, 0)
	_check_texts(card.texts(), {"pots_you": "0 of 0", "pots_opponent": "7 of 12"}, "no attempts and some")
	_free_all()


func test_11_1_the_frame_summary_says_why_the_frame_ended_and_goes_on_by_one_button() -> void:
	# spec 11.1: a short summary, then the next frame
	var words := {"black": "On the black", "respotted_black": "On the re-spotted black", "claimed": "Frame claimed",
			"awarded": "Frame claimed", "conceded": "Conceded", "three_misses": "Three misses",
			"match_conceded": "Match conceded"}
	var card := _frame_summary()
	var match_view := _fixture(MATCH_PATH)
	for reason: String in words:
		check_eq(FrameSummary.reason_text(reason), words[reason], reason)
		var summary := _first_frame(match_view)
		summary["reason"] = reason
		card.show_summary(summary, match_view, 0)
		check_eq(card.texts().get("reason"), words[reason], "%s on the card" % reason)
	var presses := [0]
	card.continue_pressed.connect(func() -> void: presses[0] += 1)
	var button := card.button("continue")
	check(button != null and button.is_visible_in_tree(), "one button on")
	_press(button, "Continue")
	check_eq(presses[0], 1, "it goes on to the next frame")
	card.close()
	check(not card.visible, "and the card closes")
	_free_all()


func test_11_1_the_match_summary_shows_the_result_and_the_frames() -> void:
	# spec 11.1: at the end, a match summary
	var card := MatchSummary.new()
	card.auto_place = false
	tree.root.add_child(card)
	_nodes.append(card)
	card.place_in(SAFE)
	var finished := _finished_match()
	card.show_summary(finished, 0)
	check(card.visible, "the summary shows")
	var won := card.texts()
	_check_texts(won, {"title": "You won", "frames_you": 2, "frames_opponent": 1, "break_you": 52, "break_opponent": 31},
			"the winner's card")
	check_eq(_list(won.get("frames")), ["41-63", "70-25", "58-40"], "each frame's score, the person's first")
	card.show_summary(finished, 1)
	var lost := card.texts()
	_check_texts(lost, {"title": "You lost", "frames_you": 1, "frames_opponent": 2, "break_you": 31, "break_opponent": 52},
			"the loser's card")
	check_eq(_list(lost.get("frames")), ["63-41", "25-70", "40-58"], "each frame's score, the person's first")
	var presses := [0]
	card.home_pressed.connect(func() -> void: presses[0] += 1)
	var button := card.button("home")
	check(button != null and button.is_visible_in_tree(), "one button back to Home")
	_press(button, "Home")
	check_eq(presses[0], 1, "it goes Home")
	card.close()
	check(not card.visible, "and the card closes")
	_free_all()


func test_11_1_home_leads_to_a_match_the_free_table_and_settings() -> void:
	# spec 11.1: Home: Play vs Computer, Training, Stats, Settings (Stats come in M5; Training is
	# the free table until M5)
	var home := HomeScreen.new()
	home.auto_place = false
	tree.root.add_child(home)
	_nodes.append(home)
	home.place_in(SAFE)
	home.open()
	check(home.visible, "Home opens")
	var got: Array[String] = []
	home.play_pressed.connect(func() -> void: got.append("play"))
	home.free_table_pressed.connect(func() -> void: got.append("free_table"))
	home.settings_pressed.connect(func() -> void: got.append("settings"))
	for id: String in ["play", "free_table", "settings"]:
		var button := home.button(id)
		check(button != null and button.is_visible_in_tree() and button.text != "", "%s is on Home" % id)
		_press(button, id)
	check_eq(got, ["play", "free_table", "settings"], "each leads where it says")
	home.close()
	check(not home.visible, "Home closes")
	_free_all()


func _setup() -> MatchSetupScreen:
	var screen := MatchSetupScreen.new()
	screen.auto_place = false
	tree.root.add_child(screen)
	_nodes.append(screen)
	screen.place_in(SAFE)
	screen.open()
	return screen


func _scoreboard() -> Scoreboard:
	var board := Scoreboard.new()
	board.auto_place = false
	tree.root.add_child(board)
	_nodes.append(board)
	board.place_in(SAFE)
	return board


func _pause() -> PauseMenu:
	var menu := PauseMenu.new()
	menu.auto_place = false
	tree.root.add_child(menu)
	_nodes.append(menu)
	menu.place_in(SAFE)
	return menu


func _strip() -> ChoiceStrip:
	var strip := ChoiceStrip.new()
	tree.root.add_child(strip)
	_nodes.append(strip)
	strip.place_in(SAFE)
	return strip


func _frame_summary() -> FrameSummary:
	var card := FrameSummary.new()
	card.auto_place = false
	tree.root.add_child(card)
	_nodes.append(card)
	card.place_in(SAFE)
	return card


## Each key of `expected` as the screen says it.
func _check_texts(said: Dictionary, expected: Dictionary, what: String) -> void:
	for key: String in expected:
		check_eq(said.get(key), expected[key], "%s: %s" % [what, key])


## A press as the person makes it, whether the screen listens to pressed or toggled.
func _press(control: Control, what: String) -> void:
	check(control != null, "%s: there is such a control" % what)
	if control == null:
		return
	var button := control as BaseButton
	check(button != null, "%s is a button" % what)
	if button == null:
		return
	check(not button.disabled, "%s can be pressed" % what)
	if button.toggle_mode:
		button.button_pressed = true
	button.pressed.emit()


## The fixture's frame in ordinary play: the person to strike, red on, nothing pending, nobody
## needing snookers. `changes` replace frame keys and `view` keys of its "view".
func _frame(changes: Dictionary = {}, view: Dictionary = {}) -> Dictionary:
	var frame := _fixture(FRAME_VIEW_PATH)
	frame.merge({"striker": 0, "pending": "none", "free_ball": false, "last_miss": false}, true)
	frame.merge(changes, true)
	var shown: Dictionary = frame.get("view", {})
	shown.merge({"nominations": [], "options": IDLE_OPTIONS.duplicate(true)}, true)
	shown.merge(view, true)
	frame["view"] = shown
	return frame


## The fixture's finished frame: 41-63 to player 1 on the black, breaks of 16 and 31, 11 pots of
## 17 attempts and 16 of 20.
func _first_frame(match_view: Dictionary) -> Dictionary:
	var history: Array = match_view.get("history", [{}])
	return (history[0] as Dictionary).duplicate(true)


## The fixture's best of three played out: 41-63, 70-25, 58-40 to player 0, with highest breaks of
## 16, 52 and 27 against 31, 12 and 22.
func _finished_match() -> Dictionary:
	var finished := _fixture(MATCH_PATH)
	var first := _first_frame(finished)
	var second := _later_frame(first, 2, [70, 25], [52, 12])
	var third := _later_frame(first, 3, [58, 40], [27, 22])
	finished.merge({"frames": [2, 1], "frame_number": 3, "history": [first, second, third], "over": true, "winner": 0}, true)
	var frame: Dictionary = finished.get("frame", {})
	frame.merge({"over": true, "winner": 0, "end_reason": "black", "scores": [58, 40]}, true)
	return finished


func _later_frame(first: Dictionary, number: int, scores: Array, breaks: Array) -> Dictionary:
	var summary := first.duplicate(true)
	summary.merge({"number": number, "breaker": (number + 1) % 2, "winner": 0, "scores": scores}, true)
	var stats: Array = summary.get("stats", [{}, {}])
	for player: int in 2:
		(stats[player] as Dictionary)["highest_break"] = breaks[player]
	return summary


func _typed(names: Array) -> Array[String]:
	var out: Array[String] = []
	out.assign(names)
	return out


static func _plain(items: Array) -> Array:
	var out: Array = []
	out.append_array(items)
	return out


## `value` as a plain list; empty when it is not a list at all.
static func _list(value: Variant) -> Array:
	return _plain(value) if value is Array else []


func _free_all() -> void:
	for node: Node in _nodes:
		if is_instance_valid(node):
			node.free()
	_nodes.clear()


func _fixture(path: String) -> Dictionary:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	check(parsed is Dictionary, "%s is there (scripts/sync_data.py copies it)" % path)
	if not parsed is Dictionary:
		return {}
	return _whole(parsed)


## JSON.parse_string makes every number a float; the binding hands whole numbers over as ints.
static func _whole(value: Variant, key: String = "") -> Variant:
	if value is Dictionary:
		var out := {}
		for k: String in (value as Dictionary):
			out[k] = _whole((value as Dictionary)[k], k)
		return out
	if value is Array:
		var items: Array = []
		for item: Variant in (value as Array):
			items.append(_whole(item, key))
		return items
	if value is float and key != "x" and key != "y":
		return int(value)
	return value
