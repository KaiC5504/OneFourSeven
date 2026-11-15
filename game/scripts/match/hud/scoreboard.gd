class_name Scoreboard
extends Control
## The match's minimal scoreboard (spec 11.1): both scores, frames, the current break, points
## remaining, "snookers required" when relevant, the ball on, and whose turn it is; with it, the
## pause button. Sits at the top of the safe area and takes no touches except on that button.
##
## One strip like a broadcast's: the names at its ends, the scores inside them and the frames won
## in brackets round "best of N" in the middle, which stands at the middle of the screen; a dot
## marks whoever is at the table and their name is the brighter one. Under that, smaller: the
## break below the striker's name, then the ball on as a disc in its colour with its word, the
## points remaining and, when a player requires penalty points, whose snookers they are. Once the
## frame is over only the scores and frames stay. The pause button is its own tile at the strip's
## left end. Everything is read from the frame view and the match; no rule is worked out here.

signal pause_pressed

const Kit := preload("res://scripts/match/hud/hud_kit.gd")
const YOU := "You"
const OPPONENT := "Computer"
const YOU_NEED_SNOOKERS := "You need snookers"
const OPPONENT_NEEDS_SNOOKERS := OPPONENT + " needs snookers"
const FREE_BALL := "Free ball"
const HEIGHT := 88.0
const EDGE_MARGIN := 16.0
const PAUSE_GAP := 8.0
# iOS can report the safe area late and changes it on rotation without a resize.
const SAFE_AREA_POLL_SECONDS := 1.0
const FRAME := "Scoreboard: the frame view"
const MATCH := "Scoreboard: the match"
const ON := "Scoreboard: the ball on"

## When true the scoreboard keeps itself at the safe area's top; set false to call place_in.
var auto_place := true

var _strip := Strip.new()
var _pause: Button
var _area := Rect2()


