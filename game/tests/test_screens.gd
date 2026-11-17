extends TestCase
## The match screens (spec 11.1): Home, match setup, the pause menu, the two summaries and the
## exhibition's. Every button emits its signal, setup offers the levels and sends what was chosen,
## pause asks before conceding or quitting, the summaries say what the example match holds from
## either chair and name the opponent, each screen sits inside the safe area it is given, and a
## malformed Dictionary shows what is there and is logged once.

const HudKit := preload("res://scripts/match/hud/hud_kit.gd")
const MATCH_PATH := "res://data/rules/example_match.json"
const END_REASONS := {
	"black": "On the black",
	"respotted_black": "On the re-spotted black",
	"claimed": "Frame claimed",
	"awarded": "Frame awarded",
	"conceded": "Frame conceded",
	"three_misses": "Three misses: frame awarded",
	"match_conceded": "Match conceded",
}
## The phone's canvas with the Dynamic Island's insets, and a narrow canvas.
const SAFE_RECTS: Array[Rect2] = [Rect2(120.0, 0.0, 1672.0, 880.0), Rect2(0.0, 0.0, 1200.0, 880.0)]
## 28 units is 30 px in its capitals on the phone (1.5 px a unit).
const MIN_FONT_SIZE := 28
## The whole canvas, and the phone's safe area as the HUD's tests take it.
const HOME_RECTS: Array[Rect2] = [Rect2(0.0, 0.0, 1912.0, 880.0), Rect2(124.0, 0.0, 1664.0, 838.0)]
const TWO_LEVELS := [
	{"id": "beginner", "name": "Beginner", "tier": 1, "maximum": false},
	{"id": "maximum", "name": "The 147", "tier": 6, "maximum": true},
]
## Stage 2's ladder (spec 10.1) and the 147: the longest names the row must hold.
const SIX_LEVELS := [
	{"id": "beginner", "name": "Beginner", "tier": 1, "maximum": false},
	{"id": "club", "name": "Club", "tier": 2, "maximum": false},
	{"id": "amateur", "name": "Amateur", "tier": 3, "maximum": false},
	{"id": "pro", "name": "Pro", "tier": 4, "maximum": false},
	{"id": "world_class", "name": "World class", "tier": 5, "maximum": false},
	{"id": "maximum", "name": "The 147", "tier": 6, "maximum": true},
]

var _log_start := 0


func test_every_screen_starts_hidden_and_covers_the_canvas() -> void:
	var screens: Array[Control] = [HomeScreen.new(), MatchSetupScreen.new(), PauseMenu.new(), FrameSummary.new(),
			MatchSummary.new(), ExhibitionSummary.new()]
	for screen in screens:
		var screen_name: String = screen.get_script().get_global_name()
		check(not screen.visible, "%s is hidden until opened" % screen_name)
		check_eq([screen.anchor_left, screen.anchor_top, screen.anchor_right, screen.anchor_bottom], [0.0, 0.0, 1.0, 1.0],
				"%s covers the canvas" % screen_name)
		var backdrop := screen.get_node("Backdrop") as Control
		check_eq([backdrop.anchor_right, backdrop.anchor_bottom], [1.0, 1.0], "%s: its backdrop covers the canvas" % screen_name)
		check_eq(backdrop.mouse_filter, Control.MOUSE_FILTER_STOP, "%s takes every touch" % screen_name)
		screen.free()


func test_home_buttons_emit_their_signals() -> void:
	var home := HomeScreen.new()
	var got: Array = []
	home.play_pressed.connect(func() -> void: got.append("play"))
	home.free_table_pressed.connect(func() -> void: got.append("free_table"))
	home.watch_pressed.connect(func() -> void: got.append("watch"))
	home.settings_pressed.connect(func() -> void: got.append("settings"))
	for id: String in ["play", "free_table", "watch", "settings"]:
		home.button(id).pressed.emit()
	check_eq(got, ["play", "free_table", "watch", "settings"], "signals")
	check_eq([home.button("play").text, home.button("free_table").text, home.button("watch").text,
			home.button("settings").text], ["Play", "Free table", "Watch a 147", "Settings"], "the buttons' words")
	check_eq((home.button("watch").get_theme_stylebox("normal") as StyleBoxFlat).bg_color, HomeScreen.BUTTON_FILL,
			"Watch a 147 is not the primary button")
	check(_says(home, "One Four Seven"), "the game's name")
	check(home.button("stats") == null, "no Stats button before M5")
	home.open()
	check(home.visible, "open shows it")
	home.close()
	check(not home.visible, "close hides it")
	home.free()


func test_setup_starts_at_best_of_three_and_a_random_break() -> void:
	var setup := MatchSetupScreen.new()
	check_eq(setup.config(), {"best_of": 3, "first_breaker": "random"}, "defaults")
	check((setup.control("best_of:3") as Button).button_pressed, "best of 3 shows chosen")
	check((setup.control("first_breaker:random") as Button).button_pressed, "random shows chosen")
	check(setup.control("level_beginner") == null, "no level until the engine's list comes")
	setup.free()


