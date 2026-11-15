class_name CuePlacer
extends Control
## Ball in hand (spec 8.6): the cue ball is dragged within the D, kept to legal places, and a
## Confirm button commits it. This control is the button and the hint beside it, low in the safe
## area; the table scene feeds it the drag in core metres and moves the ball it answers with.
## After Confirm it shrinks to a small "Move" button while the cue ball is still in hand.

## The cue ball should show at `xy` (already legal) while the finger moves.
signal moved(xy: Vector2)
## The finger lifted with the cue ball at `xy`: commit it to the engine.
signal dropped(xy: Vector2)
signal confirmed
## "Move" was pressed after Confirm: back to placing.
signal move_requested

enum { OFF, PLACING, PLACED }

const Kit := preload("res://scripts/match/hud/hud_kit.gd")
const HINT := "Drag the cue ball in the D"
const CONFIRM_SIZE := Vector2(190.0, 88.0)
const MOVE_SIZE := Vector2(130.0, 88.0)
const FONT_SIZE := 28
const HINT_FONT_SIZE := 28
const GAP := 18
const MARGIN := 10.0
const MOVE_MARGIN := 6.0

var _state := OFF
var _nearest := Callable()
var _panel := PanelContainer.new()
var _style := Kit.panel_style(0.78)
var _hint := Label.new()
var _confirm: Button
var _move: Button
var _area := Rect2()


func _init() -> void:
	mouse_filter = MOUSE_FILTER_IGNORE
	set_anchors_preset(PRESET_FULL_RECT)
	theme = SettingsPanel.make_theme()
	_style.set_content_margin_all(MARGIN)
	_panel.name = "Panel"
	_panel.add_theme_stylebox_override("panel", _style)
	_panel.mouse_filter = MOUSE_FILTER_IGNORE
	add_child(_panel)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", GAP)
	row.mouse_filter = MOUSE_FILTER_IGNORE
	_panel.add_child(row)
	_hint.name = "Hint"
	_hint.text = HINT
	_hint.add_theme_font_size_override("font_size", HINT_FONT_SIZE)
	_hint.mouse_filter = MOUSE_FILTER_IGNORE
	row.add_child(_hint)
	_confirm = Kit.button("Confirm", CONFIRM_SIZE, FONT_SIZE)
	_confirm.name = "Confirm"
	_confirm.pressed.connect(_on_confirm)
	row.add_child(_confirm)
	_move = Kit.button("Move", MOVE_SIZE, FONT_SIZE)
	_move.name = "Move"
	_move.tooltip_text = "Move the cue ball"
	_move.pressed.connect(_on_move)
	row.add_child(_move)
	_show()


func _ready() -> void:
	# Before the control is in the tree its panel has no theme, so it was measured without its
	# margins and separation.
	_fit()


## Starts placing. `nearest` maps a wanted place (core metres) to the nearest legal one
## (MatchController.nearest_cue_placement).
func begin(nearest: Callable) -> void:
	_nearest = nearest
	_state = PLACING
	_show()


## The cue ball is no longer in hand (the stroke was played, the match left): hides everything.
func end() -> void:
	_nearest = Callable()
	_state = OFF
	_show()


## True while the cue ball follows a drag (before Confirm).
func is_placing() -> bool:
	return _state == PLACING


## A drag on the table at `xy` (core metres): emits moved with the nearest legal place. Nothing
## when not placing.
func drag(xy: Vector2) -> void:
	if _state == PLACING:
		moved.emit(_legal(xy))


## The drag ended at `xy`: emits dropped with the nearest legal place.
func drop(xy: Vector2) -> void:
	if _state == PLACING:
		dropped.emit(_legal(xy))


## "confirm" or "move"; null when that button is not showing.
func button(id: String) -> Button:
	match id:
		"confirm":
			return _confirm if _state == PLACING else null
		"move":
			return _move if _state == PLACED else null
	return null


## Puts the control centred along the bottom of `area` (canvas units); Move alone at its bottom
## right.
func place_in(area: Rect2) -> void:
	_area = area
	_fit()


func covered_rect() -> Rect2:
	return Rect2(_panel.position, _panel.size) if _state != OFF else Rect2()


func _legal(xy: Vector2) -> Vector2:
	if not _nearest.is_valid():
		Kit.problem("CuePlacer: nothing to ask for the nearest legal place; the cue ball goes where it is dragged")
		return xy
	var place: Variant = _nearest.call(xy)
	if place is Vector2:
		return place
	Kit.problem("CuePlacer: the nearest legal place came back as %s, not a Vector2" % type_string(typeof(place)))
	return xy


func _on_confirm() -> void:
	if _state != PLACING:
		return
	_state = PLACED
	_show()
	confirmed.emit()


func _on_move() -> void:
	if _state != PLACED:
		return
	_state = PLACING
	_show()
	move_requested.emit()


func _show() -> void:
	_hint.visible = _state == PLACING
	_confirm.visible = _state == PLACING
	_move.visible = _state == PLACED
	_panel.visible = _state != OFF
	# The hint needs room from the panel's edge; Move alone gets a slim frame, to stay small.
	_style.set_content_margin_all(MARGIN if _state == PLACING else MOVE_MARGIN)
	if _state == PLACING:
		_style.content_margin_left = MARGIN + GAP
	_fit()


func _fit() -> void:
	var extent := _panel.get_combined_minimum_size()
	# Move alone goes to the area's right end: centred it would sit on the butt of the cue.
	var x := _area.end.x - extent.x if _state == PLACED else _area.get_center().x - extent.x / 2.0
	Kit.put(_panel, Vector2(x, _area.end.y - extent.y).round(), extent)