## The strip itself, drawn rather than built from labels so the sizes share a baseline.
class Strip extends Control:
	const PAD := 24.0
	const MARKER_SLOT := 22.0
	const MARKER_RADIUS := 6.0
	const NAME_SIZE := 28
	const SCORE_SIZE := 38
	const FRAMES_SIZE := 28
	const BEST_SIZE := 28
	const DETAIL_SIZE := 28
	const NAME_GAP := 20.0
	const SCORE_GAP := 24.0
	const FRAMES_GAP := 14.0
	const DETAIL_GAP := 30.0
	const WORD_GAP := 8.0
	const TOP_BASELINE := 42.0
	const DETAIL_BASELINE := 74.0
	const DISC_RADIUS := 10.0
	const WARNING := Color(0.96, 0.72, 0.3)
	const RIM := Color(1.0, 1.0, 1.0, 0.45)
	const GLINT := Color(1.0, 1.0, 1.0, 0.5)
	const GLINT_AT := Vector2(-3.5, -3.5)
	const GLINT_RADIUS := 2.2
	const FREE_DASHES := 6
	## The balls' colours, lifted so each reads at this size on a dark strip.
	const BALLS := {
		"red": Color(0.82, 0.1, 0.1),
		"yellow": Color(0.95, 0.8, 0.14),
		"green": Color(0.08, 0.56, 0.28),
		"brown": Color(0.54, 0.3, 0.13),
		"blue": Color(0.15, 0.36, 0.88),
		"pink": Color(0.97, 0.6, 0.71),
		"black": Color(0.02, 0.02, 0.02),
	}
	const COLOURS: Array[String] = ["yellow", "green", "brown", "blue", "pink", "black"]

	var said := {}
	## "red", a colour's name, "any" (a colour of choice), "free" (a free ball) or "".
	var on_ball := ""

	var _panel := Kit.panel_style()
	var _bold: FontVariation

	## Where the frames block is centred, from the strip's left edge. The strip is as wide as its
	## longest contents on each side of that, so it keeps one width while nobody needs snookers,
	## and the shorter name gives the shorter side: with the pause button there, the two sides come
	## out about level. Whose snookers widens it while they show, rather than leaving that room
	## empty all match.
	func centre_x() -> float:
		var font := get_theme_default_font()
		return _side(font, YOU) + _spare(font) / 2.0

	func design_width() -> float:
		var font := get_theme_default_font()
		return ceilf(_side(font, YOU) + _side(font, OPPONENT) + _spare(font))

	# From the strip's edge to the middle of the frames block, on the side of `player`.
	func _side(font: Font, player: String) -> float:
		var name_and_score := _width(font, player, NAME_SIZE) + NAME_GAP + _width(_scores(), "147", SCORE_SIZE)
		return PAD + MARKER_SLOT + name_and_score + SCORE_GAP + _middle_half(font)

	# What the fullest lower line needs beyond the upper line's width. A free ball starts a turn, so
	# it never shows with a break.
	func _spare(font: Font) -> float:
		var colour := 0.0
		for ball: String in BALLS:
			colour = maxf(colour, _width(font, ball.capitalize(), DETAIL_SIZE))
		colour = maxf(colour, _width(font, "Colour", DETAIL_SIZE))
		var first := maxf(_width(font, "Break 147", DETAIL_SIZE) + DETAIL_GAP + colour, _width(font, FREE_BALL, DETAIL_SIZE))
		var detail := 2.0 * PAD + MARKER_SLOT + 2.0 * DISC_RADIUS + WORD_GAP + first + DETAIL_GAP \
				+ _width(font, "Remaining 147", DETAIL_SIZE)
		var required := String(said.get("required", ""))
		if not required.is_empty():
			detail += DETAIL_GAP + _width(font, required, DETAIL_SIZE)
		return maxf(detail - _side(font, YOU) - _side(font, OPPONENT), 0.0)

	func _draw() -> void:
		draw_style_box(_panel, Rect2(Vector2.ZERO, size))
		if said.is_empty():
			return
		var font := get_theme_default_font()
		var text_colour := SettingsPanel.TEXT
		var muted := SettingsPanel.MUTED
		var centre := centre_x()
		var turn: String = said["turn"]
		var left := PAD + MARKER_SLOT
		var right := size.x - PAD - MARKER_SLOT
		var marker_y := TOP_BASELINE - NAME_SIZE * 0.36
		_write(font, YOU, left, TOP_BASELINE, NAME_SIZE, text_colour if turn == "you" else muted)
		_write(font, OPPONENT, right - _width(font, OPPONENT, NAME_SIZE), TOP_BASELINE, NAME_SIZE,
				text_colour if turn == "opponent" else muted)
		if turn == "you":
			draw_circle(Vector2(PAD + MARKER_RADIUS, marker_y), MARKER_RADIUS, SettingsPanel.ACCENT, true, -1.0, true)
		elif turn == "opponent":
			draw_circle(Vector2(size.x - PAD - MARKER_RADIUS, marker_y), MARKER_RADIUS, SettingsPanel.ACCENT, true, -1.0, true)

		var half := _middle_half(font)
		var scores := _scores()
		var you := _number(said["you"])
		_write(scores, you, centre - half - SCORE_GAP - _width(scores, you, SCORE_SIZE), TOP_BASELINE, SCORE_SIZE, text_colour)
		_write(scores, _number(said["opponent"]), centre + half + SCORE_GAP, TOP_BASELINE, SCORE_SIZE, text_colour)
		var best_half := _width(font, "best of 11", BEST_SIZE) / 2.0
		if int(said["best_of"]) > 0:
			var best := "best of %d" % int(said["best_of"])
			_write(font, best, centre - _width(font, best, BEST_SIZE) / 2.0, TOP_BASELINE - 2.0, BEST_SIZE, muted)
		if int(said["frames_you"]) >= 0:
			var won := "(%d)" % int(said["frames_you"])
			_write(font, won, centre - best_half - FRAMES_GAP - _width(font, won, FRAMES_SIZE), TOP_BASELINE, FRAMES_SIZE, text_colour)
		if int(said["frames_opponent"]) >= 0:
			_write(font, "(%d)" % int(said["frames_opponent"]), centre + best_half + FRAMES_GAP, TOP_BASELINE, FRAMES_SIZE, text_colour)

		# The break stands under its maker's name; the rest is centred unless the break is in its way.
		var low := PAD
		var high := size.x - PAD
		if int(said["break"]) > 0 and not turn.is_empty():
			var words := "Break "
			var points := str(int(said["break"]))
			var width := _width(font, words, DETAIL_SIZE) + _width(font, points, DETAIL_SIZE)
			var at := left if turn == "you" else right - width
			_write(font, words, at, DETAIL_BASELINE, DETAIL_SIZE, muted)
			_write(font, points, at + _width(font, words, DETAIL_SIZE), DETAIL_BASELINE, DETAIL_SIZE, text_colour)
			if turn == "you":
				low = at + width + DETAIL_GAP
			else:
				high = at - DETAIL_GAP
		# Each [words, colour]; a disc is drawn before the first when there is a ball on.
		var parts: Array[Array] = []
		if not String(said["on"]).is_empty():
			parts.append([String(said["on"]), text_colour])
		if int(said["remaining"]) >= 0:
			parts.append(["Remaining ", muted])
			parts.append([str(int(said["remaining"])), text_colour])
		if not String(said["required"]).is_empty():
			parts.append([String(said["required"]), WARNING])
		var disc := 2.0 * DISC_RADIUS + WORD_GAP if not on_ball.is_empty() else 0.0
		var total := disc
		for i in parts.size():
			total += _width(font, parts[i][0], DETAIL_SIZE) + (DETAIL_GAP if _starts_group(parts, i) else 0.0)
		var x := clampf(centre - total / 2.0, low, maxf(high - total, low))
		if disc > 0.0:
			_draw_ball(Vector2(x + DISC_RADIUS, DETAIL_BASELINE - DETAIL_SIZE * 0.36))
			x += disc
		for i in parts.size():
			if _starts_group(parts, i):
				x += DETAIL_GAP
			_write(font, parts[i][0], x, DETAIL_BASELINE, DETAIL_SIZE, parts[i][1])
			x += _width(font, parts[i][0], DETAIL_SIZE)

	# "Remaining " and its number are one group; every other part after the first starts one.
	static func _starts_group(parts: Array[Array], i: int) -> bool:
		return i > 0 and not String(parts[i - 1][0]).ends_with(" ")

	func _draw_ball(at: Vector2) -> void:
		match on_ball:
			"free":
				# No ball's colour, and broken so it isn't taken for the black: any ball may be declared.
				for i in FREE_DASHES:
					var from := TAU * (i - 0.3) / FREE_DASHES
					draw_arc(at, DISC_RADIUS - 1.5, from, from + 0.6 * TAU / FREE_DASHES, 8, SettingsPanel.TEXT, 2.5, true)
				return
			"any":
				for i in COLOURS.size():
					var wedge := PackedVector2Array([at])
					for step in 9:
						wedge.append(at + Vector2.from_angle(-PI / 2.0 + TAU * (i + step / 8.0) / COLOURS.size()) * DISC_RADIUS)
					draw_colored_polygon(wedge, BALLS[COLOURS[i]])
			_:
				draw_circle(at, DISC_RADIUS, BALLS.get(on_ball, SettingsPanel.MUTED), true, -1.0, true)
				# A glint, so the black reads as a ball and not as a hole in the strip.
				draw_circle(at + GLINT_AT, GLINT_RADIUS, GLINT, true, -1.0, true)
		draw_arc(at, DISC_RADIUS, 0.0, TAU, 32, RIM, 1.5, true)

	func _middle_half(font: Font) -> float:
		return _width(font, "best of 11", BEST_SIZE) / 2.0 + FRAMES_GAP + _width(font, "(6)", FRAMES_SIZE)

	func _scores() -> Font:
		if _bold == null:
			_bold = FontVariation.new()
			_bold.base_font = ThemeDB.fallback_font
			_bold.variation_embolden = 0.5
		return _bold

	func _write(font: Font, words: String, x: float, baseline: float, font_size: int, colour: Color) -> void:
		if not words.is_empty():
			draw_string(font, Vector2(x, baseline), words, HORIZONTAL_ALIGNMENT_LEFT, -1.0, font_size, colour)

	static func _width(font: Font, words: String, font_size: int) -> float:
		return font.get_string_size(words, HORIZONTAL_ALIGNMENT_LEFT, -1.0, font_size).x

	static func _number(value: int) -> String:
		return str(value) if value >= 0 else ""