func test_setup_sends_each_choice() -> void:
	var setup := MatchSetupScreen.new()
	var sent: Array[Dictionary] = []
	var backs := [0]
	setup.start_pressed.connect(func(config: Dictionary) -> void: sent.append(config))
	setup.back_pressed.connect(func() -> void: backs[0] += 1)
	for n in MatchSetupScreen.LENGTHS:
		(setup.control("best_of:%d" % n) as Button).button_pressed = true
		check_eq(setup.config(), {"best_of": n, "first_breaker": "random"}, "best of %d" % n)
		for other in MatchSetupScreen.LENGTHS:
			check_eq((setup.control("best_of:%d" % other) as Button).button_pressed, other == n,
					"best of %d: only it shows chosen (%d)" % [n, other])
	for who in MatchSetupScreen.BREAKERS:
		(setup.control("first_breaker:" + who) as Button).button_pressed = true
		check_eq(setup.config(), {"best_of": 11, "first_breaker": who}, "break: %s" % who)
	(setup.control("best_of:5") as Button).button_pressed = true
	(setup.control("first_breaker:you") as Button).button_pressed = true
	check_eq(sent, [], "choosing sends nothing")
	(setup.control("start") as Button).pressed.emit()
	check_eq(sent, [{"best_of": 5, "first_breaker": "you"}], "Start sends the choices")
	check_eq(typeof(sent[0]["best_of"]), TYPE_INT, "best_of is an int")
	(setup.control("back") as Button).pressed.emit()
	check_eq(backs[0], 1, "Back")
	check_eq(sent.size(), 1, "Back starts nothing")
	setup.free()


func test_setup_remembers_the_last_choice() -> void:
	var setup := MatchSetupScreen.new()
	setup.open()
	(setup.control("best_of:7") as Button).button_pressed = true
	(setup.control("first_breaker:computer") as Button).button_pressed = true
	setup.close()
	check(not setup.visible, "closed")
	setup.open()
	check(setup.visible, "open again")
	check_eq(setup.config(), {"best_of": 7, "first_breaker": "computer"}, "the choices are still there")
	check((setup.control("best_of:7") as Button).button_pressed, "and still show")
	setup.free()


func test_setup_offers_the_levels_with_the_first_chosen() -> void:
	for levels: Array in [TWO_LEVELS, SIX_LEVELS]:
		var setup := MatchSetupScreen.new()
		setup.set_levels(levels)
		var where := "%d levels" % levels.size()
		check(_shows(setup, setup.find_child("Levels", true, false) as Control), "%s: the row shows" % where)
		check(_says(setup, "Opponent"), "%s: it is titled" % where)
		for level: Dictionary in levels:
			var b := setup.control("level_" + String(level["id"])) as Button
			check(b != null and b.text == level["name"], "%s: %s has its button" % [where, level["id"]])
			if b != null:
				check_eq(b.button_pressed, level == levels[0], "%s: only the first shows chosen (%s)" % [where, level["id"]])
		check_eq(setup.config(), {"level": levels[0]["id"], "best_of": 3, "first_breaker": "random"}, "%s: config" % where)
		var last: String = levels.back()["id"]
		(setup.control("level_" + last) as Button).button_pressed = true
		check_eq(setup.config()["level"], last, "%s: choosing one" % where)
		check(not (setup.control("level_" + String(levels[0]["id"])) as Button).button_pressed, "%s: the first lets go" % where)
		var sent: Array[Dictionary] = []
		setup.start_pressed.connect(func(config: Dictionary) -> void: sent.append(config))
		(setup.control("start") as Button).pressed.emit()
		check_eq(sent, [{"level": last, "best_of": 3, "first_breaker": "random"}], "%s: Start sends the level" % where)
		setup.free()


func test_setup_keeps_the_level_chosen() -> void:
	var setup := MatchSetupScreen.new()
	setup.set_levels(TWO_LEVELS)
	setup.open()
	(setup.control("level_maximum") as Button).button_pressed = true
	setup.close()
	setup.open()
	check_eq(setup.config()["level"], "maximum", "kept over close and open")
	check((setup.control("level_maximum") as Button).button_pressed, "and still shows")
	setup.set_levels(SIX_LEVELS)
	check_eq(setup.config()["level"], "maximum", "kept when the list comes again")
	check((setup.control("level_maximum") as Button).button_pressed, "and shows chosen in the new row")
	check(not (setup.control("level_beginner") as Button).button_pressed, "the first not")
	setup.set_levels([TWO_LEVELS[0]])
	check_eq(setup.config()["level"], "beginner", "a level no longer listed gives way to the first")
	check(setup.control("level_maximum") == null, "its button goes")
	setup.free()


func test_setup_with_no_levels_hides_the_row() -> void:
	var setup := MatchSetupScreen.new()
	setup.set_levels(TWO_LEVELS)
	setup.set_levels([])
	check(not _shows(setup, setup.find_child("Levels", true, false) as Control), "the row is hidden")
	check(not _says(setup, "Opponent"), "nothing says Opponent")
	check_eq(setup.config(), {"best_of": 3, "first_breaker": "random"}, "config carries no level")
	check(setup.control("level_beginner") == null and setup.control("level_maximum") == null, "no level buttons")
	setup.free()


func test_setup_with_six_levels_keeps_every_name_on_the_card() -> void:
	var setup := MatchSetupScreen.new()
	setup.auto_place = false
	tree.root.add_child(setup)
	setup.set_levels(SIX_LEVELS)
	setup.open()
	await tree.process_frame
	for safe in SAFE_RECTS + HOME_RECTS:
		setup.place_in(safe)
		await tree.process_frame
		var card := (setup.get_node("Card") as Control).get_global_rect()
		check(safe.encloses(card), "%s: the card is inside: %s" % [safe, card])
		var font := (setup.control("level_world_class") as Button).get_theme_font("font")
		for level: Dictionary in SIX_LEVELS:
			var b := setup.control("level_" + String(level["id"])) as Button
			var rect := b.get_global_rect()
			check(card.encloses(rect), "%s: %s is on the card: %s" % [safe, level["name"], rect])
			var words := font.get_string_size(b.text, HORIZONTAL_ALIGNMENT_LEFT, -1.0, b.get_theme_font_size("font_size")).x
			check(rect.size.x >= words, "%s: %s fits its button: %f for %f" % [safe, level["name"], rect.size.x, words])
			check(rect.size.x >= 88.0 and rect.size.y >= 88.0, "%s: %s is a whole touch: %s" % [safe, level["name"], rect.size])
	setup.queue_free()
	await tree.process_frame


