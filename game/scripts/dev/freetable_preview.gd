extends Control
## Dev preview of the free table bar and the open settings panel on a dark background, laid out
## in the iPhone 17 Pro Max's landscape safe area whatever the window is:
##   godot --path game --resolution 2868x1320 res://scenes/dev/freetable_preview.tscn -- --mute

## The phone in landscape: 440 pt tall, safe insets 62 pt left and right (Dynamic Island) and
## 21 pt at the bottom (home indicator).
const PHONE_HEIGHT_PT := 440.0
const INSET_SIDE_PT := 62.0
const INSET_BOTTOM_PT := 21.0

var _bar := FreeTableBar.new()
var _panel := SettingsPanel.new()


func _ready() -> void:
	var background := ColorRect.new()
	background.color = Color(0.035, 0.04, 0.045)
	background.set_anchors_preset(PRESET_FULL_RECT)
	add_child(background)
	_bar.auto_place = false
	_bar.set_history(true, false, true)
	add_child(_bar)
	_panel.auto_place = false
	add_child(_panel)
	_panel.open()
	get_viewport().size_changed.connect(_place)
	_place()


func _place() -> void:
	var canvas := get_viewport_rect().size
	var unit := canvas.y / PHONE_HEIGHT_PT
	var safe := Rect2(INSET_SIDE_PT * unit, 0.0, canvas.x - 2.0 * INSET_SIDE_PT * unit,
			canvas.y - INSET_BOTTOM_PT * unit)
	_bar.place_in(safe)
	_panel.place_in(safe)