## The pause sign: two bars.
class PauseSign extends Control:
	const BAR := Vector2(8.0, 28.0)
	const GAP := 10.0

	func _draw() -> void:
		var centre := size / 2.0
		for side: float in [-1.0, 1.0]:
			var middle := centre.x + side * (GAP + BAR.x) / 2.0
			draw_rect(Rect2(Vector2(middle - BAR.x / 2.0, centre.y - BAR.y / 2.0), BAR), SettingsPanel.TEXT)


func _init() -> void:
	mouse_filter = MOUSE_FILTER_IGNORE
	set_anchors_preset(PRESET_FULL_RECT)
	theme = SettingsPanel.make_theme()
	_strip.name = "Strip"
	_strip.mouse_filter = MOUSE_FILTER_IGNORE
	add_child(_strip)
	_pause = Kit.button("", Vector2(HEIGHT, HEIGHT), 28)
	_pause.name = "Pause"
	_pause.tooltip_text = "Pause"
	_pause.add_theme_stylebox_override("normal", Kit.panel_style())
	_pause.add_theme_stylebox_override("hover", Kit.panel_style(0.78))
	var held := Kit.panel_style()
	held.bg_color = Color(0.3, 0.33, 0.32, 0.8)
	_pause.add_theme_stylebox_override("pressed", held)
	_pause.pressed.connect(pause_pressed.emit)
	var bars := PauseSign.new()
	bars.set_anchors_preset(PRESET_FULL_RECT)
	bars.mouse_filter = MOUSE_FILTER_IGNORE
	_pause.add_child(bars)
	add_child(_pause)
	Kit.put(_pause, Vector2.ZERO, Vector2(HEIGHT, HEIGHT))
	Kit.put(_strip, Vector2(HEIGHT + PAUSE_GAP, 0.0), Vector2(_strip.design_width(), HEIGHT))