func test_home_has_four_buttons_along_the_bottom() -> void:
	var home := HomeScreen.new()
	home.auto_place = false
	tree.root.add_child(home)
	home.open()
	await tree.process_frame
	for safe in HOME_RECTS + SAFE_RECTS:
		home.place_in(safe)
		await tree.process_frame
		var row := (home.get_node("Buttons") as Control).get_global_rect()
		check(safe.encloses(row), "%s: the row is inside: %s" % [safe, row])
		var ids: Array[String] = ["play", "free_table", "watch", "settings"]
		for id in ids:
			var rect := home.button(id).get_global_rect()
			check(row.encloses(rect) and safe.encloses(rect), "%s: %s is inside the row: %s" % [safe, id, rect])
			check(rect.size.y >= 88.0, "%s: %s is a whole touch tall" % [safe, id])
			var font := home.button(id).get_theme_font("font")
			var words := font.get_string_size(home.button(id).text, HORIZONTAL_ALIGNMENT_LEFT, -1.0,
					home.button(id).get_theme_font_size("font_size")).x
			check(rect.size.x >= words + 32.0, "%s: %s's words fit" % [safe, id])
		for i in ids.size() - 1:
			check(home.button(ids[i]).get_global_rect().end.x <= home.button(ids[i + 1]).get_global_rect().position.x,
					"%s: %s left of %s" % [safe, ids[i], ids[i + 1]])
	home.queue_free()
	await tree.process_frame


func test_exhibition_summary_says_the_break() -> void:
	var card := ExhibitionSummary.new()
	_mark_log()
	card.open({"break_points": 147, "maximum": true, "why": "maximum"})
	check(card.visible, "open shows it")
	check_eq(card.texts(), PackedStringArray(["147", "Maximum break"]), "a maximum")
	check(_says(card, "147") and _says(card, "Maximum break"), "the card says so: %s" % [_shown(card)])
	card.open({"break_points": 64, "maximum": false, "why": "missed"})
	check_eq(card.texts(), PackedStringArray(["Break 64"]), "a break that ended at 64")
	check(_says(card, "Break 64") and not _says(card, "Maximum break"), "and only that: %s" % [_shown(card)])
	check_eq(_logged("app.error"), [], "nothing missing")
	card.open({})
	check_eq(card.texts(), PackedStringArray(["Break –"]), "a result with nothing in it")
	var logged := _logged("app.error")
	check(logged.size() == 1 and String(logged[0].get("message", "")).contains("result.break_points"),
			"named in the log: %s" % [logged])
	card.close()
	check(not card.visible, "close hides it")
	card.free()


func test_exhibition_summary_buttons_emit_their_signals() -> void:
	var card := ExhibitionSummary.new()
	var got: Array = []
	card.again_pressed.connect(func() -> void: got.append("again"))
	card.home_pressed.connect(func() -> void: got.append("home"))
	(card.control("again") as Button).pressed.emit()
	(card.control("home") as Button).pressed.emit()
	check_eq(got, ["again", "home"], "signals")
	check_eq([(card.control("again") as Button).text, (card.control("home") as Button).text], ["Again", "Home"], "words")
	check_eq(((card.control("again") as Button).get_theme_stylebox("normal") as StyleBoxFlat).bg_color, SettingsPanel.TEXT,
			"Again is the primary button")
	check(card.control("continue") == null, "no other buttons")
	card.free()


func test_the_summaries_name_the_opponent() -> void:
	var match_state := _example_match()
	var frame := FrameSummary.new()
	frame.show_summary(match_state["history"][0], match_state)
	check(_says(frame, "Computer"), "the frame summary's default: %s" % [_shown(frame)])
	frame.set_opponent_name("Beginner")
	check(_says(frame, "Beginner") and not _says(frame, "Computer"), "the frame summary: %s" % [_shown(frame)])
	frame.show_summary(match_state["history"][0], match_state, 1)
	check(_says(frame, "Beginner"), "kept over the next frame: %s" % [_shown(frame)])
	var short_match := MatchSummary.new()
	short_match.show_summary(_finished_match())
	check_eq(_count_shown(short_match, "Computer"), 1, "the match summary's default: %s" % [_shown(short_match)])
	short_match.set_opponent_name("The 147")
	check_eq(_count_shown(short_match, "The 147"), 1, "the match summary: %s" % [_shown(short_match)])
	check_eq(_count_shown(short_match, "Computer"), 0, "Computer is gone")
	var long_match := MatchSummary.new()
	long_match.set_opponent_name("World class")
	long_match.show_summary(_finished_match(11))
	check_eq(_count_shown(long_match, "World class"), 3, "named before the match: the totals and both columns: %s"
			% [_shown(long_match)])
	long_match.set_opponent_name("The 147")
	check_eq(_count_shown(long_match, "The 147"), 3, "named after it: all three")
	check_eq(_count_shown(long_match, "World class"), 0, "the old name is gone")
	frame.free()
	short_match.free()
	long_match.free()


