class_name WatchButton
extends Button
## The view of the opponent's turn (spec 10.5): behind its cue, the table from above, or walking
## round it. One button that steps through the three; it shows the view a tap would go to. The
## table scene shows it while the opponent is at the table and asks CameraRig for the view chosen.

## A tap chose `view`, one of VIEWS.
signal view_chosen(view: String)

const Kit := preload("res://scripts/match/hud/hud_kit.gd")
const VIEWS: Array[String] = ["cue", "above", "round"]
## What the button says when a tap would go to that view.
const TEXT := {"cue": "Cue view", "above": "From above", "round": "Walk round"}
const SIZE := Vector2(190.0, 72.0)
const FONT_SIZE := 28
const EDGE_MARGIN := 16.0

var _view := "cue"


func _init() -> void:
	theme = SettingsPanel.make_theme()
	text = TEXT[_next()]
	custom_minimum_size = SIZE
	clip_text = true
	# Without focus a keyboard shortcut (Space shoots) can never press it.
	focus_mode = FOCUS_NONE
	add_theme_font_size_override("font_size", FONT_SIZE)
	add_theme_stylebox_override("normal", Kit.panel_style())
	add_theme_stylebox_override("hover", Kit.panel_style(0.78))
	var held := Kit.panel_style()
	held.bg_color = Color(0.3, 0.33, 0.32, 0.8)
	add_theme_stylebox_override("pressed", held)
	pressed.connect(_step)
	Kit.put(self, Vector2.ZERO, SIZE)


## Shows `view` as the one now on, without emitting.
func set_view(view: String) -> void:
	if not VIEWS.has(view):
		push_warning("WatchButton: no view \"%s\" (the views are %s); still showing \"%s\"" % [view, VIEWS, _view])
		return
	_view = view
	text = TEXT[_next()]


func view() -> String:
	return _view


## Puts the button in the top-right corner of `area` (canvas units, the safe area less whatever
## the scoreboard takes).
func place_in(area: Rect2) -> void:
	Kit.put(self, Vector2(area.end.x - EDGE_MARGIN - SIZE.x, area.position.y + EDGE_MARGIN).round(), SIZE)


func _next() -> String:
	return VIEWS[(VIEWS.find(_view) + 1) % VIEWS.size()]


func _step() -> void:
	set_view(_next())
	view_chosen.emit(_view)