func _ready() -> void:
	get_viewport().size_changed.connect(_auto_place)
	var poll := Timer.new()
	poll.wait_time = SAFE_AREA_POLL_SECONDS
	poll.timeout.connect(_auto_place)
	add_child(poll)
	poll.start()
	_auto_place()


## frame: the frame view; match_state: the match (serialize.hpp). person: which player (0 or 1)
## is "You".
func show_frame(frame: Dictionary, match_state: Dictionary, person: int = 0) -> void:
	person = clampi(person, 0, 1)
	var other := 1 - person
	# A frame view with no "over" is one in play.
	var over: bool = frame.get("over") == true
	var view := Kit.dictionary(frame, "view", FRAME)
	var required := ""
	var remaining := -1
	if not view.is_empty():
		remaining = Kit.integer(view, "points_remaining", FRAME)
		if Kit.integer_at(view, "required", person, FRAME, 0) > 0:
			required = YOU_NEED_SNOOKERS
		elif Kit.integer_at(view, "required", other, FRAME, 0) > 0:
			required = OPPONENT_NEEDS_SNOOKERS
	var config := Kit.dictionary(match_state, "config", MATCH)
	var striker := Kit.integer(frame, "striker", FRAME)
	var said := {
		"you": Kit.integer_at(frame, "scores", person, FRAME),
		"opponent": Kit.integer_at(frame, "scores", other, FRAME),
		"frames_you": Kit.integer_at(match_state, "frames", person, MATCH),
		"frames_opponent": Kit.integer_at(match_state, "frames", other, MATCH),
		"best_of": Kit.integer(config, "best_of", MATCH) if not config.is_empty() else -1,
		"break": maxi(Kit.integer(frame, "break_points", FRAME, 0), 0),
		"remaining": remaining,
		"required": required,
		"on": on_text(frame),
		"turn": "you" if striker == person else ("opponent" if striker == other else ""),
	}
	var on_ball := _on_ball(frame)
	if over:
		said.merge({"break": 0, "remaining": -1, "required": "", "on": "", "turn": ""}, true)
		on_ball = ""
	if said == _strip.said and on_ball == _strip.on_ball:
		return
	var widened: bool = said["required"] != _strip.said.get("required", "")
	_strip.said = said
	_strip.on_ball = on_ball
	_strip.queue_redraw()
	if widened:
		if _area.has_area():
			place_in(_area)
		else:
			Kit.put(_strip, _strip.position, Vector2(_strip.design_width(), HEIGHT))