func test_pause_resume_and_settings_emit_at_once() -> void:
	var pause := PauseMenu.new()
	var got := _pause_signals(pause)
	pause.open(true)
	check(pause.visible, "open")
	pause.button("resume").pressed.emit()
	pause.button("settings").pressed.emit()
	check_eq(got, ["resume", "settings"], "signals")
	check_eq(pause.confirming(), "", "nothing to confirm")
	check(pause.button("confirm") == null and pause.button("cancel") == null, "no Confirm or Cancel without a question")
	for id: String in ["resume", "concede_frame", "concede_match", "settings", "quit"]:
		check(pause.button(id) != null and _shows(pause, pause.button(id)), "%s is on the menu" % id)
	pause.close()
	check(not pause.visible, "close")
	pause.free()


func test_pause_asks_before_conceding_or_quitting() -> void:
	var asks := {
		"concede_frame": ["concede_frame_confirmed", "Computer wins the frame"],
		"concede_match": ["concede_match_confirmed", "Computer wins the match"],
		"quit": ["quit_confirmed", "The match will be discarded"],
	}
	for action: String in asks:
		var pause := PauseMenu.new()
		var got := _pause_signals(pause)
		pause.open(true)
		pause.button(action).pressed.emit()
		check_eq(got, [], "%s: nothing before Confirm" % action)
		check_eq(pause.confirming(), action, "%s is waiting" % action)
		check(_says(pause, asks[action][1]), "%s says what will happen: %s" % [action, _shown(pause)])
		check(not _shows(pause, pause.button("resume")) and not _shows(pause, pause.button(action)),
				"%s: the menu's buttons give way" % action)
		check(_shows(pause, pause.button("confirm")) and _shows(pause, pause.button("cancel")), "%s: Confirm and Cancel show" % action)
		check_eq([pause.button("confirm").text, pause.button("cancel").text], ["Confirm", "Cancel"], "their words")
		pause.button("confirm").pressed.emit()
		check_eq(got, [asks[action][0]], "%s: Confirm emits it, and only it" % action)
		check_eq(pause.confirming(), "", "%s: nothing waits after" % action)
		pause.free()


func test_pause_cancel_goes_back_to_the_menu() -> void:
	var pause := PauseMenu.new()
	var got := _pause_signals(pause)
	pause.open(true)
	for action: String in ["concede_frame", "concede_match", "quit"]:
		pause.button(action).pressed.emit()
		pause.button("cancel").pressed.emit()
		check_eq(pause.confirming(), "", "%s cancelled" % action)
		check(_shows(pause, pause.button("resume")), "%s: the menu is back" % action)
		check(pause.button("confirm") == null, "%s: no Confirm left to press" % action)
	check_eq(got, [], "cancelling emits nothing")
	pause.button("quit").pressed.emit()
	pause.open(true)
	check_eq(pause.confirming(), "", "opening again drops a question left open")
	pause.free()


func test_pause_concede_frame_follows_the_engine() -> void:
	var pause := PauseMenu.new()
	pause.open(false)
	check(pause.button("concede_frame").disabled, "disabled when the engine doesn't offer it")
	check(not pause.button("concede_match").disabled and not pause.button("quit").disabled, "the rest stay on")
	pause.open(true)
	check(not pause.button("concede_frame").disabled, "enabled when it does")
	pause.free()


func test_pause_tap_outside_the_card_resumes() -> void:
	var pause := PauseMenu.new()
	var got := _pause_signals(pause)
	pause.open(true)
	var backdrop: Control = pause.get_node("Backdrop")
	backdrop.gui_input.emit(_click(false))
	check_eq(got, [], "a finger that went down elsewhere and lifts here resumes nothing")
	backdrop.gui_input.emit(_click(true))
	check_eq(got, [], "nothing on the press")
	backdrop.gui_input.emit(_click(false))
	check_eq(got, ["resume"], "a tap resumes")
	pause.button("quit").pressed.emit()
	backdrop.gui_input.emit(_click(true))
	backdrop.gui_input.emit(_click(false))
	check_eq(got, ["resume", "resume"], "a tap outside a question resumes too, confirming nothing")
	check_eq(pause.confirming(), "", "and drops the question")
	pause.free()


func test_frame_summary_from_either_chair() -> void:
	var match_state := _example_match()
	var summary: Dictionary = match_state["history"][0]
	var card := FrameSummary.new()
	_mark_log()
	card.show_summary(summary, match_state, 0)
	check(card.visible, "showing opens it")
	check_eq(card.texts(), {"title": "You lost the frame", "reason": "On the black", "score_you": 41, "score_opponent": 63,
			"break_you": 16, "break_opponent": 31, "pots_you": "11 of 17", "pots_opponent": "16 of 20",
			"frames_you": 0, "frames_opponent": 1}, "as player 0")
	for words: String in ["You lost the frame", "On the black", "41", "63", "16", "31", "11 of 17", "16 of 20", "0", "1",
			"You", "Computer", "Score", "Highest break", "Pot success", "Frames"]:
		check(_says(card, words), "the card says %s: %s" % [words, _shown(card)])
	card.show_summary(summary, match_state, 1)
	check_eq(card.texts(), {"title": "You won the frame", "reason": "On the black", "score_you": 63, "score_opponent": 41,
			"break_you": 31, "break_opponent": 16, "pots_you": "16 of 20", "pots_opponent": "11 of 17",
			"frames_you": 1, "frames_opponent": 0}, "as player 1")
	for key: String in ["score_you", "break_you", "frames_you"]:
		check_eq(typeof(card.texts()[key]), TYPE_INT, "%s is an int" % key)
	check_eq(_logged("app.error"), [], "nothing missing from the example")
	card.close()
	check(not card.visible, "close")
	card.free()


