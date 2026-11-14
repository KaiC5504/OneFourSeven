class_name PotPopups
extends Node3D
## A potted ball's value floating up out of its pocket (spec 8.5, the shot cycle): white with a dark
## outline, facing whichever camera is on and drawn over the rails and fittings, rising RISE_M and
## fading over LIFE_S. Sized in metres, as the reference game's are, so it reads bigger from the
## pocket camera than from the TV view, but never under MIN_VIEW_SHARE of the view's height, which
## keeps it legible on the phone from the TV view.

const LIFE_S := 1.0
const RISE_M := 0.12
## Starts just above the cloth, where the ball went down.
const START_HEIGHT_M := 0.05
## The share of LIFE_S at full strength before the fade.
const HOLD := 0.35
const DIGIT_EM_M := 0.06
const MIN_VIEW_SHARE := 0.045
const FONT_SIZE := 128
const OUTLINE_SIZE := 30
const TEXT := Color(1.0, 1.0, 1.0)
const OUTLINE := Color(0.03, 0.04, 0.04, 0.92)

var _labels: Array[Label3D] = []
var _ages := PackedFloat32Array()
var _font: FontVariation


func _ready() -> void:
	set_process(not _labels.is_empty())


## Shows `value` rising out of the pocket whose drop centre is at core xy.
func pop(value: int, drop_centre: Vector2) -> void:
	var label := Label3D.new()
	label.name = "Points%d" % value
	label.text = str(value)
	label.font = _bold()
	label.font_size = FONT_SIZE
	label.outline_size = OUTLINE_SIZE
	label.modulate = TEXT
	label.outline_modulate = OUTLINE
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.no_depth_test = true
	label.shaded = false
	label.double_sided = true
	label.render_priority = 10
	label.outline_render_priority = 9
	label.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	label.layers = Stage.MOVING_LAYER
	label.position = TableSpace.core_to_godot(drop_centre.x, drop_centre.y, START_HEIGHT_M)
	label.set_meta("start", label.position)
	add_child(label)
	_labels.append(label)
	_ages.append(0.0)
	_size(label)
	if _labels.size() == 1:
		Perf.set_active(&"pot_popup", true)
		set_process(true)


func count() -> int:
	return _labels.size()


## Ends every popup at once (a new table, a retake).
func clear() -> void:
	for label in _labels:
		remove_child(label)
		label.queue_free()
	_labels.clear()
	_ages.resize(0)
	_stop()


func _process(delta: float) -> void:
	var i := 0
	while i < _labels.size():
		_ages[i] += delta
		var label := _labels[i]
		var f := minf(_ages[i] / LIFE_S, 1.0)
		if f >= 1.0:
			remove_child(label)
			label.queue_free()
			_labels.remove_at(i)
			_ages.remove_at(i)
			continue
		label.position = (label.get_meta("start") as Vector3) + Vector3.UP * RISE_M * ease(f, 0.5)
		var alpha := 1.0 - smoothstep(HOLD, 1.0, f)
		label.modulate = Color(TEXT, alpha)
		label.outline_modulate = Color(OUTLINE, OUTLINE.a * alpha)
		_size(label)
		i += 1
	if _labels.is_empty():
		_stop()


func _exit_tree() -> void:
	if not _labels.is_empty():
		Perf.set_active(&"pot_popup", false)


func _stop() -> void:
	Perf.set_active(&"pot_popup", false)
	set_process(false)


## DIGIT_EM_M, or more when the camera on is far enough away for that to be under MIN_VIEW_SHARE.
func _size(label: Label3D) -> void:
	var em := DIGIT_EM_M
	var camera := get_viewport().get_camera_3d() if is_inside_tree() else null
	if camera != null:
		var distance := camera.global_position.distance_to(label.global_position)
		em = maxf(em, MIN_VIEW_SHARE * 2.0 * distance * tan(deg_to_rad(camera.fov) / 2.0))
	label.pixel_size = em / FONT_SIZE


func _bold() -> FontVariation:
	if _font == null:
		_font = FontVariation.new()
		_font.base_font = ThemeDB.fallback_font
		_font.variation_embolden = 0.7
	return _font