## Puts the scoreboard along the top of `area` (canvas units), the frames in the strip's middle
## at the area's centre and the pause button at the strip's left end, writing only what changed.
func place_in(area: Rect2) -> void:
	_area = area
	var x := maxf(area.get_center().x - _strip.centre_x(), area.position.x + EDGE_MARGIN + HEIGHT + PAUSE_GAP)
	var top := area.position.y + EDGE_MARGIN
	Kit.put(_strip, Vector2(x, top).round(), Vector2(_strip.design_width(), HEIGHT))
	Kit.put(_pause, Vector2(_strip.position.x - PAUSE_GAP - HEIGHT, _strip.position.y), Vector2(HEIGHT, HEIGHT))


## The canvas rect it covers, for the camera to keep the table clear of.
func covered_rect() -> Rect2:
	return Rect2(_pause.position, _pause.size).merge(Rect2(_strip.position, _strip.size))


## The pause button, for tests and the harness.
func pause_button() -> Button:
	return _pause


## What the scoreboard says, for tests: {you, opponent, frames_you, frames_opponent, best_of,
## break, remaining, required ("You need snookers", "Computer needs snookers" or "" when nobody
## requires penalty points), on (the ball on in words: "Red", "Colour", "Blue", "Free ball"), turn
## ("you" | "opponent")}. The numbers are ints: break is 0 when none shows, and a number the frame
## or the match did not give is -1 and is left blank; turn is "" when the frame names no striker.
## Once the frame is over, break is 0, remaining -1, and required, on and turn are "". {} before
## any frame.
func texts() -> Dictionary:
	return _strip.said.duplicate()


## The ball on in words for a frame view: "Red", "Colour" (a colour of choice), the colour's name
## capitalised, or "Free ball" when one was called.
static func on_text(frame: Dictionary) -> String:
	var ball := _on_ball(frame)
	match ball:
		"free":
			return FREE_BALL
		"any":
			return "Colour"
	return ball.capitalize()


static func _on_ball(frame: Dictionary) -> String:
	if Kit.flag(frame, "free_ball", FRAME):
		return "free"
	var on := Kit.dictionary(frame, "on", FRAME)
	if on.is_empty():
		return ""
	var kind := Kit.text(on, "kind", ON)
	match kind:
		"red":
			return "red"
		"any_colour":
			return "any"
		"colour":
			return Kit.text(on, "colour", ON)
		"":
			return ""
	Kit.problem("Scoreboard: no words for a ball on of kind \"%s\"" % kind)
	return ""


func _auto_place() -> void:
	if auto_place and is_inside_tree():
		place_in(SettingsPanel.canvas_safe_rect(self))