func test_frame_summary_of_a_conceded_frame() -> void:
	var match_state := _example_match()
	var summary: Dictionary = (match_state["history"][0] as Dictionary).duplicate(true)
	summary["reason"] = "conceded"
	summary["winner"] = 0
	summary["stats"][0]["pots"] = 0
	summary["stats"][0]["pot_attempts"] = 0
	var card := FrameSummary.new()
	card.show_summary(summary, match_state)
	var texts := card.texts()
	check_eq([texts["title"], texts["reason"]], ["You won the frame", "Frame conceded"], "won by a concession, behind on points")
	check_eq([texts["score_you"], texts["score_opponent"]], [41, 63], "the scores as they stood")
	check_eq(texts["pots_you"], "0 of 0", "no attempts")
	card.free()


func test_reason_text_for_every_end_reason() -> void:
	for reason: String in END_REASONS:
		check_eq(FrameSummary.reason_text(reason), END_REASONS[reason], reason)
	check_eq(FrameSummary.reason_text("none"), "", "a frame still in play has no reason")
	check_eq(FrameSummary.reason_text("nonsense"), "", "an unknown reason")
	for reason: String in END_REASONS:
		if reason != "black" and reason != "respotted_black":
			check_eq(FrameSummary.reason_text(reason), RefereeLine.text_for({"kind": "frame_over", "detail": reason}),
					"%s as the referee says it" % reason)


func test_a_frame_summary_with_an_end_reason_it_has_no_words_for() -> void:
	HudKit.forget_reported()
	var match_state := _example_match()
	var summary: Dictionary = (match_state["history"][0] as Dictionary).duplicate(true)
	summary["reason"] = "abandoned"
	var card := FrameSummary.new()
	_mark_log()
	card.show_summary(summary, match_state)
	card.show_summary(summary, match_state)
	check_eq([card.texts()["title"], card.texts()["reason"]], ["You lost the frame", "Frame over"], "no empty line")
	check(_says(card, "Frame over"), "the card says so: %s" % [_shown(card)])
	var said := 0
	for data in _logged("app.error"):
		if String(data.get("message", "")).contains("abandoned"):
			said += 1
	check_eq(said, 1, "said once in the log")
	card.free()


func test_match_summary_won_and_lost() -> void:
	var match_state := _finished_match()
	var card := MatchSummary.new()
	_mark_log()
	card.show_summary(match_state, 0)
	check(card.visible, "showing opens it")
	check_eq(card.texts(), {"title": "You lost", "reason": "Frame conceded", "frames_you": 0, "frames_opponent": 2,
			"frames": ["41-63", "24-70"], "break_you": 16, "break_opponent": 45}, "as player 0")
	for words: String in ["You lost", "Frame 1", "Frame 2", "41", "63", "24", "70", "16", "45", "Highest break", "Frames"]:
		check(_says(card, words), "the card says %s: %s" % [words, _shown(card)])
	card.show_summary(match_state, 1)
	check_eq(card.texts(), {"title": "You won", "reason": "Frame conceded", "frames_you": 2, "frames_opponent": 0,
			"frames": ["63-41", "70-24"], "break_you": 45, "break_opponent": 16}, "as player 1")
	check(not _says(card, "You lost"), "the earlier match's words are gone")
	check_eq(_logged("app.error"), [], "nothing missing")
	card.free()


func test_match_summary_says_why_unless_it_ended_on_a_pot() -> void:
	var match_state := _finished_match()
	var card := MatchSummary.new()
	(match_state["history"][1] as Dictionary)["reason"] = "match_conceded"
	card.show_summary(match_state, 0)
	check_eq([card.texts()["title"], card.texts()["reason"]], ["You lost", "Match conceded"], "a conceded match")
	check(_says(card, "Match conceded"), "the card says so: %s" % [_shown(card)])
	(match_state["history"][1] as Dictionary)["reason"] = "black"
	card.show_summary(match_state, 0)
	check_eq(card.texts()["reason"], "", "on the black, the title is enough")
	check(not _says(card, "Match conceded") and not _says(card, "On the black"), "and the line goes: %s" % [_shown(card)])
	card.free()


func test_both_columns_of_a_long_match_have_the_names() -> void:
	var short_card := MatchSummary.new()
	short_card.show_summary(_finished_match(6))
	check_eq(_count_shown(short_card, "Computer"), 1, "one column: the totals' names serve: %s" % [_shown(short_card)])
	var long_card := MatchSummary.new()
	long_card.auto_place = false
	tree.root.add_child(long_card)
	long_card.show_summary(_finished_match(11))
	check_eq(_count_shown(long_card, "Computer"), 3, "the totals' and each column's: %s" % [_shown(long_card)])
	check_eq(_count_shown(long_card, "You"), 3, "and You over each")
	await tree.process_frame
	long_card.place_in(SAFE_RECTS[0])
	await tree.process_frame
	var columns := (long_card.find_child("Card", true, false) as Control).find_children("*", "GridContainer", true, false)
	var tops: Array[float] = []
	for column: Node in columns:
		var cells := (column as GridContainer).get_children()
		if cells.size() >= 3 and (cells[2] as Label).text == "Computer":
			tops.append((cells[2] as Label).get_global_rect().position.y)
	check_eq(tops.size(), 3, "three tables headed")
	if tops.size() == 3:
		check_near(tops[1], tops[2], 0.5, "the two columns' names on one line")
	# The phone's safe area is 838 units tall: the fullest card, with a reason line, keeps clear of its edges.
	var card := long_card.get_node("Card") as Control
	check(card.get_combined_minimum_size().y <= 838.0 - 32.0, "the card fits the phone: %s" % card.get_combined_minimum_size())
	short_card.free()
	long_card.queue_free()
	await tree.process_frame


