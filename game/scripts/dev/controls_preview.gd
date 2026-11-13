extends Control
## Dev preview of the spin widget, elevation control, stroke area and power bar in their states,
## for screenshots: res://scenes/dev/controls_preview.tscn.

const BACKGROUND := Color(0.035, 0.04, 0.045)
const CLOTH := Color(0.07, 0.3, 0.13)
const CAPTION_COLOUR := Color(0.7, 0.74, 0.72)
const CAPTION_SIZE := 22

var _fake_t := 0.0


func _ready() -> void:
	var background := ColorRect.new()
	background.color = BACKGROUND
	background.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	# A strip of cloth colour, since the controls sit over the table in play.
	var cloth := ColorRect.new()
	cloth.color = CLOTH
	cloth.position = Vector2(0.0, 300.0)
	cloth.size = Vector2(1912.0, 580.0)
	add_child(cloth)

	var collapsed := SpinWidget.new()
	collapsed.position = Vector2(40.0, 690.0)
	collapsed.size = Vector2(SpinWidget.COLLAPSED_SIZE, SpinWidget.COLLAPSED_SIZE)
	var collapsed_setup := ShotSetup.new()
	collapsed_setup.set_tip(0.15, -0.35)
	_place(collapsed, collapsed_setup, "collapsed")

	var expanded := SpinWidget.new()
	expanded.position = Vector2(250.0, 360.0)
	var spin_setup := ShotSetup.new()
	spin_setup.set_tip(-0.2, -0.4)
	_place(expanded, spin_setup, "expanded")
	expanded.expand()

	var elevation := ElevationControl.new()
	elevation.position = Vector2(700.0, 420.0)
	elevation.size = Vector2(120.0, 420.0)
	var elevation_setup := ShotSetup.new()
	elevation_setup.set_elevation(deg_to_rad(12.0))
	_place(elevation, elevation_setup, "elevation")

	var forced := ElevationControl.new()
	forced.position = Vector2(860.0, 420.0)
	forced.size = Vector2(120.0, 420.0)
	var forced_setup := ShotSetup.new()
	forced_setup.set_elevation(deg_to_rad(5.0))
	forced_setup.set_elevation_min(deg_to_rad(34.0))
	_place(forced, forced_setup, "forced")

	var stroke := StrokeControl.new()
	stroke.position = Vector2(1430.0, 40.0)
	stroke.size = Vector2(200.0, 800.0)
	stroke.clock = func() -> float: return _fake_t
	_place(stroke, ShotSetup.new(), "stroke, mid-pull")
	_pull(stroke)

	var power := PowerBar.new()
	power.position = Vector2(1690.0, 40.0)
	power.size = Vector2(160.0, 800.0)
	var power_setup := ShotSetup.new()
	_place(power, power_setup, "power bar")
	power.set_power(0.42)


func _place(control: Control, setup: ShotSetup, caption: String) -> void:
	add_child(control)
	control.call("bind", setup)
	var label := Label.new()
	label.text = caption
	label.add_theme_color_override("font_color", CAPTION_COLOUR)
	label.add_theme_font_size_override("font_size", CAPTION_SIZE)
	label.position = control.position + Vector2(0.0, -36.0)
	add_child(label)


func _pull(stroke: StrokeControl) -> void:
	var press := InputEventScreenTouch.new()
	press.pressed = true
	press.position = Vector2(100.0, 260.0)
	stroke._gui_input(press)
	for i in range(1, 21):
		_fake_t = i / 60.0
		var drag := InputEventScreenDrag.new()
		drag.position = Vector2(100.0 + i * 0.6, 260.0 + i * 12.0)
		stroke._gui_input(drag)