func test_pause_offers_only_what_the_engine_offers() -> void:
	var pause := PauseMenu.new()
	pause.open(true, true, false)
	check(not pause.button("concede_frame").disabled and not pause.button("concede_match").disabled, "both concessions on")
	check(not _shows(pause, pause.button("claim_frame")), "no Claim frame when it isn't offered")
	pause.open(false, true, false)
	check(pause.button("concede_frame").disabled and not pause.button("concede_match").disabled, "a frame not offered is disabled")
	pause.open(true, false, false)
	check(not pause.button("concede_frame").disabled and pause.button("concede_match").disabled, "a match not offered is disabled")
	pause.open(true, true, true)
	check(_shows(pause, pause.button("claim_frame")), "Claim frame shows when offered")
	check(pause.button("resume").get_index() < pause.button("claim_frame").get_index(), "under Resume")
	pause.set_available(true, true, false)
	check(not _shows(pause, pause.button("claim_frame")), "and goes when no longer offered")
	pause.set_available(false, false, false)
	check(pause.button("concede_frame").disabled and pause.button("concede_match").disabled, "set_available disables both")
	check(not pause.button("quit").disabled and not pause.button("resume").disabled, "Quit and Resume stay on")
	pause.free()


func test_pause_a_question_about_something_no_longer_offered_goes_back_to_the_menu() -> void:
	for action: String in ["concede_frame", "concede_match", "claim_frame"]:
		var pause := PauseMenu.new()
		var got := _pause_signals(pause)
		pause.open(true, true, true)
		pause.button(action).pressed.emit()
		check_eq(pause.confirming(), action, "%s is asked" % action)
		pause.set_available(action != "concede_frame", action != "concede_match", action == "concede_match")
		check_eq(pause.confirming(), "", "%s withdrawn: back to the menu" % action)
		check(_shows(pause, pause.button("resume")) and pause.button("confirm") == null, "%s: the menu shows" % action)
		check_eq(got, [], "%s: nothing confirmed" % action)
		pause.free()
	var still := PauseMenu.new()
	still.open(true, true, true)
	still.button("quit").pressed.emit()
	still.set_available(false, false, false)
	check_eq(still.confirming(), "quit", "Quit is always offered: its question stays")
	still.free()


func test_pause_each_confirm_emits_its_signal_once() -> void:
	var asks := {
		"concede_frame": ["concede_frame_confirmed", "Computer wins the frame", "Concede frame"],
		"concede_match": ["concede_match_confirmed", "Computer wins the match", "Concede match"],
		"claim_frame": ["claim_frame_confirmed", "You win the frame", "Claim frame"],
		"quit": ["quit_confirmed", "The match will be discarded", "Quit"],
	}
	for action: String in asks:
		var pause := PauseMenu.new()
		var got := _pause_signals(pause)
		pause.open(true, true, true)
		pause.button(action).pressed.emit()
		check(_says(pause, asks[action][1]) and _says(pause, asks[action][2]), "%s asks: %s" % [action, _shown(pause)])
		var confirm := pause.button("confirm")
		confirm.pressed.emit()
		confirm.pressed.emit()
		check_eq(got, [asks[action][0]], "%s: one signal for two presses" % action)
		check_eq(pause.confirming(), "", "%s: back to the menu" % action)
		pause.free()


func test_summary_buttons_emit_their_signals() -> void:
	var frame := FrameSummary.new()
	var match_card := MatchSummary.new()
	var got: Array = []
	frame.continue_pressed.connect(func() -> void: got.append("continue"))
	match_card.home_pressed.connect(func() -> void: got.append("home"))
	frame.button("continue").pressed.emit()
	match_card.button("home").pressed.emit()
	check_eq(got, ["continue", "home"], "signals")
	check_eq([frame.button("continue").text, match_card.button("home").text], ["Continue", "Home"], "their words")
	check(frame.button("home") == null and match_card.button("continue") == null, "no other buttons")
	frame.free()
	match_card.free()


func test_each_screen_sits_inside_the_safe_area() -> void:
	var match_state := _example_match()
	var home := HomeScreen.new()
	var setup := MatchSetupScreen.new()
	var pause := PauseMenu.new()
	var question := PauseMenu.new()
	var frame := FrameSummary.new()
	var short_match := MatchSummary.new()
	var long_match := MatchSummary.new()
	var six := MatchSetupScreen.new()
	six.set_levels(SIX_LEVELS)
	var named := MatchSummary.new()
	named.set_opponent_name("World class")
	var exhibition := ExhibitionSummary.new()
	var screens := {"home": home, "setup": setup, "pause": pause, "pause question": question, "frame summary": frame,
			"match summary": short_match, "an eleven-frame match summary": long_match, "setup with six levels": six,
			"an eleven-frame match summary against World class": named, "exhibition summary": exhibition}
	for screen_name: String in screens:
		(screens[screen_name] as Control).set("auto_place", false)
		tree.root.add_child(screens[screen_name])
	home.open()
	setup.open()
	pause.open(true)
	question.open(true)
	question.button("quit").pressed.emit()
	frame.show_summary(match_state["history"][0], match_state)
	short_match.show_summary(_finished_match())
	long_match.show_summary(_finished_match(11))
	six.open()
	named.show_summary(_finished_match(11))
	exhibition.open({"break_points": 147, "maximum": true, "why": "maximum"})
	await tree.process_frame
	var menu_card_size := Vector2.ZERO
	for safe in SAFE_RECTS:
		for screen_name: String in screens:
			(screens[screen_name] as Control).call("place_in", safe)
		# The containers lay their rows out at the end of the frame.
		await tree.process_frame
		for screen_name: String in screens:
			var screen: Control = screens[screen_name]
			var where := "%s in %s" % [screen_name, safe]
			var card := screen.get_node_or_null("Card") as Control
			if card != null:
				check(safe.encloses(card.get_global_rect()), "%s: the card is inside: %s" % [where, card.get_global_rect()])
				check_near(card.get_global_rect().get_center().x, safe.get_center().x, 1.0, "%s: centred across" % where)
				check_near(card.get_global_rect().get_center().y, safe.get_center().y, 1.0, "%s: centred down" % where)
			var shown := _visible_controls(screen)
			check(shown.size() >= 3, "%s shows its controls: %d" % [where, shown.size()])
			for control in shown:
				var rect := control.get_global_rect()
				check(safe.encloses(rect), "%s: '%s' is inside: %s" % [where, control.get("text"), rect])
				check(card == null or card.get_global_rect().encloses(rect), "%s: '%s' is on the card" % [where, control.get("text")])
				check(control.get_theme_font_size("font_size") >= MIN_FONT_SIZE, "%s: '%s' is readable" % [where, control.get("text")])
				if control is Button:
					check(rect.size.y >= 88.0, "%s: %s is at least 88 units tall" % [where, control.get("text")])
					check_eq(control.focus_mode, Control.FOCUS_NONE, "%s: %s takes no focus" % [where, control.get("text")])
				for other in shown:
					check(other == control or not rect.grow(-1.0).intersects(other.get_global_rect().grow(-1.0)),
							"%s: '%s' and '%s' overlap" % [where, control.get("text"), other.get("text")])
		menu_card_size = (pause.get_node("Card") as Control).size
		check_eq((question.get_node("Card") as Control).size, menu_card_size, "%s: the card is one size, asking or not" % safe)
		# Confirm is not where any button that asks was, so a second tap on one confirms nothing.
		var confirm := question.button("confirm").get_global_rect()
		for id: String in ["concede_frame", "concede_match", "quit"]:
			check(not confirm.intersects(pause.button(id).get_global_rect()), "%s: Confirm is clear of %s" % [safe, id])
		var title := home.get_node("Title") as Control
		var buttons := home.get_node("Buttons") as Control
		check(title.get_global_rect().end.y < safe.position.y + safe.size.y * 0.25, "%s: the name is at the top" % safe)
		check(buttons.get_global_rect().position.y > safe.position.y + safe.size.y * 0.75, "%s: the buttons are at the bottom" % safe)
	for screen_name: String in screens:
		(screens[screen_name] as Control).queue_free()
	await tree.process_frame


func test_an_open_screen_places_itself_and_follows_the_viewport() -> void:
	var setup := MatchSetupScreen.new()
	tree.root.add_child(setup)
	setup.open()
	await tree.process_frame
	var card := setup.get_node("Card") as Control
	var safe := SettingsPanel.canvas_safe_rect(setup)
	check(safe.encloses(card.get_global_rect()), "opened inside the safe area: %s in %s" % [card.get_global_rect(), safe])
	var placed := card.position
	card.position = Vector2(3.0, 5.0)
	tree.root.size_changed.emit()
	check_eq(card.position, placed, "a viewport size change puts it back")
	setup.auto_place = false
	card.position = Vector2(3.0, 5.0)
	tree.root.size_changed.emit()
	check_eq(card.position, Vector2(3.0, 5.0), "left alone when auto_place is off")
	setup.queue_free()
	await tree.process_frame


func test_a_malformed_frame_summary_shows_what_is_there_and_is_logged_once() -> void:
	var card := FrameSummary.new()
	_mark_log()
	card.show_summary({}, {})
	check(card.visible, "it still shows")
	check_eq(card.texts(), {"title": "Frame over", "reason": "", "score_you": -1, "score_opponent": -1, "break_you": -1,
			"break_opponent": -1, "pots_you": "– of –", "pots_opponent": "– of –", "frames_you": -1, "frames_opponent": -1},
			"nothing to show")
	var logged := _logged("app.error")
	check_eq(logged.size(), 1, "one app.error")
	if logged.size() == 1:
		check_eq(logged[0].get("kind"), "message", "kind")
		for key: String in ["summary.winner", "summary.reason", "summary.scores.0", "summary.scores.1",
				"summary.stats.0.highest_break", "summary.stats.1.pots", "summary.stats.1.pot_attempts",
				"match.frames.0", "match.frames.1"]:
			check(String(logged[0].get("message", "")).contains(key), "it names %s: %s" % [key, logged[0].get("message")])
		check(String(logged[0].get("message", "")).begins_with("FrameSummary"), "and the screen")

	var match_state := _example_match()
	var summary: Dictionary = (match_state["history"][0] as Dictionary).duplicate(true)
	summary["scores"] = [41]
	summary["stats"][1].erase("highest_break")
	summary["stats"][0]["pots"] = "many"
	match_state["frames"] = "level"
	_mark_log()
	card.show_summary(summary, match_state)
	check_eq(card.texts(), {"title": "You lost the frame", "reason": "On the black", "score_you": 41, "score_opponent": -1,
			"break_you": 16, "break_opponent": -1, "pots_you": "– of 17", "pots_opponent": "16 of 20",
			"frames_you": -1, "frames_opponent": -1}, "what is there shows")
	check(_says(card, "41") and _says(card, "–"), "a dash stands where a number is missing")
	logged = _logged("app.error")
	check_eq(logged.size(), 1, "one app.error for the lot")
	if logged.size() == 1:
		var message := String(logged[0].get("message", ""))
		for key: String in ["summary.scores.1", "summary.stats.1.highest_break", "summary.stats.0.pots", "match.frames.0"]:
			check(message.contains(key), "it names %s: %s" % [key, message])
		check_eq(message.count("summary.scores.1"), 1, "each key once")
		check(not message.contains("summary.scores.0") and not message.contains("summary.winner"), "and only what is missing")
	card.free()


func test_a_malformed_match_summary_shows_what_is_there_and_is_logged_once() -> void:
	var card := MatchSummary.new()
	_mark_log()
	card.show_summary({})
	check(card.visible, "it still shows")
	check_eq(card.texts(), {"title": "Match over", "reason": "", "frames_you": -1, "frames_opponent": -1, "frames": [],
			"break_you": -1, "break_opponent": -1}, "nothing to show")
	var logged := _logged("app.error")
	check_eq(logged.size(), 1, "one app.error")
	if logged.size() == 1:
		for key: String in ["match.winner", "match.history", "match.frames.0", "match.frames.1"]:
			check(String(logged[0].get("message", "")).contains(key), "it names %s: %s" % [key, logged[0].get("message")])

	var match_state := _finished_match()
	match_state["history"][1].erase("scores")
	match_state["history"].append("a frame")
	match_state.erase("winner")
	_mark_log()
	card.show_summary(match_state)
	check_eq(card.texts(), {"title": "Match over", "reason": "", "frames_you": 0, "frames_opponent": 2,
			"frames": ["41-63", "–-–", "–-–"], "break_you": 16, "break_opponent": 45}, "what is there shows")
	logged = _logged("app.error")
	check_eq(logged.size(), 1, "one app.error for the lot")
	if logged.size() == 1:
		var message := String(logged[0].get("message", ""))
		for key: String in ["match.winner", "match.history.1.scores.0", "match.history.2"]:
			check(message.contains(key), "it names %s: %s" % [key, message])
		check(not message.contains("match.history.0"), "and only what is missing: %s" % message)
	card.free()


func _pause_signals(pause: PauseMenu) -> Array:
	var got: Array = []
	pause.resume_pressed.connect(func() -> void: got.append("resume"))
	pause.settings_pressed.connect(func() -> void: got.append("settings"))
	pause.concede_frame_confirmed.connect(func() -> void: got.append("concede_frame_confirmed"))
	pause.concede_match_confirmed.connect(func() -> void: got.append("concede_match_confirmed"))
	pause.claim_frame_confirmed.connect(func() -> void: got.append("claim_frame_confirmed"))
	pause.quit_confirmed.connect(func() -> void: got.append("quit_confirmed"))
	return got


func _click(pressed: bool) -> InputEventMouseButton:
	var click := InputEventMouseButton.new()
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = pressed
	click.position = Vector2(40.0, 40.0)
	return click


func _example_match() -> Dictionary:
	return JSON.parse_string(FileAccess.get_file_as_string(MATCH_PATH))


## The example match with a second frame, conceded by player 0, which ends it 0-2; padded with
## copies of that frame to `frames` frames.
func _finished_match(frames: int = 2) -> Dictionary:
	var match_state := _example_match()
	var second: Dictionary = (match_state["history"][0] as Dictionary).duplicate(true)
	second["number"] = 2
	second["breaker"] = 1
	second["reason"] = "conceded"
	second["scores"] = [24, 70]
	second["stats"][1]["highest_break"] = 45
	second["stats"][0]["highest_break"] = 12
	while (match_state["history"] as Array).size() < frames:
		match_state["history"].append(second.duplicate(true))
	match_state["frames"] = [0, frames]
	match_state["over"] = true
	match_state["winner"] = 1
	return match_state


## Whether a control would show with its screen open: every node from it up to the screen is
## set visible.
func _shows(screen: Control, control: Control) -> bool:
	var node: Node = control
	while node != null and node != screen:
		if node is CanvasItem and not (node as CanvasItem).visible:
			return false
		node = node.get_parent()
	return node == screen


## The buttons and the labels with words that show on a screen.
func _visible_controls(screen: Control, node: Node = null) -> Array[Control]:
	var found: Array[Control] = []
	for child in (screen if node == null else node).get_children():
		if child is CanvasItem and not (child as CanvasItem).visible:
			continue
		if child is Button or (child is Label and not (child as Label).text.is_empty()):
			found.append(child)
		else:
			found.append_array(_visible_controls(screen, child))
	return found


func _shown(screen: Control) -> Array[String]:
	var words: Array[String] = []
	for control in _visible_controls(screen):
		words.append(String(control.get("text")))
	return words


func _says(screen: Control, words: String) -> bool:
	return _shown(screen).has(words)


func _count_shown(screen: Control, words: String) -> int:
	return _shown(screen).count(words)


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


## The data of each `event` logged since _mark_log, read from the session file.
func _logged(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append((parsed as Dictionary).get("data", {}))
	return found
